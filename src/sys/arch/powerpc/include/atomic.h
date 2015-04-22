/*	$OpenBSD: atomic.h,v 1.8 2015/04/22 06:39:03 mpi Exp $	*/

/*
 * Copyright (c) 2015 Martin Pieuchot
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


#ifndef _POWERPC_ATOMIC_H_
#define _POWERPC_ATOMIC_H_

#if defined(_KERNEL)

static __inline void
atomic_setbits_int(volatile unsigned int *uip, unsigned int v)
{
	unsigned int tmp;

	__asm volatile (
	    "1:	lwarx	%0, 0, %2	\n"
	    "	or	%0, %1, %0	\n"
	    "	stwcx.	%0, 0, %2	\n"
	    "	bne-	1b		\n"
	    "	sync" : "=&r" (tmp) : "r" (v), "r" (uip) : "cc", "memory");
}

static __inline void
atomic_clearbits_int(volatile unsigned int *uip, unsigned int v)
{
	unsigned int tmp;

	__asm volatile (
	    "1:	lwarx	%0, 0, %2	\n"
	    "	andc	%0, %0, %1	\n"
	    "	stwcx.	%0, 0, %2	\n"
	    "	bne-	1b		\n"
	    "	sync" : "=&r" (tmp) : "r" (v), "r" (uip) : "cc", "memory");
}

static inline unsigned int
_atomic_cas_uint(volatile unsigned int *p, unsigned int o, unsigned int n)
{
	unsigned int rv;

	__asm volatile (
	    "1:	lwarx	%0, 0, %1	\n"
	    "	cmpw	0, %0, %3	\n"
	    "	bne-	2f		\n"
	    "	stwcx.	%2, 0, %1	\n" 
	    "	bne-	1b		\n"
	    "2:				\n"
	    : "=&r" (rv)
	    : "r" (p), "r" (n), "r" (o)
	    : "cc", "memory");

	return (rv);
}
#define atomic_cas_uint(_p, _o, _n) _atomic_cas_uint((_p), (_o), (_n))

static inline unsigned long
_atomic_cas_ulong(volatile unsigned long *p, unsigned long o, unsigned long n)
{
	unsigned long rv;

	__asm volatile (
	    "1:	lwarx	%0, 0, %1	\n"
	    "	cmpw	0, %0, %3	\n"
	    "	bne-	2f		\n"
	    "	stwcx.	%2, 0, %1	\n" 
	    "	bne-	1b		\n"
	    "2:				\n"
	    : "=&r" (rv)
	    : "r" (p), "r" (n), "r" (o)
	    : "cc", "memory");

	return (rv);
}
#define atomic_cas_ulong(_p, _o, _n) _atomic_cas_ulong((_p), (_o), (_n))

static inline void *
_atomic_cas_ptr(volatile void *pp, void *o, void *n)
{
	void * volatile *p = pp;
	void *rv;

	__asm volatile (
	    "1:	lwarx	%0, 0, %3	\n"
	    "	cmpw	0, %0, %1	\n"
	    "	bne-	2f		\n"
	    "	stwcx.	%2, 0, %3	\n" 
	    "	bne-	1b		\n"
	    "2:				\n"
	    : "=&r" (rv)
	    : "r" (o), "r" (n), "r" (p)
	    : "cc", "memory");

	return (rv);
}
#define atomic_cas_ptr(_p, _o, _n) _atomic_cas_ptr((_p), (_o), (_n))

static inline unsigned int
_atomic_swap_uint(volatile unsigned int *p, unsigned int v)
{
	unsigned int rv;

	__asm volatile (
	    "1:	lwarx	%0, 0, %1	\n"
	    "	stwcx.	%2, 0, %1	\n"
	    "	bne-	1b		\n"
	    : "=&r" (rv)
	    : "r" (p), "r" (v)
	    : "cc", "memory");

	return (rv);
}
#define atomic_swap_uint(_p, _v) _atomic_swap_uint((_p), (_v))

static inline unsigned long
_atomic_swap_ulong(volatile unsigned long *p, unsigned long v)
{
	unsigned long rv;

	__asm volatile (
	    "1:	lwarx	%0, 0, %1	\n"
	    "	stwcx.	%2, 0, %1	\n"
	    "	bne-	1b		\n"
	    : "=&r" (rv)
	    : "r" (p), "r" (v)
	    : "cc", "memory");

	return (rv);
}
#define atomic_swap_ulong(_p, _v) _atomic_swap_ulong((_p), (_v))

static inline void *
_atomic_swap_ptr(volatile void *pp, void *v)
{
	void * volatile *p = pp;
	void *rv;

	__asm volatile (
	    "1:	lwarx	%0, 0, %1	\n"
	    "	stwcx.	%2, 0, %1	\n"
	    "	bne-	1b		\n"
	    : "=&r" (rv)
	    : "r" (p), "r" (v)
	    : "cc", "memory");

	return (rv);
}
#define atomic_swap_ptr(_p, _n) _atomic_swap_ptr((_p), (_n))

static inline unsigned int
_atomic_add_int_nv(volatile unsigned int *p, unsigned int v)
{
	unsigned int rv;

	__asm volatile (
	    "1:	lwarx	%0, 0, %1	\n"
	    "	add	%0, %2,	%0	\n"
	    "	stwcx.	%0, 0, %1	\n"
	    "	bne-	1b		\n"
	    : "=&r" (rv)
	    : "r" (p), "r" (v)
	    : "cc", "memory");

	return (rv + v);
}
#define atomic_add_int_nv(_p, _v) _atomic_add_int_nv((_p), (_v))
#define atomic_sub_int_nv(_p, _v) _atomic_add_int_nv((_p), 0 - (_v))

static inline unsigned long
_atomic_add_long_nv(volatile unsigned long *p, unsigned long v)
{
	unsigned long rv;

	__asm volatile (
	    "1:	lwarx	%0, 0, %1	\n"
	    "	add	%0, %2,	%0	\n"
	    "	stwcx.	%0, 0, %1	\n"
	    "	bne-	1b		\n"
	    : "=&r" (rv)
	    : "r" (p), "r" (v)
	    : "cc", "memory");

	return (rv + v);
}
#define atomic_add_long_nv(_p, _v) _atomic_add_long_nv((_p), (_v))
#define atomic_sub_long_nv(_p, _v) _atomic_add_long_nv((_p), 0 - (_v))

#define __membar(_f) do { __asm __volatile(_f ::: "memory"); } while (0)

#ifdef MULTIPROCESSOR
#define membar_enter()		__membar("isync")
#define membar_exit()		__membar("sync")
#define membar_producer()	__membar("sync")
#define membar_consumer()	__membar("isync")
#define membar_sync()		__membar("sync")
#else
#define membar_enter()		__membar("")
#define membar_exit()		__membar("")
#define membar_producer()	__membar("")
#define membar_consumer()	__membar("")
#define membar_sync()		__membar("")
#endif

#endif /* defined(_KERNEL) */
#endif /* _POWERPC_ATOMIC_H_ */
