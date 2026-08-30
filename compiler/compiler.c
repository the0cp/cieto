#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "compiler.h"
#include "chunk.h"
#include "mem.h"

#ifdef DEBUG_PRINT_CODE
#include "debug.h"
#endif

typedef enum{
    PREC_NONE,
    PREC_ASSIGNMENT,  // =
    PREC_TERNARY,    // ?:
    PREC_OR,         // or
    PREC_AND,        // and
    PREC_PIPE,       // |>
    PREC_EQUALITY,   // == !=
    PREC_COMPARISON, // < > <= >=
    PREC_TERM,       // + -
    PREC_FACTOR,     // * /
    PREC_UNARY,      // ! - (unary minus)
    PREC_CALL,       // . () []
    PREC_PRIMARY     // identifiers, literals, grouping
}Precedence;

typedef void (*ParseFunc)(Compiler* compiler, ExprDesc* expr, bool canAssign);
typedef struct{
    ParseFunc prefix;  // Function to handle prefix parsing
    ParseFunc infix;   // Function to handle infix parsing
    Precedence precedence;  // Precedence of the operator
}ParseRule;

static void initExpr(ExprDesc* expr, ExprType type, int index);
static bool isPrimVal(const ExprDesc* expr, Value* val);
static bool tryFoldUnary(TokenType op, const ExprDesc* expr, ExprDesc* result);
static bool tryFoldNumBinary(TokenType op, double left, double right, ExprDesc* result);
static bool tryFoldEq(TokenType op, const ExprDesc* left, const ExprDesc* right, ExprDesc* result);
static int emitInstruction(Compiler* compiler, Instruction instruction);
static CompileOpts defaultCompileOpts(void);

static void expr2Reg(Compiler* compiler, ExprDesc* expr, int reg);
static void expr2NextReg(Compiler* compiler, ExprDesc* expr);
static void unplugExpr(Compiler* compiler, ExprDesc* expr);
static void expr2RK(Compiler* compiler, ExprDesc* expr);
static void freeExpr(Compiler* compiler, ExprDesc* expr);

static void handleLiteral(Compiler* compiler, ExprDesc* expr, bool canAssign);
static void handleGrouping(Compiler* compiler, ExprDesc* expr, bool canAssign);
static void handlePrefix(Compiler* compiler, ExprDesc* expr, bool canAssign);
static void handlePostfix(Compiler* compiler, ExprDesc* expr, bool canAssign);
static void handleUnary(Compiler* compiler, ExprDesc* expr, bool canAssign);
static void handleBinary(Compiler* compiler, ExprDesc* expr, bool canAssign);
static void handleTernary(Compiler* compiler, ExprDesc* expr, bool canAssign);
static void handleNum(Compiler* compiler, ExprDesc* expr, bool canAssign);
static void handleString(Compiler* compiler, ExprDesc* expr, bool canAssign);
static void handleAnd(Compiler* compiler, ExprDesc* expr, bool canAssign);
static void handleOr(Compiler* compiler, ExprDesc* expr, bool canAssign);
static void handleCall(Compiler* compiler, ExprDesc* expr, bool canAssign);
static void handleImport(Compiler* compiler, ExprDesc* expr, bool canAssign);
static void handleDot(Compiler* compiler, ExprDesc* expr, bool canAssign);
static void handleList(Compiler* compiler, ExprDesc* expr, bool canAssign);
static void handleMap(Compiler* compiler, ExprDesc* expr, bool canAssign);
static void handleIndex(Compiler* compiler, ExprDesc* expr, bool canAssign);
static void handleThis(Compiler* compiler, ExprDesc* expr, bool canAssign);
static void handlePipe(Compiler* compiler, ExprDesc* expr, bool canAssign);

static void addLocal(Compiler* compiler, Token name);
static int resolveLocal(Compiler* compiler, Token* name);
static int resolveUpvalue(Compiler* compiler, Token* name);
static int identifierConst(Compiler* compiler);
static void declLocal(Compiler* compiler);
static int emitJmpIfFalse(Compiler* compiler, int reg);

ParseRule rules[] = {
    [TOKEN_LEFT_PAREN]              = {handleGrouping,  handleCall,     PREC_CALL},
    [TOKEN_RIGHT_PAREN]             = {NULL,            NULL,           PREC_NONE},
    [TOKEN_LEFT_BRACE]              = {handleMap,       NULL,           PREC_NONE},
    [TOKEN_RIGHT_BRACE]             = {NULL,            NULL,           PREC_NONE},
    [TOKEN_LEFT_BRACKET]            = {handleList,      handleIndex,    PREC_CALL},
    [TOKEN_COMMA]                   = {NULL,            NULL,           PREC_NONE},
    [TOKEN_SEMICOLON]               = {NULL,            NULL,           PREC_NONE},

    [TOKEN_PLUS]                    = {NULL,            handleBinary,   PREC_TERM},
    [TOKEN_MINUS]                   = {handleUnary,     handleBinary,   PREC_TERM},
    [TOKEN_STAR]                    = {NULL,            handleBinary,   PREC_FACTOR},
    [TOKEN_SLASH]                   = {NULL,            handleBinary,   PREC_FACTOR},
    [TOKEN_PERCENT]                 = {NULL,            handleBinary,   PREC_FACTOR},
    [TOKEN_PLUS_EQUAL]              = {NULL,            NULL,           PREC_NONE},
    [TOKEN_MINUS_EQUAL]             = {NULL,            NULL,           PREC_NONE},
    [TOKEN_PLUS_PLUS]               = {handlePrefix,    handlePostfix,  PREC_CALL},
    [TOKEN_MINUS_MINUS]             = {handlePrefix,    handlePostfix,  PREC_CALL},
    [TOKEN_QUESTION]                = {NULL,            handleTernary,  PREC_TERNARY},

    [TOKEN_NUMBER]                  = {handleNum,       NULL,           PREC_NONE},
    [TOKEN_IDENTIFIER]              = {handleVar,       NULL,           PREC_NONE},

    [TOKEN_STRING_START]            = {handleString,    NULL,           PREC_NONE},
    [TOKEN_STRING_END]              = {NULL,            NULL,           PREC_NONE},
    [TOKEN_INTERPOLATION_START]     = {NULL,            NULL,           PREC_NONE},
    [TOKEN_INTERPOLATION_END]       = {NULL,            NULL,           PREC_NONE},
    [TOKEN_INTERPOLATION_CONTENT]   = {NULL,            NULL,           PREC_NONE},

    [TOKEN_NULL]                    = {handleLiteral,   NULL,           PREC_NONE},
    [TOKEN_TRUE]                    = {handleLiteral,   NULL,           PREC_NONE},
    [TOKEN_FALSE]                   = {handleLiteral,   NULL,           PREC_NONE},

    [TOKEN_NOT]                     = {handleUnary,     NULL,           PREC_UNARY},
    [TOKEN_NOT_EQUAL]               = {NULL,            handleBinary,   PREC_EQUALITY},
    [TOKEN_EQUAL]                   = {NULL,            handleBinary,   PREC_EQUALITY},
    [TOKEN_GREATER]                 = {NULL,            handleBinary,   PREC_COMPARISON},
    [TOKEN_LESS]                    = {NULL,            handleBinary,   PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL]           = {NULL,            handleBinary,   PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]              = {NULL,            handleBinary,   PREC_COMPARISON},

    [TOKEN_AND]                     = {NULL,            handleAnd,      PREC_AND},
    [TOKEN_OR]                      = {NULL,            handleOr,       PREC_OR},

    [TOKEN_IMPORT]                  = {handleImport,    NULL,           PREC_NONE},
    [TOKEN_DOT]                     = {NULL,            handleDot,      PREC_CALL},

    [TOKEN_THIS]                    = {handleThis,      NULL,           PREC_NONE},  

    [TOKEN_PIPE]                    = {NULL,            handlePipe,     PREC_PIPE},
    [TOKEN_FUNC]                    = {funcExpr,        NULL,           PREC_NONE},

    [TOKEN_EOF]                     = {NULL,            NULL,           PREC_NONE},
};

static inline ParseRule* getRule(TokenType type){return &rules[type];}
static void parsePrecedence(Compiler* compiler, ExprDesc* expr, Precedence precedence);
static int parseVar(Compiler* compiler, const char* err);

static void advance(Compiler* compiler){
    compiler->parser.pre = compiler->parser.cur;
    while(true){
        compiler->parser.cur = scan();
        if(compiler->parser.cur.type != TOKEN_ERROR){
            break;
        }
        errorAt(compiler, &compiler->parser.cur, "Unexpected token: %.*s");
    }
}

static void initExpr(ExprDesc* expr, ExprType type, int index){
    memset(expr, 0, sizeof(ExprDesc));
    expr->type = type;
    expr->data.loc.index = index;
    expr->data.loc.aux = 0;
    expr->tJmp = -1;
    expr->fJmp = -1;
}

static bool isPrimVal(const ExprDesc* expr, Value* val){
    switch(expr->type){
        case EXPR_NULL:
            *val = NULL_VAL;
            return true;
        case EXPR_TRUE:
            *val = BOOL_VAL(true);
            return true;
        case EXPR_FALSE:
            *val = BOOL_VAL(false);
            return true;
        case EXPR_NUM:
            *val = NUM_VAL(expr->data.num);
            return true;
        default:
            return false;
    }
}

static bool tryFoldUnary(TokenType op, const ExprDesc* expr, ExprDesc* result){
    switch(op){
        case TOKEN_MINUS:
            if(expr->type != EXPR_NUM){
                return false;
            }
            initExpr(result, EXPR_NUM, 0);
            result->data.num = -expr->data.num;
            return true;
        case TOKEN_NOT:{
            Value value;
            if(!isPrimVal(expr, &value)){
                return false;
            }
            initExpr(result, isTruthy(value) ? EXPR_FALSE : EXPR_TRUE, 0);
            return true;
        }
        default:
            return false;
    }
}

static bool tryFoldNumBinary(TokenType op, double left, double right, ExprDesc* result){
    switch(op){
        case TOKEN_PLUS:
            initExpr(result, EXPR_NUM, 0);
            result->data.num = left + right;
            return true;
        case TOKEN_MINUS:
            initExpr(result, EXPR_NUM, 0);
            result->data.num = left - right;
            return true;
        case TOKEN_STAR:
            initExpr(result, EXPR_NUM, 0);
            result->data.num = left * right;
            return true;
        case TOKEN_SLASH:
            if(right == 0){
                return false;
            }
            initExpr(result, EXPR_NUM, 0);
            result->data.num = left / right;
            return true;
        case TOKEN_PERCENT:
            if(right == 0){
                return false;
            }
            initExpr(result, EXPR_NUM, 0);
            result->data.num = fmod(left, right);
            return true;
        case TOKEN_EQUAL:
            initExpr(result, left == right ? EXPR_TRUE : EXPR_FALSE, 0);
            return true;
        case TOKEN_NOT_EQUAL:
            initExpr(result, left != right ? EXPR_TRUE : EXPR_FALSE, 0);
            return true;
        case TOKEN_GREATER:
            initExpr(result, left > right ? EXPR_TRUE : EXPR_FALSE, 0);
            return true;
        case TOKEN_LESS:
            initExpr(result, left < right ? EXPR_TRUE : EXPR_FALSE, 0);
            return true;
        case TOKEN_GREATER_EQUAL:
            initExpr(result, left >= right ? EXPR_TRUE : EXPR_FALSE, 0);
            return true;
        case TOKEN_LESS_EQUAL:
            initExpr(result, left <= right ? EXPR_TRUE : EXPR_FALSE, 0);
            return true;
        default:
            return false;
    }
}

static bool tryFoldEq(TokenType op, const ExprDesc* left, const ExprDesc* right, ExprDesc* result){
    if(op != TOKEN_EQUAL && op != TOKEN_NOT_EQUAL){
        return false;
    }

    Value leftVal;
    Value rightVal;
    if(!isPrimVal(left, &leftVal) || !isPrimVal(right, &rightVal)){
        return false;
    }

    bool equal = isEqual(leftVal, rightVal);
    if(op == TOKEN_NOT_EQUAL){
        equal = !equal;
    }

    initExpr(result, equal ? EXPR_TRUE : EXPR_FALSE, 0);
    return true;
}

static int emitInstruction(Compiler* compiler, Instruction instruction){
    writeChunk(
        compiler->vm, 
        &compiler->func->chunk, 
        instruction, 
        compiler->parser.pre.line
    );
    return (int)(compiler->func->chunk.count - 1);
}

static CompileOpts defaultCompileOpts(void){
    CompileOpts opts;
    opts.foldConst = true;
    return opts;
}

static int emitABC(Compiler* compiler, uint8_t op, uint8_t a, uint8_t b, uint8_t c){
    Instruction instruction = CREATE_ABC(op, a, b, c);
    return emitInstruction(compiler, instruction);
}

static int emitABx(Compiler* compiler, uint8_t op, uint8_t a, uint16_t bx){
    Instruction instruction = CREATE_ABx(op, a, bx);
    return emitInstruction(compiler, instruction);
}

static int emitAsBx(Compiler* compiler, uint8_t op, uint8_t a, int16_t sbx){
    uint16_t bx = (uint16_t)(sbx + (MAX_BX >> 1));
    Instruction instruction = CREATE_ABx(op, a, bx);
    return emitInstruction(compiler, instruction);
}

static int getFreeReg(Compiler* compiler){
    return compiler->freeReg;
}

static void reserveReg(Compiler* compiler, int cnt){
    compiler->freeReg += cnt;

    if(compiler->freeReg > compiler->maxRegSlots){
        compiler->maxRegSlots = compiler->freeReg;
    }

    if(compiler->freeReg > REG_MAX){
        errorAt(compiler, &compiler->parser.pre, "Register overflow.");
    }
}

static void freeRegs(Compiler* compiler, int cnt){
    compiler->freeReg -= cnt;
    int minReg = compiler->localCnt > 0 ? compiler->locals[compiler->localCnt - 1].reg + 1 : 1;
    if(compiler->freeReg < minReg){
        compiler->freeReg = minReg;
    }
}

static void unplugExpr(Compiler* compiler, ExprDesc* expr){
    switch(expr->type){
        case EXPR_VOID:
            break;
        case EXPR_UPVAL:
            expr->data.loc.index = emitABC(
                compiler, 
                OP_GET_UPVAL, 
                0, 
                expr->data.loc.index, 
                0
            );
            expr->type = EXPR_TBD;
            break;
        case EXPR_GLOBAL:
            expr->data.loc.index = emitABx(
                compiler, 
                OP_GET_GLOBAL, 
                0, 
                expr->data.loc.index
            );
            expr->type = EXPR_TBD;
            break;
        case EXPR_INDEX:
        {
            int destReg = getFreeReg(compiler);
            reserveReg(compiler, 1);

            emitABC(
                compiler, 
                OP_GET_INDEX, 
                destReg, 
                expr->data.loc.index, 
                expr->data.loc.aux
            );
            expr->type = EXPR_REG;
            expr->data.loc.index = destReg;
            break;
        }
        case EXPR_PROP:
        {
            int destReg = getFreeReg(compiler);
            reserveReg(compiler, 1);

            emitABC(
                compiler, 
                OP_GET_PROPERTY, 
                destReg, 
                expr->data.loc.index, 
                expr->data.loc.aux
            );
            expr->type = EXPR_REG;
            expr->data.loc.index = destReg;
            break;
        }
        case EXPR_CALL:
            expr->type = EXPR_TBD;
            break;
        default:
            break;
    }
}

static void expr2Reg(Compiler* compiler, ExprDesc* expr,int reg){
    unplugExpr(compiler, expr);
    switch(expr->type){
        case EXPR_VOID:
            errorAt(compiler, &compiler->parser.pre, "Expression has no value.");
            break;
        case EXPR_NULL:
            emitABC(compiler, OP_LOADNULL, reg, 0, 0);
            break;
        case EXPR_TRUE:
            emitABC(compiler, OP_LOADBOOL, reg, 1, 0);
            break;
        case EXPR_FALSE:
            emitABC(compiler, OP_LOADBOOL, reg, 0, 0);
            break;
        case EXPR_K:
            emitABx(compiler, OP_LOADK, reg, expr->data.loc.index);
            break;
        case EXPR_NUM:
            int constIndex = makeConstant(compiler, NUM_VAL(expr->data.num));
            emitABx(compiler, OP_LOADK, reg, constIndex);
            break;
        case EXPR_TBD:{
            Instruction instruction = compiler->func->chunk.code[expr->data.loc.index];
            instruction = 
                (instruction & ~((Instruction)MASK_A << POS_A)) 
                | ((Instruction)(reg & MASK_A) << POS_A);
            compiler->func->chunk.code[expr->data.loc.index] = instruction;
            break;
        }
        case EXPR_LOCAL:
        case EXPR_REG:{
            if(reg != expr->data.loc.index){
                emitABC(compiler, OP_MOVE, reg, expr->data.loc.index, 0);
            }
            break;
        }
        default:
            // Should not reach here
            break;
    }
    expr->type = EXPR_REG;
    expr->data.loc.index = reg;
}

static void expr2NextReg(Compiler* compiler, ExprDesc* expr){
    unplugExpr(compiler, expr);
    freeExpr(compiler, expr);
    reserveReg(compiler, 1);
    expr2Reg(compiler, expr, compiler->freeReg - 1);
}

static void storeVar(Compiler* compiler, ExprDesc* var, ExprDesc* val){
    switch(var->type){
        case EXPR_LOCAL:
            expr2Reg(compiler, val, var->data.loc.index);
            break;
        case EXPR_UPVAL:{
            expr2NextReg(compiler, val);
            emitABC(
                compiler, 
                OP_SET_UPVAL, 
                val->data.loc.index, 
                var->data.loc.index, 
                0
            );
            break;
        }
        case EXPR_GLOBAL:{
            expr2NextReg(compiler, val);
            emitABx(
                compiler, 
                OP_SET_GLOBAL, 
                val->data.loc.index, 
                var->data.loc.index
            );
            break;
        }
        case EXPR_INDEX:
            expr2NextReg(compiler, val);
            emitABC(
                compiler, 
                OP_SET_INDEX, 
                var->data.loc.index, 
                var->data.loc.aux, 
                val->data.loc.index
            );
            break;
        case EXPR_PROP:
            expr2NextReg(compiler, val);
            emitABC(
                compiler, 
                OP_SET_PROPERTY, 
                var->data.loc.index, 
                var->data.loc.aux, 
                val->data.loc.index
            );
            break;
        default:
            errorAt(compiler, &compiler->parser.pre, "Invalid assignment target.");
            break;
    }

    freeExpr(compiler, val);
}

static void emitBinaryOp(Compiler* compiler, OpCode op, ExprDesc* left, ExprDesc* right){
    expr2NextReg(compiler, left);
    expr2NextReg(compiler, right);
    freeExpr(compiler, right);
    freeExpr(compiler, left);

    int instructionIndex = emitABC(compiler, op, 0, left->data.loc.index, right->data.loc.index);
    left->type = EXPR_TBD;
    left->data.loc.index = instructionIndex;
}

static void freeExpr(Compiler* compiler, ExprDesc* expr){
    if(expr->type == EXPR_TBD){
        int minReg = compiler->localCnt > 0 ? compiler->locals[compiler->localCnt - 1].reg + 1 : 1;
        if(expr->data.loc.index >= minReg){
            if(expr->data.loc.index < compiler->freeReg){
                freeRegs(compiler, 1);
            }
        }
    }
}

static void initCompiler(Compiler* compiler, VM* vm, Compiler* enclosing, FuncType type, ObjectString* srcName){
    compiler->enclosing = enclosing;
    compiler->vm = vm;
    compiler->globals = enclosing != NULL ? enclosing->globals : vm->curGlobal;   
    // point to defining module's global env for global access
    if(enclosing != NULL){
        compiler->opts = enclosing->opts;
    }

    compiler->type = type;
    compiler->func = newFunction(vm);
    compiler->func->srcName = srcName;
    compiler->func->type = type;

    if(type != TYPE_SCRIPT){
        compiler->func->name = copyString(
            vm, 
            compiler->parser.pre.head, 
            compiler->parser.pre.len
        );
    }

    compiler->upvalueCnt = 0;
    compiler->localCnt = 0;
    compiler->scopeDepth = 0;
    compiler->loopCnt = 0;

    if(type == TYPE_SCRIPT){
        compiler->parser.hadError = false;
        compiler->parser.panic = false;
    }

    compiler->freeReg = 0;

    Local *local = &compiler->locals[compiler->localCnt++];
    local->depth = 0;
    local->reg = 0;
    
    compiler->freeReg++;
    compiler->maxRegSlots = compiler->freeReg;

    if(type != TYPE_SCRIPT){
        local->name.head = compiler->parser.pre.head;
        local->name.len = compiler->parser.pre.len;
    }else{
        local->name.head = "";
        local->name.len = 0;
    }
}

ObjectFunc* compile(VM* vm, const char* code, const char* srcNameStr){
    return compileWithOpts(vm, code, srcNameStr, NULL);
}

ObjectFunc* compileWithOpts(VM* vm, const char* code, const char* srcNameStr, const CompileOpts* opts){
    Compiler* compiler = (Compiler*)reallocate(vm, NULL, 0, sizeof(Compiler));
    if(compiler == NULL){
        fprintf(stderr, "Not enough memory to compile.\n");
        return false;
    }
    initScanner(code);
    ObjectString* srcName = copyString(vm, srcNameStr, (int)strlen(srcNameStr));

    push(vm, OBJECT_VAL(srcName));
    Compiler* enclosing = vm->compiler;
    compiler->vm = vm;
    compiler->func = NULL;
    compiler->opts = opts != NULL ? *opts : defaultCompileOpts();
    vm->compiler = compiler;
    initCompiler(compiler, vm, enclosing, TYPE_SCRIPT, srcName);
    pop(vm);    // pop srcName

    advance(compiler);  // Initialize the first token
    if(compiler->parser.cur.type == TOKEN_EOF){
        return NULL;  // No code to compile
    }
    // expression(&compiler); // Start parsing the expression

    while(!match(compiler, TOKEN_EOF)){
        decl(compiler);
    }
    consume(compiler, TOKEN_EOF, "Expected end of file");
    ObjectFunc* func = stopCompiler(compiler);

    bool hadError = compiler->parser.hadError;
    vm->compiler = compiler->enclosing;
    reallocate(vm, compiler, sizeof(Compiler), 0);
    
    return hadError ? NULL : func;
}

void markCompilerRoots(VM* vm){
    Compiler* compiler = vm->compiler;
    
    while(compiler != NULL){
        if(compiler->func != NULL){
            markObject(vm, (Object*)compiler->func); 
            markArray(vm, &compiler->func->chunk.constants);
        }
        compiler = compiler->enclosing;
    }
}

static void expression(Compiler* compiler, ExprDesc* expr){
    parsePrecedence(compiler, expr, PREC_ASSIGNMENT);
}

static void decl(Compiler* compiler){
    if(match(compiler, TOKEN_VAR))
        varDecl(compiler);
    else if(match(compiler, TOKEN_FUNC))
        funcDecl(compiler);
    else if(match(compiler, TOKEN_CLASS))
        classDecl(compiler);
    else if(match(compiler, TOKEN_METHOD))
        methodDecl(compiler);
    else if(match(compiler, TOKEN_IMPORT))
        importDecl(compiler);
    else
        stmt(compiler);
    
    if(compiler->parser.panic){
        sync(compiler);
    }
}

static void varDecl(Compiler* compiler){
    int global = parseVar(compiler, "Expect variable name.");

    if(compiler->scopeDepth > 0){
        int reg = compiler->locals[compiler->localCnt - 1].reg;
        if(match(compiler, TOKEN_ASSIGN)){
            ExprDesc initExpr;
            expression(compiler, &initExpr);
            expr2Reg(compiler, &initExpr, reg);
        }else{
            emitABC(compiler, OP_LOADNULL, reg, 0, 0);
        }
        consume(compiler, TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
        defineVar(compiler, global);
    }else{
        if(match(compiler, TOKEN_ASSIGN)){
            ExprDesc initExpr;
            expression(compiler, &initExpr);
            storeVar(
                compiler,
                &(ExprDesc){
                    .type = EXPR_GLOBAL,
                    .data.loc.index = global
                },
                &initExpr
            );
        }else{
            int tmp = getFreeReg(compiler);
            emitABC(compiler, OP_LOADNULL, tmp, 0, 0);
            emitABx(compiler, OP_SET_GLOBAL, tmp, global);
        }
        consume(compiler, TOKEN_SEMICOLON, "Expect ';' after variable declaration.");
        defineVar(compiler, global);
    }
}

static void compileFunc(Compiler* compiler, FuncType type, int destReg, Token* funcName){
    Compiler* funcCompiler = (Compiler*)reallocate(compiler->vm, NULL, 0, sizeof(Compiler));    
    if(funcCompiler == NULL){
        errorAt(compiler, &compiler->parser.pre, "Not enough memory to compile function.");
        return;
    }
    funcCompiler->parser = compiler->parser;

    funcCompiler->enclosing = compiler;
    funcCompiler->vm = compiler->vm;
    funcCompiler->func = NULL;  
    compiler->vm->compiler = funcCompiler;

    initCompiler(funcCompiler, compiler->vm, compiler, type, compiler->func->srcName);

    if(funcName != NULL){
        funcCompiler->func->name = copyString(
            compiler->vm, 
            funcName->head, 
            funcName->len
        );
    }

    beginScope(funcCompiler);
    consume(funcCompiler, TOKEN_LEFT_PAREN, "Expect '(' after function name.");

    if(!checkType(funcCompiler, TOKEN_RIGHT_PAREN)){
        do{
            funcCompiler->func->arity++;
            if(funcCompiler->func->arity > 255){
                errorAt(funcCompiler, &funcCompiler->parser.cur, "Too many function args.");
            }
            int constant = parseVar(funcCompiler, "Expect param name.");
            defineVar(funcCompiler, constant);
        }while(match(funcCompiler, TOKEN_COMMA));
    }

    consume(funcCompiler, TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(funcCompiler, TOKEN_LEFT_BRACE, "Expect '{' before function body.");
    block(funcCompiler);

    ObjectFunc* func = stopCompiler(funcCompiler);
    
    int constIndex = makeConstant(compiler, OBJECT_VAL(func));

    emitClosure(compiler, destReg, constIndex, funcCompiler);

    compiler->parser = funcCompiler->parser;
    compiler->vm->compiler = compiler;
    reallocate(compiler->vm, funcCompiler, sizeof(Compiler), 0);
}

static void funcExpr(Compiler* compiler, ExprDesc* expr, bool canAssign){
    int destReg = getFreeReg(compiler);
    reserveReg(compiler, 1);
    compileFunc(compiler, TYPE_FUNC, destReg, NULL);
    initExpr(expr, EXPR_REG, destReg);
}

static void funcDecl(Compiler* compiler){
    int global = parseVar(compiler, "Expect function name.");
    Token funcName = compiler->parser.pre;

    if(compiler->scopeDepth > 0){
        int reg = compiler->localCnt - 1;
        compileFunc(compiler, TYPE_FUNC, reg, &funcName);
        defineVar(compiler, global);
    }else{
        int tmpReg = getFreeReg(compiler);
        compileFunc(compiler, TYPE_FUNC, tmpReg, &funcName);
        emitABx(compiler, OP_SET_GLOBAL, tmpReg, global);
        defineVar(compiler, global);
    }
}

static void compileMethod(Compiler* compiler, Token recvName, Token methodName, FuncType type, int destReg){
    Compiler* methodCompiler = (Compiler*)reallocate(compiler->vm, NULL, 0, sizeof(Compiler));    
    if(methodCompiler == NULL){
        errorAt(compiler, &compiler->parser.pre, "Not enough memory to compile method.");
        return;
    }

    methodCompiler->parser = compiler->parser;
    methodCompiler->enclosing = compiler;
    methodCompiler->vm = compiler->vm;
    methodCompiler->func = NULL;  
    compiler->vm->compiler = methodCompiler;

    initCompiler(methodCompiler, compiler->vm, compiler, type, compiler->func->srcName);

    methodCompiler->func->name = copyString(
        compiler->vm, 
        methodName.head, 
        methodName.len
    );

    Local *local = &methodCompiler->locals[0]; 
    local->name = recvName; 
    local->depth = 0;

    beginScope(methodCompiler);
    consume(methodCompiler, TOKEN_LEFT_PAREN, "Expect '(' after method name.");

    if(!checkType(methodCompiler, TOKEN_RIGHT_PAREN)){
        do{
            methodCompiler->func->arity++;
            if(methodCompiler->func->arity > 255){
                errorAt(methodCompiler, &methodCompiler->parser.cur, "Too many method args.");
            }
            int constant = parseVar(methodCompiler, "Expect param name.");
            defineVar(methodCompiler, constant);
        }while(match(methodCompiler, TOKEN_COMMA));
    }

    consume(methodCompiler, TOKEN_RIGHT_PAREN, "Expect ')' after parameters.");
    consume(methodCompiler, TOKEN_LEFT_BRACE, "Expect '{' before method body.");
    block(methodCompiler);

    ObjectFunc* func = stopCompiler(methodCompiler);

    int funcConstIndex = makeConstant(compiler, OBJECT_VAL(func));
    emitClosure(compiler, destReg, funcConstIndex, methodCompiler);

    compiler->parser = methodCompiler->parser;
    compiler->vm->compiler = compiler;
    reallocate(compiler->vm, methodCompiler, sizeof(Compiler), 0);
}

static void classDecl(Compiler* compiler){
    consume(compiler, TOKEN_IDENTIFIER, "Expect class name.");

    int nameConst = identifierConst(compiler);
    int global = compiler->scopeDepth == 0 ? identifierGlobalSlot(compiler) : 0;

    int classReg;

    if(compiler->scopeDepth > 0){
        declLocal(compiler);
        classReg = compiler->locals[compiler->localCnt - 1].reg;
        emitABx(compiler, OP_CLASS, classReg, nameConst);
    }else{
        classReg = getFreeReg(compiler);
        reserveReg(compiler, 1);
        emitABx(compiler, OP_CLASS, classReg, nameConst);
        emitABx(compiler, OP_SET_GLOBAL, classReg, global);
        defineVar(compiler, global);
    }

    consume(compiler, TOKEN_LEFT_BRACE, "Expect '{' before class body");

    while(!checkType(compiler, TOKEN_RIGHT_BRACE) && !checkType(compiler, TOKEN_EOF)){
        consume(compiler, TOKEN_IDENTIFIER, "Expect field name.");
        
        int fieldNameIndex = identifierConst(compiler); 
        reserveReg(compiler, 1); // reserve register for class instance
        int valReg = getFreeReg(compiler) - 1;

        if(match(compiler, TOKEN_ASSIGN)){
            ExprDesc initExpr;
            expression(compiler, &initExpr);
            expr2Reg(compiler, &initExpr, valReg);
            freeExpr(compiler, &initExpr);
        }else{
            emitABC(compiler, OP_LOADNULL, valReg, 0, 0);
        }

        reserveReg(compiler, 1); // reserve register for field value
        int keyReg = getFreeReg(compiler);
        emitABx(compiler, OP_LOADK, keyReg, fieldNameIndex);
        
        emitABC(compiler, OP_FIELD, classReg, keyReg, valReg);
        consume(compiler, TOKEN_SEMICOLON, "Expect ';' after field declaration.");
        freeRegs(compiler, 2);
    }
    consume(compiler, TOKEN_RIGHT_BRACE, "Expect '}' after class body.");

    if(compiler->scopeDepth == 0){
        freeRegs(compiler, 1); // free class register if it's global
    }
}

static void methodDecl(Compiler* compiler){
    consume(compiler, TOKEN_LEFT_PAREN, "Expect '(' after method.");
    consume(compiler, TOKEN_IDENTIFIER, "Expect receiver name.");
    Token recvName = compiler->parser.pre;

    consume(compiler, TOKEN_IDENTIFIER, "Expect receiver type.");
    Token className = compiler->parser.pre;
    consume(compiler, TOKEN_RIGHT_PAREN, "Expect ')' after receiver.");

    int classReg = getFreeReg(compiler);
    reserveReg(compiler, 1);

    int classIndex = resolveLocal(compiler, &className);
    if(classIndex != -1){
        emitABC(compiler, OP_MOVE, classReg, classIndex, 0);
    }else if((classIndex = resolveUpvalue(compiler, &className)) != -1){
        emitABC(compiler, OP_GET_UPVAL, classReg, classIndex, 0);
    }else{
        Value nameValue = OBJECT_VAL(copyString(compiler->vm, className.head, className.len));
        classIndex = globalSlotForToken(compiler, &className);
        emitABx(compiler, OP_GET_GLOBAL, classReg, classIndex);
    }

    consume(compiler, TOKEN_IDENTIFIER, "Expect method name.");

    Token methodName = compiler->parser.pre;
    int methodNameConst = identifierConst(compiler);

    int nameReg = getFreeReg(compiler);
    reserveReg(compiler, 1);
    emitABx(compiler, OP_LOADK, nameReg, methodNameConst);

    FuncType type = TYPE_METHOD;
    if(methodName.len == 4 && memcmp(methodName.head, "init", 4) == 0){
        type = TYPE_INITIALIZER;
    }

    int methodReg = getFreeReg(compiler);
    reserveReg(compiler, 1);

    compileMethod(compiler, recvName, methodName, type, methodReg);
    emitABC(compiler, OP_METHOD, classReg, nameReg, methodReg);
    freeRegs(compiler, 3);
}

static void importDecl(Compiler* compiler){
    consume(compiler, TOKEN_STRING_START, "Expect module name string.");
    Token pathToken = compiler->parser.cur;
    Value valStr = OBJECT_VAL(copyString(compiler->vm, pathToken.head, pathToken.len));
    
    int pathConst = makeConstant(compiler, valStr);

    advance(compiler);
    consume(compiler, TOKEN_STRING_END, "Expect '\"' after module name string.");

    Token aliasName = pathToken;
    for(int i = 0; i < aliasName.len; i++){
        char c = aliasName.head[i];
        if(c == '/' || c == '\\'){
            aliasName.head += i + 1;
            aliasName.len -= i + 1;
            i = -1;
        }
    }

    for(int i = aliasName.len - 1; i >= 0; i--){
        char c = aliasName.head[i];
        if(c == '.'){
            aliasName.len = i;
            break;
        }
    }

    int moduleReg = getFreeReg(compiler);
    reserveReg(compiler, 1);
    emitABx(compiler, OP_IMPORT, moduleReg, pathConst);

    if(compiler->scopeDepth > 0){
        addLocal(compiler, aliasName);
        compiler->locals[compiler->localCnt - 1].depth = compiler->scopeDepth;
    }else{
        int aliasIndex = globalSlotForToken(compiler, &aliasName);
        if(aliasIndex < 0){
            errorAt(compiler, &compiler->parser.pre, "Failed to add module alias constant.");
            return;
        }
        emitABx(compiler, OP_SET_GLOBAL, moduleReg, aliasIndex);
        defineVar(compiler, aliasIndex);
        freeRegs(compiler, 1);
    }

    consume(compiler, TOKEN_SEMICOLON, "Expect ';' after import statement.");
}

static void beginScope(Compiler* compiler){
    compiler->scopeDepth++;
}

static void block(Compiler* compiler){
    while(!checkType(compiler, TOKEN_RIGHT_BRACE) && !checkType(compiler, TOKEN_EOF)){
        decl(compiler);
    }
    consume(compiler, TOKEN_RIGHT_BRACE, "Expect '}' after block.");
}

static void endScope(Compiler* compiler){
    compiler->scopeDepth--;
    while(compiler->localCnt > 0 && 
        compiler->locals[compiler->localCnt-1].depth > compiler->scopeDepth){
            int closedReg = compiler->locals[compiler->localCnt - 1].reg;
            emitABC(compiler, OP_CLOSE_UPVAL, closedReg, 0, 0);
            compiler->localCnt--;
    }

    if(compiler->localCnt > 0){
        compiler->freeReg = compiler->locals[compiler->localCnt - 1].reg + 1;
    }else{
        compiler->freeReg = 1;
    }
}

static void stmt(Compiler* compiler){
    if(match(compiler, TOKEN_PRINT))
        printStmt(compiler);
    else if(match(compiler, TOKEN_IF))
        ifStmt(compiler);
    else if(match(compiler, TOKEN_WHILE))
        whileStmt(compiler);
    else if(match(compiler, TOKEN_FOR))
        forStmt(compiler);
    else if(match(compiler, TOKEN_BREAK))
        breakStmt(compiler);
    else if(match(compiler, TOKEN_SWITCH))
        switchStmt(compiler);
    else if(match(compiler, TOKEN_CONTINUE))
        continueStmt(compiler);
    else if(match(compiler, TOKEN_SYSTEM))
        systemStmt(compiler);
    else if(match(compiler, TOKEN_DEFER))
        deferStmt(compiler);
    else if(match(compiler, TOKEN_RETURN))
        returnStmt(compiler);
    else if(match(compiler, TOKEN_LEFT_BRACE)){
        beginScope(compiler);
        block(compiler);
        endScope(compiler);
    }else expressionStmt(compiler);
}

static void expressionStmt(Compiler* compiler){
    ExprDesc expr;
    expression(compiler, &expr);
    freeExpr(compiler, &expr);
    consume(compiler, TOKEN_SEMICOLON, "Expect ';' after expression.");
}

static bool checkType(Compiler* compiler, TokenType type){
    return compiler->parser.cur.type == type;
}

static bool match(Compiler* compiler, TokenType type){
    if(!checkType(compiler, type)){
        return false;
    }
    advance(compiler);
    return true;
}

static void sync(Compiler* compiler){
    compiler->parser.panic = false;
    while(compiler->parser.cur.type != TOKEN_EOF){
        if(compiler->parser.pre.type == TOKEN_SEMICOLON){
            return;
        }
        switch(compiler->parser.cur.type){
            case TOKEN_CLASS:
            case TOKEN_FUNC:
            case TOKEN_VAR:
            case TOKEN_FOR:
            case TOKEN_IF:
            case TOKEN_WHILE:
            case TOKEN_PRINT:
            case TOKEN_RETURN:
                return;
            default:
                ;
        }
        advance(compiler);
    }
}

static void printStmt(Compiler* compiler){
    ExprDesc expr;
    expression(compiler, &expr);
    expr2NextReg(compiler, &expr);
    emitABC(compiler, OP_PRINT, expr.data.loc.index, 0, 0);
    consume(compiler, TOKEN_SEMICOLON, "Expect ';' after a expression");
    freeExpr(compiler, &expr);
}

static void ifStmt(Compiler* compiler){
    /*
    code: if (condition) { then_branch } else { else_branch }

    Bytecode Layout & Jumps:
    
            +--------------------------------------------+
            |                                            |
            V                                            |
    | ---cond--- | OP_JUMP_IF_FALSE | offset_to_else | OP_POP | ---then--- | OP_JUMP | offset_to_end | OP_POP | ---else--- |
                                    |                                                ^
                                    |                                                |
                                    +------------------------------------------------+
    */
    consume(compiler, TOKEN_LEFT_PAREN, "Expect '(' after 'if'.");
    ExprDesc condition;
    expression(compiler, &condition);
    consume(compiler, TOKEN_RIGHT_PAREN, "Expect ')' after condition.");

    expr2NextReg(compiler, &condition);
    int condReg = condition.data.loc.index;
    int elseJmp = -1;

    int codeCnt = compiler->func->chunk.count;
    if(codeCnt >= 3){
        Instruction instFalse = compiler->func->chunk.code[codeCnt - 1];   // loadbool false
        Instruction instTrue = compiler->func->chunk.code[codeCnt - 2];    // loadbool true
        Instruction instCmp = compiler->func->chunk.code[codeCnt - 3];     // jump if false

        if(GET_OPCODE(instFalse) == OP_LOADBOOL && GET_OPCODE(instTrue) == OP_LOADBOOL){
            OpCode cmpOp = GET_OPCODE(instCmp);
            if(cmpOp == OP_LT || cmpOp == OP_LE || cmpOp == OP_EQ){
                compiler->func->chunk.count -= 2;
                freeRegs(compiler, 1);  // free targetReg

                // expect False
                int argA = GET_ARG_A(instCmp);
                int argB = GET_ARG_B(instCmp);
                int argC = GET_ARG_C(instCmp);
                compiler->func->chunk.code[codeCnt - 3] = CREATE_ABC(cmpOp, 0, argB, argC);
                
                elseJmp = emitJmp(compiler);
            }
        }
    }

    if(elseJmp == -1){
        elseJmp = emitJmpIfFalse(compiler, condReg);
    }

    freeExpr(compiler, &condition);
    stmt(compiler);
    if(match(compiler, TOKEN_ELSE)){
        int endJmp = emitJmp(compiler);
        patchJump(compiler, elseJmp);
        stmt(compiler);
        patchJump(compiler, endJmp);
    }else{
        patchJump(compiler, elseJmp);
    }
}

static void whileStmt(Compiler* compiler){
    /*
    code: while (condition) { body }

    Bytecode Layout & Jumps:

                            +---------------------------------------+
                            |                                       |
                            |                                       v
    | ---cond--- | OP_JUMP_IF_FALSE | OP_POP | ---body--- | OP_LOOP | OP_POP |
    ^                                                           |
    |                                                           |
    +-----------------------------------------------------------+
    */

    int loopStart = compiler->func->chunk.count;

    if(compiler->loopCnt == LOOP_MAX){
        errorAt(compiler, &compiler->parser.pre, "Too many nested loops.");
        return;
    }
    
    Loop *loop = &compiler->loops[compiler->loopCnt++];
    loop->start = loopStart;
    loop->scopeDepth = compiler->scopeDepth;
    loop->breakCnt = 0;

    consume(compiler, TOKEN_LEFT_PAREN, "Expect '(' after 'while'.");
    ExprDesc condition;
    expression(compiler, &condition);
    consume(compiler, TOKEN_RIGHT_PAREN, "Expect ')' after condition.");
    expr2NextReg(compiler, &condition);

    int condReg = condition.data.loc.index;

    int exitJmp = -1;

    int codeCnt = compiler->func->chunk.count;
    if(codeCnt >= 3){
        Instruction instFalse = compiler->func->chunk.code[codeCnt - 1];   // loadbool false
        Instruction instTrue = compiler->func->chunk.code[codeCnt - 2];    // loadbool true
        Instruction instCmp = compiler->func->chunk.code[codeCnt - 3];     // jump if false

        if(GET_OPCODE(instFalse) == OP_LOADBOOL && GET_OPCODE(instTrue) == OP_LOADBOOL){
            OpCode cmpOp = GET_OPCODE(instCmp);
            if(cmpOp == OP_LT || cmpOp == OP_LE || cmpOp == OP_EQ){
                compiler->func->chunk.count -= 2;
                freeRegs(compiler, 1);  // free targetReg

                // expect False
                int argB = GET_ARG_B(instCmp);
                int argC = GET_ARG_C(instCmp);
                compiler->func->chunk.code[codeCnt - 3] = CREATE_ABC(cmpOp, 0, argB, argC);
                
                exitJmp = emitJmp(compiler);
            }
        }
    }

    if(exitJmp == -1){
        exitJmp = emitJmpIfFalse(compiler, condReg);
    }

    freeExpr(compiler, &condition);
    stmt(compiler);

    emitLoop(compiler, loopStart);
    patchJump(compiler, exitJmp);
}

static void forStmt(Compiler* compiler){
    /*
    code: for (init; condition; increment) { body }

    Bytecode Layout & Jumps:

                 +------------------------------------------------------------------+
                 |                                                  +---------------|-------------------------------+
                 |                                              +---|---------------|--------------+                |
                 |                                              |   |               |              |                |
                 v                                              |   v               |              v                |
    | ---init--- | ---cond--- | OP_JUMP_IF_FALSE | OP_POP | OP_JUMP | ---inc--- | OP_LOOP | OP_POP | ---body--- | OP_LOOP | OP_POP |
                                        |                                                                                 ^
                                        |                                                                                 |
                                        +---------------------------------------------------------------------------------+
    */
    bool isForeach = false;
    int iterReg = -1;
    int stateReg = -1;

    beginScope(compiler);
    consume(compiler, TOKEN_LEFT_PAREN, "Expect '(' after 'for'.");

    if(match(compiler, TOKEN_SEMICOLON)){
        // No initializer
    }else if(match(compiler, TOKEN_VAR)){
        Token varName = compiler->parser.cur;
        consume(compiler, TOKEN_IDENTIFIER, "Expect variable name.");
        
        if(match(compiler, TOKEN_COLON)){
            isForeach = true;
            iterReg = getFreeReg(compiler);
            reserveReg(compiler, 2);
            stateReg = iterReg + 1;

            ExprDesc iterExpr;
            expression(compiler, &iterExpr);
            expr2Reg(compiler, &iterExpr, iterReg);
            freeExpr(compiler, &iterExpr);

            compiler->freeReg = stateReg + 1;

            emitABC(compiler, OP_LOADNULL, stateReg, 0, 0);

            addLocal(compiler, varName);
            reserveReg(compiler, 1);
            defineVar(compiler, 0);
            consume(compiler, TOKEN_RIGHT_PAREN, "Expect ')' after foreach variable.");
        }else{
            addLocal(compiler, varName);
            int reg = compiler->locals[compiler->localCnt - 1].reg;
            reserveReg(compiler, 1);
            if(match(compiler, TOKEN_ASSIGN)){
                ExprDesc initExpr;
                expression(compiler, &initExpr);
                expr2Reg(compiler, &initExpr, reg);
            }else{
                emitABC(compiler, OP_LOADNULL, reg, 0, 0);
            }
             defineVar(compiler, 0);
             consume(compiler, TOKEN_SEMICOLON, "Expect ';' after loop initializer.");
        }
    }else{
        ExprDesc initExpr;
        expression(compiler, &initExpr);
        freeExpr(compiler, &initExpr);
        consume(compiler, TOKEN_SEMICOLON, "Expect ';' after loop initializer.");
    }

    int loopStart = compiler->func->chunk.count;
    if(compiler->loopCnt == LOOP_MAX){
        errorAt(compiler, &compiler->parser.pre, "Too many nested loops.");
        return;
    }
    
    Loop *loop = &compiler->loops[compiler->loopCnt++];
    loop->start = loopStart;
    loop->scopeDepth = compiler->scopeDepth;
    loop->breakCnt = 0;

    int exitJmp = -1;
    if(isForeach){
        exitJmp = emitAsBx(compiler, OP_FOREACH, iterReg, 0);
    }else{
        if(!match(compiler, TOKEN_SEMICOLON)){
            ExprDesc condition;
            expression(compiler, &condition);
            consume(compiler, TOKEN_SEMICOLON, "Expect ';' after loop condition.");
    
            expr2NextReg(compiler, &condition);
            exitJmp = emitJmpIfFalse(compiler, condition.data.loc.index);
            freeExpr(compiler, &condition);
        }
    }
    
    int bodyJmp = -1;
    int incStart = -1;

    if(!isForeach){
        bodyJmp = emitJmp(compiler);
        incStart = compiler->func->chunk.count;
        loop->start = incStart;

        if(!match(compiler, TOKEN_RIGHT_PAREN)){
            ExprDesc incExpr;
            expression(compiler, &incExpr);
            freeExpr(compiler, &incExpr);
            consume(compiler, TOKEN_RIGHT_PAREN, "Expect ')' after loop increment.");
        }

        int loopJmpIndex = emitJmp(compiler);
        int loopOffset = loopStart - loopJmpIndex - 1;
        compiler->func->chunk.code[loopJmpIndex] = CREATE_AsBx(OP_JMP, 0, loopOffset);
        patchJump(compiler, bodyJmp);
    }

    
    stmt(compiler);

    if(isForeach){
        emitLoop(compiler, loopStart);
    }else{
        int incJmp = emitJmp(compiler);
        int incOffset = incStart - incJmp - 1;
        compiler->func->chunk.code[incJmp] = CREATE_AsBx(OP_JMP, 0, incOffset);
    }

    
    if(exitJmp != -1){
        patchJump(compiler, exitJmp);
    }

    for(int i = 0; i < loop->breakCnt; i++){
        patchJump(compiler, loop->breakJump[i]);
    }

    compiler->loopCnt--;
    endScope(compiler);
}

static void closeUpvalues(Compiler* compiler, int scopeDepth){
    int i = compiler->localCnt - 1;
    while(i >= 0 && compiler->locals[i].depth > scopeDepth){
        i--;
    }

    if(i + 1 < compiler->localCnt){
        emitABC(compiler, OP_CLOSE_UPVAL, i + 1, 0, 0);
    }
}

static void breakStmt(Compiler* compiler){
    if(compiler->loopCnt == 0){
        errorAt(compiler, &compiler->parser.pre, "Cannot use 'break' outside of a loop.");
        return;
    }
    consume(compiler, TOKEN_SEMICOLON, "Expect ';' after 'break'.");

    Loop *loop = &compiler->loops[compiler->loopCnt - 1];
    closeUpvalues(compiler, loop->scopeDepth);
    if(loop->breakCnt == LOOP_MAX){
        errorAt(compiler, &compiler->parser.pre, "Too many breaks in one loop.");
        return;
    }
    loop->breakJump[loop->breakCnt++] = emitJmp(compiler);
}

static void continueStmt(Compiler* compiler){
    if(compiler->loopCnt == 0){
        errorAt(compiler, &compiler->parser.pre, "Cannot use 'continue' outside of a loop.");
        return;
    }
    consume(compiler, TOKEN_SEMICOLON, "Expect ';' after 'continue'.");

    Loop *loop = &compiler->loops[compiler->loopCnt - 1];
    closeUpvalues(compiler, loop->scopeDepth);
    emitLoop(compiler, loop->start);
}

static void switchStmt(Compiler* compiler){
    consume(compiler, TOKEN_LEFT_PAREN, "Expect '(' after 'switch'.");
    ExprDesc condExpr;
    expression(compiler, &condExpr);
    consume(compiler, TOKEN_RIGHT_PAREN, "Expect ')' after switch condition.");
    
    expr2NextReg(compiler, &condExpr);
    int condReg = condExpr.data.loc.index;

    consume(compiler, TOKEN_LEFT_BRACE, "Expect '{' before switch body.");

    int endJmps[CASE_MAX];
    int endJmpCnt = 0;

    int nextCaseJmp = -1;
    bool hasDefault = false;

    while(!checkType(compiler, TOKEN_RIGHT_BRACE) && !checkType(compiler, TOKEN_EOF)){
        if(nextCaseJmp != -1){
            patchJump(compiler, nextCaseJmp);
            nextCaseJmp = -1;
        }

        if(match(compiler, TOKEN_DEFAULT)){
            if(hasDefault){
                errorAt(compiler, &compiler->parser.pre, "Multiple default labels in one switch.");
                return;
            }
            hasDefault = true;
            consume(compiler, TOKEN_FAT_ARROW, "Expect '=>' after 'default'.");
            stmt(compiler);
        }else{
            int bodyJmps[CASE_MAX];
            int bodyJmpCnt = 0;
            do{
                ExprDesc caseExpr;
                expression(compiler, &caseExpr);

                expr2NextReg(compiler, &caseExpr);
                emitABC(compiler, OP_EQ, 1, condReg, caseExpr.data.loc.index);
                freeExpr(compiler, &caseExpr);

                bodyJmps[bodyJmpCnt++] = emitJmp(compiler);

                if(bodyJmpCnt == CASE_MAX){
                    errorAt(compiler, &compiler->parser.pre, "Too many cases in one switch.");
                    return;
                }
            }while(match(compiler, TOKEN_COMMA));

            nextCaseJmp = emitJmp(compiler);
            consume(compiler, TOKEN_FAT_ARROW, "Expect '=>' after case expressions.");
            for(int i = 0; i < bodyJmpCnt; i++){
                patchJump(compiler, bodyJmps[i]);
            }

            stmt(compiler);

            endJmps[endJmpCnt++] = emitJmp(compiler);
        }
    }

    if(nextCaseJmp != -1){
        patchJump(compiler, nextCaseJmp);
    } // no default, no case matched

    for(int i = 0; i < endJmpCnt; i++){
        patchJump(compiler, endJmps[i]);
    }

    consume(compiler, TOKEN_RIGHT_BRACE, "Expect '}' after switch body.");
    freeExpr(compiler, &condExpr);
}

static void systemStmt(Compiler* compiler){
    Token* cmdToken = &compiler->parser.pre;

    Value cmdVal = OBJECT_VAL(copyString(
        compiler->vm, 
        cmdToken->head, 
        cmdToken->len
    ));

    int cmdConst = makeConstant(compiler, cmdVal);

    int cmdReg = getFreeReg(compiler);
    reserveReg(compiler, 1);

    emitABx(compiler, OP_LOADK, cmdReg, cmdConst);
    emitABC(compiler, OP_SYSTEM, cmdReg, cmdReg, 0);

    freeRegs(compiler, 1);
}

static void deferStmt(Compiler* compiler){
    Compiler* funcCompiler = (Compiler*)reallocate(compiler->vm, NULL, 0, sizeof(Compiler));
    if(funcCompiler == NULL){
        errorAt(compiler, &compiler->parser.pre, "Not enough memory for defer.");
        return;
    }

    funcCompiler->parser = compiler->parser;
    funcCompiler->enclosing = compiler;
    funcCompiler->vm = compiler->vm;
    funcCompiler->func = NULL;
    compiler->vm->compiler = funcCompiler;

    initCompiler(funcCompiler, compiler->vm, compiler, TYPE_FUNC, compiler->func->srcName);

    beginScope(funcCompiler);
    stmt(funcCompiler);

    ObjectFunc* func = stopCompiler(funcCompiler);
    int constIndex = makeConstant(compiler, OBJECT_VAL(func));

    int deferReg = getFreeReg(compiler);
    reserveReg(compiler, 1); // reserve register for defer function
    emitClosure(compiler, deferReg, constIndex, funcCompiler);

    emitABC(compiler, OP_DEFER, deferReg, 0, 0);

    compiler->parser = funcCompiler->parser;
    compiler->vm->compiler = compiler;
    reallocate(compiler->vm, funcCompiler, sizeof(Compiler), 0);

    freeRegs(compiler, 1);  // free defer function register
}

static void returnStmt(Compiler* compiler){
    if(compiler->type == TYPE_SCRIPT){
        errorAt(compiler, &compiler->parser.pre, "Cannot return from the top-level.");
    }

    if(match(compiler, TOKEN_SEMICOLON)){
        if(compiler->type == TYPE_INITIALIZER){
            emitABC(compiler, OP_RETURN, 0, 2, 0);
        }else{
            int reg = getFreeReg(compiler);
            emitABC(compiler, OP_LOADNULL, reg, 0, 0);
            emitABC(compiler, OP_RETURN, reg, 2, 0);
        }
    }else{
        if(compiler->type == TYPE_INITIALIZER){
            errorAt(compiler, &compiler->parser.pre, "Cannot return a value from an initializer.");
        }

        ExprDesc retExpr;
        expression(compiler, &retExpr);
        expr2NextReg(compiler, &retExpr);

        emitABC(compiler, OP_RETURN, retExpr.data.loc.index, 2, 0);
        freeExpr(compiler, &retExpr);

        consume(compiler, TOKEN_SEMICOLON, "Expect ';' after return value.");
    }
}

static void parsePrecedence(Compiler* compiler, ExprDesc* expr, Precedence precedence){
    initExpr(expr, EXPR_VOID, 0);
    advance(compiler);
    ParseFunc preRule = getRule(compiler->parser.pre.type)->prefix;
    if(preRule == NULL){
        errorAt(compiler, &compiler->parser.pre, "Expect expression");
        return;
    }

    bool canAssign = precedence <= PREC_ASSIGNMENT;
    preRule(compiler, expr, canAssign);

    while(precedence <= getRule(compiler->parser.cur.type)->precedence){
        advance(compiler);
        ParseFunc inRule = getRule(compiler->parser.pre.type)->infix;
        inRule(compiler, expr, canAssign);
    }

    if(canAssign){
        if(match(compiler, TOKEN_ASSIGN)){
            ExprDesc valExpr;
            expression(compiler, &valExpr);
            storeVar(compiler, expr, &valExpr);
            *expr = valExpr;
        }
        else if(match(compiler, TOKEN_PLUS_EQUAL)){
            ExprDesc augendExpr = *expr;
            expr2NextReg(compiler, &augendExpr);

            ExprDesc valExpr;
            expression(compiler, &valExpr);
            expr2NextReg(compiler, &valExpr);

            emitABC(
                compiler, 
                OP_ADD, 
                augendExpr.data.loc.index, 
                augendExpr.data.loc.index, 
                valExpr.data.loc.index
            );

            freeExpr(compiler, &valExpr);

            storeVar(compiler, expr, &augendExpr);
            *expr = augendExpr;
        }
        else if(match(compiler, TOKEN_MINUS_EQUAL)){
            ExprDesc minuendExpr = *expr;
            expr2NextReg(compiler, &minuendExpr);

            ExprDesc valExpr;
            expression(compiler, &valExpr);
            expr2NextReg(compiler, &valExpr);

            emitABC(
                compiler, 
                OP_SUB, 
                minuendExpr.data.loc.index, 
                minuendExpr.data.loc.index, 
                valExpr.data.loc.index
            );

            freeExpr(compiler, &valExpr);

            storeVar(compiler, expr, &minuendExpr);
            *expr = minuendExpr;
        }
    }
}

static void handlePostfix(Compiler* compiler, ExprDesc* expr, bool canAssign){
    if(!canAssign){
        runtimeError(compiler->vm, "Invalid assignment target.");
        return;
    }

    TokenType type = compiler->parser.pre.type;
    ExprDesc lval = *expr;

    expr2NextReg(compiler, expr);

    int mathReg = getFreeReg(compiler);
    reserveReg(compiler, 1);

    emitABC(
        compiler,
        OP_MOVE,
        mathReg,
        expr->data.loc.index,
        0
    );

    int oneReg = getFreeReg(compiler);
    reserveReg(compiler, 1);
    emitABx(
        compiler,
        OP_LOADK,
        oneReg,
        makeConstant(compiler, NUM_VAL(1))
    );

    if(type == TOKEN_PLUS_PLUS){
        emitABC(
            compiler,
            OP_ADD,
            mathReg,
            mathReg,
            oneReg
        );
    }else if(type == TOKEN_MINUS_MINUS){
        emitABC(
            compiler,
            OP_SUB,
            mathReg,
            mathReg,
            oneReg
        );
    }

    freeRegs(compiler, 1); // free oneReg

    ExprDesc storeExpr;
    initExpr(&storeExpr, EXPR_REG, mathReg);
    storeVar(compiler, &lval, &storeExpr);
}

static void handlePrefix(Compiler* compiler, ExprDesc* expr, bool canAssign){
    TokenType type = compiler->parser.pre.type;

    parsePrecedence(compiler, expr, PREC_UNARY);

    if(!canAssign){
        errorAt(compiler, &compiler->parser.pre, "Invalid assignment target.");
        return;
    }

    ExprDesc lval = *expr;

    // for unary plus and minus, emit code to compute the value first before storing it back
    // e.g. -x => temp = x; temp = 0 - temp; x = temp;
    // to support expressions like -x++ or -(x + 1)

    expr2NextReg(compiler, expr);

    int oneReg = getFreeReg(compiler);
    reserveReg(compiler, 1);
    emitABx(
        compiler,
        OP_LOADK,
        oneReg,
        makeConstant(compiler, NUM_VAL(1))
    );

    if(type == TOKEN_PLUS_PLUS){
        emitABC(
            compiler,
            OP_ADD,
            expr->data.loc.index,
            expr->data.loc.index,
            oneReg
        );
    }else if(type == TOKEN_MINUS_MINUS){
        emitABC(
            compiler,
            OP_SUB,
            expr->data.loc.index,
            expr->data.loc.index,
            oneReg
        );
    }

    freeRegs(compiler, 1);

    int storeReg = getFreeReg(compiler);
    reserveReg(compiler, 1);
    emitABC(
        compiler,
        OP_MOVE,
        storeReg,
        expr->data.loc.index,
        0
    );

    ExprDesc storeExpr;
    initExpr(&storeExpr, EXPR_REG, storeReg);
    storeVar(compiler, &lval, &storeExpr);
}

static int identifierConst(Compiler* compiler){
    Token* name = &compiler->parser.pre;
    Value strVal = OBJECT_VAL(copyString(compiler->vm, name->head, name->len));
    return makeConstant(compiler, strVal);
}

static int globalSlotForToken(Compiler* compiler, Token* name){
    ObjectString* str = copyString(compiler->vm, name->head, name->len);
    uint32_t slot = 0;

    if(!globalEnsureSlot(compiler->vm, compiler->globals, str, &slot)){
        errorAt(compiler, name, "Too many global variables.");
        return 0;
    }

    if(slot > MAX_ARG_BX){
        errorAt(compiler, name, "Too many global variables in one environment.");
        return 0;
    }

    return (int)slot;
}

static int identifierGlobalSlot(Compiler* compiler){
    return globalSlotForToken(compiler, &compiler->parser.pre);
}

static void addLocal(Compiler* compiler, Token name){
    if(compiler->localCnt == LOCAL_MAX){
        errorAt(compiler, &name, "Too many local variables");
        return;
    }
    Local* local = &compiler->locals[compiler->localCnt++];
    local->name = name;
    local->depth = -1;  // sentinel, decl-ed but not def-ed

    local->reg = compiler->freeReg;
    reserveReg(compiler, 1);
}

static void declLocal(Compiler* compiler){
    if(compiler->scopeDepth == 0){
        return;
    }

    Token* name = &compiler->parser.pre;

    for(int i = compiler->localCnt-1; i >= 0; i--){
        Local* local = &compiler->locals[i];
        if(local->depth != -1 && local->depth < compiler->scopeDepth){
            break;
        }
        if(name->len == local->name.len && memcmp(name->head, local->name.head, name->len) == 0){
            errorAt(compiler, name, "Variable with this name already existed.");
        }
    }

    addLocal(compiler, *name);

    if(compiler->freeReg < compiler->localCnt){
        compiler->freeReg = compiler->localCnt;
    }   // local variable is stored in register with same index as local variable count
}

static int parseVar(Compiler* compiler, const char* err){
    consume(compiler, TOKEN_IDENTIFIER, err);
    if(compiler->scopeDepth > 0){
        declLocal(compiler);
        return 0;
    }
    return identifierGlobalSlot(compiler);
}

static void defineVar(Compiler* compiler, int global){
    if(compiler->scopeDepth > 0){
        compiler->locals[compiler->localCnt - 1].depth = compiler->scopeDepth;
        return;
    }
}

static int resolveLocal(Compiler* compiler, Token* name){
    // search matched local variable
    for(int i = compiler->localCnt-1; i >= 0; i--){
        Local* local = &compiler->locals[i];
        if(name->len == local->name.len && 
            memcmp(name->head, local->name.head, name->len) == 0){
                if(local->depth == -1){
                    errorAt(compiler, name, "Cannot read local variable");
                }
                return local->reg;
        }
    }
    return -1;
}

static int addUpvalue(Compiler* compiler, uint16_t index, bool isLocal){
    for(int i = 0; i < compiler->upvalueCnt; i++){
        if(compiler->upvalues[i].index == index && compiler->upvalues[i].isLocal == isLocal){
            return i;
        }
    }

    if(compiler->upvalueCnt == LOCAL_MAX){
        errorAt(compiler, &compiler->parser.pre, "Too many upvalues.");
        return 0;
    }

    compiler->upvalues[compiler->upvalueCnt].isLocal = isLocal;
    compiler->upvalues[compiler->upvalueCnt].index = index;
    compiler->func->upvalueCnt = compiler->upvalueCnt + 1;
    return compiler->upvalueCnt++;    // return current upvalue count
}

static int resolveUpvalue(Compiler* compiler, Token* name){
    if(compiler->enclosing == NULL) return -1;  // top compiler
    
    int localIndex = resolveLocal(compiler->enclosing, name);
    if(localIndex != -1){
        return addUpvalue(compiler, (uint16_t)localIndex, true);
    }

    int upvalueIndex = resolveUpvalue(compiler->enclosing, name);
    // recursive
    if(upvalueIndex != -1){
        return addUpvalue(compiler, (uint16_t)upvalueIndex, false);
    }

    return -1;
}

static int emitJmp(Compiler* compiler){
    int instructionIndex = emitAsBx(compiler, OP_JMP, 0, 0);
    return instructionIndex;
}

static int emitJmpIfFalse(Compiler* compiler, int reg){
    int instructionIndex = emitAsBx(compiler, OP_JMP_IF_FALSE, reg, 0);
    return instructionIndex;
}

static void emitLoop(Compiler* compiler, int loopStart){
    int offset = loopStart - compiler->func->chunk.count - 1;
    if(offset < -OFFSET_sBx){
        errorAt(compiler, &compiler->parser.pre, "Loop body too large.");
    }
    emitAsBx(compiler, OP_JMP, 0, offset);
}

static void emitClosure(Compiler* compiler, int destReg, int constIndex, Compiler* funcCompiler){
    emitABx(compiler, OP_CLOSURE, destReg, constIndex);
    for(int i = 0; i < funcCompiler->upvalueCnt; i++){
        int isLocal = funcCompiler->upvalues[i].isLocal ? 1 : 0;
        int index = funcCompiler->upvalues[i].index;
        emitABC(compiler, OP_LOADNULL, 0, isLocal, index);
    }
}

static void patchJump(Compiler* compiler, int jmpIndex){
    int jmpTarget = compiler->func->chunk.count;
    int offset = jmpTarget - jmpIndex - 1;
    if(offset > MAX_BX >> 1){
        errorAt(compiler, &compiler->parser.pre, "Too much code to jump over.");
    }
    Instruction* instruction = compiler->func->chunk.code;
    Instruction prevInstruction = instruction[jmpIndex];
    instruction[jmpIndex] = CREATE_AsBx(GET_OPCODE(prevInstruction), GET_ARG_A(prevInstruction), offset);
}

static ObjectFunc* stopCompiler(Compiler* compiler){
    if(compiler->type == TYPE_INITIALIZER){
        emitABC(compiler, OP_RETURN, 0, 2, 0);
    }else{
        int reg = getFreeReg(compiler);
        emitABC(compiler, OP_LOADNULL, reg, 0, 0);
        emitABC(compiler, OP_RETURN, reg, 2, 0);
    }

    ObjectFunc* func = compiler->func;

    func->maxRegSlots = compiler->maxRegSlots;

    #ifdef DEBUG_PRINT_CODE
    if(!compiler->parser.hadError){
        printf("== Compiled code: %s ==\n", func->name != NULL ? func->name->chars : "<script>");
        dasmChunk(
            &compiler->func->chunk, 
            func->name != NULL ? func->name->chars : "<script>"
            compiler->globals
        );
    }
    #endif

    return func;
}

static void handleNum(Compiler* compiler, ExprDesc* expr, bool canAssign){
    double value = strtod(compiler->parser.pre.head, NULL);
    initExpr(expr, EXPR_NUM, 0);
    expr->data.num = value;
}

static int makeConstant(Compiler* compiler, Value value){
    push(compiler->vm, value);
    int constIndex = addConstant(compiler->vm, &compiler->func->chunk, value);
    pop(compiler->vm);

    if(constIndex < 0){
        errorAt(compiler, &compiler->parser.pre, "Failed to add constant.");
        return 0;
    }else if(constIndex > MAX_ARG_BX){
        errorAt(compiler, &compiler->parser.pre, "Too many constants in one chunk.");
        return 0;
    }
    return constIndex;
}

static void handleVar(Compiler* compiler, ExprDesc* expr, bool canAssign){
    Token* name = &compiler->parser.pre;

    int index = resolveLocal(compiler, name);
    if(index != -1){
        initExpr(expr, EXPR_LOCAL, index);
    }else if((index = resolveUpvalue(compiler, name)) != -1){
        initExpr(expr, EXPR_UPVAL, index);
    }else{
        index = identifierGlobalSlot(compiler);
        initExpr(expr, EXPR_GLOBAL, index);
    }
}

static void handleGrouping(Compiler* compiler, ExprDesc* expr, bool canAssign){
    expression(compiler, expr);
    consume(compiler, TOKEN_RIGHT_PAREN, "Expected ')' after expression");
}

static void handleUnary(Compiler* compiler, ExprDesc* expr, bool canAssign){
    TokenType type = compiler->parser.pre.type;

    parsePrecedence(compiler, expr, PREC_UNARY);

    if(compiler->opts.foldConst){
        ExprDesc folded;
        if(tryFoldUnary(type, expr, &folded)){
            *expr = folded;
            return;
        }
    }

    expr2NextReg(compiler, expr);
    freeExpr(compiler, expr);
    reserveReg(compiler, 1);

    int targetReg = getFreeReg(compiler) - 1;

    switch(type){
        case TOKEN_MINUS:
            emitABC(compiler, OP_NEG, targetReg, expr->data.loc.index, 0);
            break;
        case TOKEN_NOT:
            emitABC(compiler, OP_NOT, targetReg, expr->data.loc.index, 0);
            break;
        default: return;  // Should not reach here
    }
    initExpr(expr, EXPR_REG, targetReg);
}

static void handleBinary(Compiler* compiler, ExprDesc* expr, bool canAssign){
    TokenType type = compiler->parser.pre.type;
    ParseRule* rule = getRule(type);

    ExprDesc right;
    parsePrecedence(compiler, &right, (Precedence)(rule->precedence + 1));  // parse the right-hand side only if precedence is higher

    if(compiler->opts.foldConst && expr->type == EXPR_NUM && right.type == EXPR_NUM){
        ExprDesc folded;
        if(tryFoldNumBinary(type, expr->data.num, right.data.num, &folded)){
            *expr = folded;
            return;
        }
    }

    if(compiler->opts.foldConst){
        ExprDesc folded;
        if(tryFoldEq(type, expr, &right, &folded)){
            *expr = folded;
            return;
        }
    }

    switch(type){
        case TOKEN_PLUS:            emitBinaryOp(compiler, OP_ADD, expr, &right); break;
        case TOKEN_MINUS:           emitBinaryOp(compiler, OP_SUB, expr, &right); break;
        case TOKEN_STAR:            emitBinaryOp(compiler, OP_MUL, expr, &right); break;
        case TOKEN_SLASH:           emitBinaryOp(compiler, OP_DIV, expr, &right); break;
        case TOKEN_PERCENT:         emitBinaryOp(compiler, OP_MOD, expr, &right); break;
        case TOKEN_EQUAL:
        case TOKEN_NOT_EQUAL:
        case TOKEN_GREATER:
        case TOKEN_LESS:
        case TOKEN_GREATER_EQUAL:
        case TOKEN_LESS_EQUAL:{
            OpCode op;
            int expectTrue = 1;
            switch(type){
                case TOKEN_EQUAL:           op = OP_EQ; break;
                case TOKEN_NOT_EQUAL:       op = OP_EQ; expectTrue = 0; break;
                case TOKEN_LESS:            op = OP_LT; break;
                case TOKEN_LESS_EQUAL:      op = OP_LE; break;
                case TOKEN_GREATER:         op = OP_LE; expectTrue = 0; break;
                case TOKEN_GREATER_EQUAL:   op = OP_LT; expectTrue = 0; break;
                default:                    op = OP_EQ; break;  // Should not reach here
            }
            
            expr2NextReg(compiler, expr);
            expr2NextReg(compiler, &right);
            freeExpr(compiler, &right);
            freeExpr(compiler, expr);

            emitABC(compiler, op, expectTrue, expr->data.loc.index, right.data.loc.index);
            reserveReg(compiler, 1);
            int targetReg = getFreeReg(compiler) - 1;
            emitABC(compiler, OP_LOADBOOL, targetReg, 1, 1);
            // load true into targetReg, and jump over the next instruction
            emitABC(compiler, OP_LOADBOOL, targetReg, 0, 0);
            // load false into targetReg
            initExpr(expr, EXPR_REG, targetReg);
            break;
        }
        default: return;
    }
}

static void handleTernary(Compiler* compiler, ExprDesc* expr, bool canAssign){
    expr2NextReg(compiler, expr);
    int condReg = expr->data.loc.index;

    int elseJmp = emitJmpIfFalse(compiler, condReg);
    freeExpr(compiler, expr);
    reserveReg(compiler, 1);
    int thenReg = getFreeReg(compiler) - 1;

    ExprDesc thenExpr;
    expression(compiler, &thenExpr);

    expr2Reg(compiler, &thenExpr, thenReg);
    freeExpr(compiler, &thenExpr);

    int endJmp = emitJmp(compiler);
    patchJump(compiler, elseJmp);

    consume(compiler, TOKEN_COLON, "Expect ':' in ternary expression.");

    ExprDesc elseExpr;
    expression(compiler, &elseExpr);

    expr2Reg(compiler, &elseExpr, thenReg);
    freeExpr(compiler, &elseExpr);

    patchJump(compiler, endJmp);
    initExpr(expr, EXPR_REG, thenReg);
}

static void handleLiteral(Compiler* compiler, ExprDesc* expr,  bool canAssign){
    TokenType type = compiler->parser.pre.type;
    switch(type){
        case TOKEN_NULL: initExpr(expr, EXPR_NULL, 0); break;
        case TOKEN_TRUE: initExpr(expr, EXPR_TRUE, 0); break;
        case TOKEN_FALSE: initExpr(expr, EXPR_FALSE, 0); break;
        default: return;  // Should not reach here
    }
}

static void handleString(Compiler* compiler, ExprDesc* expr, bool canAssign){
    ExprDesc tmpExpr;
    int partCnt = 0;
    int resReg = getFreeReg(compiler);

    while(compiler->parser.cur.type != TOKEN_STRING_END && compiler->parser.cur.type != TOKEN_EOF){
        if(compiler->parser.cur.type == TOKEN_INTERPOLATION_CONTENT){
            Token* token = &compiler->parser.cur;

            char* unescaped_chars = malloc(token->len + 1);
            if(unescaped_chars == NULL){
                errorAt(compiler, token, "Memory Error while processing string.");
                advance(compiler);
                continue;
            }

            int unescaped_len = 0;
            for(int i = 0; i < token->len; i++){
                if(token->head[i] == '\\' && i + 1 < token->len){
                    i++;
                    switch(token->head[i]){
                        case 'a': unescaped_chars[unescaped_len++] = '\a'; break;
                        case 'b': unescaped_chars[unescaped_len++] = '\b'; break;
                        case 'f': unescaped_chars[unescaped_len++] = '\f'; break;
                        case 'r': unescaped_chars[unescaped_len++] = '\r'; break;
                        case 'n': unescaped_chars[unescaped_len++] = '\n'; break;
                        case 'v': unescaped_chars[unescaped_len++] = '\v'; break;
                        case 't': unescaped_chars[unescaped_len++] = '\t'; break;
                        case '\\': unescaped_chars[unescaped_len++] = '\\'; break;
                        case '"': unescaped_chars[unescaped_len++] = '"'; break;
                        case '$': unescaped_chars[unescaped_len++] = '$'; break;
                        case '0': unescaped_chars[unescaped_len++] = '\0'; break;
                        default: {
                            char err_msg[30];
                            snprintf(err_msg, sizeof(err_msg), "Invalid escape character '\\%c'.", token->head[i]);
                            errorAt(compiler, token, err_msg);
                            unescaped_chars[unescaped_len++] = token->head[i]; 
                            break;
                        }
                    }
                }else{
                    unescaped_chars[unescaped_len++] = token->head[i];
                }
            }

            ObjectString* str = copyString(compiler->vm, unescaped_chars, unescaped_len);
            free(unescaped_chars);
            int idx = makeConstant(compiler, OBJECT_VAL(str));
            initExpr(&tmpExpr, EXPR_K, idx);
            if(partCnt == 0){
                expr2Reg(compiler, &tmpExpr, resReg);
                reserveReg(compiler, 1);
            }else{
                expr2NextReg(compiler, &tmpExpr);
                emitABC(compiler, OP_ADD, resReg, resReg, tmpExpr.data.loc.index);
                freeRegs(compiler, 1);
            }
            advance(compiler);
        }else{
            consume(compiler, TOKEN_INTERPOLATION_START, "Expect string or interpolation.");
            expression(compiler, &tmpExpr);
            expr2NextReg(compiler, &tmpExpr);
            emitABC(
                compiler, 
                OP_TO_STRING, 
                tmpExpr.data.loc.index, 
                tmpExpr.data.loc.index, 
                0
            );

            if(partCnt == 0){
                if(tmpExpr.data.loc.index != resReg){
                    emitABC(compiler, OP_MOVE, resReg, tmpExpr.data.loc.index, 0);
                }
                freeExpr(compiler, &tmpExpr);
                reserveReg(compiler, 1);
            }else{
                emitABC(compiler, OP_ADD, resReg, resReg, tmpExpr.data.loc.index);
                freeExpr(compiler, &tmpExpr);
            }
            consume(compiler, TOKEN_INTERPOLATION_END, "Expect '}' after interpolation expression.");
        }
        partCnt++;
    }

    consume(compiler, TOKEN_STRING_END, "Unterminated string.");

    if(partCnt == 0){
        int idx = makeConstant(compiler, OBJECT_VAL(copyString(compiler->vm, "", 0)));
        initExpr(expr, EXPR_K, idx);
    }else{
        initExpr(expr, EXPR_REG, resReg);
    }
}

static void handleAnd(Compiler* compiler, ExprDesc* expr, bool canAssign){
    expr2NextReg(compiler, expr);
    int endJmp = emitJmpIfFalse(compiler, expr->data.loc.index);
    ExprDesc right;
    parsePrecedence(compiler, &right, PREC_AND);
    expr2Reg(compiler, &right, expr->data.loc.index);
    freeExpr(compiler, &right);
    patchJump(compiler, endJmp);
}

static void handleOr(Compiler* compiler, ExprDesc* expr, bool canAssign){
    expr2NextReg(compiler, expr);
    int endJmp = emitAsBx(compiler, OP_JMP_IF_TRUE, expr->data.loc.index, 0);
    ExprDesc right;
    parsePrecedence(compiler, &right, PREC_OR);
    expr2Reg(compiler, &right, expr->data.loc.index);
    freeExpr(compiler, &right);
    patchJump(compiler, endJmp);
}

static int argList(Compiler* compiler, ExprDesc* func){
    int argCnt = 0;
    expr2NextReg(compiler, func);
    int funcReg = func->data.loc.index;
    if(funcReg < compiler->freeReg - 1){
        reserveReg(compiler, 1);
        int newReg = compiler->freeReg - 1;
        emitABC(compiler, OP_MOVE, newReg, funcReg, 0);
        funcReg = newReg;
        func->data.loc.index = funcReg;
    }

    if(!match(compiler, TOKEN_RIGHT_PAREN)){
        do{
            ExprDesc arg;
            expression(compiler, &arg);
            int targetReg = funcReg + argCnt + 1;  
            // function is at funcReg, arguments start from funcReg + 1
            expr2Reg(compiler, &arg, targetReg);
            freeExpr(compiler, &arg);
            reserveReg(compiler, 1);
            argCnt++;
            if(argCnt >= 255){
                errorAt(compiler, &compiler->parser.pre, "Cannot have more than 255 arguments.");
            }
        }while(match(compiler, TOKEN_COMMA));

        consume(compiler, TOKEN_RIGHT_PAREN, "Expect ')' after arguments.");
    }
    return argCnt;
}

static void handleCall(Compiler* compiler, ExprDesc* expr, bool canAssign){
    int argCount = argList(compiler, expr);
    emitABC(compiler, OP_CALL, expr->data.loc.index, argCount + 1, 2);
    // +1 for the function itself
    freeRegs(compiler, argCount);
    initExpr(expr, EXPR_REG, expr->data.loc.index);
}

static void handlePipe(Compiler* compiler, ExprDesc* expr, bool canAssign){
    expr2NextReg(compiler, expr);
    int argReg = expr->data.loc.index;

    ExprDesc funcExpr;
    parsePrecedence(compiler, &funcExpr, (Precedence)(PREC_PIPE + 1));
    expr2NextReg(compiler, &funcExpr);

    int funcReg = funcExpr.data.loc.index;

    int targetFuncReg = getFreeReg(compiler);
    reserveReg(compiler, 1);

    int targetArgReg = targetFuncReg + 1;

    emitABC(compiler, OP_MOVE, targetFuncReg, funcReg, 0);
    emitABC(compiler, OP_MOVE, targetArgReg, argReg, 0);

    emitABC(compiler, OP_CALL, targetFuncReg, 2, 2);

    freeExpr(compiler, &funcExpr);
    freeRegs(compiler, 2);

    initExpr(expr, EXPR_REG, targetFuncReg);
}

static void handleImport(Compiler* compiler, ExprDesc* expr, bool canAssign){
    consume(compiler, TOKEN_STRING_START, "Expect a string after 'import'.");

    if(compiler->parser.cur.type == TOKEN_STRING_END){
        errorAt(compiler, &compiler->parser.pre, "import path cannot be empty.");
    }else{
        Token *token = &compiler->parser.cur;
        Value valStr = OBJECT_VAL(copyString(compiler->vm, token->head, token->len));
        
        int index = makeConstant(compiler, valStr);

        int destReg = getFreeReg(compiler);
        reserveReg(compiler, 1);

        emitABx(compiler, OP_IMPORT, destReg, index);
        initExpr(expr, EXPR_REG, destReg);
        advance(compiler);
    }

    consume(compiler, TOKEN_STRING_END, "Expected '\"' after import path.");
}

static void handleDot(Compiler* compiler, ExprDesc* expr, bool canAssign){
    consume(compiler, TOKEN_IDENTIFIER, "Expect property name after '.'.");

    Token* name = &compiler->parser.pre;
    int nameConst = makeConstant(compiler, OBJECT_VAL(copyString(compiler->vm, name->head, name->len)));

    expr2NextReg(compiler, expr);
    int objReg = expr->data.loc.index;

    int keyReg = getFreeReg(compiler);
    reserveReg(compiler, 1);
    emitABx(compiler, OP_LOADK, keyReg, nameConst);

    expr->type = EXPR_PROP;
    expr->data.loc.index = objReg;
    expr->data.loc.aux = keyReg;
}

static void handleList(Compiler* compiler, ExprDesc* expr, bool canAssign){
    int listReg = getFreeReg(compiler);
    reserveReg(compiler, 1);

    if(!checkType(compiler, TOKEN_RIGHT_BRACKET)){
        ExprDesc firstElem;
        expression(compiler, &firstElem);

        if(checkType(compiler, TOKEN_SEMICOLON)){
            consume(compiler, TOKEN_SEMICOLON, "Expect ';' for bulk initialization.");

            expr2NextReg(compiler, &firstElem);
            int itemReg = firstElem.data.loc.index;

            ExprDesc countExpr;
            expression(compiler, &countExpr);
            expr2NextReg(compiler, &countExpr);
            int countReg = countExpr.data.loc.index;

            emitABC(compiler, OP_FILL_LIST, listReg, itemReg, countReg);
            freeExpr(compiler, &firstElem);
            freeExpr(compiler, &countExpr);

            consume(compiler, TOKEN_RIGHT_BRACKET, "Expect ']' after list.");
            initExpr(expr, EXPR_REG, listReg);
            return;
        }

        int startReg = getFreeReg(compiler);
        expr2Reg(compiler, &firstElem, startReg);
        reserveReg(compiler, 1);
        freeExpr(compiler, &firstElem);

        int elemCnt = 1;
        while(match(compiler, TOKEN_COMMA)){
            if(elemCnt >= 255){
                errorAt(compiler, &compiler->parser.pre, "Cannot have more than 255 elements in a list.");
            }
            ExprDesc elemExpr;
            expression(compiler, &elemExpr);
            expr2Reg(compiler, &elemExpr, startReg + elemCnt);
            reserveReg(compiler, 1);
            freeExpr(compiler, &elemExpr);
            elemCnt++;
        }

        consume(compiler, TOKEN_RIGHT_BRACKET, "Expect ']' after list.");
        emitABC(compiler, OP_BUILD_LIST, listReg, elemCnt, 0);
        emitABC(compiler, OP_INIT_LIST, listReg, startReg, elemCnt);
        freeRegs(compiler, elemCnt);  // free registers used for elements
    }else{
        consume(compiler, TOKEN_RIGHT_BRACKET, "Expect ']' after list.");
        emitABC(compiler, OP_BUILD_LIST, listReg, 0, 0);
    }

    initExpr(expr, EXPR_REG, listReg);
}

static void handleIndex(Compiler* compiler, ExprDesc* expr, bool canAssign){
    expr2NextReg(compiler, expr);
    int objReg = expr->data.loc.index;
    int baseReg = getFreeReg(compiler);
    ExprDesc startExpr;
    bool isSlice = false;

    if(match(compiler, TOKEN_COLON)){
        isSlice = true;
        reserveReg(compiler, 3);  // reserve registers for start, end, step
        emitABC(compiler, OP_LOADNULL, baseReg, 0, 0);
    }else{
        expression(compiler, &startExpr);
        if(match(compiler, TOKEN_COLON)){
            isSlice = true;
            reserveReg(compiler, 3);  // reserve registers for start, end, step
            expr2Reg(compiler, &startExpr, baseReg);
            freeExpr(compiler, &startExpr);
        }
    }

    if(isSlice){
        int endReg = baseReg + 1;
        int stepReg = baseReg + 2;

        if(checkType(compiler, TOKEN_COLON) || checkType(compiler, TOKEN_RIGHT_BRACKET)){
            // if match [::step] or [start:], end is omitted
            emitABC(compiler, OP_LOADNULL, endReg, 0, 0);
        }else{
            ExprDesc endExpr;
            expression(compiler, &endExpr);
            expr2Reg(compiler, &endExpr, endReg);
            freeExpr(compiler, &endExpr);
        }

        if(match(compiler, TOKEN_COLON)){
            if(checkType(compiler, TOKEN_RIGHT_BRACKET)){ // default step
                emitABC(compiler, OP_LOADNULL, stepReg, 0, 0);
            }else{
                ExprDesc stepExpr;
                expression(compiler, &stepExpr);
                expr2Reg(compiler, &stepExpr, stepReg);
                freeExpr(compiler, &stepExpr);
            }
        }else{  // no step
            emitABC(compiler, OP_LOADNULL, stepReg, 0, 0);
        }

        consume(compiler, TOKEN_RIGHT_BRACKET, "Expect ']' after slice.");
        emitABC(compiler, OP_SLICE, baseReg, objReg, baseReg);
        freeRegs(compiler, 2);  // free endReg and stepReg
        initExpr(expr, EXPR_REG, baseReg);  // save slice result in baseReg
        return;
    }

    consume(compiler, TOKEN_RIGHT_BRACKET, "Expect ']' after index.");

    expr2NextReg(compiler, &startExpr);
    int keyReg = startExpr.data.loc.index;

    expr->type = EXPR_INDEX;
    expr->data.loc.index = objReg;
    expr->data.loc.aux = keyReg;
}

static void handleMap(Compiler* compiler, ExprDesc* expr, bool canAssign){
    int mapReg = getFreeReg(compiler);
    reserveReg(compiler, 1);
    emitABC(compiler, OP_BUILD_MAP, mapReg, 0, 0);

    if(!checkType(compiler, TOKEN_RIGHT_BRACE)){
        do{
            ExprDesc keyExpr;
            expression(compiler, &keyExpr);
            expr2NextReg(compiler, &keyExpr);
            int keyReg = keyExpr.data.loc.index;

            consume(compiler, TOKEN_COLON, "Expect ':' after map key.");

            ExprDesc valueExpr;
            expression(compiler, &valueExpr);
            expr2NextReg(compiler, &valueExpr);
            int valReg = valueExpr.data.loc.index;

            emitABC(compiler, OP_SET_INDEX, mapReg, keyReg, valReg);
            // reuse OP_SET_INDEX for setting map entries

            freeExpr(compiler, &keyExpr);
            freeExpr(compiler, &valueExpr);
        }while(match(compiler, TOKEN_COMMA));
    }
    consume(compiler, TOKEN_RIGHT_BRACE, "Expect '}' after map.");
    initExpr(expr, EXPR_REG, mapReg);
}

static void handleThis(Compiler* compiler, ExprDesc* expr, bool canAssign){
    if(compiler->enclosing == NULL || 
        (compiler->type != TYPE_METHOD && compiler->type != TYPE_INITIALIZER)){
        errorAt(compiler, &compiler->parser.pre, "Cannot use 'this' outside of a method.");
        return;
    }
    if(canAssign && match(compiler, TOKEN_ASSIGN)){
        errorAt(compiler, &compiler->parser.pre, "Cannot assign to 'this'.");
        return;
    }
    initExpr(expr, EXPR_REG, 0);  // 'this' is always at register 0
}

static void consume(Compiler* compiler, TokenType type, const char* errMsg){
    if(compiler->parser.cur.type == type){
        advance(compiler);
        return;
    }
    errorAt(compiler, &compiler->parser.cur, errMsg);
}

static void errorAt(Compiler* compiler, Token* token, const char* message){
    const char* srcName = compiler->func->srcName != NULL 
                            ? compiler->func->srcName->chars 
                            : "<script>";

    fprintf(stderr, "Error [%s, line %d] ",srcName, compiler->parser.cur.line);
    if(token->type != TOKEN_EOF){
        fprintf(stderr, "at '%.*s': ", token->len, token->head);
    }else{
        fprintf(stderr, "at end: ");
    }
    fprintf(stderr, "%s", message);
    fprintf(stderr, "\n");
    compiler->parser.hadError = true;
}
