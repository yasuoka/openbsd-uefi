/*	$OpenBSD: atomic.h,v 1.7 2014/09/30 05:07:51 dlg Exp $	*/

/* Public Domain */

#ifndef _MACHINE_ATOMIC_H_
#define _MACHINE_ATOMIC_H_

#if defined(_KERNEL)

#include <machine/psl.h>

static inline unsigned int
_atomic_cas_word(volatile unsigned int *uip, unsigned int o, unsigned int n)
{
	int psr;
	unsigned int rv;

	psr = getpsr();
	setpsr(psr | PSR_PIL);
	rv = *uip;
	if (rv == o)
		*uip = n;
	setpsr(psr);

	return (rv);
}
#define atomic_cas_uint(_p, _o, _n) _atomic_cas_word((_p), (_o), (_n))
#define atomic_cas_ulong(_p, _o, _n) _atomic_cas_word((_p), (_o), (_n))

static inline void *
_atomic_cas_ptr(volatile void *uip, void *o, void *n)
{
	int psr;
	void * volatile *uipp = (void * volatile *)uip;
	void *rv;

	psr = getpsr();
	setpsr(psr | PSR_PIL);
	rv = *uipp;
	if (rv == o)
		*uipp = n;
	setpsr(psr);

	return (rv);
}
#define atomic_cas_ptr(_p, _o, _n) _atomic_cas_ptr((_p), (_o), (_n))

static inline unsigned int
_atomic_swap_word(volatile unsigned int *uip, unsigned int n)
{
	int psr;
	unsigned int rv;

	psr = getpsr();
	setpsr(psr | PSR_PIL);
	rv = *uip;
	*uip = n;
	setpsr(psr);

	return (rv);
}
#define atomic_swap_uint(_p, _n) _atomic_swap_word((_p), (_n))
#define atomic_swap_ulong(_p, _n) _atomic_swap_word((_p), (_n))

static inline void *
_atomic_swap_ptr(volatile void *uip, void *n)
{
	int psr;
	void * volatile *uipp = (void * volatile *)uip;
	void *rv;

	psr = getpsr();
	setpsr(psr | PSR_PIL);
	rv = *uipp;
	*uipp = n;
	setpsr(psr);

	return (rv);
}
#define atomic_swap_ptr(_p, _o, _n) _atomic_swap_ptr((_p), (_o), (_n))

static inline unsigned int
_atomic_add_word_nv(volatile unsigned int *uip, unsigned int v)
{
	int psr;
	unsigned int rv;

	psr = getpsr();
	setpsr(psr | PSR_PIL);
	rv = *uip + v;
	*uip = rv;
	setpsr(psr);

	return (rv);
}
#define atomic_add_int_nv(_p, _v) _atomic_add_word_nv((_p), (_v))
#define atomic_add_long_nv(_p, _v) _atomic_add_word_nv((_p), (_v))

static inline unsigned int
_atomic_sub_word_nv(volatile unsigned int *uip, unsigned int v)
{
	int psr;
	unsigned int rv;

	psr = getpsr();
	setpsr(psr | PSR_PIL);
	rv = *uip - v;
	*uip = rv;
	setpsr(psr);

	return (rv);
}
#define atomic_sub_int_nv(_p, _v) _atomic_sub_word_nv((_p), (_v))
#define atomic_sub_long_nv(_p, _v) _atomic_sub_word_nv((_p), (_v))

static inline void
atomic_setbits_int(volatile unsigned int *uip, unsigned int v)
{
	int psr;

	psr = getpsr();
	setpsr(psr | PSR_PIL);
	*uip |= v;
	setpsr(psr);
}

static inline void
atomic_clearbits_int(volatile unsigned int *uip, unsigned int v)
{
	int psr;

	psr = getpsr();
	setpsr(psr | PSR_PIL);
	*uip &= ~v;
	setpsr(psr);
}

#endif /* defined(_KERNEL) */
#endif /* _MACHINE_ATOMIC_H_ */
