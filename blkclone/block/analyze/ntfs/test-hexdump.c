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
  fprintf(stderr,"usage: %s <NTFS image> <file number>\n",name);
  exit(1);
}

// very simple hexdump; pads file to 16-byte boundary with NUL
void hexdump(FILE * out, FILE * f)
{
  unsigned char bytes[16] = {0};
  unsigned char chars[16] = {0};
  uint64_t addr = 0;
  int i = 0;

  while (fread(&bytes, sizeof(char), 16, f)) {
    for (i=0;i<16;i++)
      chars[i] = (((bytes[i] > 0x1F)&&(bytes[i] < 0x7F)) ? bytes[i] : '.');
    fprintf(out,
	    "%08X:  %02X %02X %02X %02X %02X %02X %02X %02X"
	    " - %02X %02X %02X %02X %02X %02X %02X %02X  "
	    "|%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c|\n",
	    addr,
	    bytes[ 0], bytes[ 1], bytes[ 2], bytes[ 3],
	    bytes[ 4], bytes[ 5], bytes[ 6], bytes[ 7],
	    bytes[ 8], bytes[ 9], bytes[10], bytes[11],
	    bytes[12], bytes[13], bytes[14], bytes[15],
	    chars[ 0], chars[ 1], chars[ 2], chars[ 3],
	    chars[ 4], chars[ 5], chars[ 6], chars[ 7],
	    chars[ 8], chars[ 9], chars[10], chars[11],
	    chars[12], chars[13], chars[14], chars[15]);
    memset(bytes,0,16);
    addr += 16;
  }
}

int main(int argc, char ** argv)
{
  struct NTFS_volume_ctx * vol = NULL;
  FILE * fs = NULL;
  FILE * f = NULL;
  uint64_t recno = 0;
  if (argc!=3) testusage(argv[0]);

  recno = strtoull(argv[2],NULL,0);

  fs = fopen(argv[1],"r");
  if (!fs) fatal("open fs");

  vol = NTFSdrv_volinit(fs);
  if (!vol) fatal("NTFS volinit");
  fclose(fs);

  f = NTFSdrv_fopen(vol,recno);
  if (!f) fatal("NTFS fopen");

  hexdump(stdout,f);

  fclose(f);
  NTFSdrv_volclose(vol);

  return 0;
}
