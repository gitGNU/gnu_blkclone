#ifndef KEYLIST_H
#define KEYLIST_H

/* General key-value list
 * Copyright (C) 2009 Jacob Bachmeyer
 *
 * This file is part of blkclone.
 *
 * The blkclone tools are free software; you can redistribute and/or modify
 * them under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

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

/* Convert ARGC and ARGV into a keylist by decomposing each argument as
   KEY=VALUE.  Strips leading dashes from keys.
 */
struct keylist * keylist_parse_args(int argc, char **argv);

#endif
