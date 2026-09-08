#include <string.h>

#include "scanner.h"
#include "common.h"
#include "keywords.h"

static void mark(Scanner* scanner){
    scanner->head = scanner->cur;
    scanner->headPos = scanner->pos;
}

static bool pushMode(Scanner* scanner, ScannerMode mode){
    if(scanner->modeStackTop >= MAX_MODE_STACK - 1){
        return false;
    }

    scanner->modeStack[++scanner->modeStackTop] = mode;
    return true;
}

static bool popMode(Scanner* scanner){
    if(scanner->modeStackTop <= 0){
        return false;
    }

    scanner->modeStackTop--;
    return true;
}

static ScannerMode currentMode(const Scanner* scanner){
    if(scanner->modeStackTop < 0){
        return MODE_DEFAULT;
    }

    return scanner->modeStack[scanner->modeStackTop];
}

void initScanner(Scanner* scanner, const char* code){
    scanner->head = code;
    scanner->cur = code;
    scanner->pos = (SourcePos){.line = 1, .column = 1};
    scanner->headPos = scanner->pos;
    scanner->modeStackTop = -1;
    pushMode(scanner, MODE_DEFAULT);
}

static const char* next(Scanner* scanner){
    if(*scanner->cur == '\0'){
        return NULL;
    }

    const char* current = scanner->cur++;
    scanner->pos.offset++;
    if(*current == '\n'){
        scanner->pos.line++;
        scanner->pos.column = 1;
    }else{
        scanner->pos.column++;
    }
    return current;
}

static bool isNext(Scanner* scanner, char c){
    if(*scanner->cur != c){
        return false;
    }

    next(scanner);
    return true;
}

static Token pack(Scanner* scanner, TokenType type){
    Token token = {
        .type = type,
        .head = scanner->head,
        .len = (int)(scanner->cur - scanner->head),
        .span = {
            .start = scanner->headPos,
            .end = scanner->pos
        },
        .message = NULL
    };
    return token;
}

static Token errorToken(Scanner* scanner, const char* message){
    Token token = pack(scanner, TOKEN_ERROR);
    token.message = message;
    return token;
}

static void skipWhitespace(Scanner* scanner){
    while(*scanner->cur == ' ' || *scanner->cur == '\t' ||
          *scanner->cur == '\n' || *scanner->cur == '\r'){
        next(scanner);
    }
}

static void skipLineComment(Scanner* scanner){
    while(*scanner->cur != '\n' && *scanner->cur != '\0'){
        next(scanner);
    }
}

static bool skipBlockComment(Scanner* scanner){
    next(scanner);
    next(scanner);

    int depth = 1;
    while(depth > 0){
        if(*scanner->cur == '\0'){
            return false;
        }
        if(*scanner->cur == '#' && scanner->cur[1] == '{'){
            next(scanner);
            next(scanner);
            depth++;
        }else if(*scanner->cur == '}' && scanner->cur[1] == '#'){
            next(scanner);
            next(scanner);
            depth--;
        }else{
            next(scanner);
        }
    }
    return true;
}

static const char* skipComment(Scanner* scanner){
    if(scanner->cur[1] == '{'){
        if(!skipBlockComment(scanner)){
            return "Unclosed block comment.";
        }
    }else{
        skipLineComment(scanner);
    }
    return NULL;
}

static bool isDigit(char c){
    return c >= '0' && c <= '9';
}

static bool isAlpha(char c){
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') || c == '_';
}

static Token scanNumber(Scanner* scanner){
    while(isDigit(*scanner->cur)){
        next(scanner);
    }

    if(*scanner->cur == '.' && isDigit(scanner->cur[1])){
        next(scanner);
        while(isDigit(*scanner->cur)){
            next(scanner);
        }
    }

    if(*scanner->cur == 'e' || *scanner->cur == 'E'){
        next(scanner);
        if(*scanner->cur == '+' || *scanner->cur == '-'){
            next(scanner);
        }
        if(!isDigit(*scanner->cur)){
            return errorToken(scanner, "Invalid number format.");
        }
        while(isDigit(*scanner->cur)){
            next(scanner);
        }
    }

    return pack(scanner, TOKEN_NUMBER);
}

static Token scanIdentifier(Scanner* scanner){
    while(isAlpha(*scanner->cur) || isDigit(*scanner->cur)){
        next(scanner);
    }

    Token token = pack(scanner, TOKEN_IDENTIFIER);
    const struct Keyword* keyword = findKeyword(token.head, token.len);
    if(keyword != NULL){
        token.type = keyword->type;
    }
    return token;
}

static Token scanSystem(Scanner* scanner){
    while(*scanner->cur == ' ' || *scanner->cur == '\t'){
        next(scanner);
    }
    mark(scanner);

    while(*scanner->cur != '\n' && *scanner->cur != '\0'){
        next(scanner);
    }

    return pack(scanner, TOKEN_SYSTEM);
}

static Token scanDefault(Scanner* scanner){
    while(true){
        skipWhitespace(scanner);

        if(*scanner->cur != '#'){
            break;
        }
        mark(scanner);
        const char* error = skipComment(scanner);
        if(error != NULL){
            return errorToken(scanner, error);
        }
    }

    mark(scanner);
    if(*scanner->cur == '\0'){
        return pack(scanner, TOKEN_EOF);
    }

    char c = *next(scanner);
    if(isDigit(c)){
        return scanNumber(scanner);
    }
    if(isAlpha(c)){
        return scanIdentifier(scanner);
    }

    switch(c){
        case '+':
            if(isNext(scanner, '=')) return pack(scanner, TOKEN_PLUS_EQUAL);
            if(isNext(scanner, '+')) return pack(scanner, TOKEN_PLUS_PLUS);
            return pack(scanner, TOKEN_PLUS);
        case '-':
            if(isNext(scanner, '=')) return pack(scanner, TOKEN_MINUS_EQUAL);
            if(isNext(scanner, '-')) return pack(scanner, TOKEN_MINUS_MINUS);
            return pack(scanner, TOKEN_MINUS);
        case '/': return pack(scanner, TOKEN_SLASH);
        case '%': return pack(scanner, TOKEN_PERCENT);
        case '(': return pack(scanner, TOKEN_LEFT_PAREN);
        case ')': return pack(scanner, TOKEN_RIGHT_PAREN);
        case '[': return pack(scanner, TOKEN_LEFT_BRACKET);
        case ']': return pack(scanner, TOKEN_RIGHT_BRACKET);
        case '{': return pack(scanner, TOKEN_LEFT_BRACE);
        case '}':
            if(scanner->modeStackTop > 0){
                popMode(scanner);
                return pack(scanner, TOKEN_INTERPOLATION_END);
            }
            return pack(scanner, TOKEN_RIGHT_BRACE);
        case ',': return pack(scanner, TOKEN_COMMA);
        case ';': return pack(scanner, TOKEN_SEMICOLON);
        case '.': return pack(scanner, TOKEN_DOT);
        case ':': return pack(scanner, TOKEN_COLON);
        case '*':
            return pack(
                scanner,
                isNext(scanner, '=') ? TOKEN_EQUAL : TOKEN_STAR
            );
        case '=':
            if(isNext(scanner, '=')) return pack(scanner, TOKEN_EQUAL);
            if(isNext(scanner, '>')) return pack(scanner, TOKEN_FAT_ARROW);
            return pack(scanner, TOKEN_ASSIGN);
        case '!':
            return pack(
                scanner,
                isNext(scanner, '=') ? TOKEN_NOT_EQUAL : TOKEN_NOT
            );
        case '<':
            if(isNext(scanner, '=')) return pack(scanner, TOKEN_LESS_EQUAL);
            if(isNext(scanner, '|')) return pack(scanner, TOKEN_REV_PIPE);
            return pack(scanner, TOKEN_LESS);
        case '>':
            return pack(
                scanner,
                isNext(scanner, '=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER
            );
        case '?': return pack(scanner, TOKEN_QUESTION);
        case '"':
            if(!pushMode(scanner, MODE_IN_STRING)){
                return errorToken(scanner, "String interpolation is too deeply nested.");
            }
            return pack(scanner, TOKEN_STRING_START);
        case '$':
            if(isNext(scanner, '>')) return scanSystem(scanner);
            return errorToken(scanner, "Unexpected character after '$'.");
        case '|':
            if(isNext(scanner, '>')) return pack(scanner, TOKEN_PIPE);
            return errorToken(scanner, "Unexpected character '|'.");
    }
    return errorToken(scanner, "Unrecognized character.");
}

static Token scanString(Scanner* scanner){
    mark(scanner);
    while(*scanner->cur != '"' && *scanner->cur != '\0'){
        if(*scanner->cur == '$' && scanner->cur[1] == '{'){
            break;
        }
        if(*scanner->cur == '\\' && scanner->cur[1] != '\0'){
            next(scanner);
        }
        next(scanner);
    }

    if(scanner->cur > scanner->head){
        return pack(scanner, TOKEN_INTERPOLATION_CONTENT);
    }

    if(*scanner->cur == '$' && scanner->cur[1] == '{'){
        next(scanner);
        next(scanner);
        if(!pushMode(scanner, MODE_DEFAULT)){
            return errorToken(scanner, "String interpolation is too deeply nested.");
        }
        return pack(scanner, TOKEN_INTERPOLATION_START);
    }

    if(*scanner->cur == '"'){
        next(scanner);
        if(!popMode(scanner)){
            return errorToken(scanner, "Invalid scanner mode.");
        }
        return pack(scanner, TOKEN_STRING_END);
    }

    popMode(scanner);
    return errorToken(scanner, "Unterminated string literal.");
}

Token scan(Scanner* scanner){
    switch(currentMode(scanner)){
        case MODE_DEFAULT: return scanDefault(scanner);
        case MODE_IN_STRING: return scanString(scanner);
    }
    return errorToken(scanner, "Invalid scanner mode.");
}
