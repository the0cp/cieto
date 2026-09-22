#ifndef CIETO_MODULE_ITER_H
#define CIETO_MODULE_ITER_H

#include "vm.h"

Value iterNative(VM* vm, int argCount, Value* args);
Value iteratorAdvance(VM* vm, int argCount, Value* args);
Value iteratorCurrent(VM* vm, int argCount, Value* args);

#endif
