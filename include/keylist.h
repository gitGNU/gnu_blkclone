#ifndef KEYLIST_H
#define KEYLIST_H

/* General key-value list */

struct keylist;
struct keylist {
  struct keylist * next;
  char * key;
  char * value;
};

#include <stdlib.h>
#include <string.h>

static inline struct keylist *
keylist_new(char * key, char * value)
{ struct keylist * ret = malloc(sizeof(struct keylist));
  if (!ret) return NULL;  memset(ret, 0, sizeof(struct keylist));
  ret->key = strdup(key); ret->value = strdup(value);
  return ret; }

static inline void
keylist_free(struct keylist * c)
{ free(c->key); free(c->value); free(c); }

static inline void
keylist_destroy(struct keylist * h)
{ struct keylist * i = h;
  while (i) { i=h->next; keylist_free(h); h = i; } }

static inline struct keylist *
keylist_find(struct keylist * l, char * k)
{ while (l)
    if (!strcmp(l->key,k)) return l;
    else l = l->next;
  return NULL; }

static inline char *
keylist_get(struct keylist * l, char * k)
{ while (l)
    if (!strcmp(l->key,k)) return l->value;
    else l = l->next;
  return NULL; }

#endif
