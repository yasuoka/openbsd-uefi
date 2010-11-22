/*	$OpenBSD: conf.c,v 1.51 2010/11/22 21:10:45 miod Exp $ */

/*-
 * Copyright (c) 1995 Theo de Raadt
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Copyright (c) 1991 The Regents of the University of California.
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
 * 3. Neither the name of the University nor the names of its contributors
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
 *      @(#)conf.c	7.9 (Berkeley) 5/28/91
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/vnode.h>

#include <machine/conf.h>

#include "st.h"
#include "sd.h"
#include "cd.h"
#include "ch.h"
#include "uk.h"
#ifdef notyet
#include "xd.h"
bdev_decl(xd);
#endif
#include "vnd.h"
#include "ccd.h"
#include "rd.h"

struct bdevsw	bdevsw[] =
{
	bdev_notdef(),			/* 0 */
	bdev_notdef(),			/* 1 */
	bdev_notdef(),			/* 2 */
	bdev_swap_init(1,sw),		/* 3: swap pseudo-device */
	bdev_disk_init(NSD,sd),		/* 4: SCSI disk */
	bdev_disk_init(NCCD,ccd),	/* 5: concatenated disk driver */
	bdev_disk_init(NVND,vnd),	/* 6: vnode disk driver */
	bdev_tape_init(NST,st),		/* 7: SCSI tape */
	bdev_disk_init(NCD,cd),		/* 8: SCSI CD-ROM */
	bdev_disk_init(NRD,rd),		/* 9: RAM disk - for install tape */
#ifdef notyet
	bdev_disk_init(NXD,xd),		/* 10: XD disk */
#else
	bdev_notdef(),			/* 10 */
#endif
	bdev_notdef(),			/* 11 */
	bdev_notdef(),			/* 12 */
	bdev_lkm_dummy(),		/* 13 */
	bdev_lkm_dummy(),		/* 14 */
	bdev_lkm_dummy(),		/* 15 */
	bdev_lkm_dummy(),		/* 16 */
	bdev_lkm_dummy(),		/* 17 */
	bdev_lkm_dummy(),		/* 18 */
};
int	nblkdev = sizeof(bdevsw) / sizeof(bdevsw[0]);

#include "sram.h"
#include "vmel.h"
#include "vmes.h"
#include "nvram.h"
#include "flash.h"

#include "bio.h"
#include "pty.h"
cdev_decl(fd);

#include "cl.h"
#include "dart.h"
#include "wl.h"
#include "zs.h"

/* open, close, write, ioctl */
#define	cdev_lp_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), (dev_type_read((*))) enodev, \
	dev_init(c,n,write), dev_init(c,n,ioctl), (dev_type_stop((*))) enodev, \
	0, seltrue, (dev_type_mmap((*))) enodev }

/* open, close, ioctl, mmap, ioctl */
#define	cdev_mdev_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	dev_init(c,n,write), dev_init(c,n,ioctl), \
	(dev_type_stop((*))) enodev, 0, (dev_type_poll((*))) enodev, \
	dev_init(c,n,mmap) }

#include "lp.h"
#include "lptwo.h"
cdev_decl(lptwo);
#ifdef NNPFS
#include <nnpfs/nnnpfs.h>
cdev_decl(nnpfs_dev);
#endif
#include "ksyms.h"

#ifdef notyet
cdev_decl(xd);
#endif

#include "bpfilter.h"
#include "tun.h"

#include "pf.h"

#include "systrace.h"

#include "vscsi.h"
#include "pppx.h"

struct cdevsw	cdevsw[] =
{
	cdev_cn_init(1,cn),		/* 0: virtual console */
	cdev_ctty_init(1,ctty),		/* 1: controlling terminal */
	cdev_mm_init(1,mm),		/* 2: /dev/{null,mem,kmem,...} */
	cdev_notdef(),			/* 3 was /dev/drum */
	cdev_tty_init(NPTY,pts),	/* 4: pseudo-tty slave */
	cdev_ptc_init(NPTY,ptc),	/* 5: pseudo-tty master */
	cdev_log_init(1,log),		/* 6: /dev/klog */
	cdev_mdev_init(NSRAM,sram),	/* 7: /dev/sramX */
	cdev_disk_init(NSD,sd),		/* 8: SCSI disk */
	cdev_disk_init(NCD,cd),		/* 9: SCSI CD-ROM */
	cdev_mdev_init(NNVRAM,nvram),	/* 10: /dev/nvramX */
	cdev_mdev_init(NFLASH,flash),	/* 11: /dev/flashX */
	cdev_tty_init(NZS,zs),		/* 12: SCC serial (tty[a-d]) */
	cdev_tty_init(NCL,cl),		/* 13: CL-CD2400 serial (tty0[0-3]) */
	cdev_tty_init(NDART,dart),	/* 14: MC68681 serial (ttyd[0-1]) */
	cdev_notdef(),			/* 15 */
	cdev_notdef(),			/* 16 */
	cdev_disk_init(NCCD,ccd),	/* 17: concatenated disk */
	cdev_disk_init(NRD,rd),		/* 18: ramdisk device */
	cdev_disk_init(NVND,vnd),	/* 19: vnode disk */
	cdev_tape_init(NST,st),		/* 20: SCSI tape */
	cdev_fd_init(1,filedesc),	/* 21: file descriptor pseudo-dev */
	cdev_bpf_init(NBPFILTER,bpf),	/* 22: berkeley packet filter */
	cdev_tun_init(NTUN,tun),	/* 23: network tunnel */
	cdev_lkm_init(NLKM,lkm),	/* 24: loadable module driver */
	cdev_notdef(),			/* 25 */
#ifdef notyet
	cdev_disk_init(NXD,xd),		/* 26: XD disk */
#else
	cdev_notdef(),			/* 26 */
#endif
	cdev_bio_init(NBIO,bio),	/* 27: ioctl tunnel */
	cdev_lp_init(NLP,lp),		/* 28: lp */
	cdev_lp_init(NLPTWO,lptwo),	/* 29: lptwo */
	cdev_tty_init(NWL,wl),		/* 30: WG CL-CD2400 serial (ttywX) */
	cdev_mdev_init(NVMEL,vmel),	/* 31: /dev/vmelX */
	cdev_mdev_init(NVMES,vmes),	/* 32: /dev/vmesX */
	cdev_lkm_dummy(),		/* 33 */
	cdev_lkm_dummy(),		/* 34 */
	cdev_lkm_dummy(),		/* 35 */
	cdev_lkm_dummy(),		/* 36 */
	cdev_lkm_dummy(),		/* 37 */
	cdev_lkm_dummy(),		/* 38 */
	cdev_pf_init(NPF,pf),		/* 39: packet filter */
	cdev_random_init(1,random),	/* 40: random data source */
	cdev_uk_init(NUK,uk),		/* 41: unknown SCSI */
	cdev_notdef(),			/* 42 */
	cdev_ksyms_init(NKSYMS,ksyms),	/* 43: Kernel symbols device */
	cdev_ch_init(NCH,ch),		/* 44: SCSI autochanger */
	cdev_lkm_dummy(),		/* 45 */
	cdev_lkm_dummy(),		/* 46 */
	cdev_lkm_dummy(),		/* 47 */
	cdev_lkm_dummy(),		/* 48 */
	cdev_lkm_dummy(),		/* 49 */
	cdev_systrace_init(NSYSTRACE,systrace),	/* 50 system call tracing */
#ifdef NNPFS
	cdev_nnpfs_init(NNNPFS,nnpfs_dev),	/* 51: nnpfs communication device */
#else
	cdev_lkm_dummy(),		/* 51 */
#endif
	cdev_ptm_init(NPTY,ptm),	/* 52: pseudo-tty ptm device */
	cdev_vscsi_init(NVSCSI,vscsi),	/* 53: vscsi */
	cdev_disk_init(1,diskmap),	/* 54: disk mapper */
	cdev_pppx_init(NPPPX,pppx),	/* 55: pppx */
};
int	nchrdev = sizeof(cdevsw) / sizeof(cdevsw[0]);

int	mem_no = 2; 	/* major device number of memory special file */

/*
 * Swapdev is a fake device implemented
 * in sw.c used only internally to get to swstrategy.
 * It cannot be provided to the users, because the
 * swstrategy routine munches the b_dev and b_blkno entries
 * before calling the appropriate driver.  This would horribly
 * confuse, e.g. the hashing routines. Instead, /dev/drum is
 * provided as a character (raw) device.
 */
dev_t	swapdev = makedev(3, 0);

/*
 * Returns true if dev is /dev/mem or /dev/kmem.
 */
int
iskmemdev(dev)
	dev_t dev;
{

	return (major(dev) == mem_no && minor(dev) < 2);
}

/*
 * Returns true if dev is /dev/zero.
 */
int
iszerodev(dev)
	dev_t dev;
{

	return (major(dev) == mem_no && minor(dev) == 12);
}

dev_t
getnulldev()
{
	return makedev(mem_no, 2);
}

int chrtoblktbl[] = {
	/* XXXX This needs to be dynamic for LKMs. */
	/*VCHR*/	/*VBLK*/
	/*  0 */	NODEV,
	/*  1 */	NODEV,
	/*  2 */	NODEV,
	/*  3 */	NODEV,
	/*  4 */	NODEV,
	/*  5 */	NODEV,
	/*  6 */	NODEV,
	/*  7 */	NODEV,
	/*  8 */	4,		/* sd */
	/*  9 */	8,		/* cd */
	/* 10 */	NODEV,
	/* 11 */	NODEV,
	/* 12 */	NODEV,
	/* 13 */	NODEV,
	/* 14 */	NODEV,
	/* 15 */	NODEV,
	/* 16 */	NODEV,
	/* 17 */	5,		/* ccd */
	/* 18 */	9,		/* rd */
	/* 19 */	6,		/* vnd */
	/* 20 */	7,		/* st */
	/* 21 */	NODEV,
	/* 22 */	NODEV,
	/* 23 */	NODEV,
	/* 24 */	NODEV,
	/* 25 */	NODEV,
	/* 26 */	10,		/* xd */
};
int nchrtoblktbl = sizeof(chrtoblktbl) / sizeof(chrtoblktbl[0]);

#include <dev/cons.h>

#define clcnpollc	nullcnpollc
cons_decl(cl);
#define dartcnpollc	nullcnpollc
cons_decl(dart);
#define zscnpollc	nullcnpollc
cons_decl(zs);

struct	consdev constab[] = {
#if NCL > 0
	cons_init(cl),
#endif
#if NDART > 0
	cons_init(dart),
#endif
#if NZS > 0
	cons_init(zs),
#endif
	{ 0 },
};
