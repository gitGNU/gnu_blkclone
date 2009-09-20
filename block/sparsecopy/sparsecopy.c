/*
 * Sparsecopy core program
 *
 *  This behaves similarly to dd(1), but only copies blocks listed in an index,
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
  uint64_t imagepos;	// current block number in data stream
  uint64_t diskpos;	// current block number on disk
  uint64_t blockcount;	// number of blocks in data stream
  uint64_t blockrange;	// number of blocks on disk
  uint64_t diskcnt;	// count of blocks processed from/to disk
  uuid_t uuid;		// image data UUID
};

struct progress {
  int src_pct;		// % read from source (integer)
  int src_pct_f;	// % read from source (fractional)
  int tgt_pct;		// % written to target (integer)
  int tgt_pct_f;	// % written to target (fractional)
  int src_baton;	// left baton position
  int tgt_baton;	// right baton position
};

static inline void fatal(char * msg)
{ perror(msg); exit (1); }

static int do_export(struct keylist * args,
		     struct imaging_context * ctx, FILE * map);
static int do_import(struct keylist * args,
		     struct imaging_context * ctx, FILE * map);

static struct {
  char * name;
  int (*func)(struct keylist* args,
	      struct imaging_context * ctx, FILE * map);
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
	  p->src_pct,p->src_pct_f,baton[3 & p->src_baton],
	  p->tgt_pct,p->tgt_pct_f,baton[3 & p->tgt_baton]);
  fflush(s);
}

static int do_export(struct keylist * args,
		     struct imaging_context * ctx, FILE * map)
{
  FILE * image = NULL;
  FILE * source = NULL;
  unsigned long long int src_frac, tgt_frac; //progress in 1/10ths percent
  struct progress p = {0};

  { //verify files
    struct stat stbuf_src = {0}, stbuf_tgt = {0};

    if (stat(keylist_get(args,"src"),&stbuf_src) < 0)
      fatal("failed to stat imaging source");

    if (  (stat(keylist_get(args,"tgt"),&stbuf_tgt) < 0)
	&&( errno != ENOENT))
      fatal("failed to stat imaging target");

    if (!(S_ISBLK(stbuf_src.st_mode) || S_ISCHR(stbuf_src.st_mode)
	  || S_ISREG(stbuf_src.st_mode))) {
      fprintf(stderr,
	      "imaging source must be a device or regular file\n");
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

  source = fopen(keylist_get(args,"src"),"r");
  if (!source) fatal("open imaging source");

  image = fopen(keylist_get(args,"tgt"),"a");
  if (!image) fatal("open imaging target");

  { //prep image stream header
    struct image_header_v1 * h = ctx->block;
    memcpy(h->sig,"BLKCLONEDATA\r\n\004\000",16);
    memcpy(h->uuid,ctx->uuid,sizeof(uuid_t));
    h->version = 1;
  }

  //write image stream header
  fwrite(ctx->block, ctx->blocklen, 1, image);
  // the header does not count as a block in the image stream

  { // copy data
    struct v1_extent e = {0};
    int ret;

    void progress(void) { //update progress and maybe sync
      src_frac = ctx->diskpos  * 1000 / ctx->blockrange;
      tgt_frac = ctx->imagepos * 1000 / ctx->blockcount;

      p.src_pct = src_frac / 10; p.src_pct_f = src_frac % 10;
      p.tgt_pct = tgt_frac / 10; p.tgt_pct_f = tgt_frac % 10;

      p.src_baton = ctx->diskcnt >> 8;
      p.tgt_baton = ctx->imagepos >> 8;

      show_progress(stderr, &p);
      //      if (!((ctx->diskcnt >> 11) & 3)) fdatasync(fileno(image));
    }

    do {
      ret = map_v1_readcell(map, &e);
      fseeko(source, e.start * ctx->blocklen, SEEK_SET);
      ctx->diskpos = e.start;
      if (e.length)
	//copy whole blocks
	while (e.length--) {
	  if (fread(ctx->block, ctx->blocklen, 1, source) != 1)
	    fatal("failed to read block from source");
#ifdef EAT_DATA
	  ((unsigned long long int *)ctx->block)[0]=ctx->diskpos;
#endif
	  if (fwrite(ctx->block, ctx->blocklen, 1, image) != 1)
	    fatal("failed to write block to image stream");
	  ctx->diskpos++; ctx->diskcnt++; ctx->imagepos++;
	  progress();
	}
      else if (e.num) {
	//copy partial block
	unsigned long long int len = ctx->blocklen * e.num / e.denom;
	memset(ctx->block, 0, ctx->blocklen);
	if (fread(ctx->block, len, 1, source) != 1)
	  fatal("failed to read partial block from source");
	if (fwrite(ctx->block, ctx->blocklen, 1, image) != 1)
	  fatal("failed to write padded block to image stream");
	ctx->diskpos++; ctx->diskcnt++; ctx->imagepos++;
	progress();
      }
    } while (!(ret<0));
  }
  return 0;
}

//TODO: implement "nuke" mode
static int do_import(struct keylist * args,
		     struct imaging_context * ctx, FILE * map)
{
  FILE * image = NULL;
  FILE * target = NULL;
  unsigned long long int src_frac, tgt_frac; //progress in 1/10ths percent
  struct progress p = {0};

  { //verify files
    struct stat stbuf_src = {0}, stbuf_tgt = {0};

    if (stat(keylist_get(args,"src"),&stbuf_src) < 0)
      fatal("failed to stat imaging source");

    if (  (stat(keylist_get(args,"tgt"),&stbuf_tgt) < 0)
	&&( errno != ENOENT))
      fatal("failed to stat imaging target");

    if (!(S_ISBLK(stbuf_tgt.st_mode) || S_ISCHR(stbuf_tgt.st_mode)
	  || S_ISREG(stbuf_tgt.st_mode))) {
      fprintf(stderr,
	      "imaging target must be a device or regular file\n");
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

  image = fopen(keylist_get(args,"src"),"r");
  if (!image) fatal("open imaging source");

  target = fopen(keylist_get(args,"tgt"),"r+");
  if (!target) fatal("open imaging target");

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

  { // copy data
    struct v1_extent e = {0};
    int ret;

    void progress(void) { //update progress and maybe sync
      src_frac = ctx->imagepos * 1000 / ctx->blockcount;
      tgt_frac = ctx->diskpos  * 1000 / ctx->blockrange;

      p.src_pct = src_frac / 10; p.src_pct_f = src_frac % 10;
      p.tgt_pct = tgt_frac / 10; p.tgt_pct_f = tgt_frac % 10;

      p.src_baton = ctx->diskcnt >> 8;
      p.tgt_baton = ctx->imagepos >> 8;

      show_progress(stderr, &p);
      //      if (!((ctx->diskcnt >> 11) & 3)) fdatasync(fileno(target));
    }

    do {
      ret = map_v1_readcell(map, &e);
      if (fseeko(target, e.start * ctx->blocklen, SEEK_SET))
	fatal("failed to seek on imaging target");
      if (ftello(target) != (e.start * ctx->blocklen))
	fatal("seek did not move file pointer as expected");
      ctx->diskpos = e.start;
      if (e.length)
	//copy whole blocks
	while (e.length--) {
	  if (fread(ctx->block, ctx->blocklen, 1, image) != 1)
	    fatal("failed to read block from image stream");
#ifdef EAT_DATA
	  ((unsigned long long int *)ctx->block)[1]=ctx->imagepos;
#endif
	  if (fwrite(ctx->block, ctx->blocklen, 1, target) != 1)
	    fatal("failed to write block to target");
	  ctx->imagepos++; ctx->diskpos++; ctx->diskcnt++;
	  progress();
	}
      else if (e.num) {
	//copy partial block
	unsigned long long int len = ctx->blocklen * e.num / e.denom;
	if (fread(ctx->block, ctx->blocklen, 1, image) != 1)
	  fatal("failed to read padded block from image stream");
	if (fwrite(ctx->block, len, 1, target) != 1)
	  fatal("failed to write partial block to target");
	ctx->imagepos++; ctx->diskpos++; ctx->diskcnt++;
	progress();
      }
    } while (!(ret<0));
  }
  return 0;
}

static struct keylist * parse_args(int argc, char **argv)
{
  struct keylist * head = NULL;
  struct keylist * i = NULL;
  char * key = NULL;
  char * value = NULL;

  head = keylist_new(*argv,""); i = head; argv++; argc--;
  while (argc--) {
    if (strchr(*argv,'=')) {
      char buf[strlen(*argv)+1];
      strcpy(buf,*argv);
      value = strchr(buf,'=');
      *value = 0; value++;
      for(key = buf; (*key == '-'); key++);
      i->next = keylist_new(key,value);
    } else {
      for(key = *argv; (*key == '-'); key++);
      i->next = keylist_new(key,"");
    }
    i = i->next; argv++;
  }

  return head;
}

static void help(char * name)
{
  printf("%s <mode> idx=<index> src=<source> tgt=<target> <other options>\n",
	 name);
  puts("Options:");
  puts("\t<mode> is one of:");
  puts("\t  export -- copy data from disk to image file");
  puts("\t  import -- copy data from image file to disk");
  puts("\tidx   -- specify index file");
  puts("\tsrc   -- specify source from which to read");
  puts("\ttgt   -- specfiy target to which to write");
  puts("\tnuke  -- (load mode only) write zero to unused blocks");
  puts("\tforce -- do it anyway; even if it looks wrong");
  exit(1);
}

static void usage(char * name)
{
  fprintf(stderr,
	  "%s <mode> idx=<index> src=<source> tgt=<target> <other options>\n",
	  name);
  exit(1);
}

#include <time.h>

int main(int argc, char ** argv)
{
  struct keylist * args = NULL;
  struct keylist * map_info = NULL;
  FILE * map = NULL;
  struct imaging_context ctx = {0};

  args = parse_args(argc, argv);

  if (keylist_get(args,"help")) help(argv[0]);

  { struct keylist * i;
    for (i=args; i; i=i->next)
      printf(" %s = %s\n",i->key,i->value);
  }

  { int modecnt = 0;

    for (mode_ptr=mode_list; mode_ptr->name; mode_ptr++)
      if (keylist_get(args,mode_ptr->name)) modecnt++;

    //validate options -- must give all of idx, src, tgt...
    if (!(  (keylist_get(args,"idx"))
	  &&(keylist_get(args,"src"))&&(keylist_get(args,"tgt"))
	  &&(modecnt==1)))  // ...and exactly one mode flag
      usage(argv[0]);
  }

#ifdef EAT_DATA
  { char * s = keylist_get(args,"caveat");
    if (!s || !strstr(s,"omnomnomnom"))
      { fprintf(stderr,"This binary eats data.\n"); abort(); }
  }
#endif

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

  for (mode_ptr=mode_list; mode_ptr->name; mode_ptr++)
    if (keylist_get(args,mode_ptr->name)) break;
  if (mode_ptr->name) (mode_ptr->func)(args, &ctx, map);

#if 0
  { struct timespec d={ .tv_nsec = (100000 * 93)},t={0};
    struct progress p = {0};
    int i;
    for (i=0;i<1000;i++) {
      p.src_pct=i/10;
      p.src_pct_f=i%10;
      if (!(i&0x4)) p.src_baton++;
      p.tgt_baton = i;
      show_progress(stdout,&p);
      nanosleep(&d,&t);
    }
  }
#endif

  return 0;

}
