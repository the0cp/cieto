#ifndef FILE_H
#define FILE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vm.h"

char* readScript(const char* path);
void runScript(VM* vm, const char* path);
void runScriptWithOpts(VM* vm, const char* path, const CompileOpts* opts);
void buildScript(VM* vm, const char* path);
int dumpScript(VM* vm, const char* path);
int dumpScriptWithOpts(VM* vm, const char* path, const CompileOpts* opts);

#endif // FILE_H
