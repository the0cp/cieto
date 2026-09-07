#include <limits.h>
#include <math.h>
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

typedef struct{
    ObjectList* argv;
    ProcOpts opts;
}ProcSpec;

static bool readSpecMap(VM* vm, ObjectMap* map, ProcSpec* spec){
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
        if(strEq(key, "argv")){
            if(spec->argv != NULL){
                runtimeError(vm, "process.run argv must not be specified twice.\n");
                return false;
            }

            if(!IS_LIST(entry->value)){
                runtimeError(vm, "process.run argv must be a List.\n");
                return false;
            }

            spec->argv = AS_LIST(entry->value);
        }else if(strEq(key, "cwd")){
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

            spec->opts.cwd = cwd->chars;
        }else if(strEq(key, "env")){
            if(!IS_MAP(entry->value)){
                runtimeError(vm, "process.run env must be a Map.\n");
                return false;
            }

            spec->opts.env = AS_MAP(entry->value);
            if(!checkEnv(vm, spec->opts.env)){
                return false;
            }
        }else if(strEq(key, "timeout")){
            if(!IS_NUM(entry->value)){
                runtimeError(vm, "process.run timeout must be a number.\n");
                return false;
            }

            double seconds = AS_NUM(entry->value);
            if(!isfinite(seconds) || seconds <= 0){
                runtimeError(vm, "process.run timeout must be finite and greater than zero.\n");
                return false;
            }

            double millis = ceil(seconds * 1000.0);
            if(millis >= UINT32_MAX){
                runtimeError(vm, "process.run timeout is too large.\n");
                return false;
            }

            spec->opts.timeoutMs = (uint32_t)millis;
        }else{
            runtimeError(vm, "process.run unknown option '%.*s'.\n", (int)key->length, key->chars);
            return false;
        }
    }

    return true;
}

static bool readSpec(VM* vm, int argCount, Value* args, ProcSpec* spec){
    memset(spec, 0, sizeof(ProcSpec));

    if(argCount < 1 || argCount > 2){
        runtimeError(vm, "process.run expects argv or a process config.\n");
        return false;
    }

    if(IS_LIST(args[0])){
        spec->argv = AS_LIST(args[0]);
    }else if(argCount == 1 && IS_MAP(args[0])){
        if(!readSpecMap(vm, AS_MAP(args[0]), spec)){
            return false;
        }
    }else{
        runtimeError(vm, "process.run expects argv or a process config.\n");
        return false;
    }

    if(argCount == 2){
        if(!IS_MAP(args[1])){
            runtimeError(vm, "process.run opts must be a Map.\n");
            return false;
        }
        if(!readSpecMap(vm, AS_MAP(args[1]), spec)){
            return false;
        }
    }

    if(spec->argv == NULL){
        runtimeError(vm, "process.run config requires argv.\n");
        return false;
    }

    if(spec->argv->count == 0){
        runtimeError(vm, "process.run argv must not be empty.\n");
        return false;
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

static Value makeResult(VM* vm, const ProcRes* res){
    if(res->out.len > INT_MAX || res->err.len > INT_MAX){
        runtimeError(vm, "process.run output is too large.\n");
        return NULL_VAL;
    }

    ObjectMap* result = newMap(vm);
    push(vm, OBJECT_VAL(result));

    const char* outData = res->out.data != NULL ? res->out.data : "";
    const char* errData = res->err.data != NULL ? res->err.data : "";

    ObjectString* outStr = copyString(vm, outData, (int)res->out.len);
    mapSetVal(vm, result, "stdout", OBJECT_VAL(outStr));

    ObjectString* errStr = copyString(vm, errData, (int)res->err.len);
    mapSetVal(vm, result, "stderr", OBJECT_VAL(errStr));

    Value code = res->timedOut ? NULL_VAL : NUM_VAL((double)res->code);
    mapSetVal(vm, result, "code", code);
    mapSetVal(vm, result, "ok", BOOL_VAL(!res->timedOut && res->code == 0));
    mapSetVal(vm, result, "timedOut", BOOL_VAL(res->timedOut));

    pop(vm);    // result
    return OBJECT_VAL(result);
}

static Value process_run(VM* vm, int argCount, Value* args){
    ProcSpec spec;
    if(!readSpec(vm, argCount, args, &spec)){
        return NULL_VAL;
    }

    char** argv = makeArgv(vm, spec.argv);
    if(argv == NULL){
        return NULL_VAL;
    }

    ProcRes res = {.code = -1};

    bool ok = runProc(vm, argv, &spec.opts, &res);
    free(argv);

    if(!ok){
        freeProcBuffer(&res.out);
        freeProcBuffer(&res.err);
        return NULL_VAL;
    }

    Value result = makeResult(vm, &res);
    freeProcBuffer(&res.out);
    freeProcBuffer(&res.err);
    return result;
}

void initProcessModule(VM* vm, ObjectModule* module){
    defineCFunc(vm, &module->members, "run", process_run);
}
