#include "tape.h"
#include "slice.h"
#include "darray.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <ctype.h>
#include <stdarg.h>

#define DEBUG

Slice kwords[] = {
    {.data = "tm", .len = 2},
    {.data = "ltm", .len = 3},
};
size_t kwlen = sizeof(kwords) / sizeof(kwords[0]);

bool is_keyword(Slice s) {
    for(size_t i = 0; i < kwlen; ++i) if(slice_eq(s, kwords[i])) return true;
    return false;
}

typedef struct {
    const char *fp;
    const char *start;
    const char *current;
    const char *row_start;
    size_t row;
} Lex;

typedef enum {
    TT_UNKNOWN,
    TT_EOF,
    TT_OPENING,
    TT_CLOSING,
    TT_CHAR,
    TT_IDEN,
    TT_DIR,
    TT_KEYWORD,
    TT_STRING,
    TT_ARROW,
    TT_COUNT,
} TokType;

typedef struct {
    const char *fp;
    size_t row;
    size_t col;
} Loc;

typedef struct {
    TokType type;
    Slice raw;
    Loc loc;
} Tok;

Lex lex_create(const char *fp, const char *data) {
    return (Lex) {
        .fp = fp,
        .start = data,
        .current = data,
        .row_start = data,
        .row = 1,
    };
}

_STATIC_ASSERT(TT_COUNT == 10);
const char *tok_type_to_str(TokType type) {
    switch(type) {
        case TT_UNKNOWN: return "TT_UNKNOWN";
        case TT_EOF: return "TT_EOF";
        case TT_OPENING: return "TT_OPENING";
        case TT_CLOSING: return "TT_CLOSING";
        case TT_CHAR: return "TT_CHAR";
        case TT_IDEN: return "TT_IDEN";
        case TT_DIR: return "TT_DIR";
        case TT_KEYWORD: return "TT_KEYWORD";
        case TT_STRING: return "TT_STRING";
        case TT_ARROW: return "TT_ARROW";
        default: exit(1);
    }
}

void tok_print(Tok t) {
    printf(
        "z.ltm:%lld:%lld ."SLICE_FMT". %s %lld\n", 
        t.loc.row, t.loc.col, SLICE_ARG(t.raw),
        tok_type_to_str(t.type), t.raw.len
    );
}

Tok tok_create(TokType type, Slice raw, Loc loc) {
    return (Tok) {
        .type = type,
        .raw = raw,
        .loc = loc,
    };
}

int notdoublequotes(int c) {
    return c != '"';
}

Slice tok_parse(const char *start, int (*predicate)(int)) {
    const char *end = start;
    while(predicate(*end)) ++end;
    return slice_create(start, end - start);
}

int allchars(int c) {
    return !isspace(c) && (c != '\0');
}

Slice stringParse(const char *start) {
    const char *end = start + 1;
    while(*end != '"' && *end != '\0') ++end;
    return slice_create(start, end - start);
}

void tok_report(Tok t, const char *msg, ...) {
    va_list va;
    va_start(va, msg);
    fprintf(stderr, "%s:%lld:%lld: "SLICE_FMT" ", t.loc.fp, t.loc.row, t.loc.col, SLICE_ARG(t.raw));
    vfprintf(stderr, msg, va);
    va_end(va);
    exit(1);
}

int wholeline(int c) {
    return c != '\0' && c != '\n' && c != '\r';
}

#define LOCATION ((Loc){.fp = l->fp, .row = row, .col = start - row_start + 1})

Tok lex_peek(Lex *l) {

    size_t row = l->row;
    const char *row_start = l->row_start;

redo:
    while(isspace(*l->current)) {
        if(*l->current == '\n') {
            ++row;
            row_start = l->current + 1;
        }
        ++l->current;
    }
    
    const char *start = l->current;
    if(*start == '\0') return tok_create(TT_EOF, (Slice){0}, LOCATION);
    
    if(start[0] == '-' && start[1] == '-') {
        while(*l->current != '\n') {
            if(*l->current == '\n') {
                ++row;
                row_start = l->current + 1;
            }
            ++l->current;
        }
        goto redo;
    }

    if(isalpha(*start)) {
        Slice raw = tok_parse(start, isalnum);
        return tok_create(is_keyword(raw) ? TT_KEYWORD : TT_IDEN, raw, LOCATION);
    } else if(*start == '"') {
        Slice raw = stringParse(start);
        Tok t = tok_create(TT_STRING, raw, LOCATION);
        if(raw.data[raw.len] != '"') tok_report(t, "Unfinished string\n");
        ++t.raw.len;
        return t;
    }

    Slice raw = tok_parse(start, allchars);
    if(slice_eq(raw, slice_create_raw("{"))) return tok_create(TT_OPENING, raw, LOCATION);
    else if(slice_eq(raw, slice_create_raw("}"))) return tok_create(TT_CLOSING, raw, LOCATION);
    else if(raw.len == 3 && raw.data[0] == '\'' && raw.data[2] == '\'') return tok_create(TT_CHAR, raw, LOCATION);
    else if(raw.len == 1 && (*raw.data == '<' || *raw.data == '-' || *raw.data == '>')) return tok_create(TT_DIR, raw, LOCATION);
    else if(raw.len == 2 && raw.data[0] == '=' && raw.data[1] == '>') return tok_create(TT_ARROW, raw, LOCATION);

    return tok_create(TT_UNKNOWN, raw, LOCATION);
}

Tok lex_next(Lex *l) {
    Tok t = lex_peek(l);
#ifdef DEBUG
    tok_print(t);
#endif
    l->current = t.raw.data + t.raw.len;
    l->row = t.loc.row;
    l->row_start = t.raw.data - t.loc.col + 1;
    return t;
}

typedef enum {IT_NOP, IT_DECL_TM, IT_PUSH_RULE, IT_DECL_LTM, IT_FEED, IT_CALL, IT_COUNT} InsType;

typedef struct {
    Tok state;
    Tok read;
    Tok write;
    Tok dir;
    Tok next;
} Rule;

typedef union {
    Tok iden;
    Rule rule;
} InsValue;

typedef struct {
    InsType type;
    InsValue as;
} Ins;

typedef struct {
    Ins *data;
    size_t len;
    size_t cap;
} Program;

_STATIC_ASSERT(IT_COUNT == 6);
const char *instype_to_str(InsType type) {
    switch(type) {
        case IT_NOP: return "IT_NOP";
        case IT_DECL_TM: return "IT_DECL_TM";
        case IT_PUSH_RULE: return "IT_PUSH_RULE";
        case IT_DECL_LTM: return "IT_DECL_LTM";
        case IT_FEED: return "IT_FEED";
        case IT_CALL: return "IT_CALL";
        default: exit(1);
    }
}

typedef enum {STATE_REGULAR, STATE_DECL_TM, STATE_DECL_LTM, STATE_COUNT} State;

Dir dir_from_char(char c) {
    switch(c) {
        case '<': return DIRECTION_LEFT;
        case '-': return DIRECTION_NONE;
        case '>': return DIRECTION_RIGHT;
        default: fprintf(stderr, "Unreachable\n"); exit(1);
    }
}

char dir_to_char(Dir d) {
    switch(d) {
        case DIRECTION_LEFT: return '<';
        case DIRECTION_NONE: return '-';
        case DIRECTION_RIGHT: return '>';
        default: fprintf(stderr, "Unreachable\n"); exit(1);
    }
}

Slice tm, ltm;

void unreachable(size_t line) {
    fprintf(stderr, "%s:%lld: unreachable\n", __FILE__, line);
    exit(1);
}

void unhandled(size_t line) {
    fprintf(stderr, "%s:%lld: unhandled\n", __FILE__, line);
    exit(1);
}

Tok lex_expect(Lex *l, TokType type) {
    Tok t = lex_next(l);
    if(t.type != type) tok_report(t, "Unexpected token type %s. Expected was %s", tok_type_to_str(t.type), tok_type_to_str(type));
    return t;
}

Program lex_file(const char *fp) {

    Slice s = slice_from_file(fp);
    Lex l = lex_create(fp, s.data);
    
    Tok t = {0};
    Program p = {0};
    State state = STATE_REGULAR;

    while(t.type != TT_EOF) {

        t = lex_next(&l);

        _STATIC_ASSERT(STATE_COUNT == 3);
        switch(state) {

            default: unreachable(__LINE__); break;

            case STATE_REGULAR: {
                switch(t.type) {

                    case TT_EOF: break;
                    default: tok_report(t, "Invalid token. Expected tokens are: keywords or strings\n"); break;

                    case TT_KEYWORD: {
                        if(!slice_eq(t.raw, tm) && !slice_eq(t.raw, ltm)) unhandled(__LINE__);
                        Tok iden = lex_expect(&l, TT_IDEN);
                        lex_expect(&l, TT_OPENING);
                        Ins i = {.type = slice_eq(t.raw, tm) ? IT_DECL_TM : IT_DECL_LTM, .as.iden = iden};
                        da_append(p, i);
                        state = slice_eq(t.raw, tm) ? STATE_DECL_TM : STATE_DECL_LTM;
                    } break;

                    case TT_STRING: {
                        lex_expect(&l, TT_ARROW);
                        lex_expect(&l, TT_OPENING);
                        Ins i = {.type = IT_FEED, .as.iden = t};
                        da_append(p, i);
                        state = STATE_DECL_LTM;
                    } break;
                }
            } break;

            case STATE_DECL_TM: {
                switch(t.type) {

                    default: tok_report(t, "Invalid token. Expected tokens are: identifier or }\n"); break;

                    case TT_CLOSING: state = STATE_REGULAR; break;

                    case TT_IDEN: {
                        Tok read  = lex_expect(&l, TT_CHAR);
                        Tok write = lex_expect(&l, TT_CHAR);
                        Tok dir   = lex_expect(&l, TT_DIR);
                        Tok next  = lex_expect(&l, TT_IDEN);
                        Ins i = {.type = IT_PUSH_RULE, .as.rule = {.state = t, .read = read, .write = write, .dir = dir, .next = next}};
                        da_append(p, i);
                    } break;
                }
            } break;

            case STATE_DECL_LTM: {
                switch(t.type) {

                    default: tok_report(t, "Invalid token. Expected tokens are: identifier or }\n"); break;

                    case TT_CLOSING: state = STATE_REGULAR; break;

                    case TT_IDEN: {
                        Ins i = {.type = IT_CALL, .as.iden = t};
                        da_append(p, i);
                    } break;
                }
            } break;
        }
    }

    return p;
}

// void run(Program p) {

//     State state = STATE_REGULAR;

//     for(size_t k = 0; k < p.len; ++k) {
//         Ins i = p.data[k];
//         switch(state) {

//             default: unreachable(__LINE__); break;

//             case STATE_REGULAR: {} break;
//             case STATE_DECL_TM: {} break;
//             case STATE_DECL_LTM: {} break;
//         }
//     }
// }

int main(void) {
    
    tm = slice_create_raw("tm");
    ltm = slice_create_raw("ltm");

    Program p = lex_file("z.ltm");

    printf("======================\n");
    for(size_t i = 0; i < p.len; ++i) {
        printf("%s(%d): ", instype_to_str(p.data[i].type), p.data[i].type);
        switch(p.data[i].type) {
            case IT_DECL_LTM:
            case IT_FEED:
            case IT_CALL:
            case IT_DECL_TM: tok_print(p.data[i].as.iden); break;
            case IT_PUSH_RULE: {
                Rule r = p.data[i].as.rule;
                printf(
                    SLICE_FMT" "SLICE_FMT" "SLICE_FMT" "SLICE_FMT" "SLICE_FMT"\n", 
                    SLICE_ARG(r.state.raw), SLICE_ARG(r.read.raw), SLICE_ARG(r.write.raw),
                    SLICE_ARG(r.dir.raw), SLICE_ARG(r.next.raw)
                );
            } break;
            default: fprintf(stderr, "unhandled\n"); exit(1);
        }
    }

    return 0;
}