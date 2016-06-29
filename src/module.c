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
*/

#ifndef IS_REDIS_MODULE
#pragma GCC error "rejson must be compiled as a Redis module"
#endif

#include "redismodule.h"
#include "object.h"
#include "json_object.h"
#include "json_path.h"
// #include "../deps/rmutil/util.h"
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
#define REJSON_ERROR_PATH_PARENT "ERR couldn't locate parent... that's odd"
#define REJSON_ERROR_DICT_SET "ERR couldn't set key in dictionary"
#define REJSON_ERROR_ARRAY_SET "ERR couldn't set item in array"
#define REJSON_ERROR_SERIALIZE "ERR object serialization to JSON failed"

static RedisModuleType *JsonType;

// == JsonType methods ==
void *JsonTypeRdbLoad(RedisModuleIO *rdb, int encver) {
    if (encver != JSONTYPE_ENCODING_VERSION) {
        /* RedisModule_Log("warning","Can't load data with version %d", encver);*/
        return NULL;
    }
}

void JsonTypeRdbSave(RedisModuleIO *rdb, void *value) {}

void JsonTypeAofRewrite(RedisModuleIO *aof, RedisModuleString *key, void *value) {}

void JsonTypeDigest(RedisModuleDigest *digest, void *value) {
    // TODO: once digest is impelemented.
}

void JsonTypeFree(void *value) {
    if (value) Node_Free(value);
}

// == Module commands ==
/* JSON.SET <key> <path> <json> [SCHEMA <schema-key>]
*/
int JSONSet_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    // check args
    if ((argc < 4) || (argc > 6)) return RedisModule_WrongArity(ctx);
    RedisModule_AutoMemory(ctx);

    // key must be empty or a JSON type
    RedisModuleKey *key = RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ | REDISMODULE_WRITE);
    int type = RedisModule_KeyType(key);
    if (REDISMODULE_KEYTYPE_EMPTY != type && RedisModule_ModuleTypeGetType(key) != JsonType) {
        return RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
    }

    // path must be valid, the root path is an exception
    size_t pathlen;
    const char *path = RedisModule_StringPtrLen(argv[2], &pathlen);
    SearchPath sp = NewSearchPath(0);
    if (strncmp(OBJECT_ROOT_PATH, path,
                pathlen)) {  // TODO: this fails if the path has whitespaces...
        if (PARSE_ERR == ParseJSONPath(path, pathlen, &sp)) {
            RedisModule_ReplyWithError(ctx, REJSON_ERROR_PARSE_PATH);
            goto error;
        }
    }

    // Subcommand SCHEMA must be an existing JSON schema or the empty string

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

    // JSON must be validated against schema

    // The stored JSON object root
    if (REDISMODULE_KEYTYPE_EMPTY == type) {
        // new keys must be created at the root
        if (sp.len) {
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
        if (sp.len) {   // anything but the root
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
                if (E_OK != pe) {
                    RedisModule_ReplyWithError(ctx, REJSON_ERROR_PATH_PARENT);
                    goto error;
                }
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
                Node_Free(objTarget);
            }
            SearchPath_Free(&sp);
        } else {  // if (sp.len) <- replace the root
            // TODO: FIX MEMORY LEAK
            RedisModule_DeleteKey(key);
            RedisModule_ModuleTypeSetValue(key, JsonType, jo);
        }
    }

    RedisModule_ReplyWithSimpleString(ctx, "OK");

    RedisModule_ReplicateVerbatim(ctx);
    return REDISMODULE_OK;

error:
    SearchPath_Free(&sp);
    if (jo) Node_Free(jo);
    return REDISMODULE_ERR;
}

/* JSON.GET <key> [FORMAT PRETTIFY|MINIFY] [INDENT <indentation-string>] [BREAK <line-break-string>] [<path>]
* if path not given, defaults to root
* FORMAT defaults to prettify
* IDENTSTR: string (applies only to PRETTYIFY)
* BREAKSTR: string (applies only to PRETTYIFY)
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
    // RMUtil_ParseArgsAfter();

    // path must be valid, the root path is an exception
    size_t pathlen;
    const char *path;
    SearchPath sp = NewSearchPath(0);
    if (argc == 3) {
        path = RedisModule_StringPtrLen(argv[2], &pathlen);
        if (strncmp(OBJECT_ROOT_PATH, path, pathlen)) {
            if (PARSE_ERR == ParseJSONPath(path, pathlen, &sp)) {
                RedisModule_ReplyWithError(ctx, REJSON_ERROR_PARSE_PATH);
                return REDISMODULE_ERR;
            }
        }
    }

    Object *objRoot = RedisModule_ModuleTypeGetValue(key);
    Object *objTarget;

    if (sp.len) {
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
    char *str = NULL;
    if (JSONOBJECT_OK != SerializeNodeToJSON(objTarget, "\t", " ", "\n", &str)) {
        RedisModule_ReplyWithError(ctx, REJSON_ERROR_SERIALIZE);
        return REDISMODULE_ERR;
    }

    RedisModule_ReplyWithStringBuffer(ctx, str, strlen(str));

    RedisModule_Free(str);
    SearchPath_Free(&sp);
    return REDISMODULE_OK;
}

/* Unit test entry point for the module. */
int TestModule(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    RedisModule_AutoMemory(ctx);

    RedisModule_ReplyWithSimpleString(ctx, "PASS");
    return REDISMODULE_OK;
}

int RedisModule_OnLoad(RedisModuleCtx *ctx) {
    if (RedisModule_Init(ctx, RLMODULE_NAME, 1, REDISMODULE_APIVER_1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    JsonType = RedisModule_CreateDataType(ctx, JSONTYPE_NAME, JSONTYPE_ENCODING_VERSION,
                                          JsonTypeRdbLoad, JsonTypeRdbSave, JsonTypeAofRewrite,
                                          JsonTypeDigest, JsonTypeFree);

    // /* Main commands. */
    if (RedisModule_CreateCommand(ctx, "json.set", JSONSet_RedisCommand,
                                  "write deny-oom getkeys-api", 0, 0, 0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx, "json.get", JSONGet_RedisCommand, "readonly", 1, 1, 1) ==
        REDISMODULE_ERR)
        return REDISMODULE_ERR;

    /* Testing. */
    if (RedisModule_CreateCommand(ctx, "json.test", TestModule, "write", 1, 1, 1) ==
        REDISMODULE_ERR)
        return REDISMODULE_ERR;

    return REDISMODULE_OK;
}
