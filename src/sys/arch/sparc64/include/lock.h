/*	$OpenBSD: lock.h,v 1.1 2007/05/01 18:56:31 miod Exp $	*/

/* public domain */

#ifndef	_SPARC64_LOCK_H_
#define	_SPARC64_LOCK_H_

typedef volatile u_int8_t __cpu_simple_lock_t;

#define	__SIMPLELOCK_LOCKED	0xff
#define	__SIMPLELOCK_UNLOCKED	0x00

static __inline__ void
__cpu_simple_lock_init(__cpu_simple_lock_t *l)
{
	*l = __SIMPLELOCK_UNLOCKED;
}

static __inline__ void
__cpu_simple_lock(__cpu_simple_lock_t *l)
{
	__cpu_simple_lock_t old;

	do {
		old = __SIMPLELOCK_LOCKED;
		__asm__ __volatile__
		    ("ldstub %0, %1" : "=m" (*l), "=r" (old) : "0" (*l));
	} while (old != __SIMPLELOCK_UNLOCKED);
}

static __inline__ int
__cpu_simple_lock_try(__cpu_simple_lock_t *l)
{
	__cpu_simple_lock_t old = __SIMPLELOCK_LOCKED;

	__asm__ __volatile__
	    ("ldstub %0, %1" : "=m" (*l), "=r" (old) : "0" (*l));

	return (old == __SIMPLELOCK_UNLOCKED);
}

static __inline__ void
__cpu_simple_unlock(__cpu_simple_lock_t *l)
{
	*l = __SIMPLELOCK_UNLOCKED;
}

#endif	/* _SPARC64_LOCK_H_ */
