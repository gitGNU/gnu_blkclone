/*
 *  Test NTFS mini-driver.
 *
 * Copyright (C) 2010 Jacob Bachmeyer
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

#include "analyze-ntfs.c" //<-- note that there isn't actually a main() in there

void print_usage_and_exit(char * ignore)
{} // so the above will link (the function that calls it won't be called)

void testusage(char * name)
{
  fprintf(stderr,"usage: %s <hex bytes for encoded run>\n",name);
  exit(1);
}

int main(int argc, char ** argv)
{
  void * buf = NULL;
  unsigned char * p = NULL;
  struct NTFS_decoded_extent run = {0};
  size_t len = argc - 1;
  int i = 0;

  if (argc<2) testusage(argv[0]);

  buf=malloc(argc);
  if (!buf) fatal("alloc");

  for (p=buf,argc--,argv++;argc;argc--,argv++,p++)
    *p=(char)strtol(*argv,NULL,16);

  printf("bytes: ");
  for (p=buf,i=0;i<len;i++,p++)
    printf("%02X ",*p);
  puts("");

  p = decode_run(buf,&run);

  printf("decoded run (%d bytes long)\t%d clusters @ %d\n",
	 (uint64_t)p - (uint64_t)buf, run.length, run.offset);

  return 0;
}
