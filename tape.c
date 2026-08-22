#include "tape.h"

#include <stdio.h>
#include <assert.h>
#include <stdbool.h>

void tape_delete(Tape *t) {
    buf_delete(&t->left);
    buf_delete(&t->right);
    *t = (Tape) {0};
}

char tape_read_char(Tape t) {
    Buf side = (t.side == SIDE_LEFT) ? t.left : t.right;
    return (t.head < side.cap) ? side.buf[t.head] : '\0';
}

void tape_write_char(Tape *t, char c) {
    if(t->side == SIDE_LEFT) buf_write_char(&t->left, t->head, c);
    else buf_write_char(&t->right, t->head, c);
}

void tape_move(Tape *t, Dir dir) {
    
    switch(dir) {

        case DIRECTION_NONE: break;
        
        case DIRECTION_LEFT:
        case DIRECTION_RIGHT:
            if((t->side == SIDE_LEFT && dir == DIRECTION_LEFT) || (t->side == SIDE_RIGHT && dir == DIRECTION_RIGHT)) t->head++;
            else if(t->head != 0) t->head--;
            else t->side = t->side == SIDE_LEFT ? SIDE_RIGHT : SIDE_LEFT;
        break;

        default: assert(false);
    }
}

void tape_print(Tape t) {
    
    printf("=====================\n");
    printf("Side: %s\n", t.side == SIDE_LEFT ? "LEFT" : "RIGHT");
    printf("Head index: %zu\n", t.head);
    printf("Head: ");

    size_t lpadding = 0;
    size_t rpadding = 0;
    size_t jumps = 0;
    if(t.side == SIDE_LEFT) {
        if(t.left.cap <= t.head) lpadding = t.head - t.left.cap + 1;
        else jumps = t.left.cap - t.head - 1;
    } else {
        jumps = t.left.cap + t.head;
        if(t.right.cap <= t.head) rpadding = t.head - t.right.cap + 1;
    }

    for(size_t i = 0; i < jumps; ++i) printf("  ");
    printf("v\nTape: ");
    
    for(size_t i = 0; i < lpadding; ++i) printf("%c ", 174);
    buf_print_inverted(t.left);
    buf_print(t.right);
    for(size_t i = 0; i < rpadding; ++i) printf("%c ", 175);
    
    
    printf("\n      ");
    for(size_t i = 0; i < t.left.cap + lpadding; ++i) printf("%zu ", (t.left.cap + lpadding - i - 1) % 10);
    for(size_t i = 0; i < t.right.cap + rpadding; ++i) printf("%zu ", i % 10);
    
    printf("\n=====================\n");
}