/*	$OpenBSD: kern_clock.c,v 1.79 2013/03/12 09:37:16 mpi Exp $	*/
/*	$NetBSD: kern_clock.c,v 1.34 1996/06/09 04:51:03 briggs Exp $	*/

/*-
 * Copyright (c) 1982, 1986, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)kern_clock.c	8.5 (Berkeley) 1/21/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/dkstat.h>
#include <sys/timeout.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/resourcevar.h>
#include <sys/signalvar.h>
#include <uvm/uvm_extern.h>
#include <sys/sysctl.h>
#include <sys/sched.h>
#include <sys/timetc.h>

#include <machine/cpu.h>

#ifdef GPROF
#include <sys/gmon.h>
#endif

/*
 * Clock handling routines.
 *
 * This code is written to operate with two timers that run independently of
 * each other.  The main clock, running hz times per second, is used to keep
 * track of real time.  The second timer handles kernel and user profiling,
 * and does resource use estimation.  If the second timer is programmable,
 * it is randomized to avoid aliasing between the two clocks.  For example,
 * the randomization prevents an adversary from always giving up the cpu
 * just before its quantum expires.  Otherwise, it would never accumulate
 * cpu ticks.  The mean frequency of the second timer is stathz.
 *
 * If no second timer exists, stathz will be zero; in this case we drive
 * profiling and statistics off the main clock.  This WILL NOT be accurate;
 * do not do it unless absolutely necessary.
 *
 * The statistics clock may (or may not) be run at a higher rate while
 * profiling.  This profile clock runs at profhz.  We require that profhz
 * be an integral multiple of stathz.
 *
 * If the statistics clock is running fast, it must be divided by the ratio
 * profhz/stathz for statistics.  (For profiling, every tick counts.)
 */

/*
 * Bump a timeval by a small number of usec's.
 */
#define BUMPTIME(t, usec) { \
	volatile struct timeval *tp = (t); \
	long us; \
 \
	tp->tv_usec = us = tp->tv_usec + (usec); \
	if (us >= 1000000) { \
		tp->tv_usec = us - 1000000; \
		tp->tv_sec++; \
	} \
}

int	stathz;
int	schedhz;
int	profhz;
int	profprocs;
int	ticks;
static int psdiv, pscnt;		/* prof => stat divider */
int	psratio;			/* ratio: prof / stat */

long cp_time[CPUSTATES];

void	*softclock_si;

/*
 * Initialize clock frequencies and start both clocks running.
 */
void
initclocks(void)
{
	int i;

	softclock_si = softintr_establish(IPL_SOFTCLOCK, softclock, NULL);
	if (softclock_si == NULL)
		panic("initclocks: unable to register softclock intr");

	/*
	 * Set divisors to 1 (normal case) and let the machine-specific
	 * code do its bit.
	 */
	psdiv = pscnt = 1;
	cpu_initclocks();

	/*
	 * Compute profhz/stathz, and fix profhz if needed.
	 */
	i = stathz ? stathz : hz;
	if (profhz == 0)
		profhz = i;
	psratio = profhz / i;

	/* For very large HZ, ensure that division by 0 does not occur later */
	if (tickadj == 0)
		tickadj = 1;

	inittimecounter();
}

/*
 * hardclock does the accounting needed for ITIMER_PROF and ITIMER_VIRTUAL.
 * We don't want to send signals with psignal from hardclock because it makes
 * MULTIPROCESSOR locking very complicated. Instead we use a small trick
 * to send the signals safely and without blocking too many interrupts
 * while doing that (signal handling can be heavy).
 *
 * hardclock detects that the itimer has expired, and schedules a timeout
 * to deliver the signal. This works because of the following reasons:
 *  - The timeout can be scheduled with a 1 tick time because we're
 *    doing it before the timeout processing in hardclock. So it will
 *    be scheduled to run as soon as possible.
 *  - The timeout will be run in softclock which will run before we
 *    return to userland and process pending signals.
 *  - If the system is so busy that several VIRTUAL/PROF ticks are
 *    sent before softclock processing, we'll send only one signal.
 *    But if we'd send the signal from hardclock only one signal would
 *    be delivered to the user process. So userland will only see one
 *    signal anyway.
 */

void
virttimer_trampoline(void *v)
{
	struct process *pr = v;

	psignal(pr->ps_mainproc, SIGVTALRM);
}

void
proftimer_trampoline(void *v)
{
	struct process *pr = v;

	psignal(pr->ps_mainproc, SIGPROF);
}

/*
 * The real-time timer, interrupting hz times per second.
 */
void
hardclock(struct clockframe *frame)
{
	struct proc *p;
	struct cpu_info *ci = curcpu();

	p = curproc;
	if (p && ((p->p_flag & (P_SYSTEM | P_WEXIT)) == 0)) {
		struct process *pr = p->p_p;

		/*
		 * Run current process's virtual and profile time, as needed.
		 */
		if (CLKF_USERMODE(frame) &&
		    timerisset(&pr->ps_timer[ITIMER_VIRTUAL].it_value) &&
		    itimerdecr(&pr->ps_timer[ITIMER_VIRTUAL], tick) == 0)
			timeout_add(&pr->ps_virt_to, 1);
		if (timerisset(&pr->ps_timer[ITIMER_PROF].it_value) &&
		    itimerdecr(&pr->ps_timer[ITIMER_PROF], tick) == 0)
			timeout_add(&pr->ps_prof_to, 1);
	}

	/*
	 * If no separate statistics clock is available, run it from here.
	 */
	if (stathz == 0)
		statclock(frame);

	if (--ci->ci_schedstate.spc_rrticks <= 0)
		roundrobin(ci);

	/*
	 * If we are not the primary CPU, we're not allowed to do
	 * any more work.
	 */
	if (CPU_IS_PRIMARY(ci) == 0)
		return;

	tc_ticktock();

	/*
	 * Update real-time timeout queue.
	 * Process callouts at a very low cpu priority, so we don't keep the
	 * relatively high clock interrupt priority any longer than necessary.
	 */
	if (timeout_hardclock_update())
		softintr_schedule(softclock_si);
}

/*
 * Compute number of hz until specified time.  Used to
 * compute the second argument to timeout_add() from an absolute time.
 */
int
hzto(const struct timeval *tv)
{
	struct timeval now;
	unsigned long ticks;
	long sec, usec;

	/*
	 * If the number of usecs in the whole seconds part of the time
	 * difference fits in a long, then the total number of usecs will
	 * fit in an unsigned long.  Compute the total and convert it to
	 * ticks, rounding up and adding 1 to allow for the current tick
	 * to expire.  Rounding also depends on unsigned long arithmetic
	 * to avoid overflow.
	 *
	 * Otherwise, if the number of ticks in the whole seconds part of
	 * the time difference fits in a long, then convert the parts to
	 * ticks separately and add, using similar rounding methods and
	 * overflow avoidance.  This method would work in the previous
	 * case but it is slightly slower and assumes that hz is integral.
	 *
	 * Otherwise, round the time difference down to the maximum
	 * representable value.
	 *
	 * If ints have 32 bits, then the maximum value for any timeout in
	 * 10ms ticks is 248 days.
	 */
	getmicrotime(&now);
	sec = tv->tv_sec - now.tv_sec;
	usec = tv->tv_usec - now.tv_usec;
	if (usec < 0) {
		sec--;
		usec += 1000000;
	}
	if (sec < 0 || (sec == 0 && usec <= 0)) {
		ticks = 0;
	} else if (sec <= LONG_MAX / 1000000)
		ticks = (sec * 1000000 + (unsigned long)usec + (tick - 1))
		    / tick + 1;
	else if (sec <= LONG_MAX / hz)
		ticks = sec * hz
		    + ((unsigned long)usec + (tick - 1)) / tick + 1;
	else
		ticks = LONG_MAX;
	if (ticks > INT_MAX)
		ticks = INT_MAX;
	return ((int)ticks);
}

/*
 * Compute number of hz in the specified amount of time.
 */
int
tvtohz(const struct timeval *tv)
{
	unsigned long ticks;
	long sec, usec;

	/*
	 * If the number of usecs in the whole seconds part of the time
	 * fits in a long, then the total number of usecs will
	 * fit in an unsigned long.  Compute the total and convert it to
	 * ticks, rounding up and adding 1 to allow for the current tick
	 * to expire.  Rounding also depends on unsigned long arithmetic
	 * to avoid overflow.
	 *
	 * Otherwise, if the number of ticks in the whole seconds part of
	 * the time fits in a long, then convert the parts to
	 * ticks separately and add, using similar rounding methods and
	 * overflow avoidance.  This method would work in the previous
	 * case but it is slightly slower and assumes that hz is integral.
	 *
	 * Otherwise, round the time down to the maximum
	 * representable value.
	 *
	 * If ints have 32 bits, then the maximum value for any timeout in
	 * 10ms ticks is 248 days.
	 */
	sec = tv->tv_sec;
	usec = tv->tv_usec;
	if (sec < 0 || (sec == 0 && usec <= 0))
		ticks = 0;
	else if (sec <= LONG_MAX / 1000000)
		ticks = (sec * 1000000 + (unsigned long)usec + (tick - 1))
		    / tick + 1;
	else if (sec <= LONG_MAX / hz)
		ticks = sec * hz
		    + ((unsigned long)usec + (tick - 1)) / tick + 1;
	else
		ticks = LONG_MAX;
	if (ticks > INT_MAX)
		ticks = INT_MAX;
	return ((int)ticks);
}

/*
 * Start profiling on a process.
 *
 * Kernel profiling passes proc0 which never exits and hence
 * keeps the profile clock running constantly.
 */
void
startprofclock(struct process *pr)
{
	int s;

	if ((pr->ps_flags & PS_PROFIL) == 0) {
		atomic_setbits_int(&pr->ps_flags, PS_PROFIL);
		if (++profprocs == 1 && stathz != 0) {
			s = splstatclock();
			psdiv = pscnt = psratio;
			setstatclockrate(profhz);
			splx(s);
		}
	}
}

/*
 * Stop profiling on a process.
 */
void
stopprofclock(struct process *pr)
{
	int s;

	if (pr->ps_flags & PS_PROFIL) {
		atomic_clearbits_int(&pr->ps_flags, PS_PROFIL);
		if (--profprocs == 0 && stathz != 0) {
			s = splstatclock();
			psdiv = pscnt = 1;
			setstatclockrate(stathz);
			splx(s);
		}
	}
}

/*
 * Statistics clock.  Grab profile sample, and if divider reaches 0,
 * do process and kernel statistics.
 */
void
statclock(struct clockframe *frame)
{
#ifdef GPROF
	struct gmonparam *g;
	u_long i;
#endif
	struct cpu_info *ci = curcpu();
	struct schedstate_percpu *spc = &ci->ci_schedstate;
	struct proc *p = curproc;
	struct process *pr;

	/*
	 * Notice changes in divisor frequency, and adjust clock
	 * frequency accordingly.
	 */
	if (spc->spc_psdiv != psdiv) {
		spc->spc_psdiv = psdiv;
		spc->spc_pscnt = psdiv;
		if (psdiv == 1) {
			setstatclockrate(stathz);
		} else {
			setstatclockrate(profhz);			
		}
	}

	if (CLKF_USERMODE(frame)) {
		pr = p->p_p;
		if (pr->ps_flags & PS_PROFIL)
			addupc_intr(p, CLKF_PC(frame));
		if (--spc->spc_pscnt > 0)
			return;
		/*
		 * Came from user mode; CPU was in user state.
		 * If this process is being profiled record the tick.
		 */
		p->p_uticks++;
		if (pr->ps_nice > NZERO)
			spc->spc_cp_time[CP_NICE]++;
		else
			spc->spc_cp_time[CP_USER]++;
	} else {
#ifdef GPROF
		/*
		 * Kernel statistics are just like addupc_intr, only easier.
		 */
		g = ci->ci_gmon;
		if (g != NULL && g->state == GMON_PROF_ON) {
			i = CLKF_PC(frame) - g->lowpc;
			if (i < g->textsize) {
				i /= HISTFRACTION * sizeof(*g->kcount);
				g->kcount[i]++;
			}
		}
#endif
#if defined(PROC_PC)
		if (p != NULL && p->p_p->ps_flags & PS_PROFIL)
			addupc_intr(p, PROC_PC(p));
#endif
		if (--spc->spc_pscnt > 0)
			return;
		/*
		 * Came from kernel mode, so we were:
		 * - handling an interrupt,
		 * - doing syscall or trap work on behalf of the current
		 *   user process, or
		 * - spinning in the idle loop.
		 * Whichever it is, charge the time as appropriate.
		 * Note that we charge interrupts to the current process,
		 * regardless of whether they are ``for'' that process,
		 * so that we know how much of its real time was spent
		 * in ``non-process'' (i.e., interrupt) work.
		 */
		if (CLKF_INTR(frame)) {
			if (p != NULL)
				p->p_iticks++;
			spc->spc_cp_time[CP_INTR]++;
		} else if (p != NULL && p != spc->spc_idleproc) {
			p->p_sticks++;
			spc->spc_cp_time[CP_SYS]++;
		} else
			spc->spc_cp_time[CP_IDLE]++;
	}
	spc->spc_pscnt = psdiv;

	if (p != NULL) {
		p->p_cpticks++;
		/*
		 * If no schedclock is provided, call it here at ~~12-25 Hz;
		 * ~~16 Hz is best
		 */
		if (schedhz == 0) {
			if ((++curcpu()->ci_schedstate.spc_schedticks & 3) ==
			    0)
				schedclock(p);
		}
	}
}

/*
 * Return information about system clocks.
 */
int
sysctl_clockrate(char *where, size_t *sizep, void *newp)
{
	struct clockinfo clkinfo;

	/*
	 * Construct clockinfo structure.
	 */
	clkinfo.tick = tick;
	clkinfo.tickadj = tickadj;
	clkinfo.hz = hz;
	clkinfo.profhz = profhz;
	clkinfo.stathz = stathz ? stathz : hz;
	return (sysctl_rdstruct(where, sizep, newp, &clkinfo, sizeof(clkinfo)));
}
