#ifndef CIETO_CHUNK_H
#define CIETO_CHUNK_H

#include "common.h"
#include "value.h"
#include "instruction.h"

typedef struct PropertyCache{
    Value klass;
    Value method;
    int fieldSlot;
}PropertyCache;

typedef struct Chunk {
    Instruction* code;
    size_t count;
    size_t capacity;
    ValueArray constants;
    PropertyCache* propertyCaches;
    int propertyCacheCount;
    int propertyCacheCapacity;
    int* propertyCacheMap;
    int* lines;
    int lineCount;
    int lineCapacity;
} Chunk;

void initChunk(Chunk* chunk);
void writeChunk(VM* vm, Chunk* chunk, Instruction instruction, int line);
void freeChunk(VM* vm, Chunk* chunk);

int addConstant(VM* vm, Chunk* chunk, Value value);
void addPropertyCache(VM* vm, Chunk* chunk, int instructionIndex);

#endif  // CIETO_CHUNK_H
