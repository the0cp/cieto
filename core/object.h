#ifndef CIETO_OBJECT_H
#define CIETO_OBJECT_H

#include <stddef.h>
#include <stdio.h>

#include "chunk.h"
#include "hashtable.h"
#include "global_env.h"
#include "writer.h"

typedef struct VM VM;

typedef Value (*CFunc)(VM* vm, int argCount, Value* args);

typedef struct CieCall CieCall;

typedef void (*HostCFunc)(CieCall* call, void* userData);

#define OBJECT_TYPE(value)  (AS_OBJECT(value)->type)

#define IS_STRING(value)        (IS_OBJECT(value) && OBJECT_TYPE(value) == OBJECT_STRING)
#define AS_STRING(value)        ((ObjectString*)AS_OBJECT(value))
#define AS_CSTRING(value)       (((ObjectString*)AS_OBJECT(value))->chars)

#define IS_LIST(value)          (IS_OBJECT(value) && OBJECT_TYPE(value) == OBJECT_LIST)
#define AS_LIST(value)          ((ObjectList*)AS_OBJECT(value))

#define IS_MAP(value)           (IS_OBJECT(value) && OBJECT_TYPE(value) == OBJECT_MAP)
#define AS_MAP(value)           ((ObjectMap*)AS_OBJECT(value))

#define IS_FUNC(value)          (IS_OBJECT(value) && OBJECT_TYPE(value) == OBJECT_FUNC)
#define AS_FUNC(value)          ((ObjectFunc*)AS_OBJECT(value))

#define IS_CFUNC(value)         (IS_OBJECT(value) && OBJECT_TYPE(value) == OBJECT_CFUNC)
#define AS_CFUNC_OBJECT(value)  ((ObjectCFunc*)AS_OBJECT(value))
#define AS_CFUNC(value)         (((ObjectCFunc*)AS_OBJECT(value))->func)

#define IS_MODULE(value)        (IS_OBJECT(value) && OBJECT_TYPE(value) == OBJECT_MODULE)
#define AS_MODULE(value)        ((ObjectModule*)AS_OBJECT(value))

#define IS_CLOSURE(value)       (IS_OBJECT(value) && OBJECT_TYPE(value) == OBJECT_CLOSURE)
#define AS_CLOSURE(value)       ((ObjectClosure*)AS_OBJECT(value))

#define IS_CLASS(value)         (IS_OBJECT(value) && OBJECT_TYPE(value) == OBJECT_CLASS)
#define AS_CLASS(value)         ((ObjectClass*)AS_OBJECT(value))

#define IS_INSTANCE(value)      (IS_OBJECT(value) && OBJECT_TYPE(value) == OBJECT_INSTANCE)
#define AS_INSTANCE(value)      ((ObjectInstance*)AS_OBJECT(value))

#define IS_BOUND_METHOD(value)  (IS_OBJECT(value) && OBJECT_TYPE(value) == OBJECT_BOUND_METHOD)
#define AS_BOUND_METHOD(value)  ((ObjectBoundMethod*)AS_OBJECT(value))

#define IS_FILE(value)          (IS_OBJECT(value) && OBJECT_TYPE(value) == OBJECT_FILE)
#define AS_FILE(value)          ((ObjectFile*)AS_OBJECT(value))

#define IS_ITERATOR(value)      (IS_OBJECT(value) && OBJECT_TYPE(value) == OBJECT_ITERATOR)
#define AS_ITERATOR(value)      ((ObjectIterator*)AS_OBJECT(value))

#define IS_STRING_BUILDER(value) \
    (IS_OBJECT(value) && OBJECT_TYPE(value) == OBJECT_STRING_BUILDER)
#define AS_STRING_BUILDER(value) ((ObjectStringBuilder*)AS_OBJECT(value))

typedef enum{
    OBJECT_STRING,
    OBJECT_LIST,
    OBJECT_MAP,
    OBJECT_FUNC,
    OBJECT_CFUNC,
    OBJECT_MODULE,
    OBJECT_CLOSURE,
    OBJECT_UPVALUE,
    OBJECT_CLASS,
    OBJECT_INSTANCE,
    OBJECT_BOUND_METHOD,
    OBJECT_FILE,
    OBJECT_ITERATOR,
    OBJECT_STRING_BUILDER,
}ObjectType;

typedef struct Object{
    ObjectType type;
    bool isMarked;
    struct Object* next;
}Object;

typedef struct ObjectString{
    Object obj;
    size_t length;
    uint64_t hash;
    char chars[];   // Flexible array member
}ObjectString;

ObjectString* copyString(VM* vm, const char* chars, int len);
ObjectString* takeString(VM* vm, char* chars, int length);

ObjectString* copyStringRaw(VM* vm, const char* chars, int len);
ObjectString* takeStringRaw(VM* vm, char* chars, int length);
ObjectString* concatStringsRaw(VM* vm, const Value* parts, int count);

typedef struct ObjectStringBuilder{
    Object obj;
    int length;
    int capacity;
    char* chars;
}ObjectStringBuilder;

ObjectStringBuilder* newStringBuilder(VM* vm);

typedef struct ObjectList{
    Object obj;
    int count;
    int capacity;
    Value* items;
}ObjectList;

ObjectList* newList(VM* vm);
void appendToList(VM* vm, ObjectList* list, Value value);

typedef struct ObjectMap{
    Object obj;
    HashTable table;
}ObjectMap;

ObjectMap* newMap(VM* vm);

typedef enum{
    TYPE_FUNC,
    TYPE_SCRIPT,
    TYPE_MODULE,
    TYPE_METHOD,
    TYPE_INITIALIZER,
}FuncType;

typedef struct ObjectFunc{
    Object obj;
    int arity;
    int upvalueCnt;
    Chunk chunk;
    ObjectString* name;
    ObjectString* srcName;
    FuncType type;
    struct ObjectClass* fieldOwner;
    int maxRegSlots;
}ObjectFunc;

ObjectFunc* newFunction(VM* vm);

typedef struct ObjectCFunc{
    Object obj;
    CFunc func;
    HostCFunc hostFunc;
    void* userData;
}ObjectCFunc;

ObjectCFunc* newCFunc(VM* vm, CFunc func);

ObjectCFunc* newHostCFunc(VM* vm, CFunc adapter, HostCFunc hostFunc, void* userData);

typedef enum{
    MODULE_NATIVE,
    MODULE_SCRIPT
}ModuleKind;

typedef enum{
    MODULE_LOADED,
    MODULE_LOADING,
    MODULE_ERROR
}ModuleStatus;

typedef struct ObjectModule{
    Object obj;
    ObjectString* path;
    ObjectString* name;
    ModuleKind kind;
    ModuleStatus status;
    GlobalEnv members;
}ObjectModule;

ObjectModule* newModule(
    VM* vm, 
    ObjectString* name,
    ObjectString* path,
    ModuleKind kind
);

typedef struct ObjectUpvalue{
    Object obj;
    Value* location; // pointing to local on stack when upvalue is opened
    Value closed;    // value when upvalue is closed
    struct ObjectUpvalue* next;
}ObjectUpvalue;

typedef struct ObjectClosure{
    Object obj;
    ObjectFunc* func; // point to func template
    GlobalEnv* globals;   // point to defining module's global env for global access
    int upvalueCnt;
    ObjectUpvalue* upvalues[]; // pointer array
}ObjectClosure;

ObjectUpvalue* newUpvalue(VM* vm, Value* slot);
ObjectClosure* newClosure(VM* vm, ObjectFunc* func, GlobalEnv* globals);

typedef struct ObjectClass{
    Object obj;
    ObjectString* name;
    HashTable methods;
    HashTable fields;
}ObjectClass;

typedef struct ObjectInstance{
    Object obj;
    ObjectClass* klass;
    HashTable fields;
}ObjectInstance;

ObjectClass* newClass(VM* vm, ObjectString* name);
ObjectInstance* newInstance(VM* vm, ObjectClass* klass);

typedef struct ObjectBoundMethod{
    Object obj;
    Value receiver;
    Object* method;
}ObjectBoundMethod;

ObjectBoundMethod* newBoundMethod(VM* vm, Value receiver, Object* method);

typedef struct ObjectFile{
    Object obj;
    FILE* handle;
    char* mode;
    bool isOpen;
}ObjectFile;

ObjectFile* newFile(VM* vm, FILE* file);

typedef struct ObjectIterator{
    Object obj;
    Value receiver;
    Value current;
    int index;
    bool hasCurrent;
}ObjectIterator;

ObjectIterator* newIterator(VM* vm, Value receiver);

void objectWrite(Value value, Writer* writer);

void freeObject(VM* vm, Object* object);
void freeObjects(VM* vm);

#endif // CIETO_OBJECT_H
