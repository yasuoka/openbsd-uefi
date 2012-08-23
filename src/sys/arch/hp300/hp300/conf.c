/*	$OpenBSD: conf.c,v 1.54 2012/08/23 06:12:49 deraadt Exp $	*/
/*	$NetBSD: conf.c,v 1.39 1997/05/12 08:17:53 thorpej Exp $	*/

/*-
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
#include <sys/conf.h>
#include <sys/vnode.h>

#include "cd.h"
#include "ch.h"
#include "ct.h"
bdev_decl(ct);
#include "mt.h"
bdev_decl(mt);
#include "hd.h"
bdev_decl(hd);
#include "rd.h"
#include "sd.h"
#include "st.h"
#include "uk.h"
#include "vnd.h"

struct bdevsw	bdevsw[] =
{
	bdev_tape_init(NCT,ct),		/* 0: cs80 cartridge tape */
	bdev_tape_init(NMT,mt),		/* 1: magnetic reel tape */
	bdev_disk_init(NHD,hd),		/* 2: HPIB disk */
	bdev_swap_init(1,sw),		/* 3: swap pseudo-device */
	bdev_disk_init(NSD,sd),		/* 4: SCSI disk */
	bdev_notdef(),			/* 5: was: concatenated disk driver */
	bdev_disk_init(NVND,vnd),	/* 6: vnode disk driver */
	bdev_tape_init(NST,st),		/* 7: SCSI tape */
	bdev_disk_init(NRD,rd),		/* 8: RAM disk */
	bdev_disk_init(NCD,cd),		/* 9: SCSI CD-ROM */
	bdev_lkm_dummy(),		/* 10 */
	bdev_lkm_dummy(),		/* 11 */
	bdev_lkm_dummy(),		/* 12 */
	bdev_lkm_dummy(),		/* 13 */
	bdev_lkm_dummy(),		/* 14 */
	bdev_lkm_dummy(),		/* 15 */
};
int	nblkdev = nitems(bdevsw);

/* open, close, read, write, ioctl -- XXX should be a generic device */
#define	cdev_ppi_init(c,n) { \
	dev_init(c,n,open), dev_init(c,n,close), dev_init(c,n,read), \
	dev_init(c,n,write), dev_init(c,n,ioctl), (dev_type_stop((*))) nullop, \
	0, (dev_type_poll((*))) enodev, (dev_type_mmap((*))) enodev }

#include "audio.h"
#include "bio.h"
#define	mmread	mmrw
#define	mmwrite	mmrw
cdev_decl(mm);
#include "pty.h"
cdev_decl(ct);
cdev_decl(hd);
#include "ppi.h"
cdev_decl(ppi);
#include "dca.h"
cdev_decl(dca);
#include "apci.h"
cdev_decl(apci);
#include "dcm.h"
cdev_decl(dcm);
cdev_decl(mt);
cdev_decl(fd);
#include "bpfilter.h"
#include "tun.h"
#include "ksyms.h"
#include "wsdisplay.h"
#include "wskbd.h"
#include "wsmouse.h"
#include "wsmux.h"
#include "pf.h"
#include "systrace.h"
#include "vscsi.h"
#include "pppx.h"
#include "hotplug.h"

struct cdevsw	cdevsw[] =
{
	cdev_cn_init(1,cn),		/* 0: virtual console */
	cdev_ctty_init(1,ctty),		/* 1: controlling terminal */
	cdev_mm_init(1,mm),		/* 2: /dev/{null,mem,kmem,...} */
	cdev_notdef(),			/* 3 was /dev/drum */
	cdev_tty_init(NPTY,pts),	/* 4: pseudo-tty slave */
	cdev_ptc_init(NPTY,ptc),	/* 5: pseudo-tty master */
	cdev_log_init(1,log),		/* 6: /dev/klog */
	cdev_tape_init(NCT,ct),		/* 7: cs80 cartridge tape */
	cdev_disk_init(NSD,sd),		/* 8: SCSI disk */
	cdev_disk_init(NHD,hd),		/* 9: HPIB disk */
	cdev_notdef(),			/* 10 */
	cdev_ppi_init(NPPI,ppi),	/* 11: printer/plotter interface */
	cdev_tty_init(NDCA,dca),	/* 12: built-in single-port serial */
	cdev_notdef(),			/* 13: was console terminal emulator */
	cdev_notdef(),			/* 14: was human interface loop */
	cdev_tty_init(NDCM,dcm),	/* 15: 4-port serial */
	cdev_tape_init(NMT,mt),		/* 16: magnetic reel tape */
	cdev_notdef(),			/* 17: was: concatenated disk */
	cdev_disk_init(NCD,cd),		/* 18: SCSI CD-ROM */
	cdev_disk_init(NVND,vnd),	/* 19: vnode disk driver */
	cdev_tape_init(NST,st),		/* 20: SCSI tape */
	cdev_fd_init(1,filedesc),	/* 21: file descriptor pseudo-device */
	cdev_bpf_init(NBPFILTER,bpf),	/* 22: Berkeley packet filter */
	cdev_tun_init(NTUN,tun),	/* 23: network tunnel */
	cdev_lkm_init(NLKM,lkm),	/* 24: loadable module driver */
	cdev_lkm_dummy(),		/* 25 */
	cdev_lkm_dummy(),		/* 26 */
	cdev_lkm_dummy(),		/* 27 */
	cdev_lkm_dummy(),		/* 28 */
	cdev_lkm_dummy(),		/* 29 */
	cdev_lkm_dummy(),		/* 30 */
	cdev_lkm_dummy(),		/* 31 */
	cdev_random_init(1,random),	/* 32: random generator */
	cdev_pf_init(NPF,pf),		/* 33: packet filter */
	cdev_disk_init(NRD,rd),		/* 34: RAM disk */
	cdev_tty_init(NAPCI,apci),	/* 35: Apollo APCI UARTs */
	cdev_ksyms_init(NKSYMS,ksyms),	/* 36: Kernel symbols device */
	cdev_uk_init(NUK,uk),		/* 37 */
	cdev_notdef(),			/* 38 */
	cdev_ch_init(NCH,ch),		/* 39 */
	cdev_wsdisplay_init(NWSDISPLAY,wsdisplay), /* 40: frame buffers */
	cdev_mouse_init(NWSKBD,wskbd),	/* 41: keyboards */
	cdev_mouse_init(NWSMOUSE,wsmouse), /* 42: mice */
	cdev_mouse_init(NWSMUX,wsmux),	/* 43: ws multiplexor */
	cdev_audio_init(NAUDIO,audio),	/* 44: audio */
	cdev_notdef(),			/* 45 */
	cdev_notdef(),			/* 46 */
	cdev_notdef(),			/* 47 */
	cdev_notdef(),			/* 48 */
	cdev_bio_init(NBIO,bio),	/* 49: ioctl tunnel */
	cdev_systrace_init(NSYSTRACE,systrace),	/* 50 system call tracing */
	cdev_notdef(),			/* 51 */
	cdev_ptm_init(NPTY,ptm),	/* 52: pseudo-tty ptm device */
	cdev_vscsi_init(NVSCSI,vscsi),	/* 53: vscsi */
	cdev_disk_init(1,diskmap),	/* 54: disk mapper */
	cdev_pppx_init(NPPPX,pppx),	/* 55: pppx */
	cdev_hotplug_init(NHOTPLUG,hotplug),	/* 56: devices hot plugging */
};
int	nchrdev = nitems(cdevsw);

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
	/*  7 */	0,		/* ct */
	/*  8 */	4,		/* sd */
	/*  9 */	2,		/* hd */
	/* 10 */	NODEV,
	/* 11 */	NODEV,
	/* 12 */	NODEV,
	/* 13 */	NODEV,
	/* 14 */	NODEV,
	/* 15 */	NODEV,
	/* 16 */	1,		/* mt */
	/* 17 */	NODEV,
	/* 18 */	9,		/* cd */
	/* 19 */	6,		/* vnd */
	/* 20 */	7,		/* st */
	/* 21 */	NODEV,
	/* 22 */	NODEV,
	/* 23 */	NODEV,
	/* 24 */	NODEV,
	/* 25 */	NODEV,
	/* 26 */	NODEV,
	/* 27 */	NODEV,
	/* 28 */	NODEV,
	/* 29 */	NODEV,
	/* 30 */	NODEV,
	/* 31 */	NODEV,
	/* 32 */	NODEV,
	/* 33 */	NODEV,
	/* 34 */	8,		/* rd */
};
int nchrtoblktbl = nitems(chrtoblktbl);

/*
 * This entire table could be autoconfig()ed but that would mean that
 * the kernel's idea of the console would be out of sync with that of
 * the standalone boot.  I think it best that they both use the same
 * known algorithm unless we see a pressing need otherwise.
 */
#include <dev/cons.h>

cons_decl(ws);

#define dcacnpollc		nullcnpollc
cons_decl(dca);

#define	apcicnpollc		nullcnpollc
cons_decl(apci);

#define dcmcnpollc		nullcnpollc
cons_decl(dcm);

struct	consdev constab[] = {
#if NWSDISPLAY > 0
	cons_init(ws),
#endif
#if NDCA > 0
	cons_init(dca),
#endif
#if NAPCI > 0
	cons_init(apci),
#endif
#if NDCM > 0
	cons_init(dcm),
#endif
	{ 0 },
};
