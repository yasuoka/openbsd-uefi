/*	$OpenBSD: biosvar.h,v 1.22 1997/10/24 06:49:19 mickey Exp $	*/

/*
 * Copyright (c) 1997 Michael Shalayeff
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
 *	This product includes software developed by Michael Shalayeff.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR 
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifndef _I386_BIOSVAR_H_
#define _I386_BIOSVAR_H_

#define	BOOT_APIVER	0x00000002

/* BIOS media ID */
#define BIOSM_F320K	0xff	/* floppy ds/sd  8 spt */
#define	BIOSM_F160K	0xfe	/* floppy ss/sd  8 spt */
#define	BIOSM_F360K	0xfd	/* floppy ds/sd  9 spt */
#define	BIOSM_F180K	0xfc	/* floppy ss/sd  9 spt */
#define	BIOSM_ROMD	0xfa	/* ROM disk */
#define	BIOSM_F120M	0xf9	/* floppy ds/hd 15 spt 5.25" */
#define	BIOSM_F720K	0xf9	/* floppy ds/dd  9 spt 3.50" */
#define	BIOSM_HD	0xf8	/* hard drive */
#define	BIOSM_F144K	0xf0	/* floppy ds/hd 18 spt 3.50" */
#define	BIOSM_OTHER	0xf0	/* any other */

/*
 * BIOS memory maps
 */
#define	BIOS_MAP_END	0x00	/* End of array XXX - special */
#define	BIOS_MAP_FREE	0x01	/* Usable memory */
#define	BIOS_MAP_RES	0x02	/* Reserved memory */
#define	BIOS_MAP_ACPI	0x03	/* ACPI Reclaim memory */
#define	BIOS_MAP_NVS	0x04	/* ACPI NVS memory */

/* 
 * CTL_BIOS definitions.
 */
#define	BIOS_DEV		1	/* int: BIOS boot device */
#define	BIOS_DISKINFO		2	/* struct: BIOS boot device info */
#define	BIOS_CNVMEM		3	/* int: amount of conventional memory */
#define	BIOS_EXTMEM		4	/* int: amount of extended memory */
#define	BIOS_MAXID		5	/* number of valid machdep ids */

#define	CTL_BIOS_NAMES { \
	{ 0, 0 }, \
	{ "biosdev", CTLTYPE_INT }, \
	{ "diskinfo", CTLTYPE_STRUCT }, \
	{ "cnvmem", CTLTYPE_INT }, \
	{ "extmem", CTLTYPE_INT }, \
}

#define	BOOTARG_MEMMAP 0
typedef struct _bios_memmap {
	u_int64_t addr;		/* Beginning of block */
	u_int64_t size;		/* Size of block */
	u_int32_t type;		/* Type of block */
} bios_memmap_t;

/* Info about disk from the bios, plus the mapping from
 * BIOS numbers to BSD major (driver?) number.
 *
 * Also, do not bother with BIOSN*() macros, just parcel
 * the info out, and use it like this.  This makes for less
 * of a dependance on BIOSN*() macros having to be the same
 * across /boot, /bsd, and userland.
 */
#define	BOOTARG_DISKINFO 1
typedef struct _bios_diskinfo {
	/* BIOS section */
	int bios_number;	/* BIOS number of drive (or -1) */
	u_int bios_cylinders;	/* BIOS cylinders */
	u_int bios_heads;	/* BIOS heads */
	u_int bios_sectors;	/* BIOS sectors */
	int bios_edd;		/* EDD support */

	/* BSD section */
	dev_t bsd_dev;		/* BSD device */

	/* Checksum section */
	u_int32_t checksum;	/* Checksum for drive */
	u_int checklen;		/* Number of sectors done */

} bios_diskinfo_t;

#define	BOOTARG_APMINFO 2
typedef struct _bios_apminfo {
	/* APM_CONNECT returned values */
	u_int	apm_detail;
	u_int	apm_code32_base;
	u_int	apm_code16_base;
	u_int	apm_code_len;
	u_int	apm_data_base;
	u_int	apm_data_len;
	u_int	apm_entry;
} bios_apminfo_t;

#if defined(_KERNEL) || defined (_STANDALONE)

#ifdef _LOCORE
#define	DOINT(n)	int	$0x20+(n)
#else
#define	DOINT(n)	"int $0x20+(" #n ")"

extern struct BIOS_regs {
	u_int32_t	biosr_ax;
	u_int32_t	biosr_cx;
	u_int32_t	biosr_dx;
	u_int32_t	biosr_bx;
	u_int32_t	biosr_bp;
	u_int32_t	biosr_si;
	u_int32_t	biosr_di;
	u_int32_t	biosr_ds;
	u_int32_t	biosr_es;
}	BIOS_regs;

#ifdef _KERNEL
#include <machine/bus.h>

struct bios_attach_args {
	char *bios_dev;
	u_int bios_func;
	bus_space_tag_t bios_iot;
	bus_space_tag_t bios_memt;
	union {
		void *_p;
		bios_apminfo_t *_bios_apmp;
	} _;
};

#define	bios_apmp	_._bios_apmp

struct consdev;
struct proc;

int bios_sysctl
	__P((int *, u_int, void *, size_t *, void *, size_t, struct proc *));

void bioscnprobe __P((struct consdev *));
void bioscninit __P((struct consdev *));
void bioscnputc __P((dev_t, int));
int bioscngetc __P((dev_t));
void bioscnpollc __P((dev_t, int));

#endif /* _KERNEL */
#endif /* _LOCORE */
#endif /* _KERNEL || _STANDALONE */

#endif /* _I386_BIOSVAR_H_ */
