/*	$OpenBSD: lock_machdep.c,v 1.4 2013/12/05 01:28:45 uebayasi Exp $	*/

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


#include <sys/param.h>
#include <sys/lock.h>
#include <sys/systm.h>

#include <machine/atomic.h>
#include <machine/lock.h>
#include <machine/psl.h>

#include <ddb/db_output.h>

void
__mp_lock_init(struct __mp_lock *lock)
{
	lock->mpl_cpu = NULL;
	lock->mpl_count = 0;
}

#if defined(MP_LOCKDEBUG)
#ifndef DDB
#error "MP_LOCKDEBUG requires DDB"
#endif

/* CPU-dependent timing, needs this to be settable from ddb. */
extern int __mp_lock_spinout;
#endif

/*
 * On processors with multiple threads we force a thread switch.
 *
 * On UltraSPARC T2 and its successors, the optimal way to do this
 * seems to be to do three nop reads of %ccr.  This works on
 * UltraSPARC T1 as well, even though three nop casx operations seem
 * to be slightly more optimal.  Since these instructions are
 * effectively nops, executing them on earlier non-CMT processors is
 * harmless, so we make this the default.
 *
 * On SPARC64 VI and it successors we execute the processor-specific
 * sleep instruction.
 */
static __inline void
__mp_lock_spin_hook(void)
{
	__asm __volatile(
		"999:	rd	%%ccr, %%g0			\n"
		"	rd	%%ccr, %%g0			\n"
		"	rd	%%ccr, %%g0			\n"
		"	.section .sun4u_mtp_patch, \"ax\"	\n"
		"	.word	999b				\n"
		"	.word	0x81b01060	! sleep		\n"
		"	.word	999b + 4			\n"
		"	nop					\n"
		"	.word	999b + 8			\n"
		"	nop					\n"
		"	.previous				\n"
		: : : "memory");
}

#define SPINLOCK_SPIN_HOOK __mp_lock_spin_hook()

static __inline void
__mp_lock_spin(struct __mp_lock *mpl)
{
#ifndef MP_LOCKDEBUG
	while (mpl->mpl_count != 0)
		SPINLOCK_SPIN_HOOK;
#else
	int ticks = __mp_lock_spinout;

	while (mpl->mpl_count != 0 && --ticks > 0)
		SPINLOCK_SPIN_HOOK;

	if (ticks == 0) {
		db_printf("__mp_lock(0x%x): lock spun out", mpl);
		Debugger();
	}
#endif
}

void
__mp_lock(struct __mp_lock *mpl)
{
	/*
	 * Please notice that mpl_count gets incremented twice for the
	 * first lock. This is on purpose. The way we release the lock
	 * in mp_unlock is to decrement the mpl_count and then check if
	 * the lock should be released. Since mpl_count is what we're
	 * spinning on, decrementing it in mpl_unlock to 0 means that
	 * we can't clear mpl_cpu, because we're no longer holding the
	 * lock. In theory mpl_cpu doesn't need to be cleared, but it's
	 * safer to clear it and besides, setting mpl_count to 2 on the
	 * first lock makes most of this code much simpler.
	 */

	while (1) {
		u_int64_t s;

		s = intr_disable();
		if (sparc64_casx(&mpl->mpl_count, 0, 1) == 0) {
			sparc_membar(LoadLoad | LoadStore);
			mpl->mpl_cpu = curcpu();
		}

		if (mpl->mpl_cpu == curcpu()) {
			mpl->mpl_count++;
			intr_restore(s);
			break;
		}
		intr_restore(s);

		__mp_lock_spin(mpl);
	}
}

void
__mp_unlock(struct __mp_lock *mpl)
{
	u_int64_t s;

#ifdef MP_LOCKDEBUG
	if (mpl->mpl_cpu != curcpu()) {
		db_printf("__mp_unlock(%p): not held lock\n", mpl);
		Debugger();
	}
#endif

	s = intr_disable();
	if (--mpl->mpl_count == 1) {
		mpl->mpl_cpu = NULL;
		sparc_membar(StoreStore | LoadStore);
		mpl->mpl_count = 0;
	}
	intr_restore(s);
}

int
__mp_release_all(struct __mp_lock *mpl)
{
	int rv = mpl->mpl_count - 1;
	u_int64_t s;

#ifdef MP_LOCKDEBUG
	if (mpl->mpl_cpu != curcpu()) {
		db_printf("__mp_release_all(%p): not held lock\n", mpl);
		Debugger();
	}
#endif

	s = intr_disable();
	mpl->mpl_cpu = NULL;
	sparc_membar(StoreStore | LoadStore);
	mpl->mpl_count = 0;
	intr_restore(s);

	return (rv);
}

int
__mp_release_all_but_one(struct __mp_lock *mpl)
{
	int rv = mpl->mpl_count - 2;

#ifdef MP_LOCKDEBUG
	if (mpl->mpl_cpu != curcpu()) {
		db_printf("__mp_release_all_but_one(%p): not held lock\n", mpl);
		Debugger();
	}
#endif

	mpl->mpl_count = 2;

	return (rv);
}

void
__mp_acquire_count(struct __mp_lock *mpl, int count)
{
	while (count--)
		__mp_lock(mpl);
}

int
__mp_lock_held(struct __mp_lock *mpl)
{
	return mpl->mpl_cpu == curcpu();
}
