#ifndef ANALYZE_DISPATCH_H
#define ANALYZE_DISPATCH_H

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

#include "ldtable.h"

#define DECLARE_ANALYSIS_MODULE(tag)		\
  MAKE_LDTABLE_ENTRY(analysis_modules, tag)

struct analysis_module {
  // name of analysis module
  char * name;
  // number of bytes from the fs header needed for recognition
  size_t fs_hdrsize;
  // returns TRUE if this module knows it understands the filesystem
  //  FS is a stdio handle open on the filesystem
  //  HDRBUF is a buffer containing at least FS_HDRSIZE bytes starting at LBA 0
  int (*recognize) (FILE * fs, const void * hdrbuf);
  // performs analysis of the filesystem
  //  FS is a stdio handle open on the filesystem
  //  OUT is a stdio handle where the block list should be written
  //  MNTPNT is a location where the filesystem has been mounted read-only
  //   or NULL if the module does not set the NEED_MOUNTED_FS flag
  int (*analyze) (FILE * fs, FILE * out, char * mntpnt);
  /* flags */
  // if set, analysis of this filesystem type requires that it be mounted
  unsigned int need_mounted_fs:1;
};

DECLARE_LDTABLE(analysis_modules, struct analysis_module);

#endif
