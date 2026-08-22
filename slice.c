#include "slice.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Slice slice_create_raw(const char *data) {
    return (Slice) {
        .data  = data,
        .len = strlen(data),
    };
}

Slice slice_create(const char *data, size_t len) {
    return (Slice) {
        .data  = data,
        .len = len,
    };
}

Slice slice_slice(Slice *s, char delim) {
    
    size_t i = 0;
    while(i < s->len && s->data[i] != delim) ++i;

    Slice res = slice_create(s->data, i);

    s->data  += i + (i < s->len);
    s->len -= i + (i < s->len);

    return res;
}

Slice slice_from_file(const char *fp) {

    FILE *f = fopen(fp, "rb");
    fseek(f, 0, SEEK_END);
    size_t len = ftell(f);
    rewind(f);
    
    char *data = malloc(sizeof(char) * (len + 1));
    fread(data, sizeof(char), len, f);
    data[len] = '\0';

    return slice_create(data, len);
}

bool slice_eq(Slice a, Slice b) {
    return (a.len == b.len) && (strncmp(a.data, b.data, a.len) == 0);
}