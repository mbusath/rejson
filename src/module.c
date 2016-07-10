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
 - Appending to lists? Pushing at the beginning? Inserting in the middle?
 - JSON.COUNT key path
 - sds must: cc -Bsymbolic & ld -fvisibility=hidden
*/

#ifndef IS_REDIS_MODULE
#pragma GCC error "rejson must be compiled as a Redis module"
#endif

#include "redismodule.h"
#include "object.h"
#include "object_type.h"
#include "json_object.h"
#include "json_path.h"
#include "../deps/rmutil/util.h"
#include "../deps/rmutil/sds.h"
#include <string.h>

#define JSONTYPE_ENCODING_VERSION 0
#define JSONTYPE_NAME "JSON-MERZ"
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
#define REJSON_ERROR_PATH_UNKNOWN "ERR unknown path error"
#define REJSON_ERROR_DICT_SET "ERR couldn't set key in dictionary"
#define REJSON_ERROR_ARRAY_SET "ERR couldn't set item in array"
#define REJSON_ERROR_SERIALIZE "ERR object serialization to JSON failed"
#define REJSON_ERROR_DICT_DEL "ERR could not delete from dictionary"
#define REJSON_ERROR_ARRAY_DEL "ERR could not delete from array"

static RedisModuleType *JsonType;

// == JsonType methods ==
void *JSONTypeRdbLoad(RedisModuleIO *rdb, int encver) {
    if (encver != JSONTYPE_ENCODING_VERSION) {
        /* RedisModule_Log("warning","Can't load data with version %d", encver);*/
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

// == Module commands ==
/* JSON.SET <key> <path> <json> [SCHEMA <schema-key>]
 * Reply: OK
*/
int JSONSet_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    // check args
    if ((argc < 4) || (argc > 6)) return RedisModule_WrongArity(ctx);
    RedisModule_AutoMemory(ctx);

    // TODO: handle getkeys-api request due to SCHEMA being an optional arg

    // key must be empty or a JSON type
    RedisModuleKey *key = RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ | REDISMODULE_WRITE);
    int type = RedisModule_KeyType(key);
    if (REDISMODULE_KEYTYPE_EMPTY != type && RedisModule_ModuleTypeGetType(key) != JsonType) {
        return RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    // path must be valid
    size_t pathlen;
    const char *path = RedisModule_StringPtrLen(argv[2], &pathlen);
    SearchPath sp = NewSearchPath(0);
    if (PARSE_ERR == ParseJSONPath(path, pathlen, &sp)) {
        RedisModule_ReplyWithError(ctx, REJSON_ERROR_PARSE_PATH);
        goto error;
    }

    // TODO: Subcommand SCHEMA must be an existing JSON schema or the empty string

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
    if (!(JSONOBJECT_OK == CreateNodeFromJSON(json, jsonlen, &jo, &err))) {
        if (err) {
            RedisModule_ReplyWithError(ctx, err);
            RedisModule_Free(err);
        } else {
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_JSONOBJECT_ERROR);
        }
        goto error;
    }

    // TODO: JSON must be validated against schema

    // The stored JSON object root
    if (REDISMODULE_KEYTYPE_EMPTY == type) {
        // new keys must be created at the root
        if (!SearchPath_IsRootPath(&sp)) {
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_NEW_NOT_ROOT);
            goto error;
        }
        // new keys must be objects, remember that NULL nodes are possible
        if (!jo || (N_DICT != jo->type)) {
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_NEW_NOT_OBJECT);
            goto error;
        }
        RedisModule_ModuleTypeSetValue(key, JsonType, jo);
    } else {  // if (REDISMODULE_KEYTYPE_EMPTY == type)
        Node *objRoot = RedisModule_ModuleTypeGetValue(key);
        Node *objTarget, *objParent;
        if (!SearchPath_IsRootPath(&sp)) {  // anything but the root
            PathError pe = SearchPath_Find(&sp, objRoot, &objTarget);
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

            // Who's your daddy? TODO: maybe move to path.c
            if (sp.len == 1) {
                objParent = objRoot;
            } else {
                // reuse the search path to find the target's parent
                sp.len--;
                pe = SearchPath_Find(&sp, objRoot, &objParent);
                sp.len++;
            }

            // replace target with jo
            if (N_DICT == objParent->type) {
                if (OBJ_OK != Node_DictSet(objParent, sp.nodes[sp.len - 1].value.key, jo)) {
                    RedisModule_ReplyWithError(ctx, REJSON_ERROR_DICT_SET);
                    goto error;
                }
            } else {  // must be an array
                if (OBJ_OK != Node_ArraySet(objParent, sp.nodes[sp.len - 1].value.index, jo)) {
                    RedisModule_ReplyWithError(ctx, REJSON_ERROR_ARRAY_SET);
                    goto error;
                }
                // unlike DictSet, ArraySet does not free so we need to call it explicitly (TODO?)
                Node_Free(objTarget);
            }
        } else {  // if (sp.len) <- replace the root
            RedisModule_DeleteKey(key);
            RedisModule_ModuleTypeSetValue(key, JsonType, jo);
        }
    }

    SearchPath_Free(&sp);
    RedisModule_ReplyWithSimpleString(ctx, "OK");

    RedisModule_ReplicateVerbatim(ctx);
    return REDISMODULE_OK;

error:
    SearchPath_Free(&sp);
    if (jo) Node_Free(jo);
    return REDISMODULE_ERR;
}

/* JSON.GET <key> INDENT <indentation-string>] [NEWLINE <newline-string>] [SPACE
 * <space-string>] [<path>]
 * if path not given, defaults to root
 * INDENT: indentation string
 * NEWLINE: newline string
 * SPACE: space string
 * Reply: JSON string
*/
int JSONGet_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if ((argc < 2)) return RedisModule_WrongArity(ctx);
    RedisModule_AutoMemory(ctx);

    // key must be empty (reply with null) or a JSON type
    RedisModuleKey *key = RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ | REDISMODULE_WRITE);
    int type = RedisModule_KeyType(key);
    if (REDISMODULE_KEYTYPE_EMPTY == type) {
        RedisModule_ReplyWithNull(ctx);
        return REDISMODULE_OK;
    } else if (RedisModule_ModuleTypeGetType(key) != JsonType) {
        return RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
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

    // path must be valid, if not provided default to root
    size_t pathlen;
    const char *path;
    SearchPath sp = NewSearchPath(0);
    if (pathpos < argc) {
        path = RedisModule_StringPtrLen(argv[pathpos], &pathlen);
        if (PARSE_ERR == ParseJSONPath(path, pathlen, &sp)) {
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PARSE_PATH);
            return REDISMODULE_ERR;
        }
    } else {
        ParseJSONPath(OBJECT_ROOT_PATH, 1, &sp);
    }

    Object *objRoot = RedisModule_ModuleTypeGetValue(key);
    Object *objTarget;  // the node targetted for replacement

    if (!SearchPath_IsRootPath(&sp)) {
        PathError pe = SearchPath_Find(&sp, objRoot, &objTarget);
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
            SearchPath_Free(&sp);
            return PARSE_ERR;
        }  // if (E_OK != pe)
    } else {
        objTarget = objRoot;
    }

    // serialize it
    sds json = sdsempty();
    SerializeNodeToJSON(objTarget, &jsopt, &json);
    if (!sdslen(json)) {
        RedisModule_ReplyWithError(ctx, REJSON_ERROR_SERIALIZE);
        return REDISMODULE_ERR;
    }

    RedisModule_ReplyWithStringBuffer(ctx, json, sdslen(json));

    sdsfree(json);
    SearchPath_Free(&sp);

    return REDISMODULE_OK;
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
        return RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
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
                      // TODO: delete from array?
                // if (OBJ_OK != Node_ArrayDel(objParent, sp[i].nodes[sp[i].len - 1].value.index)) {
                //     RedisModule_ReplyWithError(ctx, REJSON_ERROR_ARRAY_DEL);
                //     goto error;
                // } else {
                //     deleted++;
                // }
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

/* Unit test entry point for the module. */
int TestModule(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    RedisModule_AutoMemory(ctx);

    RedisModule_ReplyWithSimpleString(ctx, "PASS");
    return REDISMODULE_OK;
}

int RedisModule_OnLoad(RedisModuleCtx *ctx) __attribute__((visibility("default")));
int RedisModule_OnLoad(RedisModuleCtx *ctx) {
    if (RedisModule_Init(ctx, RLMODULE_NAME, 1, REDISMODULE_APIVER_1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    JsonType = RedisModule_CreateDataType(ctx, JSONTYPE_NAME, JSONTYPE_ENCODING_VERSION,
                                          JSONTypeRdbLoad, ObjectTypeRdbSave, JSONTypeAofRewrite,
                                          ObjectTypeDigest, ObjectTypeFree);

    // /* Main commands. */
    if (RedisModule_CreateCommand(ctx, "json.set", JSONSet_RedisCommand,
                                  "write deny-oom getkeys-api", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx, "json.get", JSONGet_RedisCommand, "readonly", 1, 1, 1) ==
        REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx, "json.del", JSONDel_RedisCommand, "write", 1, 1, 1) ==
        REDISMODULE_ERR)
        return REDISMODULE_ERR;

    /* Testing. */
    if (RedisModule_CreateCommand(ctx, "json.test", TestModule, "write", 1, 1, 1) ==
        REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
