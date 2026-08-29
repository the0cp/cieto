#include "file.h"
#include "vm.h"
#include "compiler.h"
#include "chunk.h"
#include "debug.h"

static bool endsWith(const char* str, const char* suffix){
    if(!str || !suffix) return false;
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    if(suffix_len > str_len)    return false;
    return strncmp(str+str_len-suffix_len, suffix, suffix_len) == 0;
}

char* readScript(const char* path){
    FILE* file = fopen(path, "rb");
    if(!file){
        fprintf(stderr, "Could not open file %s\n", path);
        exit(EXIT_FAILURE);
    }
    fseek(file, 0L, SEEK_END);
    size_t size = ftell(file);
    rewind(file);

    char* content = malloc(size + 1);
    if(!content){
        fprintf(stderr, "Could not allocate memory for file content\n");
        fclose(file);
        exit(EXIT_FAILURE);
    }
    content[fread(content, 1, size, file)] = '\0';
    fclose(file);
    return content;
}

void runScript(VM* vm, const char* path) {
    runScriptWithOpts(vm, path, NULL);
}

void runScriptWithOpts(VM* vm, const char* path, const CompileOpts* opts) {
    char* content = readScript(path);
    InterpreterStatus status = interpretWithOpts(vm, content, path, opts);
    free(content);

    if(status == VM_COMPILE_ERROR) exit(EXIT_FAILURE);
    if(status == VM_RUNTIME_ERROR) exit(EXIT_FAILURE);
}

void buildScript(VM* vm, const char* path){
    char* source = readScript(path);

    Chunk chunk;
    initChunk(&chunk);

    ObjectFunc* func = compile(vm, source, path);
    if(func == NULL){
        free(source);
        exit(65);
    }

    char outputPath[1024];
    strncpy(outputPath, path, sizeof(outputPath) - 1);
    outputPath[sizeof(outputPath) - 1] = '\0';  // null end
    char* dot = strrchr(outputPath, '.');
    int base_len;
    if(dot != NULL){
        base_len = dot - outputPath;
    }else{
        base_len = strlen(path);
    }

    int written = snprintf(outputPath, sizeof(outputPath), "%.*s.pco", base_len, path);

    if(written < 0 || written >= sizeof(outputPath)){
        fprintf(stderr, "Error: Could not generate output path");
        free(source);
        freeChunk(vm, &chunk);
        exit(70);
    }

    printf("Compiling %s to %s...\n", path, outputPath);

    // serializeChunk();

    free(source);
    freeChunk(vm, &chunk);
    printf("Compilation successful.\n");
}

int dumpScript(VM* vm, const char* path){
    return dumpScriptWithOpts(vm, path, NULL);
}

int dumpScriptWithOpts(VM* vm, const char* path, const CompileOpts* opts){
    char* source = readScript(path);

    ObjectFunc* func = compileWithOpts(vm, source, path, opts);

    free(source);

    if(func == NULL){
        return 65;
    }

    dasmFunction(func, vm->curGlobal);
    return 0;
}
