#ifndef LTM_SLICE_H
#define LTM_SLICE_H

#include <stddef.h>
#include <stdbool.h>

#define SLICE_FMT "%.*s" 
#define SLICE_ARG(s) (int)(s).len, (s).data
#define SLICE_P(s) printf("<"SLICE_FMT"> (len: %zu)\n", SLICE_ARG(s), (s).len)

typedef struct {
    const char *data;
    size_t len;
} Slice;

Slice slice_create_raw(const char *data);
Slice slice_create(const char *data, size_t len);
Slice slice_slice(Slice *s, char delim);
Slice slice_from_file(const char *fp);
bool slice_eq(Slice a, Slice b);

#endif // LTM_SLICE_H