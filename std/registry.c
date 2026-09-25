#include <string.h>

#include "registry.h"

#include "prelude/iter.h"

#include "methods/list.h"
#include "methods/string.h"
#include "methods/string_builder.h"

#include "modules/fs.h"
#include "modules/path.h"
#include "modules/glob.h"
#include "modules/os.h"
#include "modules/process.h"
#include "modules/time.h"
#include "modules/gc.h"

void defineCFunc(VM* vm, GlobalEnv* env, const char* name, CFunc func){
    ObjectString* key = copyString(vm, name, (int)strlen(name));
    push(vm, OBJECT_VAL(key));

    ObjectCFunc* cfunc = newCFunc(vm, func);
    push(vm, OBJECT_VAL(cfunc));

    globalSetName(vm, env, key, OBJECT_VAL(cfunc));
    
    pop(vm);    // cfunc
    pop(vm);    // key
}

static NativeModuleDef nativeModules[] = {
    {"fs", initFsModule},
    {"time", initTimeModule},
    {"os", initOsModule},
    {"process", initProcessModule},
    {"path", initPathModule},
    {"glob", initGlobModule},
    {"gc", initGcModule},
    {NULL, NULL}
};

const NativeModuleDef* findNativeModule(const char* name){
    for(int i = 0; nativeModules[i].name != NULL; i++){
        if(strcmp(nativeModules[i].name, name) == 0){
            return &nativeModules[i];
        }
    }
    return NULL;
}

static CFunc findListMethod(ObjectString* name){
    switch(name->length){
        case 3:
            if(memcmp(name->chars, "pop", 3) == 0){
                return list_pop;
            }
            break;
        case 4:
            if(memcmp(name->chars, "push", 4) == 0){
                return list_push;
            }
            if(memcmp(name->chars, "size", 4) == 0){
                return list_size;
            }
            break;
    }

    return NULL;
}

static CFunc findStringMethod(ObjectString* name){
    if(name->length == 3){
        if(memcmp(name->chars, "len", 3) == 0){
            return string_len;
        }
        if(memcmp(name->chars, "sub", 3) == 0){
            return string_sub;
        }
    }else if(name->length == 4){
        if(memcmp(name->chars, "trim", 4) == 0){
            return string_trim;
        }
        if(memcmp(name->chars, "find", 4) == 0){
            return string_find;
        }
    }else if(name->length == 5){
        if(memcmp(name->chars, "upper", 5) == 0){
            return string_upper;
        }
        if(memcmp(name->chars, "lower", 5) == 0){
            return string_lower;
        }
        if(memcmp(name->chars, "split", 5) == 0){
            return string_split;
        }
    }else if(name->length == 7 && memcmp(name->chars, "replace", 7) == 0){
        return string_replace;
    }

    return NULL;
}

static CFunc findFileMethod(ObjectString* name){
    switch(name->length){
        case 4:
            if(memcmp(name->chars, "read", 4) == 0){
                return file_read;
            }
            break;
        case 5:
            if(memcmp(name->chars, "close", 5) == 0){
                return file_close;
            }
            if(memcmp(name->chars, "write", 5) == 0){
                return file_write;
            }
            break;
        case 8:
            if(memcmp(name->chars, "readLine", 8) == 0){
                return file_readLine;
            }
            break;
    }

    return NULL;
}

static CFunc findStringBuilderMethod(ObjectString* name){
    if(name->length == 3 && memcmp(name->chars, "len", 3) == 0){
        return stringBuilderLen;
    }
    if(name->length == 6){
        if(memcmp(name->chars, "append", 6) == 0){
            return stringBuilderAppend;
        }
        if(memcmp(name->chars, "string", 6) == 0){
            return stringBuilderString;
        }
    }
    return NULL;
}

static CFunc findIteratorMethod(ObjectString* name){
    if(name->length != 7){
        return NULL;
    }
    if(memcmp(name->chars, "advance", 7) == 0){
        return iteratorAdvance;
    }
    if(memcmp(name->chars, "current", 7) == 0){
        return iteratorCurrent;
    }
    return NULL;
}

CFunc findBuiltinMethod(Value receiver, ObjectString* name){
    if(IS_STRING(receiver)){
        return findStringMethod(name);
    }
    if(IS_LIST(receiver)){
        return findListMethod(name);
    }
    if(IS_FILE(receiver)){
        return findFileMethod(name);
    }
    if(IS_ITERATOR(receiver)){
        return findIteratorMethod(name);
    }
    if(IS_STRING_BUILDER(receiver)){
        return findStringBuilderMethod(name);
    }
    return NULL;
}

void registerPrelude(VM* vm, GlobalEnv* env){
    defineCFunc(vm, env, "iter", iterNative);
    defineCFunc(vm, env, "StringBuilder", stringBuilderNative);
}
