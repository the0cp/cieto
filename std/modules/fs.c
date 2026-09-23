#ifndef _WIN32
#define _DEFAULT_SOURCE
#endif
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h> // for _mkdir
#endif

#include "common.h"
#include "mem.h"
#include "vm.h"
#include "object.h"
#include "value.h"
#include "registry.h"
#include "fs.h"
#include "glob.h"

#ifdef _WIN32
    #include <windows.h>
    #define PATH_SEP '\\'
    #define PATH_SEP_STR "\\"
#else
    #include <dirent.h>
    #include <sys/stat.h>
    #define PATH_SEP '/'
    #define PATH_SEP_STR "/"
#endif

static bool is_excluded(VM* vm, const char* filename, Value excludeVal, bool ignoreCase);
static void scan_dir(VM* vm, const char* baseDir, const char* relDir, ObjectList* list, GlobConfig* config);

static void reportFileError(VM* vm, const char* action, const char* path, int errorCode){
    if(path != NULL){
        if(errorCode != 0){
            runtimeError(vm, "%s '%s': %s.", action, path, strerror(errorCode));
        }else{
            runtimeError(vm, "%s '%s'.", action, path);
        }
    }else if(errorCode != 0){
        runtimeError(vm, "%s: %s.", action, strerror(errorCode));
    }else{
        runtimeError(vm, "%s.", action);
    }
}

static bool readRemainingFile(VM* vm, FILE* file, char** content, size_t* length){
    size_t capacity = 1;
    size_t used = 0;
    char* buffer = (char*)malloc(capacity);
    if(buffer == NULL){
        runtimeError(vm, "Could not allocate memory for file content.");
        return false;
    }

    char chunk[4096];
    while(true){
        errno = 0;
        size_t readBytes = fread(chunk, 1, sizeof(chunk), file);
        if(readBytes == 0){
            break;
        }

        if(readBytes > (size_t)INT_MAX - used){
            free(buffer);
            runtimeError(vm, "File content is too large to read.");
            return false;
        }

        size_t required = used + readBytes + 1;
        if(required > capacity){
            size_t newCapacity = capacity;
            while(newCapacity < required){
                if(newCapacity > ((size_t)INT_MAX + 1) / 2){
                    newCapacity = required;
                    break;
                }
                newCapacity *= 2;
            }

            char* resized = (char*)realloc(buffer, newCapacity);
            if(resized == NULL){
                free(buffer);
                runtimeError(vm, "Could not allocate memory for file content.");
                return false;
            }
            buffer = resized;
            capacity = newCapacity;
        }

        memcpy(buffer + used, chunk, readBytes);
        used += readBytes;
    }

    if(ferror(file)){
        int errorCode = errno;
        free(buffer);
        reportFileError(vm, "Could not read file", NULL, errorCode);
        return false;
    }

    buffer[used] = '\0';
    *content = buffer;
    *length = used;
    return true;
}

static bool writeAndCloseFile(VM* vm, FILE* file, ObjectString* content, const char* path){
    errno = 0;
    size_t written = fwrite(content->chars, 1, (size_t)content->length, file);
    int writeError = errno;
    bool writeFailed = written != (size_t)content->length || ferror(file);

    errno = 0;
    int closeResult = fclose(file);
    int closeError = errno;

    if(writeFailed){
        reportFileError(vm, "Could not write all content to file", path, writeError);
        return false;
    }
    if(closeResult != 0){
        reportFileError(vm, "Could not close file after writing", path, closeError);
        return false;
    }
    return true;
}

#define GET_FILE(val) \
    if(!IS_FILE(val)){ \
        runtimeError(vm, "Expected a file object.\n"); \
        return NULL_VAL; \
    } \
    ObjectFile* fileObj = AS_FILE(val); \
    if(!fileObj->isOpen || fileObj->handle == NULL){ \
        runtimeError(vm, "File is not open.\n"); \
        return NULL_VAL; \
    }

Value file_read(VM* vm, int argCount, Value* args){
    GET_FILE(args[-1]);
    if(argCount != 0){
        runtimeError(vm, "file.read expects no arguments.");
        return NULL_VAL;
    }

    char* content;
    size_t length;
    if(!readRemainingFile(vm, fileObj->handle, &content, &length)){
        return NULL_VAL;
    }

    Value result = OBJECT_VAL(copyString(vm, content, (int)length));
    free(content);
    return result;
}

Value file_close(VM* vm, int argCount, Value* args){
    GET_FILE(args[-1]);
    if(argCount != 0){
        runtimeError(vm, "file.close expects no arguments.");
        return NULL_VAL;
    }

    FILE* handle = fileObj->handle;
    fileObj->isOpen = false;
    fileObj->handle = NULL;

    errno = 0;
    if(fclose(handle) != 0){
        reportFileError(vm, "Could not close file", NULL, errno);
    }
    return NULL_VAL;
}

Value file_write(VM* vm, int argCount, Value* args){
    GET_FILE(args[-1]);
    if(argCount != 1 || !IS_STRING(args[0])){
        runtimeError(vm, "file.write expects a single string argument.\n");
        return NULL_VAL;
    }

    ObjectString* content = AS_STRING(args[0]);
    errno = 0;
    size_t written = fwrite(content->chars, 1, (size_t)content->length, fileObj->handle);
    if(written != (size_t)content->length || ferror(fileObj->handle)){
        reportFileError(vm, "Could not write all content to file", NULL, errno);
        return NULL_VAL;
    }

    errno = 0;
    if(fflush(fileObj->handle) != 0){
        reportFileError(vm, "Could not flush file content", NULL, errno);
    }
    return NULL_VAL;
}

Value file_readLine(VM* vm, int argCount, Value* args){
    GET_FILE(args[-1]);
    if(argCount != 0){
        runtimeError(vm, "file.readLine expects no arguments.");
        return NULL_VAL;
    }

    size_t capacity = 128;
    size_t length = 0;
    char* buffer = (char*)reallocate(vm, NULL, 0, capacity);
    int c;
    errno = 0;
    while(true){
        c = fgetc(fileObj->handle);
        if(c == EOF || c == '\n'){
            break;
        }
        if(c == '\r')   continue;

        if(length + 1 >= capacity){
            size_t old = capacity;
            capacity *= 2;
            buffer = (char*)reallocate(vm, buffer, old, capacity);
        }
        buffer[length++] = (char)c;
    }

    if(c == EOF && ferror(fileObj->handle)){
        int errorCode = errno;
        reallocate(vm, buffer, capacity, 0);
        reportFileError(vm, "Could not read line from file", NULL, errorCode);
        return NULL_VAL;
    }

    if(length == 0 && c == EOF){
        reallocate(vm, buffer, capacity, 0);
        return NULL_VAL;
    }

    ObjectString* lineStr = copyString(vm, buffer, (int)length);
    reallocate(vm, buffer, capacity, 0);
    return OBJECT_VAL(lineStr);
}

static Value fs_open(VM* vm, int argCount, Value* args){
    if(argCount < 1 || argCount > 2 || !IS_STRING(args[0]) ||
       (argCount == 2 && !IS_STRING(args[1]))){
        runtimeError(vm, "fs.open expects a path string and an optional mode string.");
        return NULL_VAL;
    }

    char* path = AS_CSTRING(args[0]);
    char* mode = "r";
    if(argCount == 2){
        mode = AS_CSTRING(args[1]);
    }

    errno = 0;
    FILE* file = fopen(path, mode);
    if(!file){
        int errorCode = errno;
        if(errorCode != 0){
            runtimeError(vm, "Could not open file '%s' with mode '%s': %s.",
                         path, mode, strerror(errorCode));
        }else{
            runtimeError(vm, "Could not open file '%s' with mode '%s'.", path, mode);
        }
        return NULL_VAL;
    }
    return OBJECT_VAL(newFile(vm, file));
}


static Value fs_readFile(VM* vm, int argCount, Value* args){
    if(argCount != 1 || !IS_STRING(args[0])){
        runtimeError(vm, "fs.read expects a single string argument.\n");
        return NULL_VAL;
    }

    char* path = AS_CSTRING(args[0]);
    errno = 0;
    FILE* file = fopen(path, "rb");
    if(!file){
        reportFileError(vm, "Could not open file", path, errno);
        return NULL_VAL;
    }

    char* content;
    size_t length;
    if(!readRemainingFile(vm, file, &content, &length)){
        fclose(file);
        return NULL_VAL;
    }

    errno = 0;
    if(fclose(file) != 0){
        int errorCode = errno;
        free(content);
        reportFileError(vm, "Could not close file after reading", path, errorCode);
        return NULL_VAL;
    }

    Value result = OBJECT_VAL(copyString(vm, content, (int)length));
    free(content);
    return result;
}

static Value fs_readLines(VM* vm, int argCount, Value* args){
    // read all lines
    if(argCount != 1 || !IS_STRING(args[0])){
        runtimeError(vm, "fs.rline expects a single string argument.\n");
        return NULL_VAL;
    }

    char* path = AS_CSTRING(args[0]);
    errno = 0;
    FILE* file = fopen(path, "r");
    if(!file){
        reportFileError(vm, "Could not open file", path, errno);
        return NULL_VAL;
    }

    ObjectList* list = newList(vm);
    push(vm, OBJECT_VAL(list));

    char line[1024];
    errno = 0;
    while(fgets(line, sizeof(line), file)){
        size_t len = strlen(line);
        if(len > 0 && line[len - 1] == '\n'){
            line[len - 1] = '\0';
            len--;
        }

        ObjectString* lineStr = copyString(vm, line, (int)len);
        push(vm, OBJECT_VAL(lineStr));
        appendToList(vm, list, OBJECT_VAL(lineStr));
        pop(vm);
    }

    if(ferror(file)){
        int errorCode = errno;
        fclose(file);
        pop(vm);
        reportFileError(vm, "Could not read file", path, errorCode);
        return NULL_VAL;
    }

    errno = 0;
    if(fclose(file) != 0){
        int errorCode = errno;
        pop(vm);
        reportFileError(vm, "Could not close file after reading", path, errorCode);
        return NULL_VAL;
    }

    pop(vm);
    return OBJECT_VAL(list);
}

static Value fs_writeFile(VM* vm, int argCount, Value* args){
    if(argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])){
        runtimeError(vm, "fs.write expects two string arguments.\n");
        return NULL_VAL;
    }

    char* path = AS_CSTRING(args[0]);
    ObjectString* content = AS_STRING(args[1]);

    errno = 0;
    FILE* file = fopen(path, "wb");
    if(!file){
        reportFileError(vm, "Could not open file for writing", path, errno);
        return NULL_VAL;
    }

    if(!writeAndCloseFile(vm, file, content, path)){
        return NULL_VAL;
    }

    return BOOL_VAL(true);
}

static Value fs_appendFile(VM* vm, int argCount, Value* args){
    if(argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])){
        runtimeError(vm, "fs.append expects path and content strings.\n");
        return NULL_VAL;
    }

    char* path = AS_CSTRING(args[0]);
    ObjectString* content = AS_STRING(args[1]);

    errno = 0;
    FILE* file = fopen(path, "ab");
    if(!file){
        reportFileError(vm, "Could not open file for appending", path, errno);
        return NULL_VAL;
    }

    if(!writeAndCloseFile(vm, file, content, path)){
        return NULL_VAL;
    }
    return BOOL_VAL(true);
}

static Value fs_exists(VM* vm, int argCount, Value* args){
    if(argCount != 1 || !IS_STRING(args[0])){
        runtimeError(vm, "fs.exists expects a single string argument.\n");
        return NULL_VAL;
    }

    char* path = AS_CSTRING(args[0]);
    FILE* file = fopen(path, "rb");
    if(file){
        fclose(file);
        return BOOL_VAL(true);
    }
    return BOOL_VAL(false);
}

static Value fs_remove(VM* vm, int argCount, Value* args){
    if(argCount != 1 || !IS_STRING(args[0])){
        runtimeError(vm, "fs.remove expects a single string argument.\n");
        return NULL_VAL;
    }

    char* path = AS_CSTRING(args[0]);
    if(remove(path) == 0){
        return BOOL_VAL(true);
    }else{
        return BOOL_VAL(false);
    }
}

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

static Value fs_listDir(VM* vm, int argCount, Value* args){
    if(argCount != 1){
        runtimeError(vm, "fs.list expects a single argument.\n");
        return NULL_VAL;
    }

    GlobConfig config;

    config.pattern = "*";
    config.ignoreCase = false;
    config.excludeVal = NULL_VAL;
    config.recursive = false;
    const char* baseDir = ".";

    if(IS_STRING(args[0])){
        baseDir = AS_CSTRING(args[0]);
    }else if(IS_INSTANCE(args[0])){
        ObjectInstance* instant = AS_INSTANCE(args[0]);
        Value val;
        
        if(tableGet(vm, &instant->fields, OBJECT_VAL(copyString(vm, "Dir", 3)), &val) && IS_STRING(val)){
            baseDir = AS_CSTRING(val);
        }
        if(tableGet(vm, &instant->fields, OBJECT_VAL(copyString(vm, "Pattern", 7)), &val) && IS_STRING(val)){
            config.pattern = AS_CSTRING(val);
        }
        if(tableGet(vm, &instant->fields, OBJECT_VAL(copyString(vm, "IgnoreCase", 10)), &val) && IS_BOOL(val)){
            config.ignoreCase = AS_BOOL(val);
        }
        if(tableGet(vm, &instant->fields, OBJECT_VAL(copyString(vm, "Exclude", 7)), &val)){
            config.excludeVal = val;
        }
        if(tableGet(vm, &instant->fields, OBJECT_VAL(copyString(vm, "Recursive", 9)), &val) && IS_BOOL(val)){
            config.recursive = AS_BOOL(val);
        }
    }else{
        runtimeError(vm, "fs.list argument must be a string or a Glob object.\n");
        return NULL_VAL;
    }

    ObjectList* list = newList(vm);
    push(vm, OBJECT_VAL(list));

    scan_dir(vm, baseDir, "", list, &config);
    pop(vm);
    return OBJECT_VAL(list);
}

static Value fs_mkdir(VM* vm, int argCount, Value* args){
    if(argCount != 1 || !IS_STRING(args[0])){
        runtimeError(vm, "fs.mkdir expects a single string argument.\n");
        return NULL_VAL;
    }

    char* path = AS_CSTRING(args[0]);
    int result;

    #ifdef _WIN32
    result = _mkdir(path);
    #else
    // POSIX mkdir with 0755 permission
    result = mkdir(path, 0755);
    #endif

    return BOOL_VAL(result == 0);
}

static Value fs_isDir(VM* vm, int argCount, Value* args){
    if(argCount != 1 || !IS_STRING(args[0])){
        runtimeError(vm, "fs.isdir expects a single string argument.\n");
        return NULL_VAL;
    }

    char* path = AS_CSTRING(args[0]);
    struct stat statbuf;

    if(stat(path, &statbuf) != 0){
        return BOOL_VAL(false);
    }

    if(S_ISDIR(statbuf.st_mode)){
        return BOOL_VAL(true);
    }

    return BOOL_VAL(false);
}

static bool is_excluded(VM* vm, const char* filename, Value excludeVal, bool ignoreCase){
    if(IS_NUM(excludeVal))  return false;
    if(IS_STRING(excludeVal))   return glob_match_string(filename, AS_CSTRING(excludeVal), ignoreCase);
    if(IS_LIST(excludeVal)){
        ObjectList* list = AS_LIST(excludeVal);
        for(int i = 0; i < list->count; i++){
            if(IS_STRING(list->items[i])){
                if(glob_match_string(filename, AS_CSTRING(list->items[i]), ignoreCase)){
                    return true;
                }
            }
        }
    }
    return false;
}

static void scan_dir(VM* vm, const char* baseDir, const char* relDir, ObjectList* list, GlobConfig* config){
    char fullPath[2048];
    if(strlen(relDir) == 0){
        snprintf(fullPath, sizeof(fullPath), "%s", baseDir);
    }else{
        snprintf(fullPath, sizeof(fullPath), "%s%c%s", baseDir, PATH_SEP, relDir);
    }
#ifdef _WIN32
    char searchPath[2048];
    snprintf(searchPath, sizeof(searchPath), "%s\\*.*", fullPath);

    WIN32_FIND_DATA fd;
    HANDLE hFind = FindFirstFile(searchPath, &fd);
    
    if(hFind != INVALID_HANDLE_VALUE){
        do {
            if(strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) continue;
            
            char relPath[1024];
            if(strlen(relDir) == 0){
                snprintf(relPath, sizeof(relPath), "%s", fd.cFileName);
            }else{
                snprintf(relPath, sizeof(relPath), "%s\\%s", relDir, fd.cFileName);
            }

            bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);

            if(glob_match_string(relPath, config->pattern, config->ignoreCase)){
                if(!is_excluded(vm, relPath, config->excludeVal, config->ignoreCase)){
                    ObjectString* str = copyString(vm, relPath, strlen(relPath));
                    push(vm, OBJECT_VAL(str));
                    appendToList(vm, list, OBJECT_VAL(str));
                    pop(vm);
                }
            }

            if(isDir && config->recursive){
                scan_dir(vm, baseDir, relPath, list, config);
            }

        }while(FindNextFile(hFind, &fd));
        FindClose(hFind);
    }
#else
    DIR* dir = opendir(fullPath);
    if (!dir) return;

    struct dirent* ent;
    while((ent = readdir(dir)) != NULL){
        if(strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;

        char relPath[1024];
        if(strlen(relDir) == 0){
            snprintf(relPath, sizeof(relPath), "%s", ent->d_name);
        }else{
            snprintf(relPath, sizeof(relPath), "%s/%s", relDir, ent->d_name);
        }

        bool isDir = false;

    #ifdef _DIRENT_HAVE_D_TYPE
        if(ent->d_type == DT_DIR) isDir = true;
        else if(ent->d_type == DT_UNKNOWN){
             struct stat st;
             char absStatPath[2048];
             snprintf(absStatPath, sizeof(absStatPath), "%s/%s", fullPath, ent->d_name);
             if (stat(absStatPath, &st) == 0 && S_ISDIR(st.st_mode)) isDir = true;
        }
    #else
        struct stat st;
        char absStatPath[2048];
        snprintf(absStatPath, sizeof(absStatPath), "%s/%s", fullPath, ent->d_name);
        if(stat(absStatPath, &st) == 0 && S_ISDIR(st.st_mode)) isDir = true;
    #endif
        if(glob_match_string(relPath, config->pattern, config->ignoreCase)){
            if(!is_excluded(vm, relPath, config->excludeVal, config->ignoreCase)){
                ObjectString* str = copyString(vm, relPath, strlen(relPath));
                push(vm, OBJECT_VAL(str));
                appendToList(vm, list, OBJECT_VAL(str));
                pop(vm);
            }
        }

        if(isDir && config->recursive){
            scan_dir(vm, baseDir, relPath, list, config);
        }
    }
    closedir(dir);
#endif
}

void initFsModule(VM* vm, ObjectModule* module){
    defineCFunc(vm, &module->members, "read", fs_readFile);
    defineCFunc(vm, &module->members, "write", fs_writeFile);
    defineCFunc(vm, &module->members, "exists", fs_exists);
    defineCFunc(vm, &module->members, "remove", fs_remove);
    defineCFunc(vm, &module->members, "list", fs_listDir);
    defineCFunc(vm, &module->members, "rlines", fs_readLines);
    defineCFunc(vm, &module->members, "append", fs_appendFile);
    defineCFunc(vm, &module->members, "open", fs_open);
    defineCFunc(vm, &module->members, "mkdir", fs_mkdir);
    defineCFunc(vm, &module->members, "isDir", fs_isDir);
}
