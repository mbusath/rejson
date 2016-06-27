#include "json_object.h"
#include "object.h"
#include "jsonsl.h"
#include <float.h>
#include <math.h>
#include "rmalloc.h"
/* Open issues:
- string serialization
- move jsonsl to deps
- move include files to include
*/

/* === Parser === */
typedef enum {
    jo_error_type_ok,
    jo_error_type_jsl,
    jo_error_type_obj,
    jo_error_type_jo
} jo_error_type_t;

/* A custom context for the JSON lexer. */
typedef struct {
    jo_error_type_t error_type;
    union {
        jsonsl_error_t jsl;
        int obj;
        int jo;
    } error;
    Node **nodes;
    int len;
} json_object_context_t;

/* Decalre it. */
static int is_allowed_whitespace(unsigned c);

void push_callback(jsonsl_t jsn, jsonsl_action_t action, struct jsonsl_state_st *state,
                   const jsonsl_char_t *at) {
    json_object_context_t *joctx = (json_object_context_t *)jsn->data;

    switch (state->type) {
        case JSONSL_T_OBJECT:
            if (!(joctx->nodes[joctx->len] = NewDictNode(1))) goto alloc_error;
            joctx->len++;
            break;
        case JSONSL_T_LIST:
            if (!(joctx->nodes[joctx->len] = NewArrayNode(1))) goto alloc_error;
            joctx->len++;
            break;
    }
    return;

alloc_error:
    joctx->error_type = jo_error_type_jo;
    joctx->error.jo = JSONOBJECT_ERROR_ALLOC;
    jsonsl_stop(jsn);
    return;
}

void pop_callback(jsonsl_t jsn, jsonsl_action_t action, struct jsonsl_state_st *state,
                  const jsonsl_char_t *at) {
    json_object_context_t *joctx = (json_object_context_t *)jsn->data;

    Node *n = NULL;

    /* Hashkeys as well as numeric and Boolean literals create nodes. */
    if (JSONSL_T_STRING == state->type) {
        n = NewStringNode(jsn->base + state->pos_begin + 1, state->pos_cur - state->pos_begin - 1);
        if (!n) goto alloc_error;
        joctx->nodes[joctx->len++] = n;
    } else if (JSONSL_T_SPECIAL == state->type) {
        if (state->special_flags & JSONSL_SPECIALf_NUMERIC) {
            if (state->special_flags & JSONSL_SPECIALf_FLOAT) {
                n = NewDoubleNode(atof(jsn->base + state->pos_begin));
                if (!n) goto alloc_error;
                joctx->nodes[joctx->len++] = n;
            } else {
                n = NewIntNode(atoi(jsn->base + state->pos_begin));
                if (!n) goto alloc_error;
                joctx->nodes[joctx->len++] = n;
            }
        } else if (state->special_flags & JSONSL_SPECIALf_BOOLEAN) {
            n = NewBoolNode(state->special_flags & JSONSL_SPECIALf_TRUE);
            if (!n) goto alloc_error;
            joctx->nodes[joctx->len++] = n;
        } else if (state->special_flags & JSONSL_SPECIALf_NULL) {
            joctx->nodes[joctx->len++] = NULL;
        }
    } else if (JSONSL_T_HKEY == state->type) {
        n = NewKeyValNode(jsn->base + state->pos_begin + 1, state->pos_cur - state->pos_begin - 1,
                          NULL);
        if (!n) goto alloc_error;
        joctx->nodes[joctx->len++] = n;
    }

    /* jsonsl's hashkeys are only temporarily put in nodes. */
    if (state->type != JSONSL_T_HKEY) {
        if (joctx->len > 1) {
            if (joctx->nodes[joctx->len - 2]->type == N_KEYVAL) {
                if (OBJ_ERR == Node_DictSet(joctx->nodes[joctx->len - 3],
                                            joctx->nodes[joctx->len - 2]->value.kvval.key,
                                            joctx->nodes[joctx->len - 1]))
                    goto alloc_error;
                Node_Free(joctx->nodes[joctx->len - 2]);
                joctx->nodes[joctx->len - 2] = joctx->nodes[joctx->len - 1];
                joctx->len -= 2;
            } else if (joctx->nodes[joctx->len - 2]->type == N_ARRAY) {
                if (OBJ_ERR ==
                    Node_ArrayAppend(joctx->nodes[joctx->len - 2], joctx->nodes[joctx->len - 1]))
                    goto alloc_error;
                joctx->len -= 1;
            }
        }
    }
    return;

alloc_error:
    joctx->error_type = jo_error_type_jo;
    joctx->error.jo = JSONOBJECT_ERROR_ALLOC;
    jsonsl_stop(jsn);
    return;
}

int error_callback(jsonsl_t jsn, jsonsl_error_t err, struct jsonsl_state_st *state, char *errat) {
    json_object_context_t *joctx = (json_object_context_t *)jsn->data;

    joctx->error_type = jo_error_type_jsl;
    joctx->error.jsl = err;
    jsonsl_stop(jsn);
    return 0;
}

int CreateNodeFromJSON(const char *buf, size_t len, Node **node, char **err) {
    int levels = JSONSL_MAX_LEVELS;  // TODO: heur levels from len since we're not really streaming?

    size_t _off = 0, _len = len;
    char *_buf = (char *)buf;
    int is_literal = 0;

    // munch any leading whitespaces
    while (is_allowed_whitespace(_buf[_off]) && _off < _len) _off++;

    // embed literals in a list (also avoids JSONSL_ERROR_STRING_OUTSIDE_CONTAINER)
    if ((is_literal = ('{' != _buf[_off]) && ('[' != _buf[_off]))) {
        _len = _len - _off + 2;
        _buf = malloc(_len * sizeof(char));
        if (!_buf) goto error;
        _buf[0] = '[';
        _buf[_len - 1] = ']';
        memcpy(&_buf[1], &buf[_off], len - _off);
    }

    /* The lexer. */
    jsonsl_t jsn = jsonsl_new(levels);
    if (!jsn) goto error;
    jsn->error_callback = error_callback;
    jsn->action_callback_POP = pop_callback;
    jsn->action_callback_PUSH = push_callback;
    jsonsl_enable_all_callbacks(jsn);

    /* Set up our custom json_object context. */
    json_object_context_t *joctx = calloc(1, sizeof(json_object_context_t));
    if (!joctx) goto error;
    joctx->nodes = calloc(levels, sizeof(Node *));
    if (!joctx->nodes) goto error;
    joctx->error_type = jo_error_type_ok;
    jsn->data = joctx;

    /* Feed the lexer. */
    jsonsl_feed(jsn, _buf, _len);

    /* Finalize. */
    if (jo_error_type_ok == joctx->error_type) {
        /* Extract literals from the list. */
        if (is_literal) {
            Node_ArrayItem(joctx->nodes[0], 0, node);
            Node_ArraySet(joctx->nodes[0], 0, NULL);
            Node_Free(joctx->nodes[0]);
            free(_buf);
        } else
            *node = joctx->nodes[0];
    } else {
        goto error;
    }

    free(joctx->nodes);
    free(joctx);
    jsonsl_destroy(jsn);

    return JSONOBJECT_OK;

error:
    if ((_len > len) && (_buf)) free(_buf);
    if (jsn) jsonsl_destroy(jsn);
    if (joctx) {
        if (joctx->nodes) {
            if (joctx->len) {
                for (int i = joctx->len - 1; i <= 0; i++)
                    /* Null check is needed for special null nodes. */
                    if (joctx->nodes[i]) Node_Free(joctx->nodes[i]);
            }
            free(joctx->nodes);
        }

        if (err) {
            if (jo_error_type_jsl == joctx->error_type) {
                sprintf(*err, "ERR JSON lexer error: %s", jsonsl_strerror(joctx->error.jsl));
            } else if (jo_error_type_obj == joctx->error_type) {
                sprintf(*err, "ERR Object error: %d", joctx->error.obj);
            } else {
                sprintf(*err, "ERR JSONObject object error: %d", joctx->error.jo);
            }
        }

        free(joctx);
    }
    *node = NULL;
    return JSONOBJECT_ERROR;
}

/* === Serializer === */
typedef struct {
    size_t len;        // buffer length
    size_t cap;        // buffer capacity
    char *buf;         // buffer
    int depth;         // current depth
    int prettify;      // minify or prettify
    int inkey;         // currently printing a hash key
    char *indentstr;   // indentation string
    size_t indentlen;  // length of indent string
    char *breakstr;    // linebreak string
    size_t breaklen;   // linebreak of indent string
} json_builder_t;

/* Grows the builder's buffer if needed. */
static char *ensure(json_builder_t *b, int needed) {
    needed += b->len;
    if (needed <= b->cap) return b->buf + b->len;

    if (!b->cap) {  // 64 bytes should be ok for most literals
        b->cap = 1 << 6;
    } else if (b->cap < (1 << 16)) {  // otherwise grow by powers of 2 until 65K
        b->cap = 2 * b->cap;
    } else {  // then by 65K
        b->cap = b->cap + (1 << 16);
    }

    if (!b->buf)
        b->buf = calloc(b->cap, sizeof(char));
    else
        b->buf = realloc(b->buf, b->cap * sizeof(char));
    if (!b->buf) {
        b->cap = 0;
        b->len = 0;
        return NULL;
    }

    return b->buf + b->len;
}

/* Returns the buffer's current length. */
static int update(json_builder_t *b) {
    char *str;
    str = b->buf + b->len;
    return b->len + strlen(str);
}

static char *indent(json_builder_t *b) {
    char *str = 0;

    if (b->prettify) {
        str = ensure(b, b->indentlen * b->depth + 1);
        if (str) {
            for (int i = 0; i < b->depth; i++) {
                sprintf(str, "%s", b->indentstr);
                str = str + b->indentlen;
            }
        }
        b->len = update(b);
    } else {
        str = ensure(b, 0);
    }
    return str;
}

/* Decalre it. */
static char *serialize_Node(Node *n, json_builder_t *b);

static char *serialize_Dict(Node *n, json_builder_t *b) {
    char *str = 0;

    str = ensure(b, b->prettify * b->breaklen + 2);
    if (str) sprintf(str, "%s%s", "{", b->breakstr);
    b->len = update(b);
    b->depth++;

    int len = n->value.dictval.len;
    if (len) {
        str = indent(b);
        str = serialize_Node(n->value.dictval.entries[0], b);
        if (str && len > 1) {
            for (int i = 1; i < len; i++) {
                str = ensure(b, b->prettify * b->breaklen + 2);
                if (str) sprintf(str, ",%s", b->breakstr);
                b->len = update(b);
                str = indent(b);
                str = serialize_Node(n->value.dictval.entries[i], b);
                b->len = update(b);
            }
        }
        str = ensure(b, b->prettify * b->breaklen + 2);
        if (str) sprintf(str, "%s", b->breakstr);
    }

    b->depth--;
    b->len = update(b);
    str = indent(b);
    str = ensure(b, 2);
    if (str) sprintf(str, "}");

    return str;
}

static char *serialize_Array(Node *n, json_builder_t *b) {
    char *str = 0;

    str = ensure(b, b->prettify * b->breaklen + 2);
    if (str) sprintf(str, "%s%s", "[", b->breakstr);
    b->len = update(b);
    b->depth++;

    int len = n->value.arrval.len;
    if (len) {
        str = indent(b);
        str = serialize_Node(n->value.arrval.entries[0], b);
        if (str && len > 1) {
            for (int i = 1; i < len; i++) {
                str = ensure(b, b->prettify * b->breaklen + 2);
                if (str) sprintf(str, ",%s", b->breakstr);
                b->len = update(b);
                str = indent(b);
                str = serialize_Node(n->value.dictval.entries[i], b);
                b->len = update(b);
            }
        }
        str = ensure(b, b->prettify * b->breaklen + 2);
        if (str) sprintf(str, "%s", b->breakstr);
    }

    b->depth--;
    b->len = update(b);
    str = indent(b);
    str = ensure(b, 2);
    if (str) sprintf(str, "]");

    return str;
}

static char *serialize_String(Node *n, json_builder_t *b) {
    char *str = 0;

    // TODO: escapes and shit!
    str = ensure(b, n->value.strval.len + 3);
    if (str) sprintf(str, "\"%.*s\"", n->value.strval.len, n->value.strval.data);

    return str;
}

static char *serialize_Node(Node *n, json_builder_t *b) {
    char *str = 0;

    if (!n) {
        str = ensure(b, 5);
        if (str) sprintf(str, "null");
    } else {
        switch (n->type) {
            case N_BOOLEAN:
                if (n->value.boolval) {
                    str = ensure(b, 5);
                    if (str) sprintf(str, "true");
                } else {
                    str = ensure(b, 6);
                    if (str) sprintf(str, "false");
                }
                break;
            case N_INTEGER:
                str = ensure(b, 21);  // 19 charachter, sign and null terminator
                if (str) sprintf(str, "%ld", n->value.intval);
                break;
            case N_NUMBER:
                str = ensure(b, 64);
                if (str) {
                    if (fabs(floor(n->value.numval) - n->value.numval) <= DBL_EPSILON &&
                        fabs(n->value.numval) < 1.0e60)
                        sprintf(str, "%.0f", n->value.numval);
                    else if (fabs(n->value.numval) < 1.0e-6 || fabs(n->value.numval) > 1.0e9)
                        sprintf(str, "%e", n->value.numval);
                    else
                        sprintf(str, "%f", n->value.numval);
                }
                break;
            case N_STRING:
                str = serialize_String(n, b);
                break;
            case N_KEYVAL:
                str = ensure(b, strlen(n->value.kvval.key) + b->prettify + 4);
                if (str) sprintf(str, "\"%s\":%s", n->value.kvval.key, b->prettify ? " " : "");
                b->len = update(b);
                b->inkey = 1;
                str = serialize_Node(n->value.kvval.val, b);
                b->inkey = 0;
                break;
            case N_DICT:
                if (!b->inkey) str = indent(b);
                b->inkey = 0;
                str = serialize_Dict(n, b);
                break;
            case N_ARRAY:
                if (!b->inkey) str = indent(b);
                b->inkey = 0;
                str = serialize_Array(n, b);
                break;
        }  // switch(n->type)
    }
    b->len = update(b);
    return str;
}

int SerializeNodeToJSON(const Node *node, int prettify, char **json) {
    json_builder_t b = {0};
    b.prettify = prettify;
    if (b.prettify) {
        b.indentstr = "  ";
        b.breakstr = "\n";
    } else {
        b.indentstr = "";
        b.breakstr = "";
    }
    b.breaklen = strlen(b.breakstr);
    b.indentlen = strlen(b.indentstr);

    serialize_Node((Node *)node, &b);
    if (!b.len) return JSONOBJECT_ERROR;

    *json = b.buf;
    return JSONOBJECT_OK;
}

// from jsonsl.c
/**
 * This table contains entries for the allowed whitespace as per RFC 4627
 */
static int Allowed_Whitespace[0x100] = {
    /* 0x00 */ 0,             0, 0, 0, 0, 0, 0, 0, 0,                            /* 0x08 */
    /* 0x09 */ 1 /* <TAB> */,                                                    /* 0x09 */
    /* 0x0a */ 1 /* <LF> */,                                                     /* 0x0a */
    /* 0x0b */ 0,             0,                                                 /* 0x0c */
    /* 0x0d */ 1 /* <CR> */,                                                     /* 0x0d */
    /* 0x0e */ 0,             0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x1f */
    /* 0x20 */ 1 /* <SP> */,                                                     /* 0x20 */
    /* 0x21 */ 0,             0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x40 */
    /* 0x41 */ 0,             0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x60 */
    /* 0x61 */ 0,             0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0x80 */
    /* 0x81 */ 0,             0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0xa0 */
    /* 0xa1 */ 0,             0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0xc0 */
    /* 0xc1 */ 0,             0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 0xe0 */
    /* 0xe1 */ 0,             0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,                        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 /* 0xfe */
};

static int is_allowed_whitespace(unsigned c) { return c == ' ' || Allowed_Whitespace[c & 0xff]; }
