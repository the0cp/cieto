#include "common.h"
#include "vm.h"
#include "repl.h"
#include "file.h"
#include "version.h"

static void printVersion(void){
    printf("Cieto %s\n", CIETO_VERSION);
}

static void printHelp(const char* programName){
    printf("Cieto %s\n", CIETO_VERSION);
    printf("\n");
    printf("A compact C-family scripting language and embeddable register-based virtual machine implemented entirely in C.\n");
    printf("\n");
    printf("Usage:\n");
    printf("  %s                         Start the REPL\n", programName);
    printf("  %s <file.cies> [args...]    Run a script\n", programName);
    printf("  %s run <file.cies> [args...] Run a script\n", programName);
    printf("  %s --no-opt <file.cies> [args...] Run a script without compiler optimizations\n", programName);
    printf("  %s --dump, -d <file.cies>   Compile and dump bytecode\n", programName);
    printf("  %s --no-opt --dump <file.cies> Compile and dump without compiler optimizations\n", programName);
    printf("  %s --help                  Show this help message\n", programName);
    printf("  %s --version               Show version information\n", programName);
    printf("\n");
    printf("Examples:\n");
    printf("  %s examples/tour.cies\n", programName);
    printf("  %s examples/argv_echo.cies hello world\n", programName);
}

int main(int argc, const char* argv[]){
    if(argc >= 2){
        if(strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0){
            printHelp(argv[0]);
            return 0;
        }

        if(strcmp(argv[1], "--version") == 0 || strcmp(argv[1], "-v") == 0){
            printVersion();
            return 0;
        }
    }
    
    VM vm;

    if(argc == 1){
        initVM(&vm, 0, NULL);
        repl(&vm);
    }else{
        int scriptArgsSt = 1;
        bool noOpt = false;

        if(strcmp(argv[scriptArgsSt], "--no-opt") == 0){
            noOpt = true;
            scriptArgsSt++;
        }

        CompileOpts opts = {false};

        if(scriptArgsSt < argc && (strcmp(argv[scriptArgsSt], "--dump") == 0 || strcmp(argv[scriptArgsSt], "-d") == 0)){
            scriptArgsSt++;

            if(scriptArgsSt < argc && strcmp(argv[scriptArgsSt], "--no-opt") == 0){
                noOpt = true;
                scriptArgsSt++;
            }

            if(scriptArgsSt + 1 != argc){
                printHelp(argv[0]);
                return 64;
            }

            initVM(&vm, 0, NULL);

            int status = dumpScriptWithOpts(&vm, argv[scriptArgsSt], noOpt ? &opts : NULL);

            freeVM(&vm);
            return status;
        }

        if(scriptArgsSt < argc && strcmp(argv[scriptArgsSt], "run") == 0){
            scriptArgsSt++;
        }

        if(scriptArgsSt < argc && strcmp(argv[scriptArgsSt], "--no-opt") == 0){
            noOpt = true;
            scriptArgsSt++;
        }
        
        if(scriptArgsSt >= argc){
            fprintf(stderr, "Usage: %s [run] [script] [args...]\n", argv[0]);
            fprintf(stderr, "Try '%s --help' for more information.\n", argv[0]);
            return 64;
        }

        initVM(&vm, argc - scriptArgsSt, argv + scriptArgsSt);
        runScriptWithOpts(&vm, argv[scriptArgsSt], noOpt ? &opts : NULL);
    }
    
    freeVM(&vm);
    return 0;
}
