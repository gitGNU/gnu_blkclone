/*
 * Blkclone central multicall dispatch
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

/*  This chooses a function from the multicall binary and invokes it.
 */

#define _GNU_SOURCE

#include <stdio.h>

#include "multicall.h"

DECLARE_MULTICALL_TABLE(main);

REGISTER_MULTICALL_TABLE(main);

int main(int argc, char ** argv)
{
  LINKTABLE_ITERATOR(MULTICALL_LINKTABLE_NAME(main), i);

  LINKTABLE_FOREACH(MULTICALL_LINKTABLE_NAME(main), i)
    if (!strcmp(argv[0],i->name)) return i->func(argc, argv);

  if (argc>1) {
    LINKTABLE_FOREACH(MULTICALL_LINKTABLE_NAME(main), i)
      if (!strcmp(argv[1],i->name)) return i->func(argc-1, argv+1);
  } else {
    puts("Subprograms available in this multicall binary:");
    LINKTABLE_FOREACH(MULTICALL_LINKTABLE_NAME(main), i)
      puts(i->name);
  }

  return 0;
}
