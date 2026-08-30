#ifndef CIETO_VM_H
#define CIETO_VM_H

#include "hashtable.h"
#include "object.h"
#include "instruction.h"
#include "gc_types.h"
#include "global_env.h"
#include "writer.h"

#include <stddef.h>

typedef struct Chunk Chunk;
typedef struct Object Object;
typedef struct Compiler Compiler;
typedef struct GCPolicy GCPolicy;

#define FRAMES_MAX 64
#define STACK_MAX (FRAMES_MAX * 256)
#define GLOBAL_STATCK_MAX 64
#define MAX_DEFERS 255
#define VM_ERROR_MESSAGE_MAX 512

typedef void(*VMWriteFunc)(const char* text, size_t length, void* userData);

typedef struct CompileOpts{
    bool foldConst;
}CompileOpts;

typedef struct CallFrame{
    ObjectClosure* closure;
    Instruction* ip;
    Value* base;
    GlobalEnv* globals;   // point to defining module's global env for global access
    ObjectClosure* defers[MAX_DEFERS];
    int deferCnt;
}CallFrame;

typedef struct GCStats{
    size_t count;
    size_t bytesBefore;
    size_t bytesAfter;
    size_t bytesTotalFreed;

    double totalMs;
    double maxPauseMs;
    double markMs;
    double internMs;
    double sweepMs;
}GCStats;

typedef struct VM{
    Value stack[STACK_MAX];  // Stack for values
    Value* stackTop;         // for alloc new CallFrame
    HashTable strings;

    /*
    HashTable globals;
    HashTable* curGlobal;
    HashTable* globalStack[GLOBAL_STATCK_MAX];
    */
    
    GlobalEnv globals;
    GlobalEnv* curGlobal;
    GlobalEnv* globalStack[GLOBAL_STATCK_MAX];

    int globalCnt;
    HashTable modCache;
    Object* objects;
    ObjectString* initString;
    ObjectUpvalue* openUpvalues;    // descending locations
    CallFrame frames[FRAMES_MAX];
    int frameCount;

    size_t bytesAllocated;
    size_t nextGC;
    size_t gcThreshold;

    GCMode gcMode;
    const GCPolicy* gcPolicy;
    bool gcRunning;
    GCStats gcStats;

    Compiler* compiler;
    uint64_t hash_seed;

    int argc;
    const char** argv;

    bool hadRuntimeError;

    /*
     * Whether script code may terminate the host process through os.exit().
     * The CLI enables this to preserve its existing behavior.
     * The public embedding API disables it.
    */
    bool allowProcessExit;

    /*
     * Most recent compilation or runtime error.
     * The message remains available after recover().
    */
    char lastError[VM_ERROR_MESSAGE_MAX];

    Writer output;
    Writer errOutput;
}VM;

typedef enum{
    VM_OK,
    VM_COMPILE_ERROR,
    VM_RUNTIME_ERROR
}InterpreterStatus;

void initVM(VM* vm, int argc, const char* argv[]);
void freeVM(VM* vm);
void resetStack(VM* vm);
void recover(VM* vm);
void push(VM* vm, Value value);
Value pop(VM* vm);
Value peek(VM* vm, int distance);

static bool checkAccess(VM* vm, ObjectClass* instanceKlass, ObjectString* fieldName);

InterpreterStatus interpret(VM* vm, const char* code, const char* srcName);
InterpreterStatus interpretWithOpts(
    VM* vm,
    const char* code,
    const char* srcName,
    const CompileOpts* opts
);
InterpreterStatus vmCallValue(VM* vm, Value callee, int argCount, const Value* args, Value* result);
static InterpreterStatus run(VM* vm);

static bool call(VM* vm, ObjectClosure* closure, int argCnt);
static bool callValue(VM* vm, Value callee, int argCnt);

void vmWrite(VM* vm, const char* text, size_t length);
void vmWriteCString(VM* vm, const char* text);
void vmWriteError(VM* vm, const char* text, size_t length);
void vmWriteErrorCString(VM* vm, const char* text);

void runtimeError(VM* vm, const char* format, ...);

#endif // CIETO_VM_H
