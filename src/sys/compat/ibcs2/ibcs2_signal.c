/*	$NetBSD: ibcs2_signal.c,v 1.8 1996/05/03 17:05:27 christos Exp $	*/

/*
 * Copyright (c) 1995 Scott Bartram
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
 *    derived from this software without specific prior written permission
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/namei.h>
#include <sys/proc.h>
#include <sys/filedesc.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/kernel.h>
#include <sys/signal.h>
#include <sys/signalvar.h>
#include <sys/malloc.h>

#include <sys/syscallargs.h>

#include <compat/ibcs2/ibcs2_types.h>
#include <compat/ibcs2/ibcs2_signal.h>
#include <compat/ibcs2/ibcs2_syscallargs.h>
#include <compat/ibcs2/ibcs2_util.h>

#define sigemptyset(s)		bzero((s), sizeof(*(s)))
#define sigismember(s, n)	(*(s) & sigmask(n))
#define sigaddset(s, n)		(*(s) |= sigmask(n))

#define	ibcs2_sigmask(n)	(1 << ((n) - 1))
#define ibcs2_sigemptyset(s)	bzero((s), sizeof(*(s)))
#define ibcs2_sigismember(s, n)	(*(s) & ibcs2_sigmask(n))
#define ibcs2_sigaddset(s, n)	(*(s) |= ibcs2_sigmask(n))

int bsd_to_ibcs2_sig[] = {
	0,			/* 0 */
	IBCS2_SIGHUP,		/* 1 */
	IBCS2_SIGINT,		/* 2 */
	IBCS2_SIGQUIT,		/* 3 */
	IBCS2_SIGILL,		/* 4 */
	IBCS2_SIGTRAP,		/* 5 */
	IBCS2_SIGABRT,		/* 6 */
	IBCS2_SIGEMT,		/* 7 */
	IBCS2_SIGFPE,		/* 8 */
	IBCS2_SIGKILL,		/* 9 */
	IBCS2_SIGBUS,		/* 10 */
	IBCS2_SIGSEGV,		/* 11 */
	IBCS2_SIGSYS,		/* 12 */
	IBCS2_SIGPIPE,		/* 13 */
	IBCS2_SIGALRM,		/* 14 */
	IBCS2_SIGTERM,		/* 15 */
	0,			/* 16 - SIGURG */
	IBCS2_SIGSTOP,		/* 17 */
	IBCS2_SIGTSTP,		/* 18 */
	IBCS2_SIGCONT,		/* 19 */
	IBCS2_SIGCLD,		/* 20 */
	IBCS2_SIGTTIN,		/* 21 */
	IBCS2_SIGTTOU,		/* 22 */
	IBCS2_SIGPOLL,		/* 23 */
	0,			/* 24 - SIGXCPU */
	0,			/* 25 - SIGXFSZ */
	IBCS2_SIGVTALRM,	/* 26 */
	IBCS2_SIGPROF,		/* 27 */
	IBCS2_SIGWINCH,		/* 28 */
	0,			/* 29 */
	IBCS2_SIGUSR1,		/* 30 */
	IBCS2_SIGUSR2,		/* 31 */
};

int ibcs2_to_bsd_sig[] = {
	0,			/* 0 */
	SIGHUP,			/* 1 */
	SIGINT,			/* 2 */
	SIGQUIT,		/* 3 */
	SIGILL,			/* 4 */
	SIGTRAP,		/* 5 */
	SIGABRT,		/* 6 */
	SIGEMT,			/* 7 */
	SIGFPE,			/* 8 */
	SIGKILL,		/* 9 */
	SIGBUS,			/* 10 */
	SIGSEGV,		/* 11 */
	SIGSYS,			/* 12 */
	SIGPIPE,		/* 13 */
	SIGALRM,		/* 14 */
	SIGTERM,		/* 15 */
	SIGUSR1,		/* 16 */
	SIGUSR2,		/* 17 */
	SIGCHLD,		/* 18 */
	0,			/* 19 - SIGPWR */
	SIGWINCH,		/* 20 */
	0,			/* 21 */
	SIGIO,			/* 22 */
	SIGSTOP,		/* 23 */
	SIGTSTP,		/* 24 */
	SIGCONT,		/* 25 */
	SIGTTIN,		/* 26 */
	SIGTTOU,		/* 27 */
	SIGVTALRM,		/* 28 */
	SIGPROF,		/* 29 */
	0,			/* 30 */
	0,			/* 31 */
};

void ibcs2_to_bsd_sigset __P((const ibcs2_sigset_t *, sigset_t *));
void bsd_to_ibcs2_sigset __P((const sigset_t *, ibcs2_sigset_t *));
void ibcs2_to_bsd_sigaction __P((struct ibcs2_sigaction *,
    struct sigaction *));
void bsd_to_ibcs2_sigaction __P((struct sigaction *, struct ibcs2_sigaction *));

void
ibcs2_to_bsd_sigset(iss, bss)
	const ibcs2_sigset_t *iss;
	sigset_t *bss;
{
	int i, newsig;

	sigemptyset(bss);
	for (i = 1; i < IBCS2_NSIG; i++) {
		if (ibcs2_sigismember(iss, i)) {
			newsig = ibcs2_to_bsd_sig[i];
			if (newsig)
				sigaddset(bss, newsig);
		}
	}
}

void
bsd_to_ibcs2_sigset(bss, iss)
	const sigset_t *bss;
	ibcs2_sigset_t *iss;
{
	int i, newsig;

	ibcs2_sigemptyset(iss);
	for (i = 1; i < NSIG; i++) {
		if (sigismember(bss, i)) {
			newsig = bsd_to_ibcs2_sig[i];
			if (newsig)
				ibcs2_sigaddset(iss, newsig);
		}
	}
}

void
ibcs2_to_bsd_sigaction(isa, bsa)
	struct ibcs2_sigaction *isa;
	struct sigaction *bsa;
{

	bsa->sa_handler = isa->sa_handler;
	ibcs2_to_bsd_sigset(&isa->sa_mask, &bsa->sa_mask);
	bsa->sa_flags = 0;
	if ((isa->sa_flags & IBCS2_SA_NOCLDSTOP) != 0)
		bsa->sa_flags |= SA_NOCLDSTOP;
}

void
bsd_to_ibcs2_sigaction(bsa, isa)
	struct sigaction *bsa;
	struct ibcs2_sigaction *isa;
{

	isa->sa_handler = bsa->sa_handler;
	bsd_to_ibcs2_sigset(&bsa->sa_mask, &isa->sa_mask);
	isa->sa_flags = 0;
	if ((bsa->sa_flags & SA_NOCLDSTOP) != 0)
		isa->sa_flags |= SA_NOCLDSTOP;
}

int
ibcs2_sys_sigaction(p, v, retval)
	register struct proc *p;
	void *v;
	register_t *retval;
{
	struct ibcs2_sys_sigaction_args /* {
		syscallarg(int) signum;
		syscallarg(struct ibcs2_sigaction *) nsa;
		syscallarg(struct ibcs2_sigaction *) osa;
	} */ *uap = v;
	struct ibcs2_sigaction *nisa, *oisa, tmpisa;
	struct sigaction *nbsa, *obsa, tmpbsa;
	struct sys_sigaction_args sa;
	caddr_t sg;
	int error;

	sg = stackgap_init(p->p_emul);
	nisa = SCARG(uap, nsa);
	oisa = SCARG(uap, osa);

	if (oisa != NULL)
		obsa = stackgap_alloc(&sg, sizeof(struct sigaction));
	else
		obsa = NULL;

	if (nisa != NULL) {
		nbsa = stackgap_alloc(&sg, sizeof(struct sigaction));
		if ((error = copyin(nisa, &tmpisa, sizeof(tmpisa))) != 0)
			return error;
		ibcs2_to_bsd_sigaction(&tmpisa, &tmpbsa);
		if ((error = copyout(&tmpbsa, nbsa, sizeof(tmpbsa))) != 0)
			return error;
	} else
		nbsa = NULL;

	SCARG(&sa, signum) = ibcs2_to_bsd_sig[SCARG(uap, signum)];
	SCARG(&sa, nsa) = nbsa;
	SCARG(&sa, osa) = obsa;

	if ((error = sys_sigaction(p, &sa, retval)) != 0)
		return error;

	if (oisa != NULL) {
		if ((error = copyin(obsa, &tmpbsa, sizeof(tmpbsa))) != 0)
			return error;
		bsd_to_ibcs2_sigaction(&tmpbsa, &tmpisa);
		if ((error = copyout(&tmpisa, oisa, sizeof(tmpisa))) != 0)
			return error;
	}

	return 0;
}

int
ibcs2_sys_sigsys(p, v, retval)
	register struct proc *p;
	void *v;
	register_t *retval;
{
	struct ibcs2_sys_sigsys_args /* {
		syscallarg(int) sig;
		syscallarg(ibcs2_sig_t) fp;
	} */ *uap = v;
	int signum = ibcs2_to_bsd_sig[IBCS2_SIGNO(SCARG(uap, sig))];
	int error;
	caddr_t sg = stackgap_init(p->p_emul);

	if (signum <= 0 || signum >= IBCS2_NSIG) {
		if (IBCS2_SIGCALL(SCARG(uap, sig)) == IBCS2_SIGNAL_MASK ||
		    IBCS2_SIGCALL(SCARG(uap, sig)) == IBCS2_SIGSET_MASK)
			*retval = (int)IBCS2_SIG_ERR;
		return EINVAL;
	}
	
	switch (IBCS2_SIGCALL(SCARG(uap, sig))) {
	/*
	 * sigset is identical to signal() except that SIG_HOLD is allowed as
	 * an action.
	 */
	case IBCS2_SIGSET_MASK:
		/*
		 * sigset is identical to signal() except
		 * that SIG_HOLD is allowed as
		 * an action.
		 */
		if (SCARG(uap, fp) == IBCS2_SIG_HOLD) {
			struct sys_sigprocmask_args sa;

			SCARG(&sa, how) = SIG_BLOCK;
			SCARG(&sa, mask) = sigmask(signum);
			return sys_sigprocmask(p, &sa, retval);
		}
		/* FALLTHROUGH */

	case IBCS2_SIGNAL_MASK:
		{
			struct sys_sigaction_args sa_args;
			struct sigaction *nbsa, *obsa, sa;

			nbsa = stackgap_alloc(&sg, sizeof(struct sigaction));
			obsa = stackgap_alloc(&sg, sizeof(struct sigaction));
			SCARG(&sa_args, signum) = signum;
			SCARG(&sa_args, nsa) = nbsa;
			SCARG(&sa_args, osa) = obsa;

			sa.sa_handler = SCARG(uap, fp);
			sigemptyset(&sa.sa_mask);
			sa.sa_flags = 0;
#if 0
			if (signum != SIGALRM)
				sa.sa_flags = SA_RESTART;
#endif
			if ((error = copyout(&sa, nbsa, sizeof(sa))) != 0)
				return error;
			if ((error = sys_sigaction(p, &sa_args, retval)) != 0) {
				DPRINTF(("signal: sigaction failed: %d\n",
					 error));
				*retval = (int)IBCS2_SIG_ERR;
				return error;
			}
			if ((error = copyin(obsa, &sa, sizeof(sa))) != 0)
				return error;
			*retval = (int)sa.sa_handler;
			return 0;
		}
		
	case IBCS2_SIGHOLD_MASK:
		{
			struct sys_sigprocmask_args sa;

			SCARG(&sa, how) = SIG_BLOCK;
			SCARG(&sa, mask) = sigmask(signum);
			return sys_sigprocmask(p, &sa, retval);
		}
		
	case IBCS2_SIGRELSE_MASK:
		{
			struct sys_sigprocmask_args sa;

			SCARG(&sa, how) = SIG_UNBLOCK;
			SCARG(&sa, mask) = sigmask(signum);
			return sys_sigprocmask(p, &sa, retval);
		}
		
	case IBCS2_SIGIGNORE_MASK:
		{
			struct sys_sigaction_args sa_args;
			struct sigaction *bsa, sa;

			bsa = stackgap_alloc(&sg, sizeof(struct sigaction));
			SCARG(&sa_args, signum) = signum;
			SCARG(&sa_args, nsa) = bsa;
			SCARG(&sa_args, osa) = NULL;

			sa.sa_handler = SIG_IGN;
			sigemptyset(&sa.sa_mask);
			sa.sa_flags = 0;
			if ((error = copyout(&sa, bsa, sizeof(sa))) != 0)
				return error;
			if ((error = sys_sigaction(p, &sa_args, retval)) != 0) {
				DPRINTF(("sigignore: sigaction failed\n"));
				return error;
			}
			return 0;
		}
		
	case IBCS2_SIGPAUSE_MASK:
		{
			struct sys_sigsuspend_args sa;

			SCARG(&sa, mask) = p->p_sigmask &~ sigmask(signum);
			return sys_sigsuspend(p, &sa, retval);
		}
		
	default:
		return ENOSYS;
	}
}

int
ibcs2_sys_sigprocmask(p, v, retval)
	register struct proc *p;
	void *v;
	register_t *retval;
{
	struct ibcs2_sys_sigprocmask_args /* {
		syscallarg(int) how;
		syscallarg(ibcs2_sigset_t *) set;
		syscallarg(ibcs2_sigset_t *) oset;
	} */ *uap = v;
	ibcs2_sigset_t iss;
	sigset_t bss;
	int error = 0;

	if (SCARG(uap, oset) != NULL) {
		/* Fix the return value first if needed */
		bsd_to_ibcs2_sigset(&p->p_sigmask, &iss);
		if ((error = copyout(&iss, SCARG(uap, oset), sizeof(iss))) != 0)
			return error;
	}
		
	if (SCARG(uap, set) == NULL)
		/* Just examine */
		return 0;

	if ((error = copyin(SCARG(uap, set), &iss, sizeof(iss))) != 0)
		return error;

	ibcs2_to_bsd_sigset(&iss, &bss);

	(void) splhigh();

	switch (SCARG(uap, how)) {
	case IBCS2_SIG_BLOCK:
		p->p_sigmask |= bss & ~sigcantmask;
		break;

	case IBCS2_SIG_UNBLOCK:
		p->p_sigmask &= ~bss;
		break;

	case IBCS2_SIG_SETMASK:
		p->p_sigmask = bss & ~sigcantmask;
		break;

	default:
		error = EINVAL;
		break;
	}

	(void) spl0();

	return error;
}

int
ibcs2_sys_sigpending(p, v, retval)
	register struct proc *p;
	void *v;
	register_t *retval;
{
	struct ibcs2_sys_sigpending_args /* {
		syscallarg(ibcs2_sigset_t *) mask;
	} */ *uap = v;
	sigset_t bss;
	ibcs2_sigset_t iss;

	bss = p->p_siglist & p->p_sigmask;
	bsd_to_ibcs2_sigset(&bss, &iss);

	return copyout(&iss, SCARG(uap, mask), sizeof(iss));
}

int
ibcs2_sys_sigsuspend(p, v, retval)
	register struct proc *p;
	void *v;
	register_t *retval;
{
	struct ibcs2_sys_sigsuspend_args /* {
		syscallarg(ibcs2_sigset_t *) mask;
	} */ *uap = v;
	ibcs2_sigset_t sss;
	sigset_t bss;
	struct sys_sigsuspend_args sa;
	int error;

	if ((error = copyin(SCARG(uap, mask), &sss, sizeof(sss))) != 0)
		return error;

	ibcs2_to_bsd_sigset(&sss, &bss);

	SCARG(&sa, mask) = bss;
	return sys_sigsuspend(p, &sa, retval);
}

int
ibcs2_sys_pause(p, v, retval)
	register struct proc *p;
	void *v;
	register_t *retval;
{
	struct sys_sigsuspend_args bsa;

	SCARG(&bsa, mask) = p->p_sigmask;
	return sys_sigsuspend(p, &bsa, retval);
}

int
ibcs2_sys_kill(p, v, retval)
	register struct proc *p;
	void *v;
	register_t *retval;
{
	struct ibcs2_sys_kill_args /* {
		syscallarg(int) pid;
		syscallarg(int) signo;
	} */ *uap = v;
	struct sys_kill_args ka;

	SCARG(&ka, pid) = SCARG(uap, pid);
	SCARG(&ka, signum) = ibcs2_to_bsd_sig[SCARG(uap, signo)];
	return sys_kill(p, &ka, retval);
}
