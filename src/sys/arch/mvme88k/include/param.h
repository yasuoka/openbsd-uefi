/*	$OpenBSD: param.h,v 1.21 2001/09/28 20:46:09 miod Exp $ */
/*
 * Copyright (c) 1999 Steve Murphree, Jr.
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1982, 1986, 1990 The Regents of the University of California.
 * All rights reserved.
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
 * from: Utah $Hdr: machparam.h 1.11 89/08/14$
 *
 *	@(#)param.h	7.8 (Berkeley) 6/28/91
 *	$Id: param.h,v 1.21 2001/09/28 20:46:09 miod Exp $
 */
#ifndef _MACHINE_PARAM_H_
#define _MACHINE_PARAM_H_

#ifdef _KERNEL
#ifndef _LOCORE
#include <machine/cpu.h>
#endif	/* _LOCORE */
#endif

#define  _MACHINE       mvme88k
#define  MACHINE        "mvme88k"
#define  _MACHINE_ARCH  m88k
#define  MACHINE_ARCH   "m88k"
#define  MID_MACHINE    MID_M88K
/* Older value for MID_MACHINE */
#define	OLD_MID_MACHINE	151

/*
 * Round p (pointer or byte index) down to a correctly-aligned value
 * for all data types (int, long, ...).   The result is u_int and
 * must be cast to any desired pointer type. ALIGN() is used for
 * aligning stack, which needs to be on a double word boundary for
 * 88k.
 */

#define  ALIGNBYTES		15		/* 64 bit alignment */
#define  ALIGN(p)		(((u_int)(p) + ALIGNBYTES) & ~ALIGNBYTES)
#define  ALIGNED_POINTER(p,t)	((((u_long)(p)) & (sizeof(t)-1)) == 0)

#define NBPG		4096		/* bytes/page */
#define PGOFSET		(NBPG-1)	/* byte offset into page */
#define PGSHIFT		12		/* LOG2(NBPG) */

#define	PAGE_SHIFT	12
#define	PAGE_SIZE	(1 << PAGE_SHIFT)
#define	PAGE_MASK	(PAGE_SIZE - 1)

#define NPTEPG		(PAGE_SIZE / (sizeof(u_int)))

#define SEGSHIFT	22		/* LOG2(NBSEG) */
#define NBSEG		(1 << SEGSHIFT)	/* bytes/segment */
#define SEGOFSET	(NBSEG - 1)	/* byte offset into segment */

/*
 * 187 Bug uses the bottom 64k. We allocate ptes to map this into the
 * kernel. But when we link the kernel, we tell it to start linking
 * past this 64k. How does this change KERNBASE? XXX
 */

#define KERNBASE	0x0		/* start of kernel virtual */
#define BTOPKERNBASE	((u_long)KERNBASE >> PGSHIFT)

#define DEV_BSIZE	512
#define DEV_BSHIFT	9		/* log2(DEV_BSIZE) */
#define BLKDEV_IOSIZE	2048		/* Should this be changed? XXX */
#define MAXPHYS		(64 * 1024)	/* max raw I/O transfer size */

#define SSIZE		1		/* initial stack size/NBPG */
#define SINCR		1		/* increment of stack/NBPG */
#define USPACE		ctob(UPAGES)

#define UPAGES		8		/* pages of u-area */
#define UADDR		0xEEE00000	/* address of u */
#define UVPN		(UADDR>>PGSHIFT)	/* virtual page number of u */
#define KERNELSTACK	(UADDR+UPAGES*NBPG)	/* top of kernel stack */

#define PHYSIO_MAP_START	0xEEF00000
#define PHYSIO_MAP_SIZE		0x00100000
#define IOMAP_MAP_START		0xEF000000	/* VME etc */
#define IOMAP_SIZE		0x018F0000
#define NIOPMAP			32

/*
 * Constants related to network buffer management.
 * MCLBYTES must be no larger than the software page size, and,
 * on machines that exchange pages of input or output buffers with mbuf
 * clusters (MAPPED_MBUFS), MCLBYTES must also be an integral multiple
 * of the hardware page size.
 */
#define MSIZE		256		/* size of an mbuf */
#define MCLSHIFT	11		/* convert bytes to m_buf clusters */
#define MCLBYTES	(1 << MCLSHIFT)	/* size of a m_buf cluster */
#define MCLOFSET	(MCLBYTES - 1)	/* offset within a m_buf cluster */

#ifndef NMBCLUSTERS
#ifdef   GATEWAY
#define NMBCLUSTERS	1024		/* map size, max cluster allocation */
#else
#define NMBCLUSTERS	512		/* map size, max cluster allocation */
#endif
#endif

/*
 * Size of kernel malloc arena in logical pages
 */ 
#ifndef NKMEMCLUSTERS
#define NKMEMCLUSTERS	(4096*1024/PAGE_SIZE)
#endif

#define MSGBUFSIZE	PAGE_SIZE

/* pages ("clicks") to disk blocks */
#define ctod(x)			((x) << (PGSHIFT - DEV_BSHIFT))
#define dtoc(x)			((x) >> (PGSHIFT - DEV_BSHIFT))
#define dtob(x)			((x) << DEV_BSHIFT)

/* pages to bytes */
#define ctob(x)			((x) << PGSHIFT)

/* bytes to pages */
#define btoc(x)			(((unsigned)(x) + PAGE_MASK) >> PGSHIFT)

#define btodb(bytes)         /* calculates (bytes / DEV_BSIZE) */ \
	((unsigned)(bytes) >> DEV_BSHIFT)
#define dbtob(db)            /* calculates (db * DEV_BSIZE) */ \
	((unsigned)(db) << DEV_BSHIFT)

/*
 * Map a ``block device block'' to a file system block.
 * This should be device dependent, and should use the bsize
 * field from the disk label.
 * For now though just use DEV_BSIZE.
 */
#define bdbtofsb(bn)		((bn) / (BLKDEV_IOSIZE/DEV_BSIZE))

/*
 * Get interrupt glue.
 */
#include <machine/psl.h>
#include <machine/intr.h>

#ifdef   _KERNEL
extern int delay __P((int));
#define  DELAY(x)             delay(x)

extern int cputyp;
extern int cpumod;
#endif

/*
 * Values for the cputyp variable.
 */
#define CPU_187		0x187
#define CPU_188		0x188
#define CPU_197		0x197
#define CPU_8120	0x8120

#endif /* !_MACHINE_PARAM_H_ */


