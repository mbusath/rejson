#include "json_object.h"
#include "object.h"
#include "jsonsl.h"

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
    // TODO: free joctx->nodes and contents, special free for kvval
    joctx->error_type = jo_error_type_jo;
    joctx->error.jo = JSONOBJECT_ERROR_ALLOC;
    jsonsl_stop(jsn);
    return;
}

void pop_callback(jsonsl_t jsn, jsonsl_action_t action, struct jsonsl_state_st *state,
                  const jsonsl_char_t *at) {
    json_object_context_t *joctx = (json_object_context_t *)jsn->data;

    /* Hashkeys as well as numeric and Boolean literals create nodes. */
    Node *n = NULL;

    if (JSONSL_T_STRING == state->type) {
        n = NewStringNode(jsn->base + state->pos_begin + 1, state->pos_cur - state->pos_begin - 2);
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
        n = NewKeyValNode(jsn->base + state->pos_begin + 1, state->pos_cur - state->pos_begin - 2,
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
                free((char *)joctx->nodes[joctx->len - 2]->value.kvval.key);
                free(joctx->nodes[joctx->len - 2]);
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
    // TODO: free joctx->nodes and contents, special free for kvval, no free nulls
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

Node *CreateNodeFromJSON(const char *buf, size_t len, char *err) {
    /* The lexer. */
    jsonsl_t jsn;
    if (!(jsn = jsonsl_new(JSONSL_MAX_LEVELS))) goto alloc_error;

    /* Set up and enable the callbacks. */
    jsn->error_callback = error_callback;
    jsn->action_callback_POP = pop_callback;
    jsn->action_callback_PUSH = push_callback;
    jsonsl_enable_all_callbacks(jsn);

    /* Set up our context. */
    json_object_context_t *joctx;
    if (!(joctx = calloc(1, sizeof(json_object_context_t)))) goto alloc_error;
    if (!(joctx->nodes = calloc(JSONSL_MAX_LEVELS, sizeof(Node *)))) goto alloc_error;
    joctx->error_type = jo_error_type_ok;
    jsn->data = joctx;

    /* Feed the lexer. */
    jsonsl_feed(jsn, buf, len);

    /* Finalize. */
    Object *root;
    if (jo_error_type_ok == joctx->error_type) {
        root = joctx->nodes[0];
    } else {
        root = NULL;
        if (err) {
            if (jo_error_type_jsl == joctx->error_type) {
                sprintf(err, "lexer error: %s", jsonsl_strerror(joctx->error.jsl));
            } else if (jo_error_type_obj == joctx->error_type) {
                sprintf(err, "object error: %d", joctx->error.obj);
            } else {
                sprintf(err, "json object error: %d", joctx->error.jo);
            }
        }
    }

    free(joctx->nodes);
    free(joctx);
    jsonsl_destroy(jsn);

    return root;

alloc_error:
    if (jsn) jsonsl_destroy(jsn);
    if (joctx) {
        if (joctx->nodes) {
            if (joctx->nodes[0]) Node_Free(joctx->nodes[0]);
            free(joctx->nodes);
        }
        free(joctx);
    }
    if (err) sprintf(err, "json object error: OOM while initializing");
    return NULL;
}