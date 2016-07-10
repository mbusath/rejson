#include "vector_util.h"
#include "../deps/rmutil/vector.h"

// TODO: consider moving this and Vector_Last to vector.c/h
// ALSO: Vector_Size & Vector_Cap need to be static inline to be found, or inline moved to .c

// Pops a vector's last element (no resizing though)
void *Vector_Pop(Vector *v) {
    void *ret = NULL;
    if (v->top) {
        Vector_Get(v, v->top - 1, &ret);
        v->top--;
    }
    return ret;
}