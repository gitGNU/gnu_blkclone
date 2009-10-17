#ifndef LINKTABLE_H
#define LINKTABLE_H

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

/* LINKTABLE_START(name)
 *  Macro:  name of the linker-provided symbol that refers to the first entry
 *		of a linker-assembled table
 * LINKTABLE_END(name)
 *  Macro:  name of the linker-provided symbol that refers to the location
 *		just after the last entry of a linker-assembled table
 * LINKTABLE_CELLTYPE(name)
 *  Macro:  name of the type alias for an entry in the given table
 * LINKTABLE_CELLNAME(name, nonce)
 *  Macro:  name for a cell in the given table with the given nonce
 */
#define LINKTABLE_START(name)		linktable__i__ ## name ## _start
#define LINKTABLE_END(name)		linktable__i__ ## name ## _end
#define LINKTABLE_CELLTYPE(name)	linktable__t__ ## name ## _cell_t
#define LINKTABLE_CELLNAME(name, nonce) linktable__d__ ## name ## _ ## nonce

/* LINKTABLE_ITERATOR(name, iterator)
 *  Macro:  declare an iteration variable for the given table
 * LINKTABLE_FOREACH(name, iterator)
 *  Macro:  loop header for iterating over the given table using ITERATOR
 *		ITERATOR is a pointer to a cell in the table
 */
#define LINKTABLE_ITERATOR(name, varname) LINKTABLE_CELLTYPE(name) * varname
#define LINKTABLE_FOREACH(name, i)    \
  for ( i = &LINKTABLE_START(name); i < &LINKTABLE_END(name); i++ )


/* AS_LINKTABLE_CELL(name)
 *  Macro:  magic words to put data where it will be picked up into
 *		a linker-assembled table
 */
#define AS_LINKTABLE_CELL(name)  \
  __attribute__((section(".tab." #name), used))

/* DECLARE_LINKTABLE(name,entry_type)
 *  Macro:  declare a linker-assembled table
 *   This macro provides the declarations to make a linker-assembled table
 *    visible in the current module.
 *   NAME must be a valid C symbol name.
 *   ENTRY_TYPE is a C type.
 *   It is expected to be used in headers.
 */
#define DECLARE_LINKTABLE(name,entry_type)			\
  typedef entry_type LINKTABLE_CELLTYPE(name);			 \
  extern LINKTABLE_CELLTYPE(name) LINKTABLE_START(name);	  \
  extern LINKTABLE_CELLTYPE(name) LINKTABLE_END(name)		   \

/* REGISTER_LINKTABLE(name)
 *  Macro:  make a linker-assembled table visible to the build process
 *   This macro inserts an entry into a special linker-assembled table
 *    used by the build system.
 *   Duplicates of these entries are permitted, but this macro should
 *    be used once, in the module that "owns" the given table.
 *   This macro establishes a read-only table.
 */
#define REGISTER_LINKTABLE(name)			       \
  static char LINKTABLE_CELLNAME(_meta_, name) []		\
       AS_LINKTABLE_CELL(_meta_) = { "--" #name "--RO--\n"}

/* REGISTER_LINKTABLE_WRITABLE(name)
 *  Macro:  make a linker-assembled table visible to the build process
 *   This macro inserts an entry into a special linker-assembled table
 *    used by the build system.
 *   Duplicates of these entries are permitted, but this macro should
 *    be used once, in the module that "owns" the given table.
 *   This macro establishes a table that can be changed at runtime.
 */
#define REGISTER_LINKTABLE_WRITABLE(name)		       \
  static char LINKTABLE_CELLNAME(_meta_, name) []		\
       AS_LINKTABLE_CELL(_meta_) = { "--" #name "--RW--\n" }

/* MAKE_LINKTABLE_ENTRY(name, nonce)
 *  Macro:  make an entry in a linker-assembled table
 *   NAME is the name of the table to which to add an entry
 *   NONCE is an extra fragment to avoid symbol-name collisions
 *   This macro expands to a declaration and should be followed
 *    by a static initializer.
 */
#define MAKE_LINKTABLE_ENTRY(name, nonce) \
  static LINKTABLE_CELLTYPE(name)	   \
    LINKTABLE_CELLNAME(name, nonce)	    \
       AS_LINKTABLE_CELL(name)

struct linktable_metatable_cell {
  char name[0];
};

DECLARE_LINKTABLE(_meta_, struct linktable_metatable_cell);

#endif
