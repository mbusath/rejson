#include "json_object.h"
#include "object.h"
#include "jsonsl.h"
#include <float.h>
#include <math.h>
#include "../deps/rmutil/sds.h"
#include "rmalloc.h"
/* Open issues:
- string serialization
- move jsonsl to deps
- move include files to include
*/

/* === Parser === */
/* A custom context for the JSON lexer. */
typedef struct {
    jsonsl_error_t err;  // lexer error
    size_t errpos;       // error position
    Node **nodes;        // stack of created nodes
    int nlen;            // size of node stack
} JsonObjectContext;

#define _pushNode(ctx, n) ctx->nodes[ctx->nlen++] = n
#define _popNode(ctx) ctx->nodes[--ctx->nlen]

/* Decalre it. */
static int IsAllowedWhitespace(unsigned c);

void pushCallback(jsonsl_t jsn, jsonsl_action_t action, struct jsonsl_state_st *state,
                  const jsonsl_char_t *at) {
    JsonObjectContext *joctx = (JsonObjectContext *)jsn->data;

    // only objects (dictionaries) and lists (arrays) create a container on push
    switch (state->type) {
        case JSONSL_T_OBJECT:
            _pushNode(joctx, NewDictNode(1));
            break;
        case JSONSL_T_LIST:
            _pushNode(joctx, NewArrayNode(1));
            break;
    }
}

void popCallback(jsonsl_t jsn, jsonsl_action_t action, struct jsonsl_state_st *state,
                 const jsonsl_char_t *at) {
    JsonObjectContext *joctx = (JsonObjectContext *)jsn->data;

    // This is a good time to create literals and hashkeys on the stack
    switch (state->type) {
        case JSONSL_T_STRING:
            _pushNode(joctx, NewStringNode(jsn->base + state->pos_begin + 1,
                                           state->pos_cur - state->pos_begin - 1));
            break;
        case JSONSL_T_SPECIAL:
            if (state->special_flags & JSONSL_SPECIALf_NUMERIC) {
                if (state->special_flags & JSONSL_SPECIALf_FLOAT) {
                    _pushNode(joctx, NewDoubleNode(atof(jsn->base + state->pos_begin)));
                } else {
                    _pushNode(joctx, NewIntNode(atoi(jsn->base + state->pos_begin)));
                }
            } else if (state->special_flags & JSONSL_SPECIALf_BOOLEAN) {
                _pushNode(joctx, NewBoolNode(state->special_flags & JSONSL_SPECIALf_TRUE));
            } else if (state->special_flags & JSONSL_SPECIALf_NULL) {
                _pushNode(joctx, NULL);
            }
            break;
        case JSONSL_T_HKEY:
            _pushNode(joctx, NewKeyValNode(jsn->base + state->pos_begin + 1,
                                           state->pos_cur - state->pos_begin - 1, NULL));
            break;
    }

    // Basically anything that pops from the JSON lexer needs to be set in its parent, except
    // 1. The root element (i.e. end of JSON)
    // 2. Hashkeys are postponed until their values are popped (resulting in a double-pop, a.k.a DP)
    if (joctx->nlen > 1 && state->type != JSONSL_T_HKEY) {
        NodeType p = joctx->nodes[joctx->nlen - 2]->type;
        switch (p) {
            case N_DICT:
                Node_DictSetKeyVal(joctx->nodes[joctx->nlen - 1], _popNode(joctx));
                break;
            case N_ARRAY:
                Node_ArrayAppend(joctx->nodes[joctx->nlen - 1], _popNode(joctx));
                break;
            case N_KEYVAL:
                joctx->nodes[joctx->nlen - 2]->value.kvval.val = _popNode(joctx);
                Node_DictSetKeyVal(joctx->nodes[joctx->nlen - 1], _popNode(joctx));
                break;
            default:
                break;
        }
    }
}

int errorCallback(jsonsl_t jsn, jsonsl_error_t err, struct jsonsl_state_st *state, char *errat) {
    JsonObjectContext *joctx = (JsonObjectContext *)jsn->data;

    joctx->err = err;
    joctx->errpos = state->pos_cur;
    jsonsl_stop(jsn);
    return 0;
}

int CreateNodeFromJSON(const char *buf, size_t len, Node **node, char **err) {
    int levels = JSONSL_MAX_LEVELS;  // TODO: heur levels from len since we're not really streaming?

    size_t _off = 0, _len = len;
    char *_buf = (char *)buf;
    int is_literal = 0;

    // munch any leading whitespaces
    while (IsAllowedWhitespace(_buf[_off]) && _off < _len) _off++;

    /* Embed literals in a list (also avoids JSONSL_ERROR_STRING_OUTSIDE_CONTAINER).
     * Copying is necc. evil to avoid messing w/ non-standard string implementations (e.g. sds), but
     * forgivable because most literals are supposed to be short-ish.
    */
    if ((is_literal = ('{' != _buf[_off]) && ('[' != _buf[_off]))) {
        _len = _len - _off + 2;
        _buf = malloc(_len * sizeof(char));
        _buf[0] = '[';
        _buf[_len - 1] = ']';
        memcpy(&_buf[1], &buf[_off], len - _off);
    }

    /* The lexer. */
    jsonsl_t jsn = jsonsl_new(levels);
    jsn->error_callback = errorCallback;
    jsn->action_callback_POP = popCallback;
    jsn->action_callback_PUSH = pushCallback;
    jsonsl_enable_all_callbacks(jsn);

    /* Set up our custom context. */
    JsonObjectContext *joctx = calloc(1, sizeof(JsonObjectContext));
    joctx->nodes = calloc(levels, sizeof(Node *));
    jsn->data = joctx;

    /* Feed the lexer. */
    jsonsl_feed(jsn, _buf, _len);

    /* Finalize. */
    int error = 0;
    if (JSONSL_ERROR_SUCCESS == joctx->err) {
        /* Extract the literal and discard the list. */
        if (is_literal) {
            Node_ArrayItem(joctx->nodes[0], 0, node);
            Node_ArraySet(joctx->nodes[0], 0, NULL);
            Node_Free(_popNode(joctx));
            free(_buf);
        } else {
            *node = _popNode(joctx);
        }
    } else {
        error = 1;
        if (err) {
            *err = calloc(JSONOBJECT_MAX_ERROR_STRING_LENGTH, sizeof(char));
            snprintf(*err, JSONOBJECT_MAX_ERROR_STRING_LENGTH,
                     "JSON lexer error at position %zd: %s", joctx->errpos + 1,
                     jsonsl_strerror(joctx->err));
        }
    }

    free(joctx->nodes);
    free(joctx);
    jsonsl_destroy(jsn);

    return error ? JSONOBJECT_ERROR : JSONOBJECT_OK;
}

/* === Serializer === */

typedef struct {
    size_t len;               // buffer length
    size_t cap;               // buffer capacity
    char *buf;                // buffer
    int depth;                // current depth
    int inkey;                // currently printing a hash key
    const char *indentstr;    // indentation string
    size_t indentlen;         // length of indent string
    const char *kvindentstr;  // indentation string after key
    size_t kvindentlen;       // length of indent string after key
    const char *newlinestr;   // linebreak string
    size_t newlinelen;        // linebreak of indent string
} JsonBuilder;

/* Grows the builder's buffer if needed. */
static char *ensure(JsonBuilder *b, int needed) {
    needed += b->len;
    if (needed <= b->cap) return b->buf + b->len;

    if (!b->cap) {  // 64 bytes should be ok for most literals
        b->cap = 1 << 6;
    } else if (b->cap < (1 << 16)) {  // otherwise grow by powers of 2 until 65K
        b->cap = 2 * b->cap;
    } else {  // then by 65K
        b->cap = b->cap + (1 << 16);
    }

    b->buf = realloc(b->buf, b->cap * sizeof(char));
    return b->buf + b->len;
}

/* Returns the buffer's current length. */
static int update(JsonBuilder *b) {
    char *str;
    str = b->buf + b->len;
    return b->len + strlen(str);
}

static char *indent(JsonBuilder *b) {
    char *str = 0;

    if (b->indentlen) {
        str = ensure(b, b->indentlen * b->depth + 1);
        if (str) {
            for (int i = 0; i < b->depth; i++) {
                memcpy(str, b->indentstr, b->indentlen);
                str = str + b->indentlen;
            }
        }
        str[0] = '\0';
        b->len = update(b);
    } else {
        str = ensure(b, 0);
    }
    return str;
}

/* Decalre it. */
static char *serialize_Node(Node *n, JsonBuilder *b);

static char *serialize_Dict(Node *n, JsonBuilder *b) {
    char *str = 0;

    str = ensure(b, b->newlinelen + 2);
    sprintf(str, "%s%s", "{", b->newlinestr);
    b->len = update(b);
    b->depth++;

    int len = n->value.dictval.len;
    if (len) {
        str = indent(b);
        str = serialize_Node(n->value.dictval.entries[0], b);
        if (str && len > 1) {
            for (int i = 1; i < len; i++) {
                str = ensure(b, b->newlinelen + 2);
                sprintf(str, ",%s", b->newlinestr);
                b->len = update(b);
                str = indent(b);
                str = serialize_Node(n->value.dictval.entries[i], b);
                b->len = update(b);
            }
        }
        str = ensure(b, b->newlinelen + 2);
        sprintf(str, "%s", b->newlinestr);
    }

    b->depth--;
    b->len = update(b);
    str = indent(b);
    str = ensure(b, 2);
    sprintf(str, "}");

    return str;
}

static char *serialize_Array(Node *n, JsonBuilder *b) {
    char *str = 0;

    str = ensure(b, b->newlinelen + 2);
    sprintf(str, "%s%s", "[", b->newlinestr);
    b->len = update(b);
    b->depth++;

    int len = n->value.arrval.len;
    if (len) {
        str = indent(b);
        str = serialize_Node(n->value.arrval.entries[0], b);
        if (len > 1) {
            for (int i = 1; i < len; i++) {
                str = ensure(b, b->newlinelen + 2);
                sprintf(str, ",%s", b->newlinestr);
                b->len = update(b);
                str = indent(b);
                str = serialize_Node(n->value.dictval.entries[i], b);
                b->len = update(b);
            }
        }
        str = ensure(b, b->newlinelen + 2);
        if (str) sprintf(str, "%s", b->newlinestr);
    }

    b->depth--;
    b->len = update(b);
    str = indent(b);
    str = ensure(b, 2);
    sprintf(str, "]");

    return str;
}

static char *serialize_String(Node *n, JsonBuilder *b) {
    char *str = 0;

    // TODO: escapes and shit!
    str = ensure(b, n->value.strval.len + 3);
    sprintf(str, "\"%.*s\"", n->value.strval.len, n->value.strval.data);

    return str;
}

static char *serialize_Node(Node *n, JsonBuilder *b) {
    char *str = 0;

    if (!n) {
        str = ensure(b, 5);
        sprintf(str, "null");
    } else {
        switch (n->type) {
            case N_BOOLEAN:
                if (n->value.boolval) {
                    str = ensure(b, 5);
                    sprintf(str, "true");
                } else {
                    str = ensure(b, 6);
                    sprintf(str, "false");
                }
                break;
            case N_INTEGER:
                str = ensure(b, 21);  // 19 charachter, sign and null terminator
                sprintf(str, "%ld", n->value.intval);
                break;
            case N_NUMBER:
                str = ensure(b, 64);
                if (fabs(floor(n->value.numval) - n->value.numval) <= DBL_EPSILON &&
                    fabs(n->value.numval) < 1.0e60)
                    sprintf(str, "%.0f", n->value.numval);
                else if (fabs(n->value.numval) < 1.0e-6 || fabs(n->value.numval) > 1.0e9)
                    sprintf(str, "%e", n->value.numval);
                else
                    sprintf(str, "%f", n->value.numval);
                break;
            case N_STRING:
                str = serialize_String(n, b);
                break;
            case N_KEYVAL:
                str = ensure(b, strlen(n->value.kvval.key) + b->kvindentlen + 4);
                sprintf(str, "\"%s\":%s", n->value.kvval.key, b->kvindentstr);
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

int SerializeNodeToJSON(const Node *node, const char *indentstr, const char *kvindentstr,
                        const char *newlinestr, char **json) {
    // set up the builder
    JsonBuilder b = {0};
    b.indentstr = indentstr ? indentstr : "";
    b.kvindentstr = kvindentstr ? kvindentstr : "";
    b.newlinestr = newlinestr ? newlinestr : "";
    b.indentlen = strlen(b.indentstr);
    b.kvindentlen = strlen(b.kvindentstr);
    b.newlinelen = strlen(b.newlinestr);

    // the real work
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

static int IsAllowedWhitespace(unsigned c) { return c == ' ' || Allowed_Whitespace[c & 0xff]; }
