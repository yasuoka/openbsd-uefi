/*	$OpenBSD: biosdev.c,v 1.57 2003/05/31 15:17:43 weingart Exp $	*/

/*
 * Copyright (c) 1996 Michael Shalayeff
 * Copyright (c) 2003 Tobias Weingartner
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

#include <sys/param.h>
#include <sys/reboot.h>
#include <sys/disklabel.h>
#include <machine/tss.h>
#include <machine/biosvar.h>
#include <lib/libsa/saerrno.h>
#include "disk.h"
#include "debug.h"
#include "libsa.h"
#include "biosdev.h"

static const char *biosdisk_err(u_int);
static int biosdisk_errno(u_int);

static int CHS_rw (int, int, int, int, int, int, void *);
static int EDD_rw (int, int, u_int64_t, u_int32_t, void *);

extern int debug;

#if 0
struct biosdisk {
	bios_diskinfo_t *bios_info;
	dev_t	bsddev;
	struct disklabel disklabel;
};
#endif

struct EDD_CB {
	u_int8_t  edd_len;   /* size of packet */
	u_int8_t  edd_res1;  /* reserved */
	u_int8_t  edd_nblk;  /* # of blocks to transfer */
	u_int8_t  edd_res2;  /* reserved */
	u_int16_t edd_off;   /* address of buffer (offset) */
	u_int16_t edd_seg;   /* address of buffer (segment) */
	u_int64_t edd_daddr; /* starting block */
};

/*
 * reset disk system
 */
static int
biosdreset(dev)
	int dev;
{
	int rv;
	__asm __volatile (DOINT(0x13) "; setc %b0" : "=a" (rv)
			  : "0" (0), "d" (dev) : "%ecx", "cc");
	return (rv & 0xff)? rv >> 8 : 0;
}

/*
 * Fill out a bios_diskinfo_t for this device.
 * Return 0 if all ok.
 * Return 1 if not ok.
 */
int
bios_getdiskinfo(dev, pdi)
	int dev;
	bios_diskinfo_t *pdi;
{
	u_int rv, secl, sech;

	/* Just reset, don't check return code */
	rv = biosdreset(dev);

#ifdef BIOS_DEBUG
	if (debug)
		printf("getinfo: try #8, %x,%p\n", dev, pdi);
#endif
	__asm __volatile (DOINT(0x13) "\n\t"
			  "setc %b0; movzbl %h1, %1\n\t"
			  "movzbl %%cl, %3; andb $0x3f, %b3\n\t"
			  "xchgb %%cl, %%ch; rolb $2, %%ch"
			  : "=a" (rv), "=d" (pdi->bios_heads),
			    "=c" (pdi->bios_cylinders),
			    "=b" (pdi->bios_sectors)
			  : "0" (0x0800), "1" (dev) : "cc");

#ifdef BIOS_DEBUG
	if (debug) {
		printf("getinfo: got #8\n");
		printf("disk 0x%x: %d,%d,%d\n", dev, pdi->bios_cylinders,
			pdi->bios_heads, pdi->bios_sectors);
	}
#endif
	if (rv & 0xff)
		return (1);

	/* Fix up info */
	pdi->bios_number = dev;
	pdi->bios_heads++;
	pdi->bios_cylinders &= 0x3ff;
	pdi->bios_cylinders++;

	/*
	 * NOTE: This seems to hang on certain machines.  Use function #8
	 * first, and verify with #21 IFF #8 succeeds first.
	 * Do not try this for floppy 0 (to support CD-ROM boot).
	 */
	if (dev) {
		__asm __volatile (DOINT(0x13) ";setc %b0"
				: "=a" (rv), "=d" (secl), "=c" (sech)
				: "0" (0x15FF), "1" (dev), "2" (0xFFFF)
				: "cc");
		if (!(rv & 0xff00))
			return (1);
		if (rv & 0xff)
			return (1);
	}

	/* NOTE:
	 * This currently hangs/reboots some machines
	 * The IBM Thinkpad 750ED for one.
	 *
	 * Funny that an IBM/MS extension would not be
	 * implemented by an IBM system...
	 *
	 * Future hangs (when reported) can be "fixed"
	 * with getSYSCONFaddr() and an exceptions list.
	 */
	if (dev & 0x80) {
		int bm;
		/* EDD support check */
		__asm __volatile(DOINT(0x13) "; setc %b0"
			 : "=a" (rv), "=c" (bm)
			 : "0" (0x4100), "b" (0x55aa), "d" (dev) : "cc");
		if (!(rv & 0xff) && (BIOS_regs.biosr_bx & 0xffff) == 0xaa55)
			pdi->bios_edd = (bm & 0xffff) | ((rv & 0xff) << 16);
		else
			pdi->bios_edd = -1;
	} else
		pdi->bios_edd = -1;

	/* Sanity check */
	if (!pdi->bios_cylinders || !pdi->bios_heads || !pdi->bios_sectors)
		return(1);

	/* CD-ROMs sometimes return heads == 1 */
	if (pdi->bios_heads < 2)
		return(1);

	return(0);
}

/*
 * Read/Write a block from given place using the BIOS.
 */
static __inline int
CHS_rw(rw, dev, cyl, head, sect, nsect, buf)
	int rw, dev, cyl, head;
	int sect, nsect;
	void * buf;
{
	int rv;
	BIOS_regs.biosr_es = (u_int32_t)buf >> 4;
	__asm __volatile ("movb %b7, %h1\n\t"
			  "movb %b6, %%dh\n\t"
			  "andl $0xf, %4\n\t"
			  /* cylinder; the highest 2 bits of cyl is in %cl */
			  "xchgb %%ch, %%cl\n\t"
			  "rorb  $2, %%cl\n\t"
			  "orb %b5, %%cl\n\t"
			  "inc %%cx\n\t"
			  DOINT(0x13) "\n\t"
			  "setc %b0"
			  : "=a" (rv)
			  : "0" (nsect), "d" (dev), "c" (cyl),
			    "b" (buf), "m" (sect), "m" (head),
			    "m" ((rw == F_READ)? 2: 3)
			  : "cc", "memory");

	return (rv & 0xff)? rv >> 8 : 0;
}

static __inline int
EDD_rw(rw, dev, daddr, nblk, buf)
	int rw, dev;
	u_int64_t daddr;
	u_int32_t nblk;
	void *buf;
{
	int rv;
	volatile static struct EDD_CB cb;

	/* Zero out reserved stuff */
	cb.edd_res1 = 0;
	cb.edd_res2 = 0;

	/* Fill in parameters */
	cb.edd_len = sizeof(cb);
	cb.edd_nblk = nblk;
	cb.edd_seg = ((u_int32_t)buf >> 4) & 0xffff;
	cb.edd_off = (u_int32_t)buf & 0xf;
	cb.edd_daddr = daddr;

	/* if offset/segment are zero, punt */
	if (!cb.edd_seg && !cb.edd_off)
		return (1);

	/* Call extended read/write (with disk packet) */
	BIOS_regs.biosr_ds = (u_int32_t)&cb >> 4;
	__asm __volatile (DOINT(0x13) "; setc %b0" : "=a" (rv)
			  : "0" ((rw == F_READ)? 0x4200: 0x4300),
			    "d" (dev), "S" ((int) (&cb) & 0xf) : "%ecx", "cc");
	return (rv & 0xff)? rv >> 8 : 0;
}

/*
 * Read given sector, handling retry/errors/etc.
 */
int
biosd_io(rw, bd, off, nsect, buf)
	int rw;
	bios_diskinfo_t *bd;
	daddr_t off;
	int nsect;
	void* buf;
{
	int dev = bd->bios_number;
	int j, error;
	void *bb;

	/* use a bounce buffer to not cross 64k DMA boundary */
	if ((((u_int32_t)buf) & ~0xffff) !=
	    (((u_int32_t)buf + nsect * DEV_BSIZE) & ~0xffff)) {
		/*
		 * XXX we believe that all the io is buffered
		 * by fs routines, so no big reads anyway
		 */
		bb = alloca(nsect * DEV_BSIZE);
		if (rw != F_READ)
			bcopy (buf, bb, nsect * DEV_BSIZE);
	} else
		bb = buf;

	/* Try to do operation up to 5 times */
	for (error = 1, j = 5; j-- && error;) {
		/* CHS or LBA access? */
		if (bd->bios_edd != -1) {
			error = EDD_rw(rw, dev, off, nsect, bb);
		} else {
			int cyl, head, sect;
			size_t i, n;
			char *p = bb;

			/* Handle track boundaries */
			for (error = i = 0; error == 0 && i < nsect;
 					i += n, off += n, p += n * DEV_BSIZE) {

				btochs(off, cyl, head, sect, bd->bios_heads, bd->bios_sectors);
				if ((sect + (nsect - i)) >= bd->bios_sectors)
					n = bd->bios_sectors - sect;
				else
					n = nsect - i;

				error = CHS_rw(rw, dev, cyl, head, sect, n, p);

				/* ECC corrected */
				if (error == 0x11)
					error = 0;
			}
		}
		switch (error) {
		case 0x00:	/* No errors */
		case 0x11:	/* ECC corrected */
			error = 0;
			break;

		default:	/* All other errors */
#ifdef BIOS_DEBUG
			if (debug)
				printf("\nBIOS error 0x%x (%s)\n",
					error, biosdisk_err(error));
#endif
			biosdreset(dev);
			break;
		}
	}

	if (bb != buf && rw == F_READ)
		bcopy (bb, buf, nsect * DEV_BSIZE);

#ifdef BIOS_DEBUG
	if (debug) {
		if (error != 0)
			printf("=0x%x(%s)", error, biosdisk_err(error));
		putchar('\n');
	}
#endif

	return (error);
}

/*
 * Try to read the bsd label on the given BIOS device
 */
const char *
bios_getdisklabel(bd, label)
	bios_diskinfo_t *bd;
	struct disklabel *label;
{
	daddr_t off = LABELSECTOR;
	char *buf;
	struct dos_mbr mbr;
	int error, i;

	/* Sanity check */
	if(bd->bios_heads == 0 || bd->bios_sectors == 0)
		return("failed to read disklabel");

	/* MBR is a harddisk thing */
	if (bd->bios_number & 0x80) {
		/* Read MBR */
		error = biosd_io(F_READ, bd, DOSBBSECTOR, 1, &mbr);
		if (error)
			return(biosdisk_err(error));

		/* check mbr signature */
		if (mbr.dmbr_sign != DOSMBR_SIGNATURE)
			return("bad MBR signature\n");

		/* Search for OpenBSD partition */
		for (off = 0, i = 0; off == 0 && i < NDOSPART; i++)
			if (mbr.dmbr_parts[i].dp_typ == DOSPTYP_OPENBSD)
				off = mbr.dmbr_parts[i].dp_start + LABELSECTOR;

		/* just in case */
		if (off == 0)
			for (off = 0, i = 0; off == 0 && i < NDOSPART; i++)
				if (mbr.dmbr_parts[i].dp_typ == DOSPTYP_NETBSD)
					off = mbr.dmbr_parts[i].dp_start + LABELSECTOR;

		if (off == 0)
			return("no BSD partition\n");
	} else
		off = LABELSECTOR;

	/* Load BSD disklabel */
	buf = alloca(DEV_BSIZE);
#ifdef BIOS_DEBUG
	if (debug)
		printf("loading disklabel @ %u\n", off);
#endif
	/* read disklabel */
	error = biosd_io(F_READ, bd, off, 1, buf);

	if(error)
		return("failed to read disklabel");

	/* Fill in disklabel */
	return (getdisklabel(buf, label));
}

int
biosopen(struct open_file *f, ...)
{
	va_list ap;
	register char	*cp, **file;
	dev_t	maj, unit, part;
	struct diskinfo *dip;
	int biosdev;

	va_start(ap, f);
	cp = *(file = va_arg(ap, char **));
	va_end(ap);

#ifdef BIOS_DEBUG
	if (debug)
		printf("%s\n", cp);
#endif

	f->f_devdata = NULL;
	/* search for device specification */
	cp += 2;
	if (cp[2] != ':') {
		if (cp[3] != ':')
			return ENOENT;
		else
			cp++;
	}

	for (maj = 0; maj < nbdevs && 
	     strncmp(*file, bdevs[maj], cp - *file); maj++);
	if (maj >= nbdevs) {
		printf("Unknown device: ");
		for (cp = *file; *cp != ':'; cp++)
			putchar(*cp);
		putchar('\n');
		return EADAPT;
	}

	/* get unit */
	if ('0' <= *cp && *cp <= '9')
		unit = *cp++ - '0';
	else {
		printf("Bad unit number\n");
		return EUNIT;
	}
	/* get partition */
	if ('a' <= *cp && *cp <= 'p')
		part = *cp++ - 'a';
	else {
		printf("Bad partition id\n");
		return EPART;
	}
		
	cp++;	/* skip ':' */
	if (*cp != 0)
		*file = cp;
	else
		f->f_flags |= F_RAW;

	biosdev = unit;
	switch (maj) {
	case 0:  /* wd */
	case 4:  /* sd */
	case 17: /* hd */
		biosdev |= 0x80;
		break;
	case 2:  /* fd */
		break;
	default:
		return ENXIO;
	}

	/* Find device */
	bootdev_dip = dip = dklookup(biosdev);

	/* Fix up bootdev */
	{ dev_t bsd_dev;
		bsd_dev = dip->bios_info.bsd_dev;
		dip->bsddev = MAKEBOOTDEV(B_TYPE(bsd_dev), B_ADAPTOR(bsd_dev),
			B_CONTROLLER(bsd_dev), unit, part);
		dip->bootdev = MAKEBOOTDEV(B_TYPE(bsd_dev), B_ADAPTOR(bsd_dev),
			B_CONTROLLER(bsd_dev), B_UNIT(bsd_dev), part);
	}

#if 0
	dip->bios_info.bsd_dev = dip->bootdev;
	bootdev = dip->bootdev;
#endif

#ifdef BIOS_DEBUG
	if (debug) {
		printf("BIOS geometry: heads=%u, s/t=%u; EDD=%d\n",
			dip->bios_info.bios_heads, dip->bios_info.bios_sectors,
			dip->bios_info.bios_edd);
	}
#endif

	/* Try for disklabel again (might be removable media) */
	if(dip->bios_info.flags & BDI_BADLABEL){
		const char *st = bios_getdisklabel((void *)biosdev, &dip->disklabel);
		if (debug && st)
			printf("%s\n", st);

		return ERDLAB;
	}

	f->f_devdata = dip;

	return 0;
}

const u_char bidos_errs[] = 
/* ignored	"\x00" "successful completion\0" */
		"\x01" "invalid function/parameter\0"
		"\x02" "address mark not found\0"
		"\x03" "write-protected\0"
		"\x04" "sector not found\0"
		"\x05" "reset failed\0"
		"\x06" "disk changed\0"
		"\x07" "drive parameter activity failed\0"
		"\x08" "DMA overrun\0"
		"\x09" "data boundary error\0"
		"\x0A" "bad sector detected\0"
		"\x0B" "bad track detected\0"
		"\x0C" "invalid media\0"
		"\x0E" "control data address mark detected\0"
		"\x0F" "DMA arbitration level out of range\0"
		"\x10" "uncorrectable CRC or ECC error on read\0"
/* ignored	"\x11" "data ECC corrected\0" */
		"\x20" "controller failure\0"
		"\x31" "no media in drive\0"
		"\x32" "incorrect drive type in CMOS\0"
		"\x40" "seek failed\0"
		"\x80" "operation timed out\0"
		"\xAA" "drive not ready\0"
		"\xB0" "volume not locked in drive\0"
		"\xB1" "volume locked in drive\0"
		"\xB2" "volume not removable\0"
		"\xB3" "volume in use\0"
		"\xB4" "lock count exceeded\0"
		"\xB5" "valid eject request failed\0"
		"\xBB" "undefined error\0"
		"\xCC" "write fault\0"
		"\xE0" "status register error\0"
		"\xFF" "sense operation failed\0"
		"\x00" "\0";

static const char *
biosdisk_err(error)
	u_int error;
{
	register const u_char *p = bidos_errs;

	while (*p && *p != error)
		while(*p++);

	return ++p;
}

const struct biosdisk_errors {
	u_char error;
	u_char errno;
} tab[] = {
	{ 0x01, EINVAL },
	{ 0x03, EROFS },
	{ 0x08, EINVAL },
	{ 0x09, EINVAL },
	{ 0x0A, EBSE },
	{ 0x0B, EBSE },
	{ 0x0C, ENXIO },
	{ 0x0D, EINVAL },
	{ 0x10, EECC },
	{ 0x20, EHER },	
	{ 0x31, ENXIO },
	{ 0x32, ENXIO },
	{ 0x00, EIO }
};
static int
biosdisk_errno(error)
	u_int error;
{
	register const struct biosdisk_errors *p;

	if (!error)
		return 0;

	for (p = tab; p->error && p->error != error; p++);

	return p->errno;
}

int
biosstrategy(devdata, rw, blk, size, buf, rsize)
	void *devdata;
	int rw;
	daddr_t blk;
	size_t size;
	void *buf;
	size_t *rsize;
{
	struct diskinfo *dip = (struct diskinfo *)devdata;
	bios_diskinfo_t *bd = &dip->bios_info;
	u_int8_t error = 0;
	size_t nsect;

	nsect = (size + DEV_BSIZE-1) / DEV_BSIZE;
	if (rsize != NULL)
		blk += dip->disklabel.
			d_partitions[B_PARTITION(dip->bsddev)].p_offset;

	/* Read all, sub-functions handle track boundaries */
	error = biosd_io(rw, bd, blk, nsect, buf);

#ifdef BIOS_DEBUG
	if (debug) {
		if (error != 0)
			printf("=0x%x(%s)", error, biosdisk_err(error));
		putchar('\n');
	}
#endif

	if (rsize != NULL)
		*rsize = nsect * DEV_BSIZE;

	return biosdisk_errno(error);
}

int
biosclose(f)
	struct open_file *f;
{
	f->f_devdata = NULL;
	return 0;
}

int
biosioctl(f, cmd, data)
	struct open_file *f;
	u_long cmd;
	void *data;
{
	return 0;
}

