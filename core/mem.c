#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "mem.h"
#include "compiler.h"
#include "gc_policy.h"

#define GC_HEAP_GROW_FACTOR 2
// #define GC_MIN_THRESHOLD 1024 * 1024 * 10

static double nowMs(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

bool gcMarkSweep(VM* vm, GCReason reason){
    (void)reason;

    if(vm->bytesAllocated == 0){
        return false;
    }

    double startMs = nowMs();

#ifdef DEBUG_LOG_GC
    printf("-- gc begin --\n");
#endif

    size_t before = vm->bytesAllocated;

    double phaseStart = nowMs();
    markRoots(vm);
    double markMs = nowMs() - phaseStart;

#ifdef DEBUG_LOG_GC
    printf("Marked objects. Starting sweep...\n");
#endif

    phaseStart = nowMs();
    tableRemoveWhite(vm, &vm->strings);
    double internMs = nowMs() - phaseStart;

    phaseStart = nowMs();
    sweep(vm);
    double sweepMs = nowMs() - phaseStart;

    double elapsedMs = nowMs() - startMs;

    vm->gcStats.count++;
    vm->gcStats.bytesBefore = before;
    vm->gcStats.bytesAfter = vm->bytesAllocated;
    vm->gcStats.bytesTotalFreed += before - vm->bytesAllocated;
    vm->gcStats.totalMs += elapsedMs;
    vm->gcStats.markMs += markMs;
    vm->gcStats.internMs += internMs;
    vm->gcStats.sweepMs += sweepMs;

    if(elapsedMs > vm->gcStats.maxPauseMs){
        vm->gcStats.maxPauseMs = elapsedMs;
    }

    vm->nextGC = vm->bytesAllocated * GC_HEAP_GROW_FACTOR;

    if(vm->nextGC < vm->gcThreshold){
        vm->nextGC = vm->gcThreshold;
    }

#ifdef DEBUG_LOG_GC
    printf("-- sweep finished. Collected %zu bytes (from %zu to %zu) next at %zu --\n", 
        before - vm->bytesAllocated, 
        before, 
        vm->bytesAllocated, 
        vm->nextGC);
    printf("-- gc end --\n");
#endif

    return true;
}

void collectGarbage(VM* vm){
    (void)gcCollect(vm, GC_REASON_MANUAL);
}

static void traceRef(VM* vm, Object* object){
    switch(object->type){
        case OBJECT_LIST:{
            ObjectList* list = (ObjectList*)object;
            for(int i = 0; i < list->count; i++){
                markValue(vm, list->items[i]);
            }
            break;
        }
        case OBJECT_MAP:{
            ObjectMap* map = (ObjectMap*)object;
            markTable(vm, &map->table);
            break;
        }
        case OBJECT_FUNC:{
            ObjectFunc* func = (ObjectFunc*)object;
            markObject(vm, (Object*)func->name);
            markObject(vm, (Object*)func->srcName);
            markArray(vm, &func->chunk.constants);
            break;
        }
        case OBJECT_CLOSURE:{
            ObjectClosure* closure = (ObjectClosure*)object;
            markObject(vm, (Object*)closure->func);
            for(int i = 0; i < closure->upvalueCnt; i++){
                markObject(vm, (Object*)closure->upvalues[i]);
            }
            break;
        }
        case OBJECT_UPVALUE:{
            markValue(vm, ((ObjectUpvalue*)object)->closed);
            break;
        }
        case OBJECT_MODULE:{
            ObjectModule* module = (ObjectModule*)object;
            markObject(vm, (Object*)module->name);
            markObject(vm, (Object*)module->path);
            markGlobalEnv(vm, &module->members);
            break;
        }
        case OBJECT_CLASS:{
            ObjectClass* klass = (ObjectClass*)object;
            markObject(vm, (Object*)klass->name);
            markTable(vm, &klass->methods);
            markTable(vm, &klass->fields);
            break;
        }
        case OBJECT_INSTANCE:{
            ObjectInstance* instance = (ObjectInstance*)object;
            markObject(vm, (Object*)instance->klass);
            markTable(vm, &instance->fields);
            break;
        }
        case OBJECT_BOUND_METHOD:{
            ObjectBoundMethod* bound = (ObjectBoundMethod*)object;
            markValue(vm, bound->receiver);
            markObject(vm, (Object*)bound->method);
            break;
        }
        case OBJECT_ITERATOR:{
            ObjectIterator* iterator = (ObjectIterator*)object;
            markValue(vm, iterator->receiver);
            markValue(vm, iterator->current);
            break;
        }
        case OBJECT_STRING:
        case OBJECT_CFUNC:  
        case OBJECT_FILE:
        case OBJECT_STRING_BUILDER:
            break;
    }
}

void markObject(VM* vm, Object* object){
    if(object == NULL)  return;
    if(object->isMarked)    return;

    object->isMarked = true;
    traceRef(vm, object);
}

void markValue(VM* vm, Value value){
    if(IS_OBJECT(value)){
        markObject(vm, AS_OBJECT(value));
    }
}

void markArray(VM* vm, ValueArray* array){
    for(int i = 0; i < array->count; i++){
        markValue(vm, array->values[i]);
    }
}

static void markRoots(VM* vm){
    for(Value* slot = vm->stack; slot < vm->stackTop; slot++){
        markValue(vm, *slot);
    }

    for(int i = 0; i < vm->frameCount; i++){
        markObject(vm, (Object*)vm->frames[i].closure);

        for(int j = 0; j < vm->frames[i].deferCnt; j++){
            markObject(vm, (Object*)vm->frames[i].defers[j]);
        }
    }

    for(ObjectUpvalue* upvalue = vm->openUpvalues; upvalue != NULL; upvalue = upvalue->next){
        markObject(vm, (Object*)upvalue);
    }

    markGlobalEnv(vm, &vm->globals);

    if(vm->curGlobal != NULL && vm->curGlobal != &vm->globals){
        markGlobalEnv(vm, vm->curGlobal);
    }

    for(int i = 0; i < vm->globalCnt; i++){
        GlobalEnv* env = vm->globalStack[i];
    
        if(env != NULL && env != &vm->globals && env != vm->curGlobal){
            markGlobalEnv(vm, env);
        }
    }

    markTable(vm, &vm->modCache);

    if(vm->compiler != NULL){
        markCompilerRoots(vm);
    }

    markObject(vm, (Object*)vm->initString);
}

static void sweep(VM* vm){
    Object* prev = NULL;
    Object* object = vm->objects;
    
    while(object != NULL){
        if(object->isMarked){
            prev = object;
            object = object->next;
        }else{
            Object* unreached = object;
            object = object->next;
            if(prev != NULL){
                prev->next = object;
            }else{
                vm->objects = object;
            }
            freeObject(vm, unreached);
        }
    }
    
    object = vm->objects;
    while(object != NULL){
        object->isMarked = false;
        object = object->next;
    }
}

void* reallocate(VM* vm, void* ptr, size_t oldSize, size_t newSize){
    if(newSize >= oldSize){
        vm->bytesAllocated += newSize - oldSize;
    }else{
        vm->bytesAllocated -= oldSize - newSize;
    }
    
    gcOnAlloc(vm, ptr, oldSize, newSize);

    if(newSize == 0){
        free(ptr);
        return NULL;
    }

    void* newPtr = realloc(ptr, newSize);
    if(newPtr == NULL){
        exit(EXIT_FAILURE);
    }
    return newPtr;
}
