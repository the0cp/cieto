#include <assert.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "vm.h"
#include "registry.h"
#include "glob.h"

#ifdef _WIN32
    #define PATH_SEP '\\'
    #define PATH_SEP_STR "\\"
#else
    #define PATH_SEP '/'
    #define PATH_SEP_STR "/"
#endif

static char to_lower(char c){
    return (char)tolower((unsigned char)c);
}

static bool is_sep(char c){
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

static bool match_set(char c, const char* set_st, const char** set_end, bool ignoreCase){
    const char* p = set_st;
    bool invert = (*p == '!');
    if(invert)  p++;

    bool match = false;
    char target = ignoreCase ? to_lower(c) : c;

    while(*p && *p != ']'){
        char start = *p;
        p++;
        if(*p == '-' && p[1] != ']'){
            p++;
            char end = *p;
            char lower_st = ignoreCase ? to_lower(start) : start;
            char lower_end = ignoreCase ? to_lower(end) : end;
            if(target >= lower_st && target <= lower_end){
                match = true;
            }
        }else{
            if((ignoreCase ? to_lower(start) : start) == target){
                match = true;
            }
        }
    }
    *set_end = p;
    return invert ? !match : match;
}

static bool match_num_range(const char* text, const char** text_adv, const char* pat_body, const char* pat_end){
    const char* sep = strstr(pat_body, "..");
    if(!sep || sep > pat_end)   return false;

    long min = strtol(pat_body, NULL, 10);
    long max = strtol(sep + 2, NULL, 10);

    char* end_ptr;
    long val = strtol(text, &end_ptr, 10);
    if(end_ptr == text) return false;

    if(val >= min && val <= max){
        *text_adv = end_ptr;
        return true;
    }
    return false;
}

bool glob_match_string(const char* text, const char* pattern, bool ignoreCase){
    while(*pattern){
        char p = *pattern;
        char t = *text;

        if(p == '*'){
            if(*(pattern + 1) == '*'){
                const char* next_pat = pattern + 2;
                if(is_sep(*next_pat))   next_pat++;
                const char* cur_text = text;
                while(true){
                    if(glob_match_string(cur_text, next_pat, ignoreCase)) return true;
                    if(*cur_text == '\0') break;
                    cur_text++;
                }
                return false;
            }else{
                while(*(pattern + 1) == '*')    pattern++;
                if(glob_match_string(text, pattern + 1, ignoreCase))    return true;
                while(*text && !is_sep(*text)){
                    if(glob_match_string(text + 1, pattern + 1, ignoreCase))    return true;
                    text++;
                }
                return false;
            }
        }else if(p == '?'){
            if(!t)  return false;
            text++;
            pattern++;
        }else if(p == '['){
            if(!t)  return false;
            const char* end;
            if(!match_set(t, pattern + 1, &end, ignoreCase))    return false;
            pattern = end + 1;
            text++;
        }else if(p == '{'){
            const char* close = strchr(pattern, '}');
            if(!close)  return false;

            const char* dots = strstr(pattern, "..");
            if(dots && dots < close){
                const char* text_next;
                if(match_num_range(text, &text_next, pattern + 1, close)){
                    return glob_match_string(text_next, close + 1, ignoreCase);
                }
                return false;
            }else{
                const char* cur = pattern + 1;
                while(cur < close){
                    const char* comma = strchr(cur, ',');
                    const char* end_opt = (comma && comma < close) ? comma : close;
                    size_t opt_len = end_opt - cur;
                    bool match_opt = true;

                    for(size_t i = 0; i < opt_len; i++){
                        char tc = text[i];
                        if(!tc){
                            match_opt = false;
                            break;
                        }
                        char pc = cur[i];
                        if(ignoreCase){
                            if(to_lower(tc) != to_lower(pc))    match_opt = false;
                        }else{
                            if(tc != pc)    match_opt = false;
                        }
                    }

                    if(match_opt){
                        if(glob_match_string(text + opt_len, close + 1, ignoreCase))    return true;
                    }

                    cur = end_opt + 1;
                }
                return false;
            }
        }else{
            bool match;
            if(is_sep(p)){
                match = is_sep(t);
            }else if(ignoreCase){
                match = (to_lower(t) == to_lower(p));
            }else{
                match = (t == p);
            }
            if(!match) return false;
            text++;
            pattern++;
        }
    }
    return *text == '\0' && *pattern == '\0';
}

static Value glob_match(VM* vm, int argCount, Value* args){
    if(argCount != 2 || !IS_STRING(args[0]) || !IS_STRING(args[1])){
        runtimeError(vm, "glob.match(pattern, text) expect two strings.\n");
        return BOOL_VAL(false);
    }
    bool matched = glob_match_string(AS_CSTRING(args[1]), AS_CSTRING(args[0]), false);
    return BOOL_VAL(matched);
}

static void defineGlobField(VM* vm, ObjectClass* klass, const char* name, Value value){
    push(vm, value);

    ObjectString* key = copyString(vm, name, (int)strlen(name));
    push(vm, OBJECT_VAL(key));

    bool added = classAddField(vm, klass, key, value);
    assert(added);
    (void)added;

    pop(vm);
    pop(vm);
}

void initGlobModule(VM* vm, ObjectModule* module){
    ObjectString* className = copyString(vm, "Glob", 4);
    push(vm, OBJECT_VAL(className));
    ObjectClass* klass = newClass(vm, className);
    push(vm, OBJECT_VAL(klass));

    ObjectString* patternDefault = copyString(vm, "*", 1);
    push(vm, OBJECT_VAL(patternDefault));
    defineGlobField(vm, klass, "Pattern", OBJECT_VAL(patternDefault));
    pop(vm);

    ObjectString* dirDefault = copyString(vm, ".", 1);
    push(vm, OBJECT_VAL(dirDefault));
    defineGlobField(vm, klass, "Dir", OBJECT_VAL(dirDefault));
    pop(vm);

    defineGlobField(vm, klass, "IgnoreCase", BOOL_VAL(false));
    defineGlobField(vm, klass, "Exclude", NULL_VAL);
    defineGlobField(vm, klass, "Recursive", BOOL_VAL(false));
    
    globalSetName(vm, &module->members, className, OBJECT_VAL(klass));
    
    pop(vm); // klass
    pop(vm); // className

    defineCFunc(vm, &module->members, "match", glob_match);
}
