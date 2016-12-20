# ReJSON Module Design

## Abstract

The purpose of this module is to provide native support for JSON documents stored in redis, allowing users to:

1. Store a JSON blob.
2. Manipulate just a part of the json object without retrieving it to the client.
3. Retrieve just a portion of the object as JSON.
4. Store JSON SChema objects and validate JSON objects based on schema keys.
4. Index objects in secondary indexes based on their properties.

Later on we can use the inernal object implementation in this module to produce similar modules for other serialization formats,
namely XML and BSON.

## Design Considerations

* Documents are added as JSON but are stored in an internal representation and not as strings.
* Internal representation does not depend on any JSON parser or library, to allow connecting other formats to it later.
* The internal representation will initially be limited to the types supported by JSON, but can later be extended to types like timestamps, etc.
* Queries that include internal paths of objects will be expressed in JSON path expressionse (e.g. `foo.bar[3].baz`)
* We will not implement our own JSON parser and composer, but use existing libraries.
* The code apart from the implementation of the redis commands will not depend on redis and will be testable without being compiled as a module.

## Limitations and known issues

* AOF rewrite will fail for documents with serialization over 0.5GB?

### Perhaps

JSON.COUNT <key> <path> <json-scalar>
P: count JS: ? R: N/A
Counts the number of occurances for scalar in the array

JSON.REMOVE <key> <path> <json-scalar> [count]
P: builtin del JS: ? R: LREM (but also has a count and direction) 
Removes the first `count` occurances (default 1) of value from array. If index is negative, traversal is reversed.

JSON.POP <key> <path>
P: pop JS ? R: RPOP (but w/o index, only count)
Pops the last element from an array or the given index if any. Can be done with get and del but this is nicer.

JSON.EXISTS <key> <path>
P: in JS: ? R: HEXISTS/LINDEX
Checks if path key or array index exists. Syntactic sugar for JSON.TYPE.

JSON.REVERSE <key> <path>
P: reverse JS: ? R: N/A
Reverses the array. Nice to have.

JSON.SORT <key> <path>
P: sort JS: ? R: SORT
Sorts the values in an array. Nice to have.

JSON.TORESP <json>
Converts a JSON value to its RESP equivalent

JSON.FROMRESP <redis-command> [STORE <key>]
Returns the command's reply as a JSON value

## Future

### Infrastructure

* Array optimization - double ended?
* Dict optimization - hash table/trie
* Use an internal serialization cache to serve MFU objects
* Compress (string?) values over xKB

### API considerations

* Creation is implicit, but add NX|XX flags and command for SET?

### Schema

JSON.SETSCHEMA <key> <json> 
  Notes:
    1. not sure if needed, we can add a modifier on the generic SET
    2. indexing will be specified in the schema

JSON.VALIDATE <schema_key> <json>

### Expiry

JSON.EXPIRE <key> <path> <ttl>    

## Object Data Type

The internal representation of JSON objects will be stored in a redis data type called Object [TBD].

These will be optimized for memory efficiency and path search speed. 

See [src/object.h](src/object.h) for the API specification.

## QueryPath 

When updating, reading and deleting parts of json objects, we'll use path specifiers. 

These too will have internal representation disconnected from their JSON path representation. 

## JSONPath syntax compatability

We only support a limited subset of it. Furthermore, jsonsl's jpr implementation may be worth looking into.

| JSONPath         | rejson      | Description |
| ---------------- | ----------- | ----------------------------------------------------------------- |
| `$`              | key name    | the root element                                                  |
| `*`              | N/A #1      | wildcard, can be used instead of name or index                    |
| `..`             | N/A #2      | recursive descent a.k.a deep scan, can be used instead of name    |
| `.` or `[]`      | `.` or `[]` | child operator                                                    |
| `[]`             | `[]`        | subscript operator                                                |
| `[,]`            | N/A #3      | Union operator. Allows alternate names or array indices as a set. |
| `@`              | N/A #4      | the current element being proccessed by a filter predicate        |
| [start:end:step] | N/A #3      | array slice operator                                              |
| ?()              | N/A #4      | applies a filter (script) expression                              |
| ()               | N/A #4      | script expression, using the underlying script engine             |

ref: http://goessner.net/articles/JsonPath/

1.  Wildcard should be added, but mainly useful for filters
1.  Deep scan should be added
1.  Union and slice operators should be added to ARR*, GET, MGET, DEL...
1.  Filtering and scripting (min,max,...) should wait until some indexing is supported

## Secondary Indexes

## Connecting a JSON parser / writer

## Conneting Other Parsers 
