#include <stdlib.h>

#include "chunk.h"
#include "mem.h"

void initChunk(Chunk* chunk){
    chunk->code = NULL;
    chunk->count = 0;
    chunk->capacity = 0;
    chunk->propertyCaches = NULL;
    chunk->propertyCacheCount = 0;
    chunk->propertyCacheCapacity = 0;
    chunk->propertyCacheMap = NULL;
    chunk->lines = NULL;
    chunk->lineCount = 0;
    chunk->lineCapacity = 0;
    initValueArray(&chunk->constants);
}

void writeChunk(VM* vm, Chunk* chunk, Instruction instruction, int line){
    if(chunk->count + 1 > chunk->capacity){
        size_t oldCapacity = chunk->capacity;
        chunk->capacity = GROW_CAPACITY(oldCapacity);
        chunk->code = GROW_ARRAY(
            vm,
            Instruction,
            chunk->code,
            oldCapacity,
            chunk->capacity
        );
        chunk->lines = GROW_ARRAY(
            vm,
            int,
            chunk->lines,
            oldCapacity * 2,
            chunk->capacity * 2
        );
        chunk->propertyCacheMap = GROW_ARRAY(
            vm,
            int,
            chunk->propertyCacheMap,
            oldCapacity,
            chunk->capacity
        );
    }
    chunk->code[chunk->count] = instruction;
    chunk->lines[chunk->count] = line;
    chunk->propertyCacheMap[chunk->count] = -1;
    chunk->count++;
    
}

void freeChunk(VM* vm, Chunk* chunk){
    FREE_ARRAY(vm, Instruction, chunk->code, chunk->capacity);
    FREE_ARRAY(vm, PropertyCache, chunk->propertyCaches, chunk->propertyCacheCapacity);
    FREE_ARRAY(vm, int, chunk->propertyCacheMap, chunk->capacity);
    FREE_ARRAY(vm, int, chunk->lines, chunk->lineCapacity * 2);
    freeValueArray(vm, &chunk->constants);
    initChunk(chunk);
}

int addConstant(VM* vm, Chunk* chunk, Value value){
    writeValueArray(vm, &chunk->constants, value);
    return (int)(chunk->constants.count - 1);
}

void addPropertyCache(VM* vm, Chunk* chunk, int instructionIndex){
    if(chunk->propertyCacheCount + 1 > chunk->propertyCacheCapacity){
        int oldCapacity = chunk->propertyCacheCapacity;
        chunk->propertyCacheCapacity = GROW_CAPACITY(oldCapacity);
        chunk->propertyCaches = GROW_ARRAY(
            vm,
            PropertyCache,
            chunk->propertyCaches,
            oldCapacity,
            chunk->propertyCacheCapacity
        );
    }

    int index = chunk->propertyCacheCount++;
    chunk->propertyCaches[index] = (PropertyCache){
        .klass = NULL_VAL,
        .method = NULL_VAL,
        .fieldSlot = -1
    };
    chunk->propertyCacheMap[instructionIndex] = index;
}
