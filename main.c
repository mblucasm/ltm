#include "tape.h"
#include "slice.h"

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
#define PRINT_WORD "print"

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
    Tok tok;
    size_t idx;
} TokIdx;

typedef struct {
    char *key;
    TokIdx value;
} SH;

SH *kwords = NULL;

bool is_keyword(const char *s) {
    return (shgetp_null(kwords, s) != NULL);
}

typedef struct {
    const char *fp;
    const char *current;
    const char *row_start;
    size_t row;
} Lex;

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
        case TT_UNKNOWN: return "Unknown";
        case TT_EOF: return "End of file";
        case TT_OPENING: return "Opening brace";
        case TT_CLOSING: return "Closing brace";
        case TT_CHAR: return "Char";
        case TT_IDEN: return "Identifier";
        case TT_DIR: return "Dir";
        case TT_KEYWORD: return "Keyword";
        case TT_STRING: return "String";
        case TT_ARROW: return "Arrow";
        case TT_STAR: return "Star";
        default: exit(1);
    }
}

void tok_print(Tok t) {
    printf(
        "z.ltm:%lld:%lld %s %lld "SLICE_FMT"\n",
        t.loc.row, t.loc.col,
        tok_type_to_str(t.type), t.slice.len,
        SLICE_ARG(t.slice)
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

typedef enum {IT_NOP, IT_DECL_TM, IT_PUSH_RULE, IT_DECL_LTM, IT_RETURN, IT_FEED, IT_MOVE, IT_WRITE, IT_CALL, IT_IF, IT_ELSE, IT_PRINT, IT_COUNT} InsType;

typedef union {
    Tok tok;
    Rule rule;
    TokIdx iff;
    TokIdx ltm;
    Dir dir;
    size_t idx;
} InsValue;

typedef struct {
    InsType type;
    InsValue as;
} Ins;

_STATIC_ASSERT(IT_COUNT == 12);
const char *instype_to_str(InsType type) {
    switch(type) {
        case IT_NOP: return "No op";
        case IT_DECL_TM: return "Declare tm";
        case IT_PUSH_RULE: return "Push rule";
        case IT_DECL_LTM: return "Declare ltm";
        case IT_FEED: return "Feed";
        case IT_CALL: return "Call";
        case IT_IF: return "If";
        case IT_ELSE: return "Else";
        case IT_PRINT: return "Print";
        case IT_RETURN: return "Return";
        case IT_WRITE: return "Write";
        case IT_MOVE: return "Move";
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

char dir_to_char(Dir d) {
    switch(d) {
        case DIR_LEFT:  return '<';
        case DIR_NONE:  return '-';
        case DIR_RIGHT: return '>';
        default: unreachable; exit(1);
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

Ins *gen_ir(const char *fp) {

    Slice s = slice_from_file(fp);
    Lex l = lex_create(fp, s.data);

    Tok t = {0};
    State state = STATE_REGULAR;

    Ins *ir = NULL;
    Ins *stack = NULL;

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
                        if(!slice_eq(t.slice, slice_create_raw(TM_WORD)) && !slice_eq(t.slice, slice_create_raw(LTM_WORD))) tok_report(t, "Invalid keyword for this context. Valid keywords are: %s and %s\n", TM_WORD, LTM_WORD);
                        Tok tok = lex_expect(&l, TT_IDEN);
                        lex_expect(&l, TT_OPENING);
                        
                        Ins i = { .type = slice_eq(t.slice, slice_create_raw(TM_WORD)) ? IT_DECL_TM : IT_DECL_LTM };
                        if(i.type == IT_DECL_TM) i.as.tok = tok;
                        else if(i.type == IT_DECL_LTM) {
                            i.as.ltm.tok = tok;
                            Ins ret = { .type = IT_RETURN, .as.idx = arrlenu(ir) };
                            arrput(stack, ret);
                        } else unreachable;

                        arrput(ir, i);
                        state = slice_eq(t.slice, slice_create_raw(TM_WORD)) ? STATE_DECL_TM : STATE_DECL_LTM;
                    } break;

                    case TT_STRING: {
                        lex_expect(&l, TT_ARROW);
                        lex_expect(&l, TT_OPENING);
                        Ins i = {.type = IT_FEED, .as.tok = t};
                        arrput(ir, i);
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
                        arrput(ir, i);
                    } break;
                }
            } break;

            case STATE_DECL_LTM: {
                switch(t.type) {

                    default: tok_report(t, "Invalid token. Expected tokens are: identifier or }\n"); break;

                    case TT_CLOSING: {
                        Ins i = { .type = IT_RETURN };
                        arrput(ir, i);
                        Ins ret = arrpop(stack);
                        assert(ret.type == IT_RETURN);
                        assert(ir[ret.as.idx].type == IT_DECL_LTM);
                        ir[ret.as.idx].as.ltm.idx = arrlen(ir);
                        state = STATE_REGULAR;
                    } break;

                    case TT_IDEN: {
                        Ins i = {.type = IT_CALL, .as.tok = t};
                        arrput(ir, i);
                    } break;
                }
            } break;

            case STATE_DECL_BLOCK: {
                switch(t.type) {

                    default: tok_report(t, "Invalid token. Expected tokens are: identifier or if statements or strings or < or > or }\n"); break;

                    case TT_STRING: {
                        Ins i = {.type = IT_WRITE, .as.tok = t};
                        arrput(ir, i);
                    } break;

                    case TT_DIR: {
                        Ins i = {.type = IT_MOVE, .as.dir = dir_from_char(t.slice.data[0])};
                        arrput(ir, i);
                    } break;

                    case TT_CLOSING: {
                        if(arrlenu(stack) == 0) state = STATE_REGULAR;
                        else {
                            Ins ins = arrpop(stack); 
                            assert(ins.type == IT_IF || ins.type == IT_ELSE);
                            TokIdx iff = ins.as.iff;
                            ir[iff.idx].as.iff.idx = arrlenu(ir) + (size_t)slice_eq(lex_peek(&l).slice, slice_create_raw(ELSE_WORD));
                        }
                    } break;

                    case TT_IDEN: {
                        Ins i = {.type = IT_CALL, .as.tok = t};
                        arrput(ir, i);
                    } break;

                    case TT_KEYWORD: {
                        if(slice_eq(t.slice, slice_create_raw(IF_WORD))) {
                            Tok read = lex_expect(&l, TT_CHAR);
                            lex_expect(&l, TT_OPENING);
                            Ins i = {.type = IT_IF, .as.iff = { .tok = read }};
                            arrput(ir, i);
                            Ins ifi = { .type = IT_IF, .as.iff = { .idx = arrlenu(ir) - 1 }};
                            arrput(stack, ifi);
                        } else if(slice_eq(t.slice, slice_create_raw(ELSE_WORD))) {
                            lex_expect(&l, TT_OPENING);
                            Ins i = {.type = IT_ELSE};
                            arrput(ir, i);
                            Ins elsei = { .type = IT_ELSE, .as.iff = { .idx = arrlenu(ir) - 1 }};
                            arrput(stack, elsei);
                        } else if(slice_eq(t.slice, slice_create_raw(PRINT_WORD))) {
                            Ins i = {.type = IT_PRINT};
                            arrput(ir, i);
                        } else tok_report(t, "Invalid token. if, else and print are the only valid keywords inside blocks\n");
                    } break;
                }
            } break;
        }
    }

    arrfree(stack);
    return ir;
}

// Rule *tm_match(Tm *tm, Slice state, char c) {
//     for(size_t i = 0; i < tm->rules.len; ++i) {
//         Rule *curr = tm->rules.data + i;
//         if(slice_eq(curr->state.slice, state) && ((curr->read.type == TT_STAR || curr->read.slice.data[1] == c))) return curr;
//     } return NULL;
// }

// void tm_run(Tm *tm, Tape *tape) {

//     if(tm->rules.len == 0) return;

//     Slice state = tm->rules.data[0].state.slice;
//     char c = tape_read_char(*tape);

//     Rule *rule;
//     while((rule = tm_match(tm, state, c)) != NULL) {
//         if(rule->write.type != TT_STAR) tape_write_char(tape, rule->write.slice.data[1]);
//         tape_move(tape, dir_from_char(rule->dir.slice.data[0]));
//         state = rule->next.slice;
//         c = tape_read_char(*tape);
//     }
// }

// void ltm_run(Ltm *ltm, Tape *tape) {
//     size_t len = arrlenu(ltm->idens);
//     printf("Running ltm for %lld macs\n", len);
//     for(size_t i = 0; i < len; ++i) {
//         void *p = NULL;
//         if((p = shgetp_null(tms, ltm->idens[i])) != NULL) tm_run(&((Sh_tm*)p)->value, tape);
//         else if((p = shgetp_null(ltms, ltm->idens[i])) != NULL) ltm_run(&((Sh_ltm*)p)->value, tape);
//         else unreachable;
//     }
// }

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

void todo(const char *msg, ...) {
    va_list va;
    va_start(va, msg);
    fprintf(stderr, "TODO: ");
    vfprintf(stderr, msg, va);
    va_end(va);
}

void run_ir(Ins *ir) {

    Tape tape = {0};
    size_t len = arrlenu(ir);

    SH *idens = NULL;
    sh_new_arena(idens);

    for(size_t k = 0; k < len; ++k) {
        Ins ins = ir[k];
        switch(ins.type) {

            case IT_NOP: break;
            default: printf("%d\n", ins.type); unreachable; break;

            case IT_ELSE: k = ins.as.iff.idx - 1; break; // Jump to the previous idx. Then for loop adds 1.

            case IT_IF: {
                if(tape_read_char(tape) != ins.as.iff.tok.slice.data[1]) k = ins.as.iff.idx - 1; // Jump to the previous idx. Then for loop adds 1.
            } break;

            case IT_DECL_TM: {
                unimplemented;
                // union {Sh_tm* tm; Sh_ltm *ltm;} p = {0};
                // Tok t = ins.as.tok;
                // slice_to_buf(t.slice, &tbuf);
                // if((p.tm = shgetp_null(tms, tbuf.buf)) != NULL)  tok_report(t, "Redefinition of tm. Previous definition at %s:%lld:%lld\n", p.tm->value.tok.loc.fp, p.tm->value.tok.loc.row, p.tm->value.tok.loc.col);
                // if((p.ltm = shgetp_null(ltms, tbuf.buf)) != NULL) tok_report(t, "Redefinition of ltm. Previous definition at %s:%lld:%lld\n", p.ltm->value.tok.loc.fp, p.ltm->value.tok.loc.row, p.ltm->value.tok.loc.col);
                // shput(idens, tbuf.buf, '\0');
                // shput(tms, shlast(idens).key, (Tm){ .tok = t });
            } break;

            case IT_RETURN: {
                unimplemented;
            } break;

            case IT_DECL_LTM: {
                // Upon reaching a LTM declaration skip all instructions until return.
                // Should add some way to check for undefined calls inside the ltm body.
                Tok t = ins.as.ltm.tok;
                slice_to_buf(t.slice, &tbuf);
                int i;
                todo("Check if this iden refers to a tm\n");
                if((i = shgeti(idens, tbuf.buf)) != -1) tok_report(t, "Redefinition of ltm. Previous definition at %s:%lld:%lld\n", idens[i].value.tok.loc.fp, idens[i].value.tok.loc.row, idens[i].value.tok.loc.col);
                TokIdx ti = { .tok = t, .idx = k };
                shput(idens, tbuf.buf, ti);
                k = ins.as.ltm.idx - 1; // Jump to the previous idx. Then for loop adds 1.
            } break;

            case IT_PUSH_RULE: {
                unimplemented;
                // Tm *tm = &shlast(tms).value;
                // for(size_t i = 0; i < tm->rules.len; ++i) {
                //     if(rule_beg_eq(tm->rules.data[i], ins.as.rule)) {
                //         Tok prev = tm->rules.data[i].state;
                //         tok_report(ins.as.tok, "Redefinition of rule. Previous definition at %s:%lld:%lld\n", prev.loc.fp, prev.loc.row, prev.loc.col);
                //     }
                // }
                // da_append(tm->rules, ins.as.rule);
            } break;

            case IT_FEED: {
                tape_delete(&tape);
                for(size_t i = 1; i < ins.as.tok.slice.len - 1; ++i) {
                    tape_write_char(&tape, ins.as.tok.slice.data[i]);
                    tape_move(&tape, DIR_RIGHT);
                }
                tape.head = 0;
            } break;

            case IT_CALL: {
                unimplemented;
                // void *p = NULL;
                // slice_to_buf(ins.as.tok.slice, &tbuf);

                // if((p = shgetp_null(tms, tbuf.buf)) != NULL) {
                //     tm_run(&((Sh_tm*)p)->value, &tape);
                //     break;
                // }

                // if((p = shgetp_null(ltms, tbuf.buf)) != NULL) {
                //     ltm_run(&((Sh_ltm*)p)->value, &tape);
                //     break;
                // }

                // tok_report(ins.as.tok, "Undefined reference to tm / ltm\n");
            } break;

            case IT_PRINT: tape_print(tape); break;

            case IT_MOVE: tape_move(&tape, ins.as.dir); break;

            case IT_WRITE: {
                for(size_t i = 1; i < ins.as.tok.slice.len - 1; ++i) {
                    tape_write_char(&tape, ins.as.tok.slice.data[i]);
                    tape_move(&tape, DIR_RIGHT);
                }
            } break;
        }
    }

    shfree(idens);
}

int main(void) {

    shput(kwords, TM_WORD, (TokIdx){0});
    shput(kwords, LTM_WORD, (TokIdx){0});
    shput(kwords, IF_WORD, (TokIdx){0});
    shput(kwords, ELSE_WORD, (TokIdx){0});
    shput(kwords, PRINT_WORD, (TokIdx){0});

    Ins *ir = gen_ir("z.ltm");

    printf("======================\n");
    printf("Printing IR\n");
    printf("======================\n");
    for(size_t k = 0; k < arrlenu(ir); ++k) {
        Ins i = ir[k];
        printf("%lld>> %s ", k, instype_to_str(i.type));
        switch(i.type) {
            case IT_DECL_LTM: printf(SLICE_FMT" and jump to %lld\n", SLICE_ARG(i.as.ltm.tok.slice), i.as.ltm.idx); break;
            case IT_FEED: printf(SLICE_FMT"\n", SLICE_ARG(i.as.tok.slice)); break;
            case IT_CALL: printf(SLICE_FMT"\n", SLICE_ARG(i.as.tok.slice)); break;
            case IT_DECL_TM: printf(SLICE_FMT"\n", SLICE_ARG(i.as.tok.slice)); break;
            case IT_PUSH_RULE: {
                Rule r = i.as.rule;
                rule_print(r);
            } break;
            case IT_IF: printf("not "SLICE_FMT" jump to %lld\n", SLICE_ARG(i.as.iff.tok.slice), i.as.iff.idx); break;
            case IT_ELSE: printf("jump to %lld\n", i.as.iff.idx); break;
            case IT_PRINT: printf("\n"); break;
            case IT_WRITE: printf(SLICE_FMT"\n", SLICE_ARG(i.as.tok.slice)); break;
            case IT_MOVE: printf("%c\n", dir_to_char(i.as.dir)); break;
            case IT_RETURN: printf("\n"); break;
            default: unhandled;
        }
    }
    printf("======================\n");

    run_ir(ir);

    return 0;
}