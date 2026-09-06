#ifndef CIETO_MODULES_PROCESS_H
#define CIETO_MODULES_PROCESS_H

#include <stdbool.h>
#include <stddef.h>

typedef struct VM VM;
typedef struct ObjectMap ObjectMap;
typedef struct ObjectModule ObjectModule;

typedef struct{
    char* data;
    size_t len;
    size_t capacity;
}ProcBuffer;

typedef struct{
    const char* cwd;
    ObjectMap* env;
}ProcOpts;

void freeProcBuffer(ProcBuffer* buf);
bool appendProcBuffer(ProcBuffer* buf, const char* data, size_t len);
bool appendProcChar(ProcBuffer* buf, char ch);

bool runProc(
    VM* vm,
    char** argv,
    const ProcOpts* opts,
    int* code,
    ProcBuffer* out,
    ProcBuffer* err
);

void initProcessModule(VM* vm, ObjectModule* module);

#endif
