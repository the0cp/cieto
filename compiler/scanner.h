#ifndef CIETO_SCANNER_H
#define CIETO_SCANNER_H

#include <stdbool.h>

#define MAX_MODE_STACK 16

typedef enum{
    MODE_DEFAULT,
    MODE_IN_STRING,
}ScannerMode;

typedef enum{
    TOKEN_EOF, TOKEN_ERROR,
    TOKEN_IDENTIFIER,
    TOKEN_IF, TOKEN_ELSE,
    TOKEN_WHILE, TOKEN_FOR,
    TOKEN_BREAK, TOKEN_CONTINUE,
    TOKEN_SWITCH, TOKEN_DEFAULT, TOKEN_FAT_ARROW,
    TOKEN_FUNC, TOKEN_RETURN, TOKEN_CLASS, TOKEN_THIS, TOKEN_METHOD,
    TOKEN_TRUE, TOKEN_FALSE,
    TOKEN_VAR,
    TOKEN_NULL,
    TOKEN_NUMBER,
    TOKEN_STRING_START, TOKEN_STRING_END,
    TOKEN_INTERPOLATION_START, TOKEN_INTERPOLATION_END, TOKEN_INTERPOLATION_CONTENT,
    TOKEN_PLUS, TOKEN_MINUS, TOKEN_STAR, TOKEN_SLASH, TOKEN_PERCENT, 
    TOKEN_PLUS_EQUAL, TOKEN_MINUS_EQUAL, TOKEN_PLUS_PLUS, TOKEN_MINUS_MINUS,
    TOKEN_EQUAL, TOKEN_NOT_EQUAL, TOKEN_NOT, TOKEN_QUESTION, 
    TOKEN_AND, TOKEN_OR,
    TOKEN_LESS, TOKEN_GREATER, TOKEN_LESS_EQUAL, TOKEN_GREATER_EQUAL,
    TOKEN_ASSIGN,
    TOKEN_LEFT_PAREN, TOKEN_RIGHT_PAREN,
    TOKEN_LEFT_BRACE, TOKEN_RIGHT_BRACE,
    TOKEN_LEFT_BRACKET, TOKEN_RIGHT_BRACKET,
    TOKEN_COMMA, TOKEN_SEMICOLON, TOKEN_DOT, TOKEN_COLON,
    TOKEN_PRINT,
    TOKEN_IMPORT,
    TOKEN_SYSTEM, TOKEN_PIPE, TOKEN_REV_PIPE,
    TOKEN_DEFER, 
}TokenType;

typedef struct{
    const char* head;
    const char* cur;
    int line;
    ScannerMode modeStack[MAX_MODE_STACK];
    int modeStackTop;
}Scanner;

typedef struct{
    TokenType type;
    const char* head;
    int len;
    int line;
}Token;

void initScanner(const char* code);
static Token scanDefault();
static Token scanString();
static Token scanSystem();
Token scan();

static inline void pushMode(ScannerMode mode);
static inline ScannerMode popMode();
static inline ScannerMode currentMode();

static inline const char* next();
static inline bool is_next(char c);

static inline Token pack(TokenType type, const char* head, int len, int line);
static inline Token error(const char* message, int line);

static inline void skipWhitespace();
static inline void handleLineComment();
static inline void handleBlockComment();
static bool handleComment();

static Token handleNumber();
static TokenType identifierType();
static inline Token handleIdentifier();

static inline bool isDigit(char c);
static inline bool isAlpha(char c);

#endif // CIETO_SCANNER_H
