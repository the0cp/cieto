#ifndef _WIN32

#include <errno.h>
#include <stdint.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "object.h"
#include "value.h"
#include "vm.h"

#include "process.h"

extern char** environ;

typedef enum{
    READ_OK,
    READ_EOF,
    READ_ERROR,
    READ_NOMEM
}ReadRes;

typedef struct{
    char** items;
    size_t count;
}EnvList;

static void writeChildErr(int fd, const char* text, size_t len){
    while(len > 0){
        ssize_t wrote = write(fd, text, len);
        if(wrote > 0){
            text += wrote;
            len -= (size_t)wrote;
        }else if(wrote < 0 && errno == EINTR){
            continue;
        }else{
            break;
        }
    }
}

static void envListFree(EnvList* list){
    for(size_t i = 0; i < list->count; i++){
        free(list->items[i]);
    }
    free(list->items);
}

static bool envEntryEq(const char* entry, ObjectString* key){
    const char* equal = strchr(entry, '=');
    size_t len = equal != NULL ? (size_t)(equal - entry) : strlen(entry);
    return len == key->length && memcmp(entry, key->chars, len) == 0;
}

static bool envHasKey(ObjectMap* env, const char* entry){
    for(int i = 0; i < env->table.capacity; i++){
        Entry* item = &env->table.entries[i];
        if(!IS_EMPTY(item->key) && envEntryEq(entry, AS_STRING(item->key))){
            return true;
        }
    }
    return false;
}

static bool appendEnv(EnvList* list, ObjectString* key, ObjectString* value){
    if(key->length > SIZE_MAX - value->length - 2){
        return false;
    }

    size_t len = key->length + value->length + 1;
    char* entry = (char*)malloc(len + 1);
    if(entry == NULL){
        return false;
    }

    memcpy(entry, key->chars, key->length);
    entry[key->length] = '=';
    memcpy(entry + key->length + 1, value->chars, value->length);
    entry[len] = '\0';
    list->items[list->count++] = entry;
    return true;
}

static bool buildEnv(VM* vm, ObjectMap* env, EnvList* list){
    list->items = NULL;
    list->count = 0;
    if(env == NULL || env->table.count == 0){
        return true;
    }

    size_t inherited = 0;
    while(environ[inherited] != NULL){
        inherited++;
    }

    if(inherited > SIZE_MAX - (size_t)env->table.count - 1){
        runtimeError(vm, "process.run environment is too large.\n");
        return false;
    }

    list->items = (char**)calloc(
        inherited + (size_t)env->table.count + 1,
        sizeof(char*)
    );
    if(list->items == NULL){
        runtimeError(vm, "process.run could not allocate the environment.\n");
        return false;
    }

    for(size_t i = 0; i < inherited; i++){
        if(envHasKey(env, environ[i])){
            continue;
        }

        char* copy = strdup(environ[i]);
        if(copy == NULL){
            envListFree(list);
            runtimeError(vm, "process.run could not allocate the environment.\n");
            return false;
        }
        list->items[list->count++] = copy;
    }

    for(int i = 0; i < env->table.capacity; i++){
        Entry* entry = &env->table.entries[i];
        if(IS_EMPTY(entry->key)){
            continue;
        }
        if(!appendEnv(list, AS_STRING(entry->key), AS_STRING(entry->value))){
            envListFree(list);
            runtimeError(vm, "process.run could not allocate the environment.\n");
            return false;
        }
    }

    return true;
}

static ReadRes readReadyFd(int fd, ProcBuffer* buf){
    char chunk[4096];

    for(;;){
        ssize_t got = read(fd, chunk, sizeof(chunk));
        if(got > 0){
            if(!appendProcBuffer(buf, chunk, (size_t)got)){
                return READ_NOMEM;
            }
            return READ_OK;
        }

        if(got == 0){
            return READ_EOF;
        }

        if(errno == EINTR){
            continue;
        }

        return READ_ERROR;
    }
}

static int procExitCode(int status){
    if(WIFEXITED(status)){
        return WEXITSTATUS(status);
    }

    if(WIFSIGNALED(status)){
        return 128 + WTERMSIG(status);
    }

    return -1;
}

static bool monotonicMs(VM* vm, uint64_t* millis){
    struct timespec now;
    if(clock_gettime(CLOCK_MONOTONIC, &now) != 0){
        runtimeError(vm, "process.run could not read the monotonic clock: %s.\n", strerror(errno));
        return false;
    }

    *millis = (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
    return true;
}

static pid_t waitProc(VM* vm, pid_t pid, int* status, int opts){
    pid_t waited;
    do{
        waited = waitpid(pid, status, opts);
    }while(waited < 0 && errno == EINTR);

    if(waited < 0){
        runtimeError(vm, "process.run could not wait for process: %s.\n", strerror(errno));
    }
    return waited;
}

static bool killProc(pid_t pid){
    if(kill(-pid, SIGKILL) == 0){
        return true;
    }

    return kill(pid, SIGKILL) == 0 || errno == ESRCH;
}

static bool collectProc(
    VM* vm,
    pid_t pid,
    int outFd,
    int errFd,
    const ProcOpts* opts,
    ProcRes* res
){
    bool ok = true;
    bool reaped = false;
    int status = 0;
    uint64_t deadline = 0;

    if(opts->timeoutMs > 0){
        if(!monotonicMs(vm, &deadline)){
            killProc(pid);
            waitProc(vm, pid, &status, 0);
            close(outFd);
            close(errFd);
            return false;
        }
        deadline += opts->timeoutMs;
    }

    while(outFd >= 0 || errFd >= 0 || (opts->timeoutMs > 0 && !reaped)){
        uint64_t remaining = 0;
        if(opts->timeoutMs > 0 && !reaped){
            pid_t waited = waitProc(vm, pid, &status, WNOHANG);
            if(waited < 0){
                ok = false;
                break;
            }
            if(waited == pid){
                reaped = true;
                kill(-pid, SIGKILL);
            }else{
                uint64_t now;
                if(!monotonicMs(vm, &now)){
                    ok = false;
                    break;
                }

                if(now >= deadline){
                    if(!killProc(pid)){
                        runtimeError(vm, "process.run could not terminate timed out process: %s.\n", strerror(errno));
                        ok = false;
                        break;
                    }
                    res->timedOut = true;
                    if(waitProc(vm, pid, &status, 0) < 0){
                        ok = false;
                        break;
                    }
                    reaped = true;
                }else{
                    remaining = deadline - now;
                }
            }
        }

        if(outFd < 0 && errFd < 0 && reaped){
            break;
        }

        fd_set reads;
        FD_ZERO(&reads);

        int maxFd = -1;
        if(outFd >= 0){
            FD_SET(outFd, &reads);
            maxFd = outFd;
        }
        if(errFd >= 0){
            FD_SET(errFd, &reads);
            if(errFd > maxFd){
                maxFd = errFd;
            }
        }

        struct timeval wait;
        struct timeval* waitPtr = NULL;
        if(opts->timeoutMs > 0 && !reaped){
            uint64_t waitMs = remaining < 20 ? remaining : 20;
            wait.tv_sec = (long)(waitMs / 1000);
            wait.tv_usec = (long)(waitMs % 1000) * 1000;
            waitPtr = &wait;
        }

        int ready = select(maxFd + 1, &reads, NULL, NULL, waitPtr);
        if(ready < 0){
            if(errno == EINTR){
                continue;
            }
            runtimeError(vm, "process.run failed while reading process output: %s.\n", strerror(errno));
            ok = false;
            break;
        }
        if(ready == 0){
            continue;
        }

        if(outFd >= 0 && FD_ISSET(outFd, &reads)){
            ReadRes readRes = readReadyFd(outFd, &res->out);
            if(readRes == READ_EOF){
                close(outFd);
                outFd = -1;
            }else if(readRes == READ_NOMEM){
                runtimeError(vm, "process.run could not allocate stdout.\n");
                ok = false;
                break;
            }else if(readRes == READ_ERROR){
                runtimeError(vm, "process.run failed while reading stdout.\n");
                ok = false;
                break;
            }
        }

        if(errFd >= 0 && FD_ISSET(errFd, &reads)){
            ReadRes readRes = readReadyFd(errFd, &res->err);
            if(readRes == READ_EOF){
                close(errFd);
                errFd = -1;
            }else if(readRes == READ_NOMEM){
                runtimeError(vm, "process.run could not allocate stderr.\n");
                ok = false;
                break;
            }else if(readRes == READ_ERROR){
                runtimeError(vm, "process.run failed while reading stderr.\n");
                ok = false;
                break;
            }
        }
    }

    if(outFd >= 0){
        close(outFd);
    }
    if(errFd >= 0){
        close(errFd);
    }

    if(!reaped){
        if(!ok){
            killProc(pid);
        }
        if(waitProc(vm, pid, &status, 0) < 0){
            ok = false;
        }
    }

    if(ok && !res->timedOut){
        res->code = procExitCode(status);
    }

    return ok;
}

bool runProc(
    VM* vm,
    char** argv,
    const ProcOpts* opts,
    ProcRes* res
){
    EnvList env;
    if(!buildEnv(vm, opts->env, &env)){
        return false;
    }

    int outPipe[2];
    int errPipe[2];

    if(pipe(outPipe) != 0){
        envListFree(&env);
        runtimeError(vm, "process.run could not create stdout pipe: %s.\n", strerror(errno));
        return false;
    }

    if(pipe(errPipe) != 0){
        envListFree(&env);
        close(outPipe[0]);
        close(outPipe[1]);
        runtimeError(vm, "process.run could not create stderr pipe: %s.\n", strerror(errno));
        return false;
    }

    pid_t pid = fork();
    if(pid < 0){
        envListFree(&env);
        close(outPipe[0]);
        close(outPipe[1]);
        close(errPipe[0]);
        close(errPipe[1]);
        runtimeError(vm, "process.run could not fork: %s.\n", strerror(errno));
        return false;
    }

    if(pid == 0){
        if(dup2(outPipe[1], STDOUT_FILENO) < 0 ||
            dup2(errPipe[1], STDERR_FILENO) < 0){
            const char msg[] = "process.run: failed to redirect process output\n";
            writeChildErr(errPipe[1], msg, sizeof(msg) - 1);
            close(outPipe[0]);
            close(outPipe[1]);
            close(errPipe[0]);
            close(errPipe[1]);
            _exit(126);
        }

        close(outPipe[0]);
        close(outPipe[1]);
        close(errPipe[0]);
        close(errPipe[1]);

        if(opts->timeoutMs > 0 && setpgid(0, 0) != 0){
            const char msg[] = "process.run: failed to create process group\n";
            writeChildErr(STDERR_FILENO, msg, sizeof(msg) - 1);
            _exit(126);
        }

        if(opts->cwd != NULL && chdir(opts->cwd) != 0){
            const char msg[] = "process.run: failed to enter cwd\n";
            writeChildErr(STDERR_FILENO, msg, sizeof(msg) - 1);
            _exit(127);
        }
        if(env.items != NULL){
            environ = env.items;
        }

        execvp(argv[0], argv);
        const char msg[] = "process.run: failed to execute process\n";
        writeChildErr(STDERR_FILENO, msg, sizeof(msg) - 1);
        _exit(127);
    }

    envListFree(&env);
    close(outPipe[1]);
    close(errPipe[1]);

    if(opts->timeoutMs > 0){
        setpgid(pid, pid);
    }

    return collectProc(vm, pid, outPipe[0], errPipe[0], opts, res);
}

#endif
