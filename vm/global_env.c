#include "global_env.h"

#include "mem.h"
#include "object.h"
#include "vm.h"

#define GLOBAL_NAME_MAX_LOAD 0.75

/*
 * use linear probing for the name map, and store values in a separate array indexed by the slot number.
 * this allows us to keep the name map compact and efficient, 
 * while still allowing for fast lookups and updates of global variables by slot number.
 * a GlobalNameMap is append-only, so we don't need to worry about tombstones or deleted entries in the name map. 
 * when we need to resize the name map, we can simply rehash all the existing entries into a new array with a larger capacity.
 * Robin Hood hashing is another option, but it adds complexity and may not provide significant performance benefits for our use case, 
 * since we expect the number of global variables to be relatively small and the load factor to be low.
 * linear probing is also great for cache locality, which can improve performance for small maps like this.
*/

void initGlobalEnv(GlobalEnv* env){
    env->names.count = 0;
    env->names.capacity = 0;
    env->names.entries = NULL;
    env->values = NULL;
    env->count = 0;
    env->capacity = 0;
}

static void clearNameEntry(GlobalNameEntry* entry){
    entry->name = NULL;
    entry->slot = 0;
}

static GlobalNameEntry* findNameEntry(GlobalNameEntry* entries, size_t capacity, ObjectString* name){
    size_t index = (size_t)(name->hash & (uint64_t)(capacity - 1));
    
    for(;;){
        GlobalNameEntry* entry = &entries[index];
        if(entry->name == NULL || entry->name == name){
            return entry;
        }
        index = (index + 1) & (capacity - 1);
        // use & instead of % because capacity is always a power of 2, 
        // so this wraps around to the beginning of the array when it reaches the end.
    }
}

static void adjustNameMap(VM* vm, GlobalNameMap* map, size_t newCapacity){
    GlobalNameEntry* entries = GROW_ARRAY(vm, GlobalNameEntry, NULL, 0, newCapacity);

    for(size_t i = 0; i < newCapacity; i++){
        clearNameEntry(&entries[i]);
    }

    size_t oldCount = map->count;

    for(size_t i = 0; i < map->capacity; i++){
        GlobalNameEntry* entry = &map->entries[i];
        if(entry->name == NULL){
            continue;
        }

        GlobalNameEntry* dest = findNameEntry(entries, newCapacity, entry->name);
        *dest = *entry;
    }

    FREE_ARRAY(vm, GlobalNameEntry, map->entries, map->capacity);
    map->entries = entries;
    map->capacity = newCapacity;
    map->count = oldCount;
}

static bool ensureValueCapacity(VM* vm, GlobalEnv* env, size_t minCapacity){
    if(env->count >= minCapacity){
        return true;
    }

    size_t oldCapacity = env->capacity;
    size_t newCapacity = GROW_CAPACITY(oldCapacity);

    while(newCapacity < minCapacity){
        newCapacity = GROW_CAPACITY(newCapacity);
    }

    env->values = GROW_ARRAY(vm, Value, env->values, oldCapacity, newCapacity);

    for(size_t i = oldCapacity; i < newCapacity; i++){
        env->values[i] = EMPTY_VAL;
    }

    env->capacity = newCapacity;
    return true;
}

void freeGlobalEnv(VM* vm, GlobalEnv* env){
    FREE_ARRAY(vm, GlobalNameEntry, env->names.entries, env->names.capacity);
    FREE_ARRAY(vm, Value, env->values, env->capacity);
    initGlobalEnv(env);
}

bool globalResolveSlot(GlobalEnv* env, ObjectString* name, uint32_t* slot){
    if(env->names.count == 0 || env->names.capacity == 0){
        return false;
    }

    GlobalNameEntry* entry = findNameEntry(env->names.entries, env->names.capacity, name);
    if(entry->name == NULL){
        return false;
    }

    if(slot != NULL){
        *slot = entry->slot;
    }

    return true;
}

bool globalEnsureSlot(VM* vm, GlobalEnv* env, ObjectString* name, uint32_t* slot){
    uint32_t existingSlot;
    if(globalResolveSlot(env, name, &existingSlot)){
        if(slot != NULL){
            *slot = existingSlot;
        }
        return true;
    }

    push(vm, OBJECT_VAL(name));

    if(env->count > UINT32_MAX){
        pop(vm);
        return false;
    }

    if(env->names.count + 1 > env->names.capacity * GLOBAL_NAME_MAX_LOAD){
        size_t newCapacity = GROW_CAPACITY(env->names.capacity);
        adjustNameMap(vm, &env->names, newCapacity);
    }

    if(!ensureValueCapacity(vm, env, env->count + 1)){
        pop(vm);
        return false;
    }

    uint32_t newSlot = (uint32_t)env->count;
    GlobalNameEntry* entry = findNameEntry(env->names.entries, env->names.capacity, name);

    entry->name = name;
    entry->slot = newSlot;
    env->names.count++;

    env->values[newSlot] = EMPTY_VAL;
    env->count++;

    if(slot != NULL){
        *slot = newSlot;
    }

    pop(vm);
    return true;
}

bool globalGetSlot(GlobalEnv* env, uint32_t slot, Value* out){
    if((size_t)slot >= env->count){
        return false;
    }

    Value value = env->values[slot];
    if(IS_EMPTY(value)){
        return false;
    }

    if(out != NULL){
        *out = value;
    }

    return true;
}

bool globalSetSlot(GlobalEnv* env, uint32_t slot, Value value){
    if((size_t)slot >= env->count){
        return false;
    }

    env->values[slot] = value;
    return true;
}

bool globalGetName(GlobalEnv* env, ObjectString* name, Value* out){
    uint32_t slot;
    if(!globalResolveSlot(env, name, &slot)){
        return false;
    }
    return globalGetSlot(env, slot, out);
}

bool globalSetName(VM* vm, GlobalEnv* env, ObjectString* name, Value value){
    uint32_t slot;
    if(!globalEnsureSlot(vm, env, name, &slot)){
        return false;
    }
    return globalSetSlot(env, slot, value);
}

void markGlobalEnv(VM* vm, GlobalEnv* env){
    if(env == NULL){
        return;
    }

    for(size_t i = 0; i < env->names.capacity; i++){
        GlobalNameEntry* entry = &env->names.entries[i];
        if(entry->name != NULL){
            markObject(vm, (Object*)entry->name);
        }
    }

    for(size_t i = 0; i < env->count; i++){
        if(!IS_EMPTY(env->values[i])){
            markValue(vm, env->values[i]);
        }
    }
}
