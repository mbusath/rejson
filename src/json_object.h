#ifndef __JSON_OBJECT_H__
#define __JSON_OBJECT_H__

#include <stdlib.h>
#include "object.h"

#define JSONOBJECT_OK 0
#define JSONOBJECT_ERROR 1
#define JSONOBJECT_ERROR_ALLOC 2

Node *CreateNodeFromJSON(const char *buf, size_t len, char *err);

#endif