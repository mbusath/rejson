#ifndef __VECTOR_UTIL_H__
#define __VECTOR_UTIL_H__

#include "../deps/rmutil/vector.h"

#define Vector_Last(v) Vector_Size(v) - 1

void *Vector_Pop(Vector *v);

#endif