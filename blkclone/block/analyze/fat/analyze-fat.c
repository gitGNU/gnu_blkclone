/*
 *  Analyze a FAT filesystem to generate a block map for sparse imaging.
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

#define _GNU_SOURCE
/* to ensure ability to scan entire filesystem */
#define _FILE_OFFSET_BITS 64

/* PORTABILITY NOTE: this code assumes a little-endian CPU */

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <fcntl.h>
#include <unistd.h>

#include "multicall.h"

#include "analyze/ecma-107.h"
#include "analyze/dispatch.h"

/* It appears that the ONLY aligned block in a FAT filesystem is the
 *  hardware sector.  In other words, the cluster size means nothing beyond
 *  the amount of space each FAT entry represents.  In particular, we have
 *  no guarantee that bootsect+FATs+rootdir align the data region on a
 *  cluster boundary.  This means that the blocksize used for imaging just
 *  about must be the sector size, since otherwise the blocks can't be
 *  guaranteed to line up with the allocation unit used in the filesystem.
 */

struct FAT_context {
  off_t FAT_offset;	// offset of first FAT in filesystem
  FILE * fs;		// stream open on filesystem
  uint32_t ssize;	// sector size in bytes (imaging block size)
  uint32_t spc;		// sectors per cluster
  uint32_t spf;		// sectors per FAT
  uint32_t ssa;		// number of sectors preceding data region
  uint32_t scount;	// total number of sectors
  uint32_t dscount;	// number of sectors with data (includes system area)
  uint32_t type;	// bits per FAT entry
};

//reads boot record and fills in context struct
// the read pointer on FS must point to the boot record
// returns -error code on error
// on success, FS is positioned at first sector of first FAT
static int FAT_init(struct FAT_context * ctx, FILE * fs)
{
  struct ecma107_desc brec = {{0},{0},0};

  if (!(ctx && fs)) return -EFAULT;
  ctx->fs = fs;

  if(fread(&brec,sizeof(brec),1,fs) != 1)
    return -EIO;

  ctx->ssize = brec.ssize;
  ctx->spc   = brec.spc;
  ctx->spf   = brec.spf;
  ctx->ssa   = ssa_from_ecma107_desc(&brec);
  if (brec.scnt_small) ctx->scount = brec.scnt_small;
  else ctx->scount = brec.scnt;
  // if the 16-bit sector count is zero,
  //  we assume that the 32-bit sector count is valid,
  //  without checking for an EPB (it MUST be there)

  //classic M$: FAT32 puts something else where the old EPB signature
  //		 is expected; it is possible for the signature to match,
  //		 yet be wrong; only believe it if the EPB fstype is also good
  if (((brec.epb.xtnd_sig | 1) == 0x29)
      &&(brec.epb.fstype[0] == 'F')
      &&(brec.epb.fstype[1] == 'A')
      &&(brec.epb.fstype[2] == 'T')) {
    // this should be either FAT12 or FAT16
    ctx->type =(brec.epb.fstype[3]&0x0F)*10;
    ctx->type+=(brec.epb.fstype[4]&0x0F)* 1;
  } else if (((brec.f32.xtnd_sig | 1) == 0x29)
	     &&(brec.f32.fstype[0] == 'F')
	     &&(brec.f32.fstype[1] == 'A')
	     &&(brec.f32.fstype[2] == 'T')
	     &&(brec.f32.fstype[3] == '3')
	     &&(brec.f32.fstype[4] == '2')) {
    // this sure *looks* like FAT32; paranoia here because FAT32
    //  puts the EPB signature where FAT12/FAT16 stored boot code
    ctx->type = 32;
    //fixup other values (I'm doubting SSA is still valid, for example)
  } else {
    // ancient FAT filesystem
    fprintf(stderr,
	    "Archaic FAT filesystem sans EPB detected; assuming FAT12\n");
    ctx->type = 12;
  }

  //seek to first byte of first FAT
  fseeko(fs, brec.ssize - sizeof(brec), SEEK_CUR);
  fseeko(fs, brec.ssize * (brec.rscnt - 1), SEEK_CUR);

  ctx->FAT_offset = ftello(fs);

  return 0; //success
}

static uint32_t FAT_count_used_sectors(struct FAT_context * ctx)
{
  uint32_t count = 0;
  uint32_t block = 0;

  //first: account for the System Area
  block = count = ctx->ssa;

  //second: run through the FAT and count used sectors
  fseeko(ctx->fs, ctx->FAT_offset, SEEK_SET);
  switch (ctx->type) {
  case 12:
    {
      union {
	uint8_t buf[3];
	uint16_t even;
	struct {
	  uint8_t pad;
	  uint16_t odd;
	} __attribute__((packed));
      } cell;
      uint16_t step = 0;
#define FATcell ((step & 1) ? ((cell.odd >> 4) & 0xFFF) : (cell.even & 0xFFF))
      //skip the first two FAT entries
      fread(cell.buf,3,1,ctx->fs);
      fread(cell.buf,3,1,ctx->fs);
      for (step=0; block < ctx->scount; step++, block += ctx->spc) {
	// skip bad clusters also (0xFF7)
	if ((FATcell != 0) && (FATcell != 0xFF7))
	  count +=ctx->spc;
	if (step & 1) fread(cell.buf,3,1,ctx->fs);
      }
#undef FATcell
    }
    break;
  case 16:
    {
      uint16_t FATcell;

      //skip first two FAT entries
      fseek(ctx->fs,4,SEEK_CUR);
      for (FATcell=0; block < ctx->scount; block += ctx->spc) {
	fread(&FATcell,2,1,ctx->fs);
	//also skip bad clusters (0xFFF7)
	if ((FATcell != 0) && (FATcell != 0xFFF7))
	  count += ctx->spc;
      }
    }
    break;
  case 32:
    {
    }
    break;
  default:
    fprintf(stderr,"FAT filesystem not one of FAT12/FAT16/FAT32\n");
    return 0;
  }
  return count;
}

static void emit_FAT_blocklist(FILE * out,struct FAT_context * ctx)
{
  uint64_t block = 0; /* counter */
  uint64_t start = 0; /* first block in current extent */
  enum {FREE, ALLOC} state = FREE;

  //first: account for the System Area
  fprintf(out,"0+%ld\n",ctx->ssa);
  block = ctx->ssa;

  //second: run through the FAT and list the used blocks in the Data Area
  fseeko(ctx->fs, ctx->FAT_offset, SEEK_SET);
  switch (ctx->type) {
  case 12:
    {
      union {
	uint8_t buf[3];
	uint16_t even;
	struct {
	  uint8_t pad;
	  uint16_t odd;
	} __attribute__((packed));
      } cell;
      uint16_t step = 0;
#define FATcell ((step & 1) ? ((cell.odd >> 4) & 0xFFF) : (cell.even & 0xFFF))
#ifdef DUMP_FAT_INSTEAD
      printf("%6s %4X%4X%4X%4X%4X%4X%4X%4X%4X%4X%4X%4X%4X%4X%4X%4X",
	     "",0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15);
#else
      //skip the first two FAT entries
      fread(cell.buf,3,1,ctx->fs);
#endif
      fread(cell.buf,3,1,ctx->fs);
      for (step=0; block < ctx->scount; step++, block += ctx->spc) {
#ifdef DUMP_FAT_INSTEAD
	if (!((block - ctx->ssa) & 15))
	  printf("\n%6X:",block - ctx->ssa);
	printf("%4X",FATcell);
#else
	switch (state) {
	case FREE:
	  // skip bad clusters also (0xFF7)
	  if ((FATcell != 0) && (FATcell != 0xFF7))
	    { start = block; state = ALLOC; }
	  break;
	case ALLOC:
	  if ((FATcell == 0) || (FATcell == 0xFF7))
	    { fprintf(out,"%lld+%lld\n",start,block-start); state = FREE; }
	  break;
	}
#endif
	if (step & 1) fread(cell.buf,3,1,ctx->fs);
      }
#undef FATcell
    }
    break;
  case 16:
    {
      uint16_t FATcell;
      uint16_t step;
#ifdef DUMP_FAT_INSTEAD
      printf("%6s %6X%6X%6X%6X%6X%6X%6X%6X%6X%6X%6X%6X%6X%6X%6X%6X",
	     "",0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15);
#else
      //skip first two FAT entries
      fseek(ctx->fs,4,SEEK_CUR);
#endif
      for (step=0; block < ctx->scount; step++, block += ctx->spc) {
	fread(&FATcell,2,1,ctx->fs);
#ifdef DUMP_FAT_INSTEAD
	if (!((step) & 15))
	  printf("\n%6X:",step);
	printf("%6X",FATcell);
#else
	switch (state) {
	case FREE:
	  //also skip bad clusters (0xFFF7)
	  if ((FATcell != 0) && (FATcell != 0xFFF7))
	    { start = block; state = ALLOC; }
	  break;
	case ALLOC:
	  if ((FATcell == 0) || (FATcell == 0xFFF7))
	    { fprintf(out,"%lld+%lld\n",start,block-start); state = FREE; }
	  break;
	}
#endif
      }
    }
    break;
  case 32:
    {
      fprintf(stderr,"FAT32 support not implemented yet\n");
      return;
    }
    break;
  default:
    fprintf(stderr,"FAT filesystem not one of FAT12/FAT16/FAT32\n");
    return;
  }
  if (state == ALLOC) //emit last run
    fprintf(out,"%lld+%lld\n",start,block-start);
}

static char usagetext[] = "analyze_fat <FAT filesystem image>\n";

static inline void fatal(char * msg)
{ perror(msg); exit (1); }

static int FAT_ad_recognize(FILE * fs, const void * hdrbuf)
{
  struct ecma107_desc * f = (struct ecma107_desc *) hdrbuf;
  int ret = 1;

  // one of the sector counts must be non-zero
  ret = ret && (f->scnt_small || f->scnt);
  // and both sectors/cluster and sectors/FAT be non-zero
  ret = ret && f->spc && f->spf;
  // and the System Area must have a non-zero computed size
  ret = ret && ssa_from_ecma107_desc(f);
  // and the EPB must contain "FAT" fstype
  ret = ret && ((((f->epb.xtnd_sig | 1) == 0x29)
		 &&(f->epb.fstype[0] == 'F')
		 &&(f->epb.fstype[1] == 'A')
		 &&(f->epb.fstype[2] == 'T'))
		||(((f->f32.xtnd_sig | 1) == 0x29)
		   &&(f->f32.fstype[0] == 'F')
		   &&(f->f32.fstype[1] == 'A')
		   &&(f->f32.fstype[2] == 'T')
		   &&(f->f32.fstype[3] == '3')
		   &&(f->f32.fstype[4] == '2')));
  // auto-detection will not work with archaic FAT filesystems

  return ret;
}

static int FAT_ad_analyze(FILE * fs, FILE * out, char * ignore)
{
  struct FAT_context ctx = { 0 };
  int ret = 0;

  ret = FAT_init(&ctx,fs);
  if (ret < 0) fatal("failed to read FS descriptor");

  ctx.dscount = FAT_count_used_sectors(&ctx);

  fprintf(out,"Type:\tFAT\n");
  fprintf(out,"FsType:\tFAT%d\n",ctx.type);

  fprintf(out,"# %d sectors/cluster; %d sectors/FAT\n",ctx.spc,ctx.spf);
  fprintf(out,"# FAT spans %d entries\n",ctx.spf * ctx.ssize * 8 / ctx.type);

  fprintf(out,"BlockSize:\t%d\n",ctx.ssize);
  fprintf(out,"BlockCount:\t%d\n",ctx.dscount);
  fprintf(out,"BlockRange:\t%d\n",ctx.scount);

  fprintf(out,"BEGIN BLOCK LIST\n");
  emit_FAT_blocklist(out,&ctx);
  fprintf(out,"END BLOCK LIST\n");

  return 0;
}

DECLARE_ANALYSIS_MODULE(FAT) = {
  .name = "FAT",
  .fs_hdrsize = sizeof(struct ecma107_desc),
  .recognize = FAT_ad_recognize,
  .analyze = FAT_ad_analyze,
  0 };

//EOF
