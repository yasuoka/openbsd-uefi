/*	$OpenBSD: intr.h,v 1.36 2008/09/18 03:56:25 drahn Exp $ */

/*
 * Copyright (c) 1997 Per Fogelstrom, Opsycon AB and RTMX Inc, USA.
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
 *	This product includes software developed under OpenBSD by
 *	Per Fogelstrom, Opsycon AB, Sweden for RTMX Inc, North Carolina USA.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 */

#ifndef _POWERPC_INTR_H_
#define _POWERPC_INTR_H_

#define	IPL_NONE	0
#define	IPL_SOFT	1
#define	IPL_SOFTCLOCK	2
#define	IPL_SOFTNET	3
#define	IPL_SOFTTTY	4
#define	IPL_BIO		5
#define	IPL_AUDIO	IPL_BIO /* XXX - was defined this val in audio_if.h */
#define	IPL_NET		6
#define	IPL_TTY		7
#define	IPL_VM		8
#define	IPL_CLOCK	9
#define	IPL_HIGH	10
#define	IPL_NUM		11

#define	IST_NONE	0
#define	IST_PULSE	1
#define	IST_EDGE	2
#define	IST_LEVEL	3

#if defined(_KERNEL) && !defined(_LOCORE)

#include <sys/evcount.h>
#include <machine/atomic.h>

#define	PPC_NIRQ	66
#define	PPC_CLK_IRQ	64
#define	PPC_STAT_IRQ	65

void setsoftclock(void);
void clearsoftclock(void);
int  splsoftclock(void);
void setsoftnet(void);
void clearsoftnet(void);
int  splsoftnet(void);

int	splraise(int);
int	spllower(int);
void	splx(int);

typedef int (ppc_splraise_t) (int);
typedef int (ppc_spllower_t) (int);
typedef void (ppc_splx_t) (int);

extern struct ppc_intr_func {
	ppc_splraise_t *raise;
	ppc_spllower_t *lower;
	ppc_splx_t *x;
}ppc_intr_func;

#if 0
/* does it make sense to call directly ?? */
#define	splraise(x)	ppc_intr.raise(x)
#define	spllower(x)	ppc_intr.lower(x)
#define	splx(x)		ppc_intr.x(x)
#endif

extern int ppc_smask[IPL_NUM];

void ppc_smask_init(void);
char *ppc_intr_typename(int type);

void do_pending_int(void);

/* SPL asserts */
#define	splassert(wantipl)	/* nothing */

#define	set_sint(p)	atomic_setbits_int(&curcpu()->ci_ipending, p)

#if 0
#define	SINT_CLOCK	0x10000000
#define	SINT_NET	0x20000000
#define	SINT_TTY	0x40000000
#define	SPL_CLOCK	0x80000000
#define	SINT_MASK	(SINT_CLOCK|SINT_NET|SINT_TTY)
#endif

#define	splbio()	splraise(IPL_BIO)
#define	splnet()	splraise(IPL_NET)
#define	spltty()	splraise(IPL_TTY)
#define	splaudio()	splraise(IPL_AUDIO)
#define	splclock()	splraise(IPL_CLOCK)
#define	splvm()		splraise(IPL_VM)
#define	splsched()	splhigh()
#define	spllock()	splhigh()
#define	splstatclock()	splhigh()
#define	splsoftclock()	splraise(IPL_SOFTCLOCK)
#define	splsoftnet()	splraise(IPL_SOFTNET)
#define	splsofttty()	splraise(IPL_SOFTTTY)

#define	setsoftclock()	set_sint(SI_TO_IRQBIT(SI_SOFTCLOCK))
#define	setsoftnet()	set_sint(SI_TO_IRQBIT(SI_SOFTNET))
#define	setsofttty()	set_sint(SI_TO_IRQBIT(SI_SOFTTTY))

#define	splhigh()	splraise(IPL_HIGH)
#define	spl0()		spllower(IPL_NONE)

/*
 *	Interrupt control struct used to control the ICU setup.
 */

struct intrhand {
	TAILQ_ENTRY(intrhand) ih_list;
	int		(*ih_fun)(void *);
	void		*ih_arg;
	struct evcount	ih_count;
	int		ih_level;
	int		ih_irq;
	char		*ih_what;
};

struct intrq {
	TAILQ_HEAD(, intrhand) iq_list; /* handler list */
	int iq_ipl;			/* IPL_ to mask while handling */ 
	int iq_ist;			/* share type */
};

extern int ppc_configed_intr_cnt;
#define	MAX_PRECONF_INTR 16
extern struct intrhand ppc_configed_intr[MAX_PRECONF_INTR];
void softnet(int isr);

#define	SI_TO_IRQBIT(x) (1 << (x))

#define	SI_SOFT			0	/* for IPL_SOFT */
#define	SI_SOFTCLOCK		1	/* for IPL_SOFTCLOCK */
#define	SI_SOFTNET		2	/* for IPL_SOFTNET */
#define	SI_SOFTTTY		3	/* for IPL_SOFTSERIAL */

#define	SI_NQUEUES		4

#define SI_QUEUENAMES {		\
	"generic",		\
	"clock",		\
	"net",			\
	"serial",		\
}

#define PPC_IPI_NOP		0
#define PPC_IPI_DDB		1

void ppc_send_ipi(struct cpu_info *, int);

#define PPC_IPI_NOP		0
#define PPC_IPI_DDB		1

void ppc_send_ipi(struct cpu_info *, int);

#endif /* _LOCORE */
#endif /* _POWERPC_INTR_H_ */
