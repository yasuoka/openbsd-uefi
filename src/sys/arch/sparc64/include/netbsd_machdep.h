/*	$OpenBSD: netbsd_machdep.h,v 1.2 2002/03/14 01:26:45 millert Exp $	*/

#ifndef _NETBSD_MACHDEP_H
#define _NETBSD_MACHDEP_H

struct netbsd_sigcontext {
	int	sc_onstack;
	int	__sc_mask13;
	long	sc_sp;
	long	sc_pc;
	long	sc_npc;
	long	sc_tstate;
	long	sc_g1;
	long	sc_o0;
	netbsd_sigset_t sc_mask;
};

#ifdef _KERNEL
void netbsd_sendsig(sig_t, int, int, u_long, int, union sigval);
#endif

#endif /* _NETBSD_MACHDEP_H */
