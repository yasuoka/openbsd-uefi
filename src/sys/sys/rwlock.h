/*	$OpenBSD: rwlock.h,v 1.3 2004/01/11 00:42:03 tedu Exp $	*/
/*
 * Copyright (c) 2002 Artur Grabowski <art@openbsd.org>
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 *
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL  DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

/*
 * Multiple readers, single writer lock.
 *
 * Simplistic implementation modelled after rw locks in Solaris.
 *
 * The rwl_owner has the following layout:
 * [ owner or count of readers | wrlock | wrwant | wait ]
 *
 * When the WAIT bit is set (bit 0), the lock has waiters sleeping on it.
 * When the WRWANT bit is set (bit 1), at least one waiter wants a write lock.
 * When the WRLOCK bit is set (bit 2) the lock is currently write-locked.
 *
 * When write locked, the upper bits contain the struct proc * pointer to
 * the writer, otherwise they count the number of readers.
 *
 * We provide a simple machine independent implementation that can be
 * optimized by machine dependent code when __HAVE_MD_RWLOCK is defined.
 *
 * MD code that defines __HAVE_MD_RWLOCK and implement four functions:
 *  
 * void rw_enter_read(struct rwlock *)
 *  atomically test for RWLOCK_WRLOCK and if not set, increment the lock
 *  by RWLOCK_READ_INCR. While RWLOCK_WRLOCK is set, loop into rw_enter_wait.
 *
 * void rw_enter_write(struct rwlock *, struct proc *);
 *  atomically test for the lock being 0 (it's not possible to have
 *  owner/read count unset and waiter bits set) and if 0 set the owner to
 *  the proc and RWLOCK_WRLOCK. While not zero, loop into rw_enter_wait.
 *
 * void rw_exit_read(struct rwlock *);
 *  atomically decrement lock by RWLOCK_READ_INCR and unset RWLOCK_WAIT and
 *  RWLOCK_WRWANT remembering the old value of lock and if RWLOCK_WAIT was set,
 *  call rw_exit_waiters with the old contents of the lock.
 *
 * void rw_exit_write(struct rwlock *);
 *  atomically swap the contents of the lock with 0 and if RWLOCK_WAIT was
 *  set, call rw_exit_waiters with the old contents of the lock.
 *
 * (XXX - the rest of the API for this is not invented yet).
 */

#ifndef SYS_RWLOCK_H
#define SYS_RWLOCK_H


struct proc;

struct rwlock {
	__volatile unsigned long rwl_owner;
};

#define RWLOCK_INITIALIZER	{ 0 }

#define RWLOCK_WAIT		0x01
#define RWLOCK_WRWANT		0x02
#define RWLOCK_WRLOCK		0x04
#define RWLOCK_MASK		0x07

#define RWLOCK_OWNER(rwl)	((struct proc *)((rwl)->rwl_owner & ~RWLOCK_MASK))

#define RWLOCK_READER_SHIFT	3
#define RWLOCK_READ_INCR	(1 << RWLOCK_READER_SHIFT)

void rw_init(struct rwlock *);

void rw_enter_read(struct rwlock *);
void rw_enter_write(struct rwlock *, struct proc *);
void rw_exit_read(struct rwlock *);
void rw_exit_write(struct rwlock *);

/*
 * Internal API.
 */
#define RW_WRITE	0x00
#define RW_READ		0x01
void rw_enter_wait(struct rwlock *, struct proc *, int);
void rw_exit_waiters(struct rwlock *, unsigned long);

#endif
