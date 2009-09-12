/*
 *  Analyze an NTFS filesystem to generate a block map for sparse imaging.
 *
 *  Requires that the NTFS filesystem be mounted read-only.
 */

#define _GNU_SOURCE

/* PORTABILITY NOTE: the boot sector handling assumes a little-endian CPU */

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

/* Amazingly enough, NTFS filesystems have a bootsector that includes
 *  a BIOS parameter block, this info gives us the cluster size.
 */
#include "analyze/ecma-107.h"

/* NOTE: according to comments in the Linux NTFS driver code,
 *	  a backup copy of the NTFS boot sector is stored after the last
 *	  sector in the filesystem; the sector count is reduced accordingly
 */

struct NTFS_info {
  uint64_t scount;	// sector count for whole volume
  uint64_t ccount;	// cluster count for volume (scount/spc)
  uint64_t dccount;	// count of allocated clusters
  uint32_t csize;	// cluster size in bytes (imaging block size)
  uint32_t ssize;	// sector size in bytes
  uint32_t spc;		// sectors per cluster
};

int NTFS_get_info(struct NTFS_info * ctx, FILE * boot)
{
  struct ecma107_desc brec = {{0},{0},0};

  if (!(ctx && boot)) return -EFAULT;

  rewind(boot);

  if (fread(&brec,sizeof(brec),1,boot) != 1)
    return -EIO;

  ctx->ssize  = brec.ssize;
  ctx->spc    = brec.spc;
  ctx->scount = brec.ntfs.scount64;

  ctx->csize  = ctx->ssize  * ctx->spc;
  ctx->ccount = ctx->scount / ctx->spc;

  return 1;
}

/*
 * Each bit in the bitmap represents one cluster;
 *  the cluster is allocated iff the bit is set
 */
uint64_t NTFS_count_used_blocks(FILE * bitmap)
{
  unsigned long long int cluster = 0; /*  index  */
  unsigned long long int count   = 0; /* counter */
  int bcnt = 0; /* current bit index */
  int byte = 0; /* byte read from $Bitmap */

  rewind(bitmap);

  while ((byte = getc(bitmap)) != EOF)
    for (bcnt=8; bcnt; bcnt--, byte>>=1, cluster++)
      if (byte & 1) count++;

  return count;
}
void emit_NTFS_extent_list(FILE * output, FILE * bitmap)
{
  unsigned long long int cluster = 0; /* counter */
  unsigned long long int start = 0;   /* start of current extent */
  int bcnt = 0; /* current bit index */
  int byte = 0; /* byte read from $Bitmap */
  enum {FREE, ALLOC} state = FREE;

  rewind(bitmap);

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
  FILE * boot = NULL;
  FILE * bitmap = NULL;
  char * bname = NULL;
  struct NTFS_info ctx = { 0 };
  int ret = 0;

  if (argc != 2) usage(argv[0]);

  asprintf(&bname, "%s/$Bitmap", argv[1]);
  if (!bname) fatal("allocation failed");
  bitmap = fopen(bname,"r");
  if (!bitmap) fatal("could not open $Bitmap");
  free (bname);
  asprintf(&bname, "%s/$Boot", argv[1]);
  if (!bname) fatal("allocation failed");
  boot = fopen(bname,"r");
  if (!bitmap) fatal("could not open $Boot");
  free (bname);

  ret = NTFS_get_info(&ctx,boot);
  if (ret < 0) fatal("failed to read boot record");
  fclose(boot);

  ctx.dccount = NTFS_count_used_blocks(bitmap);

  printf("Type:\tNTFS\n");

  printf("# %d bytes/sector;  %d sectors/cluster; %d bytes/cluster\n",
	 ctx.ssize,ctx.spc,ctx.csize);

  printf("BlockSize:\t%lld\n",ctx.csize);
  printf("BlockCount:\t%lld\n",ctx.dccount);
  printf("BlockRange:\t%lld\n",ctx.ccount);

  printf("BEGIN BLOCK LIST\n");
  emit_NTFS_extent_list(stdout,bitmap);
  //also catch the backup boot record
  fprintf(stdout,"%lld+.1/%d\n",ctx.ccount,ctx.spc);
  printf("END BLOCK LIST\n");

  fclose(bitmap);

  return 0;
}
