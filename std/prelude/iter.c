#include <string.h>

#include "iter.h"
#include "object.h"

static bool isIterable(Value value){
    return IS_LIST(value) || IS_MAP(value) || IS_STRING(value) || IS_FILE(value);
}

static bool advanceIterator(VM* vm, ObjectIterator* iterator, Value* result){
    Value receiver = iterator->receiver;

    if(IS_LIST(receiver)){
        ObjectList* list = AS_LIST(receiver);
        if(iterator->index >= list->count){
            return false;
        }

        *result = list->items[iterator->index++];
        return true;
    }

    if(IS_MAP(receiver)){
        Entry* entry;
        if(!tableNextEntry(&AS_MAP(receiver)->table, &iterator->index, &entry)){
            return false;
        }

        *result = entry->key;
        return true;
    }

    if(IS_STRING(receiver)){
        ObjectString* string = AS_STRING(receiver);
        if((size_t)iterator->index >= string->length){
            return false;
        }

        char chars[2] = {string->chars[iterator->index++], '\0'};
        *result = OBJECT_VAL(copyStringRaw(vm, chars, 1));
        return true;
    }

    if(IS_FILE(receiver)){
        ObjectFile* file = AS_FILE(receiver);
        if(!file->isOpen || file->handle == NULL){
            runtimeError(vm, "Cannot iterate a closed file.");
            return false;
        }

        char buffer[1024];
        if(fgets(buffer, sizeof(buffer), file->handle) == NULL){
            if(ferror(file->handle)){
                runtimeError(vm, "Could not read from file while iterating.");
            }
            return false;
        }

        size_t len = strlen(buffer);
        if(len > 0 && buffer[len - 1] == '\n'){
            buffer[--len] = '\0';
        }

        iterator->index++;
        *result = OBJECT_VAL(copyString(vm, buffer, (int)len));
        return true;
    }

    return false;
}

Value iterNative(VM* vm, int argCount, Value* args){
    if(argCount != 1){
        runtimeError(vm, "iter expects exactly one argument.");
        return NULL_VAL;
    }
    
    Value collection = args[0];
    if(isIterable(collection)){
        return OBJECT_VAL(newIterator(vm, collection));
    }
    
    runtimeError(vm, "Object is not iterable.");
    return NULL_VAL;
}

Value iteratorAdvance(VM* vm, int argCount, Value* args){
    if(argCount != 0){
        runtimeError(vm, "iterator.advance expects no arguments.");
        return NULL_VAL;
    }

    ObjectIterator* iterator = AS_ITERATOR(args[-1]);
    iterator->hasCurrent = advanceIterator(vm, iterator, &iterator->current);
    if(!iterator->hasCurrent){
        iterator->current = NULL_VAL;
    }

    return BOOL_VAL(iterator->hasCurrent);
}

Value iteratorCurrent(VM* vm, int argCount, Value* args){
    if(argCount != 0){
        runtimeError(vm, "iterator.current expects no arguments.");
        return NULL_VAL;
    }

    ObjectIterator* iterator = AS_ITERATOR(args[-1]);
    if(!iterator->hasCurrent){
        runtimeError(vm, "iterator.current is unavailable before advance or after completion.");
        return NULL_VAL;
    }

    return iterator->current;
}
