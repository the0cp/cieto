#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "object.h"
#include "registry.h"
#include "value.h"
#include "vm.h"

#include "process.h"

void freeProcBuffer(ProcBuffer* buf){
    free(buf->data);
    buf->data = NULL;
    buf->len = 0;
    buf->capacity = 0;
}

static bool growProcBuffer(ProcBuffer* buf, size_t need){
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

bool appendProcBuffer(ProcBuffer* buf, const char* data, size_t len){
    if(len == 0){
        return true;
    }

    if(buf->len == SIZE_MAX || len > SIZE_MAX - buf->len - 1){
        return false;
    }

    size_t need = buf->len + len + 1;
    if(!growProcBuffer(buf, need)){
        return false;
    }

    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    buf->data[buf->len] = '\0';
    return true;
}

bool appendProcChar(ProcBuffer* buf, char ch){
    return appendProcBuffer(buf, &ch, 1);
}

static void mapSetVal(VM* vm, ObjectMap* map, const char* key, Value value){
    push(vm, value);

    ObjectString* keyStr = copyString(vm, key, (int)strlen(key));
    push(vm, OBJECT_VAL(keyStr));

    tableSet(vm, &map->table, OBJECT_VAL(keyStr), value);

    pop(vm);    // keyStr
    pop(vm);    // value
}

static bool strEq(ObjectString* str, const char* text){
    size_t len = strlen(text);
    return str->length == len && memcmp(str->chars, text, len) == 0;
}

static bool hasNullByte(ObjectString* str){
    return memchr(str->chars, '\0', str->length) != NULL;
}

static bool checkEnv(VM* vm, ObjectMap* env){
    for(int i = 0; i < env->table.capacity; i++){
        Entry* entry = &env->table.entries[i];
        if(IS_EMPTY(entry->key)){
            continue;
        }

        if(!IS_STRING(entry->key)){
            runtimeError(vm, "process.run env names must be strings.\n");
            return false;
        }

        if(!IS_STRING(entry->value)){
            runtimeError(vm, "process.run env values must be strings.\n");
            return false;
        }

        ObjectString* key = AS_STRING(entry->key);
        ObjectString* value = AS_STRING(entry->value);

        if(key->length == 0 || memchr(key->chars, '=', key->length) != NULL){
            runtimeError(vm, "process.run env names must be non-empty and must not contain '='.\n");
            return false;
        }

        if(hasNullByte(key) || hasNullByte(value)){
            runtimeError(vm, "process.run env must not contain null bytes.\n");
            return false;
        }
    }

    return true;
}

static bool readOpts(VM* vm, int argCount, Value* args, ProcOpts* opts){
    opts->cwd = NULL;
    opts->env = NULL;

    if(argCount == 1){
        return true;
    }

    if(!IS_MAP(args[1])){
        runtimeError(vm, "process.run opts must be a Map.\n");
        return false;
    }

    ObjectMap* map = AS_MAP(args[1]);
    for(int i = 0; i < map->table.capacity; i++){
        Entry* entry = &map->table.entries[i];
        if(IS_EMPTY(entry->key)){
            continue;
        }

        if(!IS_STRING(entry->key)){
            runtimeError(vm, "process.run option names must be strings.\n");
            return false;
        }

        ObjectString* key = AS_STRING(entry->key);
        if(strEq(key, "cwd")){
            if(!IS_STRING(entry->value)){
                runtimeError(vm, "process.run cwd must be a string.\n");
                return false;
            }

            ObjectString* cwd = AS_STRING(entry->value);
            if(cwd->length == 0){
                runtimeError(vm, "process.run cwd must not be empty.\n");
                return false;
            }

            if(hasNullByte(cwd)){
                runtimeError(vm, "process.run cwd must not contain null bytes.\n");
                return false;
            }

            opts->cwd = cwd->chars;
        }else if(strEq(key, "env")){
            if(!IS_MAP(entry->value)){
                runtimeError(vm, "process.run env must be a Map.\n");
                return false;
            }

            opts->env = AS_MAP(entry->value);
            if(!checkEnv(vm, opts->env)){
                return false;
            }
        }else{
            runtimeError(vm, "process.run unknown option '%.*s'.\n", (int)key->length, key->chars);
            return false;
        }
    }

    return true;
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
        if(hasNullByte(arg)){
            free(argv);
            runtimeError(vm, "process.run argv must not contain null bytes.\n");
            return NULL;
        }

        argv[i] = arg->chars;
    }

    return argv;
}

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
    if(argCount < 1 || argCount > 2 || !IS_LIST(args[0])){
        runtimeError(vm, "process.run expects argv and an optional opts Map.\n");
        return NULL_VAL;
    }

    ObjectList* argvList = AS_LIST(args[0]);
    if(argvList->count == 0){
        runtimeError(vm, "process.run argv must not be empty.\n");
        return NULL_VAL;
    }

    ProcOpts opts;
    if(!readOpts(vm, argCount, args, &opts)){
        return NULL_VAL;
    }

    char** argv = makeArgv(vm, argvList);
    if(argv == NULL){
        return NULL_VAL;
    }

    ProcBuffer out = {0};
    ProcBuffer err = {0};
    int code = -1;

    bool ok = runProc(vm, argv, &opts, &code, &out, &err);
    free(argv);

    if(!ok){
        freeProcBuffer(&out);
        freeProcBuffer(&err);
        return NULL_VAL;
    }

    Value result = makeResult(vm, code, &out, &err);
    freeProcBuffer(&out);
    freeProcBuffer(&err);
    return result;
}

void initProcessModule(VM* vm, ObjectModule* module){
    defineCFunc(vm, &module->members, "run", process_run);
}
