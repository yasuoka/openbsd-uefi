/*      $OpenBSD: param.h,v 1.29 2011/04/07 15:45:17 miod Exp $ */

/*
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department and Ralph Campbell.
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
 *	from: Utah Hdr: machparam.h 1.11 89/08/14
 *	from: @(#)param.h	8.1 (Berkeley) 6/10/93
 */

#ifndef _MIPS64_PARAM_H_
#define _MIPS64_PARAM_H_

#ifdef _KERNEL
#include <machine/cpu.h>
#endif

/*
 * Round p (pointer or byte index) up to a correctly-aligned value for all
 * data types (int, long, ...).   The result is u_long and must be cast to
 * any desired pointer type.
 */
#define	ALIGNBYTES	7
#define	ALIGN(p)	(((u_long)(p) + ALIGNBYTES) &~ ALIGNBYTES)
#define ALIGNED_POINTER(p, t)  ((((u_long)(p)) & (sizeof (t) - 1)) == 0)

#define PAGE_SIZE	(1 << PAGE_SHIFT)
#define PAGE_MASK	(PAGE_SIZE - 1)

#define	NBPG		PAGE_SIZE
#define	PGOFSET		PAGE_MASK
#define	PGSHIFT		PAGE_SHIFT

#define	KERNBASE	0xffffffff80000000L	/* start of kernel virtual */

#define	DEV_BSHIFT	9		/* log2(DEV_BSIZE) */
#define	DEV_BSIZE	(1 << DEV_BSHIFT)
#define BLKDEV_IOSIZE	2048
#define	MAXPHYS		(64 * 1024)	/* max raw I/O transfer size */

#define USPACE		(16384)
#ifdef _KERNEL
#define	UPAGES		(USPACE >> PAGE_SHIFT)
#if PAGE_SHIFT > 12
#define	USPACE_ALIGN	0
#else
#define	USPACE_ALIGN	(2 * PAGE_SIZE)	/* align to an even TLB boundary */
#endif
#endif	/* _KERNEL */

/*
 * Constants related to network buffer management.
 */
#define	NMBCLUSTERS	4096		/* map size, max cluster allocation */

#ifdef _KERNEL
#if PAGE_SHIFT > 12
#define	MSGBUFSIZE	PAGE_SIZE
#else
#define	MSGBUFSIZE	8192
#endif

/* Default malloc arena size */
#define	NKMEMPAGES_MIN_DEFAULT  ((8 * 1024 * 1024) >> PAGE_SHIFT)
#define	NKMEMPAGES_MAX_DEFAULT  ((128 * 1024 * 1024) >> PAGE_SHIFT)
#endif /* _KERNEL */

/* pages ("clicks") to disk blocks */
#define	ctod(x)	((x) << (PGSHIFT - DEV_BSHIFT))
#define	dtoc(x)	((x) >> (PGSHIFT - DEV_BSHIFT))

/* bytes to disk blocks */
#define	btodb(x)	((x) >> DEV_BSHIFT)
#define dbtob(x)	((x) << DEV_BSHIFT)

#ifdef _KERNEL
#ifndef _LOCORE
#define	DELAY(n)	delay(n)
void delay(int);
#endif
#endif /* _KERNEL */

#endif /* !_MIPS64_PARAM_H_ */
