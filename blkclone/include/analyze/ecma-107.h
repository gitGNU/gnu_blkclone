#ifndef ECMA_107_H
#define ECMA_107_H

/* ECMA 107 structures */
/* taken from ECMA 107 with additional information from other sources */

/* This file contains minimal, if any, original work, and exists
 *  for interoperability purposes.
 * I, Jacob Bachmeyer, therefore release it into the public domain.
 */

#include <stdint.h>

/* Extended Parameter Block for ECMA-107 compliant volumes */
struct ecma107_desc_epb {
  uint8_t  drvno;	// physical drive number
  uint8_t  rsrv_2;	// disk-check-needed flag under WinNT
  uint8_t  xtnd_sig;	// == 0x29
  uint32_t serno;	// volume serial number
  uint8_t  label[11];	// volume label
  uint8_t  fstype[8];	// filesystem type (string)
  uint8_t  code[448];	// boot code area
} __attribute__((packed));

/* Extended Parameter Block for FAT32 */
struct ecma107_desc_f32 {
  uint32_t spf;		// sectors per FAT
  uint16_t flags;	// "flags" (???)
  uint16_t version;	// "version" (???)
  uint32_t rdfc;	// first cluster of root directory
  uint16_t fssectno;	// "FS information" sector
  uint16_t shadno;	// sector containing copy of boot record
  uint8_t  rsrv_3[12];	// "reserved"
  uint8_t  drvno;	// physical drive number
  uint8_t  rsrv_2;	// disk-check-needed flag under WinNT
  uint8_t  xtnd_sig;	// == 0x29
  uint32_t serno;	// volume serial number
  uint8_t  label[11];	// volume label
  uint8_t  fstype[8];	// filesystem type (string)
  uint8_t  code[420];	// boot code area
} __attribute__((packed));

/* Extended Parameter Block for NTFS */
struct ecma107_desc_ntfs {
  uint8_t  drvno;	// physical drive number
  uint8_t  current_head;// zero
  uint8_t  xtnd_sig;	// == 0x80
  uint8_t  rsrv_2;	// reserved
  // above four bytes are officially unused
  int64_t scount64;	// 64-bit sector count
  int64_t MFTlcn;	// first cluster of $MFT
  int64_t MFTMlcn;	// first cluster of $MFTMirr
  int8_t  MFTreclen;	// clusters per MFT record
  uint8_t  rsrv_3a[3];	// "zero"
  int8_t  cpib;		// clusters per index block
  uint8_t  rsrv_3b[3];	// "zero"
  uint64_t serno;	// volume serial number
  uint32_t checksum;	// boot sector checksum; generally ignored
  uint8_t  code[426];	// boot code area
} __attribute__((packed));

/* (Extended) FDC Descriptor
 *   -- also with FAT32 info from Wikipedia
 *   -- also with NTFS info extracted from Linux 2.6.24.2 NTFS driver
 */

struct ecma107_desc {	// ECMA 107 filesystem header
  uint8_t  rsrv_1[3];	// reserved; JMP to boot code on PC
  uint8_t  sysid[8];	// system identifier
  uint16_t ssize;	// sector size in bytes
  uint8_t  spc;		// sectors per cluster
  uint16_t rscnt;	// number of reserved sectors
  uint8_t  fatcnt;	// number of FATs (normally 2)
  uint16_t rdecnt;	// number of entries in root directory
  uint16_t scnt_small;	// number of sectors iff less than 65536
  uint8_t  medesc;	// media descriptor byte
  uint16_t spf;		// sectors per FAT
  uint16_t spt;		// sectors per track
  uint16_t heads;	// number of heads
  uint32_t hscnt;	// count of hidden sectors
  uint32_t scnt;	// number of sectors iff more than 65535
  union {
    struct ecma107_desc_epb  epb;	// FAT12/FAT16 Extended Parameter Block
    struct ecma107_desc_f32  f32;	// FAT32 Extended Parameter Block
    struct ecma107_desc_ntfs ntfs;	// NTFS Extended Parameter Block
    uint8_t pad[474];	// padding to account for boot code in non-EPB case
  };
  uint16_t sig;		// == 0xAA55
} __attribute__((packed));

// formula from 6.3.4 in ECMA 107
//  NB! multiply evaluates RDE, SS
#define COMPUTE_SSA(RSC,NF,SF,RDE,SS)	\
  ((RSC)+((NF)*(SF))+(((RDE)*32)/(SS))+(!!(((RDE)*32)%(SS))))
// SSA == Size of System Area; also first sector of Data Region
static inline int ssa_from_ecma107_desc(struct ecma107_desc * desc)
{ return COMPUTE_SSA(desc->rscnt,desc->fatcnt,desc->spf,
		     desc->rdecnt,desc->ssize); }

//formula from D.3.3 in ECMA 107
// gives LBA of first sector in given cluster
#define CN_TO_LSN(CN,SC,SSA)	((((CN) - 2) * (SC)) + (SSA))

// assertion (neat trick from autoconf)
unsigned char ____assert_struct_ecma107_FDC_size_check
[ (sizeof(struct ecma107_desc) == 512) ? 0 : -512 ];

#endif
