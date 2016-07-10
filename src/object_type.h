#ifndef __OBJECT_TYPE_H__
#define __OBJECT_TYPE_H__

#include "redismodule.h"

void *ObjectTypeRdbLoad(RedisModuleIO *rdb);
void ObjectTypeRdbSave(RedisModuleIO *rdb, void *value);
void ObjectTypeDigest(RedisModuleDigest *digest, void *value);
void ObjectTypeFree(void *value);

#endif