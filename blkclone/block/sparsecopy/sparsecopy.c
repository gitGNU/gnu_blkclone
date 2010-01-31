/*
 * Sparsecopy core program
 *
 * Copyright (C) 2009, 2010 Jacob Bachmeyer
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

/*  This behaves similarly to dd(1), but only copies blocks listed in an index,
 *   from which most parameters are taken.
 *  Further, the data stream read or written is prefixed with a header,
 *   preventing mixing up a data stream with an index for a different stream.
 *
 *  The intended use for this program is disk imaging.
 *
 *  The argument handling is "interesting", and notably permits the program
 *   binary name to serve as a parameter; unrecognized parameters are ignored.
 */

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

//#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "multicall.h"

#include "uuid.h"
#include "keylist.h"
#include "block/map-parse-v1.h"

/* v1 image header */
struct image_header_v1 {
  char    sig[16];	//signature: "BLKCLONEDATA\r\n\004\000"
  uint8_t uuid[16];	//UUID
  uint8_t version;	// == 1
};

struct imaging_context {
  void * block;		// buffer holding current block
  size_t blocklen;	// block size
  uint64_t logpos;	// current block number in data stream
  uint64_t phypos;	// current block number on disk
  uint64_t blockcount;	// number of blocks in data stream
  uint64_t blockrange;	// number of blocks on disk
  uint64_t diskcnt;	// count of blocks processed from/to disk
  uuid_t uuid;		// image data UUID
};

struct progress {
  int log_pct;		// % data stream (integer)
  int log_pct_f;	// % data stream (fractional)
  int phy_pct;		// % block device (integer)
  int phy_pct_f;	// % block device (fractional)
  int src_baton;	// left baton position
  int tgt_baton;	// right baton position
  int src_baton_;	// left baton position as of last update
  int tgt_baton_;	// right baton position as of last update
};

enum sparsecopy_mode {
  MODE_EXPORT, // copy data to image
  MODE_IMPORT, // copy data from image
  MODE_COUNT };

static inline void fatal(char * msg)
{ perror(msg); exit (1); }

static int do_copy_internal(struct imaging_context * ctx,
			    enum sparsecopy_mode mode,
			    FILE * map, FILE * source, FILE * target);

static int do_export(struct keylist * args,
		     struct imaging_context * ctx,
		     FILE * map, FILE * source, FILE * image);
static int do_import(struct keylist * args,
		     struct imaging_context * ctx,
		     FILE * map, FILE * image, FILE * target);

static struct {
  char * name;
  int (*func)(struct keylist * args,
	      struct imaging_context * ctx,
	      FILE * map, FILE * source, FILE * target);
} *mode_ptr, mode_list[] = {
  {"export",do_export},
  {"import",do_import},
  {NULL,NULL}};

static char baton[] = "|/-\\";

// suggest stepping baton every 2048 blocks copied
//	and blocking on sync every 4 steps
static inline void show_progress(FILE * s, struct progress * p)
{
  fprintf(s,"  %2d.%d%% %c -> %2d.%d%% %c\r",
	  p->log_pct,p->log_pct_f,baton[3 & p->src_baton],
	  p->phy_pct,p->phy_pct_f,baton[3 & p->tgt_baton]);
  fflush(s);
}

/* This function just copies data according to MAP from the stdio stream READ
 *  to the stdio stream WRITE, seeking on the stream SEEK (which should be one
 *  of the other two) according to the extents in MAP.
 */
static int do_copy_internal(struct imaging_context * ctx,
			    enum sparsecopy_mode mode,
			    FILE * map, FILE * source, FILE * target)
{
  FILE * seek;
  struct progress p = {0};
  struct v1_extent e = {0};
  unsigned int phy_frac, log_frac; //progress in 1/10ths percent
  int ret;

  void progress(void) { //update progress and maybe sync
    log_frac = ctx->logpos * 1000 / ctx->blockcount;
    phy_frac = ctx->phypos * 1000 / ctx->blockrange;

    p.log_pct = log_frac / 10; p.log_pct_f = log_frac % 10;
    p.phy_pct = phy_frac / 10; p.phy_pct_f = phy_frac % 10;

    p.src_baton = ctx->diskcnt >> 8;
    p.tgt_baton = ctx->logpos >> 8;

    if ((p.src_baton != p.src_baton_)||(p.tgt_baton != p.tgt_baton_)) {
      p.src_baton_ = p.src_baton;
	p.tgt_baton_ = p.tgt_baton;
	show_progress(stderr, &p);
    }
    //      if (!((ctx->diskcnt >> 11) & 3)) fdatasync(fileno(image));
  }

  switch (mode) {
  case MODE_EXPORT:
    seek = source; break;
  case MODE_IMPORT:
    seek = target; break;
  default:
    return -1;
  }

  do {
    ret = map_v1_readcell(map, &e);
    if (fseeko(seek, e.start * ctx->blocklen, SEEK_SET))
      fatal("failed to seek");
    if (ftello(seek) != (e.start * ctx->blocklen))
      fatal("seek did not move file pointer as expected");
    ctx->phypos = e.start;
    if (e.length)
      //copy whole blocks
      while (e.length--) {
	if (fread(ctx->block, ctx->blocklen, 1, source) != 1)
	  fatal("failed to read block");
	if (fwrite(ctx->block, ctx->blocklen, 1, target) != 1)
	  fatal("failed to write block");
	ctx->logpos++; ctx->phypos++; ctx->diskcnt++;
	progress();
      }
    else if (e.num) {
      //copy partial block
      unsigned long long int len = ctx->blocklen * e.num / e.denom;
      memset(ctx->block, 0, ctx->blocklen);
      // ARGH! copying a partial block breaks the abstraction
      //    Big Surprise -- this feature exists to support MS weirdness
      switch (mode) {
      case MODE_EXPORT:
	if (fread(ctx->block, len, 1, source) != 1)
	  fatal("failed to read partial block from source");
	if (fwrite(ctx->block, ctx->blocklen, 1, target) != 1)
	  fatal("failed to write padded block to image stream");
	break;
      case MODE_IMPORT:
	if (fread(ctx->block, ctx->blocklen, 1, source) != 1)
	  fatal("failed to read padded block from image stream");
	if (fwrite(ctx->block, len, 1, target) != 1)
	  fatal("failed to write partial block to target");
	break;
      default:
	fprintf(stderr,"No, we did not just reach line %d in %s.\n",
		__LINE__, __FILE__);
	abort(); // assertion failed
      }
      ctx->logpos++; ctx->phypos++; ctx->diskcnt++;
      progress();
    }
  } while (!(ret<0));
  show_progress(stderr, &p); // force showing final progress report
  return 0;
}

static int do_export(struct keylist * args,
		     struct imaging_context * ctx,
		     FILE * map, FILE * source, FILE * image)
{
  { //verify files
    struct stat stbuf_src = {0}, stbuf_tgt = {0};

    if (fstat(fileno(source),&stbuf_src) < 0)
      fatal("failed to stat imaging source");

    if (fstat(fileno(image),&stbuf_tgt) < 0)
      fatal("failed to stat imaging target");

    if (   fseeko(source, ctx->blocklen / 2, SEEK_SET)
	||(ftello(source) != ctx->blocklen / 2)
	||(fseeko(source, 0L, SEEK_SET))) {
      fprintf(stderr, "imaging source must be seekable\n");
      exit(1);
    }

    if (S_ISREG(stbuf_src.st_mode) && S_ISBLK(stbuf_tgt.st_mode)) {
      fprintf(stderr,
	      "WARNING:  Imaging source and target appear swapped.\n");
      if (keylist_get(args,"force")) {
	fprintf(stderr,
		" NOTICE:  Continuing anyway; as per \"force\" option.\n");
      } else {
	fprintf(stderr,
	"  NOTE:   If you REALLY want to store an image from a regular file\n"
	"           into a block device use the \"force\" option.\n");
	exit(1);
      }
    }
  }

  { //prep image stream header
    struct image_header_v1 * h = ctx->block;
    memcpy(h->sig,"BLKCLONEDATA\r\n\004\000",16);
    memcpy(h->uuid,ctx->uuid,sizeof(uuid_t));
    h->version = 1;
  }

  //write image stream header
  fwrite(ctx->block, ctx->blocklen, 1, image);
  // the header does not count as a block in the image stream

  return do_copy_internal(ctx, MODE_EXPORT, map, source, image);
}

//TODO: implement "nuke" mode
static int do_import(struct keylist * args,
		     struct imaging_context * ctx,
		     FILE * map, FILE * image, FILE * target)
{
  { //verify files
    struct stat stbuf_src = {0}, stbuf_tgt = {0};

    if (fstat(fileno(image),&stbuf_src) < 0)
      fatal("failed to stat imaging source");

    if (fstat(fileno(target),&stbuf_tgt) < 0)
      fatal("failed to stat imaging target");

    if (   fseeko(target, ctx->blocklen / 2, SEEK_SET)
	||(ftello(target) != ctx->blocklen / 2)
	||(fseeko(target, 0L, SEEK_SET))) {
      fprintf(stderr, "imaging target must be seekable\n");
      exit(1);
    }

    if (S_ISBLK(stbuf_src.st_mode) && S_ISREG(stbuf_tgt.st_mode)) {
      fprintf(stderr,
	      "WARNING:  Imaging source and target appear swapped.\n");
      if (keylist_get(args,"force")) {
	fprintf(stderr,
		" NOTICE:  Continuing anyway; as per \"force\" option.\n");
      } else {
	fprintf(stderr,
	"  NOTE:   If you REALLY want to load an image from a block device\n"
	"           into a regular file use the \"force\" option.\n");
	exit(1);
      }
    }
  }

  //read image stream header
  fread(ctx->block, ctx->blocklen, 1, image);
  // the header does not count as a block in the image stream

#ifndef BYPASS_UUID_CHECK /* to enable /dev/zero->/dev/null tests */
  { //verify image stream header
    struct image_header_v1 * h = ctx->block;
    if (memcmp(h->sig,"BLKCLONEDATA\r\n\004\000",16)) {
      fprintf(stderr, "Image stream header missing.\n");
      exit(1);
    }
    if (memcmp(h->uuid,ctx->uuid,sizeof(uuid_t))) {
      fprintf(stderr, "UUID mismatch between index and image stream.\n");
      exit(1);
    }
  }
#endif

  return do_copy_internal(ctx, MODE_IMPORT, map, image, target);
}

static char usagetext[] =
  "sparsecopy <mode> idx=<index> src=<source> tgt=<target> <other options>\n";
static char helptext[] =
  "Options:\n"
  "\t<mode> is one of:\n"
  "\t  export -- copy data from disk to image file\n"
  "\t  import -- copy data from image file to disk\n"
  "\tidx   -- specify index file\n"
  "\tsrc   -- specify source from which to read\n"
  "\ttgt   -- specfiy target to which to write\n"
  "\tnuke  -- (import mode only) write zero to unused blocks\n"
  "\tforce -- do it anyway; even if it looks wrong\n";

DECLARE_MULTICALL_TABLE(main);
//int main(int argc, char ** argv)
SUBCALL_MAIN(main, sparsecopy, usagetext, helptext,
	     int argc, char ** argv)
{
  struct keylist * args = NULL;
  struct keylist * map_info = NULL;
  FILE * map = NULL;
  FILE * source = NULL;
  FILE * target = NULL;
  struct imaging_context ctx = {0};
  int ret = 1;

  args = keylist_parse_args(argc, argv);

  { int modecnt = 0;

    for (mode_ptr=mode_list; mode_ptr->name; mode_ptr++)
      if (keylist_get(args,mode_ptr->name)) modecnt++;

    //validate options -- must give all of idx, src, tgt...
    if (!(  (keylist_get(args,"idx"))
	  &&(keylist_get(args,"src"))&&(keylist_get(args,"tgt"))
	  &&(modecnt==1)))  // ...and exactly one mode flag
      print_usage_and_exit(usagetext);
  }

  map = fopen(keylist_get(args,"idx"),"r");
  if (!map) fatal("failed to open index file");

  map_info = map_v1_parsekeys(map);
  if (!map_info) fatal("failed to read map");

  { //verify required map keys
    char *keys[] = { "UUID" , "Type",
		     "BlockSize", "BlockCount", "BlockRange",
		     NULL };
    char **k;
    for (k=keys;*k;k++)
      if (!keylist_get(map_info,*k))
	{ fprintf(stderr, "map missing required key %s\n", *k); exit(1); }
  }

  parse_uuid(keylist_get(map_info,"UUID"),&(ctx.uuid));
  ctx.blocklen = strtoull(keylist_get(map_info,"BlockSize"),NULL,0);
  ctx.blockcount = strtoull(keylist_get(map_info,"BlockCount"),NULL,0);
  ctx.blockrange = strtoull(keylist_get(map_info,"BlockRange"),NULL,0);

  ctx.block = malloc(ctx.blocklen);
  if (!ctx.block) fatal("allocate block buffer");
  memset(ctx.block,0,ctx.blocklen);

  { struct keylist * i;
    for (i=map_info; i; i=i->next)
      printf(" %s : %s\n",i->key,i->value);
  }

  source = fopen(keylist_get(args,"src"),"r");
  if (!source) fatal("open imaging source");
  target = fopen(keylist_get(args,"tgt"),"r+");
  if (!target) fatal("open imaging target");

  for (mode_ptr=mode_list; mode_ptr->name; mode_ptr++)
    if (keylist_get(args,mode_ptr->name)) break;
  if (mode_ptr->name)
    ret = (mode_ptr->func)(args, &ctx, map, source, target);

  fclose(map); fclose(source); fclose(target);
  free(ctx.block);
  keylist_destroy(args);
  keylist_destroy(map_info);

  return ret;

}
