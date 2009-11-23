/*
 * Blkclone central help command
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

#include <stdio.h>
#include <stdlib.h>

#include "multicall.h"

DECLARE_MULTICALL_TABLE(main);

static char usagetext[] = "help <subprogram>\n";

extern char * program_invocation_short_name; // GNU libc provides this

void print_usage_and_exit(char * usagetext)
{
  fprintf(stderr,"%s %s",program_invocation_short_name, usagetext);
  exit(1);
}

SUBCALL_MAIN(main, help, usagetext, NULL,
	     int argc, char ** argv)
{
  LDTABLE_ITERATOR(MULTICALL_LDTABLE_NAME(main), i);
  LDTABLE_ITERATOR(MULTICALL_LDTABLE_NAME(main), found);

  found = NULL;

  if (argc>1) {
    LDTABLE_FOREACH(MULTICALL_LDTABLE_NAME(main), i)
      if (!strcmp(i->name,argv[1])) { found = i; break; }
    if (found) {
      if (found->usagetext) {
	printf("%s %s",program_invocation_short_name,found->usagetext);
	if (found->helptext)
	  printf("%s",found->helptext);
      } else
	printf("No help for subprogram %s\n", found->name);
    }
  } else {
    printf("%s %s",program_invocation_short_name, usagetext);
    puts("The following subprograms have long help text:");
    LDTABLE_FOREACH(MULTICALL_LDTABLE_NAME(main), i)
      if (i->helptext) puts(i->name);
  }

  return 0;
}
