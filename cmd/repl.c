#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "repl.h"
#include "vm.h"

#ifndef _WIN32

#include "linenoise.h"

static const char* keywords[] = {
    "and", "break", "class", "continue", "default", "else", "false",
    "for", "func", "if", "import", "method", "null", "or",
    "return", "switch", "this", "true", "var", "while", "system",
    NULL
};

void completion(const char *buf, linenoiseCompletions *lc){
    size_t len = strlen(buf);

    for(int i = 0; keywords[i] != NULL; i++){
        if(strncmp(buf, keywords[i], len) == 0){
            linenoiseAddCompletion(lc, keywords[i]);
        }
    }
}
#endif

void repl(VM* vm){
#ifdef _WIN32
    char line[1024];
    printf("Cieto REPL. Press Ctrl+C to exit.\n");
    while(true){
        printf(">>> ");
        
        if(fgets(line, sizeof(line), stdin) == NULL){
            printf("\n");
            break; // EOF (Ctrl+Z)
        }

        size_t len = strlen(line);
        if(len > 0 && line[len - 1] == '\n'){
            line[len - 1] = '\0';
        }

        if(line[0] != '\0'){
            interpret(vm, line, "<stdin>");
        }
    }
#else
    char* line;
    const char* his = ".cieto_history";

    linenoiseSetCompletionCallback(completion);
    linenoiseHistorySetMaxLen(100);
    linenoiseHistoryLoad(his);

    printf("Cieto REPL. Press Ctrl+C to exit.\n");

    while((line = linenoise(">>> ")) != NULL){
        if(line[0] != '\0'){
            linenoiseHistoryAdd(line);
            linenoiseHistorySave(his);
            interpret(vm, line, "<stdin>");
        }
        free(line);
    }
#endif
}
