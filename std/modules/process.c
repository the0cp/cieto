#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/select.h>
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

#include "object.h"
#include "registry.h"
#include "value.h"
#include "vm.h"

#include "process.h"

typedef struct{
    char* data;
    size_t len;
    size_t capacity;
} ProcBuffer;

static void bufferFree(ProcBuffer* buf){
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->capacity = 0;
}

static bool bufferGrow(ProcBuffer* buf, size_t need){
    if(need <= buf->capacity){
        return true;
    }

    size_t capacity = buf->capacity < 256 ? 256 : buf->capacity;
    while(capacity < need){
        if(capacity > SIZE_MAX / 2){
            return false;
        }
        capacity *= 2;
    }

    char* data = (char*)realloc(buf->data, capacity);
    if(data == NULL){
        return false;
    }

    buf->data = data;
    buf->capacity = capacity;
    return true;
}

static bool bufferAppend(ProcBuffer* buf, const char* data, size_t len){
    if(len == 0){
        return true;
    }

    if(buf->len == SIZE_MAX || len > SIZE_MAX - buf->len - 1){
        return false;
    }

    size_t need = buf->len + len + 1;
    if(!bufferGrow(buf, need)){
        return false;
    }

    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
    return true;
}

static bool bufferAppendChar(ProcBuffer* buf, char ch){
    return bufferAppend(buf, &ch, 1);
}

static void mapSetVal(VM* vm, ObjectMap* map, const char* key, Value value){
    push(vm, value);

    ObjectString* keyStr = copyString(vm, key, (int)strlen(key));
    push(vm, OBJECT_VAL(keyStr));

    tableSet(vm, &map->table, OBJECT_VAL(keyStr), value);

    pop(vm);    // keyStr
    pop(vm);    // value
}

static char** makeArgv(VM* vm, ObjectList* list){
    char** argv = (char**)calloc((size_t)list->count + 1, sizeof(char*));
    if(argv == NULL){
        runtimeError(vm, "process.run could not allocate argv.\n");
        return NULL;
    }

    for(int i = 0; i < list->count; i++){
        if(!IS_STRING(list->items[i])){
            free(argv);
            runtimeError(vm, "process.run argv must contain only strings.\n");
            return NULL;
        }

        ObjectString* arg = AS_STRING(list->items[i]);
        if(memchr(arg->chars, '\0', arg->length) != NULL){
            free(argv);
            runtimeError(vm, "process.run argv must not contain null bytes.\n");
            return NULL;
        }

        argv[i] = arg->chars;
    }

    argv[list->count] = NULL;
    return argv;
}

#ifdef _WIN32  // Windows

typedef struct{
    HANDLE pipe;
    ProcBuffer* buf;
    bool ok;
} PipeRead;

static DWORD WINAPI readPipeThread(LPVOID raw){
    PipeRead* read = (PipeRead*)raw;
    char chunk[4096];
    DWORD got = 0;

    while(ReadFile(read->pipe, chunk, sizeof(chunk), &got, NULL) && got > 0){
        if(read->ok && !bufferAppend(read->buf, chunk, (size_t)got)){
            read->ok = false;
        }
    }

    CloseHandle(read->pipe);
    return read->ok ? 0 : 1;
}

static bool makePipe(HANDLE* readEnd, HANDLE* writeEnd){
    SECURITY_ATTRIBUTES attrs;
    attrs.nLength = sizeof(attrs);
    attrs.lpSecurityDescriptor = NULL;
    attrs.bInheritHandle = TRUE;

    if(!CreatePipe(readEnd, writeEnd, &attrs, 0)){
        return false;
    }

    if(!SetHandleInformation(*readEnd, HANDLE_FLAG_INHERIT, 0)){
        CloseHandle(*readEnd);
        CloseHandle(*writeEnd);
        return false;
    }

    return true;
}

// Windows command line quoting implementation is based on the rules described in
// https://docs.microsoft.com/en-us/cpp/c-language/parsing-c-command-line-arguments
static bool needsQuote(const char* arg){
    // Empty string needs quotes
    if(arg[0] == '\0'){
        return true;
    }

    for(const char* c = arg; *c != '\0'; c++){
        if(*c == ' ' || *c == '\t' || *c == '"'){
            return true;
        }
    }

    return false;
}

static bool appendQuotedArg(ProcBuffer* cmd, const char* arg){
    if(!needsQuote(arg)){
        return bufferAppend(cmd, arg, strlen(arg));
    }

    if(!bufferAppendChar(cmd, '"')){
        return false;
    }

    int slashes = 0;
    for(const char* c = arg; *c != '\0'; c++){
        if(*c == '\\'){
            slashes++;
            continue;
        }

        if(*c == '"'){
            for(int i = 0; i < slashes * 2 + 1; i++){  // Escape all slashes and the quote
                if(!bufferAppendChar(cmd, '\\')){
                    return false;
                }
            }
            slashes = 0;
            if(!bufferAppendChar(cmd, '"')){
                return false;
            }
            continue;
        }

        for(int i = 0; i < slashes; i++){
            if(!bufferAppendChar(cmd, '\\')){
                return false;
            }
        }
        slashes = 0;

        if(!bufferAppendChar(cmd, *c)){
            return false;
        }
    }

    for(int i = 0; i < slashes * 2; i++){
        if(!bufferAppendChar(cmd, '\\')){
            return false;
        }
    }

    return bufferAppendChar(cmd, '"');
}

static bool buildCmdLine(char** argv, ProcBuffer* cmd){
    for(int i = 0; argv[i] != NULL; i++){
        if(i > 0 && !bufferAppendChar(cmd, ' ')){
            return false;
        }

        if(!appendQuotedArg(cmd, argv[i])){
            return false;
        }
    }

    return true;
}

static bool runProc(VM* vm, char** argv, int* code, ProcBuffer* out, ProcBuffer* err){
    HANDLE outRead = INVALID_HANDLE_VALUE;
    HANDLE outWrite = INVALID_HANDLE_VALUE;
    HANDLE errRead = INVALID_HANDLE_VALUE;
    HANDLE errWrite = INVALID_HANDLE_VALUE;

    if(!makePipe(&outRead, &outWrite)){
        runtimeError(vm, "process.run could not create pipes.\n");
        return false;
    }

    if(!makePipe(&errRead, &errWrite)){
        CloseHandle(outRead);
        CloseHandle(outWrite);
        runtimeError(vm, "process.run could not create pipes.\n");
        return false;
    }

    ProcBuffer cmd = {0};
    if(!buildCmdLine(argv, &cmd)){
        CloseHandle(outRead);
        CloseHandle(outWrite);
        CloseHandle(errRead);
        CloseHandle(errWrite);
        runtimeError(vm, "process.run could not allocate command line.\n");
        return false;
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));

    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = outWrite;
    si.hStdError = errWrite;

    BOOL started = CreateProcessA(
        NULL,
        cmd.data,
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &si,
        &pi
    );

    bufferFree(&cmd);
    CloseHandle(outWrite);
    CloseHandle(errWrite);

    if(!started){
        DWORD errCode = GetLastError();
        CloseHandle(outRead);
        CloseHandle(errRead);

        char msg[128];
        int len = snprintf(
            msg,
            sizeof(msg),
            "process.run: failed to start process: %lu\n",
            (unsigned long)errCode
        );
        if(len >= (int)sizeof(msg)){
            len = (int)sizeof(msg) - 1;
        }
        if(len < 0 || !bufferAppend(err, msg, (size_t)len)){
            runtimeError(vm, "process.run could not allocate process error.\n");
            return false;
        }

        *code = 127;
        return true;
    }

    PipeRead outJob = {outRead, out, true};
    PipeRead errJob = {errRead, err, true};
    HANDLE readers[2] = {0};
    readers[0] = CreateThread(NULL, 0, readPipeThread, &outJob, 0, NULL);
    if(readers[0] == NULL){
        CloseHandle(outRead);
        CloseHandle(errRead);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        runtimeError(vm, "process.run could not start output readers.\n");
        return false;
    }

    readers[1] = CreateThread(NULL, 0, readPipeThread, &errJob, 0, NULL);
    if(readers[1] == NULL){
        CloseHandle(errRead);
        WaitForSingleObject(pi.hProcess, INFINITE);
        WaitForSingleObject(readers[0], INFINITE);
        CloseHandle(readers[0]);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        runtimeError(vm, "process.run could not start output readers.\n");
        return false;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    WaitForMultipleObjects(2, readers, TRUE, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(readers[0]);
    CloseHandle(readers[1]);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if(!outJob.ok || !errJob.ok){
        runtimeError(vm, "process.run could not allocate process output.\n");
        return false;
    }

    *code = (int)exitCode;
    return true;
}

#else  // POSIX

typedef enum{
    READ_OK,
    READ_EOF,
    READ_ERROR,
    READ_NOMEM
}ReadRes;

static ReadRes readReadyFd(int fd, ProcBuffer* buf){
    char chunk[4096];

    for(;;){
        ssize_t got = read(fd, chunk, sizeof(chunk));
        if(got > 0){
            if(!bufferAppend(buf, chunk, (size_t)got)){
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

static bool runProc(VM* vm, char** argv, int* code, ProcBuffer* out, ProcBuffer* err){
    int outPipe[2];
    int errPipe[2];

    if(pipe(outPipe) != 0){
        runtimeError(vm, "process.run could not create stdout pipe: %s.\n", strerror(errno));
        return false;
    }

    if(pipe(errPipe) != 0){
        close(outPipe[0]);
        close(outPipe[1]);
        runtimeError(vm, "process.run could not create stderr pipe: %s.\n", strerror(errno));
        return false;
    }

    pid_t pid = fork();
    if(pid < 0){
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
            int errCode = errno;
            dprintf(
                errPipe[1],
                "process.run: failed to redirect process output: %s\n",
                strerror(errCode)
            );
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

        execvp(argv[0], argv);
        fprintf(stderr, "process.run: failed to execute '%s': %s\n", argv[0], strerror(errno));
        _exit(127);
    }

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

static Value makeResult(VM* vm, int code, ProcBuffer* out, ProcBuffer* err){
    if(out->len > INT_MAX || err->len > INT_MAX){
        runtimeError(vm, "process.run output is too large.\n");
        return NULL_VAL;
    }

    ObjectMap* result = newMap(vm);
    push(vm, OBJECT_VAL(result));

    const char* outData = out->data != NULL ? out->data : "";
    const char* errData = err->data != NULL ? err->data : "";

    ObjectString* outStr = copyString(vm, outData, (int)out->len);
    mapSetVal(vm, result, "stdout", OBJECT_VAL(outStr));

    ObjectString* errStr = copyString(vm, errData, (int)err->len);
    mapSetVal(vm, result, "stderr", OBJECT_VAL(errStr));

    mapSetVal(vm, result, "code", NUM_VAL((double)code));
    mapSetVal(vm, result, "ok", BOOL_VAL(code == 0));

    pop(vm);    // result
    return OBJECT_VAL(result);
}

static Value process_run(VM* vm, int argCount, Value* args){
    if(argCount != 1 || !IS_LIST(args[0])){
        runtimeError(vm, "process.run expects a single argv list.\n");
        return NULL_VAL;
    }

    ObjectList* argvList = AS_LIST(args[0]);
    if(argvList->count == 0){
        runtimeError(vm, "process.run argv must not be empty.\n");
        return NULL_VAL;
    }

    char** argv = makeArgv(vm, argvList);
    if(argv == NULL){
        return NULL_VAL;
    }

    ProcBuffer out = {0};
    ProcBuffer err = {0};
    int code = -1;

    bool ok = runProc(vm, argv, &code, &out, &err);
    free(argv);

    if(!ok){
        bufferFree(&out);
        bufferFree(&err);
        return NULL_VAL;
    }

    Value result = makeResult(vm, code, &out, &err);
    bufferFree(&out);
    bufferFree(&err);
    return result;
}

void initProcessModule(VM* vm, ObjectModule* module){
    defineCFunc(vm, &module->members, "run", process_run);
}
