/*	$OpenBSD: hd.c,v 1.1 1997/02/03 07:19:06 downsj Exp $	*/
/*	$NetBSD: rd.c,v 1.11 1996/12/21 21:34:40 thorpej Exp $	*/

/*
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1982, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
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
 * from: Utah Hdr: rd.c 1.20 92/12/21
 *
 *	@(#)rd.c	8.1 (Berkeley) 7/15/93
 */

/*
 * CS80/SS80 disk driver
 */
#include <sys/param.h>
#include <sys/disklabel.h>
#include "stand.h"
#include "samachdep.h"

#include <hp300/dev/hdreg.h>

struct	hd_iocmd hd_ioc;
struct	hd_rscmd hd_rsc;
struct	hd_stat hd_stat;
struct	hd_ssmcmd hd_ssmc;

struct	disklabel hdlabel;

struct	hdminilabel {
	u_short	npart;
	u_long	offset[MAXPARTITIONS];
};

struct	hd_softc {
	int	sc_ctlr;
	int	sc_unit;
	int	sc_part;
	char	sc_retry;
	char	sc_alive;
	short	sc_type;
	struct	hdminilabel sc_pinfo;
} hd_softc[NHPIB][NHD];

#define	HDRETRY		5

struct	hdidentinfo {
	short	ri_hwid;
	short	ri_maxunum;
	int	ri_nblocks;
} hdidentinfo[] = {
	{ HD7946AID,	0,	 108416 },
	{ HD9134DID,	1,	  29088 },
	{ HD9134LID,	1,	   1232 },
	{ HD7912PID,	0,	 128128 },
	{ HD7914PID,	0,	 258048 },
	{ HD7958AID,	0,	 255276 },
	{ HD7957AID,	0,	 159544 },
	{ HD7933HID,	0,	 789958 },
	{ HD9134LID,	1,	  77840 },
	{ HD7936HID,	0,	 600978 },
	{ HD7937HID,	0,	1116102 },
	{ HD7914CTID,	0,	 258048 },
	{ HD7946AID,	0,	 108416 },
	{ HD9134LID,	1,	   1232 },
	{ HD7957BID,	0,	 159894 },
	{ HD7958BID,	0,	 297108 },
	{ HD7959BID,	0,	 594216 },
	{ HD2200AID,	0,	 654948 },
	{ HD2203AID,	0,	1309896 }
};
int numhdidentinfo = sizeof(hdidentinfo) / sizeof(hdidentinfo[0]);

hdinit(ctlr, unit)
	int ctlr, unit;
{
	register struct hd_softc *rs = &hd_softc[ctlr][unit];
	u_char stat;

	rs->sc_type = hdident(ctlr, unit);
	if (rs->sc_type < 0)
		return (0);
	rs->sc_alive = 1;
	return (1);
}

hdreset(ctlr, unit)
	register int ctlr, unit;
{
	u_char stat;

	hd_ssmc.c_unit = C_SUNIT(0);
	hd_ssmc.c_cmd = C_SSM;
	hd_ssmc.c_refm = REF_MASK;
	hd_ssmc.c_fefm = FEF_MASK;
	hd_ssmc.c_aefm = AEF_MASK;
	hd_ssmc.c_iefm = IEF_MASK;
	hpibsend(ctlr, unit, C_CMD, &hd_ssmc, sizeof(hd_ssmc));
	hpibswait(ctlr, unit);
	hpibrecv(ctlr, unit, C_QSTAT, &stat, 1);
}

hdident(ctlr, unit)
	register int ctlr, unit;
{
	struct hd_describe desc;
	u_char stat, cmd[3];
	char name[7];
	register int id, i;

	id = hpibid(ctlr, unit);
	if ((id & 0x200) == 0)
		return(-1);
	for (i = 0; i < numhdidentinfo; i++)
		if (id == hdidentinfo[i].ri_hwid)
			break;
	if (i == numhdidentinfo)
		return(-1);
	id = i;
	hdreset(ctlr, unit);
	cmd[0] = C_SUNIT(0);
	cmd[1] = C_SVOL(0);
	cmd[2] = C_DESC;
	hpibsend(ctlr, unit, C_CMD, cmd, sizeof(cmd));
	hpibrecv(ctlr, unit, C_EXEC, &desc, 37);
	hpibrecv(ctlr, unit, C_QSTAT, &stat, sizeof(stat));
	bzero(name, sizeof(name));
	if (!stat) {
		register int n = desc.d_name;
		for (i = 5; i >= 0; i--) {
			name[i] = (n & 0xf) + '0';
			n >>= 4;
		}
	}
	/*
	 * Take care of a couple of anomolies:
	 * 1. 7945A and 7946A both return same HW id
	 * 2. 9122S and 9134D both return same HW id
	 * 3. 9122D and 9134L both return same HW id
	 */
	switch (hdidentinfo[id].ri_hwid) {
	case HD7946AID:
		if (bcmp(name, "079450", 6) == 0)
			id = HD7945A;
		else
			id = HD7946A;
		break;

	case HD9134LID:
		if (bcmp(name, "091340", 6) == 0)
			id = HD9134L;
		else
			id = HD9122D;
		break;

	case HD9134DID:
		if (bcmp(name, "091220", 6) == 0)
			id = HD9122S;
		else
			id = HD9134D;
		break;
	}
	return(id);
}

#ifdef COMPAT_NOLABEL
int hdcyloff[][8] = {
	{ 1, 143, 0, 143, 0,   0,   323, 503, },	/* 7945A */
	{ 1, 167, 0, 0,	  0,   0,   0,	 0,   },	/* 9134D */
	{ 0, 0,	  0, 0,	  0,   0,   0,	 0,   },	/* 9122S */
	{ 0, 71,  0, 221, 292, 542, 221, 0,   },	/* 7912P */
	{ 1, 72,  0, 72,  362, 802, 252, 362, },	/* 7914P */
	{ 1, 28,  0, 140, 167, 444, 140, 721, },	/* 7933H */
	{ 1, 200, 0, 200, 0,   0,   450, 600, },	/* 9134L */
	{ 1, 105, 0, 105, 380, 736, 265, 380, },	/* 7957A */
	{ 1, 65,  0, 65,  257, 657, 193, 257, },	/* 7958A */
	{ 1, 128, 0, 128, 518, 918, 388, 518, },	/* 7957B */
	{ 1, 44,  0, 44,  174, 496, 131, 174, },	/* 7958B */
	{ 1, 44,  0, 44,  218, 1022,174, 218, },	/* 7959B */
	{ 1, 37,  0, 37,  183, 857, 147, 183, },	/* 2200A */
	{ 1, 19,  0, 94,  112, 450, 94,	 788, },	/* 2203A */
	{ 1, 20,  0, 98,  117, 256, 98,	 397, },	/* 7936H */
	{ 1, 11,  0, 53,  63,  217, 53,	 371, },	/* 7937H */
};

struct hdcompatinfo {
	int	nbpc;
	int	*cyloff;
} hdcompatinfo[] = {
	NHD7945ABPT*NHD7945ATRK, hdcyloff[0],
	NHD9134DBPT*NHD9134DTRK, hdcyloff[1],
	NHD9122SBPT*NHD9122STRK, hdcyloff[2],
	NHD7912PBPT*NHD7912PTRK, hdcyloff[3],
	NHD7914PBPT*NHD7914PTRK, hdcyloff[4],
	NHD7958ABPT*NHD7958ATRK, hdcyloff[8],
	NHD7957ABPT*NHD7957ATRK, hdcyloff[7],
	NHD7933HBPT*NHD7933HTRK, hdcyloff[5],
	NHD9134LBPT*NHD9134LTRK, hdcyloff[6],
	NHD7936HBPT*NHD7936HTRK, hdcyloff[14],
	NHD7937HBPT*NHD7937HTRK, hdcyloff[15],
	NHD7914PBPT*NHD7914PTRK, hdcyloff[4],
	NHD7945ABPT*NHD7945ATRK, hdcyloff[0],
	NHD9122SBPT*NHD9122STRK, hdcyloff[2],
	NHD7957BBPT*NHD7957BTRK, hdcyloff[9],
	NHD7958BBPT*NHD7958BTRK, hdcyloff[10],
	NHD7959BBPT*NHD7959BTRK, hdcyloff[11],
	NHD2200ABPT*NHD2200ATRK, hdcyloff[12],
	NHD2203ABPT*NHD2203ATRK, hdcyloff[13],
};
int	nhdcompatinfo = sizeof(hdcompatinfo) / sizeof(hdcompatinfo[0]);
#endif					

char io_buf[MAXBSIZE];

hdgetinfo(rs)
	register struct hd_softc *rs;
{
	register struct hdminilabel *pi = &rs->sc_pinfo;
	register struct disklabel *lp = &hdlabel;
	char *msg, *getdisklabel();
	int hdstrategy(), err, savepart;
	size_t i;

	bzero((caddr_t)lp, sizeof *lp);
	lp->d_secsize = DEV_BSIZE;

	/* Disklabel is always from RAW_PART. */
	savepart = rs->sc_part;
	rs->sc_part = RAW_PART;
	err = hdstrategy(rs, F_READ, LABELSECTOR,
	    lp->d_secsize ? lp->d_secsize : DEV_BSIZE, io_buf, &i);
	rs->sc_part = savepart;

	if (err) {
		printf("hdgetinfo: hdstrategy error %d\n", err);
		return(0);
	}
	
	msg = getdisklabel(io_buf, lp);
	if (msg) {
		printf("hd(%d,%d,%d): WARNING: %s, ",
		       rs->sc_ctlr, rs->sc_unit, rs->sc_part, msg);
#ifdef COMPAT_NOLABEL
		{
			register struct hdcompatinfo *ci;

			printf("using old default partitioning\n");
			ci = &hdcompatinfo[rs->sc_type];
			pi->npart = 8;
			for (i = 0; i < pi->npart; i++)
				pi->offset[i] = ci->cyloff[i] * ci->nbpc;
		}
#else
		printf("defining `c' partition as entire disk\n");
		pi->npart = 3;
		pi->offset[0] = pi->offset[1] = -1;
		pi->offset[2] = 0;
#endif
	} else {
		pi->npart = lp->d_npartitions;
		for (i = 0; i < pi->npart; i++)
			pi->offset[i] = lp->d_partitions[i].p_size == 0 ?
				-1 : lp->d_partitions[i].p_offset;
	}
	return(1);
}

hdopen(f, ctlr, unit, part)
	struct open_file *f;
	int ctlr, unit, part;
{
	register struct hd_softc *rs;
	struct hdinfo *ri;

	if (ctlr >= NHPIB || hpibalive(ctlr) == 0)
		return (EADAPT);
	if (unit >= NHD)
		return (ECTLR);
	rs = &hd_softc[ctlr][unit];
	rs->sc_part = part;
	rs->sc_unit = unit;
	rs->sc_ctlr = ctlr;
	if (rs->sc_alive == 0) {
		if (hdinit(ctlr, unit) == 0)
			return (ENXIO);
		if (hdgetinfo(rs) == 0)
			return (ERDLAB);
	}
	if (part != RAW_PART &&     /* always allow RAW_PART to be opened */
	    (part >= rs->sc_pinfo.npart || rs->sc_pinfo.offset[part] == -1))
		return (EPART);
	f->f_devdata = (void *)rs;
	return (0);
}

hdclose(f)
	struct open_file *f;
{
	struct hd_softc *rs = f->f_devdata;

	/*
	 * Mark the disk `not alive' so that the disklabel
	 * will be re-loaded at next open.
	 */
	bzero(rs, sizeof(struct hd_softc));
	f->f_devdata = NULL;

	return (0);
}

hdstrategy(devdata, func, dblk, size, v_buf, rsize)
	void *devdata;
	int func;
	daddr_t dblk;
	size_t size;
	void *v_buf;
	size_t *rsize;
{
	char *buf = v_buf;
	struct hd_softc *rs = devdata;
	register int ctlr = rs->sc_ctlr;
	register int unit = rs->sc_unit;
	daddr_t blk;
	char stat;

	if (size == 0)
		return(0);

	/*
	 * Don't do partition translation on the `raw partition'.
	 */
	blk = (dblk + ((rs->sc_part == RAW_PART) ? 0 :
	    rs->sc_pinfo.offset[rs->sc_part]));

	rs->sc_retry = 0;
	hd_ioc.c_unit = C_SUNIT(0);
	hd_ioc.c_volume = C_SVOL(0);
	hd_ioc.c_saddr = C_SADDR;
	hd_ioc.c_hiaddr = 0;
	hd_ioc.c_addr = HDBTOS(blk);
	hd_ioc.c_nop2 = C_NOP;
	hd_ioc.c_slen = C_SLEN;
	hd_ioc.c_len = size;
	hd_ioc.c_cmd = func == F_READ ? C_READ : C_WRITE;
retry:
	hpibsend(ctlr, unit, C_CMD, &hd_ioc.c_unit, sizeof(hd_ioc)-2);
	hpibswait(ctlr, unit);
	hpibgo(ctlr, unit, C_EXEC, buf, size, func);
	hpibswait(ctlr, unit);
	hpibrecv(ctlr, unit, C_QSTAT, &stat, 1);
	if (stat) {
		if (hderror(ctlr, unit, rs->sc_part) == 0)
			return(EIO);
		if (++rs->sc_retry > HDRETRY)
			return(EIO);
		goto retry;
	}
	*rsize = size;

	return(0);
}

hderror(ctlr, unit, part)
	register int ctlr, unit;
	int part;
{
	register struct hd_softc *hd = &hd_softc[ctlr][unit];
	char stat;

	hd_rsc.c_unit = C_SUNIT(0);
	hd_rsc.c_sram = C_SRAM;
	hd_rsc.c_ram = C_RAM;
	hd_rsc.c_cmd = C_STATUS;
	hpibsend(ctlr, unit, C_CMD, &hd_rsc, sizeof(hd_rsc));
	hpibrecv(ctlr, unit, C_EXEC, &hd_stat, sizeof(hd_stat));
	hpibrecv(ctlr, unit, C_QSTAT, &stat, 1);
	if (stat) {
		printf("hd(%d,%d,0,%d): request status fail %d\n",
		       ctlr, unit, part, stat);
		return(0);
	}
	printf("hd(%d,%d,0,%d) err: vu 0x%x",
	       ctlr, unit, part, hd_stat.c_vu);
	if ((hd_stat.c_aef & AEF_UD) || (hd_stat.c_ief & (IEF_MD|IEF_RD)))
		printf(", block %d", hd_stat.c_blk);
	printf(", R0x%x F0x%x A0x%x I0x%x\n",
	       hd_stat.c_ref, hd_stat.c_fef, hd_stat.c_aef, hd_stat.c_ief);
	return(1);
}
