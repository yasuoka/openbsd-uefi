/*	$OpenBSD: types.h,v 1.14 2004/11/26 21:23:06 miod Exp $	*/
/*	$NetBSD: types.h,v 1.14 1998/08/13 02:10:49 eeh Exp $	*/

/*-
 * Copyright (c) 1990 The Regents of the University of California.
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
 *	@(#)types.h	7.5 (Berkeley) 3/9/91
 */

#ifndef	_MACHTYPES_H_
#define	_MACHTYPES_H_

#include <sys/cdefs.h>

#if defined(_KERNEL)
typedef struct label_t {
	int val[6];
} label_t;
#endif

typedef	unsigned long	vm_offset_t;
typedef	unsigned long	vm_size_t;

typedef vm_offset_t	paddr_t;
typedef vm_size_t	psize_t;
typedef vm_offset_t	vaddr_t;
typedef vm_size_t	vsize_t;

/*
 * Basic integral types.  Omit the typedef if
 * not possible for a machine/compiler combination.
 */
#define __BIT_TYPES_DEFINED__
typedef __signed char              int8_t;
typedef unsigned char			  uint8_t;
typedef unsigned char            u_int8_t;
typedef short                     int16_t;
typedef unsigned short			 uint16_t;
typedef unsigned short          u_int16_t;
typedef int                       int32_t;
typedef unsigned int			 uint32_t;
typedef unsigned int            u_int32_t;
/* LONGLONG */
typedef long long                 int64_t;
/* LONGLONG */
typedef unsigned long long       uint64_t;
typedef unsigned long long      u_int64_t;

typedef int32_t                 register_t;

#define	__HAVE_DEVICE_REGISTER

#endif	/* _MACHTYPES_H_ */
