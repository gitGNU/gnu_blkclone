/*
 *  Parse the $Bitmap file in an NTFS filesystem to generate a list
 *   of blocks that contain data.
 *
 *  This program actually does not need to care about what the block
 *   size actually is, however.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Each bit in the bitmap represents one cluster;
 *  the cluster is allocated iff the bit is set
 */
void emit_NTFS_extent_list(FILE * output, FILE * bitmap)
{
  unsigned long long int cluster = 0; /* counter */
  unsigned long long int start = 0;   /* start of current extent */
  int bcnt = 0; /* current bit index */
  int byte = 0; /* byte read from $Bitmap */
  enum {FREE, ALLOC} state = FREE;

  while ((byte = getc(bitmap)) != EOF) {
    for (bcnt=8; bcnt; bcnt--, byte>>=1, cluster++)
      switch (state) {
      case FREE:
	if (byte & 1) { start = cluster; state = ALLOC; }
	break;
      case ALLOC:
	if (!(byte & 1)) {
	  fprintf(output,"%lld+%lld\n",start,cluster-start);
	  state = FREE;
	}
	break;
      }
  }
}

void usage(char * name)
{
  fprintf(stderr,"%s: <mountpoint of NTFS filesystem>\n",name);
  exit(1);
}

void fatal(char * msg)
{ perror(msg); exit (1); }

int main(int argc, char ** argv)
{
  FILE * bitmap = NULL;
  char * bname = NULL;

  if (argc != 2) usage(argv[0]);

  asprintf(&bname, "%s/$Bitmap", argv[1]);
  if (!bname) fatal("allocation failed");
  bitmap = fopen(bname,"r");
  if (!bitmap) fatal("could not open $Bitmap");

  emit_NTFS_extent_list(stdout,bitmap);

  fclose(bitmap);

  return 0;
}
