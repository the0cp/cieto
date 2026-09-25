#include <limits.h>
#include <string.h>

#include "string_builder.h"
#include "mem.h"
#include "object.h"

Value stringBuilderNative(VM* vm, int argCount, Value* args){
    (void)args;

    if(argCount != 0){
        runtimeError(vm, "StringBuilder expects no arguments.");
        return NULL_VAL;
    }

    return OBJECT_VAL(newStringBuilder(vm));
}

Value stringBuilderAppend(VM* vm, int argCount, Value* args){
    ObjectStringBuilder* builder = AS_STRING_BUILDER(args[-1]);
    int length = builder->length;

    for(int i = 0; i < argCount; i++){
        if(!IS_STRING(args[i])){
            runtimeError(vm, "StringBuilder.append expects strings.");
            return NULL_VAL;
        }

        size_t partLength = AS_STRING(args[i])->length;
        if(partLength > (size_t)(INT_MAX - length)){
            runtimeError(vm, "StringBuilder is too large.");
            return NULL_VAL;
        }
        length += (int)partLength;
    }

    if(length == builder->length){
        return args[-1];
    }

    if(length > builder->capacity){
        int oldCapacity = builder->capacity;
        int capacity = oldCapacity < 8 ? 8 : oldCapacity;
        while(capacity < length){
            capacity = capacity > INT_MAX / 2 ? length : capacity * 2;
        }

        builder->chars = (char*)reallocate(
            vm,
            builder->chars,
            oldCapacity == 0 ? 0 : (size_t)oldCapacity + 1,
            (size_t)capacity + 1
        );
        builder->capacity = capacity;
    }

    char* dest = builder->chars + builder->length;
    for(int i = 0; i < argCount; i++){
        ObjectString* part = AS_STRING(args[i]);
        if(part->length == 0){
            continue;
        }
        memcpy(dest, part->chars, part->length);
        dest += part->length;
    }

    builder->length = length;
    if(builder->chars != NULL){
        builder->chars[length] = '\0';
    }

    return args[-1];
}

Value stringBuilderString(VM* vm, int argCount, Value* args){
    if(argCount != 0){
        runtimeError(vm, "StringBuilder.string expects no arguments.");
        return NULL_VAL;
    }

    ObjectStringBuilder* builder = AS_STRING_BUILDER(args[-1]);
    const char* chars = builder->chars == NULL ? "" : builder->chars;
    return OBJECT_VAL(copyStringRaw(vm, chars, builder->length));
}

Value stringBuilderLen(VM* vm, int argCount, Value* args){
    if(argCount != 0){
        runtimeError(vm, "StringBuilder.len expects no arguments.");
        return NULL_VAL;
    }

    return NUM_VAL(AS_STRING_BUILDER(args[-1])->length);
}
