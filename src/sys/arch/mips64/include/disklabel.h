/*	$OpenBSD: disklabel.h,v 1.8 2006/07/07 23:47:37 krw Exp $	*/
/*	$NetBSD: disklabel.h,v 1.1 1995/02/13 23:07:34 cgd Exp $	*/

/*
 * Copyright (c) 1994 Christopher G. Demetriou
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MACHINE_DISKLABEL_H_
#define _MACHINE_DISKLABEL_H_

#define	SGI_LABELSECTOR		1		/* sector containing label */
#define	SGI_LABELOFFSET		0		/* offset of label in sector */
#define	I386_LABELSECTOR	1		/* sector containing label */
#define	I386_LABELOFFSET	0		/* offset of label in sector */

#define LABELSECTOR		SGI_LABELSECTOR
#define LABELOFFSET		SGI_LABELOFFSET

#define	MAXPARTITIONS		16		/* number of partitions */
#define	RAW_PART		2		/* raw partition: xx?c */

/* DOS partition table -- located in boot block */
#define	DOSBBSECTOR	0		/* DOS boot block relative sector # */
#define	DOSPARTOFF	446
#define DOSACTIVE	0x80
#define	NDOSPART	4
#define DOSMBR_SIGNATURE 0xaa55
#define DOSMBR_SIGNATURE_OFF 0x1fe

struct dos_partition {
	u_int8_t	dp_flag;	/* bootstrap flags */
	u_int8_t	dp_shd;		/* starting head */
	u_int8_t	dp_ssect;	/* starting sector */
	u_int8_t	dp_scyl;	/* starting cylinder */
	u_int8_t	dp_typ;		/* partition type (see below) */
	u_int8_t	dp_ehd;		/* end head */
	u_int8_t	dp_esect;	/* end sector */
	u_int8_t	dp_ecyl;	/* end cylinder */
	u_int32_t	dp_start;	/* absolute starting sector number */
	u_int32_t	dp_size;	/* partition size in sectors */
};

/* Known DOS partition types. */
#define	DOSPTYP_UNUSED	0x00		/* Unused partition */
#define DOSPTYP_FAT12	0x01		/* 12-bit FAT */
#define DOSPTYP_FAT16S	0x04		/* 16-bit FAT, less than 32M */
#define DOSPTYP_EXTEND	0x05		/* Extended; contains sub-partitions */
#define DOSPTYP_FAT16B	0x06		/* 16-bit FAT, more than 32M */
#define DOSPTYP_FAT32	0x0b		/* 32-bit FAT */
#define DOSPTYP_FAT32L	0x0c		/* 32-bit FAT, LBA-mapped */
#define DOSPTYP_FAT16L	0x0e		/* 16-bit FAT, LBA-mapped */
#define DOSPTYP_EXTENDL 0x0f		/* Extended, LBA-mapped; contains sub-partitions */
#define DOSPTYP_ONTRACK	0x54
#define	DOSPTYP_LINUX	0x83		/* That other thing */
#define DOSPTYP_FREEBSD	0xa5		/* FreeBSD partition type */
#define DOSPTYP_OPENBSD	0xa6		/* OpenBSD partition type */
#define DOSPTYP_NETBSD	0xa9		/* NetBSD partition type */

/* Isolate the relevant bits to get sector and cylinder. */
#define	DPSECT(s)	((s) & 0x3f)
#define	DPCYL(c, s)	((c) + (((s) & 0xc0) << 2))

/* SGI */
struct devparms {
        u_int8_t        dp_skew;
        u_int8_t        dp_gap1;
        u_int8_t        dp_gap2;
        u_int8_t        dp_spares_cyl;
        u_int16_t       dp_cyls;
        u_int16_t       dp_shd0;
        u_int16_t       dp_trks0;
        u_int8_t        dp_ctq_depth;
        u_int8_t        dp_cylshi;
        u_int16_t       dp_unused;
        u_int16_t       dp_secs;
        u_int16_t       dp_secbytes;
        u_int16_t       dp_interleave;
        u_int32_t       dp_flags;
        u_int32_t       dp_datarate;
        u_int32_t       dp_nretries;
        u_int32_t       dp_mspw;
        u_int16_t       dp_xgap1;
        u_int16_t       dp_xsync;
        u_int16_t       dp_xrdly;
        u_int16_t       dp_xgap2;
        u_int16_t       dp_xrgate;
        u_int16_t       dp_xwcont;
} __packed;

struct sgilabel {
#define SGILABEL_MAGIC  0xbe5a941
	u_int32_t       magic;
	int16_t         root;
	int16_t         swap;
	char            bootfile[16];
	struct devparms dp;
	struct {
		char	name[8];
		int32_t	block;
		int32_t	bytes;
	} voldir[15];
	struct {
		int32_t	blocks;
		int32_t	first;
		int32_t	type;
	} partitions[MAXPARTITIONS];
	int32_t         checksum;
	int32_t         _pad;
} __packed;

#define SGI_PTYPE_VOLHDR        0
#define SGI_PTYPE_RAW           3
#define SGI_PTYPE_BSD           4
#define SGI_PTYPE_VOLUME        6
#define SGI_PTYPE_EFS           7
#define SGI_PTYPE_LVOL          8
#define SGI_PTYPE_RLVOL         9
#define SGI_PTYPE_XFS           10
#define SGI_PTYPE_XFSLOG        11
#define SGI_PTYPE_XLV           12
#define SGI_PTYPE_XVM           13


#include <sys/dkbad.h>
struct cpu_disklabel {
	int labelsector;
	union {
		struct {
			struct dos_partition dosparts[NDOSPART];
			struct dkbad bad;
		} _i386;
	} u;
};

#define DKBAD(x) ((x)->u._i386.bad)

#endif /* _MACHINE_DISKLABEL_H_ */
