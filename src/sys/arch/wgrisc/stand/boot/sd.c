/*	$NetBSD: sd.c,v 1.6 1995/06/28 10:22:35 jonathan Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Van Jacobson of Lawrence Berkeley Laboratory and Ralph Campbell.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)sd.c	8.1 (Berkeley) 6/10/93
 */

#include <stdarg.h>

#include <stand.h>
#include <sys/param.h>
#include <sys/disklabel.h>

int prom_seek __P((int, long, int));
int disk_read __P((int, char *, int, int));
int disk_open __P((char *, int));

struct	sd_softc {
	int	sc_fd;			/* PROM file id */
	int	sc_part;		/* disk partition number */
	struct	disklabel sc_label;	/* disk label for this disk */
};

int
sdstrategy(devdata, rw, bn, reqcnt, addr, cnt)
	void *devdata;
	int rw;
	daddr_t bn;
	u_int reqcnt;
	char *addr;
	u_int *cnt;	/* out: number of bytes transferred */
{
	struct sd_softc *sc = (struct sd_softc *)devdata;
	struct partition *pp = &sc->sc_label.d_partitions[sc->sc_part];
	int s;
	long offset;

	offset = (pp->p_offset + bn) * DEV_BSIZE;

	s = disk_read(sc->sc_fd, addr, offset, reqcnt);
	if (s < 0)
		return (-s);
	*cnt = s;
	return (0);
}

int
sdopen(struct open_file *f, ...)
{
	int ctlr, unit, part;

	struct sd_softc *sc;
	struct disklabel *lp;
	int i, fd;
	char *msg = "rd err";
	char buf[DEV_BSIZE];
	int cnt;
	daddr_t labelsector;
	static char device[] = "sd(0)";
	va_list ap;

	va_start(ap, f);

	ctlr = va_arg(ap, int);
	unit = va_arg(ap, int);
	part = va_arg(ap, int);
	if (unit >= 7 || part >= 16)
		return (ENXIO);

	device[3] = unit + '0';
	fd = disk_open(device, 0);
	if (fd < 0) {
		return (ENXIO);
	}

	sc = alloc(sizeof(struct sd_softc));
	bzero(sc, sizeof(struct sd_softc));
	f->f_devdata = (void *)sc;

	sc->sc_fd = fd;
	sc->sc_part = part;

	lp = &sc->sc_label;
	lp->d_secsize = DEV_BSIZE;
	lp->d_secpercyl = 1;
	lp->d_npartitions = MAXPARTITIONS;
	lp->d_partitions[part].p_offset = 0;
	lp->d_partitions[part].p_size = 0x7fff0000;

	labelsector = LABELSECTOR;

#ifdef USE_DOSBBSECTOR
	/* First check for any DOS partition table */
	i = sdstrategy(sc, F_READ, (daddr_t)DOSBBSECTOR, DEV_BSIZE, buf, &cnt);
	if (!(i || cnt != DEV_BSIZE)) {
		struct dos_partition dp, *dp2;
		bcopy(buf + DOSPARTOFF, &dp, NDOSPART * sizeof(dp));
		for (dp2=&dp, i=0; i < NDOSPART; i++, dp2++) {
			if (dp2->dp_size) {
				if((dp2->dp_typ == DOSPTYP_OPENBSD) ||
				   (dp2->dp_typ == DOSPTYP_FREEBSD)) {
					labelsector += dp2->dp_start;
					break;
				}
			}
		}
	}
	else {
		goto bad;
	}
#endif


	/* try to read disk label and partition table information */
	i = sdstrategy(sc, F_READ, (daddr_t)labelsector, DEV_BSIZE, buf, &cnt);
	if (i || cnt != DEV_BSIZE) {
		goto bad;
	}
	msg = getdisklabel(buf, lp);
	if (msg) {
		goto bad;
	}

	if (part >= lp->d_npartitions || lp->d_partitions[part].p_size == 0) {
		msg = "no part";
bad:
		printf("sd%d: %s\n", unit, msg);
		return (ENXIO);
	}
	return (0);
}

int
sdclose(f)
	struct open_file *f;
{
#ifdef FANCY
	free(f->f_devdata, sizeof(struct sd_softc));
	f->f_devdata = (void *)0;
#endif
	return (0);
}
