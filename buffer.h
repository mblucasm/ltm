#ifndef LTM_BUFFER_H
#define LTM_BUFFER_H

#include <stddef.h>

typedef struct {
    char *buf;
    size_t cap;
} Buf;

void buf_delete(Buf *buf);
void buf_write_char(Buf *buf, size_t idx, char c);
void buf_print(Buf buf);
void buf_print_inverted(Buf buf);

#endif // LTM_BUFFER_H