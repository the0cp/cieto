#ifndef CIETO_METHODS_STRING_BUILDER_H
#define CIETO_METHODS_STRING_BUILDER_H

#include "vm.h"

Value stringBuilderNative(VM* vm, int argCount, Value* args);
Value stringBuilderAppend(VM* vm, int argCount, Value* args);
Value stringBuilderString(VM* vm, int argCount, Value* args);
Value stringBuilderLen(VM* vm, int argCount, Value* args);

#endif // CIETO_METHODS_STRING_BUILDER_H
