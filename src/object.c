#include "object.h"
#include <sys/param.h>
#include <string.h>
#include <stdio.h>
#include "../deps/rmutil/vector.h"
#include "rmalloc.h"

Node *__newNode(NodeType t) {
    Node *ret = malloc(sizeof(Node));
    ret->type = t;
    return ret;
}

Node *NewBoolNode(int val) {
    Node *ret = __newNode(N_BOOLEAN);
    ret->value.boolval = val != 0;
    return ret;
}

Node *NewDoubleNode(double val) {
    Node *ret = __newNode(N_NUMBER);
    ret->value.numval = val;
    return ret;
}

Node *NewIntNode(int64_t val) {
    Node *ret = __newNode(N_INTEGER);
    ret->value.intval = val;
    return ret;
}

Node *NewStringNode(const char *s, u_int32_t len) {
    Node *ret = __newNode(N_STRING);
    ret->value.strval.data = strndup(s, len);
    ret->value.strval.len = len;
    return ret;
}

Node *NewCStringNode(const char *s) { return NewStringNode(s, strlen(s)); }

Node *NewKeyValNode(const char *key, u_int32_t len, Node *n) {
    Node *ret = __newNode(N_KEYVAL);
    ret->value.kvval.key = strndup(key, len);
    ret->value.kvval.val = n;
    return ret;
}

Node *NewArrayNode(u_int32_t cap) {
    Node *ret = __newNode(N_ARRAY);
    ret->value.arrval.cap = cap;
    ret->value.arrval.len = 0;
    ret->value.arrval.entries = calloc(cap, sizeof(Node *));
    return ret;
}

Node *NewDictNode(u_int32_t cap) {
    Node *ret = __newNode(N_DICT);
    ret->value.dictval.cap = cap;
    ret->value.dictval.len = 0;
    ret->value.dictval.entries = calloc(cap, sizeof(Node *));
    return ret;
}

void __node_FreeKV(Node *n) {
    Node_Free(n->value.kvval.val);
    free((char *)n->value.kvval.key);
    free(n);
}

void __node_FreeObj(Node *n) {
    for (int i = 0; i < n->value.dictval.len; i++) {
        Node_Free(n->value.dictval.entries[i]);
    }
    if (n->value.dictval.entries) free(n->value.dictval.entries);
    free(n);
}

void __node_FreeArr(Node *n) {
    for (int i = 0; i < n->value.arrval.len; i++) {
        Node_Free(n->value.arrval.entries[i]);
    }
    free(n->value.arrval.entries);
    free(n);
}
void __node_FreeString(Node *n) {
    free((char *)n->value.strval.data);
    free(n);
}

void Node_Free(Node *n) {
    // ignore NULL nodes
    if (!n) return;

    switch (n->type) {
        case N_ARRAY:
            __node_FreeArr(n);
            break;
        case N_DICT:
            __node_FreeObj(n);
            break;
        case N_STRING:
            __node_FreeString(n);
            break;
        case N_KEYVAL:
            __node_FreeKV(n);
            break;
        default:
            free(n);
    }
}

int Node_ArrayAppend(Node *arr, Node *n) {
    t_array *a = &arr->value.arrval;
    if (a->len >= a->cap) {
        a->cap = a->cap ? MIN(a->cap * 2, 1024 * 1024) : 1;
        a->entries = realloc(a->entries, a->cap * sizeof(Node *));
    }
    a->entries[a->len++] = n;
    return OBJ_OK;
}

int Node_ArraySet(Node *arr, int index, Node *n) {
    t_array *a = &arr->value.arrval;

    // invalid index!
    if (index < 0 || index >= a->len) {
        return OBJ_ERR;
    }
    a->entries[index] = n;

    return OBJ_OK;
}

int Node_ArrayItem(Node *arr, int index, Node **n) {
    t_array *a = &arr->value.arrval;

    // invalid index!
    if (index < 0 || index >= a->len) {
        *n = NULL;
        return OBJ_ERR;
    }
    *n = a->entries[index];
    return OBJ_OK;
}

Node *__obj_find(t_dict *o, const char *key, int *idx) {
    for (int i = 0; i < o->len; i++) {
        if (!strcmp(key, o->entries[i]->value.kvval.key)) {
            if (idx) *idx = i;

            return o->entries[i];
        }
    }

    return NULL;
}

#define __obj_insert(o, n)                                             \
    if (o->len >= o->cap) {                                            \
        o->cap = o->cap ? MIN(o->cap * 2, 1024 * 1024) : 1;            \
        o->entries = realloc(o->entries, o->cap * sizeof(t_keyval *)); \
    }                                                                  \
    o->entries[o->len++] = n;

int Node_DictSet(Node *obj, const char *key, Node *n) {
    t_dict *o = &obj->value.dictval;

    if (key == NULL) return OBJ_ERR;

    int idx;
    Node *kv = __obj_find(o, key, &idx);
    // first find a replacement possiblity
    if (kv) {
        if (kv->value.kvval.val) {
            Node_Free(kv->value.kvval.val);
        }
        kv->value.kvval.val = n;
        return OBJ_OK;
    }

    // append another entry
    __obj_insert(o, NewKeyValNode(key, strlen(key), n));

    return OBJ_OK;
}

int Node_DictSetKeyVal(Node *obj, Node *kv) {
    t_dict *o = &obj->value.dictval;

    if (kv->value.kvval.key == NULL) return OBJ_ERR;

    int idx;
    Node *_kv = __obj_find(o, kv->value.kvval.key, &idx);
    // first find a replacement possiblity
    if (_kv) {
        o->entries[idx] = kv;
        Node_Free(_kv);
        return OBJ_OK;
    }

    // append another entry
    __obj_insert(o, kv);

    return OBJ_OK;
}

int Node_DictDel(Node *obj, const char *key) {
    if (key == NULL) return OBJ_ERR;

    t_dict *o = &obj->value.dictval;

    int idx = -1;
    Node *kv = __obj_find(o, key, &idx);

    // tried to delete a non existing node
    if (!kv) return OBJ_ERR;

    // let's delete the node's memory
    if (kv->value.kvval.val) {
        Node_Free(kv->value.kvval.val);
    }
    free((char *)kv->value.kvval.key);

    // replace the deleted entry and the top entry to avoid holes
    if (idx < o->len - 1) {
        o->entries[idx] = o->entries[o->len - 1];
    }
    o->len--;

    return OBJ_OK;
}

int Node_DictGet(Node *obj, const char *key, Node **val) {
    if (key == NULL) return OBJ_ERR;

    t_dict *o = &obj->value.dictval;

    int idx = -1;
    Node *kv = __obj_find(o, key, &idx);

    // not found!
    if (!kv) return OBJ_ERR;

    *val = kv->value.kvval.val;
    return OBJ_OK;
}

void __objTraverse(Node *n, NodeVisitor f, void *ctx) {
    t_dict *o = &n->value.dictval;

    f(n, ctx);
    for (int i = 0; i < o->len; i++) {
        Node_Traverse(o->entries[i], f, ctx);
    }
}
void __arrTraverse(Node *n, NodeVisitor f, void *ctx) {
    t_array *a = &n->value.arrval;
    f(n, ctx);

    for (int i = 0; i < a->len; i++) {
        Node_Traverse(a->entries[i], f, ctx);
    }
}

void Node_Traverse(Node *n, NodeVisitor f, void *ctx) {
    // for null node - just call the callback
    if (!n) {
        f(n, ctx);
        return;
    }
    switch (n->type) {
        case N_ARRAY:
            __arrTraverse(n, f, ctx);
            break;
        case N_DICT:
            __objTraverse(n, f, ctx);
            break;
        // for all other types - just call the callback
        default:
            f(n, ctx);
    }
}

#define __node_indent(depth)          \
    for (int i = 0; i < depth; i++) { \
        printf("  ");                 \
    }

/** Pretty print a JSON-like (but not compatible!) version of a node */
void Node_Print(Node *n, int depth) {
    if (n == NULL) {
        printf("null");
        return;
    }
    switch (n->type) {
        case N_ARRAY: {
            printf("[\n");
            for (int i = 0; i < n->value.arrval.len; i++) {
                __node_indent(depth + 1);
                Node_Print(n->value.arrval.entries[i], depth + 1);
                if (i < n->value.arrval.len - 1) printf(",");
                printf("\n");
            }
            __node_indent(depth);
            printf("]");
        } break;

        case N_DICT: {
            printf("{\n");
            for (int i = 0; i < n->value.dictval.len; i++) {
                __node_indent(depth + 1);
                Node_Print(n->value.dictval.entries[i], depth + 1);
                if (i < n->value.dictval.len - 1) printf(",");
                printf("\n");
            }
            __node_indent(depth);
            printf("}");
        } break;
        case N_BOOLEAN:
            printf("%s", n->value.boolval ? "true" : "false");
            break;
        case N_NUMBER:
            printf("%f", n->value.numval);
            break;
        case N_INTEGER:
            printf("%ld", n->value.intval);
            break;
        case N_KEYVAL: {
            printf("\"%s\": ", n->value.kvval.key);
            Node_Print(n->value.kvval.val, depth);
        } break;
        case N_STRING:
            printf("\"%.*s\"", n->value.strval.len, n->value.strval.data);
    }
}

// serializer stack
typedef struct {
    int level;      // current level
    int pos;        // 0-based level
    Vector *nodes;
    Vector *indices;

} t_serializer_stack;

// Pops a vector's last element (no resizing though)
// TODO: maybe move to vector.h?
void *Vector_Pop(Vector *v) {
    void *ret = NULL;
    if (v->top) {
        Vector_Get(v, v->top - 1, &ret);
        v->top--;
    }
    return ret;
}

// serializer stack push
void _serializerPush(t_serializer_stack *s, const Node *n) {
    s->level++;
    Vector_Push(s->nodes, n);
    Vector_Push(s->indices, 0);
}

// serializer stack push
void _serializerPop(t_serializer_stack *s) {
    s->level--;
    Vector_Pop(s->nodes);
    Vector_Pop(s->indices);
}

void Node_Serializer(const Node *n, const NodeSerializerOpt *o, void *ctx) {
    Node *curr_node;
    int curr_len;
    int curr_index;
    Node **curr_entries;

    t_serializer_stack s = {0};
    s.nodes = NewVector(Node *, 0);
    s.indices = NewVector(int, 0);

    _serializerPush(&s, n);
    
NODE_BEGIN:
    Vector_Get(s.nodes, s.level - 1, &curr_node);
    o->fBegin(curr_node, ctx);
    // NULL nodes need special care
    if (!curr_node) goto NODE_END;

NODE_CONTINUE:
    // Only containers require further fondling, the rest fall through
    if (N_DICT == curr_node->type) {
        curr_len = curr_node->value.dictval.len;
        curr_entries = curr_node->value.dictval.entries;
        goto CONTAINER;
    } else if (N_ARRAY == curr_node->type) {
        curr_len = curr_node->value.arrval.len;
        curr_entries = curr_node->value.arrval.entries;
        goto CONTAINER;
    } else if (N_KEYVAL == curr_node->type) {
        curr_len = 1;
        curr_entries = &curr_node->value.kvval.val;
        goto CONTAINER;
    }
    // Others go to the end
    goto NODE_END;

CONTAINER:
    Vector_Get(s.indices, s.level - 1, &curr_index);
    if (curr_index < curr_len) {
        if (curr_index) o->fDelim(ctx);
        Vector_Put(s.indices, s.level - 1, curr_index + 1);
        _serializerPush(&s, curr_entries[curr_index]);
        goto NODE_BEGIN;
    }

NODE_END:
    o->fEnd(curr_node, ctx);
    _serializerPop(&s);
    if (s.level) {
        Vector_Get(s.nodes, s.level - 1, &curr_node);
        goto NODE_CONTINUE;
    }

SERIALIZER_END:
    Vector_Free(s.nodes);
    Vector_Free(s.indices);
}