/*
* rejson -
* Copyright (C) 2016 Redis Labs
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU Affero General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/* Issues/Open
 - sds must: cc -Bsymbolic & ld -fvisibility=hidden
*/

#ifndef IS_REDIS_MODULE
#pragma GCC error "rejson must be compiled as a Redis module"
#endif

#include <string.h>
#include "../deps/rmutil/sds.h"
#include "../deps/rmutil/util.h"
#include "json_object.h"
#include "json_path.h"
#include "object.h"
#include "object_type.h"
#include "redismodule.h"

#define JSONTYPE_ENCODING_VERSION 0
#define JSONTYPE_NAME "OBJECT-RL"
#define RLMODULE_NAME "REJSON"
#define RLMODULE_VERSION "1.0.0"
#define RLMODULE_PROTO "1.0"
#define RLMODULE_DESC "JSON object store"

#define RM_LOGLEVEL_WARNING "warning"

#define OBJECT_ROOT_PATH "."

#define REJSON_ERROR_PARSE_PATH "ERR error parsing path"
#define REJSON_ERROR_EMPTY_STRING "ERR the empty string is not a valid JSON value"
#define REJSON_ERROR_JSONOBJECT_ERROR "ERR unspecified json_object error (probably OOM)"
#define REJSON_ERROR_NEW_NOT_OBJECT "ERR new objects must created from JSON objects"
#define REJSON_ERROR_NEW_NOT_ROOT "ERR new objects must be created at the root ('.')"
#define REJSON_ERROR_PATH_BADTYPE "ERR path includes non-key/non-index"
#define REJSON_ERROR_PATH_NOINDEX "ERR path index out of range"
#define REJSON_ERROR_PATH_NOKEY "ERR path key does not exist"
#define REJSON_ERROR_PATH_INFINDEX "ERR path includes an infinite index"
#define REJSON_ERROR_PATH_NONTERMINAL_INFINITE "ERR infinite index not a terminal path token"
#define REJSON_ERROR_PATH_NONTERMINAL_KEY "ERR missing key not a terminal path token"
#define REJSON_ERROR_PATH_UNKNOWN "ERR unknown path error"
#define REJSON_ERROR_TYPE_INVALID "ERR invalid JSON type encountered"
#define REJSON_ERROR_DICT_SET "ERR could not set key in dictionary"
#define REJSON_ERROR_ARRAY_SET "ERR could not set item in array"
#define REJSON_ERROR_ARRAY_APPEND "ERR could not append item to array"
#define REJSON_ERROR_ARRAY_PREPEND "ERR could not prepend item to array"
#define REJSON_ERROR_SERIALIZE "ERR object serialization to JSON failed"
#define REJSON_ERROR_DICT_DEL "ERR could not delete from dictionary"
#define REJSON_ERROR_ARRAY_DEL "ERR could not delete from array"
#define REJSON_ERROR_TYPE_NOT_ARRAY "ERR operation requires an array"
#define REJSON_ERROR_INSERT "ERR could not insert into array"
#define REJSON_ERROR_INSERT_NONTERMINAL "ERR can not insert into a non terminal list"
#define REJSON_ERROR_INSERT_SUBARRY "ERR could not not prepare the insert operation"
#define REJSON_ERROR_INDEX_INVALID "ERR invalid array index"

static RedisModuleType *JsonType;

// == JsonType methods ==
void *JSONTypeRdbLoad(RedisModuleIO *rdb, int encver) {
    if (encver != JSONTYPE_ENCODING_VERSION) {
        // RedisModule_Log(ctx, RM_LOGLEVEL_WARNING, "Can't load data with version %d", encver);
        return NULL;
    }
    return ObjectTypeRdbLoad(rdb);
}

void JSONTypeAofRewrite(RedisModuleIO *aof, RedisModuleString *key, void *value) {
    // two approaches:
    // 1. For small documents it makes more sense to serialze the entire document in one go
    // 2. Large documents need to be broken to smaller pieces in order to stay within 0.5GB, but
    // we'll need some meta data to make sane-sized chunks so this gets lower priority atm
    Node *n = (Node *)value;

    // serialize it
    JSONSerializeOpt jsopt = {.indentstr = "", .newlinestr = "", .spacestr = ""};
    sds json = sdsnewlen("\"", 1);
    SerializeNodeToJSON(n, &jsopt, &json);
    json = sdscatlen(json, "\"", 1);
    RedisModule_EmitAOF(aof, "JSON.SET", "scb", key, OBJECT_ROOT_PATH, json, sdslen(json));
    sdsfree(json);
}

// == Helpers ==
/* Check if a search path is the root search path (i.e. '.') */
static inline int SearchPath_IsRootPath(SearchPath *sp) {
    return (1 == sp->len && NT_ROOT == sp->nodes[0].type);
}

/* Sets n to the target node by path.
 * p is n's parent, errors are set into err and level is the error's depth
 * Returns PARSE_OK if parsing successful
*/
typedef struct {
    Node *n;        // the referenced node
    Node *p;        // its parent
    SearchPath sp;  // the search path
    PathError err;  // set in case of path error
    int errlevel;   // indicates the level of the error in the path
} JSONPathNode_t;

void JSONPathNode_Free(JSONPathNode_t *jpn) { SearchPath_Free(&jpn->sp); }

int NodeFromJSONPath(Node *root, const RedisModuleString *path, JSONPathNode_t *jpn) {
    jpn->n = NULL;
    jpn->p = NULL;
    jpn->err = E_OK;
    jpn->errlevel = -1;

    // path must be valid, if not provided default to root
    jpn->sp = NewSearchPath(0);
    size_t pathlen;
    const char *spath = RedisModule_StringPtrLen(path, &pathlen);
    if (PARSE_ERR == ParseJSONPath(spath, pathlen, &jpn->sp)) {
        SearchPath_Free(&jpn->sp);
        return PARSE_ERR;
    }
    if (!SearchPath_IsRootPath(&jpn->sp)) {
        jpn->err = SearchPath_FindEx(&jpn->sp, root, &jpn->n, &jpn->p, &jpn->errlevel);
    } else {
        jpn->n = root;
    }

    return PARSE_OK;
}

// == Module Object commands ==
/* OBJ.GET <key>
 *
 * Returns the RESP representation of the object at 'key'. Because RESP's only container is an
 * array, object containers are prefixed with a distinctive character indicating their type as
 * follows:
 *  '{' means that the following entries are a dictionary
 *  '[' means that the following entries are an array
 * Boolean literals and (dictionary key names are simple strings. Strings are always quoted.
 */
int ObjectGet_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if ((argc != 2)) return RedisModule_WrongArity(ctx);
    RedisModule_AutoMemory(ctx);

    // key must be empty (reply with null) or an object type
    RedisModuleKey *key = RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ);
    int type = RedisModule_KeyType(key);
    if (REDISMODULE_KEYTYPE_EMPTY == type) {
        RedisModule_ReplyWithNull(ctx);
        return REDISMODULE_OK;
    } else if (RedisModule_ModuleTypeGetType(key) != JsonType) {
        return RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    Object *objRoot = RedisModule_ModuleTypeGetValue(key);
    ObjectTypeToRespReply(ctx, objRoot);
    return REDISMODULE_OK;
}

// == Module JSON commands ==

/* JSON.TYPE <key> <path>
 * Reports the type of JSON element at `path` in `key`
 * Reply: Simple string
*/
int JSONType_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    // type names
    const char *types[] = {"none",   "null",   "boolean", "integer",
                           "number", "string", "object",  "array"};

    // check args
    if (argc != 3) return RedisModule_WrongArity(ctx);
    RedisModule_AutoMemory(ctx);

    // key must be empty or a JSON type
    RedisModuleKey *key = RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ);
    int type = RedisModule_KeyType(key);
    if (REDISMODULE_KEYTYPE_EMPTY == type) {
        RedisModule_ReplyWithSimpleString(ctx, types[0]);
        return REDISMODULE_OK;
    }
    if (RedisModule_ModuleTypeGetType(key) != JsonType) {
        RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
        return REDISMODULE_ERR;
    }

    // validate path
    JSONPathNode_t jpn;
    Object *objRoot = RedisModule_ModuleTypeGetValue(key);
    if (PARSE_OK != NodeFromJSONPath(objRoot, argv[2], &jpn)) {
        RedisModule_ReplyWithError(ctx, REJSON_ERROR_PARSE_PATH);
        return REDISMODULE_ERR;
    }

    // deal with path errors
    switch (jpn.err) {
        case E_OK:
            break;
        case E_BADTYPE:
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_BADTYPE);
            goto error;
        case E_NOINDEX:
            RedisModule_ReplyWithSimpleString(ctx, types[0]);
            goto error;
        case E_NOKEY:
            RedisModule_ReplyWithSimpleString(ctx, types[0]);
            goto error;
        case E_INFINDEX:
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_INFINDEX);
            goto error;
        default:
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_UNKNOWN);
            goto error;
    }  // switch (err)

    // determine node type
    if (!jpn.n) {
        RedisModule_ReplyWithSimpleString(ctx, types[1]);
    } else {
        switch (jpn.n->type) {
            case N_BOOLEAN:
                RedisModule_ReplyWithSimpleString(ctx, types[2]);
                break;
            case N_INTEGER:
                RedisModule_ReplyWithSimpleString(ctx, types[3]);
                break;
            case N_NUMBER:
                RedisModule_ReplyWithSimpleString(ctx, types[4]);
                break;
            case N_STRING:
                RedisModule_ReplyWithSimpleString(ctx, types[5]);
                break;
            case N_DICT:
                RedisModule_ReplyWithSimpleString(ctx, types[6]);
                break;
            case N_ARRAY:
                RedisModule_ReplyWithSimpleString(ctx, types[7]);
                break;
            default:
                RedisModule_ReplyWithError(ctx, REJSON_ERROR_TYPE_INVALID);
                goto error;
        }  // switch (jpn.n->type)
    }      // else

    JSONPathNode_Free(&jpn);
    return REDISMODULE_OK;

error:
    JSONPathNode_Free(&jpn);
    return REDISMODULE_ERR;
}

/* JSON.LEN <key> <path>
 * Reports the length of JSON element at `path` in `key`
 * If the key does not exist, null is returned.
 * Reply: Integer, specifically the length of the value or -1 if no length is defined for it.
*/
int JSONLen_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    // check args
    if (argc != 3) return RedisModule_WrongArity(ctx);
    RedisModule_AutoMemory(ctx);

    // key must be empty or a JSON type
    RedisModuleKey *key = RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ);
    int type = RedisModule_KeyType(key);
    if (REDISMODULE_KEYTYPE_EMPTY == type) {
        RedisModule_ReplyWithNull(ctx);
        return REDISMODULE_OK;
    }
    if (RedisModule_ModuleTypeGetType(key) != JsonType) {
        RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
        return REDISMODULE_ERR;
    }

    // validate path
    JSONPathNode_t jpn;
    Object *objRoot = RedisModule_ModuleTypeGetValue(key);
    if (PARSE_OK != NodeFromJSONPath(objRoot, argv[2], &jpn)) {
        RedisModule_ReplyWithError(ctx, REJSON_ERROR_PARSE_PATH);
        return REDISMODULE_ERR;
    }

    // deal with path errors
    switch (jpn.err) {
        case E_OK:
            break;
        case E_BADTYPE:
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_BADTYPE);
            goto error;
        case E_NOINDEX:
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_NOINDEX);
            goto error;
        case E_NOKEY:
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_NOKEY);
            goto error;
        case E_INFINDEX:
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_INFINDEX);
            goto error;
        default:
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_UNKNOWN);
            goto error;
    }  // switch (err)

    // determine the length
    if (!jpn.n) {
        RedisModule_ReplyWithLongLong(ctx, -1);
    } else {
        switch (jpn.n->type) {
            case N_BOOLEAN:
            case N_INTEGER:
            case N_NUMBER:
                RedisModule_ReplyWithLongLong(ctx, -1);
                break;
            case N_STRING:
            case N_DICT:
            case N_ARRAY:
                RedisModule_ReplyWithLongLong(ctx, Node_Length(jpn.n));
                break;
            default:
                RedisModule_ReplyWithError(ctx, REJSON_ERROR_TYPE_INVALID);
                goto error;
        }  // switch (jpn.n->type)
    }      // else

    JSONPathNode_Free(&jpn);
    return REDISMODULE_OK;

error:
    JSONPathNode_Free(&jpn);
    return REDISMODULE_ERR;
}

/* JSON.SET <key> <path> <json>
 * Creates or updates the JSON object in `key`
 * Paths always begin at the root (`.`). For paths referencing anything deeper than the root, the
 * starting `.`is optional.
 * Any path from the root begins with a key token. Key tokens are specfied by name and are separated
 * by dots, or with assoicative array syntax: `.foo.bar` and `foo["bar"]` both refer to the key
 * `bar` in the dictionary `foo`.
 * Tokens can also be elements from lists and are specified by their 0-based index in brackets (e.g.
 * `.arr[1]`). Negative index values are treated as is with Python's lists.
 * For new keys, `path` must be the root and `json` must be a JSON object.
 * For existing keys, when the entire  `path` exists, the value it contains is replaced with the
 * `json` value. A key with the value is created when only the last token in the path isn't resolved
 * and its parent is a dictionary. An value is prepended or appended to the parent array if the
 * missing last token is `-inf` or `+inf`, respectively.
 * Reply: Simple string
*/
int JSONSet_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    // check args
    if (argc != 4) return RedisModule_WrongArity(ctx);
    RedisModule_AutoMemory(ctx);

    // key must be empty or a JSON type
    RedisModuleKey *key = RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ | REDISMODULE_WRITE);
    int type = RedisModule_KeyType(key);
    if (REDISMODULE_KEYTYPE_EMPTY != type && RedisModule_ModuleTypeGetType(key) != JsonType) {
        RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
        return REDISMODULE_ERR;
    }

    // JSON must be valid
    size_t jsonlen;
    const char *json = RedisModule_StringPtrLen(argv[3], &jsonlen);
    if (!jsonlen) {
        RedisModule_ReplyWithError(ctx, REJSON_ERROR_EMPTY_STRING);
        goto error;
    }

    // Create object from json
    Object *jo = NULL;
    char *err = NULL;
    if (JSONOBJECT_OK != CreateNodeFromJSON(json, jsonlen, &jo, &err)) {
        if (err) {
            RedisModule_ReplyWithError(ctx, err);
            RedisModule_Free(err);
        } else {
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_JSONOBJECT_ERROR);
        }
        return REDISMODULE_ERR;
    }

    // validate path
    JSONPathNode_t jpn;
    Object *objRoot =
        (REDISMODULE_KEYTYPE_EMPTY == type ? jo : RedisModule_ModuleTypeGetValue(key));
    if (PARSE_OK != NodeFromJSONPath(objRoot, argv[2], &jpn)) {
        RedisModule_ReplyWithError(ctx, REJSON_ERROR_PARSE_PATH);
        return REDISMODULE_ERR;
    }
    int isRootPath = SearchPath_IsRootPath(&jpn.sp);

    if (REDISMODULE_KEYTYPE_EMPTY == type) {
        // new keys must be created at the root
        if (E_OK != jpn.err || !isRootPath) {
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_NEW_NOT_ROOT);
            goto error;
        }
        // new keys must be objects, remember that NULL nodes are possible
        if (!jo || (N_DICT != jo->type)) {
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_NEW_NOT_OBJECT);
            goto error;
        }
        RedisModule_ModuleTypeSetValue(key, JsonType, jo);
    } else {
        // deal with path errors
        switch (jpn.err) {
            case E_OK:
                if (isRootPath) {
                    Node_Free(objRoot);
                    RedisModule_ModuleTypeSetValue(key, JsonType, jo);
                } else if (N_DICT == jpn.p->type) {
                    if (OBJ_OK != Node_DictSet(jpn.p, jpn.sp.nodes[jpn.sp.len - 1].value.key, jo)) {
                        RedisModule_ReplyWithError(ctx, REJSON_ERROR_DICT_SET);
                        goto error;
                    }
                } else {  // must be an array
                    int index = jpn.sp.nodes[jpn.sp.len - 1].value.index;
                    if (index < 0) index = jpn.p->value.arrval.len + index;
                    if (OBJ_OK != Node_ArraySet(jpn.p, index, jo)) {
                        RedisModule_ReplyWithError(ctx, REJSON_ERROR_ARRAY_SET);
                        goto error;
                    }
                    // unlike DictSet, ArraySet does not free so we need to call it explicitly
                    // (TODO?)
                    Node_Free(jpn.n);
                }
                break;
            case E_NOKEY:
                // only allow inserting at terminal
                if (jpn.errlevel != jpn.sp.len - 1) {
                    RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_NONTERMINAL_KEY);
                    goto error;
                }
                if (OBJ_OK != Node_DictSet(jpn.p, jpn.sp.nodes[jpn.sp.len - 1].value.key, jo)) {
                    RedisModule_ReplyWithError(ctx, REJSON_ERROR_DICT_SET);
                    goto error;
                }
                break;
            case E_INFINDEX:
                // only allow inserting at terminal
                if (jpn.errlevel != jpn.sp.len - 1) {
                    RedisModule_ReplyWithError(ctx, REJSON_ERROR_INSERT_NONTERMINAL);
                    goto error;
                }
                if (OBJ_OK != (jpn.sp.nodes[jpn.sp.len - 1].value.positive
                                   ? Node_ArrayAppend(jpn.p, jo)
                                   : Node_ArrayPrepend(jpn.p, jo))) {
                    RedisModule_ReplyWithError(ctx, jpn.sp.nodes[jpn.sp.len - 1].value.positive
                                                        ? REJSON_ERROR_ARRAY_APPEND
                                                        : REJSON_ERROR_ARRAY_PREPEND);
                    goto error;
                }
                break;
            case E_NOINDEX:
                RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_NOINDEX);
                goto error;
            case E_BADTYPE:
                RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_BADTYPE);
                goto error;
            default:
                RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_UNKNOWN);
                goto error;
        }  // switch (err)
    }

    JSONPathNode_Free(&jpn);
    RedisModule_ReplyWithSimpleString(ctx, "OK");

    RedisModule_ReplicateVerbatim(ctx);
    return REDISMODULE_OK;

error:
    JSONPathNode_Free(&jpn);
    if (jo) Node_Free(jo);
    return REDISMODULE_ERR;
}

/* JSON.GET <key> [INDENT <indentation-string>] [NEWLINE <newline-string>] [SPACE <space-string>]
 *                [<path>]
 * Path must be last. If path not given, defaults to root.
 * INDENT: indentation string
 * NEWLINE: newline string
 * SPACE: space string
 * Reply: JSON string
*/
int JSONGet_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if ((argc < 2)) return RedisModule_WrongArity(ctx);
    RedisModule_AutoMemory(ctx);

    // key must be empty (reply with null) or a an object type
    RedisModuleKey *key = RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ);
    int type = RedisModule_KeyType(key);
    if (REDISMODULE_KEYTYPE_EMPTY == type) {
        RedisModule_ReplyWithNull(ctx);
        return REDISMODULE_OK;
    } else if (RedisModule_ModuleTypeGetType(key) != JsonType) {
        RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
        return REDISMODULE_ERR;
    }

    // check for optional arguments
    int pathpos = 2;
    JSONSerializeOpt jsopt = {0};
    if (pathpos < argc) {
        RMUtil_ParseArgsAfter("indent", argv, argc, "c", &jsopt.indentstr);
        if (jsopt.indentstr) {
            pathpos += 2;
        } else {
            jsopt.indentstr = "";
        }
    }
    if (pathpos < argc) {
        RMUtil_ParseArgsAfter("newline", argv, argc, "c", &jsopt.newlinestr);
        if (jsopt.newlinestr) {
            pathpos += 2;
        } else {
            jsopt.newlinestr = "";
        }
    }
    if (pathpos < argc) {
        RMUtil_ParseArgsAfter("space", argv, argc, "c", &jsopt.spacestr);
        if (jsopt.spacestr) {
            pathpos += 2;
        } else {
            jsopt.spacestr = "";
        }
    }

    // verify that 0 or 1 paths were provided
    if (argc - pathpos > 1) return RedisModule_WrongArity(ctx);

    // validate path, if not provided default to root
    JSONPathNode_t jpn;
    Object *objRoot = RedisModule_ModuleTypeGetValue(key);
    if (pathpos < argc) {
        if (PARSE_OK != NodeFromJSONPath(objRoot, argv[pathpos], &jpn)) {
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PARSE_PATH);
            return REDISMODULE_ERR;
        }
    } else {
        NodeFromJSONPath(objRoot, RedisModule_CreateString(ctx, OBJECT_ROOT_PATH, 1), &jpn);
    }

    // initialize the reply
    sds json = sdsempty();

    // deal with errors
    switch (jpn.err) {
        case E_OK:
            break;
        case E_BADTYPE:
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_BADTYPE);
            goto error;
        case E_NOINDEX:
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_NOINDEX);
            goto error;
        case E_NOKEY:
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_NOKEY);
            goto error;
        case E_INFINDEX:
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_INFINDEX);
            goto error;
        default:
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_UNKNOWN);
            goto error;
    }  // switch (jpn.err)

    // serialize it
    SerializeNodeToJSON(jpn.n, &jsopt, &json);
    if (!sdslen(json)) {
        RedisModule_ReplyWithError(ctx, REJSON_ERROR_SERIALIZE);
        goto error;
    }

    RedisModule_ReplyWithStringBuffer(ctx, json, sdslen(json));

    JSONPathNode_Free(&jpn);
    sdsfree(json);
    return REDISMODULE_OK;

error:
    JSONPathNode_Free(&jpn);
    sdsfree(json);
    return REDISMODULE_ERR;
}

/* JSON.MGET <path> <key> [<key> ...]
 * Reply: Array of JSON strings
*/
int JSONMGet_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if ((argc < 2)) return RedisModule_WrongArity(ctx);
    if (RedisModule_IsKeysPositionRequest(ctx)) {
        for (int i = 2; i < argc - 2; i++)
            RedisModule_KeyAtPos(ctx, i);
        return REDISMODULE_OK;
    }
    RedisModule_AutoMemory(ctx);

    // validate search path
    size_t spathlen;
    const char *spath = RedisModule_StringPtrLen(argv[1], &spathlen);
    JSONPathNode_t jpn;
    jpn.sp = NewSearchPath(0);
    if (PARSE_ERR == ParseJSONPath(spath, spathlen, &jpn.sp)) {
        RedisModule_ReplyWithError(ctx, REJSON_ERROR_PARSE_PATH);
        goto error;
    }

    // iterate keys
    RedisModule_ReplyWithArray(ctx, argc - 2);
    int isRootPath = SearchPath_IsRootPath(&jpn.sp);
    JSONSerializeOpt jsopt = {0};
    for (int i = 2; i < argc; i++) {
        RedisModuleKey *key = RedisModule_OpenKey(ctx, argv[i], REDISMODULE_READ);

        // key must an object type, empties and others return null like MGET
        int type = RedisModule_KeyType(key);
        if (REDISMODULE_KEYTYPE_EMPTY == type) goto null;
        if (RedisModule_ModuleTypeGetType(key) != JsonType) goto null;

        // follow the path to the target node
        Node *objRoot = RedisModule_ModuleTypeGetValue(key);
        if (isRootPath) {
            jpn.err = E_OK;
            jpn.n = objRoot;
        } else {
            jpn.err = SearchPath_FindEx(&jpn.sp, objRoot, &jpn.n, &jpn.p, &jpn.errlevel);
        }

        // deal with errors by returning null for them as well
        if (E_OK != jpn.err) goto null;

        // serialize it
        sds json = sdsempty();
        SerializeNodeToJSON(jpn.n, &jsopt, &json);
        RedisModule_ReplyWithStringBuffer(ctx, json, sdslen(json));
        sdsfree(json);
        continue;

    null:
        RedisModule_ReplyWithNull(ctx);
    }

    SearchPath_Free(&jpn.sp);
    return REDISMODULE_OK;

error:
    SearchPath_Free(&jpn.sp);
    return REDISMODULE_ERR;
}

/* JSON.DEL <key> <path> [<path> ...]
 * Reply: Number of paths deleted
*/
int JSONDel_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    // check args
    if (argc < 3) return RedisModule_WrongArity(ctx);
    RedisModule_AutoMemory(ctx);

    // key must be empty or a JSON type
    RedisModuleKey *key = RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ | REDISMODULE_WRITE);
    int type = RedisModule_KeyType(key);
    if (REDISMODULE_KEYTYPE_EMPTY == type) {
        RedisModule_ReplyWithLongLong(ctx, 0);
        return REDISMODULE_OK;
    } else if (RedisModule_ModuleTypeGetType(key) != JsonType) {
        RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
        return REDISMODULE_ERR;
    }

    // The stored JSON object root
    Node *objRoot = RedisModule_ModuleTypeGetValue(key);

    // paths must be valid and resolvable in the object
    int npaths = argc - 2;
    SearchPath *sp = calloc(npaths, sizeof(SearchPath));
    Node **targets = calloc(npaths, sizeof(Node *));
    int foundroot = 0;

    for (int i = 0; i < npaths; i++) {
        // first validate the path
        size_t pathlen;
        const char *path = RedisModule_StringPtrLen(argv[i + 2], &pathlen);
        sp[i] = NewSearchPath(0);
        if (PARSE_ERR == ParseJSONPath(path, pathlen, &sp[i])) {
            npaths = i + 1;
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PARSE_PATH);
            goto error;
        }
        // next try resolving it
        if (SearchPath_IsRootPath(&sp[i])) {
            targets[i] = objRoot;
            foundroot = 1;
        } else {
            PathError pe = SearchPath_Find(&sp[i], objRoot, &targets[i]);
            if (E_OK != pe) {
                switch (pe) {
                    case E_BADTYPE:
                        RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_BADTYPE);
                        break;
                    case E_NOINDEX:
                        RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_NOINDEX);
                        break;
                    case E_NOKEY:
                        RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_NOKEY);
                        break;
                    default:
                        RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_UNKNOWN);
                        break;
                }  // switch (pe)
                goto error;
            }  // if (E_OK != pe)
        }
    }

    // Iterate the target paths and remove them from their respective parents
    // although root could appear anywhere, but we just delete the key if it is present
    int deleted = 0;
    if (foundroot) {
        deleted = 1;
        RedisModule_DeleteKey(key);
    } else {
        for (int i = 0; i < npaths; i++) {
            Node *objParent;

            // Who's your daddy? TODO: maybe move to path.c
            if (sp[i].len == 1) {
                objParent = objRoot;
            } else {
                // reuse the search path to find the target's parent
                sp[i].len--;
                PathError pe = SearchPath_Find(&sp[i], objRoot, &objParent);
                sp[i].len++;
            }

            // delete the target
            if (N_DICT == objParent->type) {
                if (OBJ_OK != Node_DictDel(objParent, sp[i].nodes[sp[i].len - 1].value.key)) {
                    RedisModule_ReplyWithError(ctx, REJSON_ERROR_DICT_DEL);
                    goto error;
                } else {
                    deleted++;
                }
            } else {  // must be an array
                if (OBJ_OK != Node_ArrayDel(objParent, sp[i].nodes[sp[i].len - 1].value.index)) {
                    RedisModule_ReplyWithError(ctx, REJSON_ERROR_ARRAY_DEL);
                    goto error;
                } else {
                    deleted++;
                }
            }
        }  // for (int i = 0; i < npaths; i++)
    }      // else

    for (int i = 0; i < npaths; i++) SearchPath_Free(&sp[i]);
    free(targets);
    RedisModule_ReplyWithLongLong(ctx, (long long)deleted);

    RedisModule_ReplicateVerbatim(ctx);
    return REDISMODULE_OK;

error:
    if (sp) {
        for (int i = 0; i < npaths; i++) SearchPath_Free(&sp[i]);
    }
    free(targets);
    return REDISMODULE_ERR;
}

/* JSON.INSERT <key> <path> <json> [<json> ...]
 * Inserts the `json` value(s) to the terminal array at `path` before the `index`
 * Inserting at index 0, -inf or any -N where N is larger then the array's size is the equivalent of
 * prepending to it (i.e. LPUSH). Similarly, appending is done with +inf or any positive index geq
 * to the length.
 * Reply: Integer, specifically the array's new size
*/
int JSONInsert_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    // check args
    if (argc < 4) return RedisModule_WrongArity(ctx);
    RedisModule_AutoMemory(ctx);

    // key can't be empty and must be a JSON type
    RedisModuleKey *key = RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ | REDISMODULE_WRITE);
    int type = RedisModule_KeyType(key);
    if (REDISMODULE_KEYTYPE_EMPTY == type) {
        // an empty key has no paths so break early
        RedisModule_ReplyWithError(ctx, REJSON_ERROR_TYPE_NOT_ARRAY);
        return REDISMODULE_ERR;
    } else if (RedisModule_ModuleTypeGetType(key) != JsonType) {
        RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
        return REDISMODULE_ERR;
    }

    // validate path
    JSONPathNode_t jpn;
    Object *objRoot = RedisModule_ModuleTypeGetValue(key);
    if (PARSE_OK != NodeFromJSONPath(objRoot, argv[2], &jpn)) {
        RedisModule_ReplyWithError(ctx, REJSON_ERROR_PARSE_PATH);
        return REDISMODULE_ERR;
    }

    // extract the index and deal with path errors
    int index;
    switch (jpn.err) {
        case E_OK:
            // the target must be an array
            if (N_ARRAY != jpn.p->type) {
                RedisModule_ReplyWithError(ctx, REJSON_ERROR_TYPE_NOT_ARRAY);
                goto error;
            }
            index = jpn.sp.nodes[jpn.sp.len - 1].value.index;
            if (index < 0) {
                index = jpn.p->value.arrval.len + index;
                if (index < 0) index = 0;
            } else if (index >= jpn.p->value.arrval.len) {
                index = jpn.p->value.arrval.len;
            }
            break;
        case E_NOINDEX:
            // only allow inserting at terminal
            if (jpn.errlevel != jpn.sp.len - 1) {
                RedisModule_ReplyWithError(ctx, REJSON_ERROR_INSERT_NONTERMINAL);
                goto error;
            }
            // get index
            index = jpn.sp.nodes[jpn.errlevel].value.index;
            if (index < 0) {
                index = MIN(jpn.p->value.arrval.len + index, 0);
                if (index < 0) index = 0;
            } else if (index >= jpn.p->value.arrval.len) {
                index = jpn.p->value.arrval.len;
            }
            break;
        case E_INFINDEX:
            // only allow inserting at terminal
            if (jpn.errlevel != jpn.sp.len - 1) {
                RedisModule_ReplyWithError(ctx, REJSON_ERROR_INSERT_NONTERMINAL);
                goto error;
            }
            // get index
            index = jpn.sp.nodes[jpn.errlevel].value.positive ? jpn.p->value.arrval.len : 0;
            break;
        case E_BADTYPE:
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_BADTYPE);
            goto error;
        case E_NOKEY:
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_NOKEY);
            goto error;
        default:
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_UNKNOWN);
            goto error;
    }  // switch (jpn.err)

    // make an array from the JSON values
    Node *sub = NewArrayNode(argc - 3);
    for (int i = 3; i < argc; i++) {
        // JSON must be valid
        size_t jsonlen;
        const char *json = RedisModule_StringPtrLen(argv[i], &jsonlen);
        if (!jsonlen) {
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_EMPTY_STRING);
            Node_Free(sub);
            goto error;
        }

        // create object from json
        Object *jo = NULL;
        char *jerr = NULL;
        if (JSONOBJECT_OK != CreateNodeFromJSON(json, jsonlen, &jo, &jerr)) {
            Node_Free(sub);
            if (jerr) {
                RedisModule_ReplyWithError(ctx, jerr);
                RedisModule_Free(jerr);
            } else {
                RedisModule_ReplyWithError(ctx, REJSON_ERROR_JSONOBJECT_ERROR);
            }
            goto error;
        }

        // append it to the sub array
        if (OBJ_OK != Node_ArrayAppend(sub, jo)) {
            Node_Free(jo);
            Node_Free(sub);
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_INSERT_SUBARRY);
            goto error;
        }
    }

    // insert it
    if (OBJ_OK != Node_ArrayInsert(jpn.p, index, sub)) {
        Node_Free(sub);
        RedisModule_ReplyWithError(ctx, REJSON_ERROR_INSERT);
        goto error;
    }

    RedisModule_ReplyWithLongLong(ctx, jpn.p->value.arrval.len);

    JSONPathNode_Free(&jpn);
    return REDISMODULE_OK;

error:
    JSONPathNode_Free(&jpn);
    return REDISMODULE_ERR;
}

/* JSON.INDEX <key> <path> <scalar> [start] [stop]
 * Returns the lowest index of value in the array, optionally between start (default 0) and stop
 * (default -1)
*/
int JSONIndex_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    // check args
    if ((argc < 4) || (argc > 6)) return RedisModule_WrongArity(ctx);
    RedisModule_AutoMemory(ctx);

    // key can't be empty and must be a JSON type
    RedisModuleKey *key = RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ);
    int type = RedisModule_KeyType(key);
    if (REDISMODULE_KEYTYPE_EMPTY == type) {
        // an empty key has no paths so break early
        RedisModule_ReplyWithError(ctx, REJSON_ERROR_TYPE_NOT_ARRAY);
        return REDISMODULE_ERR;
    } else if (RedisModule_ModuleTypeGetType(key) != JsonType) {
        RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
        return REDISMODULE_ERR;
    }

    // validate path
    JSONPathNode_t jpn;
    Object *objRoot = RedisModule_ModuleTypeGetValue(key);
    if (PARSE_OK != NodeFromJSONPath(objRoot, argv[2], &jpn)) {
        RedisModule_ReplyWithError(ctx, REJSON_ERROR_PARSE_PATH);
        return REDISMODULE_ERR;
    }

    // deal with errors
    switch (jpn.err) {
        case E_OK:
            if (N_ARRAY != jpn.n->type) {
                RedisModule_ReplyWithError(ctx, REJSON_ERROR_TYPE_NOT_ARRAY);
                goto error;
            }
            break;
        case E_BADTYPE:
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_BADTYPE);
            goto error;
        case E_NOINDEX:
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_NOINDEX);
            goto error;
        case E_NOKEY:
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_NOKEY);
            goto error;
        case E_INFINDEX:
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_INFINDEX);
            goto error;
        default:
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_UNKNOWN);
            goto error;
    }  // switch (jpn.err)

    // JSON must be valid
    size_t jsonlen;
    const char *json = RedisModule_StringPtrLen(argv[3], &jsonlen);
    if (!jsonlen) {
        RedisModule_ReplyWithError(ctx, REJSON_ERROR_EMPTY_STRING);
        goto error;
    }

    // create object from json
    Object *jo = NULL;
    char *jerr = NULL;
    if (JSONOBJECT_OK != CreateNodeFromJSON(json, jsonlen, &jo, &jerr)) {
        if (jerr) {
            RedisModule_ReplyWithError(ctx, jerr);
            RedisModule_Free(jerr);
        } else {
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_JSONOBJECT_ERROR);
        }
        goto error;
    }

    // get start & stop, translate negatives
    long long start = 0, stop = -1;
    if (argc > 4) {
        if (REDISMODULE_OK != RedisModule_StringToLongLong(argv[4], &start)) {
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_INDEX_INVALID);
            goto error;
        }
        if (argc > 5) {
            if (REDISMODULE_OK != RedisModule_StringToLongLong(argv[5], &stop)) {
                RedisModule_ReplyWithError(ctx, REJSON_ERROR_INDEX_INVALID);
                goto error;
            }
        }
    }

    RedisModule_ReplyWithLongLong(ctx, Node_ArrayIndex(jpn.n, jo, (int)start, (int)stop));

    JSONPathNode_Free(&jpn);
    return REDISMODULE_OK;

error:
    JSONPathNode_Free(&jpn);
    return REDISMODULE_ERR;
}

int RedisModule_OnLoad(RedisModuleCtx *ctx) __attribute__((visibility("default")));
int RedisModule_OnLoad(RedisModuleCtx *ctx) {
    // Register the module
    if (RedisModule_Init(ctx, RLMODULE_NAME, 1, REDISMODULE_APIVER_1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    // Register the JSON data type
    RedisModuleTypeMethods tm = {.version = REDISMODULE_TYPE_METHOD_VERSION,
                                 .rdb_load = JSONTypeRdbLoad,
                                 .rdb_save = ObjectTypeRdbSave,
                                 .aof_rewrite = JSONTypeAofRewrite,
                                 .free = ObjectTypeFree};
    JsonType = RedisModule_CreateDataType(ctx, JSONTYPE_NAME, JSONTYPE_ENCODING_VERSION, &tm);

    /* Object commands. */
    if (RedisModule_CreateCommand(ctx, "obj.get", ObjectGet_RedisCommand, "readonly", 1, 1, 1) ==
        REDISMODULE_ERR)
        return REDISMODULE_ERR;

    /* Main commands. */
    if (RedisModule_CreateCommand(ctx, "json.type", JSONType_RedisCommand, "readonly", 1, 1, 1) ==
        REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx, "json.len", JSONLen_RedisCommand, "readonly", 1, 1, 1) ==
        REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx, "json.set", JSONSet_RedisCommand, "write deny-oom", 1, 1,
                                  1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx, "json.get", JSONGet_RedisCommand, "readonly", 1, 1, 1) ==
        REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx, "json.mget", JSONMGet_RedisCommand, "readonly getkeys-api", 1,
                                  1, 1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx, "json.del", JSONDel_RedisCommand, "write", 1, 1, 1) ==
        REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx, "json.forget", JSONDel_RedisCommand, "write", 1, 1, 1) ==
        REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx, "json.insert", JSONInsert_RedisCommand, "write", 1, 1, 1) ==
        REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx, "json.index", JSONIndex_RedisCommand, "readonly", 1, 1, 1) ==
        REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
