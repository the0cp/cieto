#ifndef CIETO_HASHTABLE_H
#define CIETO_HASHTABLE_H

#include "common.h"
#include "value.h"

typedef struct ObjectString ObjectString;

typedef struct{
    Value key;
    Value value;
}Entry;

typedef struct{
    int count;
    int capacity;
    Entry* entries;
    uint64_t version;
}HashTable;

void initHashTable(HashTable* table);
void freeHashTable(VM* vm, HashTable* table);

bool tableGet(VM* vm, HashTable* table, Value key, Value* value);
bool tableSet(VM* vm, HashTable* table, Value key, Value value);
bool tableRemove(VM* vm, HashTable* table, Value key);
bool tableMerge(VM* vm, HashTable* from, HashTable* to);

static inline bool tableNextEntry(HashTable* table, int* cursor, Entry** entry){
    while(*cursor < table->capacity){
        Entry* candidate = &table->entries[(*cursor)++];
        if(!IS_EMPTY(candidate->key)){
            *entry = candidate;
            return true;
        }
    }

    *entry = NULL;
    return false;
}

ObjectString* tableGetInternedString(VM* vm, HashTable* table, const char* chars, int len, uint64_t hash);
void tableRemoveWhite(VM* vm, HashTable* table);
void markTable(VM* vm, HashTable* table);

static uint64_t hashValue(Value value, uint64_t seed);

#endif
