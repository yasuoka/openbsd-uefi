/*	$OpenBSD: biosdev.c,v 1.13 1997/04/23 06:49:06 mickey Exp $	*/

/*
 * Copyright (c) 1996 Michael Shalayeff
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
#include <string.h>
#include <libsa.h>
#include "biosdev.h"

extern int debug;

struct biosdisk {
	u_int	dinfo;
	struct {
		u_int8_t		mboot[DOSPARTOFF];
		struct dos_partition	dparts[NDOSPART];
		u_int16_t		signature;
	}	mbr;
	struct disklabel disklabel;
	dev_t	bsddev;
	u_int8_t biosdev;
};

int
biosopen(struct open_file *f, ...)
{
	va_list ap;
	register char	*cp, **file;
	dev_t	maj, unit, part;
	register struct biosdisk *bd;
	daddr_t off = LABELSECTOR;
	u_int8_t *buf;
	int i;

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

	for (maj = 0; maj < NENTS(bdevs) && 
	     strncmp(*file, bdevs[maj], cp - *file); maj++);
	if (maj >= NENTS(bdevs)) {
		printf("Unknown device: ");
		for (cp = *file; *cp != ':'; cp++)
			putchar(*cp);
		putchar('\n');
		return ENXIO;
	}

	/* get unit */
	if ('0' <= *cp && *cp <= '9')
		unit = *cp++ - '0';
	else {
		printf("Bad unit number\n");
		return ENXIO;
	}
	/* get partition */
	if ('a' <= *cp && *cp <= 'p')
		part = *cp++ - 'a';
	else {
		printf("Bad partition id\n");
		return ENXIO;
	}
		
	cp++;	/* skip ':' */
	if (*cp != 0)
		*file = cp;
	else
		f->f_flags |= F_RAW;

	bd = alloc(sizeof(*bd));
	bzero(bd, sizeof(bd));
	/* XXX for exec stuff */
	bootdev = bd->bsddev = MAKEBOOTDEV(maj, 0, 0, unit, part);

	switch (maj) {
	case 0:  /* wd */
	case 4:  /* sd */
	case 17: /* hd */
		bd->biosdev = (u_int8_t)(unit | 0x80);
		break;
	case 2:  /* fd */
		bd->biosdev = (u_int8_t)unit;
		break;
	case 7:  /* mcd */
	case 15: /* scd */
	case 6:  /* cd */
	case 18: /* acd */
#ifdef DEBUG
		printf("no any CD supported at this time\n");
#endif
	case 3:  /* wt */
#ifdef DEBUG
		if (maj == 3)
			printf("Wangtek is unsupported\n");
#endif
	default:
		free(bd, 0);
		return ENXIO;
	}

	bd->dinfo = biosdinfo((dev_t)bd->biosdev);

#ifdef BIOS_DEBUG
	if (debug) {
		printf("BIOS geometry: heads: %u, s/t: %u\n",
			BIOSNHEADS(bd->dinfo), BIOSNSECTS(bd->dinfo));
	}
#endif

	if (maj == 0 || maj == 4) {	/* wd, sd */
		if ((errno = biosstrategy(bd, F_READ, DOSBBSECTOR,
		    DEV_BSIZE, &bd->mbr, NULL)) != 0) {
#ifdef DEBUG
			if (debug)
				printf("cannot read MBR\n");
#endif
			free(bd, 0);
			return errno;
		}

		/* check mbr signature */
		if (bd->mbr.signature != 0xaa55) {
#ifdef DEBUG
			if (debug)
				printf("bad MBR signature\n");
#endif
			free(bd, 0);
			return EINVAL;
		}

		for (off = 0, i = 0; off == 0 && i < NDOSPART; i++)
			if (bd->mbr.dparts[i].dp_typ == DOSPTYP_OPENBSD)
				off = bd->mbr.dparts[i].dp_start + LABELSECTOR;

		if (off == 0) {
#ifdef DEBUG
			if (debug)
				printf("no BSD partition\n");
#endif
			free(bd, 0);
			return EINVAL;
		}
	}

	buf = alloc(DEV_BSIZE);
#ifdef BIOS_DEBUG
	if (debug)
		printf("loading disklabel @ %u\n", off);
#endif
	/* read disklabel */
	if ((errno = biosstrategy(bd, F_READ, off,
	    DEV_BSIZE, buf, NULL)) != 0) {
#ifdef DEBUG
		if (debug)
			printf("failed to read disklabel\n");
#endif
		free(buf, 0);
		free(bd, 0);
		return errno;
	}

	if ((cp = getdisklabel(buf, &bd->disklabel)) != NULL) {
#ifdef DEBUG
		if (debug)
			printf("%s\n", cp);
#endif
		free(buf, 0);
		free(bd, 0);
		return EINVAL;
	}

	free(buf,0);
	f->f_devdata = bd;

	return 0;
}

/* BIOS disk errors translation table */
const struct bd_error {
	u_int8_t bd_id;
	int	unix_id;
	char	*msg;
} bd_errors[] = {
	{ 0x00, 0      , "successful completion" },
	{ 0x01, EINVAL , "invalid function or parameter" },
	{ 0x02, EIO    , "address mark not found" },
	{ 0x03, EROFS  , "disk write-protected" },
	{ 0x04, EIO    , "sector not found/read error" },
	{ 0x05, EIO    , "reset failed" },
	{ 0x06, EIO    , "disk changed" },
	{ 0x07, EIO    , "drive parameter activity failed" },
	{ 0x08, EINVAL , "DMA overrun" },
	{ 0x09, EINVAL , "data boundary error" },
	{ 0x0A, EIO    , "bad sector detected" },
	{ 0x0B, EIO    , "bad track detected" },
	{ 0x0C, ENXIO  , "unsupported track or invalid media" },
	{ 0x0D, EINVAL , "invalid number of sectors on format" },
	{ 0x0E, EIO    , "control data address mark detected" },
	{ 0x0F, EIO    , "DMA arbitration level out of range" },
	{ 0x10, EIO    , "uncorrectable CRC or ECC error on read" },
	{ 0x11, 0      , "data ECC corrected" },
	{ 0x20, EIO    , "controller failure" },
	{ 0x31, ENXIO  , "no media in drive" },
	{ 0x32, ENXIO  , "incorrect drive type stored in CMOS" },
	{ 0x40, EIO    , "seek failed" },
	{ 0x80, EIO    , "operation timed out" },
	{ 0xAA, EIO    , "drive not ready" },
	{ 0xB0, EIO    , "volume not locked in drive" },
	{ 0xB1, EIO    , "volume locked in drive" },
	{ 0xB2, EIO    , "volume not removable" },
	{ 0xB3, EDEADLK, "volume in use" },
	{ 0xB4, ENOLCK , "lock count exceeded" },
	{ 0xB5, EINVAL , "valid eject request failed" },
	{ 0xBB, EIO    , "undefined error" },
	{ 0xCC, EROFS  , "write fault" },
	{ 0xE0, EIO    , "status register error" },
	{ 0xFF, EIO    , "sense operation failed" }
};
int	bd_nents = NENTS(bd_errors);

int
biosstrategy(void *devdata, int rw,
	daddr_t blk, size_t size, void *buf, size_t *rsize)
{
	u_int8_t error = 0;
	register struct biosdisk *bd = (struct biosdisk *)devdata;
	register size_t i, nsect, n, spt;
	register const struct bd_error *p = bd_errors;

	nsect = (size + DEV_BSIZE-1) / DEV_BSIZE;
	if (rsize != NULL)
		blk += bd->disklabel.
			d_partitions[B_PARTITION(bd->bsddev)].p_offset;

#ifdef	BIOS_DEBUG
	if (debug)
		printf("biosstrategy(%p,%s,%u,%u,%p,%p), dev=%x:%x\nbiosread:",
		       bd, (rw==F_READ?"reading":"writing"), blk, size,
		       buf, rsize, bd->biosdev, bd->bsddev);
#endif

	/* handle floppies w/ different from drive geometry */
	if (!(bd->biosdev & 0x80) && bd->disklabel.d_nsectors != 0)
		spt = bd->disklabel.d_nsectors;
	else
		spt = BIOSNSECTS(bd->dinfo);

	for (i = 0; error == 0 && i < nsect;
	     i += n, blk += n, buf += n * DEV_BSIZE) {
		register int	cyl, hd, sect, j;
		void *bb;

		btochs(blk, cyl, hd, sect, BIOSNHEADS(bd->dinfo), spt);
		if ((sect + (nsect - i)) >= spt)
			n = spt - sect;
		else
			n = nsect - i;
		
		/* use a bounce buffer to not cross 64k DMA boundary */
		if ((((u_int32_t)buf) & ~0xffff) !=
		    (((u_int32_t)buf + n * DEV_BSIZE) & ~0xffff)) {
			bb = alloc(n * DEV_BSIZE);
			if (rw != F_READ)
				bcopy (buf, bb, n * DEV_BSIZE);
		} else
			bb = buf;
#ifdef	BIOS_DEBUG
		if (debug)
			printf(" (%d,%d,%d,%d)@%p", cyl, hd, sect, n, bb);
#endif
		/* Try to do operation up to 5 times */
		for (error = 1, j = 5; error && (j > 0); j--) {
			if(rw == F_READ)
				error = biosread(bd->biosdev, cyl, hd, sect, n, bb);
			else
				error = bioswrite(bd->biosdev, cyl, hd, sect, n, bb);

			switch (error) {
			case 0x00:	/* No errors */
			case 0x11:	/* ECC corrected */
				error = 0;
				break;

			default:	/* All other errors */
				for (p = bd_errors; p < &bd_errors[bd_nents] &&
					p->bd_id != error; p++);
				printf("\nBIOS error %x (%s)\n",
					p->bd_id, p->msg);
				biosdreset();
				break;
			}
		}

		if (bb != buf) {
			if (rw == F_READ)
				bcopy (bb, buf, n * DEV_BSIZE);
			free (bb, n * DEV_BSIZE);
		}
	}

#ifdef	BIOS_DEBUG
	if (debug) {
		if (error != 0) {
			for (p = bd_errors; p < &bd_errors[bd_nents] &&
			     p->bd_id != error; p++);
			printf("=%x(%s)", p->bd_id, p->msg);
		}
		putchar('\n');
	}
#endif

	if (rsize != NULL)
		*rsize = i * DEV_BSIZE;

	return p->unix_id;
}

int
biosclose(struct open_file *f)
{
	free(f->f_devdata, 0);
	return 0;
}

int
biosioctl(struct open_file *f, u_long cmd, void *data)
{

	return 0;
}

