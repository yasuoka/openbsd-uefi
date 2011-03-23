/*	$OpenBSD: intr.h,v 1.22 2011/03/23 16:54:36 pirofti Exp $	*/
/*	$NetBSD: intr.h,v 1.9 1998/08/12 06:58:42 scottr Exp $	*/

/*
 * Copyright (C) 1997 Scott Reynolds
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _MACHINE_INTR_H_
#define _MACHINE_INTR_H_

#include <machine/psl.h>

#ifdef _KERNEL

/*
 * splnet must block hardware network interrupts
 * splvm must be > spltty
 */
extern u_short	mac68k_ttyipl;
extern u_short	mac68k_netipl;
extern u_short	mac68k_vmipl;
extern u_short	mac68k_audioipl;
extern u_short	mac68k_clockipl;
extern u_short	mac68k_statclockipl;

/*
 * Interrupt "levels".  These are a more abstract representation
 * of interrupt levels, and do not have the same meaning as m68k
 * CPU interrupt levels.  They serve two purposes:
 *
 *	- properly order ISRs in the list for that CPU ipl
 *	- compute CPU PSL values for the spl*() calls.
 */
#define	IPL_NONE	0
#define	IPL_SOFTINT	1
#define	IPL_BIO		2
#define	IPL_AUDIO	PSLTOIPL(mac68k_audioipl)
#define	IPL_NET		PSLTOIPL(mac68k_netipl)
#define	IPL_TTY		PSLTOIPL(mac68k_ttyipl)
#define	IPL_VM		PSLTOIPL(mac68k_vmipl)
#define	IPL_CLOCK	PSLTOIPL(mac68k_clockipl)
#define	IPL_STATCLOCK	PSLTOIPL(mac68k_statclockipl)
#define	IPL_SCHED	7
#define	IPL_HIGH	7

#define	MD_IPLTOPSL(ipl)	IPLTOPSL(ipl)

/*
 * These should be used for:
 * 1) ensuring mutual exclusion (why use processor level?)
 * 2) allowing faster devices to take priority
 *
 * Note that on the Mac, most things are masked at spl1, almost
 * everything at spl2, and everything but the panic switch and
 * power at spl4.
 */
#define	splsoft()		_splraise(PSL_S | PSL_IPL1)
#define	splsoftclock()		splsoft()
#define	splsoftnet()		splsoft()
#define	spltty()		_splraise(mac68k_ttyipl)
#define	splbio()		_splraise(PSL_S | PSL_IPL2)
#define	splnet()		_splraise(mac68k_netipl)
#define	splvm()			_splraise(mac68k_vmipl)
#define	splaudio()		_splraise(mac68k_audioipl)
#define	splclock()		_splraise(mac68k_clockipl)
#define	splstatclock()		_splraise(mac68k_statclockipl)
#define	splserial()		_splraise(PSL_S | PSL_IPL4)
#define	splhigh()		_spl(PSL_S | PSL_IPL7)
#define	splsched()		splhigh()

/* These spl calls are _not_ to be used by machine-independent code. */
#define	splzs()			splserial()

/* watch out for side effects */
#define splx(s)         	((s) & PSL_IPL ? _spl(s) : spl0())

#include <m68k/intr.h>		/* soft interrupt support */

/* intr.c */
void	intr_init(void);
void	intr_establish(int (*)(void *), void *, int, const char *);
void	intr_disestablish(int);
void	intr_dispatch(int);

/* locore.s */
int	spl0(void);

/*
 * Interrupt handler.
 * There is no support for shared interrupts at the moment.
 */
#include <sys/evcount.h>
struct intrhand {
	int		(*ih_fn)(void *);
	void		*ih_arg;
	int		ih_ipl;
	struct evcount	ih_count;
};
#endif /* _KERNEL */

#endif /* _MACHINE_INTR_H_ */
