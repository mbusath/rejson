#ifndef __OBJECT_H__
#define __OBJECT_H__

#include <stdlib.h>

// Return code from successful ops
#define OBJ_OK 0
// Return code from failed ops
#define OBJ_ERR 1

/**
* NodeType represents the type of a node in an object.
*/
typedef enum {
    N_NULL = 0x1,       // only used in masks
    N_STRING = 0x2,
    N_NUMBER = 0x4,
    N_INTEGER = 0x8,
    N_BOOLEAN = 0x10,
    N_DICT = 0x20,
    N_ARRAY = 0x40,
    N_KEYVAL = 0x80
    // N_DATETIME = 0x100
    // N_BINARY = 0x200
} NodeType;

struct t_node;

/*
* Internal representation of a string with data and length
*/
typedef struct {
    const char *data;
    u_int32_t len;
} t_string;

/*
* Internal representation of an array, that has a length and capacity
*/
typedef struct {
    struct t_node **entries;
    u_int32_t len;
    u_int32_t cap;
} t_array;

/*
* Internal representation of a key-value pair in an object.
* The key is a NULL terminated C-string, the value is another node
*/
typedef struct {
    const char *key;
    struct t_node *val;
} t_keyval;

/*
* Internal representation of a dictionary node.
* Currently implemented as a list of key-value pairs, will be converted
* to a hash-table on big objects in the future
*/
typedef struct {
    struct t_node **entries;
    u_int32_t len;
    u_int32_t cap;
} t_dict;

/*
* A node in an object can be any one of the types we support.
* Basically an object is just a treee of nodes that can have children
* if they are of type dict or array.
*/
typedef struct t_node {
    // the actual value of the node
    union {
        int boolval;
        double numval;
        int64_t intval;
        t_string strval;
        t_array arrval;
        t_dict dictval;
        t_keyval kvval;
    } value;

    // type specifier
    NodeType type;
} Node;

typedef Node Object;

/** Create a new boolean node, with 0 as false 1 as true */
Node *NewBoolNode(int val);

/** Create a new double node with the given value */
Node *NewDoubleNode(double val);

/** Create a new integer node with the given value */
Node *NewIntNode(int64_t val);

/**
* Create a new string node with the given c-string and its length.
* NOTE: The string's value will be copied to a newly allocated string
*/
Node *NewStringNode(const char *s, u_int32_t len);

/**
* Create a new string node from a NULL terminated c-string. #ifdef 0
* NOTE: The string's value will be copied to a newly allocated string
*/
Node *NewCStringNode(const char *su);

/**
* Create a new keyval node from a C-string and its length as key and a pointer
* to a Node as value.
* NOTE: The string's value will be copied to a newly allocated string
*/
Node *NewKeyValNode(const char *key, u_int32_t len, Node *n);

/** Create a new zero length array node with the given capacity */
Node *NewArrayNode(u_int32_t cap);

/** Create a new dict node with the given capacity */
Node *NewDictNode(u_int32_t cap);

/** Free a node, and if needed free its allocated data and its children recursively */
void Node_Free(Node *n);

/** Pretty-pring a node. Not JSON compliant but will produce something almost JSON-ish */
void Node_Print(Node *n, int depth);

/** Append a node to an array node. If needed the array's internal list of children will be resized
 */
int Node_ArrayAppend(Node *arr, Node *n);

/**
* Set an array's member at a given index to a new node.
* If the index is out of range, we will return an error
*/
int Node_ArraySet(Node *arr, int index, Node *n);

/**
* Retrieve an array item into Node n's pointer by index
* Returns OBJ_ERR if the index is outof range
*/
int Node_ArrayItem(Node *arr, int index, Node **n);

/**
* Set an item in a dictionary for a given key.
* If an existing item is at the key, we replace it and free the old value
*/
int Node_DictSet(Node *obj, const char *key, Node *n);

/**
* Set a keyval node in a dictionary.
* If an existing node has the same key, we replace it and free the old keyval node
*/
int Node_DictSetKeyVal(Node *obj, Node *kv);

/**
* Delete an item from the dict node by key. Returns OBJ_ERR if the key was
* not found
*/
int Node_DictDel(Node *objm, const char *key);

/**
* Get a dict node item by key, and put it Node val's pointer.
* Return OBJ_ERR if the key was not found. Can put NULL into val
* if it is a NULL node
*/
int Node_DictGet(Node *obj, const char *key, Node **val);

/* The type signature of visitor callbacks for node trees */
typedef void (*NodeVisitor)(Node *, void *);
void __objTraverse(Node *n, NodeVisitor f, void *ctx);
void __arrTraverse(Node *n, NodeVisitor f, void *ctx);

/**
* Traverse a node recursively with a visitor callback.
* We will pass the provided ctx to the callback
*/
void Node_Traverse(Node *n, NodeVisitor f, void *ctx);

/* The type signature of serializer callbacks for node trees */
typedef void (*NodeSerializerValue)(Node *, void *);
typedef void (*NodeSerializerContainer)(void *);

/* The options container for the serializer */
typedef struct {
    NodeSerializerValue fBegin;      // begin node serializer callback
    NodeSerializerValue fEnd;        // end node serializer callback
    NodeSerializerContainer fDelim;  // container node delimiter callback
    int xBegin, xEnd, xDelim;        // node type bitmasks 
} NodeSerializerOpt;

/**
* Scan a node without recursion but with callbacks.
* We will pass the provided ctx to the callback
*/
void Node_Serializer(const Node *n, const NodeSerializerOpt *o, void *ctx);

#endif