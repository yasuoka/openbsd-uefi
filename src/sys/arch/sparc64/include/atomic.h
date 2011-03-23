/*	$OpenBSD: atomic.h,v 1.7 2011/03/23 16:54:37 pirofti Exp $	*/
/*
 * Copyright (c) 2007 Artur Grabowski <art@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _MACHINE_ATOMIC_H_
#define _MACHINE_ATOMIC_H_

#if defined(_KERNEL)

static __inline unsigned int
sparc64_cas(volatile unsigned int *uip, unsigned int expect, unsigned int new)
{
	__asm __volatile("cas [%2], %3, %0"
	    : "+r" (new), "=m" (*uip)
	    : "r" (uip), "r" (expect), "m" (*uip));

	return (new);
}

static __inline unsigned long
sparc64_casx(volatile unsigned long *uip, unsigned long expect,
    unsigned long new)
{
	__asm __volatile("casx [%2], %3, %0"
	    : "+r" (new), "=m" (*uip)
	    : "r" (uip), "r" (expect), "m" (*uip));

	return (new);
}

static __inline void
atomic_setbits_int(volatile unsigned int *uip, unsigned int v)
{
	volatile unsigned int e, r;

	r = *uip;
	do {
		e = r;
		r = sparc64_cas(uip, e, e | v);
	} while (r != e);
}

static __inline void
atomic_clearbits_int(volatile unsigned int *uip, unsigned int v)
{
	volatile unsigned int e, r;

	r = *uip;
	do {
		e = r;
		r = sparc64_cas(uip, e, e & ~v);
	} while (r != e);
}

static __inline void
atomic_add_ulong(volatile unsigned long *ulp, unsigned long v)
{
	volatile unsigned long e, r;

	r = *ulp;
	do {
		e = r;
		r = sparc64_casx(ulp, e, e + v);
	} while (r != e);
}

#endif /* defined(_KERNEL) */
#endif /* _MACHINE_ATOMIC_H_ */
