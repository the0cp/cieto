#ifndef CIETO_COMPILER_H
#define CIETO_COMPILER_H

#include "vm.h"
#include "scanner.h"
#include "common.h"

#define LOCAL_MAX (UINT16_MAX + 1)
#define REG_MAX 256
#define LOOP_MAX 16
#define CASE_MAX 32

typedef struct{
    Token name;
    int depth;
    int reg;
}Local;

typedef struct{
    Token pre;  // Previous token
    Token cur;  // Current token
    bool hadError;  // Flag to indicate if there was a compilation error
    bool panic; // Flag to indicate if we are in panic mode
}Parser;

typedef struct{
    int start;
    int scopeDepth;
    int breakJump[LOOP_MAX];
    int breakCnt;
}Loop;

typedef struct{
    uint16_t index; // pointing to local or upvalue
    bool isLocal;   // T: local; F: upvalue
}Upvalue;

typedef struct Compiler{
    struct Compiler* enclosing;  // Enclosing compiler for nested functions
    Parser parser;
    VM* vm;
    GlobalEnv* globals;   // point to defining module's global env for global access
    Local locals[LOCAL_MAX];
    int localCnt;
    Upvalue upvalues[LOCAL_MAX];
    int upvalueCnt;
    int scopeDepth;
    Loop loops[LOOP_MAX];
    int loopCnt;
    int freeReg;
    int maxRegSlots;
    FuncType type;
    CompileOpts opts;
    ObjectFunc* func;
}Compiler;

typedef enum{
    EXPR_VOID,
    EXPR_NULL,
    EXPR_TRUE,
    EXPR_FALSE,
    EXPR_K,
    EXPR_NUM,
    EXPR_LOCAL,
    EXPR_UPVAL,
    EXPR_GLOBAL,
    EXPR_INDEX,
    EXPR_PROP,
    EXPR_JMP,
    EXPR_TBD,
    EXPR_REG,
    EXPR_CALL,
}ExprType;

typedef struct{
    ExprType type;
    union{
        struct{
            int index;
            int aux;
        }loc;
        double num;
    }data;
    int tJmp;
    int fJmp;
}ExprDesc;

ObjectFunc* compile(VM* vm, const char* code, const char* srcName);
ObjectFunc* compileWithOpts(VM* vm, const char* code, const char* srcName, const CompileOpts* opts);
void markCompilerRoots(VM* vm);
static ObjectFunc* stopCompiler(Compiler* compiler);
static int emitJmp(Compiler* compiler);
static void patchJump(Compiler* compiler, int offset);
static void emitLoop(Compiler* compiler, int loopStart);
static void emitClosure(Compiler* compiler, int destReg, int constIndex, Compiler* funcCompiler);
static void consume(Compiler* compiler, TokenType type, const char* errMsg);
static void errorAt(Compiler* compiler, Token* token, const char* message);

static int globalSlotForToken(Compiler* compiler, Token* name);
static int identifierGlobalSlot(Compiler* compiler);

static void handleNum(Compiler* compiler, ExprDesc* expr, bool canAssign);
static int makeConstant(Compiler* compiler, Value value);
static void handleVar(Compiler* compiler, ExprDesc* expr, bool canAssign);

static void handleGrouping(Compiler* compiler, ExprDesc* expr, bool canAssign);

static void handleUnary(Compiler* compiler, ExprDesc* expr, bool canAssign);
static void handleBinary(Compiler* compiler, ExprDesc* expr, bool canAssign);

static void handleLiteral(Compiler* compiler, ExprDesc* expr, bool canAssign);

static void handleString(Compiler* compiler, ExprDesc* expr, bool canAssign);

static void handleAnd(Compiler* compiler, ExprDesc* expr, bool canAssign);
static void handleOr(Compiler* compiler, ExprDesc* expr, bool canAssign);

static int argList(Compiler* compiler, ExprDesc* func);
static void handleCall(Compiler* compiler, ExprDesc* expr, bool canAssign);

static void handleImport(Compiler* compiler, ExprDesc* expr, bool canAssign);
static void handleDot(Compiler* compiler, ExprDesc* expr, bool canAssign);

static void handleList(Compiler* compiler, ExprDesc* expr, bool canAssign);
static void handleIndex(Compiler* compiler, ExprDesc* expr, bool canAssign);
static void handleMap(Compiler* compiler, ExprDesc* expr, bool canAssign);

static void handleThis(Compiler* compiler, ExprDesc* expr, bool canAssign);

static void handlePipe(Compiler* compiler, ExprDesc* expr, bool canAssign);

static void advance(Compiler* compiler);

static void expression(Compiler* compiler, ExprDesc* expr);

static void decl(Compiler* compiler);
static void varDecl(Compiler* compiler);
static void defineVar(Compiler* compiler, int global);
static void funcDecl(Compiler* compiler);
static void funcExpr(Compiler* compiler, ExprDesc* expr, bool canAssign);
static void classDecl(Compiler* compiler);
static void methodDecl(Compiler* compiler);
static void importDecl(Compiler* compiler);

static void beginScope(Compiler* compiler);
static void block(Compiler* compiler);
static void endScope(Compiler* compiler);

static void stmt(Compiler* compiler);

static void expressionStmt(Compiler* compiler);

static bool checkType(Compiler* compiler, TokenType type);
static bool match(Compiler* compiler, TokenType type);
static void sync(Compiler* compiler);

static void printStmt(Compiler* compiler);

static void ifStmt(Compiler* compiler);

static void whileStmt(Compiler* compiler);
static void forStmt(Compiler* compiler);
static void breakStmt(Compiler* compiler);
static void continueStmt(Compiler* compiler);
static void switchStmt(Compiler* compiler);
static void systemStmt(Compiler* compiler);
static void deferStmt(Compiler* compiler);
static void returnStmt(Compiler* compiler);

#endif // CIETO_COMPILER_H
