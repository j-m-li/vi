#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_ENV 32
#define MAX_PROPS 32
#define MAX_PARAMS 8
#define MAX_ARRAY 64
#define MAX_CODE 1024
#define MAX_STR 256

enum ValueType { VAL_UNDEF, VAL_NUMBER, VAL_STRING, VAL_OBJECT, VAL_ARRAY, VAL_FUNCTION, VAL_BREAK, VAL_CONTINUE, VAL_RETURN };

struct Value;
struct Env;
struct Function;

struct Prop {
    char name[32];
    struct Value* value;
};

struct Object {
    struct Prop props[MAX_PROPS];
    int count;
    struct Object* prototype;
};

struct Array {
    struct Value* items[MAX_ARRAY];
    int count;
};

struct Function {
    char name[32];
    char params[MAX_PARAMS][32];
    int param_count;
    char body[MAX_CODE];
    struct Env* closure;
};

struct Value {
    enum ValueType type;
    union {
        double number;
        char* string;
        struct Object* object;
        struct Array* array;
        struct Function* function;
        struct Value* inner; // For break/continue/return
    } as;
};

/* --- Environment --- */
struct EnvEntry {
    char name[32];
    struct Value* value;
};
struct Env {
    struct EnvEntry vars[MAX_ENV];
    int count;
    struct Env* parent;
};

/* --- Value Constructors --- */
struct Value* make_number(double n) {
    struct Value* v = malloc(sizeof(struct Value));
    v->type = VAL_NUMBER; v->as.number = n; return v;
}
struct Value* make_string(const char* s) {
    struct Value* v = malloc(sizeof(struct Value));
    v->type = VAL_STRING;
    v->as.string = malloc(strlen(s)+1);
    strcpy(v->as.string, s);
    return v;
}
struct Value* make_undef() {
    struct Value* v = malloc(sizeof(struct Value));
    v->type = VAL_UNDEF; return v;
}
struct Value* make_object(struct Object* proto) {
    struct Value* v = malloc(sizeof(struct Value));
    v->type = VAL_OBJECT;
    v->as.object = malloc(sizeof(struct Object));
    v->as.object->count = 0;
    v->as.object->prototype = proto;
    return v;
}
struct Value* make_array() {
    struct Value* v = malloc(sizeof(struct Value));
    v->type = VAL_ARRAY;
    v->as.array = malloc(sizeof(struct Array));
    v->as.array->count = 0;
    return v;
}
struct Value* make_function(const char* name, char params[][32], int param_count, const char* body, struct Env* closure) {
    struct Value* v = malloc(sizeof(struct Value));
    v->type = VAL_FUNCTION;
    v->as.function = malloc(sizeof(struct Function));
    strncpy(v->as.function->name, name, 31);
    int i; for (i = 0; i < param_count; ++i) strcpy(v->as.function->params[i], params[i]);
    v->as.function->param_count = param_count;
    strncpy(v->as.function->body, body, MAX_CODE-1);
    v->as.function->closure = closure;
    return v;
}
struct Value* make_ctrl(enum ValueType t, struct Value* inner) {
    struct Value* v = malloc(sizeof(struct Value));
    v->type = t;
    v->as.inner = inner;
    return v;
}

/* --- Environment helpers --- */
struct Env* env_new(struct Env* parent) {
    struct Env* e = malloc(sizeof(struct Env));
    e->count = 0; e->parent = parent; return e;
}
void env_set(struct Env* env, const char* name, struct Value* v) {
    int i;
    for (i = 0; i < env->count; ++i)
        if (strcmp(env->vars[i].name, name) == 0) { env->vars[i].value = v; return; }
    if (env->count < MAX_ENV) {
        strcpy(env->vars[env->count].name, name);
        env->vars[env->count].value = v;
        env->count++;
    }
}
struct Value* env_get(struct Env* env, const char* name) {
    struct Env* e = env;
    while (e) {
        int i;
        for (i = 0; i < e->count; ++i)
            if (strcmp(e->vars[i].name, name) == 0)
                return e->vars[i].value;
        e = e->parent;
    }
    return make_undef();
}

/* --- Object helpers --- */
void obj_set(struct Value* obj, const char* key, struct Value* val) {
    if (obj->type != VAL_OBJECT) return;
    struct Object* o = obj->as.object;
    int i;
    for (i = 0; i < o->count; ++i) {
        if (strcmp(o->props[i].name, key) == 0) {
            o->props[i].value = val;
            return;
        }
    }
    if (o->count < MAX_PROPS) {
        strcpy(o->props[o->count].name, key);
        o->props[o->count].value = val;
        o->count++;
    }
}
struct Value* obj_get(struct Value* obj, const char* key) {
    if (obj->type != VAL_OBJECT) return make_undef();
    struct Object* o = obj->as.object;
    int i;
    for (i = 0; i < o->count; ++i)
        if (strcmp(o->props[i].name, key) == 0)
            return o->props[i].value;
    if (o->prototype) return obj_get((struct Value*)o->prototype, key);
    return make_undef();
}
/* --- Array helpers --- */
void array_push(struct Value* arr, struct Value* val) {
    if (arr->type != VAL_ARRAY) return;
    if (arr->as.array->count < MAX_ARRAY)
        arr->as.array->items[arr->as.array->count++] = val;
}
struct Value* array_get(struct Value* arr, int idx) {
    if (arr->type != VAL_ARRAY) return make_undef();
    if (idx < 0 || idx >= arr->as.array->count) return make_undef();
    return arr->as.array->items[idx];
}
void array_set(struct Value* arr, int idx, struct Value* val) {
    if (arr->type != VAL_ARRAY) return;
    if (idx < 0 || idx >= arr->as.array->count) return;
    arr->as.array->items[idx] = val;
}

/* --- Tokenizer --- */
enum Tok { 
    TK_NONE, TK_NUM, TK_STR, TK_ID, TK_PLUS, TK_MINUS, TK_STAR, TK_SLASH, TK_MOD,
    TK_ASSIGN, TK_SEMI, TK_LPAREN, TK_RPAREN, TK_LBRACE, TK_RBRACE, TK_COMMA, TK_DOT,
    TK_EQ, TK_NEQ, TK_LT, TK_GT, TK_LE, TK_GE, TK_LBRACKET, TK_RBRACKET, TK_COLON,
    TK_IF, TK_ELSE, TK_WHILE, TK_BREAK, TK_CONTINUE, TK_FUNCTION, TK_VAR, TK_RETURN, TK_EOF 
};
struct Token {
    enum Tok type;
    char text[64];
    double num;
};
struct Lexer {
    const char* src;
    int pos;
    struct Token current;
};
void skip(struct Lexer* lex) {
    while (isspace(lex->src[lex->pos])) lex->pos++;
}
int isid0(char c) { return isalpha(c) || c == '_'; }
int isid(char c) { return isalnum(c) || c == '_'; }
void next_token(struct Lexer* lex) {
    skip(lex); char c = lex->src[lex->pos];
    struct Token* t = &lex->current; t->text[0] = 0; t->num = 0;
    if (!c) { t->type = TK_EOF; return; }
    if (isdigit(c)) {
        int i = 0; while (isdigit(lex->src[lex->pos]) || lex->src[lex->pos] == '.')
            t->text[i++] = lex->src[lex->pos++];
        t->text[i]=0; t->type = TK_NUM; t->num = atof(t->text); return;
    }
    if (c == '"') {
        lex->pos++; int i = 0;
        while (lex->src[lex->pos] && lex->src[lex->pos] != '"' && i < MAX_STR-1)
            t->text[i++] = lex->src[lex->pos++];
        t->text[i]=0;
        if (lex->src[lex->pos] == '"') lex->pos++;
        t->type = TK_STR; return;
    }
    if (isid0(c)) {
        int i=0; while (isid(lex->src[lex->pos])) t->text[i++] = lex->src[lex->pos++];
        t->text[i]=0;
        if (strcmp(t->text,"var")==0) t->type=TK_VAR;
        else if (strcmp(t->text,"function")==0) t->type=TK_FUNCTION;
        else if (strcmp(t->text,"return")==0) t->type=TK_RETURN;
        else if (strcmp(t->text,"if")==0) t->type=TK_IF;
        else if (strcmp(t->text,"else")==0) t->type=TK_ELSE;
        else if (strcmp(t->text,"while")==0) t->type=TK_WHILE;
        else if (strcmp(t->text,"break")==0) t->type=TK_BREAK;
        else if (strcmp(t->text,"continue")==0) t->type=TK_CONTINUE;
        else t->type=TK_ID;
        return;
    }
    if (c == '=' && lex->src[lex->pos+1] == '=') { t->type = TK_EQ; lex->pos += 2; return; }
    if (c == '!' && lex->src[lex->pos+1] == '=') { t->type = TK_NEQ; lex->pos += 2; return; }
    if (c == '<' && lex->src[lex->pos+1] == '=') { t->type = TK_LE; lex->pos += 2; return; }
    if (c == '>' && lex->src[lex->pos+1] == '=') { t->type = TK_GE; lex->pos += 2; return; }
    switch (c) {
    case '+': t->type=TK_PLUS; lex->pos++; return;
    case '-': t->type=TK_MINUS; lex->pos++; return;
    case '*': t->type=TK_STAR; lex->pos++; return;
    case '/': t->type=TK_SLASH; lex->pos++; return;
    case '%': t->type=TK_MOD; lex->pos++; return;
    case '=': t->type=TK_ASSIGN; lex->pos++; return;
    case ';': t->type=TK_SEMI; lex->pos++; return;
    case ',': t->type=TK_COMMA; lex->pos++; return;
    case '(': t->type=TK_LPAREN; lex->pos++; return;
    case ')': t->type=TK_RPAREN; lex->pos++; return;
    case '{': t->type=TK_LBRACE; lex->pos++; return;
    case '}': t->type=TK_RBRACE; lex->pos++; return;
    case '.': t->type=TK_DOT; lex->pos++; return;
    case '[': t->type=TK_LBRACKET; lex->pos++; return;
    case ']': t->type=TK_RBRACKET; lex->pos++; return;
    case ':': t->type=TK_COLON; lex->pos++; return; // <-- Fixed: Added TK_COLON handling
    case '<': t->type=TK_LT; lex->pos++; return;
    case '>': t->type=TK_GT; lex->pos++; return;
    }
    printf("Lex error: %c\n",c); exit(1);
}

/* --- Parser/Evaluator --- */
struct Lexer* global_lex = NULL;
struct Value* eval_stmt(struct Env* env);
struct Value* eval_expr(struct Env* env);

int is_truthy(struct Value* v) {
    if (v->type == VAL_NUMBER) return v->as.number != 0;
    if (v->type == VAL_STRING) return v->as.string[0] != 0;
    if (v->type == VAL_UNDEF) return 0;
    return 1;
}

struct Value* eval_primary(struct Env* env) {
    struct Token* tok = &global_lex->current;
    if (tok->type == TK_NUM) {
        double n = tok->num; next_token(global_lex); return make_number(n);
    }
    if (tok->type == TK_STR) {
        char tmp[MAX_STR]; strcpy(tmp, tok->text);
        next_token(global_lex); return make_string(tmp);
    }
    if (tok->type == TK_ID) {
        char id[64]; strcpy(id, tok->text); next_token(global_lex);
        struct Value* v = env_get(env, id);
        while (global_lex->current.type == TK_LPAREN ||
               global_lex->current.type == TK_DOT ||
               global_lex->current.type == TK_LBRACKET) {
            if (global_lex->current.type == TK_LPAREN) {
                // Function call (print, user, or method)
                next_token(global_lex);
                struct Value* args[MAX_PARAMS]; int argc = 0;
                if (global_lex->current.type != TK_RPAREN) {
                    args[argc++] = eval_expr(env);
                    while (global_lex->current.type == TK_COMMA) {
                        next_token(global_lex);
                        args[argc++] = eval_expr(env);
                    }
                }
                if (global_lex->current.type != TK_RPAREN) { printf("Expected )\n"); exit(1); }
                next_token(global_lex);
                if (v->type == VAL_FUNCTION) {
                    struct Function* f = v->as.function;
                    struct Env* callenv = env_new(f->closure);
                    int i;
                    for (i = 0; i < f->param_count; ++i)
                        env_set(callenv, f->params[i], (i<argc)?args[i]:make_undef());
                    struct Lexer saved;
                    memcpy(&saved, global_lex, sizeof(struct Lexer));
                    struct Lexer calllex = {f->body, 0};
                    global_lex = &calllex;
                    next_token(global_lex);
                    struct Value* r = make_undef();
                    while (global_lex->current.type != TK_EOF)
                        r = eval_stmt(callenv);
                    global_lex = &saved;
                    return r;
                } else if (strcmp(id, "print") == 0) {
                    int i; for (i = 0; i < argc; ++i) {
                        if (args[i]->type == VAL_NUMBER) printf("%g", args[i]->as.number);
                        else if (args[i]->type == VAL_STRING) printf("%s", args[i]->as.string);
                        else if (args[i]->type == VAL_UNDEF) printf("undefined");
                        else printf("[object]");
                    }
                    printf("\n"); return make_undef();
                } else { printf("Not a function\n"); exit(1); }
            } else if (global_lex->current.type == TK_DOT) {
                next_token(global_lex);
                if (global_lex->current.type != TK_ID) { printf("Expected property name\n"); exit(1); }
                char prop[64]; strcpy(prop, global_lex->current.text); next_token(global_lex);
                v = obj_get(v, prop);
            } else if (global_lex->current.type == TK_LBRACKET) {
                next_token(global_lex);
                struct Value* idx = eval_expr(env);
                if (global_lex->current.type != TK_RBRACKET) { printf("Expected ]\n"); exit(1); }
                next_token(global_lex);
                v = array_get(v, (int)idx->as.number);
            }
        }
        return v;
    }
    if (tok->type == TK_LPAREN) {
        next_token(global_lex);
        struct Value* v = eval_expr(env);
        if (global_lex->current.type != TK_RPAREN) { printf("Expected )\n"); exit(1); }
        next_token(global_lex); return v;
    }
    if (tok->type == TK_LBRACKET) {
        // Array literal
        next_token(global_lex);
        struct Value* arr = make_array();
        if (global_lex->current.type != TK_RBRACKET) {
            array_push(arr, eval_expr(env));
            while (global_lex->current.type == TK_COMMA) {
                next_token(global_lex);
                array_push(arr, eval_expr(env));
            }
        }
        if (global_lex->current.type != TK_RBRACKET) { printf("Expected ]\n"); exit(1); }
        next_token(global_lex); return arr;
    }
    if (tok->type == TK_LBRACE) {
        // Object literal
        next_token(global_lex);
        struct Value* obj = make_object(NULL);
        if (global_lex->current.type != TK_RBRACE) {
            do {
                if (global_lex->current.type != TK_ID) { printf("Expected key\n"); exit(1); }
                char key[64]; strcpy(key, global_lex->current.text); next_token(global_lex);
                if (global_lex->current.type != TK_COLON) { printf("Expected :\n"); exit(1); }
                next_token(global_lex);
                obj_set(obj, key, eval_expr(env));
                if (global_lex->current.type == TK_COMMA) next_token(global_lex);
                else break;
            } while (1);
        }
        if (global_lex->current.type != TK_RBRACE) { printf("Expected }\n"); exit(1); }
        next_token(global_lex); return obj;
    }
    if (tok->type == TK_FUNCTION) {
        next_token(global_lex);
        char fname[64] = "";
        if (global_lex->current.type == TK_ID)
            { strcpy(fname, global_lex->current.text); next_token(global_lex); }
        if (global_lex->current.type != TK_LPAREN) { printf("Expected (\n"); exit(1); }
        next_token(global_lex);
        char params[MAX_PARAMS][32]; int param_count = 0;
        if (global_lex->current.type != TK_RPAREN) {
            strcpy(params[param_count++], global_lex->current.text); next_token(global_lex);
            while (global_lex->current.type == TK_COMMA) {
                next_token(global_lex);
                strcpy(params[param_count++], global_lex->current.text); next_token(global_lex);
            }
        }
        if (global_lex->current.type != TK_RPAREN) { printf("Expected )\n"); exit(1); }
        next_token(global_lex);
        if (global_lex->current.type != TK_LBRACE) { printf("Expected {\n"); exit(1); }
        // Copy function body as a string until matching }
        int start = ++global_lex->pos, depth = 1;
        while (global_lex->src[global_lex->pos] && depth) {
            if (global_lex->src[global_lex->pos] == '{') depth++;
            else if (global_lex->src[global_lex->pos] == '}') depth--;
            global_lex->pos++;
        }
        int len = global_lex->pos - start - 1;
        char body[MAX_CODE] = {0};
        strncpy(body, global_lex->src + start, len);
        next_token(global_lex);
        return make_function(fname, params, param_count, body, env);
    }
    printf("Parse error\n"); exit(1);
}
struct Value* eval_factor(struct Env* env) {
    struct Value* v = eval_primary(env);
    while (global_lex->current.type == TK_STAR || global_lex->current.type == TK_SLASH ||
           global_lex->current.type == TK_MOD) {
        enum Tok op = global_lex->current.type; next_token(global_lex);
        struct Value* r = eval_primary(env);
        if (v->type == VAL_STRING && op == TK_PLUS && r->type == VAL_STRING) {
            char buf[MAX_STR*2];
            snprintf(buf, sizeof(buf), "%s%s", v->as.string, r->as.string);
            v = make_string(buf);
        } else if (v->type == VAL_NUMBER && r->type == VAL_NUMBER) {
            if (op == TK_STAR) v->as.number *= r->as.number;
            else if (op == TK_SLASH) v->as.number /= r->as.number;
            else if (op == TK_MOD) v->as.number = (int)v->as.number % (int)r->as.number;
        } else { printf("Type error\n"); exit(1); }
    }
    return v;
}
struct Value* eval_term(struct Env* env) {
    struct Value* v = eval_factor(env);
    while (global_lex->current.type == TK_PLUS || global_lex->current.type == TK_MINUS) {
        enum Tok op = global_lex->current.type; next_token(global_lex);
        struct Value* r = eval_factor(env);
        if (v->type == VAL_STRING && op == TK_PLUS && r->type == VAL_STRING) {
            char buf[MAX_STR*2];
            snprintf(buf, sizeof(buf), "%s%s", v->as.string, r->as.string);
            v = make_string(buf);
        } else if (v->type == VAL_NUMBER && r->type == VAL_NUMBER) {
            if (op == TK_PLUS) v->as.number += r->as.number;
            else v->as.number -= r->as.number;
        } else { printf("Type error\n"); exit(1); }
    }
    return v;
}
struct Value* eval_cmp(struct Env* env) {
    struct Value* v = eval_term(env);
    while (global_lex->current.type == TK_LT || global_lex->current.type == TK_GT ||
           global_lex->current.type == TK_LE || global_lex->current.type == TK_GE) {
        enum Tok op = global_lex->current.type; next_token(global_lex);
        struct Value* r = eval_term(env);
        int res = 0;
        if (v->type == VAL_NUMBER && r->type == VAL_NUMBER) {
            if (op == TK_LT) res = (v->as.number < r->as.number);
            else if (op == TK_GT) res = (v->as.number > r->as.number);
            else if (op == TK_LE) res = (v->as.number <= r->as.number);
            else if (op == TK_GE) res = (v->as.number >= r->as.number);
            v = make_number(res);
        } else if (v->type == VAL_STRING && r->type == VAL_STRING) {
            if (op == TK_LT) res = (strcmp(v->as.string, r->as.string) < 0);
            else if (op == TK_GT) res = (strcmp(v->as.string, r->as.string) > 0);
            else if (op == TK_LE) res = (strcmp(v->as.string, r->as.string) <= 0);
            else if (op == TK_GE) res = (strcmp(v->as.string, r->as.string) >= 0);
            v = make_number(res);
        } else { printf("Type error\n"); exit(1); }
    }
    return v;
}
struct Value* eval_eq(struct Env* env) {
    struct Value* v = eval_cmp(env);
    while (global_lex->current.type == TK_EQ || global_lex->current.type == TK_NEQ) {
        enum Tok op = global_lex->current.type; next_token(global_lex);
        struct Value* r = eval_cmp(env);
        int res = 0;
        if (v->type == VAL_NUMBER && r->type == VAL_NUMBER) {
            if (op == TK_EQ) res = (v->as.number == r->as.number);
            else if (op == TK_NEQ) res = (v->as.number != r->as.number);
        } else if (v->type == VAL_STRING && r->type == VAL_STRING) {
            if (op == TK_EQ) res = (strcmp(v->as.string, r->as.string) == 0);
            else if (op == TK_NEQ) res = (strcmp(v->as.string, r->as.string) != 0);
        } else { printf("Type error\n"); exit(1); }
        v = make_number(res);
    }
    return v;
}
struct Value* eval_expr(struct Env* env) { return eval_eq(env); }

struct Value* eval_stmt(struct Env* env) {
    struct Token* tok = &global_lex->current;
    if (tok->type == TK_VAR) {
        next_token(global_lex);
        if (global_lex->current.type != TK_ID) { printf("Expected identifier\n"); exit(1); }
        char id[64]; strcpy(id, global_lex->current.text); next_token(global_lex);
        struct Value* val = make_undef();
        if (global_lex->current.type == TK_ASSIGN) {
            next_token(global_lex);
            val = eval_expr(env);
        }
        env_set(env, id, val);
        if (global_lex->current.type == TK_SEMI) next_token(global_lex);
        return val;
    }
    if (tok->type == TK_IF) {
        next_token(global_lex);
        if (global_lex->current.type != TK_LPAREN) { printf("Expected (\n"); exit(1); }
        next_token(global_lex);
        struct Value* cond = eval_expr(env);
        if (global_lex->current.type != TK_RPAREN) { printf("Expected )\n"); exit(1); }
        next_token(global_lex);
        if (global_lex->current.type == TK_LBRACE) {
            next_token(global_lex);
            if (is_truthy(cond)) {
                while (global_lex->current.type != TK_RBRACE && global_lex->current.type != TK_EOF)
                    eval_stmt(env);
                if (global_lex->current.type == TK_RBRACE) next_token(global_lex);
                if (global_lex->current.type == TK_ELSE) {
                    next_token(global_lex);
                    if (global_lex->current.type == TK_LBRACE) {
                        int depth = 1;
                        next_token(global_lex);
                        while (depth && global_lex->current.type != TK_EOF) {
                            if (global_lex->current.type == TK_LBRACE) depth++;
                            else if (global_lex->current.type == TK_RBRACE) depth--;
                            next_token(global_lex);
                        }
                    }
                }
            } else {
                int depth = 1;
                while (depth && global_lex->current.type != TK_EOF) {
                    if (global_lex->current.type == TK_LBRACE) depth++;
                    else if (global_lex->current.type == TK_RBRACE) depth--;
                    next_token(global_lex);
                }
                if (global_lex->current.type == TK_ELSE) {
                    next_token(global_lex);
                    if (global_lex->current.type == TK_LBRACE) {
                        next_token(global_lex);
                        while (global_lex->current.type != TK_RBRACE && global_lex->current.type != TK_EOF)
                            eval_stmt(env);
                        if (global_lex->current.type == TK_RBRACE) next_token(global_lex);
                    }
                }
            }
        } else { printf("Expected block\n"); exit(1); }
        return make_undef();
    }
    if (tok->type == TK_WHILE) {
        next_token(global_lex);
        if (global_lex->current.type != TK_LPAREN) { printf("Expected (\n"); exit(1); }
        next_token(global_lex);
        int cond_pos = global_lex->pos;
        struct Value* cond = eval_expr(env);
        if (global_lex->current.type != TK_RPAREN) { printf("Expected )\n"); exit(1); }
        next_token(global_lex);
        if (global_lex->current.type != TK_LBRACE) { printf("Expected block\n"); exit(1); }
        int body_start = global_lex->pos+1;
        int body_end, depth = 1;
        while (global_lex->src[global_lex->pos] && depth) {
            if (global_lex->src[global_lex->pos] == '{') depth++;
            else if (global_lex->src[global_lex->pos] == '}') depth--;
            global_lex->pos++;
        }
        body_end = global_lex->pos-1;
        if (global_lex->current.type == TK_RBRACE) next_token(global_lex);
        while (is_truthy(cond)) {
            struct Lexer body_lex = {global_lex->src+body_start,0};
            struct Lexer* old_lex = global_lex;
            global_lex = &body_lex;
            next_token(global_lex);
            while (global_lex->current.type != TK_EOF)
                eval_stmt(env);
            global_lex = old_lex;
            struct Lexer cond_lex = {old_lex->src, cond_pos};
            global_lex = &cond_lex;
            next_token(global_lex);
            cond = eval_expr(env);
            global_lex = old_lex;
        }
        return make_undef();
    }
    if (tok->type == TK_BREAK) { next_token(global_lex); return make_ctrl(VAL_BREAK, NULL); }
    if (tok->type == TK_CONTINUE) { next_token(global_lex); return make_ctrl(VAL_CONTINUE, NULL); }
    if (tok->type == TK_RETURN) {
        next_token(global_lex);
        struct Value* v = eval_expr(env);
        return make_ctrl(VAL_RETURN, v);
    }
    if (tok->type == TK_ID) {
        char id[64]; strcpy(id, tok->text); next_token(global_lex);
        if (global_lex->current.type == TK_ASSIGN) {
            next_token(global_lex);
            struct Value* val = eval_expr(env);
            env_set(env, id, val);
            if (global_lex->current.type == TK_SEMI) next_token(global_lex);
            return val;
        }
        struct Value* target = env_get(env, id);
        if (global_lex->current.type == TK_DOT) {
            next_token(global_lex);
            if (global_lex->current.type != TK_ID) { printf("Expected prop\n"); exit(1); }
            char prop[64]; strcpy(prop, global_lex->current.text); next_token(global_lex);
            if (global_lex->current.type == TK_ASSIGN) {
                next_token(global_lex);
                struct Value* val = eval_expr(env);
                obj_set(target, prop, val);
                if (global_lex->current.type == TK_SEMI) next_token(global_lex);
                return val;
            }
        } else if (global_lex->current.type == TK_LBRACKET) {
            next_token(global_lex);
            struct Value* idx = eval_expr(env);
            if (global_lex->current.type != TK_RBRACKET) { printf("Expected ]\n"); exit(1); }
            next_token(global_lex);
            if (global_lex->current.type == TK_ASSIGN) {
                next_token(global_lex);
                struct Value* val = eval_expr(env);
                array_set(target, (int)idx->as.number, val);
                if (global_lex->current.type == TK_SEMI) next_token(global_lex);
                return val;
            }
        }
        if (global_lex->current.type == TK_SEMI) next_token(global_lex);
        return target;
    }
    if (tok->type == TK_FUNCTION) {
        struct Value* fn = eval_primary(env);
        if (fn->type == VAL_FUNCTION && strlen(fn->as.function->name))
            env_set(env, fn->as.function->name, fn);
        if (global_lex->current.type == TK_SEMI) next_token(global_lex);
        return fn;
    }
    if (tok->type == TK_SEMI) { next_token(global_lex); return make_undef(); }
    struct Value* v = eval_expr(env);
    if (global_lex->current.type == TK_SEMI) next_token(global_lex);
    return v;
}

/* --- Run code --- */
void run(const char* src) {
    struct Lexer lex = {src, 0};
    global_lex = &lex;
    next_token(global_lex);
    struct Env* global = env_new(NULL);
    while (global_lex->current.type != TK_EOF)
        eval_stmt(global);
}

/* --- Demo --- */
int main() {
    printf("Mini JS Interpreter (C90): control flow, strings, error handling\n");
    printf("Supports: var, function, arrays, objects, prototype, string, control flow (if, else, while, break, continue, return)\n");
    printf("Example:\n"
           "var s = \"hi\";\n"
           "var n = 3;\n"
           "while (n > 0) { print(s + \" \" + n); n = n - 1; }\n"
           "if (s == \"hi\") { print(\"yes\"); } else { print(\"no\"); }\n"
           "\nEnter JS code (end with empty line):\n");
    char src[4096] = {0}, line[512];
    while (fgets(line, sizeof(line), stdin)) {
        if (strlen(line) == 1) break;
        strcat(src, line);
    }
    run(src);
    return 0;
}