#ifndef CIETO_MODULES_H
#define CIETO_MODULES_H

#include "vm.h"

typedef void (*NativeModuleInit)(VM* vm, ObjectModule* module);

typedef struct NativeModuleDef{
    const char* name;
    NativeModuleInit initFunc;
}NativeModuleDef;

void defineCFunc(VM* vm, GlobalEnv* env, const char* name, CFunc func);

const NativeModuleDef* findNativeModule(const char* name);
CFunc findBuiltinMethod(Value receiver, ObjectString* name);

void registerPrelude(VM* vm);

#endif // CIETO_MODULES_H