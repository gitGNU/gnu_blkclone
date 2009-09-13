#ifndef UUID_H
#define UUID_H

/* DCE UUID structures */
/*  this is very simple, since we only need to store and emit UUIDs;
 *   we rely on system facilities to generate them when needed
 *   --on Linux: read UUID from /proc/sys/kernel/random/uuid
 */

/* PORTABILITY NOTE: assumes use of ASCII */

#include <stdio.h>
#include <string.h>

typedef unsigned char uuid_t[16];

static inline int uuid_compare(uuid_t * a, uuid_t * b)
{ return memcmp(a,b,sizeof(uuid_t)); }
static inline int uuid_equals_p(uuid_t * a, uuid_t * b)
{ return !uuid_compare(a,b); }

static inline void parse_uuid(char * text, uuid_t * uuid)
{
  unsigned char * byte = (unsigned char *) uuid;
  int i;

  // simple hex digit parser; gives strange results on invalid input
  for (i=16; i; byte++,i--) {
    while (*text < '0') text++;
    *byte = (((*text) & 0x0F) + ((*text > '9') ? 9 : 0))<<4; text++;
    *byte|= (((*text) & 0x0F) + ((*text > '9') ? 9 : 0))   ; text++;
  }
}

static inline void print_uuid(FILE * out, uuid_t * uuid)
{
  char hexcode[16] = "0123456789abcdef";
  unsigned char * byte = (unsigned char *) uuid;
  int i;

  for (i=16; i; byte++,i--) {
    putc(hexcode[(*byte>>4)&0xF],out);
    putc(hexcode[(*byte   )&0xF],out);
    switch (i) {
    case 13: case 11: case 9: case 7:
      putc('-',out);
    default: ;
    }
  }
}

#endif
