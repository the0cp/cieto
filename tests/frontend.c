#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "compiler.h"
#include "scanner.h"
#include "vm.h"

#define TEST_SOURCE_CAP (32 * 1024)

typedef struct{
    int count;
    DiagSeverity severity;
    SourceSpan span;
    const char* expectedSource;
    bool sourceMatches;
    char srcName[32];
    char message[64];
}DiagCapture;

typedef struct{
    char data[TEST_SOURCE_CAP];
    size_t len;
}SourceBuf;

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

static bool compileRejects(VM* vm, const char* source, const char* srcName, const char* message){
    DiagCapture capture = {.expectedSource = source};
    DiagSink sink = {captureDiag, &capture};
    ObjectFunc* func = compileWithDiag(vm, source, srcName, NULL, &sink);
    return func == NULL && capture.count == 1 && vm->compiler == NULL &&
           capture.sourceMatches && strcmp(capture.srcName, srcName) == 0 &&
           strcmp(capture.message, message) == 0;
}

static bool appendSource(SourceBuf* source, const char* format, ...){
    if(source->len >= sizeof(source->data)){
        return false;
    }

    va_list args;
    va_start(args, format);
    int written = vsnprintf(
        source->data + source->len,
        sizeof(source->data) - source->len,
        format,
        args
    );
    va_end(args);

    if(written < 0 || (size_t)written >= sizeof(source->data) - source->len){
        return false;
    }
    source->len += (size_t)written;
    return true;
}

static bool chunkHasOpB(const Chunk* chunk, OpCode op, int b){
    for(size_t i = 0; i < chunk->count; i++){
        Instruction instruction = chunk->code[i];
        if(GET_OPCODE(instruction) == op && GET_ARG_B(instruction) == b){
            return true;
        }
    }
    return false;
}

static bool appendParams(SourceBuf* source, int count){
    for(int i = 0; i < count; i++){
        if(!appendSource(source, "%sp%d", i == 0 ? "" : ",", i)){
            return false;
        }
    }
    return true;
}

static bool appendNullArgs(SourceBuf* source, int count){
    for(int i = 0; i < count; i++){
        if(!appendSource(source, "%snull", i == 0 ? "" : ",")){
            return false;
        }
    }
    return true;
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

static int testCompilerLimits(void){
    SourceBuf source = {0};

    if(!appendSource(&source, "func edge(") ||
       !appendParams(&source, ARG_MAX) ||
       !appendSource(&source, ") { var last; return; } edge(") ||
       !appendNullArgs(&source, ARG_MAX) ||
       !appendSource(&source, ");")){
        fprintf(stderr, "Could not build compiler limit test source.\n");
        return 1;
    }

    VM vm;
    initVM(&vm, 0, NULL);
    DiagCapture capture = {.expectedSource = source.data};
    DiagSink sink = {captureDiag, &capture};
    ObjectFunc* script = compileWithDiag(&vm, source.data, "limits.cies", NULL, &sink);

    ObjectFunc* edge = NULL;
    if(script != NULL){
        for(size_t i = 0; i < script->chunk.constants.count; i++){
            Value value = script->chunk.constants.values[i];
            if(IS_FUNC(value)){
                edge = AS_FUNC(value);
                break;
            }
        }
    }

    int failed = 0;
    if(script == NULL || capture.count != 0 || edge == NULL ||
       !chunkHasOpB(&script->chunk, OP_CALL, MASK_B) ||
       !chunkHasOpB(&edge->chunk, OP_RETURN, 1) ||
       edge->arity != ARG_MAX || edge->maxRegSlots != REG_MAX){
        fprintf(stderr, "Valid compiler limits were not encoded correctly.\n");
        failed = 1;
    }
    if(interpret(&vm, source.data, "limits.cies") != VM_OK){
        fprintf(stderr, "Valid compiler limits did not execute correctly.\n");
        failed = 1;
    }

    source = (SourceBuf){0};
    if(!appendSource(&source, "func tooMany(") ||
       !appendParams(&source, ARG_MAX + 1) ||
       !appendSource(&source, ") {}")){
        fprintf(stderr, "Could not build argument overflow test source.\n");
        freeVM(&vm);
        return 1;
    }

    if(!compileRejects(&vm, source.data, "arg_limit.cies", "Too many function args.")){
        fprintf(stderr, "Argument overflow was not rejected cleanly.\n");
        failed = 1;
    }

    source = (SourceBuf){0};
    if(!appendSource(&source, "class C {} method (c C) run(") ||
       !appendParams(&source, ARG_MAX + 1) ||
       !appendSource(&source, ") {}")){
        fprintf(stderr, "Could not build method argument overflow test source.\n");
        freeVM(&vm);
        return 1;
    }
    if(!compileRejects(&vm, source.data, "method_limit.cies", "Too many method args.")){
        fprintf(stderr, "Method argument overflow was not rejected cleanly.\n");
        failed = 1;
    }

    source = (SourceBuf){0};
    if(!appendSource(&source, "func f() {} f(") ||
       !appendNullArgs(&source, ARG_MAX + 1) ||
       !appendSource(&source, ");")){
        fprintf(stderr, "Could not build call argument overflow test source.\n");
        freeVM(&vm);
        return 1;
    }
    if(!compileRejects(
           &vm,
           source.data,
           "call_limit.cies",
           "Cannot have more than 254 arguments."
       )){
        fprintf(stderr, "Call argument overflow was not rejected cleanly.\n");
        failed = 1;
    }

    source = (SourceBuf){0};
    if(!appendSource(&source, "func tooManyLocals() {")){
        fprintf(stderr, "Could not build local overflow test source.\n");
        freeVM(&vm);
        return 1;
    }
    for(int i = 0; i < REG_MAX; i++){
        if(!appendSource(&source, "var v%d;", i)){
            fprintf(stderr, "Could not build local overflow test source.\n");
            freeVM(&vm);
            return 1;
        }
    }
    if(!appendSource(&source, "}")){
        fprintf(stderr, "Could not build local overflow test source.\n");
        freeVM(&vm);
        return 1;
    }

    if(!compileRejects(&vm, source.data, "local_limit.cies", "Too many local variables.")){
        fprintf(stderr, "Local overflow was not rejected cleanly.\n");
        failed = 1;
    }

    source = (SourceBuf){0};
    if(!appendSource(&source, "func outer(") ||
       !appendParams(&source, ARG_MAX) ||
       !appendSource(&source, ") { func middle() {")){
        fprintf(stderr, "Could not build upvalue overflow test source.\n");
        freeVM(&vm);
        return 1;
    }
    for(int i = 0; i < ARG_MAX; i++){
        if(!appendSource(&source, "var l%d;", i)){
            fprintf(stderr, "Could not build upvalue overflow test source.\n");
            freeVM(&vm);
            return 1;
        }
    }
    if(!appendSource(&source, "return func() { outer;")){
        fprintf(stderr, "Could not build upvalue overflow test source.\n");
        freeVM(&vm);
        return 1;
    }
    for(int i = 0; i < ARG_MAX; i++){
        if(!appendSource(&source, "p%d;", i)){
            fprintf(stderr, "Could not build upvalue overflow test source.\n");
            freeVM(&vm);
            return 1;
        }
    }
    if(!appendSource(&source, "middle; l0; }; } }")){
        fprintf(stderr, "Could not build upvalue overflow test source.\n");
        freeVM(&vm);
        return 1;
    }

    if(!compileRejects(&vm, source.data, "upvalue_limit.cies", "Too many upvalues.")){
        fprintf(stderr, "Upvalue overflow was not rejected cleanly.\n");
        failed = 1;
    }

    freeVM(&vm);
    return failed;
}

int main(void){
    if(testScanner() != 0 || testDiagnostic() != 0 || testCompilerLimits() != 0){
        return 1;
    }

    printf("Frontend tests passed.\n");
    return 0;
}
