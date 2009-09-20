/* V1 block list parsing */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "map-parse-v1.h"

struct keylist * map_v1_parsekeys(FILE * in)
{
  struct keylist * head = NULL;
  struct keylist * i = NULL;
  char * linebuf = NULL;
  size_t linebuflen = 0;
  ssize_t linelen = 0;
  char * value = NULL;

  // read signature
  linelen = getline(&linebuf, &linebuflen, in);
  if (linelen == -1) return NULL;
  if (strcmp(linebuf,MAP_V1_SIGNATURE"\n")) goto out;

  // read keys
  head = keylist_new("MapVersion","1");
  if (!head) goto out;
  i = head;
  while ((linelen = getline(&linebuf, &linebuflen, in)) != -1) {
    if (linebuf[0] == '#') continue;
    // trim trailing newline
    *(strchr(linebuf,'\n')) = '\0';
    if (!strcmp(linebuf,MAP_V1_STARTBLOCKS)) goto done;
    value = strchr(linebuf, ':');
    if (!value) continue;
    *value = '\0'; value++;
    // trim leading whitespace from value
    value += strspn(value, " \t");
    // store it into the list
    i->next = keylist_new(linebuf,value);
    i = i->next; if (!i) goto out_destroy_keylist;
  }

 done:
  free(linebuf);
  return head;

 out_destroy_keylist:
  keylist_destroy(head);
 out:
  free(linebuf);
  return NULL;
}

int map_v1_readcell(FILE * in, struct v1_extent * cell)
{
  static char * linebuf = NULL;
  static size_t linebuflen = 0;
  ssize_t linelen = 0;

  linelen = getline(&linebuf, &linebuflen, in);
  if (linelen == -1) return -2;

  memset(cell,0,sizeof(struct v1_extent));

  if (!strcmp(linebuf,MAP_V1_ENDBLOCKS"\n")) {
    // end-of-list reached
    free(linebuf); linebuf=NULL; linebuflen=0;
    return -1;
  }

  if (strstr(linebuf,"+.")) {
    //fractional block
    if (sscanf(linebuf,"%llu+.%lu/%lu\n",
	       &(cell->start),&(cell->num),&(cell->denom))
	!= 3) {
      fprintf(stderr,"syntax error in block map index at \"%s\"\n",linebuf);
      free(linebuf); linebuf=NULL; linebuflen=0;
      return -2;
    }
  } else {
    //integral blocks
    if (sscanf(linebuf,"%llu+%llu\n",&(cell->start),&(cell->length)) != 2) {
      fprintf(stderr,"syntax error in block map index at \"%s\"\n",linebuf);
      free(linebuf); linebuf=NULL; linebuflen=0;
      return -2;
    }
  }

  return 0;
}

#ifdef UNIT_TEST
#include <errno.h>
void fatal(char * msg)
{ perror(msg); exit (1); }

int main(int argc, char ** argv)
{
  FILE * f = NULL;
  struct keylist * k = NULL;
  struct keylist * i = NULL;

  struct v1_extent e = {0};
  int ret = 0;

  if (argc != 2) {
    fprintf(stderr,
	    "Usage: %s <name of index to read>\n",argv[0]);
    exit(1);
  }

  f = fopen(argv[1],"r");
  if (!f) fatal("open input");

  k = map_v1_parsekeys(f);

  for (i=k; i; i=i->next)
    printf("\"%s\" -> \"%s\"\n",i->key,i->value);

  keylist_destroy(k);

  do {
    ret = map_v1_readcell(f, &e);
    if (e.length)
      printf(": %6lld blocks @ %8lld\n",e.length,e.start);
    else if (e.num)
      printf(": %ld/%ld block @ %8lld\n",e.num,e.denom,e.start);
    else if (ret != -1)
      printf("!!! %lld %lld %ld %ld\n",e.start,e.length,e.num,e.denom);
  } while (!(ret<0));

  printf("Exited read-blocks loop with ret = %d",ret);

  return 0;
}
#endif
