/*	$OpenBSD: clock.c,v 1.29 2004/01/14 20:50:48 miod Exp $ */
/*
 * Copyright (c) 1999 Steve Murphree, Jr.
 * Copyright (c) 1995 Theo de Raadt
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 * Copyright (c) 1995 Nivas Madhur
 * Copyright (c) 1994 Gordon W. Ross
 * Copyright (c) 1993 Adam Glass
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratory.
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
 *	@(#)clock.c	8.1 (Berkeley) 6/11/93
 */

/*
 * Interval and statistic clocks driver.
 */

#include <sys/param.h>
#include <sys/simplelock.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <machine/asm.h>
#include <machine/board.h>	/* for register defines */
#include <machine/psl.h>
#include <machine/autoconf.h>
#include <machine/bugio.h>
#include <machine/cpu.h>
#include <machine/cmmu.h>	/* DMA_CACHE_SYNC, etc... */

#include "pcctwo.h"
#if NPCCTWO > 0
#include <mvme88k/dev/pcctwofunc.h>
#include <mvme88k/dev/pcctworeg.h>
extern struct vme2reg *sys_vme2;
#endif

#include "syscon.h"
#if NSYSCON > 0
#include <mvme88k/dev/sysconfunc.h>
#include <mvme88k/dev/sysconreg.h>
#endif
#include <mvme88k/dev/vme.h>

#include "bugtty.h"
#if NBUGTTY > 0
#include <mvme88k/dev/bugttyfunc.h>
#endif

int	clockmatch(struct device *, void *, void *);
void	clockattach(struct device *, struct device *, void *);

void	sbc_initclock(void);
void	sbc_initstatclock(void);
void	m188_initclock(void);
void	m188_initstatclock(void);
void	m188_timer_init(unsigned);
void	m188_cio_init(unsigned);
u_int8_t read_cio(int);
void	write_cio(int, u_int8_t);

struct clocksoftc {
	struct device	sc_dev;
	struct intrhand	sc_profih;
	struct intrhand	sc_statih;
};

struct cfattach clock_ca = {
        sizeof(struct clocksoftc), clockmatch, clockattach
};

struct cfdriver clock_cd = {
        NULL, "clock", DV_DULL
};

int	sbc_clockintr(void *);
int	sbc_statintr(void *);
int	m188_clockintr(void *);
int	m188_statintr(void *);

u_int8_t prof_reset;
u_int8_t stat_reset;

struct simplelock cio_lock;

#define	CIO_LOCK	simple_lock(&cio_lock)
#define	CIO_UNLOCK	simple_unlock(&cio_lock)

/*
 * Statistics clock interval and variance, in usec.  Variance must be a
 * power of two.  Since this gives us an even number, not an odd number,
 * we discard one case and compensate.  That is, a variance of 4096 would
 * give us offsets in [0..4095].  Instead, we take offsets in [1..4095].
 * This is symmetric about the point 2048, or statvar/2, and thus averages
 * to that value (assuming uniform random numbers).
 */
int statvar = 8192;
int statmin;			/* statclock interval - 1/2*variance */

/*
 * Every machine must have a clock tick device of some sort; for this
 * platform this file manages it, no matter what form it takes.
 */
int
clockmatch(struct device *parent, void *vcf, void *args)
{
	struct confargs *ca = args;
	struct cfdata *cf = vcf;

	if (strcmp(cf->cf_driver->cd_name, "clock")) {
		return (0);
	}

	/*
	 * clock has to be at ipl 5
	 * We return the ipl here so that the parent can print
	 * a message if it is different from what ioconf.c says.
	 */
	ca->ca_ipl = IPL_CLOCK;
	return (1);
}

void
clockattach(struct device *parent, struct device *self, void *args)
{
	struct confargs *ca = args;
	struct clocksoftc *sc = (struct clocksoftc *)self;

	switch (ca->ca_bustype) {
#if NPCCTWO > 0
	case BUS_PCCTWO:
		sc->sc_profih.ih_fn = sbc_clockintr;
		sc->sc_profih.ih_arg = 0;
		sc->sc_profih.ih_wantframe = 1;
		sc->sc_profih.ih_ipl = ca->ca_ipl;
		prof_reset = ca->ca_ipl | PCC2_IRQ_IEN | PCC2_IRQ_ICLR;
		pcctwointr_establish(PCC2V_TIMER1, &sc->sc_profih);
		md.clock_init_func = sbc_initclock;
		sc->sc_statih.ih_fn = sbc_statintr;
		sc->sc_statih.ih_arg = 0;
		sc->sc_statih.ih_wantframe = 1;
		sc->sc_statih.ih_ipl = ca->ca_ipl;
		stat_reset = ca->ca_ipl | PCC2_IRQ_IEN | PCC2_IRQ_ICLR;
		pcctwointr_establish(PCC2V_TIMER2, &sc->sc_statih);
		md.statclock_init_func = sbc_initstatclock;
		break;
#endif /* NPCCTWO */
#if NSYSCON > 0
	case BUS_SYSCON:
		sc->sc_profih.ih_fn = m188_clockintr;
		sc->sc_profih.ih_arg = 0;
		sc->sc_profih.ih_wantframe = 1;
		sc->sc_profih.ih_ipl = ca->ca_ipl;
		sysconintr_establish(SYSCV_TIMER1, &sc->sc_profih);
		md.clock_init_func = m188_initclock;
		sc->sc_statih.ih_fn = m188_statintr;
		sc->sc_statih.ih_arg = 0;
		sc->sc_statih.ih_wantframe = 1;
		sc->sc_statih.ih_ipl = ca->ca_ipl;
		sysconintr_establish(SYSCV_TIMER2, &sc->sc_statih);
		md.statclock_init_func = m188_initstatclock;
		break;
#endif /* NSYSCON */
	}
	printf("\n");
}

#if NPCCTWO > 0

void
sbc_initclock(void)
{
#ifdef CLOCK_DEBUG
	printf("SBC clock init\n");
#endif
	if (1000000 % hz) {
		printf("cannot get %d Hz clock; using 100 Hz\n", hz);
		hz = 100;
		tick = 1000000 / hz;
	}

	/* profclock */
	sys_pcc2->pcc2_t1ctl = 0;
	sys_pcc2->pcc2_t1cmp = pcc2_timer_us2lim(tick);
	sys_pcc2->pcc2_t1count = 0;
	sys_pcc2->pcc2_t1ctl = PCC2_TCTL_CEN | PCC2_TCTL_COC | PCC2_TCTL_COVF;
	sys_pcc2->pcc2_t1irq = prof_reset;

}

/*
 * clockintr: ack intr and call hardclock
 */
int
sbc_clockintr(void *eframe)
{
	sys_pcc2->pcc2_t1irq = prof_reset;

	intrcnt[M88K_CLK_IRQ]++;
	hardclock(eframe);
#if NBUGTTY > 0
	bugtty_chkinput();
#endif /* NBUGTTY */

	return (1);
}

void
sbc_initstatclock(void)
{
	int statint, minint;

#ifdef CLOCK_DEBUG
	printf("SBC statclock init\n");
#endif
	if (stathz == 0)
		stathz = hz;
	if (1000000 % stathz) {
		printf("cannot get %d Hz statclock; using 100 Hz\n", stathz);
		stathz = 100;
	}
	profhz = stathz;		/* always */

	statint = 1000000 / stathz;
	minint = statint / 2 + 100;
	while (statvar > minint)
		statvar >>= 1;

	/* statclock */
	sys_pcc2->pcc2_t2ctl = 0;
	sys_pcc2->pcc2_t2cmp = pcc2_timer_us2lim(statint);
	sys_pcc2->pcc2_t2count = 0;
	sys_pcc2->pcc2_t2ctl = PCC2_TCTL_CEN | PCC2_TCTL_COC | PCC2_TCTL_COVF;
	sys_pcc2->pcc2_t2irq = stat_reset;

	statmin = statint - (statvar >> 1);
}

int
sbc_statintr(void *eframe)
{
	u_long newint, r, var;

	sys_pcc2->pcc2_t2irq = stat_reset;

	/* increment intr counter */
	intrcnt[M88K_SCLK_IRQ]++;

	statclock((struct clockframe *)eframe);

	/*
	 * Compute new randomized interval.  The intervals are uniformly
	 * distributed on [statint - statvar / 2, statint + statvar / 2],
	 * and therefore have mean statint, giving a stathz frequency clock.
	 */
	var = statvar;
	do {
		r = random() & (var - 1);
	} while (r == 0);
	newint = statmin + r;

	sys_pcc2->pcc2_t2ctl = 0;
	sys_pcc2->pcc2_t2cmp = pcc2_timer_us2lim(newint);
	sys_pcc2->pcc2_t2count = 0;		/* should I? */
	sys_pcc2->pcc2_t2irq = stat_reset;
	sys_pcc2->pcc2_t2ctl = PCC2_TCTL_CEN | PCC2_TCTL_COC;
	return (1);
}

#endif /* NPCCTWO */

#if NSYSCON > 0
int
m188_clockintr(void *eframe)
{
	volatile int tmp;

	/* acknowledge the timer interrupt */
	tmp = *(int *volatile)DART_ISR;

	/* stop the timer while the interrupt is being serviced */
	tmp = *(int *volatile)DART_STOPC;

	intrcnt[M88K_CLK_IRQ]++;
	hardclock(eframe);
#if NBUGTTY > 0
	bugtty_chkinput();
#endif /* NBUGTTY */

	tmp = *(int *volatile)DART_STARTC;

#ifdef CLOCK_DEBUG
	if (*(int *volatile)MVME188_IST & DTI_BIT) {
		printf("DTI not clearing!\n");
	}
#endif

	return (1);
}

void
m188_initclock(void)
{
#ifdef CLOCK_DEBUG
	printf("VME188 clock init\n");
#endif
	if (1000000 % hz) {
		printf("cannot get %d Hz clock; using 100 Hz\n", hz);
		hz = 100;
		tick = 1000000 / hz;
	}
	m188_timer_init(tick);
}

void
m188_timer_init(unsigned period)
{
	volatile int imr;
	int counter;

	/* make sure the counter range is proper. */
	if ( period < 9 )
		counter = 2;
	else if ( period > 284421 )
		counter = 65535;
	else
		counter	= period / 4.34;
#ifdef CLOCK_DEBUG
	printf("tick == %d, period == %d\n", tick, period);
	printf("timer will interrupt every %d usec\n", (int) (counter * 4.34));
#endif
	/* clear the counter/timer output OP3 while we program the DART */
	*((int *volatile)DART_OPCR) = 0x00;

	/* do the stop counter/timer command */
	imr = *((int *volatile)DART_STOPC);

	/* set counter/timer to counter mode, clock/16 */
	*((int *volatile)DART_ACR) = 0x30;

	*((int *volatile)DART_CTUR) = counter / 256;	/* set counter MSB */
	*((int *volatile)DART_CTLR) = counter % 256;	/* set counter LSB */
	*((int *volatile)DART_IVR) = SYSCV_TIMER1;	/* set interrupt vec */

	/* give the start counter/timer command */
	/* (yes, this is supposed to be a read) */
	imr = *((int *volatile)DART_STARTC);

	/* set the counter/timer output OP3 */
	*((int *volatile)DART_OPCR) = 0x04;
}

int
m188_statintr(void *eframe)
{
	u_long newint, r, var;

	CIO_LOCK;

	/* increment intr counter */
	intrcnt[M88K_SCLK_IRQ]++;

	statclock((struct clockframe *)eframe);
	write_cio(CIO_CSR1, CIO_GCB|CIO_CIP);  /* Ack the interrupt */

	/*
	 * Compute new randomized interval.  The intervals are uniformly
	 * distributed on [statint - statvar / 2, statint + statvar / 2],
	 * and therefore have mean statint, giving a stathz frequency clock.
	 */
	var = statvar;
	do {
		r = random() & (var - 1);
	} while (r == 0);
	newint = statmin + r;

	/* Load time constant CTC #1 */
	write_cio(CIO_CT1MSB, (newint & 0xff00) >> 8);
	write_cio(CIO_CT1LSB, newint & 0xff);

	/* Start CTC #1 running */
	write_cio(CIO_CSR1, CIO_GCB|CIO_CIP);

	CIO_UNLOCK;
	return (1);
}

void
m188_initstatclock(void)
{
	int statint, minint;

#ifdef CLOCK_DEBUG
	printf("VME188 clock init\n");
#endif
	simple_lock_init(&cio_lock);
	if (stathz == 0)
		stathz = hz;
	if (1000000 % stathz) {
		printf("cannot get %d Hz statclock; using 100 Hz\n", stathz);
		stathz = 100;
	}
	profhz = stathz;		/* always */

	statint = 1000000 / stathz;
	minint = statint / 2 + 100;
	while (statvar > minint)
		statvar >>= 1;
	m188_cio_init(statint);
	statmin = statint - (statvar >> 1);
}

#define CIO_CNTRL 0xfff8300c

/* Write CIO register */
void
write_cio(int reg, u_int8_t val)
{
	int s, i;
	int *volatile cio_ctrl = (int *volatile)CIO_CNTRL;

	s = splclock();
	CIO_LOCK;

	i = *cio_ctrl;				/* goto state 1 */
	*cio_ctrl = 0;				/* take CIO out of RESET */
	i = *cio_ctrl;				/* reset CIO state machine */

	*cio_ctrl = (reg & 0xff);		/* Select register */
	*cio_ctrl = (val & 0xff);		/* Write the value */

	CIO_UNLOCK;
	splx(s);
}

/* Read CIO register */
u_int8_t
read_cio(int reg)
{
	int c;
	int s, i;
	int *volatile cio_ctrl = (int *volatile)CIO_CNTRL;

	s = splclock();
	CIO_LOCK;

	/* Select register */
	*cio_ctrl = (char)(reg & 0xff);
	/* Delay for a short time to allow 8536 to settle */
	for (i = 0; i < 100; i++)
		;
	/* read the value */
	c = *cio_ctrl;
	CIO_UNLOCK;
	splx(s);
	return (c & 0xff);
}

/*
 * Initialize the CTC (8536)
 * Only the counter/timers are used - the IO ports are un-comitted.
 * Channels 1 and 2 are linked to provide a /32 counter.
 */

void
m188_cio_init(unsigned p)
{
	long i;
	short period;

	CIO_LOCK;

	period = p & 0xffff;

	/* Initialize 8536 CTC */

	/* Start by forcing chip into known state */
	read_cio(CIO_MICR);
	write_cio(CIO_MICR, CIO_MICR_RESET);	/* Reset the CTC */
	for (i = 0; i < 1000; i++)	 	/* Loop to delay */
		;

	/* Clear reset and start init seq. */
	write_cio(CIO_MICR, 0x00);

	/* Wait for chip to come ready */
	while ((read_cio(CIO_MICR) & CIO_MICR_RJA) == 0)
		;

	/* Initialize the 8536 */
	write_cio(CIO_MICR,
	    CIO_MICR_MIE | CIO_MICR_NV | CIO_MICR_RJA | CIO_MICR_DLC);
	write_cio(CIO_CTMS1, CIO_CTMS_CSC);	/* Continuous count */
	write_cio(CIO_PDCB, 0xff);		/* set port B to input */

	/* Load time constant CTC #1 */
	write_cio(CIO_CT1MSB, (period & 0xff00) >> 8);
	write_cio(CIO_CT1LSB, period & 0xff);

	/* enable counter 1 */
	write_cio(CIO_MCCR, CIO_MCCR_CT1E | CIO_MCCR_PBE);

	/* Start CTC #1 running */
	write_cio(CIO_CSR1, CIO_GCB | CIO_TCB | CIO_IE);

	CIO_UNLOCK;
}
#endif /* NSYSCON */

void
delay(int us)
{

#if NPCCTWO > 0
	/*
	 * On MVME187 and MVME197, we use the vme system controller for
	 * the delay clock.
	 * Do not go to the real timer until the vme device is attached.
	 * We could directly access the chip, but oh well, who cares.
	 */
	if (sys_vme2 != NULL) {
		sys_vme2->vme2_t1cmp = 0xffffffff;
		sys_vme2->vme2_t1count = 0;
		sys_vme2->vme2_tctl |= VME2_TCTL1_CEN;

		while (sys_vme2->vme2_t1count < us)
			;
		sys_vme2->vme2_tctl &= ~VME2_TCTL1_CEN;
	} else
#endif

	/*
	 * If we can't use a real timer, use a tight loop.
	 */
	{
		volatile int c = (25 * us) / 3;	/* XXX not accurate! */
		while (--c > 0)
			;
	}
}
