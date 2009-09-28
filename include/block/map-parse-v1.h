#ifndef MAP_PARSE_V1_H
#define MAP_PARSE_V1_H

/* V1 block list parsing
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

#include "keylist.h"

#define MAP_V1_SIGNATURE   "BLKCLONE BLOCK LIST V1"
#define MAP_V1_STARTBLOCKS "BEGIN BLOCK LIST"
#define MAP_V1_ENDBLOCKS   "END BLOCK LIST"

struct v1_extent {
  unsigned long long int start;	 // first block in run
  unsigned long long int length; // length in whole blocks
  unsigned long int num;	 // numerator for fractional block
  unsigned long int denom;	 // denominator for fractional block
};

/* read a v1 header from IN and return a keylist
 *  returns NULL on failure
 *  the returned value must be deallocated with keylist_destroy
 *  leaves IN positioned at first block list entry
 */
struct keylist * map_v1_parsekeys(FILE * in);

/* read a v1 extent record from IN
 *  and fill out the provided struct v1_extent
 *  returns -1 on end-of-list; -2 on failure
 *  either the length field in CELL will be valid, or
 *   it will be zero and the num/denom pair valid
 *  DO NOT call this function again after end-of-list is reached
 */
int map_v1_readcell(FILE * in, struct v1_extent * cell);

#endif
