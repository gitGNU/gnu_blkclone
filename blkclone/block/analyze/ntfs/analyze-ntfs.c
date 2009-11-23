/*
 *  Analyze an NTFS filesystem to generate a block map for sparse imaging.
 *
 *  Requires that the NTFS filesystem be mounted read-only.
 *
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

#define _GNU_SOURCE

/* PORTABILITY NOTE: the boot sector handling assumes a little-endian CPU
 *			This isn't as big a problem as it could be, since
 *			 only NT-based Windows uses NTFS, it only supports
 *			 little-endian processors, and this tool is designed
 *			 to run from a boot disk or network-boot environment
 *			 on the machine where the imaged system will run.
 */

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "multicall.h"

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

static int NTFS_get_info(struct NTFS_info * ctx, FILE * boot)
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
 * BOUND is the highest cluster number,
 *  apparently, it is possible for the bitmap to show clusters as "in-use"
 *  that are "off the end" of the volume
 */
static uint64_t NTFS_count_used_blocks(FILE * bitmap, unsigned long long int bound)
{
  unsigned long long int cluster = 0; /*  index  */
  unsigned long long int count   = 0; /* counter */
  int bcnt = 0; /* current bit index */
  int byte = 0; /* byte read from $Bitmap */

  rewind(bitmap);

  while (((byte = getc(bitmap)) != EOF) && cluster <= bound)
    for (bcnt=8; bcnt && cluster <= bound; bcnt--, byte>>=1, cluster++)
      if (byte & 1) count++;

  return count;
}
static void emit_NTFS_extent_list(FILE * output, FILE * bitmap,
				  unsigned long long int bound)
{
  unsigned long long int cluster = 0; /* counter */
  unsigned long long int start = 0;   /* start of current extent */
  int bcnt = 0; /* current bit index */
  int byte = 0; /* byte read from $Bitmap */
  enum {FREE, ALLOC} state = FREE;

  rewind(bitmap);

  while (((byte = getc(bitmap)) != EOF) && cluster <= bound)
    for (bcnt=8; bcnt && cluster <= bound; bcnt--, byte>>=1, cluster++)
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
  if (state == ALLOC) //emit last run
    fprintf(output,"%lld+%lld\n",start,cluster-start);
}

static char usagetext[] = "analyze_ntfs <mountpoint of NTFS filesystem>\n";

static inline void fatal(char * msg)
{ perror(msg); exit (1); }

DECLARE_MULTICALL_TABLE(main);
//int main(int argc, char ** argv)
SUBCALL_MAIN(main, analyze_ntfs, usagetext, NULL,
	     int argc, char ** argv)
{
  FILE * boot = NULL;
  FILE * bitmap = NULL;
  char * bname = NULL;
  struct NTFS_info ctx = { 0 };
  int ret = 0;

  if (argc != 2) print_usage_and_exit(usagetext);

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

  ctx.dccount = NTFS_count_used_blocks(bitmap,ctx.ccount);

  printf("Type:\tNTFS\n");

  printf("# %d bytes/sector;  %d sectors/cluster; %d bytes/cluster\n",
	 ctx.ssize,ctx.spc,ctx.csize);

  printf("BlockSize:\t%lld\n",ctx.csize);
  printf("BlockCount:\t%lld\n",ctx.dccount);
  printf("BlockRange:\t%lld\n",ctx.ccount);

  printf("BEGIN BLOCK LIST\n");
  emit_NTFS_extent_list(stdout,bitmap,ctx.ccount);
  //also catch the backup boot record
  fprintf(stdout,"%lld+.1/%d\n",ctx.ccount,ctx.spc);
  printf("END BLOCK LIST\n");

  fclose(bitmap);

  return 0;
}
