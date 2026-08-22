// USAGE(darray.h):
//
// typedef struct {
//    #Type# *data;
//    size_t len;
//    size_t cap;
// } #Name#;
//
// Example1: *************************************
//
// typedef struct {
//    int *data;
//    size_t len;
//    size_t cap;
// } IntegerDynamicArray;
//
// IntegerDynamicArray arr = {0};
// da_append(arr, 2);
// da_append(arr, 3);
// da_append(arr, -1);
// da_append(arr, 0);
// ...
// da_del(arr);
//
// Example2: *************************************
//
// typedef struct {
//    float x, y;
// } Vector2;
//
// typedef struct {
//    Vector2 *data;
//    size_t len;
//    size_t cap;
// } Vector2DynamicArray;
//
// Vector2DynamicArray arr = {0};
// da_append(arr, ((Vector2){1, 1}));
// da_append(arr, ((Vector2){-1, 1}));
// da_append(arr, ((Vector2){0, 1}));
// da_append(arr, ((Vector2){1, 0}));
// ...
// da_del(arr);

#ifndef DARRAY_H
#define DARRAY_H

#include <assert.h>
#include <stdlib.h>

#define DA_CHUNK_LEN 30

// append elem to dinamic array
#define da_append(da, elem)                                               \
    do {                                                                  \
        if((da).cap == 0) {                                               \
            (da).cap = DA_CHUNK_LEN;                                      \
            (da).data = malloc(sizeof(elem) * (da).cap);                 \
            if((da).data == NULL) assert(0 && "ERROR: not enough RAM");  \
        }                                                                 \
        if((da).cap == (da).len) {                                        \
            (da).cap += DA_CHUNK_LEN;                                     \
            (da).data = realloc((da).data, sizeof(elem) * (da).cap);    \
            if((da).data == NULL) assert(0 && "ERROR: not enough RAM");  \
        }                                                                 \
        (da).data[(da).len++] = elem;                                    \
    } while(0)
//

// pop elem from dinamic array
#define da_pop(da, idx)                                                                   \
    do {                                                                                  \
        assert(0 < (da).len && idx < (da).len);                                           \
        (da).len--;                                                                       \
        for(size_t __da__iter__i__ = idx; __da__iter__i__ < (da).len; ++__da__iter__i__)  \
            (da).data[__da__iter__i__] = (da).data[__da__iter__i__ + 1];                \
    } while(0)
//

// delete full dinamic array
#define da_del(da)        \
    do {                  \
        (da).cap = 0;     \
        (da).len = 0;     \
        free((da).data); \
    } while (0)
//

// expands to a pointer to the last element of the dinamic array
#define da_last(da) ((da).data + (da).len - 1)

#endif // DARRAY_H