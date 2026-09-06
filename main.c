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

// #define DEBUG
#define unreachable   (_unreachable(__LINE__))
#define unhandled     (_unhandled(__LINE__))
#define unimplemented (_unimplemented(__LINE__))

#define TM_WORD  "tm"
#define LTM_WORD "ltm"
#define IF_WORD  "if"
#define ELSE_WORD "else"

Buf tbuf = {0};

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

#define STB_DS_IMPLEMENTATION
#include "stb_ds.h"

typedef struct {
    char *key;
    char value;
} SH;

SH *kwords = NULL;
SH *idens = NULL;

bool is_keyword(const char *s) {
    return (shgetp_null(kwords, s) != NULL);
}

typedef struct {
    const char *fp;
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
    Slice slice;
    Loc loc;
} Tok;

typedef struct {
    Tok state;
    Tok read;
    Tok write;
    Tok dir;
    Tok next;
} Rule;

Lex lex_create(const char *fp, const char *data) {
    return (Lex) {
        .fp = fp,
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
        t.loc.row, t.loc.col, SLICE_ARG(t.slice),
        tok_type_to_str(t.type), t.slice.len
    );
}

Tok tok_create(TokType type, Slice raw, Loc loc) {
    return (Tok) {
        .type = type,
        .slice = raw,
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
    fprintf(stderr, "%s:%lld:%lld: "SLICE_FMT" ", t.loc.fp, t.loc.row, t.loc.col, SLICE_ARG(t.slice));
    vfprintf(stderr, msg, va);
    va_end(va);
    exit(1);
}

int wholeline(int c) {
    return c != '\0' && c != '\n' && c != '\r';
}

void rule_print(Rule r) {
    printf(
        SLICE_FMT" "SLICE_FMT" "SLICE_FMT" "SLICE_FMT" "SLICE_FMT"\n",
        SLICE_ARG(r.state.slice), SLICE_ARG(r.read.slice), SLICE_ARG(r.write.slice),
        SLICE_ARG(r.dir.slice), SLICE_ARG(r.next.slice)
    );
}

Loc lex_location(Lex *l) {
    return (Loc) {
        .fp = l->fp,
        .row = l->row,
        .col = l->current - l->row_start + 1
    };
}

#define PEEK_LOCATION ((Loc){.fp = l->fp, .row = row, .col = start - row_start + 1})

void lex_skip(Lex *l, int (*pred)(int)) {
    while(pred(*l->current)) {
        if(*l->current == '\n') {
            ++l->row;
            l->row_start = l->current + 1;
        }
        ++l->current;
    }
}

const char *advance_while(const char *start, int (*pred)(int)) {
    while(pred(*start)) ++start;
    return start;
}

int iscomment(int c) {
    return c != '\0' && c != '\n';
}

Tok lex_peek(Lex *l) {

    redo: {
        lex_skip(l, isspace);
        if(*l->current == '\0') return tok_create(TT_EOF, (Slice){0}, lex_location(l));
        if(l->current[0] == '-' && l->current[1] == '-') {
            lex_skip(l, iscomment);
            goto redo;
        }
    }

    size_t row = l->row;
    const char *start = l->current;
    const char *row_start = l->row_start;

    if(isalpha(*start)) {
        Slice raw = tok_parse(start, isalnum);
        slice_to_buf(raw, &tbuf);
        return tok_create(is_keyword(tbuf.buf) ? TT_KEYWORD : TT_IDEN, raw, PEEK_LOCATION);
    } else if(*start == '"') {
        Slice raw = stringParse(start);
        slice_to_buf(raw, &tbuf);
        Tok t = tok_create(TT_STRING, raw, PEEK_LOCATION);
        if(raw.data[raw.len] != '"') tok_report(t, "Unfinished string\n");
        ++t.slice.len;
        return t;
    }

    Slice raw = tok_parse(start, allchars);
    slice_to_buf(raw, &tbuf);

    if(slice_eq(raw, slice_create_raw("{"))) return tok_create(TT_OPENING, raw, PEEK_LOCATION);
    else if(slice_eq(raw, slice_create_raw("}"))) return tok_create(TT_CLOSING, raw, PEEK_LOCATION);
    else if(slice_eq(raw, slice_create_raw("*"))) return tok_create(TT_STAR, raw, PEEK_LOCATION);
    else if(raw.len == 3 && raw.data[0] == '\'' && raw.data[2] == '\'') return tok_create(TT_CHAR, raw, PEEK_LOCATION);
    else if(raw.len == 1 && (*raw.data == '<' || *raw.data == '-' || *raw.data == '>')) return tok_create(TT_DIR, raw, PEEK_LOCATION);
    else if(raw.len == 2 && raw.data[0] == '=' && raw.data[1] == '>') return tok_create(TT_ARROW, raw, PEEK_LOCATION);

    return tok_create(TT_UNKNOWN, raw, PEEK_LOCATION);
}

Tok lex_next(Lex *l) {
    Tok t = lex_peek(l);
#ifdef DEBUG
    tok_print(t);
#endif
    l->current = t.slice.data + t.slice.len;
    l->row = t.loc.row;
    l->row_start = t.slice.data - t.loc.col + 1;
    return t;
}

typedef enum {IT_NOP, IT_DECL_TM, IT_PUSH_RULE, IT_DECL_LTM, IT_FEED, IT_QCALL, IT_CALL, IT_IF, IT_ELSE, IT_COUNT} InsType;

typedef struct {
    Tok read;
    size_t jidx; // idx to jump to
} IfValue;

typedef union {
    Tok iden;
    Rule rule;
    IfValue iff;
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

_STATIC_ASSERT(IT_COUNT == 9);
const char *instype_to_str(InsType type) {
    switch(type) {
        case IT_NOP: return "IT_NOP";
        case IT_DECL_TM: return "IT_DECL_TM";
        case IT_PUSH_RULE: return "IT_PUSH_RULE";
        case IT_DECL_LTM: return "IT_DECL_LTM";
        case IT_FEED: return "IT_FEED";
        case IT_CALL: return "IT_CALL";
        case IT_IF: return "IT_IF";
        case IT_ELSE: return "IT_ELSE";
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

    Program ifstack = {0};

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
                        if(!slice_eq(t.slice, slice_create_raw(TM_WORD)) && !slice_eq(t.slice, slice_create_raw(LTM_WORD))) tok_report(t, "Invalid keyword for this context\n");
                        Tok iden = lex_expect(&l, TT_IDEN);
                        lex_expect(&l, TT_OPENING);
                        Ins i = {.type = slice_eq(t.slice, slice_create_raw(TM_WORD)) ? IT_DECL_TM : IT_DECL_LTM, .as.iden = iden};
                        da_append(p, i);
                        state = slice_eq(t.slice, slice_create_raw(TM_WORD)) ? STATE_DECL_TM : STATE_DECL_LTM;
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
                        rule_print(i.as.rule);
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

                    default: tok_report(t, "Invalid token. Expected tokens are: identifier or if statements or }\n"); break;

                    case TT_CLOSING: {
                        if(ifstack.len == 0) state = STATE_REGULAR;
                        else {
                            Ins *i = da_last(ifstack);
                            p.data[i->as.iff.jidx].as.iff.jidx = p.len + (size_t)slice_eq(lex_peek(&l).slice, slice_create_raw(ELSE_WORD));
                            --ifstack.len;
                        }
                    } break;

                    case TT_IDEN: {
                        Ins i = {.type = IT_CALL, .as.iden = t};
                        da_append(p, i);
                    } break;

                    case TT_KEYWORD: {
                        if(slice_eq(t.slice, slice_create_raw(IF_WORD))) {
                            Tok read = lex_expect(&l, TT_CHAR);
                            lex_expect(&l, TT_OPENING);
                            Ins i = {.type = IT_IF, .as.iff = { .read = read }};
                            da_append(p, i);
                            Ins ifi = { .as.iff = { .jidx = p.len - 1 }};
                            da_append(ifstack, ifi);
                        } else if(slice_eq(t.slice, slice_create_raw(ELSE_WORD))) {
                            lex_expect(&l, TT_OPENING);
                            Ins i = {.type = IT_ELSE};
                            da_append(p, i);
                            Ins elsei = { .as.iff = { .jidx = p.len - 1 }};
                            da_append(ifstack, elsei);
                        } else tok_report(t, "Invalid token. if, else are the only valid keywords inside blocks\n");
                    } break;
                }
            } break;
        }
    }

    da_del(ifstack);
    return p;
}

typedef struct {
    Rule *data;
    size_t len;
    size_t cap;
} Rules;

typedef struct {
    Tok iden;
    Rules rules; // TODO: stb_ds.h
} Tm;

typedef struct {
    char *key;
    Tm value;
} Sh_tm;

typedef struct {
    Tok iden;
    SH *idens;
} Ltm;

typedef struct {
    char *key;
    Ltm value;
} Sh_ltm;

Sh_tm *tms  = NULL;
Sh_ltm *ltms = NULL;

Rule *tm_match(Tm *tm, Slice state, char c) {
    for(size_t i = 0; i < tm->rules.len; ++i) {
        Rule *curr = tm->rules.data + i;
        if(slice_eq(curr->state.slice, state) && ((curr->read.type == TT_STAR || curr->read.slice.data[1] == c))) return curr;
    } return NULL;
}

void tm_run(Tm *tm, Tape *tape) {

    if(tm->rules.len == 0) return;

    Slice state = tm->rules.data[0].state.slice;
    char c = tape_read_char(*tape);

    Rule *rule;
    while((rule = tm_match(tm, state, c)) != NULL) {
        tape_write_char(tape, rule->write.type == TT_STAR ? c : rule->write.slice.data[1]);
        tape_move(tape, dir_from_char(rule->dir.slice.data[0]));
        state = rule->next.slice;
        c = tape_read_char(*tape);
    }

    printf("Tape after call:\n");
    tape_print(*tape);
}

void ltm_run(Ltm *ltm, Tape *tape) {
    size_t len = shlenu(ltm->idens);
    for(size_t i = 0; i < len; ++i) {
        void *p = NULL;
        if((p = shgetp_null(tms, ltm->idens[i].key)) != NULL) tm_run(&((Sh_tm*)p)->value, tape);
        else if((p = shgetp_null(ltms, ltm->idens[i].key)) != NULL) ltm_run(&((Sh_ltm*)p)->value, tape);
        else unreachable;
    }
}

bool rule_beg_eq(Rule a, Rule b) {
    return slice_eq(a.state.slice, b.state.slice)
        && slice_eq(a.read.slice, b.read.slice)
    ;
}

bool rule_eq(Rule a, Rule b) {
    return rule_beg_eq(a, b)
        && slice_eq(a.write.slice, b.write.slice)
        && slice_eq(a.dir.slice, b.dir.slice)
        && slice_eq(a.next.slice, b.next.slice)
    ;
}

#define shlast(t) (t[shlenu(t) - 1])

#define print_sh(t) \
    printf("--------------------\n"); \
    printf("Printing sh: "#t"\n");    \
    printf("shlen: %lld\n", shlenu(t)); \
    for(size_t i = 0; i < shlenu(t); ++i) { \
        printf("key: %s\n", t[i].key); \
    } \
    printf("--------------------\n");

void run(Program p) {

    Tape tape = {0};

    for(size_t k = 0; k < p.len; ++k) {
        Ins ins = p.data[k];
        switch(ins.type) {

            case IT_NOP: break;
            default: _unreachable(__LINE__); break;

            case IT_ELSE: k = ins.as.iff.jidx - 1; break; // Jump to the previous idx. Then for loop adds 1.

            case IT_IF: {
                if(tape_read_char(tape) != ins.as.iff.read.slice.data[1]) k = ins.as.iff.jidx - 1; // Jump to the previous idx. Then for loop adds 1.
            } break;

            case IT_DECL_TM: {
                void *p = NULL;
                Tok t = ins.as.iden;
                slice_to_buf(t.slice, &tbuf);
                if((p = shgetp_null(tms, tbuf.buf)) != NULL)  tok_report(t, "Redefinition of tm. Previous definition at %s:%lld:%lld\n", ((Tm*)p)->iden.loc.fp, ((Tm*)p)->iden.loc.row, ((Tm*)p)->iden.loc.col);
                if((p = shgetp_null(ltms, tbuf.buf)) != NULL) tok_report(t, "Redefinition of ltm. Previous definition at %s:%lld:%lld\n", ((Ltm*)p)->iden.loc.fp, ((Ltm*)p)->iden.loc.row, ((Ltm*)p)->iden.loc.col);
                shput(idens, tbuf.buf, '\0');
                shput(tms, shlast(idens).key, (Tm){ .iden = t });
            } break;

            case IT_DECL_LTM: {
                void *p = NULL;
                Tok t = ins.as.iden;
                slice_to_buf(t.slice, &tbuf);
                if((p = shgetp_null(tms, tbuf.buf)) != NULL)  tok_report(t, "Redefinition of tm. Previous definition at %s:%lld:%lld\n", ((Tm*)p)->iden.loc.fp, ((Tm*)p)->iden.loc.row, ((Tm*)p)->iden.loc.col);
                if((p = shgetp_null(ltms, tbuf.buf)) != NULL) tok_report(t, "Redefinition of ltm. Previous definition at %s:%lld:%lld\n", ((Ltm*)p)->iden.loc.fp, ((Ltm*)p)->iden.loc.row, ((Ltm*)p)->iden.loc.col);
                shput(idens, tbuf.buf, '\0');
                Ltm ltm = {.iden = t, .idens = NULL};
                shput(ltms, shlast(idens).key, ltm);
            } break;

            case IT_PUSH_RULE: {
                Tm *tm = &shlast(tms).value;
                for(size_t i = 0; i < tm->rules.len; ++i) {
                    if(rule_beg_eq(tm->rules.data[i], ins.as.rule)) {
                        Tok prev = tm->rules.data[i].state;
                        tok_report(ins.as.iden, "Redefinition of rule. Previous definition at %s:%lld:%lld\n", prev.loc.fp, prev.loc.row, prev.loc.col);
                    }
                }
                da_append(tm->rules, ins.as.rule);
            } break;

            case IT_FEED: {
                tape_delete(&tape);
                for(size_t i = 1; i < ins.as.iden.slice.len - 1; ++i) {
                    tape_write_char(&tape, ins.as.iden.slice.data[i]);
                    tape_move(&tape, DIR_RIGHT);
                }
                tape.head = 0;
                printf("Printing tape before:\n");
                tape_print(tape);
            } break;

            case IT_CALL: {
                void *p = NULL;
                slice_to_buf(ins.as.iden.slice, &tbuf);

                if((p = shgetp_null(tms, tbuf.buf)) != NULL) {
                    tm_run(&((Sh_tm*)p)->value, &tape);
                    break;
                }

                if((p = shgetp_null(ltms, tbuf.buf)) != NULL) {
                    ltm_run(&((Sh_ltm*)p)->value, &tape);
                    break;
                }

                tok_report(ins.as.iden, "Undefined reference to tm / ltm\n");
            } break;

            case IT_QCALL: {
                Tok t = ins.as.iden;
                slice_to_buf(t.slice, &tbuf);
                if(shgetp_null(idens, tbuf.buf) == NULL)  tok_report(t, "Queuing a call to a undefined tm / ltm");
                Ltm *ltm = &shlast(ltms).value;
                shput(ltm->idens, shgets(idens, tbuf.buf).key, '\0');
            } break;
        }
    }
}

int main(void) {

    sh_new_arena(idens);

    shput(kwords, TM_WORD, '\0');
    shput(kwords, LTM_WORD, '\0');
    shput(kwords, IF_WORD, '\0');
    shput(kwords, ELSE_WORD, '\0');

    Program p = lex_file("z.ltm");

    printf("======================\n");
    for(size_t i = 0; i < p.len; ++i) {
        printf("%lld>> %s(%d): ", i, instype_to_str(p.data[i].type), p.data[i].type);
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
            case IT_IF: printf("Read: ."SLICE_FMT". Jump to: %lld\n", SLICE_ARG(p.data[i].as.iff.read.slice), p.data[i].as.iff.jidx); break;
            case IT_ELSE: printf("Jump to: %lld\n", p.data[i].as.iff.jidx); break;
            default: fprintf(stderr, "_unhandled\n"); exit(1);
        }
    }
    printf("======================\n");

    run(p);

    return 0;
}