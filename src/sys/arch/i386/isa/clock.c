/*	$OpenBSD: clock.c,v 1.23 2001/02/19 04:57:02 ho Exp $	*/
/*	$NetBSD: clock.c,v 1.39 1996/05/12 23:11:54 mycroft Exp $	*/

/*-
 * Copyright (c) 1993, 1994 Charles Hannum.
 * Copyright (c) 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * William Jolitz and Don Ahn.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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
 *	@(#)clock.c	7.2 (Berkeley) 5/12/91
 */
/* 
 * Mach Operating System
 * Copyright (c) 1991,1990,1989 Carnegie Mellon University
 * All Rights Reserved.
 * 
 * Permission to use, copy, modify and distribute this software and its
 * documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 * 
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND FOR
 * ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 * 
 * Carnegie Mellon requests users of this software to return to
 * 
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 * 
 * any improvements or extensions that they make and grant Carnegie Mellon
 * the rights to redistribute these changes.
 */
/*
  Copyright 1988, 1989 by Intel Corporation, Santa Clara, California.

		All Rights Reserved

Permission to use, copy, modify, and distribute this software and
its documentation for any purpose and without fee is hereby
granted, provided that the above copyright notice appears in all
copies and that both the copyright notice and this permission notice
appear in supporting documentation, and that the name of Intel
not be used in advertising or publicity pertaining to distribution
of the software without specific, written prior permission.

INTEL DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE
INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS,
IN NO EVENT SHALL INTEL BE LIABLE FOR ANY SPECIAL, INDIRECT, OR
CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
LOSS OF USE, DATA OR PROFITS, WHETHER IN ACTION OF CONTRACT,
NEGLIGENCE, OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION
WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

/*
 * Primitive clock interrupt routines.
 */
#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/timeout.h>

#include <machine/cpu.h>
#include <machine/intr.h>
#include <machine/pio.h>
#include <machine/cpufunc.h>

#include <dev/clock_subr.h>
#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>
#include <dev/ic/mc146818reg.h>
#include <i386/isa/nvram.h>
#include <i386/isa/timerreg.h>

#include "pcppi.h"
#if (NPCPPI > 0)
#include <dev/isa/pcppivar.h>

#define __BROKEN_INDIRECT_CONFIG /* XXX */
#ifdef __BROKEN_INDIRECT_CONFIG
int sysbeepmatch __P((struct device *, void *, void *));
#else
int sysbeepmatch __P((struct device *, struct cfdata *, void *));
#endif
void sysbeepattach __P((struct device *, struct device *, void *));

struct cfattach sysbeep_ca = {
	sizeof(struct device), sysbeepmatch, sysbeepattach
};

struct cfdriver sysbeep_cd = {
	NULL, "sysbeep", DV_DULL
};

static int ppi_attached;
static pcppi_tag_t ppicookie;
#endif /* PCPPI */

void	spinwait __P((int));
void	findcpuspeed __P((void));
int	clockintr __P((void *));
int	gettick __P((void));
void	sysbeep __P((int, int));
int	rtcget __P((mc_todregs *));
void	rtcput __P((mc_todregs *));
int 	hexdectodec __P((int));
int	dectohexdec __P((int));
int	rtcintr __P((void *));
void	rtcdrain __P((void *));

__inline u_int mc146818_read __P((void *, u_int));
__inline void mc146818_write __P((void *, u_int, u_int));

#if defined(I586_CPU) || defined(I686_CPU)
int pentium_mhz;
#endif

#define	SECMIN	((unsigned)60)			/* seconds per minute */
#define	SECHOUR	((unsigned)(60*SECMIN))		/* seconds per hour */

__inline u_int
mc146818_read(sc, reg)
	void *sc;					/* XXX use it? */
	u_int reg;
{
	int s;
	u_char v;

	s = splhigh();
	outb(IO_RTC, reg);
	DELAY(1);
	v = inb(IO_RTC+1);
	DELAY(1);
	splx(s);
	return (v);
}

__inline void
mc146818_write(sc, reg, datum)
	void *sc;					/* XXX use it? */
	u_int reg, datum;
{
	int s;

	s = splhigh();
	outb(IO_RTC, reg);
	DELAY(1);
	outb(IO_RTC+1, datum);
	DELAY(1);
	splx(s);
}

void
startrtclock()
{
	int s;

	findcpuspeed();		/* use the clock (while it's free)
					to find the cpu speed */
	initrtclock();

	/* Check diagnostic status */
	if ((s = mc146818_read(NULL, NVRAM_DIAG)) != 0)	/* XXX softc */
		printf("RTC BIOS diagnostic error %b\n", (unsigned int) s, 
		    NVRAM_DIAG_BITS);
}

void
rtcdrain(void *v)
{
	struct timeout *to = (struct timeout *)v;

	if (to != NULL)
		timeout_del(to);

	/* 
	 * Drain any un-acknowledged RTC interrupts. 
	 * See comment in cpu_initclocks(). 
	 */
  	while (mc146818_read(NULL, MC_REGC) & MC_REGC_PF)
		; /* Nothing. */
}

void
initrtclock()
{
	/* initialize 8253 clock */
	outb(TIMER_MODE, TIMER_SEL0|TIMER_RATEGEN|TIMER_16BIT);

	/* Correct rounding will buy us a better precision in timekeeping */
	outb(IO_TIMER1, TIMER_DIV(hz) % 256);
	outb(IO_TIMER1, TIMER_DIV(hz) / 256);
}

int
clockintr(arg)
	void *arg;
{
	struct clockframe *frame = arg;		/* not strictly necessary */

	hardclock(frame);
	return (1);
}

int
rtcintr(arg)
	void *arg;
{
	struct clockframe *frame = arg;		/* not strictly necessary */
	u_int stat = 0;

	/* 
	 * If rtcintr is 'late', next intr may happen immediately. 
	 * Get them all. (Also, see comment in cpu_initclocks().)
	 */
	while (mc146818_read(NULL, MC_REGC) & MC_REGC_PF) {
		statclock(frame);
		stat = 1;
	}
	return (stat);
}

int
gettick()
{
	u_char lo, hi;

	/* Don't want someone screwing with the counter while we're here. */
	disable_intr();
	/* Select counter 0 and latch it. */
	outb(TIMER_MODE, TIMER_SEL0 | TIMER_LATCH);
	lo = inb(TIMER_CNTR0);
	hi = inb(TIMER_CNTR0);
	enable_intr();
	return ((hi << 8) | lo);
}

/*
 * Wait "n" microseconds.
 * Relies on timer 1 counting down from (TIMER_FREQ / hz) at TIMER_FREQ Hz.
 * Note: timer had better have been programmed before this is first used!
 * (Note that we use `rate generator' mode, which counts at 1:1; `square
 * wave' mode counts at 2:1).
 */
void
delay(n)
	int n;
{
	int limit, tick, otick;

	/*
	 * Read the counter first, so that the rest of the setup overhead is
	 * counted.
	 */
	otick = gettick();

#ifdef __GNUC__
	/*
	 * Calculate ((n * TIMER_FREQ) / 1e6) using explicit assembler code so
	 * we can take advantage of the intermediate 64-bit quantity to prevent
	 * loss of significance.
	 */
	n -= 5;
	if (n < 0)
		return;
	__asm __volatile("mul %2\n\tdiv %3"
			 : "=a" (n) 
			 : "0" (n), "r" (TIMER_FREQ), "r" (1000000)
			 : "%edx", "cc");
#else
	/*
	 * Calculate ((n * TIMER_FREQ) / 1e6) without using floating point and
	 * without any avoidable overflows.
	 */
	n -= 20;
	{
		int sec = n / 1000000,
		    usec = n % 1000000;
		n = sec * TIMER_FREQ +
		    usec * (TIMER_FREQ / 1000000) +
		    usec * ((TIMER_FREQ % 1000000) / 1000) / 1000 +
		    usec * (TIMER_FREQ % 1000) / 1000000;
	}
#endif

	limit = TIMER_FREQ / hz;

	while (n > 0) {
		tick = gettick();
		if (tick > otick)
			n -= limit - (tick - otick);
		else
			n -= otick - tick;
		otick = tick;
	}
}

#if (NPCPPI > 0)
int
sysbeepmatch(parent, match, aux)
	struct device *parent;
#ifdef __BROKEN_INDIRECT_CONFIG
	void *match;
#else
	struct cfdata *match;
#endif
	void *aux;
{
	return (!ppi_attached);
}

void
sysbeepattach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	printf("\n");

	ppicookie = ((struct pcppi_attach_args *)aux)->pa_cookie;
	ppi_attached = 1;
}
#endif

void
sysbeep(pitch, period)
	int pitch, period;
{
#if (NPCPPI > 0)
	if (ppi_attached)
		pcppi_bell(ppicookie, pitch, period, 0);
#endif
}

unsigned int delaycount;	/* calibrated loop variable (1 millisecond) */

#define FIRST_GUESS   0x2000

void
findcpuspeed()
{
	int i;
	int remainder;

	/* Put counter in count down mode */
	outb(TIMER_MODE, TIMER_SEL0 | TIMER_16BIT | TIMER_RATEGEN);
	outb(TIMER_CNTR0, 0xff);
	outb(TIMER_CNTR0, 0xff);
	for (i = FIRST_GUESS; i; i--)
		;
	/* Read the value left in the counter */
	remainder = gettick();
	/*
	 * Formula for delaycount is:
	 *  (loopcount * timer clock speed) / (counter ticks * 1000)
	 */
	delaycount = (FIRST_GUESS * TIMER_DIV(1000)) / (0xffff-remainder);
}

#if defined(I586_CPU) || defined(I686_CPU)
void
calibrate_cyclecounter()
{
	unsigned long long count, last_count;
#ifdef NTP
	extern long time_precision;
#endif

	__asm __volatile(".byte 0xf, 0x31" : "=A" (last_count));
	delay(1000000);
	__asm __volatile(".byte 0xf, 0x31" : "=A" (count));
	pentium_mhz = ((count - last_count) + 500000) / 1000000;
#ifdef NTP
	time_precision = 1;	/* XXX */
#endif
}
#endif

void
cpu_initclocks()
{
	static struct timeout rtcdrain_timeout;
	stathz = 128;
	profhz = 1024;

	/*
	 * XXX If you're doing strange things with multiple clocks, you might
	 * want to keep track of clock handlers.
	 */
	(void)isa_intr_establish(NULL, 0, IST_PULSE, IPL_CLOCK, clockintr,
	    0, "clock");
	(void)isa_intr_establish(NULL, 8, IST_PULSE, IPL_CLOCK, rtcintr,
	    0, "rtc");

	mc146818_write(NULL, MC_REGA, MC_BASE_32_KHz | MC_RATE_128_Hz);
	mc146818_write(NULL, MC_REGB, MC_REGB_24HR | MC_REGB_PIE);

	/*
	 * On a number of i386 systems, the rtc will fail to start when booting
	 * the system. This is due to us missing to acknowledge an interrupt
	 * during early stages of the boot process. If we do not acknowledge
	 * the interrupt, the rtc clock will not generate further interrupts.
	 * To solve this, once interrupts are enabled, use a timeout (once)
	 * to drain any un-acknowledged rtc interrupt(s).
	 */

	timeout_set(&rtcdrain_timeout, rtcdrain, (void *)&rtcdrain_timeout);
	timeout_add(&rtcdrain_timeout, 1);
}

int
rtcget(regs)
	mc_todregs *regs;
{
	if ((mc146818_read(NULL, MC_REGD) & MC_REGD_VRT) == 0) /* XXX softc */
		return (-1);
	MC146818_GETTOD(NULL, regs);			/* XXX softc */
	return (0);
}	

void
rtcput(regs)
	mc_todregs *regs;
{
	MC146818_PUTTOD(NULL, regs);			/* XXX softc */
}

int
hexdectodec(n)
	int n;
{

	return (((n >> 4) & 0x0f) * 10 + (n & 0x0f));
}

int
dectohexdec(n)
	int n;
{

	return ((u_char)(((n / 10) << 4) & 0xf0) | ((n % 10) & 0x0f));
}

static int timeset;

/*
 * check whether the CMOS layout is "standard"-like (ie, not PS/2-like),
 * to be called at splclock()
 */
int cmoscheck __P((void));
int
cmoscheck()
{
	int i;
	unsigned short cksum = 0;

	for (i = 0x10; i <= 0x2d; i++)
		cksum += mc146818_read(NULL, i); /* XXX softc */

	return (cksum == (mc146818_read(NULL, 0x2e) << 8)
			  + mc146818_read(NULL, 0x2f));
}

/*
 * patchable to control century byte handling:
 * 1: always update
 * -1: never touch
 * 0: try to figure out itself
 */
int rtc_update_century = 0;

/*
 * Expand a two-digit year as read from the clock chip
 * into full width.
 * Being here, deal with the CMOS century byte.
 */
int clock_expandyear __P((int));
int
clock_expandyear(clockyear)
	int clockyear;
{
	int s, clockcentury, cmoscentury;

	clockcentury = (clockyear < 70) ? 20 : 19;
	clockyear += 100 * clockcentury;

	if (rtc_update_century < 0)
		return (clockyear);

	s = splclock();
	if (cmoscheck())
		cmoscentury = mc146818_read(NULL, NVRAM_CENTURY);
	else
		cmoscentury = 0;
	splx(s);
	if (!cmoscentury) {
#ifdef DIAGNOSTIC
		printf("clock: unknown CMOS layout\n");
#endif
		return (clockyear);
	}
	cmoscentury = hexdectodec(cmoscentury);

	if (cmoscentury != clockcentury) {
		/* XXX note: saying "century is 20" might confuse the naive. */
		printf("WARNING: NVRAM century is %d but RTC year is %d\n",
		       cmoscentury, clockyear);

		/* Kludge to roll over century. */
		if ((rtc_update_century > 0) ||
		    ((cmoscentury == 19) && (clockcentury == 20) &&
		     (clockyear == 2000))) {
			printf("WARNING: Setting NVRAM century to %d\n",
			       clockcentury);
			s = splclock();
			mc146818_write(NULL, NVRAM_CENTURY,
				       dectohexdec(clockcentury));
			splx(s);
		}
	} else if (cmoscentury == 19 && rtc_update_century == 0)
		rtc_update_century = 1; /* will update later in resettodr() */

	return (clockyear);
}

/*
 * Initialize the time of day register, based on the time base which is, e.g.
 * from a filesystem.
 */
void
inittodr(base)
	time_t base;
{
	mc_todregs rtclk;
	struct clock_ymdhms dt;
	int s;

	/*
	 * We mostly ignore the suggested time and go for the RTC clock time
	 * stored in the CMOS RAM.  If the time can't be obtained from the
	 * CMOS, or if the time obtained from the CMOS is 5 or more years
	 * less than the suggested time, we used the suggested time.  (In
	 * the latter case, it's likely that the CMOS battery has died.)
	 */

	if (base < 15*SECYR) {	/* if before 1985, something's odd... */
		printf("WARNING: preposterous time in file system\n");
		/* read the system clock anyway */
		base = 17*SECYR + 186*SECDAY + SECDAY/2;
	}

	time.tv_usec = 0;

	s = splclock();
	if (rtcget(&rtclk)) {
		splx(s);
		printf("WARNING: invalid time in clock chip\n");
		goto fstime;
	}
	splx(s);

	dt.dt_sec = hexdectodec(rtclk[MC_SEC]);
	dt.dt_min = hexdectodec(rtclk[MC_MIN]);
	dt.dt_hour = hexdectodec(rtclk[MC_HOUR]);
	dt.dt_day = hexdectodec(rtclk[MC_DOM]);
	dt.dt_mon = hexdectodec(rtclk[MC_MONTH]);
	dt.dt_year = clock_expandyear(hexdectodec(rtclk[MC_YEAR]));


	/*
	 * If time_t is 32 bits, then the "End of Time" is 
	 * Mon Jan 18 22:14:07 2038 (US/Eastern)
	 * This code copes with RTC's past the end of time if time_t
	 * is an int32 or less. Needed because sometimes RTCs screw
	 * up or are badly set, and that would cause the time to go
	 * negative in the calculation below, which causes Very Bad
	 * Mojo. This at least lets the user boot and fix the problem.
	 * Note the code is self eliminating once time_t goes to 64 bits.
	 */
	if (sizeof(time_t) <= sizeof(int32_t)) {
		if (dt.dt_year >= 2038) {
			printf("WARNING: RTC time at or beyond 2038.\n");
			dt.dt_year = 2037;
			printf("WARNING: year set back to 2037.\n");
			printf("WARNING: CHECK AND RESET THE DATE!\n");
		}
	}

	time.tv_sec = clock_ymdhms_to_secs(&dt) + tz.tz_minuteswest * 60;
	if (tz.tz_dsttime)
		time.tv_sec -= 3600;

	if (base < time.tv_sec - 5*SECYR)
		printf("WARNING: file system time much less than clock time\n");
	else if (base > time.tv_sec + 5*SECYR) {
		printf("WARNING: clock time much less than file system time\n");
		printf("WARNING: using file system time\n");
		goto fstime;
	}

	timeset = 1;
	return;

fstime:
	timeset = 1;
	time.tv_sec = base;
	printf("WARNING: CHECK AND RESET THE DATE!\n");
}

/*
 * Reset the clock.
 */
void
resettodr()
{
	mc_todregs rtclk;
	struct clock_ymdhms dt;
	int diff;
	int century;
	int s;

	/*
	 * We might have been called by boot() due to a crash early
	 * on.  Don't reset the clock chip in this case.
	 */
	if (!timeset)
		return;

	s = splclock();
	if (rtcget(&rtclk))
		bzero(&rtclk, sizeof(rtclk));
	splx(s);

	diff = tz.tz_minuteswest * 60;
	if (tz.tz_dsttime)
		diff -= 3600;
	clock_secs_to_ymdhms(time.tv_sec - diff, &dt);

	rtclk[MC_SEC] = dectohexdec(dt.dt_sec);
	rtclk[MC_MIN] = dectohexdec(dt.dt_min);
	rtclk[MC_HOUR] = dectohexdec(dt.dt_hour);
	rtclk[MC_DOW] = dt.dt_wday;
	rtclk[MC_YEAR] = dectohexdec(dt.dt_year % 100);
	rtclk[MC_MONTH] = dectohexdec(dt.dt_mon);
	rtclk[MC_DOM] = dectohexdec(dt.dt_day);
	s = splclock();
	rtcput(&rtclk);
	if (rtc_update_century > 0) {
		century = dectohexdec(dt.dt_year / 100);
		mc146818_write(NULL, NVRAM_CENTURY, century); /* XXX softc */
	}
	splx(s);
}

void
setstatclockrate(arg)
	int arg;
{
	if (arg == stathz)
		mc146818_write(NULL, MC_REGA, MC_BASE_32_KHz | MC_RATE_128_Hz);
	else
		mc146818_write(NULL, MC_REGA, MC_BASE_32_KHz | MC_RATE_1024_Hz);
}
