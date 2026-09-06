#include <stdio.h>
#include <string.h>

#include "scanner.h"
#include "common.h"
#include "keywords.h"

Scanner sc;

void initScanner(const char* code){
    sc.head = code;
    sc.cur = code;
    sc.line = 1;
    sc.modeStackTop = -1;   // Initialize mode stack top
    pushMode(MODE_DEFAULT); // Start in default mode
}

static inline void pushMode(ScannerMode mode){
    if(sc.modeStackTop < MAX_MODE_STACK - 1){
        sc.modeStack[++sc.modeStackTop] = mode;
    }else{
        fprintf(stderr, "Error: Scanner Mode stack overflow at line %d\n", sc.line);
    }
}

static inline ScannerMode popMode(){
    if(sc.modeStackTop > 0){
        return sc.modeStack[sc.modeStackTop--];
    }else{
        fprintf(stderr, "Error: Scanner Mode stack underflow at line %d\n", sc.line);
        return MODE_DEFAULT; // Default mode if stack is empty
    }
}

static inline ScannerMode currentMode(){
    if(sc.modeStackTop >= 0){
        return sc.modeStack[sc.modeStackTop];
    }
    return MODE_DEFAULT; // Default mode if stack is empty
}

static inline Token pack(TokenType type, const char* head, int len, int line){
    Token token;
    token.type = type;
    token.head = head;
    token.len = len;
    token.line = line;
    return token;
}

static inline Token error(const char* message, int line){
    fprintf(stderr, "Error at line %d: %s\n", line, message);
    return pack(TOKEN_ERROR, message, (int)strlen(message), line);
}

static inline const char* next(){
    if(*sc.cur == '\0') return NULL;
    return sc.cur++;
}

static inline bool is_next(char c){
    if(*sc.cur == '\0') return false;
    if(*sc.cur == c){
        sc.cur++;
        return true;
    }
    return false;
}

static inline void skipWhitespace(){
    while(*sc.cur == ' ' || *sc.cur == '\t' || *sc.cur == '\n' || *sc.cur == '\r'){
        if(*sc.cur == '\n') sc.line++;
        next();
    }
    return;
}

static inline void handleLineComment(){
    while(*sc.cur != '\n' && *sc.cur != '\0'){
        next();
    }
}

static inline void handleBlockComment(){
    next(); next(); // Skip #{
    int depth = 1;
    while(depth > 0 && *sc.cur != '\0'){
        if(*sc.cur == '#' && sc.cur[1] == '{'){
            next(); next(); // Skip #{
            depth++;
            continue;
        }

        if(*sc.cur == '}' && sc.cur[1] == '#'){
            next(); next(); // Skip }#
            depth--;
            continue;
        }

        if(*sc.cur == '\n') sc.line++;

        next();

        if(*sc.cur == '\0' && depth > 0){
            fprintf(stderr, "Error: Unclosed block comment at line %d\n", sc.line);
            return;
        }
    }
}

static bool handleComment(){
    if(*sc.cur == '#'){
        // This also supports Unix shebang lines, 
        // e.g. "#!/usr/bin/env cieto".
        if(sc.cur[1] == '{'){
            handleBlockComment();
        }else{
            handleLineComment();
        }
        return true;
    }
    return false;
}

static inline bool isDigit(char c){
    return c >= '0' && c <= '9';
}

static inline bool isAlpha(char c){
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static Token handleNumber(){
    while(isDigit(*sc.cur)){
        next();
    }

    if(*sc.cur == '.' && isDigit(sc.cur[1])){
        next();
        while(isDigit(*sc.cur)){
            next();
        }
    }

    if(*sc.cur == 'e' || *sc.cur == 'E'){
        next();
        if(*sc.cur == '+' || *sc.cur == '-'){
            next();
        }
        if(!isDigit(*sc.cur)){
            return error("Invalid number format.", sc.line);
        }
        while(isDigit(*sc.cur)){
            next();
        }
    }

    return pack(TOKEN_NUMBER, sc.head, (int)(sc.cur - sc.head), sc.line);
}

static inline Token handleIdentifier(){
    while(isAlpha(*sc.cur) || isDigit(*sc.cur)){
        next();
    }

    return pack(TOKEN_IDENTIFIER, sc.head, (int)(sc.cur - sc.head), sc.line);
}

static TokenType identifierType(){
    const struct Keyword* keyword = findKeyword(sc.head, (int)(sc.cur - sc.head));
    if(keyword != NULL){
        return keyword->type;
    }
    return TOKEN_IDENTIFIER;
}

static Token scanDefault(){
    while(true){
        skipWhitespace();
        if(handleComment()){
            continue; // Skip comments
        }
        break;
    }
    
    sc.head = sc.cur;
    if(*sc.cur == '\0') {
        return pack(TOKEN_EOF, sc.head, 0, sc.line);
    }

    char c = *next();

    if(isDigit(c)){
        return handleNumber();
    }

    if(isAlpha(c)){
        Token token = handleIdentifier();
        token.type = identifierType();
        return token;
    }

    switch(c){
        case '+': 
            if(is_next('='))    return pack(TOKEN_PLUS_EQUAL, sc.head, 2, sc.line);
            if(is_next('+'))    return pack(TOKEN_PLUS_PLUS, sc.head, 2, sc.line);
            return pack(TOKEN_PLUS, sc.head, 1, sc.line);
        case '-': 
            if(is_next('='))    return pack(TOKEN_MINUS_EQUAL, sc.head, 2, sc.line);
            if(is_next('-'))    return pack(TOKEN_MINUS_MINUS, sc.head, 2, sc.line);
            return pack(TOKEN_MINUS, sc.head, 1, sc.line);
        case '/': return pack(TOKEN_SLASH, sc.head, 1, sc.line);
        case '%': return pack(TOKEN_PERCENT, sc.head, 1, sc.line);
        case '(': return pack(TOKEN_LEFT_PAREN, sc.head, 1, sc.line);
        case ')': return pack(TOKEN_RIGHT_PAREN, sc.head, 1, sc.line);
        case '[': return pack(TOKEN_LEFT_BRACKET, sc.head, 1, sc.line);
        case ']': return pack(TOKEN_RIGHT_BRACKET, sc.head, 1, sc.line);
        case '{': return pack(TOKEN_LEFT_BRACE, sc.head, 1, sc.line);
        case '}': 
            if(sc.modeStackTop > 0){
                popMode();
                return pack(TOKEN_INTERPOLATION_END, sc.head, 1, sc.line);
            }
            return pack(TOKEN_RIGHT_BRACE, sc.head, 1, sc.line);
        case ',': return pack(TOKEN_COMMA, sc.head, 1, sc.line);
        case ';': return pack(TOKEN_SEMICOLON, sc.head, 1, sc.line);
        case '.': return pack(TOKEN_DOT, sc.head, 1, sc.line);
        case ':': return pack(TOKEN_COLON, sc.head, 1, sc.line);
        case '*':
            return pack(is_next('=') ? TOKEN_EQUAL : TOKEN_STAR, sc.head, (int)(sc.cur - sc.head), sc.line);
        case '=':
            if(is_next('='))
                return pack(TOKEN_EQUAL, sc.head, 2, sc.line);
            else if(is_next('>'))
                return pack(TOKEN_FAT_ARROW, sc.head, 2, sc.line);
            else
                return pack(TOKEN_ASSIGN, sc.head, 1, sc.line);
        case '!':
            return pack(is_next('=') ? TOKEN_NOT_EQUAL : TOKEN_NOT, sc.head, (int)(sc.cur - sc.head), sc.line);
        case '<':
            if(is_next('=')) return pack(TOKEN_LESS_EQUAL, sc.head, 2, sc.line);
            if(is_next('|')) return pack(TOKEN_REV_PIPE, sc.head, 2, sc.line);
            return pack(TOKEN_LESS, sc.head, 1, sc.line);
        case '>':
            return pack(is_next('=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER, sc.head, (int)(sc.cur - sc.head), sc.line);
        case '?': return pack(TOKEN_QUESTION, sc.head, 1, sc.line);
        case '"':
            pushMode(MODE_IN_STRING);
            return pack(TOKEN_STRING_START, sc.head, 1, sc.line);
        case '$':
            if(is_next('>'))    return scanSystem();
            return error("Unexpected character after '$'.", sc.line);
        case '|':
            if(is_next('>'))    return pack(TOKEN_PIPE, sc.head, 2, sc.line);
            return error("Unexpected character '|'.", sc.line);
    }
    return error("Unrecognized character", sc.line);
}

static Token scanString(){
    sc.head = sc.cur;
    while(*sc.cur != '"' && *sc.cur != '\0'){
        if(*sc.cur == '$' && sc.cur[1] == '{'){
            break; // Interpolation start
        }

        if(*sc.cur == '\\' && sc.cur[1] != '\0'){
            next(); // Skip escape character, it is necessary while precessing '\$' etc.
        }
        next();
    }

    if(sc.cur > sc.head){
        return pack(TOKEN_INTERPOLATION_CONTENT, sc.head, (int)(sc.cur - sc.head), sc.line);
    }

    if(*sc.cur == '$' && sc.cur[1] == '{'){
        next(); next(); // Skip ${
        pushMode(MODE_DEFAULT);
        return pack(TOKEN_INTERPOLATION_START, sc.head, 2, sc.line);
    }

    if(*sc.cur == '"'){
        next();     // Skip closing quote
        popMode();  // Exit string mode
        return pack(TOKEN_STRING_END, sc.head, (int)(sc.cur - sc.head), sc.line);
    }

    popMode();
    return error("Unterminated string literal", sc.line);
}

static Token scanSystem(){
    while(*sc.cur == ' ' || *sc.cur == '\t'){
        sc.cur++;
    }

    const char* cmdSt = sc.cur;

    while(*sc.cur != '\n' && *sc.cur != '\0'){
        sc.cur++;
    }

    return pack(TOKEN_SYSTEM, cmdSt, (int)(sc.cur - cmdSt), sc.line);
}

static Token scanComment(){

}

Token scan(){
    switch(currentMode()){
        case MODE_DEFAULT: return scanDefault();
        case MODE_IN_STRING: return scanString();
        default:
            return error("Unknown scanner mode", sc.line);
    }
}
