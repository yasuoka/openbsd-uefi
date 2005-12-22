/*	$OpenBSD: kern_proc.c,v 1.31 2005/12/22 06:55:03 tedu Exp $	*/
/*	$NetBSD: kern_proc.c,v 1.14 1996/02/09 18:59:41 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1989, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)kern_proc.c	8.4 (Berkeley) 1/4/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/buf.h>
#include <sys/acct.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <ufs/ufs/quota.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/ioctl.h>
#include <sys/tty.h>
#include <sys/signalvar.h>
#include <sys/pool.h>

#define	UIHASH(uid)	(&uihashtbl[(uid) & uihash])
LIST_HEAD(uihashhead, uidinfo) *uihashtbl;
u_long uihash;		/* size of hash table - 1 */

/*
 * Other process lists
 */
struct pidhashhead *pidhashtbl;
u_long pidhash;
struct pgrphashhead *pgrphashtbl;
u_long pgrphash;
struct proclist allproc;
struct proclist zombproc;

struct pool proc_pool;
struct pool rusage_pool;
struct pool ucred_pool;
struct pool pgrp_pool;
struct pool session_pool;
struct pool pcred_pool;

static void orphanpg(struct pgrp *);
#ifdef DEBUG
void pgrpdump(void);
#endif

/*
 * Initialize global process hashing structures.
 */
void
procinit(void)
{
	LIST_INIT(&allproc);
	LIST_INIT(&zombproc);


	pidhashtbl = hashinit(maxproc / 4, M_PROC, M_NOWAIT, &pidhash);
	pgrphashtbl = hashinit(maxproc / 4, M_PROC, M_NOWAIT, &pgrphash);
	uihashtbl = hashinit(maxproc / 16, M_PROC, M_NOWAIT, &uihash);
	if (!pidhashtbl || !pgrphashtbl || !uihashtbl)
		panic("procinit: malloc");

	pool_init(&proc_pool, sizeof(struct proc), 0, 0, 0, "procpl",
	    &pool_allocator_nointr);
	pool_init(&rusage_pool, sizeof(struct rusage), 0, 0, 0, "zombiepl",
	    &pool_allocator_nointr);
	pool_init(&ucred_pool, sizeof(struct ucred), 0, 0, 0, "ucredpl",
	    &pool_allocator_nointr);
	pool_init(&pgrp_pool, sizeof(struct pgrp), 0, 0, 0, "pgrppl",
	    &pool_allocator_nointr);
	pool_init(&session_pool, sizeof(struct session), 0, 0, 0, "sessionpl",
	    &pool_allocator_nointr);
	pool_init(&pcred_pool, sizeof(struct pcred), 0, 0, 0, "pcredpl",
	    &pool_allocator_nointr);
}

/*
 * Change the count associated with number of processes
 * a given user is using.
 */
struct uidinfo *
uid_find(uid_t uid)
{
	struct uidinfo *uip, *nuip;
	struct uihashhead *uipp;

	uipp = UIHASH(uid);
	LIST_FOREACH(uip, uipp, ui_hash)
		if (uip->ui_uid == uid)
			break;
	if (uip)
		return (uip);
	MALLOC(nuip, struct uidinfo *, sizeof(*nuip), M_PROC, M_WAITOK);
	/* may have slept, have to check again */
	LIST_FOREACH(uip, uipp, ui_hash)
		if (uip->ui_uid == uid)
			break;
	if (uip) {
		free(nuip, M_PROC);
		return (uip);
	}
	bzero(nuip, sizeof(*nuip));
	nuip->ui_uid = uid;
	LIST_INSERT_HEAD(uipp, nuip, ui_hash);

	return (nuip);
}

int
chgproccnt(uid_t uid, int diff)
{
	struct uidinfo *uip;

	uip = uid_find(uid);
	uip->ui_proccnt += diff;
	if (uip->ui_proccnt < 0)
		panic("chgproccnt: procs < 0");
	return (uip->ui_proccnt);
}

/*
 * Is p an inferior of the current process?
 */
int
inferior(struct proc *p)
{

	for (; p != curproc; p = p->p_pptr)
		if (p->p_pid == 0)
			return (0);
	return (1);
}

/*
 * Locate a process by number
 */
struct proc *
pfind(pid_t pid)
{
	struct proc *p;

	LIST_FOREACH(p, PIDHASH(pid), p_hash)
		if (p->p_pid == pid)
			return (p);
	return (NULL);
}

/*
 * Locate a process group by number
 */
struct pgrp *
pgfind(pid_t pgid)
{
	struct pgrp *pgrp;

	LIST_FOREACH(pgrp, PGRPHASH(pgid), pg_hash)
		if (pgrp->pg_id == pgid)
			return (pgrp);
	return (NULL);
}

/*
 * Move p to a new or existing process group (and session)
 */
int
enterpgrp(struct proc *p, pid_t pgid, int mksess)
{
	struct pgrp *pgrp = pgfind(pgid);

#ifdef DIAGNOSTIC
	if (pgrp != NULL && mksess)	/* firewalls */
		panic("enterpgrp: setsid into non-empty pgrp");
	if (SESS_LEADER(p))
		panic("enterpgrp: session leader attempted setpgrp");
#endif
	if (pgrp == NULL) {
		pid_t savepid = p->p_pid;
		struct proc *np;
		/*
		 * new process group
		 */
#ifdef DIAGNOSTIC
		if (p->p_pid != pgid)
			panic("enterpgrp: new pgrp and pid != pgid");
#endif
		if ((np = pfind(savepid)) == NULL || np != p)
			return (ESRCH);
		pgrp = pool_get(&pgrp_pool, PR_WAITOK);
		if (mksess) {
			struct session *sess;

			/*
			 * new session
			 */
			sess = pool_get(&session_pool, PR_WAITOK);
			sess->s_leader = p;
			sess->s_count = 1;
			sess->s_ttyvp = NULL;
			sess->s_ttyp = NULL;
			bcopy(p->p_session->s_login, sess->s_login,
			    sizeof(sess->s_login));
			p->p_flag &= ~P_CONTROLT;
			pgrp->pg_session = sess;
#ifdef DIAGNOSTIC
			if (p != curproc)
				panic("enterpgrp: mksession and p != curproc");
#endif
		} else {
			pgrp->pg_session = p->p_session;
			pgrp->pg_session->s_count++;
		}
		pgrp->pg_id = pgid;
		LIST_INIT(&pgrp->pg_members);
		LIST_INSERT_HEAD(PGRPHASH(pgid), pgrp, pg_hash);
		pgrp->pg_jobc = 0;
	} else if (pgrp == p->p_pgrp)
		return (0);

	/*
	 * Adjust eligibility of affected pgrps to participate in job control.
	 * Increment eligibility counts before decrementing, otherwise we
	 * could reach 0 spuriously during the first call.
	 */
	fixjobc(p, pgrp, 1);
	fixjobc(p, p->p_pgrp, 0);

	LIST_REMOVE(p, p_pglist);
	if (LIST_EMPTY(&p->p_pgrp->pg_members))
		pgdelete(p->p_pgrp);
	p->p_pgrp = pgrp;
	LIST_INSERT_HEAD(&pgrp->pg_members, p, p_pglist);
	return (0);
}

/*
 * remove process from process group
 */
int
leavepgrp(struct proc *p)
{

	LIST_REMOVE(p, p_pglist);
	if (LIST_EMPTY(&p->p_pgrp->pg_members))
		pgdelete(p->p_pgrp);
	p->p_pgrp = 0;
	return (0);
}

/*
 * delete a process group
 */
void
pgdelete(struct pgrp *pgrp)
{

	if (pgrp->pg_session->s_ttyp != NULL && 
	    pgrp->pg_session->s_ttyp->t_pgrp == pgrp)
		pgrp->pg_session->s_ttyp->t_pgrp = NULL;
	LIST_REMOVE(pgrp, pg_hash);
	SESSRELE(pgrp->pg_session);
	pool_put(&pgrp_pool, pgrp);
}

/*
 * Adjust pgrp jobc counters when specified process changes process group.
 * We count the number of processes in each process group that "qualify"
 * the group for terminal job control (those with a parent in a different
 * process group of the same session).  If that count reaches zero, the
 * process group becomes orphaned.  Check both the specified process'
 * process group and that of its children.
 * entering == 0 => p is leaving specified group.
 * entering == 1 => p is entering specified group.
 */
void
fixjobc(struct proc *p, struct pgrp *pgrp, int entering)
{
	struct pgrp *hispgrp;
	struct session *mysession = pgrp->pg_session;

	/*
	 * Check p's parent to see whether p qualifies its own process
	 * group; if so, adjust count for p's process group.
	 */
	if ((hispgrp = p->p_pptr->p_pgrp) != pgrp &&
	    hispgrp->pg_session == mysession) {
		if (entering)
			pgrp->pg_jobc++;
		else if (--pgrp->pg_jobc == 0)
			orphanpg(pgrp);
	}

	/*
	 * Check this process' children to see whether they qualify
	 * their process groups; if so, adjust counts for children's
	 * process groups.
	 */
	LIST_FOREACH(p, &p->p_children, p_sibling)
		if ((hispgrp = p->p_pgrp) != pgrp &&
		    hispgrp->pg_session == mysession &&
		    P_ZOMBIE(p) == 0) {
			if (entering)
				hispgrp->pg_jobc++;
			else if (--hispgrp->pg_jobc == 0)
				orphanpg(hispgrp);
		}
}

/* 
 * A process group has become orphaned;
 * if there are any stopped processes in the group,
 * hang-up all process in that group.
 */
static void
orphanpg(struct pgrp *pg)
{
	struct proc *p;

	LIST_FOREACH(p, &pg->pg_members, p_pglist) {
		if (p->p_stat == SSTOP) {
			LIST_FOREACH(p, &pg->pg_members, p_pglist) {
				psignal(p, SIGHUP);
				psignal(p, SIGCONT);
			}
			return;
		}
	}
}

#ifdef DDB
void 
proc_printit(struct proc *p, const char *modif, int (*pr)(const char *, ...))
{
	static const char *const pstat[] = {
		"idle", "run", "sleep", "stop", "zombie", "dead", "onproc"
	};
	char pstbuf[5];
	const char *pst = pstbuf;

	if (p->p_stat < 1 || p->p_stat > sizeof(pstat) / sizeof(pstat[0]))
		snprintf(pstbuf, sizeof(pstbuf), "%d", p->p_stat);
	else
		pst = pstat[(int)p->p_stat - 1];

	(*pr)("PROC (%s) pid=%d stat=%s flags=%b\n",
	    p->p_comm, p->p_pid, pst, p->p_flag, P_BITS);
	(*pr)("    pri=%u, usrpri=%u, nice=%d\n",
	    p->p_priority, p->p_usrpri, p->p_nice);
	(*pr)("    forw=%p, back=%p, list=%p,%p\n",
	    p->p_forw, p->p_back, p->p_list.le_next, p->p_list.le_prev);
	(*pr)("    user=%p, vmspace=%p\n",
	    p->p_addr, p->p_vmspace);
	(*pr)("    estcpu=%u, cpticks=%d, pctcpu=%u.%u%, swtime=%u\n",
	    p->p_estcpu, p->p_cpticks, p->p_pctcpu / 100, p->p_pctcpu % 100,
	    p->p_swtime);
	(*pr)("    user=%llu, sys=%llu, intr=%llu\n",
	    p->p_uticks, p->p_sticks, p->p_iticks);
}
#include <machine/db_machdep.h>

#include <ddb/db_interface.h>
#include <ddb/db_output.h>

void
db_show_all_procs(db_expr_t addr, int haddr, db_expr_t count, char *modif)
{
	char *mode;
	int doingzomb = 0;
	struct proc *p, *pp;
    
	if (modif[0] == 0)
		modif[0] = 'n';			/* default == normal mode */

	mode = "mawn";
	while (*mode && *mode != modif[0])
		mode++;
	if (*mode == 0 || *mode == 'm') {
		db_printf("usage: show all procs [/a] [/n] [/w]\n");
		db_printf("\t/a == show process address info\n");
		db_printf("\t/n == show normal process info [default]\n");
		db_printf("\t/w == show process wait/emul info\n");
		return;
	}
	
	p = LIST_FIRST(&allproc);

	switch (*mode) {

	case 'a':
		db_printf("   PID  %-10s  %18s  %18s  %18s\n",
		    "COMMAND", "STRUCT PROC *", "UAREA *", "VMSPACE/VM_MAP");
		break;
	case 'n':
		db_printf("   PID  %5s  %5s  %5s  S  %10s  %-9s  %-16s\n",
		    "PPID", "PGRP", "UID", "FLAGS", "WAIT", "COMMAND");
		break;
	case 'w':
		db_printf("   PID  %-16s  %-8s  %18s  %s\n",
		    "COMMAND", "EMUL", "WAIT-CHANNEL", "WAIT-MSG");
		break;
	}

	while (p != 0) {
		pp = p->p_pptr;
		if (p->p_stat) {

			db_printf("%c%5d  ", p == curproc ? '*' : ' ',
				p->p_pid);

			switch (*mode) {

			case 'a':
				db_printf("%-10.10s  %18p  %18p  %18p\n",
				    p->p_comm, p, p->p_addr, p->p_vmspace);
				break;

			case 'n':
				db_printf("%5d  %5d  %5d  %d  %#10x  "
				    "%-9.9s  %-16s\n",
				    pp ? pp->p_pid : -1, p->p_pgrp->pg_id,
				    p->p_cred->p_ruid, p->p_stat, p->p_flag,
				    (p->p_wchan && p->p_wmesg) ?
					p->p_wmesg : "", p->p_comm);
				break;

			case 'w':
				db_printf("%-16s  %-8s  %18p  %s\n", p->p_comm,
				    p->p_emul->e_name, p->p_wchan,
				    (p->p_wchan && p->p_wmesg) ? 
					p->p_wmesg : "");
				break;

			}
		}
		p = LIST_NEXT(p, p_list);
		if (p == 0 && doingzomb == 0) {
			doingzomb = 1;
			p = LIST_FIRST(&zombproc);
		}
	}
}
#endif

#ifdef DEBUG
void
pgrpdump(void)
{
	struct pgrp *pgrp;
	struct proc *p;
	int i;

	for (i = 0; i <= pgrphash; i++) {
		if (!LIST_EMPTY(&pgrphashtbl[i])) {
			printf("\tindx %d\n", i);
			LIST_FOREACH(pgrp, &pgrphashtbl[i], pg_hash) {
				printf("\tpgrp %p, pgid %d, sess %p, sesscnt %d, mem %p\n",
				    pgrp, pgrp->pg_id, pgrp->pg_session,
				    pgrp->pg_session->s_count,
				    LIST_FIRST(&pgrp->pg_members));
				LIST_FOREACH(p, &pgrp->pg_members, p_pglist) {
					printf("\t\tpid %d addr %p pgrp %p\n", 
					    p->p_pid, p, p->p_pgrp);
				}
			}
		}
	}
}
#endif /* DEBUG */
