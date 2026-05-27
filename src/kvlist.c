#include "kvlist.h"

#include <stdlib.h>
#include <string.h>

kv_list *kvlist_create(void) {
    kv_list *l = calloc(1, sizeof(*l));
    return l;
}

void kvlist_free(kv_list *l) {
    if (!l) return;
    kv_lnode *n = l->head;
    while (n) {
        kv_lnode *next = n->next;
        free(n->val);
        free(n);
        n = next;
    }
    free(l);
}

static kv_lnode *make_node(const char *val, size_t vlen) {
    kv_string *s = malloc(sizeof(kv_string) + vlen + 1);
    if (!s) return NULL;
    s->len = vlen;
    memcpy(s->buf, val, vlen);
    s->buf[vlen] = '\0';

    kv_lnode *n = malloc(sizeof(kv_lnode));
    if (!n) { free(s); return NULL; }
    n->val  = s;
    n->prev = NULL;
    n->next = NULL;
    return n;
}

int64_t kvlist_lpush(kv_list *l, const char *val, size_t vlen) {
    kv_lnode *n = make_node(val, vlen);
    if (!n) return -1;
    n->next = l->head;
    if (l->head) l->head->prev = n;
    l->head = n;
    if (!l->tail) l->tail = n;
    return (int64_t)++l->len;
}

int64_t kvlist_rpush(kv_list *l, const char *val, size_t vlen) {
    kv_lnode *n = make_node(val, vlen);
    if (!n) return -1;
    n->prev = l->tail;
    if (l->tail) l->tail->next = n;
    l->tail = n;
    if (!l->head) l->head = n;
    return (int64_t)++l->len;
}

kv_string *kvlist_lpop(kv_list *l) {
    if (!l->head) return NULL;
    kv_lnode  *n   = l->head;
    kv_string *val = n->val;
    l->head = n->next;
    if (l->head) l->head->prev = NULL;
    else         l->tail = NULL;
    free(n);
    l->len--;
    return val; // caller owns
}

kv_string *kvlist_rpop(kv_list *l) {
    if (!l->tail) return NULL;
    kv_lnode  *n   = l->tail;
    kv_string *val = n->val;
    l->tail = n->prev;
    if (l->tail) l->tail->next = NULL;
    else         l->head = NULL;
    free(n);
    l->len--;
    return val; // caller owns
}
