#ifndef _WIN32

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/wait.h>
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

static bool readPipes(VM* vm, int outFd, int errFd, ProcBuffer* out, ProcBuffer* err){
    bool ok = true;

    while(outFd >= 0 || errFd >= 0){
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

        int ready = select(maxFd + 1, &reads, NULL, NULL, NULL);
        if(ready < 0){
            if(errno == EINTR){
                continue;
            }
            runtimeError(vm, "process.run failed while reading process output: %s.\n", strerror(errno));
            ok = false;
            break;
        }

        if(outFd >= 0 && FD_ISSET(outFd, &reads)){
            ReadRes res = readReadyFd(outFd, out);
            if(res == READ_EOF){
                close(outFd);
                outFd = -1;
            }else if(res == READ_NOMEM){
                runtimeError(vm, "process.run could not allocate stdout.\n");
                ok = false;
                break;
            }else if(res == READ_ERROR){
                runtimeError(vm, "process.run failed while reading stdout.\n");
                ok = false;
                break;
            }
        }

        if(errFd >= 0 && FD_ISSET(errFd, &reads)){
            ReadRes res = readReadyFd(errFd, err);
            if(res == READ_EOF){
                close(errFd);
                errFd = -1;
            }else if(res == READ_NOMEM){
                runtimeError(vm, "process.run could not allocate stderr.\n");
                ok = false;
                break;
            }else if(res == READ_ERROR){
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

    return ok;
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

bool runProc(
    VM* vm,
    char** argv,
    const ProcOpts* opts,
    int* code,
    ProcBuffer* out,
    ProcBuffer* err
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

    bool ok = readPipes(vm, outPipe[0], errPipe[0], out, err);

    int status = 0;
    while(waitpid(pid, &status, 0) < 0){
        if(errno == EINTR){
            continue;
        }

        runtimeError(vm, "process.run could not wait for process: %s.\n", strerror(errno));
        return false;
    }

    if(!ok){
        return false;
    }

    *code = procExitCode(status);
    return true;
}

#endif
