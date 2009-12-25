#ifndef LDTABLE_H
#define LDTABLE_H

/* Linker-assembled tables
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

/*  This facility provides a means for a binary to be aware of what modules
 *   were linked into it, using support from the build procedure.
 *  The linker is used to gather arrays with bits of data in different objects.
 *  This probably requires GNU ld.
 */

/* LDTABLE_START(name)
 *  Macro:  name of the linker-provided symbol that refers to the first entry
 *		of a linker-assembled table
 * LDTABLE_END(name)
 *  Macro:  name of the linker-provided symbol that refers to the location
 *		just after the last entry of a linker-assembled table
 * LDTABLE_CELLTYPE(name)
 *  Macro:  name of the type alias for an entry in the given table
 * LDTABLE_CELLNAME(name, nonce)
 *  Macro:  name for a cell in the given table with the given nonce
 */
#define LDTABLE_START(name)		ldtable__i__ ## name ## _start
#define LDTABLE_END(name)		ldtable__i__ ## name ## _end
#define LDTABLE_CELLTYPE(name)		ldtable__t__ ## name ## _cell_t
#define LDTABLE_CELLNAME(name, nonce)	ldtable__d__ ## name ## _ ## nonce

/* LDTABLE_ITERATOR(name, iterator)
 *  Macro:  declare an iteration variable for the given table
 * LDTABLE_FOREACH(name, iterator)
 *  Macro:  loop header for iterating over the given table using ITERATOR
 *		ITERATOR is a pointer to a cell in the table
 */
#define LDTABLE_ITERATOR(name, varname) LDTABLE_CELLTYPE(name) * varname
#define LDTABLE_FOREACH(name, i)    \
  for ( i = &LDTABLE_START(name); i < &LDTABLE_END(name); i++ )


/* AS_LDTABLE_CELL(name)
 *  Macro:  magic words to put data where it will be picked up into
 *		a linker-assembled table
 */
#define AS_LDTABLE_CELL(name)  \
  __attribute__((section("ldtab." #name), used, aligned(1)))

/* DECLARE_LDTABLE(name,entry_type)
 *  Macro:  declare a linker-assembled table
 *   This macro provides the declarations to make a linker-assembled table
 *    visible in the current module.
 *   NAME must be a valid C symbol name.
 *   ENTRY_TYPE is a C type.
 *   It is expected to be used in headers.
 */
#define DECLARE_LDTABLE(name,entry_type)			\
  typedef entry_type LDTABLE_CELLTYPE(name);			 \
  extern LDTABLE_CELLTYPE(name) LDTABLE_START(name);		  \
  extern LDTABLE_CELLTYPE(name) LDTABLE_END(name)		   \

/* REGISTER_LDTABLE(name)
 *  Macro:  make a linker-assembled table visible to the build process
 *   This macro inserts an entry into a special linker-assembled table
 *    used by the build system.
 *   Duplicates of these entries are permitted, but this macro should
 *    be used once, in the module that "owns" the given table.
 *   This macro establishes a read-only table.
 */
#define REGISTER_LDTABLE(name)			       \
  static struct ldtable_metatable_cell			\
  LDTABLE_CELLNAME(_meta_, name)			 \
       AS_LDTABLE_CELL(_meta_) =			  \
  { {0x3f, 0x34, 0x32}, 1,				   \
    __alignof__(LDTABLE_CELLTYPE(name)),		    \
    sizeof(LDTABLE_CELLTYPE(name)), {#name} }

/* REGISTER_LDTABLE_WRITABLE(name)
 *  Macro:  make a linker-assembled table visible to the build process
 *   This macro inserts an entry into a special linker-assembled table
 *    used by the build system.
 *   Duplicates of these entries are permitted, but this macro should
 *    be used once, in the module that "owns" the given table.
 *   This macro establishes a table that can be changed at runtime.
 */
#define REGISTER_LDTABLE_WRITABLE(name)		       \
  static struct ldtable_metatable_cell			\
  LDTABLE_CELLNAME(_meta_, name) []			 \
       AS_LDTABLE_CELL(_meta_) =			  \
  { {0x3f, 0x34, 0x32}, 2,				   \
    __alignof__(LDTABLE_CELLTYPE(name)),		    \
    sizeof(LDTABLE_CELLTYPE(name)), {#name} }

/* MAKE_LDTABLE_ENTRY(name, nonce)
 *  Macro:  make an entry in a linker-assembled table
 *   NAME is the name of the table to which to add an entry
 *   NONCE is an extra fragment to avoid symbol-name collisions
 *   This macro expands to a declaration and should be followed
 *    by a static initializer.
 */
#define MAKE_LDTABLE_ENTRY(name, nonce) \
  static LDTABLE_CELLTYPE(name)		 \
   LDTABLE_CELLNAME(name, nonce)	  \
       AS_LDTABLE_CELL(name)

#include <stdint.h>

struct ldtable_metatable_cell {
  uint8_t  magic[3];	// magic value: 0x3f3432
  uint8_t  mode;	// 1 -- R/O ; 2 -- R/W
  uint16_t align;	// alignment for this table
  uint16_t cellsize;	// sizeof LDTABLE_CELLTYPE for this table
  char name[0];
} __attribute__((packed));

DECLARE_LDTABLE(_meta_, struct ldtable_metatable_cell);

#endif
