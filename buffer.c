#include "buffer.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

void buf_delete(Buf *buf) {
    assert(buf != NULL);
    if(buf->buf != NULL) free(buf->buf);
    buf->cap = 0;
}

void buf_write_char(Buf *buf, size_t idx, char c) {
    
    assert(buf != NULL);

    if(idx < buf->cap) {
        assert(buf->buf != NULL);
        buf->buf[idx] = c;
        return;
    }

    buf->buf = realloc(buf->buf, (idx + 1) * sizeof(char));
    assert(buf->buf != NULL);

    memset(buf->buf + buf->cap, 0, (idx - buf->cap + 1) * sizeof(char));
    buf->cap = idx + 1;
    buf->buf[idx] = c;
}

void buf_print(Buf buf) {
    for(size_t i = 0; i < buf.cap; ++i) {
        if(buf.buf[i] == '\0') printf("%c ", 176);
        else printf("%c ", buf.buf[i]);
    }
}

void buf_print_inverted(Buf buf) {
    for(size_t i = 0; i < buf.cap; ++i) {
        if(buf.buf[buf.cap - i - 1] == '\0') printf("%c ", 176);
        else printf("%c ", buf.buf[buf.cap - i - 1]);
    }
}