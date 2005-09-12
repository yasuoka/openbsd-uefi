/*	$OpenBSD: param.h,v 1.3 2005/09/12 23:05:05 miod Exp $	*/

/* 
 * Copyright (c) 1988-1994, The University of Utah and
 * the Computer Systems Laboratory at the University of Utah (CSL).
 * All rights reserved.
 *
 * Permission to use, copy, modify and distribute this software is hereby
 * granted provided that (1) source code retains these copyright, permission,
 * and disclaimer notices, and (2) redistributions including binaries
 * reproduce the notices in supporting documentation, and (3) all advertising
 * materials mentioning features or use of this software display the following
 * acknowledgement: ``This product includes software developed by the
 * Computer Systems Laboratory at the University of Utah.''
 *
 * THE UNIVERSITY OF UTAH AND CSL ALLOW FREE USE OF THIS SOFTWARE IN ITS "AS
 * IS" CONDITION.  THE UNIVERSITY OF UTAH AND CSL DISCLAIM ANY LIABILITY OF
 * ANY KIND FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * CSL requests users of this software to return to csl-dist@cs.utah.edu any
 * improvements that they make and grant CSL redistribution rights.
 *
 * 	Utah $Hdr: param.h 1.18 94/12/16$
 */

#include <machine/cpu.h>

/*
 * Machine dependent constants for PA-RISC.
 */

#define	_MACHINE	hppa64
#define	MACHINE		"hppa64"
#define	_MACHINE_ARCH	hppa64
#define	MACHINE_ARCH	"hppa64"
#define	MID_MACHINE	MID_HPPA20

/*
 * Round p (pointer or byte index) up to a correctly-aligned value for all
 * data types (int, long, ...).   The result is u_int and must be cast to
 * any desired pointer type.
 */
#define	ALIGNBYTES	7
#define	ALIGN(p)	(((u_long)(p) + ALIGNBYTES) &~ ALIGNBYTES)
#define	ALIGNED_POINTER(p,t) ((((u_long)(p)) & (sizeof(t) - 1)) == 0)

#define	PAGE_SIZE	4096
#define	PAGE_MASK	(PAGE_SIZE-1)
#define	PAGE_SHIFT	12

#define	NBPG		4096		/* bytes/page */
#define	PGOFSET		(NBPG-1)	/* byte offset into page */
#define	PGSHIFT		12		/* LOG2(NBPG) */

#define	KERNBASE	0x00000000	/* start of kernel virtual */
#define	BTOPKERNBASE	((u_long)KERNBASE >> PAGE_SHIFT)

#define	DEV_BSIZE	512
#define	DEV_BSHIFT	9		/* log2(DEV_BSIZE) */
#define BLKDEV_IOSIZE	2048
#define	MAXPHYS		(64 * 1024)	/* max raw I/O transfer size */

#define	MACHINE_STACK_GROWS_UP	1	/* stack grows to higher addresses */

#define	USPACE		(4 * NBPG)	/* pages for user struct and kstack */
#define	USPACE_ALIGN	(0)		/* u-area alignment 0-none */

#ifndef	MSGBUFSIZE
#define	MSGBUFSIZE	2*NBPG		/* default message buffer size */
#endif

/*
 * Constants related to network buffer management.
 * MCLBYTES must be no larger than the software page size, and,
 * on machines that exchange pages of input or output buffers with mbuf
 * clusters (MAPPED_MBUFS), MCLBYTES must also be an integral multiple
 * of the hardware page size.
 */
#define	MSIZE		256		/* size of an mbuf */
#define	MCLSHIFT	11
#define	MCLBYTES	(1 << MCLSHIFT)	/* large enough for ether MTU */
#define	MCLOFSET	(MCLBYTES - 1)
#define	NMBCLUSTERS	4096		/* map size, max cluster allocation */

/*
 * Minimum and maximum sizes of the kernel malloc arena in PAGE_SIZE-sized
 * logical pages.
 */
#define	NKMEMPAGES_MIN_DEFAULT	((8 * 1024 * 1024) >> PAGE_SHIFT)
#define	NKMEMPAGES_MAX_DEFAULT	((128 * 1024 * 1024) >> PAGE_SHIFT)

/* pages ("clicks") (4096 bytes) to disk blocks */
#define	ctod(x)		((x) << (PAGE_SHIFT - DEV_BSHIFT))
#define	dtoc(x)		((x) >> (PAGE_SHIFT - DEV_BSHIFT))

/* pages to bytes */
#define	ctob(x)		((x) << PAGE_SHIFT)
#define	btoc(x)		(((x) + PAGE_MASK) >> PAGE_SHIFT)

#define	btodb(x)	((x) >> DEV_BSHIFT)
#define	dbtob(x)	((x) << DEV_BSHIFT)

/*
 * Map a ``block device block'' to a file system block.
 * This should be device dependent, and should use the bsize
 * field from the disk label.
 * For now though just use DEV_BSIZE.
 */
#define	bdbtofsb(bn)	((bn) / (BLKDEV_IOSIZE/DEV_BSIZE))

/*
 * Mach derived conversion macros
 */
#define hppa_round_page(x)	((((unsigned long)(x)) + NBPG - 1) & ~(NBPG-1))
#define hppa_trunc_page(x)	((unsigned long)(x) & ~(NBPG-1))

#define btop(x)		((unsigned long)(x) >> PAGE_SHIFT)
#define ptob(x)		((unsigned long)(x) << PAGE_SHIFT)

#ifndef _LOCORE
#define	CONADDR	conaddr
#define	CONUNIT	conunit
#define	COM_FREQ	7372800
extern hppa_hpa_t conaddr;
extern int conunit;
#endif

#define	__SWAP_BROKEN
