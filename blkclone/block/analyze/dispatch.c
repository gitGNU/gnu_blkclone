/* Filesystem type identification and module dispatch
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

/*  This implements the "analyze" command and dispatches to the appropriate
    analysis module.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include <fcntl.h>
#include <unistd.h>

#include "keylist.h"
#include "multicall.h"
#include "analyze/dispatch.h"

REGISTER_LDTABLE(analysis_modules);

static char usagetext[] =
  "analyze [type=<fstype>] src=<source> <other options>\n";
static char helptext[] =
  "Options:\n"
  "\ttype   -- specify type of filesystem (omit for auto-detection)\n"
  "\tsrc    -- specify source from which to read filesystem\n"
  "\tdetect -- only determine filesystem type; do not actually analyze\n";

static inline void fatal(char * msg)
{ perror(msg); exit (1); }

DECLARE_MULTICALL_TABLE(main);
SUBCALL_MAIN(main, analyze, usagetext, helptext,
	     int argc, char ** argv)
{
  LDTABLE_ITERATOR(analysis_modules, i);
  struct analysis_module * mod = NULL;
  struct keylist * args = NULL;
  FILE * fs = NULL;
  void * fshdrbuf = NULL;
  size_t fshdrlen = 0;
  int ret = 0;

  args = keylist_parse_args(argc, argv);

  if (!keylist_get(args,"src"))
    print_usage_and_exit(usagetext);

  LDTABLE_FOREACH(analysis_modules, i)
    if (i->fs_hdrsize > fshdrlen) fshdrlen = i->fs_hdrsize;

  fshdrbuf = malloc(fshdrlen);
  if (!fshdrbuf)
    fatal("failed to allocate buffer for filesystem header");
  memset(fshdrbuf,0,fshdrlen);

  fs = fopen(keylist_get(args,"src"),"r");
  if (!fs) fatal("could not open filesystem");

  if (fread(fshdrbuf,fshdrlen,1,fs) != 1)
    fatal("failed to read filesystem header");

  if (keylist_get(args,"type")) {
    // use particular module (case-insensitive)
    char * name = keylist_get(args,"type");
    LDTABLE_FOREACH(analysis_modules, i)
      if (i->name && !strcasecmp(i->name,name))
	mod = i;
    if (!mod) {
      fprintf(stderr,"Requested module %s not found.\n",name);
      return 1;
    }
  } else {
    // attempt to auto-detect
    LDTABLE_FOREACH(analysis_modules, i)
      if (i->recognize && i->recognize(fs, fshdrbuf)) {
	mod = i; // We found an analysis module for this fs
	break;  // Take first match only
      }
    if (!mod) {
      fprintf(stderr,"No module recognizes %s.\n",keylist_get(args,"src"));
      return 1;
    }
  }

  if (keylist_get(args,"detect")) {
    printf("Would analyze %s using module %s.%s\n",
	   keylist_get(args,"src"),mod->name,
	   keylist_get(args,"type") ? "  (as requested)" : "");
    return 0;
  }

  if (mod->need_mounted_fs) {
    fprintf(stderr,
	    "Mounting filesystem under analysis not yet implemented.\n"
	    "    (needed by module %s)\n", mod->name);
    return 1;
  }

  rewind(fs);

  ret = mod->analyze(fs,stdout,NULL);

  fclose(fs);

  return ret;
}
