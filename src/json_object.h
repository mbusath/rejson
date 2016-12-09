#ifndef __JSON_OBJECT_H__
#define __JSON_OBJECT_H__

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include "../deps/rmutil/sds.h"
#include "jsonsl.h"
#include "object.h"
#include "rmalloc.h"

#define JSONOBJECT_OK 0
#define JSONOBJECT_ERROR 1

#define JSONOBJECT_MAX_ERROR_STRING_LENGTH 256

/**
* Parses a JSON stored in `buf` of size `len` and creates an object.
* The resulting object tree is stored in `node` and in case of error the optional `err` is set with
* the relevant error message.
*
* Note: the JSONic 'null' is represented internally as NULL, so `node` can be NULL even when the
*       return code is JSONOBJECT_OK.
*/
int CreateNodeFromJSON(const char *buf, size_t len, Node **node, char **err);

typedef struct {
    char *indentstr;   // indentation string
    char *newlinestr;  // linebreak string
    char *spacestr;    // spacing before/after element in size=1 containers, and after keys
} JSONSerializeOpt;

/**
* Produces a JSON serialization from an object.
*/
void SerializeNodeToJSON(const Node *node, const JSONSerializeOpt *opt, sds *json);

#endif