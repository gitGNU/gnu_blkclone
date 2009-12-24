/* Key-value list extra routines
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

#include <stdlib.h>

#include "keylist.h"

struct keylist * keylist_parse_args(int argc, char **argv)
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
