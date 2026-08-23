// TODO: Pivot to stb_ds.h

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
#define unreachable   (_unreachable(__LINE__))
#define unhandled     (_unhandled(__LINE__))
#define unimplemented (_unimplemented(__LINE__))

void _unreachable(size_t line) {
    fprintf(stderr, "%s:%lld: unreachable\n", __FILE__, line);
    exit(1);
}

void _unhandled(size_t line) {
    fprintf(stderr, "%s:%lld: unhandled\n", __FILE__, line);
    exit(1);
}

void _unimplemented(size_t line) {
    fprintf(stderr, "%s:%lld: unimplemented\n", __FILE__, line);
    exit(1);
}

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
    TT_STAR,
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

_STATIC_ASSERT(TT_COUNT == 11);
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
        case TT_STAR: return "TT_STAR";
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
    else if(slice_eq(raw, slice_create_raw("*"))) return tok_create(TT_STAR, raw, LOCATION);
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

typedef enum {IT_NOP, IT_DECL_TM, IT_PUSH_RULE, IT_DECL_LTM, IT_FEED, IT_QCALL, IT_CALL, IT_COUNT} InsType;

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

_STATIC_ASSERT(IT_COUNT == 7);
const char *instype_to_str(InsType type) {
    switch(type) {
        case IT_NOP: return "IT_NOP";
        case IT_DECL_TM: return "IT_DECL_TM";
        case IT_PUSH_RULE: return "IT_PUSH_RULE";
        case IT_DECL_LTM: return "IT_DECL_LTM";
        case IT_FEED: return "IT_FEED";
        case IT_CALL: return "IT_CALL";
        case IT_QCALL: return "IT_QCALL"; // Queue call
        default: exit(1);
    }
}

typedef enum {STATE_REGULAR, STATE_DECL_TM, STATE_DECL_LTM, STATE_DECL_BLOCK, STATE_COUNT} State;

Dir dir_from_char(char c) {
    switch(c) {
        case '<': return DIR_LEFT;
        case '-': return DIR_NONE;
        case '>': return DIR_RIGHT;
        default: _unreachable(__LINE__); exit(1);
    }
}

Slice tm, ltm;

Tok lex_expect2(Lex *l, TokType type1, TokType type2) {
    Tok t = lex_next(l);
    if(t.type != type1 && t.type != type2) tok_report(t, "Unexpected token type %s. Expected was %s or %s", tok_type_to_str(t.type), tok_type_to_str(type1), tok_type_to_str(type2));
    return t;
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

        _STATIC_ASSERT(STATE_COUNT == 4);
        switch(state) {

            default: _unreachable(__LINE__); break;

            case STATE_REGULAR: {
                switch(t.type) {

                    case TT_EOF: break;
                    default: tok_report(t, "Invalid token. Expected tokens are: keywords or strings\n"); break;

                    case TT_KEYWORD: {
                        if(!slice_eq(t.raw, tm) && !slice_eq(t.raw, ltm)) _unhandled(__LINE__);
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
                        state = STATE_DECL_BLOCK;
                    } break;
                }
            } break;

            case STATE_DECL_TM: {
                switch(t.type) {

                    default: tok_report(t, "Invalid token. Expected tokens are: identifier or }\n"); break;

                    case TT_CLOSING: state = STATE_REGULAR; break;

                    case TT_IDEN: {
                        Tok read  = lex_expect2(&l, TT_CHAR, TT_STAR);
                        Tok write = lex_expect2(&l, TT_CHAR, TT_STAR);
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
                        Ins i = {.type = IT_QCALL, .as.iden = t};
                        da_append(p, i);
                    } break;
                }
            } break;

            case STATE_DECL_BLOCK: {
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

typedef struct {
    Rule *data;
    size_t len;
    size_t cap;
} Rules;

typedef struct {
    Tok iden;
    Rules rules;
} TM;

typedef struct {
    TM *data;
    size_t len;
    size_t cap;
} TMs;

Rule *tm_match(TM *tm, Slice state, char c) {
    for(size_t i = 0; i < tm->rules.len; ++i) {
        Rule *curr = tm->rules.data + i;
        if(slice_eq(curr->state.raw, state) && ((curr->read.type == TT_STAR || curr->read.raw.data[1] == c))) return curr;
    } return NULL;
}

typedef enum {MT_TM, MT_LTM} MacType;

typedef struct ltm LTM;

typedef union {
    TM *tm;
    LTM *ltm;
} MacValue;

typedef struct {
    MacType type;
    MacValue as;
} Mac;

typedef struct {
    Mac *data;
    size_t len;
    size_t cap;
} Macs;

struct ltm {
    Tok iden;
    Macs macs;
};

typedef struct {
    LTM *data;
    size_t len;
    size_t cap;
} LTMs;

void tm_run(TM *tm, Tape *tape) {

    Slice state = tm->rules.data[0].state.raw;
    char c = tape_read_char(*tape);

    Rule *rule;
    while((rule = tm_match(tm, state, c)) != NULL) {
        tape_write_char(tape, rule->write.type == TT_STAR ? c : rule->write.raw.data[1]);
        tape_move(tape, dir_from_char(rule->dir.raw.data[0]));
        state = rule->next.raw;
        c = tape_read_char(*tape);
    }

    printf("Tape after call:\n");
    tape_print(*tape);
}

void ltm_run(LTM *ltm, Tape *tape) {
    for(size_t i = 0; i < ltm->macs.len; ++i) {
        Mac mac = ltm->macs.data[i];
        switch(mac.type) {
            default: _unreachable(__LINE__); break;
            case MT_LTM: ltm_run(mac.as.ltm, tape); break;
            case MT_TM: tm_run(mac.as.tm, tape); break;
        }
    }
}

void rule_print(Rule r) {
    printf(
        SLICE_FMT" "SLICE_FMT" "SLICE_FMT" "SLICE_FMT" "SLICE_FMT"\n",
        SLICE_ARG(r.state.raw), SLICE_ARG(r.read.raw), SLICE_ARG(r.write.raw),
        SLICE_ARG(r.dir.raw), SLICE_ARG(r.next.raw)
    );
}

bool tm_exists(TMs tms, Tok tm) {
    for(size_t i = 0; i < tms.len; ++i) if(slice_eq(tms.data[i].iden.raw, tm.raw)) return true;
    return false;
}

bool ltm_exists(LTMs ltms, Tok ltm) {
    for(size_t i = 0; i < ltms.len; ++i) if(slice_eq(ltms.data[i].iden.raw, ltm.raw)) return true;
    return false;
}

TM *tm_find(TMs tms, Tok tm) {
    for(size_t i = 0; i < tms.len; ++i) if(slice_eq(tms.data[i].iden.raw, tm.raw)) return tms.data + i;
    return NULL;
}

LTM *ltm_find(LTMs ltms, Tok ltm) {
    for(size_t i = 0; i < ltms.len; ++i) if(slice_eq(ltms.data[i].iden.raw, ltm.raw)) return ltms.data + i;
    return NULL;
}

bool rule_beg_eq(Rule a, Rule b) {
    return slice_eq(a.state.raw, b.state.raw)
        && slice_eq(a.read.raw, b.read.raw)
    ;
}

bool rule_eq(Rule a, Rule b) {
    return rule_beg_eq(a, b)
        && slice_eq(a.write.raw, b.write.raw)
        && slice_eq(a.dir.raw, b.dir.raw)
        && slice_eq(a.next.raw, b.next.raw)
    ;
}

void run(Program p) {

    TMs tms   = {0};
    LTMs ltms = {0};
    Tape tape = {0};

    for(size_t k = 0; k < p.len; ++k) {
        Ins ins = p.data[k];
        switch(ins.type) {

            case IT_NOP: break;
            default: _unreachable(__LINE__); break;

            case IT_DECL_TM: {
                Tok t = ins.as.iden;
                {TM  *prev = tm_find(tms, t);   if(prev != NULL) tok_report(t, "Redefinition of tm. Previous definition at %s:%lld:%lld\n", prev->iden.loc.fp, prev->iden.loc.row, prev->iden.loc.col);}
                {LTM *prev = ltm_find(ltms, t); if(prev != NULL) tok_report(t, "Redefinition of ltm. Previous definition at %s:%lld:%lld\n", prev->iden.loc.fp, prev->iden.loc.row, prev->iden.loc.col);}
                TM tm = { .iden = t };
                da_append(tms, tm);
            } break;

            case IT_PUSH_RULE: {
                TM *tm = da_last(tms);
                for(size_t i = 0; i < tm->rules.len; ++i) {
                    if(rule_beg_eq(tm->rules.data[i], ins.as.rule)) {
                        Tok prev = tm->rules.data[i].state;
                        tok_report(ins.as.iden, "Redefinition of rule. Previous definition at %s:%lld:%lld\n", prev.loc.fp, prev.loc.row, prev.loc.col);
                    }
                }
                da_append(tm->rules, ins.as.rule);
            } break;

            case IT_DECL_LTM: {
                Tok t = ins.as.iden;
                {TM  *prev = tm_find(tms, t);   if(prev != NULL) tok_report(t, "Redefinition of tm. Previous definition at %s:%lld:%lld\n", prev->iden.loc.fp, prev->iden.loc.row, prev->iden.loc.col);}
                {LTM *prev = ltm_find(ltms, t); if(prev != NULL) tok_report(t, "Redefinition of ltm. Previous definition at %s:%lld:%lld\n", prev->iden.loc.fp, prev->iden.loc.row, prev->iden.loc.col);}
                LTM ltm = {.iden = t };
                da_append(ltms, ltm);
            } break;

            case IT_FEED: {
                tape_delete(&tape);
                for(size_t i = 1; i < ins.as.iden.raw.len - 1; ++i) {
                    tape_write_char(&tape, ins.as.iden.raw.data[i]);
                    tape_move(&tape, DIR_RIGHT);
                }
                tape.head = 0;
                printf("Printing tape before:\n");
                tape_print(tape);
            } break;

            case IT_CALL: {
                TM *tm = tm_find(tms, ins.as.iden);
                if(tm != NULL) {
                    tm_run(tm, &tape);
                    break;
                }

                LTM *ltm = ltm_find(ltms, ins.as.iden);
                if(ltm != NULL) {
                    ltm_run(ltm, &tape);
                    break;
                }

                tok_report(ins.as.iden, "Undefined reference to tm / ltm\n");
            } break;

            case IT_QCALL: {
                Mac mac = { .type = MT_TM, .as.tm = tm_find(tms, ins.as.iden) };
                if(mac.as.tm == NULL) {
                    mac = (Mac){ .type = MT_LTM, .as.ltm = ltm_find(ltms, ins.as.iden) };
                    if(mac.as.ltm == NULL) tok_report(ins.as.iden, "Undefined reference to tm / ltm");
                }
                LTM *ltm = da_last(ltms);
                da_append(ltm->macs, mac);
            } break;
        }
    }
}

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
                rule_print(r);
            } break;
            case IT_QCALL: tok_print(p.data[i].as.iden); break;
            default: fprintf(stderr, "_unhandled\n"); exit(1);
        }
    }
    printf("======================\n");

    run(p);

    return 0;
}