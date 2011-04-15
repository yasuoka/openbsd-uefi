/*	$OpenBSD: macintr.c,v 1.42 2011/04/15 20:52:55 deraadt Exp $	*/

/*-
 * Copyright (c) 1995 Per Fogelstrom
 * Copyright (c) 1993, 1994 Charles M. Hannum.
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
 *	@(#)isa.c	7.2 (Berkeley) 5/12/91
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/ioctl.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/systm.h>

#include <uvm/uvm.h>
#include <ddb/db_var.h>

#include <machine/atomic.h>
#include <machine/autoconf.h>
#include <machine/intr.h>
#include <machine/psl.h>
#include <machine/pio.h>
#include <machine/powerpc.h>

#include <dev/ofw/openfirm.h>

#define ICU_LEN 64
#define LEGAL_IRQ(x) ((x >= 0) && (x < ICU_LEN))

int m_intrtype[ICU_LEN], m_intrmask[ICU_LEN], m_intrlevel[ICU_LEN];
struct intrhand *m_intrhand[ICU_LEN];
int m_hwirq[ICU_LEN], m_virq[64];
unsigned int imen_m = 0xffffffff;
int m_virq_max = 0;

static int fakeintr(void *);
static char *intr_typename(int type);
static void intr_calculatemasks(void);
static void enable_irq(int x);
static __inline int cntlzw(int x);
static int mapirq(int irq);
static int read_irq(void);
static void mac_intr_do_pending_int(void);

extern u_int32_t *heathrow_FCR;

#define HWIRQ_MAX 27
#define HWIRQ_MASK 0x0fffffff

#define INT_STATE_REG0  (interrupt_reg + 0x20)
#define INT_ENABLE_REG0 (interrupt_reg + 0x24)
#define INT_CLEAR_REG0  (interrupt_reg + 0x28)
#define INT_LEVEL_REG0  (interrupt_reg + 0x2c)
#define INT_STATE_REG1  (INT_STATE_REG0  - 0x10)
#define INT_ENABLE_REG1 (INT_ENABLE_REG0 - 0x10)
#define INT_CLEAR_REG1  (INT_CLEAR_REG0  - 0x10)
#define INT_LEVEL_REG1  (INT_LEVEL_REG0  - 0x10)

struct macintr_softc {
	struct device sc_dev;
};

int	macintr_match(struct device *parent, void *cf, void *aux);
void	macintr_attach(struct device *, struct device *, void *);
void	mac_do_pending_int(void);
void	mac_ext_intr(void);

struct cfattach macintr_ca = {
	sizeof(struct macintr_softc),
	macintr_match,
	macintr_attach
};

struct cfdriver macintr_cd = {
	NULL, "macintr", DV_DULL
};

int
macintr_match(struct device *parent, void *cf, void *aux)
{
	struct confargs *ca = aux;
	char type[40];

	/*
	 * Match entry according to "present" openfirmware entry.
	 */
	if (strcmp(ca->ca_name, "interrupt-controller") == 0 ) {
		OF_getprop(ca->ca_node, "device_type", type, sizeof(type));
		if (strcmp(type,  "interrupt-controller") == 0)
			return 1;
	}

	/*
	 * Check name for legacy interrupt controller, this is
	 * faked to allow old firmware which does not have an entry
	 * to attach to this device.
	 */
	if (strcmp(ca->ca_name, "legacy-interrupt-controller") == 0 )
		return 1;
	return 0;
}

u_int8_t *interrupt_reg;
typedef void  (void_f) (void);
extern void_f *pending_int_f;
int macintr_prog_button (void *arg);

intr_establish_t macintr_establish;
intr_disestablish_t macintr_disestablish;
extern intr_establish_t *mac_intr_establish_func;
extern intr_disestablish_t *mac_intr_disestablish_func;
void macintr_collect_preconf_intr(void);

void
macintr_attach(struct device *parent, struct device *self, void *aux)
{
	struct confargs *ca = aux;
	extern intr_establish_t *intr_establish_func;
	extern intr_disestablish_t *intr_disestablish_func;

	interrupt_reg = (void *)mapiodev(ca->ca_baseaddr,0x100); /* XXX */

	install_extint(mac_ext_intr);
	pending_int_f = mac_intr_do_pending_int;
	intr_establish_func  = macintr_establish;
	intr_disestablish_func  = macintr_disestablish;
	mac_intr_establish_func  = macintr_establish;
	mac_intr_disestablish_func  = macintr_disestablish;

	macintr_collect_preconf_intr();

	mac_intr_establish(parent, 0x14, IST_LEVEL, IPL_HIGH,
	    macintr_prog_button, (void *)0x14, "progbutton");

	ppc_intr_enable(1);

	printf("\n");
}

void
macintr_collect_preconf_intr()
{
	int i;
	for (i = 0; i < ppc_configed_intr_cnt; i++) {
#ifdef DEBUG
		printf("\n\t%s irq %d level %d fun %p arg %p",
			ppc_configed_intr[i].ih_what,
			ppc_configed_intr[i].ih_irq,
			ppc_configed_intr[i].ih_level,
			ppc_configed_intr[i].ih_fun,
			ppc_configed_intr[i].ih_arg
			);
#endif
		macintr_establish(NULL,
			ppc_configed_intr[i].ih_irq,
			IST_LEVEL,
			ppc_configed_intr[i].ih_level,
			ppc_configed_intr[i].ih_fun,
			ppc_configed_intr[i].ih_arg,
			ppc_configed_intr[i].ih_what);
	}
}


/*
 * programmer_button function to fix args to Debugger.
 * deal with any enables/disables, if necessary.
 */
int
macintr_prog_button (void *arg)
{
#ifdef DDB
	if (db_console)
		Debugger();
#else
	printf("programmer button pressed, debugger not available\n");
#endif
	return 1;
}

static int
fakeintr(void *arg)
{

	return 0;
}

/*
 * Register an interrupt handler.
 */
void *
macintr_establish(void * lcv, int irq, int type, int level,
    int (*ih_fun)(void *), void *ih_arg, const char *name)
{
	struct intrhand **p, *q, *ih;
	static struct intrhand fakehand;

	fakehand.ih_next = NULL;
	fakehand.ih_fun  = fakeintr;

#if 0
printf("macintr_establish, hI %d L %d ", irq, type);
printf("addr reg0 %x\n", INT_STATE_REG0);
#endif
	irq = mapirq(irq);
#if 0
printf("vI %d ", irq);
#endif

	/* no point in sleeping unless someone can free memory. */
	ih = malloc(sizeof *ih, M_DEVBUF, cold ? M_NOWAIT : M_WAITOK);
	if (ih == NULL)
		panic("intr_establish: can't malloc handler info");

	if (!LEGAL_IRQ(irq) || type == IST_NONE)
		panic("intr_establish: bogus irq or type");

	switch (m_intrtype[irq]) {
	case IST_NONE:
		m_intrtype[irq] = type;
		break;
	case IST_EDGE:
		intr_shared_edge = 1;
		/* FALLTHROUGH */
	case IST_LEVEL:
		if (type == m_intrtype[irq])
			break;
	case IST_PULSE:
		if (type != IST_NONE)
			panic("intr_establish: can't share %s with %s",
			    intr_typename(m_intrtype[irq]),
			    intr_typename(type));
		break;
	}

	/*
	 * Figure out where to put the handler.
	 * This is O(N^2), but we want to preserve the order, and N is
	 * generally small.
	 */
	for (p = &m_intrhand[irq]; (q = *p) != NULL; p = &q->ih_next)
		;

	/*
	 * Actually install a fake handler momentarily, since we might be doing
	 * this with interrupts enabled and DON'T WANt the real routine called
	 * until masking is set up.
	 */
	fakehand.ih_level = level;
	*p = &fakehand;

	intr_calculatemasks();

	/*
	 * Poke the real handler in now.
	 */
	ih->ih_fun = ih_fun;
	ih->ih_arg = ih_arg;
	ih->ih_next = NULL;
	ih->ih_level = level;
	ih->ih_irq = irq;
	evcount_attach(&ih->ih_count, name, &m_hwirq[irq]);
	*p = ih;

	return (ih);
}

/*
 * Deregister an interrupt handler.
 */
void
macintr_disestablish(void *lcp, void *arg)
{
	struct intrhand *ih = arg;
	int irq = ih->ih_irq;
	struct intrhand **p, *q;

	if (!LEGAL_IRQ(irq))
		panic("intr_disestablish: bogus irq");

	/*
	 * Remove the handler from the chain.
	 * This is O(n^2), too.
	 */
	for (p = &m_intrhand[irq]; (q = *p) != NULL && q != ih; p = &q->ih_next)
		;
	if (q)
		*p = q->ih_next;
	else
		panic("intr_disestablish: handler not registered");

	evcount_detach(&ih->ih_count);
	free((void *)ih, M_DEVBUF);

	intr_calculatemasks();

	if (m_intrhand[irq] == NULL)
		m_intrtype[irq] = IST_NONE;
}


static char *
intr_typename(int type)
{
	switch (type) {
        case IST_NONE :
		return ("none");
        case IST_PULSE:
		return ("pulsed");
        case IST_EDGE:
		return ("edge-triggered");
        case IST_LEVEL:
		return ("level-triggered");
	default:
		panic("intr_typename: invalid type %d", type);
#if 1 /* XXX */
		return ("unknown");
#endif
	}
}
/*
 * Recalculate the interrupt masks from scratch.
 * We could code special registry and deregistry versions of this function that
 * would be faster, but the code would be nastier, and we don't expect this to
 * happen very much anyway.
 */
static void
intr_calculatemasks()
{
	int irq, level;
	struct intrhand *q;

	/* First, figure out which levels each IRQ uses. */
	for (irq = 0; irq < ICU_LEN; irq++) {
		register int levels = 0;
		for (q = m_intrhand[irq]; q; q = q->ih_next)
			levels |= 1 << q->ih_level;
		m_intrlevel[irq] = levels;
	}

	/* Then figure out which IRQs use each level. */
	for (level = IPL_NONE; level < IPL_NUM; level++) {
		register int irqs = 0;
		for (irq = 0; irq < ICU_LEN; irq++)
			if (m_intrlevel[irq] & (1 << level))
				irqs |= 1 << irq;
		cpu_imask[level] = irqs | SINT_ALLMASK;
	}

	/*
	 * There are tty, network and disk drivers that use free() at interrupt
	 * time, so vm > (tty | net | bio).
	 *
	 * Enforce a hierarchy that gives slow devices a better chance at not
	 * dropping data.
	 */
	cpu_imask[IPL_NET] |= cpu_imask[IPL_BIO];
	cpu_imask[IPL_TTY] |= cpu_imask[IPL_NET];
	cpu_imask[IPL_VM] |= cpu_imask[IPL_TTY];
	cpu_imask[IPL_CLOCK] |= cpu_imask[IPL_VM] | SPL_CLOCKMASK;

	/*
	 * These are pseudo-levels.
	 */
	cpu_imask[IPL_NONE] = 0x00000000;
	cpu_imask[IPL_HIGH] = 0xffffffff;

	/* And eventually calculate the complete masks. */
	for (irq = 0; irq < ICU_LEN; irq++) {
		register int irqs = 1 << irq;
		for (q = m_intrhand[irq]; q; q = q->ih_next)
			irqs |= cpu_imask[q->ih_level];
		m_intrmask[irq] = irqs | SINT_ALLMASK;
	}

	/* Lastly, determine which IRQs are actually in use. */
	{
		register int irqs = 0;
		for (irq = 0; irq < ICU_LEN; irq++) {
			if (m_intrhand[irq])
				irqs |= 1 << irq;
		}
		imen_m = ~irqs;
		enable_irq(~imen_m);
	}
}
static void
enable_irq(int x)
{
	int state0, state1, v;
	int irq;

	x &= HWIRQ_MASK;	/* XXX Higher bits are software interrupts. */

	state0 = state1 = 0;
	while (x) {
		v = 31 - cntlzw(x);
		irq = m_hwirq[v];
		if (irq < 32)
			state0 |= 1 << irq;
		else
			state1 |= 1 << (irq - 32);

		x &= ~(1 << v);
	}

	if (heathrow_FCR)
		out32rb(INT_ENABLE_REG1, state1);

	out32rb(INT_ENABLE_REG0, state0);
}

int m_virq_inited = 0;

/*
 * Map 64 irqs into 32 (bits).
 */
static int
mapirq(int irq)
{
	int v;
	int i;

	if (m_virq_inited == 0) {
		m_virq_max = 0;
		for (i = 0; i < ICU_LEN; i++) {
			m_virq[i] = 0;
		}
		m_virq_inited = 1;
	}

	/* irq in table already? */
	if (m_virq[irq] != 0)
		return m_virq[irq];

	if (irq < 0 || irq >= 64)
		panic("invalid irq %d", irq);
	m_virq_max++;
	v = m_virq_max;
	if (v > HWIRQ_MAX)
		panic("virq overflow");

	m_hwirq[v] = irq;
	m_virq[irq] = v;
#if 0
printf("\nmapirq %x to %x\n", irq, v);
#endif

	return v;
}

/*
 * Count leading zeros.
 */
static __inline int
cntlzw(int x)
{
	int a;

	__asm __volatile ("cntlzw %0,%1" : "=r"(a) : "r"(x));

	return a;
}

/*
 * external interrupt handler
 */
void
mac_ext_intr()
{
	int irq = 0;
	int o_imen, r_imen;
	int pcpl, ret;
	struct cpu_info *ci = curcpu();
	struct intrhand *ih;
	volatile unsigned long int_state;

	pcpl = ci->ci_cpl;	/* Turn off all */

	int_state = read_irq();
	if (int_state == 0)
		goto out;

start:
	irq = 31 - cntlzw(int_state);

	o_imen = imen_m;
	r_imen = 1 << irq;

	if ((ci->ci_cpl & r_imen) != 0) {
		/* Masked! Mark this as pending. */
		ci->ci_ipending |= r_imen;
		imen_m |= r_imen;
		enable_irq(~imen_m);
	} else {
		splraise(m_intrmask[irq]);

		ih = m_intrhand[irq];
		while (ih) {
			ret = ((*ih->ih_fun)(ih->ih_arg));
			if (ret) {
				ih->ih_count.ec_count++;
				if (intr_shared_edge == 0 && ret == 1)
					break;
			}
			ih = ih->ih_next;
		}

		uvmexp.intrs++;
	}
	int_state &= ~r_imen;
	if (int_state)
		goto start;

out:
	splx(pcpl);	/* Process pendings. */
}

void
mac_intr_do_pending_int()
{
	struct cpu_info *ci = curcpu();
	struct intrhand *ih;
	int irq;
	int pcpl;
	int hwpend;
	int s;

	if (ci->ci_iactive)
		return;

	ci->ci_iactive = 1;
	pcpl = splhigh();		/* Turn off all */
	s = ppc_intr_disable();

	hwpend = ci->ci_ipending & ~pcpl;	/* Do now unmasked pendings */
	imen_m &= ~hwpend;
	enable_irq(~imen_m);
	hwpend &= HWIRQ_MASK;
	while (hwpend) {
		irq = 31 - cntlzw(hwpend);
		hwpend &= ~(1L << irq);
		ih = m_intrhand[irq];
		while(ih) {
			if ((*ih->ih_fun)(ih->ih_arg))
				ih->ih_count.ec_count++;
			ih = ih->ih_next;
		}
	}

	/*out32rb(INT_ENABLE_REG, ~imen_m);*/

	do {
		if((ci->ci_ipending & SINT_CLOCK) & ~pcpl) {
			ci->ci_ipending &= ~SINT_CLOCK;
			softintr_dispatch(SI_SOFTCLOCK);
		}
		if((ci->ci_ipending & SINT_NET) & ~pcpl) {
			ci->ci_ipending &= ~SINT_NET;
			softintr_dispatch(SI_SOFTNET);
		}
		if((ci->ci_ipending & SINT_TTY) & ~pcpl) {
			ci->ci_ipending &= ~SINT_TTY;
			softintr_dispatch(SI_SOFTTTY);
		}
	} while ((ci->ci_ipending & SINT_ALLMASK) & ~pcpl);
	ci->ci_ipending &= pcpl;
	ci->ci_cpl = pcpl;	/* Don't use splx... we are here already! */
	ppc_intr_enable(s);
	ci->ci_iactive = 0;
}

static int
read_irq()
{
	int rv = 0;
	int state0, state1, p;
	int state0save, state1save;

	state0 = in32rb(INT_STATE_REG0);
	if (state0)
		out32rb(INT_CLEAR_REG0, state0);
	state0save = state0;
	while (state0) {
		p = 31 - cntlzw(state0);
		rv |= 1 << m_virq[p];
		state0 &= ~(1 << p);
	}

	if (heathrow_FCR)			/* has heathrow? */
		state1 = in32rb(INT_STATE_REG1);
	else
		state1 = 0;

	if (state1)
		out32rb(INT_CLEAR_REG1, state1);
	state1save = state1;
	while (state1) {
		p = 31 - cntlzw(state1);
		rv |= 1 << m_virq[p + 32];
		state1 &= ~(1 << p);
	}
#if 0
printf("mac_intr int_stat 0:%x 1:%x\n", state0save, state1save);
#endif

	/* 1 << 0 is invalid. */
	return rv & ~1;
}
