#ifndef MULTICALL_H
#define MULTICALL_H

/* Linker-assembled multicall binary support
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

/* This uses ldtable.h (and the associated build support) to provide a simple
 *  means to contruct multicall binaries.
 */

#include "ldtable.h"

#define MULTICALL_LDTABLE_NAME(name) mcall_ ## name
#define DECLARE_MULTICALL_TABLE(name) \
  DECLARE_LDTABLE(mcall_ ## name, struct multicall_cell)
#define REGISTER_MULTICALL_TABLE(name) \
  REGISTER_LDTABLE(mcall_ ## name)

struct multicall_cell {
  char * name;
  char * usagetext;	//usage syntax
  char * helptext;	//long help text; printed after usage
  int (*func)(int argc, char **argv);
};

//central place to print a usage message for an invalid call
void print_usage_and_exit(char * usagetext);

/* the ... are the args for main; followed by function body */
#define SUBCALL_MAIN(tabname,module_name,use_,hlp_,...)	    \
  int main__ ## tabname ## __ ## module_name (__VA_ARGS__);  \
  MAKE_LDTABLE_ENTRY(mcall_ ## tabname,module_name) =	      \
  { #module_name, use_, hlp_,				       \
    main__ ## tabname ## __ ## module_name };			\
  int main__ ## tabname ## __ ## module_name (__VA_ARGS__)

#endif
