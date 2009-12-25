/*
 * Blkclone build tool -- generate ldscript to collect ldtables
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

#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include "ldtable.h"

off_t sizeof_file(int fd)
{
  struct stat stbuf = { 0 };
  int ret;

  ret = fstat(fd, &stbuf);
  if (ret<0) { perror("fstat"); exit(1); }

  return stbuf.st_size;
}

// mmaps entire file read-only; returns address
void * map_file(int fd)
{
  void * ret = NULL;

  ret = mmap(NULL, sizeof_file(fd), PROT_READ, MAP_PRIVATE, fd, 0);

  if (!ret) { perror("mmap"); exit(1); }

  return ret;
}

static size_t sizeof_cell(struct ldtable_metatable_cell * c)
{ return sizeof(struct ldtable_metatable_cell) + strlen(c->name) + 1; }

static struct ldtable_metatable_cell *
next_cell(struct ldtable_metatable_cell * c, size_t * bound)
{
  uint8_t * i = (uint8_t *)c;
  i += sizeof(struct ldtable_metatable_cell);
  i += strlen(c->name);
  // now skip any padding that may have been inserted
  i = memmem(i,(*bound) - (i-((uint8_t*)c)),"?42",3);
  if (i) *bound -= (i-((uint8_t*)c));
  else   *bound = 0;
  return (struct ldtable_metatable_cell *)i;
}

void generate_ldscript(struct ldtable_metatable_cell * tab, size_t tablen)
{
  struct ldtable_metatable_cell * cell = tab;
  size_t len = tablen;

  puts("SECTIONS {");

  //first pass; collect R/O tables
  puts("  .rodata 0 : {");
  puts("    *(.rodata)");
  for (cell = tab, len = tablen;
       cell;
       cell = next_cell(cell,&len))
    if (cell->mode == 1) {
      //align to the largest of declared alignment and cell size
      printf("    . = ALIGN(0x%X);\n",
	     (cell->align > cell->cellsize) ? cell->align : cell->cellsize);
      printf("    ldtable__i__%s_start = .;\n", cell->name);
      printf("      *(ldtab.%s)\n", cell->name);
      printf("    ldtable__i__%s_end = .;\n", cell->name);

      fprintf(stderr,"  table %s align 0x%X cellsize 0x%X R/O\n",
	      cell->name, cell->align, cell->cellsize);
    }
  puts("  }");

  //second pass; collect R/W tables
  puts("  .data 0 : {");
  puts("    *(.data)");
  for (cell = tab, len = tablen;
       cell;
       cell = next_cell(cell,&len))
    if (cell->mode == 2) {
      //align to the largest of declared alignment and cell size
      printf("    . = ALIGN(0x%X);\n",
	     (cell->align > cell->cellsize) ? cell->align : cell->cellsize);
      printf("    ldtable__i__%s_start = .;\n", cell->name);
      printf("      *(ldtab.%s)\n", cell->name);
      printf("    ldtable__i__%s_end = .;\n", cell->name);

      fprintf(stderr,"  table %s align 0x%X cellsize 0x%X R/W\n",
	      cell->name, cell->align, cell->cellsize);
    }
  puts("  }");

  puts("}");
}

void usage(char * name)
{
  fprintf(stderr, "usage: %s <name of table-list file>\n", name);
  exit(1);
}

int main(int argc, char ** argv)
{
  void * tablist = NULL;
  int fd = -1;

  if (argc!=2) usage(argv[0]);

  fd = open(argv[1], O_RDONLY);
  if (fd<0) { perror("open table list"); exit(1); }

  tablist = map_file(fd);

  generate_ldscript(tablist, sizeof_file(fd));

  return 0; //leak the fd and mapping -- the system will close them
}
