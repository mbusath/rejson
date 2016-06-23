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

#include <string.h>
#include "redismodule.h"
#include "object.h"

static RedisModuleType *JsonType;
#define JSONTYPE_ENCODING_VERSION 0
#define JSONTYPE_NAME "REJSON-RL"
#define RLMODULE_NAME "REJSON"
#define RLMODULE_VERSION "1.0.0"
#define RLMODULE_PROTO "1.0"
#define RLMODULE_DESC "JSON date structure"

#define REDIS_LOG(str) fprintf(stderr, "rejson.so: %s\n", str);

// == JsonType methods ==
void *JsonTypeRdbLoad(RedisModuleIO *rdb, int encver) {
  if (encver != JSONTYPE_ENCODING_VERSION) {
    /* RedisModule_Log("warning","Can't load data with version %d", encver);*/
    return NULL;
  }
}

void JsonTypeRdbSave(RedisModuleIO *rdb, void *value) {}

void JsonTypeAofRewrite(RedisModuleIO *aof, RedisModuleString *key,
                        void *value) {}

void JsonTypeDigest(RedisModuleDigest *digest, void *value) {}

void JsonTypeFree(void *value) {}

// == Module commands ==
/* JSON.SET <key> <path> <json> [SCHEMA <schema-key>]
*/
int JSONSet_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv,
                         int argc) {
  // check args
  if ((argc < 4) || (argc > 6)) return RedisModule_WrongArity(ctx);
  RedisModule_AutoMemory(ctx);

  // key must be empty or a JSON type
  RedisModuleKey *key =
      RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ | REDISMODULE_WRITE);
  int type = RedisModule_KeyType(key);
  if (type != REDISMODULE_KEYTYPE_EMPTY &&
      RedisModule_ModuleTypeGetType(key) != JsonType) {
    return RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
  }

  // path must be valid

  
  // JSON must be valid
  // Subcommand SCHEMA must be an existing JSON schema or the empty string
  // JSON must be validated against schema

  return RedisModule_ReplyWithNull(ctx);

  RedisModule_ReplicateVerbatim(ctx);
  return REDISMODULE_OK;
}

/* JSON.GET key [path]
*/
int JSONGet_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv,
                         int argc) {
  if ((argc < 2) || (argc > 3)) return RedisModule_WrongArity(ctx);

  RedisModule_AutoMemory(ctx);

  return RedisModule_ReplyWithNull(ctx);

  return REDISMODULE_OK;
}

/* Unit test entry point for the module. */
int TestModule(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
  RedisModule_AutoMemory(ctx);

  RedisModule_ReplyWithSimpleString(ctx, "PASS");
  return REDISMODULE_OK;
}

int RedisModule_OnLoad(RedisModuleCtx *ctx) {
  if (RedisModule_Init(ctx, RLMODULE_NAME, 1, REDISMODULE_APIVER_1) ==
      REDISMODULE_ERR)
    return REDISMODULE_ERR;

  JsonType = RedisModule_CreateDataType(
      ctx, JSONTYPE_NAME, JSONTYPE_ENCODING_VERSION, JsonTypeRdbLoad,
      JsonTypeRdbSave, JsonTypeAofRewrite, JsonTypeDigest, JsonTypeFree);

  // /* Main commands. */
  if (RedisModule_CreateCommand(ctx, "json.set", JSONSet_RedisCommand,
                                "write deny-oom getkeys-api", 0, 0,
                                0) == REDISMODULE_ERR)
    return REDISMODULE_ERR;

  if (RedisModule_CreateCommand(ctx, "json.get", JSONGet_RedisCommand,
                                "readonly", 1, 1, 1) == REDISMODULE_ERR)
    return REDISMODULE_ERR;

  /* Testing. */
  if (RedisModule_CreateCommand(ctx, "json.test", TestModule, "write", 1, 1,
                                1) == REDISMODULE_ERR)
    return REDISMODULE_ERR;

  return REDISMODULE_OK;
}
