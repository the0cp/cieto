#ifdef _WIN32

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "object.h"
#include "value.h"
#include "vm.h"

#include "process.h"

typedef struct{
    HANDLE pipe;
    ProcBuffer* buf;
    bool ok;
}PipeRead;

typedef struct{
    char** items;
    size_t count;
}EnvList;

static DWORD WINAPI readPipeThread(LPVOID raw){
    PipeRead* read = (PipeRead*)raw;
    char chunk[4096];
    DWORD got = 0;

    while(ReadFile(read->pipe, chunk, sizeof(chunk), &got, NULL) && got > 0){
        if(read->ok && !appendProcBuffer(read->buf, chunk, (size_t)got)){
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

// Follow the argument parsing rules used by the Microsoft C runtime.
static bool needsQuote(const char* arg){
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
        return appendProcBuffer(cmd, arg, strlen(arg));
    }

    if(!appendProcChar(cmd, '"')){
        return false;
    }

    int slashes = 0;
    for(const char* c = arg; *c != '\0'; c++){
        if(*c == '\\'){
            slashes++;
            continue;
        }

        if(*c == '"'){
            for(int i = 0; i < slashes * 2 + 1; i++){
                if(!appendProcChar(cmd, '\\')){
                    return false;
                }
            }
            slashes = 0;
            if(!appendProcChar(cmd, '"')){
                return false;
            }
            continue;
        }

        for(int i = 0; i < slashes; i++){
            if(!appendProcChar(cmd, '\\')){
                return false;
            }
        }
        slashes = 0;

        if(!appendProcChar(cmd, *c)){
            return false;
        }
    }

    for(int i = 0; i < slashes * 2; i++){
        if(!appendProcChar(cmd, '\\')){
            return false;
        }
    }

    return appendProcChar(cmd, '"');
}

static bool buildCmdLine(char** argv, ProcBuffer* cmd){
    for(int i = 0; argv[i] != NULL; i++){
        if(i > 0 && !appendProcChar(cmd, ' ')){
            return false;
        }

        if(!appendQuotedArg(cmd, argv[i])){
            return false;
        }
    }

    return true;
}

static void envListFree(EnvList* list){
    for(size_t i = 0; i < list->count; i++){
        free(list->items[i]);
    }
    free(list->items);
}

static bool envEntryEq(const char* entry, ObjectString* key){
    if(entry[0] == '='){
        return false;
    }

    const char* equal = strchr(entry, '=');
    size_t len = equal != NULL ? (size_t)(equal - entry) : strlen(entry);
    return len == key->length && _strnicmp(entry, key->chars, len) == 0;
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

static bool checkEnvNames(VM* vm, ObjectMap* env){
    for(int i = 0; i < env->table.capacity; i++){
        Entry* left = &env->table.entries[i];
        if(IS_EMPTY(left->key)){
            continue;
        }

        ObjectString* leftKey = AS_STRING(left->key);
        for(int j = i + 1; j < env->table.capacity; j++){
            Entry* right = &env->table.entries[j];
            if(IS_EMPTY(right->key)){
                continue;
            }

            ObjectString* rightKey = AS_STRING(right->key);
            if(leftKey->length == rightKey->length &&
                _strnicmp(leftKey->chars, rightKey->chars, leftKey->length) == 0){
                runtimeError(vm, "process.run env names must be unique ignoring case on Windows.\n");
                return false;
            }
        }
    }

    return true;
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

static int cmpEnv(const void* left, const void* right){
    const char* a = *(const char* const*)left;
    const char* b = *(const char* const*)right;
    return _stricmp(a, b);
}

static bool buildEnv(VM* vm, ObjectMap* env, char** block){
    *block = NULL;
    if(env == NULL || env->table.count == 0){
        return true;
    }
    if(!checkEnvNames(vm, env)){
        return false;
    }

    LPCH raw = GetEnvironmentStringsA();
    if(raw == NULL){
        runtimeError(vm, "process.run could not read the environment.\n");
        return false;
    }

    size_t inherited = 0;
    for(const char* item = raw; *item != '\0'; item += strlen(item) + 1){
        inherited++;
    }

    if(inherited > SIZE_MAX - (size_t)env->table.count){
        FreeEnvironmentStringsA(raw);
        runtimeError(vm, "process.run environment is too large.\n");
        return false;
    }

    EnvList list = {0};
    list.items = (char**)calloc(inherited + (size_t)env->table.count, sizeof(char*));
    if(list.items == NULL){
        FreeEnvironmentStringsA(raw);
        runtimeError(vm, "process.run could not allocate the environment.\n");
        return false;
    }

    for(const char* item = raw; *item != '\0'; item += strlen(item) + 1){
        if(envHasKey(env, item)){
            continue;
        }

        char* copy = strdup(item);
        if(copy == NULL){
            FreeEnvironmentStringsA(raw);
            envListFree(&list);
            runtimeError(vm, "process.run could not allocate the environment.\n");
            return false;
        }
        list.items[list.count++] = copy;
    }
    FreeEnvironmentStringsA(raw);

    for(int i = 0; i < env->table.capacity; i++){
        Entry* entry = &env->table.entries[i];
        if(IS_EMPTY(entry->key)){
            continue;
        }
        if(!appendEnv(&list, AS_STRING(entry->key), AS_STRING(entry->value))){
            envListFree(&list);
            runtimeError(vm, "process.run could not allocate the environment.\n");
            return false;
        }
    }

    qsort(list.items, list.count, sizeof(char*), cmpEnv);

    size_t size = 1;
    for(size_t i = 0; i < list.count; i++){
        size_t len = strlen(list.items[i]) + 1;
        if(size > SIZE_MAX - len){
            envListFree(&list);
            runtimeError(vm, "process.run environment is too large.\n");
            return false;
        }
        size += len;
    }

    char* data = (char*)calloc(size, 1);
    if(data == NULL){
        envListFree(&list);
        runtimeError(vm, "process.run could not allocate the environment.\n");
        return false;
    }

    char* next = data;
    for(size_t i = 0; i < list.count; i++){
        size_t len = strlen(list.items[i]) + 1;
        memcpy(next, list.items[i], len);
        next += len;
    }

    envListFree(&list);
    *block = data;
    return true;
}

bool runProc(
    VM* vm,
    char** argv,
    const ProcOpts* opts,
    int* code,
    ProcBuffer* out,
    ProcBuffer* err
){
    char* env = NULL;
    if(!buildEnv(vm, opts->env, &env)){
        return false;
    }

    HANDLE outRead = INVALID_HANDLE_VALUE;
    HANDLE outWrite = INVALID_HANDLE_VALUE;
    HANDLE errRead = INVALID_HANDLE_VALUE;
    HANDLE errWrite = INVALID_HANDLE_VALUE;

    if(!makePipe(&outRead, &outWrite)){
        free(env);
        runtimeError(vm, "process.run could not create pipes.\n");
        return false;
    }

    if(!makePipe(&errRead, &errWrite)){
        free(env);
        CloseHandle(outRead);
        CloseHandle(outWrite);
        runtimeError(vm, "process.run could not create pipes.\n");
        return false;
    }

    ProcBuffer cmd = {0};
    if(!buildCmdLine(argv, &cmd)){
        free(env);
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
        env,
        opts->cwd,
        &si,
        &pi
    );

    free(env);
    freeProcBuffer(&cmd);
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
        if(len < 0 || !appendProcBuffer(err, msg, (size_t)len)){
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

#endif
