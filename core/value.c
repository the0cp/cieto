#define CIETO_MAX_SAFE_INT 9007199254740991.0

#include <stdio.h>

#include "mem.h"
#include "value.h"
#include "object.h"

static int u64ToString(uint64_t num, char* out){
    char buffer[32];
    int length = 0;

    do{
        buffer[length++] = (char)('0' + (num % 10));
        num /= 10;
    }while(num != 0);

    for(int i = 0; i < length; i++){
        out[i] = buffer[length - 1 - i];
    }

    out[length] = '\0';
    return length;
}

int numToString(double num, char* out, size_t cap){
    if(num == 0.0){
        out[0] = '0';
        out[1] = '\0';
        return 1;
    }

    if(num > 0.0 && num <= CIETO_MAX_SAFE_INT){
        uint64_t n = (uint64_t)num;
        if((double)n == num){
            return u64ToString(n, out);
        }
    }

    if(num < 0.0 && num >= -CIETO_MAX_SAFE_INT){
        uint64_t n = (uint64_t)(-num);
        if((double)(-n) == num){
            out[0] = '-';
            return 1 + u64ToString(n, out + 1);
        }
    }

    return snprintf(out, cap, "%.14g", num);
}

char* valueToString(Value value){
    if(IS_BOOL(value)){
        return AS_BOOL(value) ? "true" : "false";
    }else if(IS_NULL(value)){
        return "null";
    }else if(IS_NUM(value)){
        static char num_str[32];
        numToString(AS_NUM(value), num_str, sizeof(num_str));
        return num_str;
    }else if(IS_STRING(value)){
        return AS_CSTRING(value);
    }
    return "Unknown";
}

static void appendString(VM* vm, char** buffer, int* length, int* capacity, const char* str, int len){
    if(*length + len > *capacity){
        int oldCapacity = *capacity;
        *capacity = (*capacity < 8 ? 8 : *capacity * 2) + len;
        *buffer = (char*)reallocate(vm, *buffer, oldCapacity, *capacity);
    }
    memcpy(*buffer + *length, str, len);
    *length += len;
    (*buffer)[*length] = '\0';
}

ObjectString* toString(VM* vm, Value value){
    if(IS_STRING(value)){
        return AS_STRING(value);
    }

    if(IS_LIST(value)){
        ObjectList* list = AS_LIST(value);
        char* buffer = NULL;
        int length = 0;
        int capacity = 0;

        appendString(vm, &buffer, &length, &capacity, "[", 1);

        for(int i = 0; i < list->count; i++){
            Value item = list->items[i];
            
            if(IS_STRING(item)){
                ObjectString* s = AS_STRING(item);
                appendString(vm, &buffer, &length, &capacity, "\"", 1);
                appendString(vm, &buffer, &length, &capacity, s->chars, s->length);
                appendString(vm, &buffer, &length, &capacity, "\"", 1);
            }else if(IS_OBJECT(item)){
                ObjectString* s = toString(vm, item); 
                appendString(vm, &buffer, &length, &capacity, s->chars, s->length);
            }else{
                char* s = valueToString(item);
                appendString(vm, &buffer, &length, &capacity, s, (int)strlen(s));
            }

            if(i < list->count - 1){
                appendString(vm, &buffer, &length, &capacity, ", ", 2);
            }
        }

        appendString(vm, &buffer, &length, &capacity, "]", 1);

        ObjectString* result = copyStringRaw(vm, buffer, length);
        reallocate(vm, buffer, capacity, 0);
        return result;
    }
    char* chars = valueToString(value);
    return copyStringRaw(vm, chars, (int)strlen(chars));
}

void initValueArray(ValueArray* array){
    array->values = NULL;
    array->count = 0;
    array->capacity = 0;
}

void writeValueArray(VM* vm, ValueArray* array, Value value){
    if(array->count + 1 > array->capacity){
        size_t oldCapacity = array->capacity;
        array->capacity = oldCapacity < 8 ? 8 : oldCapacity * 2;
        array->values = (Value*)reallocate(
            vm,
            array->values,
            sizeof(Value) * oldCapacity,
            sizeof(Value) * array->capacity
        );
    }
    array->values[array->count++] = value;
}

void freeValueArray(VM* vm, ValueArray* array){
    reallocate(vm, array->values, sizeof(Value) * array->capacity, 0);
    initValueArray(array);
}

void valueWrite(Value value, Writer* writer){
    if(IS_NULL(value)){
        writerWCString(writer, "null");
    }else if(IS_BOOL(value)){
        writerWCString(writer, AS_BOOL(value) ? "true" : "false");
    }else if(IS_NUM(value)){
        char buffer[32];
        int length = numToString(AS_NUM(value), buffer, sizeof(buffer));
        writerW(writer, buffer, (size_t)length);
    }else if(IS_OBJECT(value)){
        objectWrite(value, writer);
    }else{
        writerWCString(writer, "Unknown value type");
    }
}

ValueType getValueType(Value value){
    if(IS_NULL(value))   return VALUE_NULL;
    if(IS_BOOL(value))   return VALUE_BOOL;
    if(IS_NUM(value))    return VALUE_NUM;
    if(IS_OBJECT(value)) return VALUE_OBJECT;
    return VALUE_UNKNOWN;  // For unsupported types
}

bool isTruthy(Value value){
    return !(IS_NULL(value) ||
            (IS_BOOL(value) && !AS_BOOL(value)) ||
            (IS_NUM(value) && AS_NUM(value) == 0));
}

bool isEqual(Value a, Value b){
    if(getValueType(a) != getValueType(b)){
        return false;
    }

    switch(getValueType(a)){
        case VALUE_NULL:    return true;  // Both are null
        case VALUE_BOOL:    return AS_BOOL(a) == AS_BOOL(b);
        case VALUE_NUM:     return AS_NUM(a) == AS_NUM(b);
        case VALUE_OBJECT: {
            Object* objA = AS_OBJECT(a);
            Object* objB = AS_OBJECT(b);

            if(objA == objB){
                return true;
            }

            if(IS_STRING(a) && IS_STRING(b)){
                ObjectString* strA = AS_STRING(a);
                ObjectString* strB = AS_STRING(b);
                if(strA->hash != strB->hash) return false;
                if(strA->length != strB->length) return false;

                return memcmp(strA->chars, strB->chars, strA->length) == 0;
            }

            if(IS_LIST(a) && IS_LIST(b)){
                ObjectList* listA = AS_LIST(a);
                ObjectList* listB = AS_LIST(b);

                if(listA->count != listB->count) return false;

                for(int i = 0; i < listA->count; i++){
                    if(!isEqual(listA->items[i], listB->items[i])){
                        return false;
                    }
                }
                return true;
            }
            return false;
        }
        default:
            return false;  // Unsupported type comparison
    }
}
