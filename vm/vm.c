#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

#include "vm.h"
#include "compiler.h"
#include "mem.h"
#include "registry.h"
#include "module_loader.h"
#include "gc_policy.h"

#include "methods/list.h"
#include "methods/string.h"

#include "modules/fs.h"

#ifdef DEBUG_TRACE
#include "debug.h"
#endif

#define R(n) (frame->base[(n)])
#define K(n) (frame->closure->func->chunk.constants.values[(n)])

#ifdef _WIN32
static int normalizeSystemStatus(int status){
    return status;
}
#else
#include <sys/wait.h>

static int normalizeSystemStatus(int status){
    if(status == -1){
        return -1;
    }

    if(WIFEXITED(status)){
        return WEXITSTATUS(status);
    }

    if(WIFSIGNALED(status)){
        return 128 + WTERMSIG(status);
    }

    return -1;
}
#endif

static void defaultOWrite(const char* text, size_t length, void* userData){
    (void)userData;
    fwrite(text, 1, length, stdout);
}

static void defaultEWrite(const char* text, size_t length, void* userData){
    (void)userData;
    fwrite(text, 1, length, stderr);
}

void vmWrite(VM* vm, const char* text, size_t length){
    if(vm == NULL || text == NULL || length == 0){
        return;
    }

    if(vm->output.write == NULL){
        defaultOWrite(text, length, NULL);
        return;
    }

    writerW(&vm->output, text, length);
}

void vmWriteCString(VM* vm, const char* text){
    if(text != NULL){
        vmWrite(vm, text, strlen(text));
    }
}

void vmWriteError(VM* vm, const char* text, size_t length){
    if(vm == NULL || text == NULL || length == 0){
        return;
    }

    if(vm->errOutput.write == NULL){
        defaultEWrite(text, length, NULL);
        return;
    }

    writerW(&vm->errOutput, text, length);
}

void vmWriteErrorCString(VM* vm, const char* text){
    if(text != NULL){
        vmWriteError(vm, text, strlen(text));
    }
}

void resetStack(VM* vm){
    vm->stackTop = vm->stack;
}

void recover(VM* vm){
    resetStack(vm);
    vm->frameCount = 0;
    vm->openUpvalues = NULL;
    vm->globalCnt = 0;
    vm->curGlobal = &vm->globals;
    vm->globalStack[0] = vm->curGlobal;
    vm->hadRuntimeError = false;
}

void initVM(VM* vm, int argc, const char* argv[]){
    resetStack(vm);
    vm->objects = NULL;
    vm->openUpvalues = NULL;
    vm->frameCount = 0;

    srand((unsigned int)time(NULL));
    uint64_t p1 = (uint64_t)rand();
    uint64_t p2 = (uint64_t)rand();
    vm->hash_seed = (p1 << 32) | p2;
    if(vm->hash_seed == 0) vm->hash_seed = 1;

    initHashTable(&vm->strings);
    initGlobalEnv(&vm->globals);
    initHashTable(&vm->modCache);

    vm->globalCnt = 0;
    vm->curGlobal = &vm->globals;
    vm->globalStack[0] = vm->curGlobal;

    /*
    vm->bytesAllocated = 0;
    vm->gcThreshold = 1024 * 1024 * 10;   // 10MB
    vm->nextGC = vm->gcThreshold;
    vm->gcMode = GC_MODE_AUTO;
    */

    initGC(vm);

    vm->compiler = NULL;

    vm->initString = NULL;
    vm->initString = copyString(vm, "init", 4);

    vm->argc = argc;
    vm->argv = argv;

    vm->hadRuntimeError = false;

    /*
     * initVM() is also used directly by the CLI. The embedding API overrides this after initialization.
    */

    vm->allowProcessExit = true;
    vm->lastError[0] = '\0';

    vm->output.write = defaultOWrite;
    vm->output.userData = NULL;
    vm->errOutput.write = defaultEWrite;
    vm->errOutput.userData = NULL;

    registerPrelude(vm);
}

void freeVM(VM* vm){
    freeHashTable(vm, &vm->strings);
    freeGlobalEnv(vm, &vm->globals);
    freeHashTable(vm, &vm->modCache);

    vm->globalCnt = 0;
    vm->curGlobal = NULL;
    vm->openUpvalues = NULL;
    freeObjects(vm);

    shutdownGC(vm);
}

void push(VM* vm, Value value){
    if(vm->stackTop - vm->stack >= STACK_MAX){
        runtimeError(vm, "Stack overflow");
        exit(EXIT_FAILURE);
    }
    *vm->stackTop++ = value;
}

Value pop(VM* vm){
    vm->stackTop--;
    return *vm->stackTop;
}

Value peek(VM* vm, int distance){
    if(vm->stackTop - vm->stack - 1 - distance < 0){
        runtimeError(vm, "Stack underflow");
        exit(EXIT_FAILURE);
    }
    return vm->stackTop[-1 - distance];
}

static void pushGlobal(VM* vm, GlobalEnv* globals){
    if(vm->globalCnt >= GLOBAL_STATCK_MAX){
        runtimeError(vm, "Too many nested imports, globals stack overflow.");
        return;
    }
    vm->globalStack[vm->globalCnt++] = vm->curGlobal;
    vm->curGlobal = globals;
}

static void popGlobal(VM* vm){
    if(vm->globalCnt <= 0){
        runtimeError(vm, "Global statck underflow.");
        return;
    }
    vm->curGlobal = vm->globalStack[--vm->globalCnt];
}

static bool isValidKey(Value key){
    if(IS_NUM(key)){
        double n = AS_NUM(key);
        return n == (int64_t)n;
    }
    return true;
}

static ObjectUpvalue* captureUpvalue(VM* vm, Value* local){
    ObjectUpvalue* prevUpvalue = NULL;
    ObjectUpvalue* upvalue = vm->openUpvalues;

    while(upvalue != NULL && upvalue->location > local){
        prevUpvalue = upvalue;
        upvalue = upvalue->next;
    }

    if(upvalue != NULL && upvalue->location == local){
        return upvalue;
    }

    ObjectUpvalue* createdUpvalue = newUpvalue(vm, local);
    createdUpvalue->next = upvalue;

    if(prevUpvalue == NULL){
        vm->openUpvalues = createdUpvalue;
    }else{
        prevUpvalue->next = createdUpvalue;
    }
    return createdUpvalue;
}

static void closeUpvalues(VM* vm, Value* last){
    while(vm->openUpvalues != NULL && vm->openUpvalues->location >= last){
        ObjectUpvalue* upvalue = vm->openUpvalues;
        upvalue->closed = *upvalue->location;
        upvalue->location = &upvalue->closed;
        vm->openUpvalues = upvalue->next;
    }
}

InterpreterStatus interpret(VM* vm, const char* code, const char* srcName){
    return interpretWithOpts(vm, code, srcName, NULL);
}

InterpreterStatus interpretWithOpts(VM* vm, const char* code, const char* srcName, const CompileOpts* opts){
    vm->lastError[0] = '\0';

    Value* stackBase = vm->stackTop;
    ObjectFunc* func = compileWithOpts(vm, code, srcName, opts);

    if(func == NULL){
        snprintf(vm->lastError, sizeof(vm->lastError), "Compilation failed.");
        return VM_COMPILE_ERROR;
    }

    push(vm, OBJECT_VAL(func));

    ObjectClosure* closure = newClosure(vm, func, vm->curGlobal);
    pop(vm);    // pop func
    push(vm, OBJECT_VAL(closure));

    if(!call(vm, closure, 0)){
        recover(vm);
        return VM_RUNTIME_ERROR;
    }

    InterpreterStatus status = run(vm);

    if(status == VM_RUNTIME_ERROR){
        recover(vm);
    }else{
        vm->stackTop = stackBase;
    }   // restore stack top to the base before the call

    return status;
}

static bool checkAccess(VM* vm, ObjectClass* instanceKlass, ObjectString* fieldName){
    // public
    if(isupper(fieldName->chars[0])){
        return true;
    }

    // private check
    if(vm->frameCount > 0){
        CallFrame* frame = &vm->frames[vm->frameCount - 1];
        ObjectFunc* func = frame->closure->func;
        if(func->fieldOwner == instanceKlass){
            return true;
        }
    }

    return false;
}

static Value bindListFunc(VM* vm, Value receiver, ObjectString* name){
    CFunc func = NULL;
    switch(name->length){
        case 3:
            if(memcmp(name->chars, "pop", 3) == 0){
                func = list_pop;
            }
            break;
        case 4:
            if(memcmp(name->chars, "push", 4) == 0){
                func = list_push;
            }else if(memcmp(name->chars, "size", 4) == 0){
                func = list_size;
            }
            break;
    }

    if(func){
        ObjectCFunc* cfuncObj = newCFunc(vm, func);
        push(vm, OBJECT_VAL(cfuncObj));
        ObjectBoundMethod* bound = newBoundMethod(vm, receiver, (Object*)cfuncObj);
        pop(vm);    // pop cfuncObj
        return OBJECT_VAL(bound);
    }

    return NULL_VAL;
}

static Value bindFileFunc(VM* vm, Value receiver, ObjectString* name){
    CFunc func = NULL;
    switch(name->length){
        case 4:
            if(memcmp(name->chars, "read", 4) == 0){
                func = file_read;
            }
            break;
        case 5:
            if(memcmp(name->chars, "close", 5) == 0){
                func = file_close;
            }else if(memcmp(name->chars, "write", 5) == 0){
                func = file_write;
            }
            break;
        case 8:
            if(memcmp(name->chars, "readLine", 8) == 0){
                func = file_readLine;
            }
            break;
    }

    if(func){
        ObjectCFunc* cfuncObj = newCFunc(vm, func);
        push(vm, OBJECT_VAL(cfuncObj));
        ObjectBoundMethod* bound = newBoundMethod(vm, receiver, (Object*)cfuncObj);
        pop(vm);    // pop cfuncObj
        return OBJECT_VAL(bound);
    }

    return NULL_VAL;
}

static Value bindStringFunc(VM* vm, Value receiver, ObjectString* name){
    CFunc func = NULL;
    if(name->length == 3){
        if(memcmp(name->chars, "len", 3) == 0){
            func = string_len;
        }else if(memcmp(name->chars, "sub", 3) == 0){
            func = string_sub;
        }
    }else if(name->length == 4){
        if(memcmp(name->chars, "trim", 4) == 0){
            func = string_trim;
        }else if(memcmp(name->chars, "find", 4) == 0){
            func = string_find;
        }
    }else if(name->length == 5){
        if(memcmp(name->chars, "upper", 5) == 0){
            func = string_upper;
        }else if(memcmp(name->chars, "lower", 5) == 0){
            func = string_lower;
        }else if(memcmp(name->chars, "split", 5) == 0){
            func = string_split;
        }
    }else if(name->length == 7){
        if(memcmp(name->chars, "replace", 7) == 0){
            func = string_replace;
        }
    }

    if(func){
        ObjectCFunc* cfuncObj = newCFunc(vm, func);
        push(vm, OBJECT_VAL(cfuncObj));
        ObjectBoundMethod* bound = newBoundMethod(vm, receiver, (Object*)cfuncObj);
        pop(vm);    // pop cfuncObj
        return OBJECT_VAL(bound);
    }
    return NULL_VAL;
}

static InterpreterStatus run(VM* vm){
    CallFrame* frame = &vm->frames[vm->frameCount - 1];

    Instruction instruction;

    static void* dispatchTable[] = {
        [OP_MOVE]           = &&DO_OP_MOVE,
        [OP_LOADK]          = &&DO_OP_LOADK,
        [OP_LOADNULL]       = &&DO_OP_LOADNULL,
        [OP_LOADBOOL]       = &&DO_OP_LOADBOOL,

        [OP_GET_GLOBAL]     = &&DO_OP_GET_GLOBAL,
        [OP_SET_GLOBAL]     = &&DO_OP_SET_GLOBAL,
        [OP_GET_UPVAL]      = &&DO_OP_GET_UPVAL,
        [OP_SET_UPVAL]      = &&DO_OP_SET_UPVAL,

        [OP_GET_INDEX]      = &&DO_OP_GET_INDEX,
        [OP_SET_INDEX]      = &&DO_OP_SET_INDEX,
        [OP_GET_PROPERTY]   = &&DO_OP_GET_PROPERTY,
        [OP_SET_PROPERTY]   = &&DO_OP_SET_PROPERTY,

        [OP_ADD]            = &&DO_OP_ADD,
        [OP_SUB]            = &&DO_OP_SUB,
        [OP_MUL]            = &&DO_OP_MUL,
        [OP_DIV]            = &&DO_OP_DIV,
        [OP_MOD]            = &&DO_OP_MOD,
        [OP_NEG]            = &&DO_OP_NEG,
        [OP_NOT]            = &&DO_OP_NOT,

        [OP_EQ]             = &&DO_OP_EQ,
        [OP_LT]             = &&DO_OP_LT,
        [OP_LE]             = &&DO_OP_LE,

        [OP_JMP]            = &&DO_OP_JMP,
        [OP_JMP_IF_FALSE]   = &&DO_OP_JMP_IF_FALSE,
        [OP_JMP_IF_TRUE]    = &&DO_OP_JMP_IF_TRUE,
        [OP_RETURN]         = &&DO_OP_RETURN,

        [OP_CLOSURE]        = &&DO_OP_CLOSURE,
        [OP_CLOSE_UPVAL]    = &&DO_OP_CLOSE_UPVAL,

        [OP_CLASS]          = &&DO_OP_CLASS,
        [OP_METHOD]         = &&DO_OP_METHOD,
        [OP_FIELD]          = &&DO_OP_FIELD,

        [OP_PRINT]          = &&DO_OP_PRINT,
        
        [OP_DEFER]          = &&DO_OP_DEFER,
        [OP_SYSTEM]         = &&DO_OP_SYSTEM,

        [OP_TO_STRING]      = &&DO_OP_TO_STRING,

        [OP_CALL]           = &&DO_OP_CALL,

        [OP_IMPORT]         = &&DO_OP_IMPORT,

        [OP_BUILD_LIST]     = &&DO_OP_BUILD_LIST,
        [OP_INIT_LIST]      = &&DO_OP_INIT_LIST,
        [OP_FILL_LIST]      = &&DO_OP_FILL_LIST,
        [OP_BUILD_MAP]      = &&DO_OP_BUILD_MAP,
        [OP_SLICE]          = &&DO_OP_SLICE,

        [OP_FOREACH]        = &&DO_OP_FOREACH,
    };

    #ifdef DEBUG_TRACE
        #define DISPATCH() \
            do { \
                instruction = *frame->ip++; \
                dasmInstruction(&frame->closure->func->chunk, \
                    (int)(frame->ip - frame->closure->func->chunk.code - 1), NULL); \
                goto *dispatchTable[GET_OPCODE(instruction)]; \
            } while (0)
    #else
        #define DISPATCH() \
            do { \
                instruction = *frame->ip++; \
                goto *dispatchTable[GET_OPCODE(instruction)]; \
            } while (0)
    #endif

    #define BI_OP(type, op) \
        do { \
            Value b = R(GET_ARG_B(instruction)); \
            Value c = R(GET_ARG_C(instruction)); \
            if(!IS_NUM(b) || !IS_NUM(c)){ \
                runtimeError(vm, "Operands must be numbers."); \
                return VM_RUNTIME_ERROR; \
            } \
            R(GET_ARG_A(instruction)) = type(AS_NUM(b) op AS_NUM(c)); \
        } while(false)

    DISPATCH();

    DO_OP_MOVE:
    {
        R(GET_ARG_A(instruction)) = R(GET_ARG_B(instruction));
    } DISPATCH();

    DO_OP_LOADK:
    {
        R(GET_ARG_A(instruction)) = K(GET_ARG_Bx(instruction));
    } DISPATCH();

    DO_OP_LOADNULL:
    {
        int a = GET_ARG_A(instruction);
        int b = GET_ARG_B(instruction);
        for(int i = 0; i <= b; i++){
            R(a + i) = NULL_VAL;
        }
    } DISPATCH();

    DO_OP_LOADBOOL:
    {
        R(GET_ARG_A(instruction)) = BOOL_VAL(GET_ARG_B(instruction));
        if(GET_ARG_C(instruction)) frame->ip++; // skip next instruction if C != 0
    } DISPATCH();

    DO_OP_GET_GLOBAL:
    {
        int a = GET_ARG_A(instruction);
        uint32_t slot = (uint32_t)GET_ARG_Bx(instruction);
        Value value;

        if(!globalGetSlot(frame->globals, slot, &value)){
            runtimeError(vm, "Undefined global variable.");
            return VM_RUNTIME_ERROR; 
        }
        R(a) = value;
    } DISPATCH();

    DO_OP_SET_GLOBAL:
    {
        int a = GET_ARG_A(instruction);
        uint32_t slot = (uint32_t)GET_ARG_Bx(instruction);

        if(!globalSetSlot(frame->globals, slot, R(a))){
            runtimeError(vm, "Undefined global variable.");
            return VM_RUNTIME_ERROR; 
        }
    } DISPATCH();

    DO_OP_GET_UPVAL:
    {
        R(GET_ARG_A(instruction)) = *frame->closure->upvalues[GET_ARG_B(instruction)]->location;
    } DISPATCH();

    DO_OP_SET_UPVAL:
    {
        *frame->closure->upvalues[GET_ARG_B(instruction)]->location = R(GET_ARG_A(instruction));
    } DISPATCH();

    DO_OP_GET_INDEX:
    {
        Value val = R(GET_ARG_B(instruction));
        Value key = R(GET_ARG_C(instruction));
        Value result = NULL_VAL;

        if(IS_LIST(val)){
            if(!IS_NUM(key)){
                runtimeError(vm, "List index must be a number.");
                return VM_RUNTIME_ERROR;
            }

            ObjectList* list = AS_LIST(val);

            int index = (int)AS_NUM(key);
            if(index < 0){
                index += list->count;
            }
            if(index < 0 || index >= list->count){
                runtimeError(vm, "List index out of range.");
                return VM_RUNTIME_ERROR;
            }
            result = list->items[index];
        }else if(IS_MAP(val)){
            ObjectMap* map = AS_MAP(val);
            if(!isValidKey(key)){
                runtimeError(vm, "Invalid map key.");
                return VM_RUNTIME_ERROR;
            }
            if(!tableGet(vm, &map->table, key, &result)){
                runtimeError(vm, "Key not found in map.");
                return VM_RUNTIME_ERROR;
            }
        }else if(IS_STRING(val)){
            if(!IS_NUM(key)){
                runtimeError(vm, "String index must be a number.");
                return VM_RUNTIME_ERROR;
            }

            ObjectString* str = AS_STRING(val);

            int index = (int)AS_NUM(key);
            if(index < 0){
                index += str->length;
            }
            if(index < 0 || index >= str->length){
                runtimeError(vm, "String index out of range.");
                return VM_RUNTIME_ERROR;
            }
            char chars[2] = {str->chars[index], '\0'};
            ObjectString* charStr = copyStringRaw(vm, chars, 1);
            result = OBJECT_VAL(charStr);
        }else{
            runtimeError(vm, "Only list and map type support indexing.");
            return VM_RUNTIME_ERROR;
        }

        R(GET_ARG_A(instruction)) = result;
    } DISPATCH();

    DO_OP_SET_INDEX:
    {
        Value cont = R(GET_ARG_A(instruction));
        Value key = R(GET_ARG_B(instruction));
        Value newVal = R(GET_ARG_C(instruction));

        if(IS_LIST(cont)){
            if(!IS_NUM(key)){
                runtimeError(vm, "List index must be a number.");
                return VM_RUNTIME_ERROR;
            }

            ObjectList* list = AS_LIST(cont);

            int index = (int)AS_NUM(key);
            if(index < 0){
                index += list->count;
            }
            if(index < 0 || index >= list->count){
                runtimeError(vm, "List index out of range.");
                return VM_RUNTIME_ERROR;
            }
            list->items[index] = newVal;
        }else if(IS_MAP(cont)){
            ObjectMap* map = AS_MAP(cont);
            if(!isValidKey(key)){
                runtimeError(vm, "Invalid map key.");
                return VM_RUNTIME_ERROR;
            }

            tableSet(vm, &map->table, key, newVal);
        }else{
            runtimeError(vm, "Only map type support key-value assignment.");
            return VM_RUNTIME_ERROR;
        }
    } DISPATCH();

    DO_OP_GET_PROPERTY:
    {
        Value instanceVal = R(GET_ARG_B(instruction));
        Value keyVal = R(GET_ARG_C(instruction));
        if(!IS_STRING(keyVal)){
            runtimeError(vm, "Property name must be a string.");
            return VM_RUNTIME_ERROR;
        }

        ObjectString* key = AS_STRING(keyVal);

        Value result = NULL_VAL;

        if(IS_INSTANCE(instanceVal)){
            ObjectInstance* instance = AS_INSTANCE(instanceVal);
            if(tableGet(vm, &instance->fields, OBJECT_VAL(key), &result)){
                if(!checkAccess(vm, instance->klass, key)){
                    runtimeError(vm, "Cannot access private field '%s'.", key->chars);
                    return VM_RUNTIME_ERROR;
                }
                R(GET_ARG_A(instruction)) = result;
            }else{
                Value methodVal;
                if(tableGet(vm, &instance->klass->methods, OBJECT_VAL(key), &methodVal)){
                    if(IS_CLOSURE(methodVal) || (IS_OBJECT(methodVal) && AS_OBJECT(methodVal)->type == OBJECT_CFUNC)){
                        ObjectBoundMethod* bound = newBoundMethod(vm, instanceVal, AS_OBJECT(methodVal));
                        R(GET_ARG_A(instruction)) = OBJECT_VAL(bound);
                    }else{
                        R(GET_ARG_A(instruction)) = methodVal;
                    }
                }else{
                    runtimeError(vm, "Undefined field '%s'.", key->chars);
                    return VM_RUNTIME_ERROR;
                }
            }
        }else if(IS_MODULE(instanceVal)){
            ObjectModule* module = AS_MODULE(instanceVal);
            if(!globalGetName(&module->members, key, &result)){
                runtimeError(vm, "Module has no member '%s'.", key->chars);
                return VM_RUNTIME_ERROR;
            }
            R(GET_ARG_A(instruction)) = result;
        }else{
            Value bound = NULL_VAL;
            
            if(IS_STRING(instanceVal)) bound = bindStringFunc(vm, instanceVal, key);
            else if(IS_LIST(instanceVal)) bound = bindListFunc(vm, instanceVal, key);
            else if(IS_FILE(instanceVal)) bound = bindFileFunc(vm, instanceVal, key);
            
            if(!IS_NULL(bound)){
                R(GET_ARG_A(instruction)) = bound;
            }else{
                runtimeError(vm, "Property '%s' not found on object.", key->chars); 
                return VM_RUNTIME_ERROR;
            }
        }
        
    } DISPATCH();

    DO_OP_SET_PROPERTY:
    {
        Value instanceVal = R(GET_ARG_A(instruction));
        Value keyVal = R(GET_ARG_B(instruction));
        if(!IS_STRING(keyVal)){
            runtimeError(vm, "Property name must be a string.");
            return VM_RUNTIME_ERROR;
        }
        ObjectString* key = AS_STRING(keyVal);
        Value newVal = R(GET_ARG_C(instruction));

        if(IS_INSTANCE(instanceVal)){
            ObjectInstance* instance = AS_INSTANCE(instanceVal);
            if(!checkAccess(vm, instance->klass, key)){
                runtimeError(vm, "Cannot access private field '%s'.", key->chars);
                return VM_RUNTIME_ERROR;
            }
            tableSet(vm, &instance->fields, OBJECT_VAL(key), newVal);
        }else if(IS_MODULE(instanceVal)){
            ObjectModule* module = AS_MODULE(instanceVal);

            if(!globalSetName(vm, &module->members, key, newVal)){
                runtimeError(vm, "Module has no member '%s'.", key->chars);
                return VM_RUNTIME_ERROR;
            }
        }else{
            runtimeError(vm, "Only instance and module support field assignment.");
            return VM_RUNTIME_ERROR;
        }
    } DISPATCH();

    DO_OP_TO_STRING:
    {
        int a = GET_ARG_A(instruction);
        int b = GET_ARG_B(instruction);
        Value val = R(b);

        if(IS_STRING(val)){
            R(a) = val;
        }else{
            ObjectString* str = toString(vm, val);
            R(a) = OBJECT_VAL(str);
        }
    } DISPATCH();

    DO_OP_NOT: {
        Value b = R(GET_ARG_B(instruction));
        R(GET_ARG_A(instruction)) = BOOL_VAL(!isTruthy(b));
    } DISPATCH();

    DO_OP_EQ:
    {
        Value b = R(GET_ARG_B(instruction));
        Value c = R(GET_ARG_C(instruction));
        int a = GET_ARG_A(instruction);
        if(isEqual(b, c) != a){
            frame->ip++;
        }
    } DISPATCH();

    DO_OP_LT:
    {
        Value b = R(GET_ARG_B(instruction));
        Value c = R(GET_ARG_C(instruction));
        int expect = GET_ARG_A(instruction);

        if(!IS_NUM(b) || !IS_NUM(c)){
            runtimeError(vm, "Operands must be numbers.");
            return VM_RUNTIME_ERROR;
        }

        if((AS_NUM(b) < AS_NUM(c)) != expect){
            frame->ip++;
        }
    } DISPATCH();

    DO_OP_LE:
    {
        Value b = R(GET_ARG_B(instruction));
        Value c = R(GET_ARG_C(instruction));
        int expect = GET_ARG_A(instruction);

        if(!IS_NUM(b) || !IS_NUM(c)){
            runtimeError(vm, "Operands must be numbers.");
            return VM_RUNTIME_ERROR;
        }

        if((AS_NUM(b) <= AS_NUM(c)) != expect){
            frame->ip++;
        }
    } DISPATCH();

    DO_OP_ADD: 
    {
        Value b = R(GET_ARG_B(instruction));
        Value c = R(GET_ARG_C(instruction));

        if(IS_NUM(b) && IS_NUM(c)){
            double result = AS_NUM(b) + AS_NUM(c);
            R(GET_ARG_A(instruction)) = NUM_VAL(result);
        }else if(IS_STRING(b) || IS_STRING(c)){
            push(vm, b);
            push(vm, c);

            ObjectString* bStr = IS_STRING(b) ? AS_STRING(b) : toString(vm, b);
            vm->stackTop[-2] = OBJECT_VAL(bStr);

            ObjectString* cStr = IS_STRING(c) ? AS_STRING(c) : toString(vm, c);
            vm->stackTop[-1] = OBJECT_VAL(cStr);

            size_t len = bStr->length + cStr->length;
            char* chars = (char*)reallocate(vm, NULL, 0, len + 1);  // add 1 for '\0'
            if(chars == NULL){
                runtimeError(vm, "Memory allocation failed for string concatenation.");
                return VM_RUNTIME_ERROR;
            }
            memcpy(chars, bStr->chars, bStr->length);
            memcpy(chars + bStr->length, cStr->chars, cStr->length);
            chars[len] = '\0';

            ObjectString* reStr = takeStringRaw(vm, chars, (int)len);

            pop(vm);
            pop(vm);

            R(GET_ARG_A(instruction)) = OBJECT_VAL(reStr);
        }else{
            runtimeError(vm, "Operands must be two numbers or two strings.");
            return VM_RUNTIME_ERROR;
        }
    } DISPATCH();

    DO_OP_SUB: BI_OP(NUM_VAL, -); DISPATCH();

    DO_OP_MUL: BI_OP(NUM_VAL, *); DISPATCH();

    DO_OP_DIV:
    {
        Value b = R(GET_ARG_B(instruction));
        Value c = R(GET_ARG_C(instruction));

        if(IS_NUM(b) && IS_NUM(c)){ 
            double v = AS_NUM(c);
            if(v == 0){
                runtimeError(vm, "Runtime error: Division by zero");
                return VM_RUNTIME_ERROR;
            }
            R(GET_ARG_A(instruction)) = NUM_VAL(AS_NUM(b) / v);
        }else if(IS_STRING(b) && IS_STRING(c)){
            ObjectString* aStr = AS_STRING(b);
            ObjectString* bStr = AS_STRING(c);

            #ifdef _WIN32
                #define PATH_SEP '\\'
                #define IS_SEP(c) ((c) == '\\' || (c) == '/')
            #else
                #define PATH_SEP '/'
                #define IS_SEP(c) ((c) == '/')
            #endif

            ObjectString* checkList[2] = {aStr, bStr};
            for(int j = 0; j < 2; j++){
                ObjectString* str = checkList[j];
                for(int i = 0; i < str->length; i++){
                    char c = str->chars[i];
                    bool invalid = false;
                    if(c < 32) invalid = true;
                    switch(c){
                        case '<': case '>': case '*': case '"': 
                        case '|': case '?': invalid = true; break;
                        case ':': 
                            #ifdef _WIN32
                            if (!(i == 1 && ((str->chars[0] >= 'a' && str->chars[0] <= 'z') || 
                                             (str->chars[0] >= 'A' && str->chars[0] <= 'Z')))) {
                                invalid = true;
                            }
                            #else
                            #endif
                            break;
                    }
                    if(invalid){
                        runtimeError(vm, "Path contains invalid characters.");
                        return VM_RUNTIME_ERROR;
                    }
                }
            }

            bool resetPath = false;
            #ifdef _WIN32
                if(bStr->length >= 2 && bStr->chars[1] == ':' && 
                   ((bStr->chars[0] >= 'a' && bStr->chars[0] <= 'z') || 
                    (bStr->chars[0] >= 'A' && bStr->chars[0] <= 'Z')
                )){
                    resetPath = true;
                }
                if(bStr->length > 0 && IS_SEP(bStr->chars[0])) resetPath = true; 
            #else
                if(bStr->length > 0 && IS_SEP(bStr->chars[0])) resetPath = true;
            #endif

            if(resetPath || aStr->length == 0){
                R(GET_ARG_A(instruction)) = OBJECT_VAL(bStr);
            }else if(bStr->length == 0){
                R(GET_ARG_A(instruction)) = OBJECT_VAL(aStr);
            }else{
                bool hasSepA = (aStr->length > 0 && IS_SEP(aStr->chars[aStr->length - 1]));
                bool hasSepB = (bStr->length > 0 && IS_SEP(bStr->chars[0]));

                size_t len = aStr->length + bStr->length - hasSepA - hasSepB + 1; 
                // add 1 for sep; it is the length of valid chars
                
                char* chars = (char*)reallocate(vm, NULL, 0, len + 1);  // add 1 for '\0'
                if(chars == NULL){
                    runtimeError(vm, "Memory allocation failed for path join.");
                    return VM_RUNTIME_ERROR;
                }

                memcpy(chars, aStr->chars, aStr->length);

                if(!hasSepA && !hasSepB){
                    chars[aStr->length] = PATH_SEP;
                    memcpy(chars + aStr->length + 1, bStr->chars, bStr->length);
                }else if(hasSepA && hasSepB){
                    memcpy(chars + aStr->length, bStr->chars + 1, bStr->length - 1);
                }else{
                    memcpy(chars + aStr->length, bStr->chars, bStr->length);
                }

                chars[len] = '\0';

                R(GET_ARG_A(instruction)) = OBJECT_VAL(takeStringRaw(vm, chars, (int)len));
            }
            #undef PATH_SEP
            #undef IS_SEP
        }else{
            runtimeError(vm, "Operands must be numbers or strings.");
            return VM_RUNTIME_ERROR;
        }

    } DISPATCH();

    DO_OP_NEG:
    {
        Value b = R(GET_ARG_B(instruction));
        if(!IS_NUM(b)){
            runtimeError(vm, "Operand must be a number.");
            return VM_RUNTIME_ERROR;
        }
        R(GET_ARG_A(instruction)) = NUM_VAL(-AS_NUM(b));
    } DISPATCH();

    DO_OP_MOD:
    {
        Value b = R(GET_ARG_B(instruction));
        Value c = R(GET_ARG_C(instruction));
        if(!IS_NUM(b) || !IS_NUM(c)){
            runtimeError(vm, "Operands must be numbers.");
            return VM_RUNTIME_ERROR;
        }
        double divisor = AS_NUM(c);
        if(divisor == 0){
            runtimeError(vm, "Runtime error: Division by zero");
            return VM_RUNTIME_ERROR;
        }
        R(GET_ARG_A(instruction)) = NUM_VAL(fmod(AS_NUM(b), divisor));
    } DISPATCH();

    DO_OP_PRINT:
    {
        int a = GET_ARG_A(instruction);
        valueWrite(R(a), &vm->output);
        vmWriteCString(vm, "\n");
    } DISPATCH();

    DO_OP_JMP:
    {
        int sBx = GET_ARG_sBx(instruction);
        frame->ip += sBx;
    } DISPATCH();

    DO_OP_JMP_IF_FALSE:
    {
        int reg = GET_ARG_A(instruction);
        int sBx = GET_ARG_sBx(instruction);
        if(!isTruthy(R(reg))){
            frame->ip += sBx;
        }
    } DISPATCH();

    DO_OP_JMP_IF_TRUE:
    {
        int reg = GET_ARG_A(instruction);
        int sBx = GET_ARG_sBx(instruction);
        if(isTruthy(R(reg))){
            frame->ip += sBx;
        }
    } DISPATCH();

    DO_OP_CALL:
    {
        int a = GET_ARG_A(instruction);
        int b = GET_ARG_B(instruction);

        Value callee = R(a);
        int argCount = b - 1;

        Value* oldStackTop = vm->stackTop;
        Value* callTop = &R(a + b);
        for(Value* slot = callTop; slot < oldStackTop; slot++){
            *slot = NULL_VAL;
        }
        vm->stackTop = callTop;

        int frameCnt = vm->frameCount;

        if(!callValue(vm, callee, argCount)){
            return VM_RUNTIME_ERROR;
        }

        if(vm->frameCount == frameCnt){ 
            // no new frame was pushed
            vm->stackTop = frame->base + frame->closure->func->maxRegSlots;
        }

        frame = &vm->frames[vm->frameCount - 1];
    } DISPATCH();

    DO_OP_IMPORT:
    {
        int a = GET_ARG_A(instruction);
        int bx = GET_ARG_Bx(instruction);
        ObjectString* spec = AS_STRING(K(bx));

        ImportResult result;
        const char* requester = frame->closure->func->srcName != NULL ? 
            frame->closure->func->srcName->chars : 
            NULL;

        if(importModule(vm, spec, requester, &result) != VM_OK){
            runtimeError(vm, "Failed to import module '%s'.", spec->chars);
            return VM_RUNTIME_ERROR;
        }

        R(a) = OBJECT_VAL(result.module);

        if(result.closure != NULL){
            pushGlobal(vm, &result.module->members);
            vm->stackTop[0] = OBJECT_VAL(result.closure);
            vm->stackTop++;

            if(!call(vm, result.closure, 0)){
                popGlobal(vm);
                result.module->status = MODULE_ERROR;
                return VM_RUNTIME_ERROR;
            }

            frame = &vm->frames[vm->frameCount - 1];
        }
    } DISPATCH();

    DO_OP_CLOSURE:
    {
        int a = GET_ARG_A(instruction);
        int bx = GET_ARG_Bx(instruction);
        ObjectFunc* func = AS_FUNC(K(bx));
        ObjectClosure* closure = newClosure(vm, func, frame->globals);
        if(closure == NULL){
            runtimeError(vm, "Out of memory creating closure");
            return VM_RUNTIME_ERROR;
        }
        R(a) = OBJECT_VAL(closure);

        for(int i = 0; i < closure->upvalueCnt; i++){
            Instruction nextInstruction = *frame->ip++;
            int isLocal = GET_ARG_B(nextInstruction);
            int index = GET_ARG_C(nextInstruction);
            if(isLocal){
                closure->upvalues[i] = captureUpvalue(vm, &R(index));
            }else{
                closure->upvalues[i] = frame->closure->upvalues[index];
            }
        }
    } DISPATCH();

    DO_OP_CLOSE_UPVAL:
    {
        int a = GET_ARG_A(instruction);
        closeUpvalues(vm, &R(a));
    } DISPATCH();

    DO_OP_CLASS:
    {
        int a = GET_ARG_A(instruction);
        int bx = GET_ARG_Bx(instruction);
        ObjectString* name = AS_STRING(K(bx));
        ObjectClass* klass = newClass(vm, name);
        if(klass == NULL){
            runtimeError(vm, "Out of memory creating class.");
            return VM_RUNTIME_ERROR;
        }
        R(a) = OBJECT_VAL(klass);
    } DISPATCH();

    DO_OP_METHOD:
    {
        int a = GET_ARG_A(instruction);
        int b = GET_ARG_B(instruction);
        int c = GET_ARG_C(instruction);

        ObjectClass* klass = AS_CLASS(R(a));
        Value nameVal = R(b);
        if(!IS_STRING(nameVal)){
            runtimeError(vm, "Method name must be a string.");
            return VM_RUNTIME_ERROR;
        }
        ObjectString* name = AS_STRING(nameVal);
        Value method = R(c);

        AS_CLOSURE(method)->func->fieldOwner = klass;
        tableSet(vm, &klass->methods, OBJECT_VAL(name), method);
    } DISPATCH();

    DO_OP_FIELD:
    {
        int a = GET_ARG_A(instruction);
        int b = GET_ARG_B(instruction);
        int c = GET_ARG_C(instruction);

        ObjectClass* klass = AS_CLASS(R(a));
        Value nameVal = R(b);
        if(!IS_STRING(nameVal)){
            runtimeError(vm, "Field name must be a string.");
            return VM_RUNTIME_ERROR;
        }
        tableSet(vm, &klass->fields, nameVal, R(c));
    } DISPATCH();

    DO_OP_BUILD_LIST:
    {
        int a = GET_ARG_A(instruction);
        int size = GET_ARG_B(instruction);

        ObjectList* list = newList(vm);
        push(vm, OBJECT_VAL(list)); // in case of GC during allocation
        
        if(size > 0){
            list->items = (Value*)reallocate(vm, NULL, 0, sizeof(Value) * size);
            list->capacity = size;
            list->count = 0;
        }
        pop(vm); // pop list after initialization
        
        R(a) = OBJECT_VAL(list);
    } DISPATCH();

    DO_OP_INIT_LIST:
    {
        int a = GET_ARG_A(instruction);
        int startReg = GET_ARG_B(instruction);
        int count = GET_ARG_C(instruction);

        Value listVal = R(a);
        if(!IS_LIST(listVal)){
            runtimeError(vm, "Operand must be a list.");
            return VM_RUNTIME_ERROR;
        }
        
        ObjectList* list = AS_LIST(listVal);

        if(count > 0){
            int needed = list->count + count;
            
            if(list->capacity < needed){
                int oldCapacity = list->capacity;
                int newCapacity = oldCapacity;
                
                while(newCapacity < needed){
                    newCapacity = GROW_CAPACITY(newCapacity);
                }
                
                list->items = (Value*)reallocate(vm, list->items, 
                    sizeof(Value) * oldCapacity, 
                    sizeof(Value) * newCapacity);
                list->capacity = newCapacity;
            }

            for(int k = 0; k < count; k++){
                list->items[list->count++] = R(startReg + k);
            }
        }
    } DISPATCH();

    DO_OP_FILL_LIST:
    {
        int a = GET_ARG_A(instruction);
        Value item = R(GET_ARG_B(instruction));
        Value countVal = R(GET_ARG_C(instruction));

        if(!IS_NUM(countVal)){
            runtimeError(vm, "List fill count must be a number.");
            return VM_RUNTIME_ERROR;
        }

        int count = (int)AS_NUM(countVal);
        if(count < 0){
            runtimeError(vm, "List fill count cannot be negative.");
            return VM_RUNTIME_ERROR;
        }

        ObjectList* list = newList(vm);

        push(vm, OBJECT_VAL(list)); // in case of GC during allocation
        
        if(count > 0){
            list->items = (Value*)reallocate(vm, NULL, 0, sizeof(Value) * count);
            list->capacity = count;
            list->count = count;
            
            for(int k = 0; k < count; k++){
                list->items[k] = item;
            }
        }
        pop(vm); // pop list after initialization
        
        R(a) = OBJECT_VAL(list);
    } DISPATCH();

    // R(A) = slice(R(B), start=R(C), end=R(C+1), step=R(C+2))
    // obj[start:end:step]
    DO_OP_SLICE: {
        int a = GET_ARG_A(instruction);
        int b = GET_ARG_B(instruction);
        int c = GET_ARG_C(instruction);

        Value receiver = R(b);
        Value startVal = R(c);
        Value endVal   = R(c + 1);
        Value stepVal  = R(c + 2);

        int length = 0;
        if(IS_STRING(receiver)){
            length = AS_STRING(receiver)->length;
        }else if (IS_LIST(receiver)){
            length = AS_LIST(receiver)->count;
        }else{
            runtimeError(vm, "Only lists and strings can be sliced.");
            return VM_RUNTIME_ERROR;
        }

        int step = 1;
        if(!IS_NULL(stepVal)){
            if(!IS_NUM(stepVal)){
                runtimeError(vm, "Slice step must be a number.");
                return VM_RUNTIME_ERROR;
            }
            step = (int)AS_NUM(stepVal);
        }
        
        if(step == 0){
            runtimeError(vm, "Slice step cannot be zero.");
            return VM_RUNTIME_ERROR;
        }

        int start;
        if(IS_NULL(startVal)){
            start = (step > 0) ? 0 : length - 1;
        }else{
            if(!IS_NUM(startVal)){
                runtimeError(vm, "Slice start must be a number."); 
                return VM_RUNTIME_ERROR; 
            }
            start = (int)AS_NUM(startVal);

            int originalStart = start;

            if(start < 0){
                start += length;
            }

            if(start < 0){
                #ifdef DEBUG_TRACE
                fprintf(stderr, "[Warn] Slice start %d underflow, clamped to %d.\n", originalStart, (step > 0) ? 0 : -1);
                #endif
                start = (step > 0) ? 0 : -1; 
            }else if(start >= length){
                #ifdef DEBUG_TRACE
                fprintf(stderr, "[Warn] Slice start %d overflow, clamped to %d.\n", originalStart, (step > 0) ? length : length - 1);
                #endif
                start = (step > 0) ? length : length - 1;
            }
        }

        int end;
        if(IS_NULL(endVal)){
            end = (step > 0) ? length : -1;
        }else{
            if(!IS_NUM(endVal)){
                runtimeError(vm, "Slice end must be a number."); 
                return VM_RUNTIME_ERROR; 
            }
            end = (int)AS_NUM(endVal);

            int originalEnd = end;

            if(end < 0){
                end += length;
            }

            if(end < 0){
                #ifdef DEBUG_TRACE
                fprintf(stderr, "[Warn] Slice end %d underflow, clamped to -1.\n", originalEnd);
                #endif
                end = -1;
            }else if(end > length){
                #ifdef DEBUG_TRACE
                fprintf(stderr, "[Warn] Slice end %d overflow, clamped to %d.\n", originalEnd, length);
                #endif
                end = length;
            }
        }

        int count = 0;
        if(step > 0){
            if(start < end){
                count = (end - start + step - 1) / step;
            }
        }else{
            if(start > end){
                count = (start - end - step - 1) / (-step);
            }
        }

        if(IS_STRING(receiver)){
            ObjectString* str = AS_STRING(receiver);
            if(count <= 0){
                R(a) = OBJECT_VAL(copyString(vm, "", 0));
            }else{
                char* chars = (char*)reallocate(vm, NULL, 0, count + 1);
                int destIdx = 0;
                int current = start;
                
                for(int k = 0; k < count; k++){
                    chars[destIdx++] = str->chars[current];
                    current += step;
                }
                chars[count] = '\0';
                R(a) = OBJECT_VAL(takeStringRaw(vm, chars, count));
            }
        }else{
            // List
            ObjectList* list = AS_LIST(receiver);
            ObjectList* newListObj = newList(vm);

            push(vm, OBJECT_VAL(newListObj)); // in case of GC during allocation
            
            if(count > 0){
                newListObj->items = (Value*)reallocate(vm, NULL, 0, sizeof(Value) * count);
                newListObj->capacity = count;
                newListObj->count = count;
                
                int destIdx = 0;
                int current = start;
                
                for(int k = 0; k < count; k++){
                    newListObj->items[destIdx++] = list->items[current];
                    current += step;
                }
            }
            pop(vm); // pop newListObj after initialization

            R(a) = OBJECT_VAL(newListObj);
        }

    } DISPATCH();

    DO_OP_FOREACH:
    {
        int a = GET_ARG_A(instruction);
        int sBx = GET_ARG_sBx(instruction);

        Value iter = R(a);
        Value state = R(a + 1);

        bool hasNext = false;

        if(IS_LIST(iter)){
            ObjectList* list = AS_LIST(iter);
            int index = IS_NUM(state) ? (int)AS_NUM(state) : 0;
            if(index < list->count){
                R(a + 2) = list->items[index];
                R(a + 1) = NUM_VAL(index + 1);
                hasNext = true;
            }
        }else if(IS_MAP(iter)){
            ObjectMap* map = AS_MAP(iter);
            int index = IS_NUM(state) ? (int)AS_NUM(state) : 0;
            while(index < map->table.capacity){
                if(!IS_NULL(map->table.entries[index].key)){
                    R(a + 2) = map->table.entries[index].key;
                    R(a + 1) = NUM_VAL(index + 1);
                    hasNext = true;
                    break;
                }
                index++;
            }
            if(!hasNext){
                R(a + 1) = NUM_VAL(index);
            }
        }else if(IS_STRING(iter)){
            ObjectString* str = AS_STRING(iter);
            int index = IS_NUM(state) ? (int)AS_NUM(state) : 0;
            if(index < str->length){
                char chars[2] = {str->chars[index], '\0'};
                R(a + 2) = OBJECT_VAL(copyStringRaw(vm, chars, 1));
                R(a + 1) = NUM_VAL(index + 1);
                hasNext = true;
            }
        }else{
            runtimeError(vm, "Object is not iterable.");
            return VM_RUNTIME_ERROR;
        }

        if(!hasNext){
            frame->ip += sBx;
        }
    } DISPATCH();

    DO_OP_BUILD_MAP:
    {
        int a = GET_ARG_A(instruction);
        R(a) = OBJECT_VAL(newMap(vm));        
    } DISPATCH();

    DO_OP_SYSTEM:
    {
        int a = GET_ARG_A(instruction);
        int b = GET_ARG_B(instruction);
        
        Value cmd = R(b);
        if(!IS_STRING(cmd)){
            runtimeError(vm, "System command expected to be a string.");
            return VM_RUNTIME_ERROR;
        }
        
        int rawStatus = system(AS_STRING(cmd)->chars);
        int status = normalizeSystemStatus(rawStatus);
        Value statusVal = NUM_VAL((double)status);
        R(a) = statusVal;

        ObjectString* exitKey = copyString(vm, "_exit_code", 10);
        globalSetName(vm, frame->globals, exitKey, statusVal);
    } DISPATCH();

    DO_OP_DEFER:
    {
        Value closureVal = R(GET_ARG_A(instruction));
        if(frame->deferCnt >= MAX_DEFERS){
            runtimeError(vm, "Exceeded maximum defer count (%d).", MAX_DEFERS);
            return VM_RUNTIME_ERROR;
        }
        if(!IS_CLOSURE(closureVal)){
            runtimeError(vm, "Defer operand must be a closure.");
            return VM_RUNTIME_ERROR;
        }
        frame->defers[frame->deferCnt++] = AS_CLOSURE(closureVal);
    } DISPATCH();

    DO_OP_RETURN:
    {
        int a = GET_ARG_A(instruction);
        int b = GET_ARG_B(instruction);

        if(frame->deferCnt > 0){
            ObjectClosure* deferClosure = frame->defers[--frame->deferCnt];
            frame->ip--;  // step back to re-execute return after defer

            vm->stackTop[0] = OBJECT_VAL(deferClosure);
            vm->stackTop++;

            call(vm, deferClosure, 0);
            frame = &vm->frames[vm->frameCount - 1];
            DISPATCH();
        }

        closeUpvalues(vm, &R(0));

        Value result = (b > 1) ? R(a) : NULL_VAL;

        Value* calleeBase = frame->base;

        if(frame->closure->func->type == TYPE_MODULE){
            popGlobal(vm);
        }

        vm->frameCount--;

        if(vm->frameCount == 0){
            vm->stackTop = calleeBase;
            push(vm, result);
            return VM_OK;
        }   // save the result

        frame = &vm->frames[vm->frameCount - 1];

        vm->stackTop = frame->base + frame->closure->func->maxRegSlots;

        calleeBase[0] = result; // place return value in caller's register 0
    } DISPATCH();
    
    #undef DISPATCH

}

static bool call(VM* vm, ObjectClosure* closure, int argCnt){
    if(argCnt != closure->func->arity){
        runtimeError(vm, "Expected %d args but got %d.", closure->func->arity, argCnt);
        return false;
    }

    if(vm->frameCount == FRAMES_MAX){
        runtimeError(vm, "Stack overflow.");
        return false;
    }

    Value* newBase = vm->stackTop - argCnt - 1; // -1 to skip func self
    if(newBase + closure->func->maxRegSlots >= vm->stack + STACK_MAX){
        runtimeError(vm, "Stack overflow.");
        return false;
    }

    CallFrame* frame = &vm->frames[vm->frameCount++];   // 0-indexing++ to actual count
    frame->closure = closure;
    frame->ip = closure->func->chunk.code;
    frame->base = newBase;
    frame->globals = closure->globals;
    frame->deferCnt = 0;

    for(int i = argCnt + 1; i < closure->func->maxRegSlots; i++){
        frame->base[i] = NULL_VAL;
    }
   
    vm->stackTop = frame->base + closure->func->maxRegSlots;

    return true;
}

static bool callValue(VM* vm, Value callee, int argCnt){
    if(IS_OBJECT(callee)){
        switch(OBJECT_TYPE(callee)){
            case OBJECT_CLASS:{
                ObjectClass* klass = AS_CLASS(callee);
                vm->stackTop[-argCnt - 1] = OBJECT_VAL(newInstance(vm, klass));
                Value initializer;
                if(tableGet(vm, &klass->methods, OBJECT_VAL(vm->initString), &initializer)){
                    if (!call(vm, AS_CLOSURE(initializer), argCnt)){
                        return false;
                    }
                }else if(argCnt != 0){  // no initializer found but got arguments
                    runtimeError(vm, "Expected 0 arguments but got %d.", argCnt);
                    return false;
                }
                return true;
            }
            case OBJECT_BOUND_METHOD:{
                ObjectBoundMethod* bound = AS_BOUND_METHOD(callee);
                vm->stackTop[-argCnt -1] = bound->receiver;
                Object* method = bound->method;
                if(method->type == OBJECT_CFUNC){
                    CFunc cfunc = AS_CFUNC(OBJECT_VAL(method));
                    Value result = cfunc(vm, argCnt, vm->stackTop - argCnt);

                    if(vm->hadRuntimeError){
                        return false;
                    }

                    vm->stackTop -= (argCnt + 1); 
                    push(vm, result);
                    return true;
                }else{
                    return call(vm, AS_CLOSURE(OBJECT_VAL(method)), argCnt);
                }
            }
            case OBJECT_CLOSURE:
                return call(vm, AS_CLOSURE(callee), argCnt);
            case OBJECT_CFUNC:{
                CFunc cfunc = AS_CFUNC(callee);
                Value result = cfunc(vm, argCnt, vm->stackTop - argCnt);

                if(vm->hadRuntimeError){  
                    return false;
                }

                vm->stackTop -= (argCnt + 1); 
                push(vm, result);
                return true;
            }
            default:
                break;
        }
    }
    runtimeError(vm, "Can only call functions and classes.");
    return false;
}

InterpreterStatus vmCallValue(
    VM* vm,
    Value callee,
    int argCnt,
    const Value* args,
    Value* result
){
    if(vm->frameCount != 0){
        runtimeError(vm, "Cieto VM calls are not reentrant.");
        return VM_RUNTIME_ERROR;
    }

    if(argCnt < 0 || (argCnt > 0 && args == NULL)){
        runtimeError(vm, "Invalid argument count or null arguments.");
        return VM_RUNTIME_ERROR;
    }

    Value* stackBase = vm->stackTop;

    push(vm, callee);

    for(int i = 0; i < argCnt; i++){
        push(vm, args[i]);
    }

    if(!callValue(vm, callee, argCnt)){
        recover(vm);
        return VM_RUNTIME_ERROR;
    }

    InterpreterStatus status = VM_OK;

    /*
     * Native C functions complete immediately, so we can return the result directly.
     * Cieto closures push a new frame onto the stack, and we need to run the VM loop to execute it.
    */

    if(vm->frameCount > 0){
        status = run(vm);
    }

    if(status == VM_RUNTIME_ERROR){
        recover(vm);
        return status;
    }

    if(vm->stackTop <= stackBase){
        runtimeError(vm, "Stack underflow after Cieto function call.");
        recover(vm);
        return VM_RUNTIME_ERROR;
    }

    Value returnValue = pop(vm);
    vm->stackTop = stackBase;  
    // restore stack top to the base before the call

    if(result != NULL){
        *result = returnValue;
    }

    return VM_OK;
}

void runtimeError(VM* vm, const char* format, ...){
    va_list args;
    va_start(args, format);
    vsnprintf(vm->lastError, sizeof(vm->lastError), format, args);
    va_end(args);
    vmWriteErrorCString(vm, vm->lastError);
    vmWriteErrorCString(vm, "\n");

    #ifdef DEBUG_TRACE
    if(vm->frameCount > 0){
        CallFrame* frame = &vm->frames[vm->frameCount - 1];
        size_t instructionOffset = frame->ip - frame->closure->func->chunk.code -1;
        
        int line = getLine(&frame->closure->func->chunk, instructionOffset); 

        const char* srcName = frame->closure->func->srcName != NULL 
                                 ? frame->closure->func->srcName->chars 
                                 : "<script>";
        fprintf(stderr, "Runtime error [%s, line %d]\n", srcName, line);
    }else{
        fprintf(stderr, "Runtime error [No stack trace available]\n");
    }
    #endif
    
    vm->hadRuntimeError = true;
}
