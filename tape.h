#ifndef LTM_TAPE_H
#define LTM_TAPE_H

#include "buffer.h"
#include <stddef.h>

typedef enum {DIRECTION_LEFT = -1, DIRECTION_NONE, DIRECTION_RIGHT} Dir;
typedef enum {SIDE_LEFT = -1, SIDE_RIGHT} Side;

typedef struct {
    Side side;
    Buf left;
    Buf right;
    size_t head;
} Tape;

void tape_delete(Tape *t);
char tape_read_char(Tape t);
void tape_write_char(Tape *t, char c);
void tape_move(Tape *t, Dir dir);
void tape_print(Tape t);

#endif // LTM_TAPE_H