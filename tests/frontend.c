#include <stdio.h>
#include <string.h>

#include "compiler.h"
#include "scanner.h"
#include "vm.h"

typedef struct{
    int count;
    DiagSeverity severity;
    SourceSpan span;
    const char* expectedSource;
    bool sourceMatches;
    char srcName[32];
    char message[64];
}DiagCapture;

static void captureDiag(const Diagnostic* diag, void* userData){
    DiagCapture* capture = (DiagCapture*)userData;
    if(capture->count++ > 0){
        return;
    }

    capture->severity = diag->severity;
    capture->span = diag->span;
    capture->sourceMatches = diag->source == capture->expectedSource;
    snprintf(capture->srcName, sizeof(capture->srcName), "%s", diag->srcName);
    snprintf(capture->message, sizeof(capture->message), "%s", diag->message);
}

static bool tokenIs(Token token, TokenType type, const char* text){
    size_t len = strlen(text);
    return token.type == type && token.len == (int)len &&
           memcmp(token.head, text, len) == 0;
}

static int testScanner(void){
    Scanner left;
    Scanner right;
    initScanner(&left, "var left = 1;\nprint left;");
    initScanner(&right, "print \"right\";");

    Token token = scan(&left);
    if(!tokenIs(token, TOKEN_VAR, "var") ||
       token.span.start.line != 1 || token.span.start.column != 1 ||
       token.span.end.column != 4){
        fprintf(stderr, "Unexpected first token from left scanner.\n");
        return 1;
    }

    token = scan(&right);
    if(!tokenIs(token, TOKEN_PRINT, "print") ||
       token.span.start.line != 1 || token.span.start.column != 1){
        fprintf(stderr, "Unexpected first token from right scanner.\n");
        return 1;
    }

    token = scan(&left);
    if(!tokenIs(token, TOKEN_IDENTIFIER, "left") ||
       token.span.start.column != 5){
        fprintf(stderr, "Left scanner lost its position.\n");
        return 1;
    }

    token = scan(&right);
    if(token.type != TOKEN_STRING_START || token.span.start.column != 7){
        fprintf(stderr, "Right scanner lost its position.\n");
        return 1;
    }

    while((token = scan(&left)).type != TOKEN_PRINT){
        if(token.type == TOKEN_EOF || token.type == TOKEN_ERROR){
            fprintf(stderr, "Left scanner did not reach the second line.\n");
            return 1;
        }
    }
    if(token.span.start.line != 2 || token.span.start.column != 1){
        fprintf(stderr, "Unexpected second-line token position.\n");
        return 1;
    }

    return 0;
}

static int testDiagnostic(void){
    const char* source =
        "var value = ;\n"
        "var other = ;";
    VM vm;
    initVM(&vm, 0, NULL);

    DiagCapture capture = {.expectedSource = source};
    DiagSink sink = {captureDiag, &capture};
    ObjectFunc* func = compileWithDiag(&vm, source, "diag.cies", NULL, &sink);

    int failed = 0;
    if(func != NULL || capture.count != 2){
        fprintf(stderr, "Expected two structured compile diagnostics, got %d.\n", capture.count);
        failed = 1;
    }else if(capture.severity != DIAG_ERROR ||
             strcmp(capture.srcName, "diag.cies") != 0 ||
             !capture.sourceMatches ||
             capture.span.start.line != 1 || capture.span.start.column != 13 ||
             strcmp(capture.message, "Expect expression") != 0){
        fprintf(stderr, "Unexpected structured diagnostic fields.\n");
        failed = 1;
    }

    const char* lexSource = "@";
    capture = (DiagCapture){.expectedSource = lexSource};
    func = compileWithDiag(&vm, lexSource, "lex.cies", NULL, &sink);
    if(func != NULL || capture.count != 1 || vm.compiler != NULL ||
       capture.span.start.column != 1 ||
       strcmp(capture.message, "Unrecognized character.") != 0){
        fprintf(stderr, "Lexical error did not cleanly finish compilation.\n");
        failed = 1;
    }

    const char* deferSource = "func test() { defer print ; }";
    capture = (DiagCapture){.expectedSource = deferSource};
    func = compileWithDiag(&vm, deferSource, "defer.cies", NULL, &sink);
    if(func != NULL || capture.count != 1 || vm.compiler != NULL ||
       !capture.sourceMatches ||
       strcmp(capture.srcName, "defer.cies") != 0 ||
       capture.span.start.line != 1 || capture.span.start.column != 27 ||
       strcmp(capture.message, "Expect expression") != 0){
        fprintf(stderr, "Nested defer error did not cleanly finish compilation.\n");
        failed = 1;
    }

    capture = (DiagCapture){0};
    func = compileWithDiag(&vm, "", "empty.cies", NULL, &sink);
    if(func == NULL || capture.count != 0 || vm.compiler != NULL){
        fprintf(stderr, "Empty source did not compile as an empty script.\n");
        failed = 1;
    }

    freeVM(&vm);
    return failed;
}

int main(void){
    if(testScanner() != 0 || testDiagnostic() != 0){
        return 1;
    }

    printf("Frontend tests passed.\n");
    return 0;
}
