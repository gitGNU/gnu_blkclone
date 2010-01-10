/*
 *  Analyze an NTFS filesystem to generate a block map for sparse imaging.
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
#define _FILE_OFFSET_BITS 64

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
#include <string.h>
#include <unistd.h>

#include "multicall.h"

/* Amazingly enough, NTFS filesystems have a bootsector that includes
 *  a BIOS parameter block, this info gives us the cluster size.
 */
#include "analyze/ecma-107.h"
#include "analyze/dispatch.h"

/* NOTE: according to comments in the Linux NTFS driver code,
 *	  a backup copy of the NTFS boot sector is stored after the last
 *	  sector in the filesystem; the sector count is reduced accordingly
 */

struct NTFS_info {
  uint64_t scount;	// sector count for whole volume
  uint64_t ccount;	// cluster count for volume (scount/spc)
  uint64_t MFTlcn;	// first cluster of MFT
  uint64_t dccount;	// count of allocated clusters
  uint32_t csize;	// cluster size in bytes (imaging block size)
  uint32_t ssize;	// sector size in bytes
  uint32_t spc;		// sectors per cluster
  uint32_t MFTreclen;	// size in bytes of an MFT record
};

// Simple, read-only NTFS driver
//  This only needs to be able to:
//    -- access files by MFT slot number
//	  (The system files we care about have fixed slot numbers.)
//    -- read $DATA attributes from non-resident files
//	  (Neither $Boot nor $Bitmap can be in the MFT.)
//	  (It turns out that supporting resident files is trivial after the
//	    infrastructure to read non-resident files is in place.)
//  Simplifying assumptions made:
//    -- $DATA attribute always has type 0x80
//	  (Else chicken-and-egg: need to read $AttrDef's $DATA attribute.)
//    -- MFT records do not span clusters
//    -- The extents for the $DATA attribute are themselves resident in the MFT.
//    -- This code is heavily optimized for sequential reads.
//	  (Seeking to position 0 is also efficient; anything else requires
//	    reparsing the data runs from the beginning of the file.)
//	  (This optimization is actually *less* work than always decoding the
//	    data runs fully and keeping the decoded extents around in memory.)


#define NTFS_DATA_ATTRTYPE 0x80

//fixed record numbers for system files
// here for documentation purposes; only $Bitmap is used in this code
#define NTFS_RECNO_MFT		 0 // $MFT
#define NTFS_RECNO_MFTMIRR	 1 // $MFTMirr
#define NTFS_RECNO_LOGFILE	 2 // $LogFile
#define NTFS_RECNO_VOLUME	 3 // $Volume
#define NTFS_RECNO_ATTRDEF	 4 // $AttrDef
#define NTFS_RECNO_ROOTDIR	 5 // . (root directory)
#define NTFS_RECNO_BITMAP	 6 // $Bitmap
#define NTFS_RECNO_BOOT		 7 // $Boot
#define NTFS_RECNO_BADCLUS	 8 // $BadClus
#define NTFS_RECNO_SECURE	 9 // $Secure
#define NTFS_RECNO_UPCASE	10 // $UpCase
#define NTFS_RECNO_EXTEND	11 // $Extend

struct NTFS_volume_ctx;
struct NTFS_file_ctx {		// represents a file opened for reading
  struct NTFS_volume_ctx * vol;	// reference to volume containing this file
  char * Frec;			// buffer for FILE record
  char * first_run;		// pointer into Frec for first run of file
  char * this_run;		// pointer into Frec to data run for current POS
  uint64_t pos;			// current read position in file
  uint64_t this_run_lcn;	// base lcn used with THIS_RUN
  uint64_t this_run_pos;	// read position for start of THIS_RUN
  uint64_t size;		// size of file
  // for a resident file (data is embedded in the MFT record):
  //  first_run will be NULL, and this_run will point to the file contents
  // (an explicit flag was not added to the struct because its current size is
  //  exactly 8 words on 64-bit systems and 12 words on 32-bit systems)
};

struct NTFS_volume_ctx {	// represents an NTFS volume
  struct NTFS_info info;	// info block for the volume
  struct NTFS_file_ctx MFT;	// handle for $MFT
  int fs_fd;			// file descriptor for volume (must be seekable)
};

//given: base of MFT FILE record
//return: ptr to first attribute or NULL
static char * get_first_attr(char * base)
{
  if (!((base[0] == 'F')
      &&(base[1] == 'I')
      &&(base[2] == 'L')
      &&(base[3] == 'E')))
    return NULL; // bad magic number
  return base + *((uint16_t*)(base + 0x14));
}
//given: ptr to attribute in MFT record
//return: ptr to next attribute or NULL
static char * get_next_attr(char * attr)
{
  if (*((uint32_t*)attr) == 0xFFFFFFFF)
    return NULL;
  return attr + *((uint32_t*)(attr + 0x04));
}
static inline int attr_type_p(char * attr, uint32_t type)
{ return *((uint32_t*)attr) == type; }

//given: type, starting point for search
//return: ptr to first attribute found of type TYPE
static inline char * find_attr_by_type(char * attr, uint32_t type)
{
  while (attr && !attr_type_p(attr, type))
    attr = get_next_attr(attr);
  return attr;
}

//given: base of MFT FILE record
//return: ptr to unnamed $DATA attribute
static char * find_unnamed_DATA_attr(char * attr)
{
  attr = get_first_attr(attr);
  if (!attr) return NULL;
  while (attr && (!attr_type_p(attr, NTFS_DATA_ATTRTYPE)
		  || (*((uint8_t*)(attr + 0x09)) != 0)))
    attr = get_next_attr(attr);
  return attr;
}

struct NTFS_decoded_extent {
  uint64_t length;		// length of extent in clusters
  int64_t offset;		// offset from previous extent in clusters
};

//given: ptrs to encoded data run and extent struct
//return: ptr to next run
//side effect: fills in extent struct
static char * decode_run(char * run, struct NTFS_decoded_extent * out)
{
  uint8_t llen = ((*((uint8_t*)(run))) & 0x0F);
  uint8_t olen = ((*((uint8_t*)(run))) & 0xF0) >> 4;
  // lengths of encoded length and offset
  int i = 0;

  out->length = out->offset = 0;

  if (*run == 0x00)
    return run; // nothing to decode (also EOF)

  run++; // assume sizeof(char) == sizeof(uint8_t)

  //length must be greater than zero
  for (i=0;llen;llen--,i+=8,run++)
    out->length |= (((uint64_t)(*run))&0xFFLL) << i;
  //offset is signed
  for (i=0;olen;olen--,i+=8,run++)
    out->offset |= (((uint64_t)(*run))&0xFFLL) << i;
  //...now sign extend it
  if (out->offset & (1 << (i-1))) // sign bit set
    for (;i<64;i+=8)
      out->offset |= 0xFFLL << i;

  return run;
}

//given: ptr to buffer containing FILE record as read from disk
//	 sector size
//return: status value (TRUE: success)
//		       (FALSE: invalid USN fields)
//side effect: fixups are applied to the record in BUF
// On failure, the buffer may have been partially fixed-up,
//  but was not valid to begin with.
// This function should actually be endian-neutral.
static int fixup_FILE_record(char * buf, uint32_t ssize)
{
  char * src = buf + *((uint16_t*)(buf+0x04)) + 2;
  char * tgt = buf + ssize - 2;
  int count = *((uint16_t*)(buf+0x06));
  uint16_t usn = *((uint16_t*)(buf + *((uint16_t*)(buf+0x04))));

  if (!usn) return 0;

  while (--count) {
    if (usn != *((uint16_t*)tgt))
      return 0;
    *(tgt++) = *(src++);
    *(tgt++) = *(src++);
    tgt += ssize - 2;
  }
  return 1;
}

static int NTFS_get_info(struct NTFS_info *, FILE *);

//given:  seekable, fd-backed stdio handle for NTFS volume
//return: ptr to NTFS_volume_ctx struct or NULL on failure
static struct NTFS_volume_ctx * NTFSdrv_volinit(FILE * fs)
{
  struct NTFS_volume_ctx * ctx = NULL;
  ssize_t ret = 0;

  ctx = malloc(sizeof(struct NTFS_volume_ctx));
  if (!ctx) return NULL;
  memset(ctx,0,sizeof(struct NTFS_volume_ctx));

  ctx->fs_fd = dup(fileno(fs));
  if (ctx->fs_fd < 0)
    goto out_free_ctx;

  // since the boot record is also at the start of the filesystem...
  if (NTFS_get_info(&(ctx->info),fs) < 0)
    goto out_free_ctx;

  // open the MFT "by hand"
  ctx->MFT.vol = ctx;
  ctx->MFT.Frec = malloc(ctx->info.MFTreclen);
  if (!ctx->MFT.Frec) goto out_free_ctx;
  memset(ctx->MFT.Frec,0,ctx->info.MFTreclen);

  ret = pread(ctx->fs_fd, ctx->MFT.Frec, ctx->info.MFTreclen,
	      ctx->info.csize * ctx->info.MFTlcn);
  if (ret != ctx->info.MFTreclen)
    //it is a disk file; the requested number of bytes should be read
    goto out_full_cleanup;

  // Apply fixups
  if (!fixup_FILE_record(ctx->MFT.Frec,ctx->info.ssize))
    goto out_full_cleanup;

  // The MFT should only have one $DATA attribute
  {
    char * data = find_unnamed_DATA_attr(ctx->MFT.Frec);
    if (!data) goto out_full_cleanup; // filesystem is corrupt
    //  the MFT's $DATA attribute really kinda has to be non-resident...
    if (*((uint8_t*)(data + 0x08)) == 0x00)
      //resident $DATA on $MFT???
      goto out_full_cleanup;
    ctx->MFT.first_run = data + *((uint16_t*)(data + 0x20));
    ctx->MFT.size = *((uint64_t*)(data + 0x30));
  }
  ctx->MFT.this_run = ctx->MFT.first_run;
  ctx->MFT.pos = 0;
  ctx->MFT.this_run_pos = 0;
  { struct NTFS_decoded_extent run = {0};
    decode_run(ctx->MFT.this_run, &run);
    ctx->MFT.this_run_lcn = run.offset; }

  return ctx;

 out_full_cleanup:
  free(ctx->MFT.Frec);
 out_free_ctx:
  free(ctx);
  return NULL;
}

//given: NTFSdrv file handle
//return: current file position
static inline uint64_t NTFSdrv_tell(struct NTFS_file_ctx * file)
{ return file->pos; }

//given: NTFSdrv file handle and absolute offset
//return: status code (TRUE: success)
//side effect on success: current postion of file handle is set as requested
//side effect on failure: file handle MAY be set to beginning-of-file
static int NTFSdrv_seekto(struct NTFS_file_ctx * file, uint64_t offset)
{
  if (offset == file->pos)
    // already there
    return 1;

  if (offset > file->size)
    // off the end -- fail
    return 0;

  if (!file->first_run) {
    // resident data; only one run possible
    file->pos = offset;
    return 1;
  }

  if (offset < file->this_run_pos) {
    // seeking backwards past the beginning of the current run
    //  is done by first seeking to the beginning of the file,
    //  then seeking forwards to the requested position
    file->this_run = file->first_run;
    file->pos = file->this_run_lcn = file->this_run_pos = 0; // rewind
  }

  if (offset < file->pos) {
    // seeking backwards in the current run
    file->pos = offset;
    return 1;
  }

  // If we get here, then we are seeking forwards,
  //  possibly after having rewound the file
  {
    struct NTFS_decoded_extent run = {0};
    char * next_run = decode_run(file->this_run, &run);
    uint64_t run_bound =
      file->this_run_pos + (run.length * file->vol->info.csize);
    uint64_t run_lcn = file->this_run_lcn;
    uint64_t run_pos = file->this_run_pos;

    if (offset < run_bound) {
      // seeking forwards still within the current run
      file->pos = offset;
      return 1;
    }

    // seeking forwards beyond the current run
    //  advance by runs until we reach one containing OFFSET
    while (*next_run && (offset > run_bound)) {
      next_run = decode_run(next_run, &run);
      run_pos = run_bound;
      run_lcn += run.offset;
      run_bound = run_pos + (run.length * file->vol->info.csize);
    }
    if (offset < run_bound) {
      // we found a run containing the requested offset, make it current
      file->this_run = next_run;
      file->this_run_lcn = run_lcn;
      file->this_run_pos = run_pos;
      file->pos = offset;
      return 1;
    }
    // we reached EOF while seeking (should not happen)
    return 0;
  }
}

//given: NTFSdrv file handle, target buffer, and length
//return: bytes read, negative on error
//	   There is one non-error condition where bytes
//	    read can be less than requested--EOF was
//	    encountered during the read and further reads
//	    will read zero bytes.
//side effect: data is read from file into buffer
static ssize_t NTFSdrv_read(struct NTFS_file_ctx * file,
			    void * buf, size_t len)
{
  struct NTFS_decoded_extent run = {0};
  char * next_run = decode_run(file->this_run, &run);
  uint64_t run_bound =
    file->this_run_pos + (run.length * file->vol->info.csize);
  ssize_t ret = 0;
  size_t rcnt = 0;

  if (len == 0) return 0;

  if (file->pos >= (file->size - 1))
    //at EOF
    return 0;

  if ((file->pos + len) >= file->size)
    len = file->size - file->pos; // set bound on LEN at to-end-of-file

  if (!file->first_run) {
    // shortcut: data is resident in FILE record; just copy some bytes
    memmove(buf,(file->this_run + file->pos),len);
    return len;
  }

  while ((len - rcnt) > 0) {
    while (file->pos < run_bound) {
      //attempt to satisfy request from current run
      ret = pread(file->vol->fs_fd,buf+rcnt,len-rcnt,
		  (file->this_run_lcn * file->vol->info.csize)
		  +(file->pos - file->this_run_pos));
      if (ret < 0) return ret; // oops, an error
      if (ret == 0) return -EIO; // failed to read when we expected to read
      rcnt += ret;
      file->pos += ret;
      if (rcnt >= len) return rcnt;
    }
    if ((len - rcnt) > 0) {
      //move to the next run
      file->this_run = next_run;
      next_run = decode_run(file->this_run, &run);
      file->this_run_lcn += run.offset;
      file->this_run_pos = run_bound;
      run_bound = file->this_run_pos + (run.length * file->vol->info.csize);
    }
  }
  return rcnt;
}

//given: NTFSdrv volume handle and record number in MFT
//return: NTFSdrv file handle (ptr to NTFS_file_ctx struct) or NULL on failure
static struct NTFS_file_ctx * NTFSdrv_open(struct NTFS_volume_ctx * vol,
					   uint64_t recno)
{
  struct NTFS_file_ctx * ctx = NULL;

  ctx = malloc(sizeof(struct NTFS_file_ctx));
  if (!ctx) return NULL;
  memset(ctx,0,sizeof(struct NTFS_file_ctx));

  ctx->vol = vol;
  ctx->Frec = malloc(vol->info.MFTreclen);
  if (!ctx->Frec) goto out_free_ctx;
  memset(ctx->Frec,0,vol->info.MFTreclen);

  //get the FILE record from the MFT
  if (!NTFSdrv_seekto(&(vol->MFT),recno * vol->info.MFTreclen))
    goto out_full_cleanup;
  if (NTFSdrv_read(&(vol->MFT),ctx->Frec,vol->info.MFTreclen)
      != vol->info.MFTreclen)
    goto out_full_cleanup;	// EOF while reading MFT is an error
  if (!fixup_FILE_record(ctx->Frec,vol->info.ssize))
    goto out_full_cleanup;

  //get the unnamed $DATA attribute
  {
    char * data = find_unnamed_DATA_attr(ctx->Frec);
    if (!data) goto out_full_cleanup; // filesystem is corrupt
    if (*((uint8_t*)(data + 0x08))) {
      // non-resident $DATA
      ctx->first_run = data + *((uint16_t*)(data + 0x20));
      ctx->size = *((uint64_t*)(data + 0x30));
      ctx->this_run = ctx->first_run;
    } else {
      // contents of this file are in its FILE record
      ctx->first_run = NULL; // mark the handle as resident-data
      ctx->size = *((uint32_t*)(data + 0x10));
      ctx->this_run = data + *((uint16_t*)(data + 0x14));
    }
  }

  //implicit seek to beginning-of-file
  ctx->pos = 0;
  ctx->this_run_pos = 0;
  if (ctx->first_run)
    { struct NTFS_decoded_extent run = {0};
      decode_run(ctx->first_run, &run);
      ctx->this_run_lcn = run.offset; }

  return ctx;

 out_full_cleanup:
  free(ctx->Frec);
 out_free_ctx:
  free(ctx);
  return NULL;
}

//given: NTFSdrv file handle
//returns void
//side effect: closes file handle, frees associated storage
static void NTFSdrv_close(struct NTFS_file_ctx * file)
{ free(file->Frec); free(file); }

//given: NTFSdrv volume handle
//returns void
//side effect: closes volume handle, frees associated storage
//		NB! INVALIDATES ALL FILE HANDLES STILL OPEN ON THE VOLUME!
//		    USE OF ANY SUCH FILE HANDLES WILL LIKELY CAUSE A CRASH!
//		    THE ONLY SAFE OPERATION ON SUCH A HANDLE IS TO CLOSE IT!
static void NTFSdrv_volclose(struct NTFS_volume_ctx * vol)
{ free(vol->MFT.Frec); close(vol->fs_fd); free(vol); }

// stdio interface shim for NTFSdrv (glibc interface)
//  the (void *) used as the cookie is actually a (struct NTFS_file_ctx *)

static ssize_t NTFSdrv__cookieread(void * cookie, char * buf, size_t len)
{
  struct NTFS_file_ctx * ctx = (struct NTFS_file_ctx *) cookie;
  ssize_t ret = NTFSdrv_read(ctx,buf,len);
  if (ret < 0) return -1;
  return ret;
}
static int NTFSdrv__cookieseek(void * cookie, off64_t * pos, int whence)
{
  struct NTFS_file_ctx * ctx = (struct NTFS_file_ctx *) cookie;
  uint64_t to = 0;

  switch (whence) {
  case SEEK_SET: to = *pos;			break;
  case SEEK_CUR: to = NTFSdrv_tell(ctx) + *pos;	break;
  case SEEK_END: to = ctx->size + *pos;		break;
  default: return -1;
  }

  return (NTFSdrv_seekto(ctx,to) ? 0 : -1);
}
static int NTFSdrv__cookieclose(void * cookie)
{
  struct NTFS_file_ctx * ctx = (struct NTFS_file_ctx *) cookie;
  NTFSdrv_close(ctx); return 0;
}
static cookie_io_functions_t NTFSdrv__stdio_hooks = {
  .read = NTFSdrv__cookieread,
  .write = NULL, // driver is read-only
  .seek = NTFSdrv__cookieseek,
  .close = NTFSdrv__cookieclose,
  0};
//given: NTFSdrv volume handle and MFT record number
//return: seekable stdio file handle opened for read
//	  or NULL on failure
static FILE * NTFSdrv_fopen(struct NTFS_volume_ctx * vol, uint64_t recno)
{
  struct NTFS_file_ctx * ctx = NULL;
  FILE * ret = NULL;

  ctx = NTFSdrv_open(vol,recno);
  if (!ctx) return NULL;

  ret = fopencookie(ctx,"r",NTFSdrv__stdio_hooks);
  if (!ret) NTFSdrv_close(ctx);

  return ret;
}

// NTFS filesystem analysis code

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
  ctx->MFTlcn = brec.ntfs.MFTlcn;

  ctx->csize  = ctx->ssize  * ctx->spc;
  ctx->ccount = ctx->scount / ctx->spc;

  if (brec.ntfs.MFTreclen > 0)
    ctx->MFTreclen = brec.ntfs.MFTreclen * ctx->csize;
  else
    ctx->MFTreclen = 1 << (-brec.ntfs.MFTreclen);

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

static int NTFS_ad_recognize(FILE * fs, const void * hdrbuf)
{
  struct ecma107_desc * f = (struct ecma107_desc *) hdrbuf;
  int ret = 1;

  // the ECMA-107 sysid must be "NTFS    "
  ret = ret && !memcmp(f->sysid,"NTFS    ",8);
  // and the NTFS sector count must be non-zero
  ret = ret && f->ntfs.scount64;
  // and the MFT and MFTMirr first LCNs must be non-zero
  ret = ret && (f->ntfs.MFTlcn && f->ntfs.MFTMlcn);

  return ret;
}

static int NTFS_ad_analyze(FILE * fs, FILE * out, char * ignore)
{
  struct NTFS_volume_ctx * vol = NULL;
  FILE * bitmap = NULL;

  vol = NTFSdrv_volinit(fs);
  if (!vol) fatal("NTFS volinit failed");

  bitmap = NTFSdrv_fopen(vol, NTFS_RECNO_BITMAP);
  if (!bitmap) fatal("could not open bitmap");

  vol->info.dccount = NTFS_count_used_blocks(bitmap,vol->info.ccount);

  fprintf(out,"Type:\tNTFS\n");

  fprintf(out,"# %d bytes/sector;  %d sectors/cluster; %d bytes/cluster\n",
	 vol->info.ssize,vol->info.spc,vol->info.csize);

  fprintf(out,"BlockSize:\t%lld\n",vol->info.csize);
  fprintf(out,"BlockCount:\t%lld\n",vol->info.dccount);
  fprintf(out,"BlockRange:\t%lld\n",vol->info.ccount);

  fprintf(out,"BEGIN BLOCK LIST\n");
  emit_NTFS_extent_list(stdout,bitmap,vol->info.ccount);
  //also catch the backup boot record
  fprintf(out,"%lld+.1/%d\n",vol->info.ccount,vol->info.spc);
  fprintf(out,"END BLOCK LIST\n");

  fclose(bitmap);
  NTFSdrv_volclose(vol);

  return 0;
}

DECLARE_ANALYSIS_MODULE(NTFS) = {
  .name = "NTFS",
  .fs_hdrsize = sizeof(struct ecma107_desc),
  .recognize = NTFS_ad_recognize,
  .analyze = NTFS_ad_analyze,
  0 };

//EOF
