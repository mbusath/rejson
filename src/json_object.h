#ifndef __JSON_OBJECT_H__
#define __JSON_OBJECT_H__

#include <stdlib.h>
#include "object.h"

#define JSONOBJECT_OK 0
#define JSONOBJECT_ERROR 1
#define JSONOBJECT_ERROR_ALLOC 2

/**
* Parses a JSON stored in `buf` of size `len` and creates an object.
* The resulting object tree is stored in `node` and in case of error the optional `err` is set with
* the relevant error message.
*
* Note: the JSONic 'null' is represented internally as NULL, so `node` can be NULL even when the
*       return code is JSONOBJECT_OK.
*/
int CreateNodeFromJSON(const char *buf, size_t len, Node **node, char **err);

/**
* Produces a JSON serialization from an object.
*/
int SerializeNodeToJSON(const Node *node, int prettify, char **json);

#endif