#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "module_loader.h"
#include "object.h"
#include "value.h"
#include "registry.h"
#include "compiler.h"
#include "file.h"

#ifdef _WIN32
    #define PATH_SEP '\\'
    #define PATH_SEP_STR "\\"
#else
    #define PATH_SEP '/'
    #define PATH_SEP_STR "/"
#endif

static bool isPathSep(char c){
    return c == '/' || c == '\\';
}

static bool hasSep(const char* path){
    for(const char* p = path; *p != '\0'; p++){
        if(isPathSep(*p)) return true;
    }
    return false;
}

static bool isAbsPath(const char* path){
    if(path == NULL || path[0] == '\0'){
        return false;
    }

#ifdef _WIN32
    // Check for drive letter (e.g., C:\)
    if(((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
       path[1] == ':' && isPathSep(path[2])){
        return true;
    }
#endif

    // Check for Unix-style absolute path
    return isPathSep(path[0]);
}

static char* dupPath(const char* path){
    size_t len = strlen(path);
    char* copy = (char*)malloc(len + 1);
    if(copy != NULL){
        memcpy(copy, path, len + 1);
    }
    return copy;
}

static char* resolveScriptPath(const char* spec, const char* requester){
    if(spec == NULL || spec[0] == '\0'){
        return NULL;
    }

    if(isAbsPath(spec) || requester == NULL || requester[0] == '\0'){
        return dupPath(spec);
    }

    const char* lastSep = NULL;
    for(const char* p = requester; *p != '\0'; p++){
        if(isPathSep(*p)){
            lastSep = p;
        }
    }

    if(lastSep == NULL){
        return dupPath(spec);
    }

    size_t dirLen = (size_t)(lastSep - requester);
    if(dirLen == 0){
        dirLen = 1;  // Handle cases like "/module" or "C:\module"
    }

    bool needSep = dirLen > 0 && !isPathSep(requester[dirLen - 1]);
    size_t specLen = strlen(spec);
    size_t totalLen = dirLen + (needSep ? 1 : 0) + specLen;

    char* resolved = (char*)malloc(totalLen + 1);
    if(resolved == NULL){
        return NULL;
    }

    memcpy(resolved, requester, dirLen);
    size_t pos = dirLen;
    if(needSep){
        resolved[pos++] = PATH_SEP;
    }

    memcpy(resolved + pos, spec, specLen);
    resolved[totalLen] = '\0';
    return resolved;
}

inline static char normalizeSep(){
    return '/';  // Use '/' as the normalized separator
}

static char* normalizeScriptPath(const char* path){
    if(path == NULL){
        return NULL;
    }

    size_t len = strlen(path);
    char* work = (char*)malloc(len + 1);
    if(work == NULL){
        return NULL;
    }

    for(size_t i = 0; i < len; i++){
        char c = path[i];
        work[i] = isPathSep(c) ? normalizeSep() : c;
    }
    work[len] = '\0';

    char** parts = (char**)malloc(sizeof(char*) * (len + 1));
    if(parts == NULL){
        free(work);
        return NULL;
    }

    size_t partCount = 0;
    size_t prefixLen = 0;
    bool absolute = false;

#ifdef _WIN32
    if(len >= 3 &&
        ((work[0] >= 'A' && work[0] <= 'Z') || (work[0] >= 'a' && work[0] <= 'z')) &&
        work[1] == ':' && 
        isPathSep(work[2])){
            work[0] = (char)toupper((unsigned char)work[0]);
            prefixLen = 3;
            absolute = true;
    }else
#endif
    if(len >= 1 && isPathSep(work[0])){
        prefixLen = 1;
        absolute = true;
    }

    char* p = work + prefixLen;

    while(*p != '\0'){
        while(isPathSep(*p)){
            p++;
        }

        if(*p == '\0'){
            break;
        }

        char* start = p;

        while(*p != '\0' && !isPathSep(*p)){
            p++;
        }

        if(*p != '\0'){
            *p = '\0';
            p++;
        }

        if(strcmp(start, ".") == 0){
            continue;
        }

        if(strcmp(start, "..") == 0){
            if(partCount > 0 && strcmp(parts[partCount - 1], "..") != 0){
                partCount--;
            }else if(!absolute){
                parts[partCount++] = start;
            }
            continue;
        }

        parts[partCount++] = start;
    }

    char* result = (char*)malloc(len + 1);
    if(result == NULL){
        free(work);
        free(parts);
        return NULL;
    }

    size_t pos = 0;

    if(prefixLen > 0){
        memcpy(result, work, prefixLen);
        pos += prefixLen;

        if(pos > 0 && result[pos - 1] != normalizeSep()){
            result[pos++] = normalizeSep();
        }
    }

    for(size_t i = 0; i < partCount; i++){
        size_t partLen = strlen(parts[i]);

        if(pos > 0 && result[pos - 1] != normalizeSep()){
            result[pos++] = normalizeSep();
        }

        memcpy(result + pos, parts[i], partLen);
        pos += partLen;
    }

    if(pos == 0){
        result[pos++] = '.';
    }

    result[pos] = '\0';

    free(parts);
    free(work);

    return result;
}

static bool endWith(const char* str, const char* suffix){
    size_t len = strlen(str);
    size_t suffixLen = strlen(suffix);

    if(suffixLen > len) return false;

    return memcmp(str + len - suffixLen, suffix, suffixLen) == 0;
}

static bool isScriptModule(const char* str){
    return hasSep(str) || endWith(str, ".cies");
}

static ObjectString* getModuleName(VM* vm, const char* spec){
    const char* start = spec;

    for(const char* p = spec; *p != '\0'; p++){
        if(isPathSep(*p)){
            start = p + 1;
        }
    }

    const char* end = start + strlen(start);

    if(end - start >= 4 && strcmp(end - 4, ".cies") == 0){
        end -= 4;
    }else{
        for(const char* p = end; p > start; p--){
            if(*(p - 1) == '.'){
                end = p - 1;
                break;
            }
        }
    }

    if(end <= start){
        return copyString(vm, spec, (int)strlen(spec));
    }

    return copyString(vm, start, (int)(end - start));
}

static InterpreterStatus loadNativeModule(
    VM* vm, 
    const NativeModuleDef* nativeDef,
    ImportResult* result
){
    ObjectString* key = copyString(vm, nativeDef->name, (int)strlen(nativeDef->name));
    push(vm, OBJECT_VAL(key));

    Value cached;
    if(tableGet(vm, &vm->modCache, OBJECT_VAL(key), &cached)){
        result->module = AS_MODULE(cached);
        result->closure = NULL;
        pop(vm);    // key
        return VM_OK;
    }

    ObjectModule* module = newModule(vm, key, NULL, MODULE_NATIVE);
    push(vm, OBJECT_VAL(module));

    tableSet(vm, &vm->modCache, OBJECT_VAL(key), OBJECT_VAL(module));

    nativeDef->initFunc(vm, module);

    result->module = module;
    result->closure = NULL;

    pop(vm);    // module
    pop(vm);    // key

    return VM_OK;
}

static InterpreterStatus loadScriptModule(
    VM* vm,
    ObjectString* spec,
    ImportResult* result
){
    push(vm, OBJECT_VAL(spec));

    Value cache;
    if(tableGet(vm, &vm->modCache, OBJECT_VAL(spec), &cache)){
        result->module = AS_MODULE(cache);
        result->closure = NULL;
        pop(vm);    // spec
        return VM_OK;
    }

    char* source = readScript(spec->chars);
    if(source == NULL){
        runtimeError(vm, "Failed to read module file '%s'.", spec->chars);
        pop(vm);    // spec
        return VM_COMPILE_ERROR;
    }

    ObjectString* moduleName = getModuleName(vm, spec->chars);
    push(vm, OBJECT_VAL(moduleName));

    ObjectModule* module = newModule(vm, moduleName, spec, MODULE_SCRIPT);
    push(vm, OBJECT_VAL(module));

    GlobalEnv* prevGlobal = vm->curGlobal;
    vm->curGlobal = &module->members;
    registerPrelude(vm, &module->members);

    ObjectFunc* func = compile(vm, source, spec->chars);

    vm->curGlobal = prevGlobal;
    free(source);

    if(func == NULL){
        runtimeError(vm, "Failed to compile module '%s'.", spec->chars);
        pop(vm);    // module
        pop(vm);    // moduleName
        pop(vm);    // spec
        return VM_COMPILE_ERROR;
    }

    func->type = TYPE_MODULE;
    push(vm, OBJECT_VAL(func));

    tableSet(vm, &vm->modCache, OBJECT_VAL(spec), OBJECT_VAL(module));
    // cache module after compilation to avoid infinite recursion on circular imports

    ObjectClosure* closure = newClosure(vm, func, &module->members);
    if(closure == NULL){
        runtimeError(vm, "Failed to create closure for module '%s'.", spec->chars);
        pop(vm);    // func
        pop(vm);    // module
        pop(vm);    // moduleName
        pop(vm);    // spec
        return VM_COMPILE_ERROR;
    }

    result->module = module;
    result->closure = closure;

    pop(vm);    // func
    pop(vm);    // module
    pop(vm);    // moduleName
    pop(vm);    // spec

    return VM_OK;
}

InterpreterStatus importModule(
    VM* vm,
    ObjectString* spec,
    const char* requester,
    ImportResult* result
){
    result->module = NULL;
    result->closure = NULL;

    if(!isScriptModule(spec->chars)){
        const NativeModuleDef* nativeDef = findNativeModule(spec->chars);

        if(nativeDef != NULL){
            return loadNativeModule(vm, nativeDef, result);
        }

        runtimeError(vm, "Module '%s' not found.", spec->chars);
        return VM_COMPILE_ERROR;
    }

    char* resolvedPath = resolveScriptPath(spec->chars, requester);
    if(resolvedPath == NULL){
        runtimeError(vm, "Failed to resolve module path for '%s'.", spec->chars);
        return VM_COMPILE_ERROR;
    }

    char* normalizedPath = normalizeScriptPath(resolvedPath);
    free(resolvedPath);

    if(normalizedPath == NULL){
        runtimeError(vm, "Failed to normalize module path for '%s'.", spec->chars);
        return VM_COMPILE_ERROR;
    }

    ObjectString* resolvedSpec = copyString(vm, normalizedPath, (int)strlen(normalizedPath));
    free(normalizedPath);

    return loadScriptModule(vm, resolvedSpec, result);
}

