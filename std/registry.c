#include <string.h>

#include "registry.h"

#include "prelude/iter.h"

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

void registerPrelude(VM* vm){
    defineCFunc(vm, &vm->globals, "iter", iterNative);
    defineCFunc(vm, &vm->globals, "next", nextNative);
}
