/*	$OpenBSD: nfs_syscalls.c,v 1.37 2003/06/02 23:28:20 millert Exp $	*/
/*	$NetBSD: nfs_syscalls.c,v 1.19 1996/02/18 11:53:52 fvdl Exp $	*/

/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
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
 *	@(#)nfs_syscalls.c	8.5 (Berkeley) 3/30/95
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/proc.h>
#include <sys/uio.h>
#include <sys/malloc.h>
#include <sys/buf.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/namei.h>
#include <sys/syslog.h>
#include <sys/filedesc.h>
#include <sys/signalvar.h>
#include <sys/kthread.h>

#include <sys/syscallargs.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#ifdef ISO
#include <netiso/iso.h>
#endif
#include <nfs/xdr_subs.h>
#include <nfs/rpcv2.h>
#include <nfs/nfsproto.h>
#include <nfs/nfs.h>
#include <nfs/nfsm_subs.h>
#include <nfs/nfsrvcache.h>
#include <nfs/nfsmount.h>
#include <nfs/nfsnode.h>
#include <nfs/nfsrtt.h>
#include <nfs/nfs_var.h>

void	nfsrv_zapsock(struct nfssvc_sock *);

/* Global defs. */
extern int32_t (*nfsrv3_procs[NFS_NPROCS])(struct nfsrv_descript *,
						struct nfssvc_sock *,
						struct proc *, struct mbuf **);
extern struct proc *nfs_iodwant[NFS_MAXASYNCDAEMON];
extern int nfs_numasync;
extern int nfsrtton;
extern struct nfsstats nfsstats;
extern int nfsrvw_procrastinate;
struct nfssvc_sock *nfs_udpsock, *nfs_cltpsock;
int nuidhash_max = NFS_MAXUIDHASH;
int nfsd_waiting = 0;
#ifdef NFSSERVER
static int nfs_numnfsd = 0;
static struct nfsdrt nfsdrt;
#endif

struct nfssvc_sockhead nfssvc_sockhead;
struct nfsdhead nfsd_head;

int nfssvc_sockhead_flag;
int nfsd_head_flag;

#define	TRUE	1
#define	FALSE	0

#ifdef NFSCLIENT
struct proc *nfs_asyncdaemon[NFS_MAXASYNCDAEMON];
int nfs_niothreads = -1;
#endif

#ifdef NFSSERVER
static void nfsd_rt(int, struct nfsrv_descript *, int);
#endif

int nfs_clientd(struct nfsmount *nmp, struct ucred *cred, 
    struct nfsd_cargs *ncd, int flag, caddr_t argp, struct proc *p);

/*
 * Nfs client helper daemon.
 * It also get authorization strings for "kerb" mounts.
 * It must start at the beginning of the list again after any potential
 * "sleep" since nfs_reclaim() called from vclean() can pull a node off
 * the list asynchronously.
 */
int
nfs_clientd(struct nfsmount *nmp, struct ucred *cred, struct nfsd_cargs *ncd, 
    int flag, caddr_t argp, struct proc *p)
{
	struct nfsuid *nuidp, *nnuidp;
	int error = 0;

	/*
	 * First initialize some variables
	 */

	/*
	 * If an authorization string is being passed in, get it.
	 */
	if ((flag & NFSSVC_GOTAUTH) &&
	    (nmp->nm_flag & (NFSMNT_WAITAUTH | NFSMNT_DISMNT)) == 0) {
	    if (nmp->nm_flag & NFSMNT_HASAUTH)
		panic("cld kerb");
	    if ((flag & NFSSVC_AUTHINFAIL) == 0) {
		if (ncd->ncd_authlen <= nmp->nm_authlen &&
		    ncd->ncd_verflen <= nmp->nm_verflen &&
		    !copyin(ncd->ncd_authstr,nmp->nm_authstr,ncd->ncd_authlen)&&
		    !copyin(ncd->ncd_verfstr,nmp->nm_verfstr,ncd->ncd_verflen)){
		    nmp->nm_authtype = ncd->ncd_authtype;
		    nmp->nm_authlen = ncd->ncd_authlen;
		    nmp->nm_verflen = ncd->ncd_verflen;
		} else
		    nmp->nm_flag |= NFSMNT_AUTHERR;
	    } else
		nmp->nm_flag |= NFSMNT_AUTHERR;
	    nmp->nm_flag |= NFSMNT_HASAUTH;
	    wakeup((caddr_t)&nmp->nm_authlen);
	} else
	    nmp->nm_flag |= NFSMNT_WAITAUTH;

	/*
	 * Loop every second updating queue until there is a termination sig.
	 */
	while ((nmp->nm_flag & NFSMNT_DISMNT) == 0) {
	    /*
	     * Get an authorization string, if required.
	     */
	    if ((nmp->nm_flag & (NFSMNT_WAITAUTH | NFSMNT_DISMNT | NFSMNT_HASAUTH)) == 0) {
		ncd->ncd_authuid = nmp->nm_authuid;
		if (copyout((caddr_t)ncd, argp, sizeof (struct nfsd_cargs)))
			nmp->nm_flag |= NFSMNT_WAITAUTH;
		else
			return (ENEEDAUTH);
	    }

	    /*
	     * Wait a bit (no pun) and do it again.
	     */
	    if ((nmp->nm_flag & NFSMNT_DISMNT) == 0 &&
		(nmp->nm_flag & (NFSMNT_WAITAUTH | NFSMNT_HASAUTH))) {
		    error = tsleep((caddr_t)&nmp->nm_authstr, PSOCK | PCATCH,
			"nqnfstimr", hz / 3);
		    if (error == EINTR || error == ERESTART) {
			if (vfs_busy(nmp->nm_mountp, LK_EXCLUSIVE, NULL, p) == 0)
			    dounmount(nmp->nm_mountp, MNT_FORCE, p, NULL);
		    }
	    }
	}

	/*
	 * Finally, we can free up the mount structure.
	 */
	for (nuidp = TAILQ_FIRST(&nmp->nm_uidlruhead); nuidp != NULL;
	    nuidp = nnuidp) {
		nnuidp = TAILQ_NEXT(nuidp, nu_lru);
		LIST_REMOVE(nuidp, nu_hash);
		TAILQ_REMOVE(&nmp->nm_uidlruhead, nuidp, nu_lru);
		free((caddr_t)nuidp, M_NFSUID);
	}
	free((caddr_t)nmp, M_NFSMNT);
	if (error == EWOULDBLOCK)
		error = 0;
	return (error);
}


/*
 * NFS server system calls
 */

/*
 * Nfs server pseudo system call for the nfsd's
 * Based on the flag value it either:
 * - adds a socket to the selection list
 * - remains in the kernel as an nfsd
 * - remains in the kernel as an nfsiod
 */
int
sys_nfssvc(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	struct sys_nfssvc_args /* {
		syscallarg(int) flag;
		syscallarg(caddr_t) argp;
	} */ *uap = v;
#ifdef NFSCLIENT
	struct nameidata nd;
	struct nfsmount *nmp;
	struct nfsd_cargs ncd;
#endif
	int error;
#ifdef NFSSERVER
	struct file *fp;
	struct mbuf *nam;
	struct nfsd_args nfsdarg;
	struct nfsd_srvargs nfsd_srvargs, *nsd = &nfsd_srvargs;
	struct nfsd *nfsd;
	struct nfssvc_sock *slp;
	struct nfsuid *nuidp;
#endif

	/*
	 * Must be super user
	 */
	error = suser(p->p_ucred, &p->p_acflag);
	if(error)
		return (error);
	while (nfssvc_sockhead_flag & SLP_INIT) {
		 nfssvc_sockhead_flag |= SLP_WANTINIT;
		(void) tsleep((caddr_t)&nfssvc_sockhead, PSOCK, "nfsd init", 0);
	}
	if (SCARG(uap, flag) & NFSSVC_BIOD) {
		error = ENOSYS;
	} else if (SCARG(uap, flag) & NFSSVC_MNTD) {
#ifndef NFSCLIENT
		error = ENOSYS;
#else
		error = copyin(SCARG(uap, argp), (caddr_t)&ncd, sizeof (ncd));
		if (error)
			return (error);
		NDINIT(&nd, LOOKUP, FOLLOW | LOCKLEAF, UIO_USERSPACE,
			ncd.ncd_dirp, p);
		error = namei(&nd);
		if (error)
			return (error);
		if ((nd.ni_vp->v_flag & VROOT) == 0)
			error = EINVAL;
		nmp = VFSTONFS(nd.ni_vp->v_mount);
		vput(nd.ni_vp);
		if (error)
			return (error);
		if ((nmp->nm_flag & NFSMNT_MNTD) &&
			(SCARG(uap, flag) & NFSSVC_GOTAUTH) == 0)
			return (0);
		nmp->nm_flag |= NFSMNT_MNTD;
		error = nfs_clientd(nmp, p->p_ucred, &ncd, SCARG(uap, flag),
			SCARG(uap, argp), p);
#endif /* NFSCLIENT */
	} else if (SCARG(uap, flag) & NFSSVC_ADDSOCK) {
#ifndef NFSSERVER
		error = ENOSYS;
#else
		error = copyin(SCARG(uap, argp), (caddr_t)&nfsdarg,
		    sizeof(nfsdarg));
		if (error)
			return (error);
		error = getsock(p->p_fd, nfsdarg.sock, &fp);
		if (error)
			return (error);
		/*
		 * Get the client address for connected sockets.
		 */
		if (nfsdarg.name == NULL || nfsdarg.namelen == 0)
			nam = (struct mbuf *)0;
		else {
			error = sockargs(&nam, nfsdarg.name, nfsdarg.namelen,
				MT_SONAME);
			if (error) {
				FRELE(fp);
				return (error);
			}
		}
		error = nfssvc_addsock(fp, nam);
		FRELE(fp);
#endif /* !NFSSERVER */
	} else {
#ifndef NFSSERVER
		error = ENOSYS;
#else
		error = copyin(SCARG(uap, argp), (caddr_t)nsd, sizeof (*nsd));
		if (error)
			return (error);
		if ((SCARG(uap, flag) & NFSSVC_AUTHIN) &&
		    ((nfsd = nsd->nsd_nfsd)) != NULL &&
		    (nfsd->nfsd_slp->ns_flag & SLP_VALID)) {
			slp = nfsd->nfsd_slp;

			/*
			 * First check to see if another nfsd has already
			 * added this credential.
			 */
			for (nuidp = NUIDHASH(slp,nsd->nsd_cr.cr_uid)->lh_first;
			    nuidp != NULL; nuidp = LIST_NEXT(nuidp, nu_hash)) {
				if (nuidp->nu_cr.cr_uid == nsd->nsd_cr.cr_uid &&
				    (!nfsd->nfsd_nd->nd_nam2 ||
				     netaddr_match(NU_NETFAM(nuidp),
				     &nuidp->nu_haddr, nfsd->nfsd_nd->nd_nam2)))
					break;
			}
			if (nuidp) {
			    nfsrv_setcred(&nuidp->nu_cr,&nfsd->nfsd_nd->nd_cr);
			    nfsd->nfsd_nd->nd_flag |= ND_KERBFULL;
			} else {
			    /*
			     * Nope, so we will.
			     */
			    if (slp->ns_numuids < nuidhash_max) {
				slp->ns_numuids++;
				nuidp = (struct nfsuid *)
				   malloc(sizeof (struct nfsuid), M_NFSUID,
					M_WAITOK);
			    } else
				nuidp = (struct nfsuid *)0;
			    if ((slp->ns_flag & SLP_VALID) == 0) {
				if (nuidp)
				    free((caddr_t)nuidp, M_NFSUID);
			    } else {
				if (nuidp == (struct nfsuid *)0) {
				    nuidp = TAILQ_FIRST(&slp->ns_uidlruhead);
				    LIST_REMOVE(nuidp, nu_hash);
				    TAILQ_REMOVE(&slp->ns_uidlruhead, nuidp,
					nu_lru);
				    if (nuidp->nu_flag & NU_NAM)
					m_freem(nuidp->nu_nam);
			        }
				nuidp->nu_flag = 0;
				nuidp->nu_cr = nsd->nsd_cr;
				if (nuidp->nu_cr.cr_ngroups > NGROUPS)
				    nuidp->nu_cr.cr_ngroups = NGROUPS;
				nuidp->nu_cr.cr_ref = 1;
				nuidp->nu_timestamp = nsd->nsd_timestamp;
				nuidp->nu_expire = time.tv_sec + nsd->nsd_ttl;
				/*
				 * and save the session key in nu_key.
				 */
				bcopy(nsd->nsd_key, nuidp->nu_key,
				    sizeof (nsd->nsd_key));
				if (nfsd->nfsd_nd->nd_nam2) {
				    struct sockaddr_in *saddr;

				    saddr = mtod(nfsd->nfsd_nd->nd_nam2,
					 struct sockaddr_in *);
				    switch (saddr->sin_family) {
				    case AF_INET:
					nuidp->nu_flag |= NU_INETADDR;
					nuidp->nu_inetaddr =
					     saddr->sin_addr.s_addr;
					break;
				    case AF_ISO:
				    default:
					nuidp->nu_flag |= NU_NAM;
					nuidp->nu_nam = m_copym(
					    nfsd->nfsd_nd->nd_nam2, 0,
					     M_COPYALL, M_WAIT);
					break;
				    };
				}
				TAILQ_INSERT_TAIL(&slp->ns_uidlruhead, nuidp,
					nu_lru);
				LIST_INSERT_HEAD(NUIDHASH(slp, nsd->nsd_uid),
					nuidp, nu_hash);
				nfsrv_setcred(&nuidp->nu_cr,
				    &nfsd->nfsd_nd->nd_cr);
				nfsd->nfsd_nd->nd_flag |= ND_KERBFULL;
			    }
			}
		}
		if ((SCARG(uap, flag) & NFSSVC_AUTHINFAIL) &&
		    (nfsd = nsd->nsd_nfsd))
			nfsd->nfsd_flag |= NFSD_AUTHFAIL;
		error = nfssvc_nfsd(nsd, SCARG(uap, argp), p);
#endif /* !NFSSERVER */
	}
	if (error == EINTR || error == ERESTART)
		error = 0;
	return (error);
}

#ifdef NFSSERVER
/*
 * Adds a socket to the list for servicing by nfsds.
 */
int
nfssvc_addsock(fp, mynam)
	struct file *fp;
	struct mbuf *mynam;
{
	struct mbuf *m;
	int siz;
	struct nfssvc_sock *slp;
	struct socket *so;
	struct nfssvc_sock *tslp;
	int error, s;

	so = (struct socket *)fp->f_data;
	tslp = (struct nfssvc_sock *)0;
	/*
	 * Add it to the list, as required.
	 */
	if (so->so_proto->pr_protocol == IPPROTO_UDP) {
		tslp = nfs_udpsock;
		if (tslp->ns_flag & SLP_VALID) {
			m_freem(mynam);
			return (EPERM);
		}
#ifdef ISO
	} else if (so->so_proto->pr_protocol == ISOPROTO_CLTP) {
		tslp = nfs_cltpsock;
		if (tslp->ns_flag & SLP_VALID) {
			m_freem(mynam);
			return (EPERM);
		}
#endif /* ISO */
	}
	if (so->so_type == SOCK_STREAM)
		siz = NFS_MAXPACKET + sizeof (u_long);
	else
		siz = NFS_MAXPACKET;
	error = soreserve(so, siz, siz); 
	if (error) {
		m_freem(mynam);
		return (error);
	}

	/*
	 * Set protocol specific options { for now TCP only } and
	 * reserve some space. For datagram sockets, this can get called
	 * repeatedly for the same socket, but that isn't harmful.
	 */
	if (so->so_type == SOCK_STREAM) {
		MGET(m, M_WAIT, MT_SOOPTS);
		*mtod(m, int32_t *) = 1;
		m->m_len = sizeof(int32_t);
		sosetopt(so, SOL_SOCKET, SO_KEEPALIVE, m);
	}
	if (so->so_proto->pr_domain->dom_family == AF_INET &&
	    so->so_proto->pr_protocol == IPPROTO_TCP) {
		MGET(m, M_WAIT, MT_SOOPTS);
		*mtod(m, int32_t *) = 1;
		m->m_len = sizeof(int32_t);
		sosetopt(so, IPPROTO_TCP, TCP_NODELAY, m);
	}
	so->so_rcv.sb_flags &= ~SB_NOINTR;
	so->so_rcv.sb_timeo = 0;
	so->so_snd.sb_flags &= ~SB_NOINTR;
	so->so_snd.sb_timeo = 0;
	if (tslp)
		slp = tslp;
	else {
		slp = (struct nfssvc_sock *)
			malloc(sizeof (struct nfssvc_sock), M_NFSSVC, M_WAITOK);
		bzero((caddr_t)slp, sizeof (struct nfssvc_sock));
		TAILQ_INIT(&slp->ns_uidlruhead);
		TAILQ_INSERT_TAIL(&nfssvc_sockhead, slp, ns_chain);
	}
	slp->ns_so = so;
	slp->ns_nam = mynam;
	fp->f_count++;
	slp->ns_fp = fp;
	s = splsoftnet();
	so->so_upcallarg = (caddr_t)slp;
	so->so_upcall = nfsrv_rcv;
	slp->ns_flag = (SLP_VALID | SLP_NEEDQ);
	nfsrv_wakenfsd(slp);
	splx(s);
	return (0);
}

/*
 * Called by nfssvc() for nfsds. Just loops around servicing rpc requests
 * until it is killed by a signal.
 */
int
nfssvc_nfsd(nsd, argp, p)
	struct nfsd_srvargs *nsd;
	caddr_t argp;
	struct proc *p;
{
	struct mbuf *m;
	int siz;
	struct nfssvc_sock *slp;
	struct socket *so;
	int *solockp;
	struct nfsd *nfsd = nsd->nsd_nfsd;
	struct nfsrv_descript *nd = NULL;
	struct mbuf *mreq;
	int error = 0, cacherep, s, sotype, writes_todo;
	u_quad_t cur_usec;

#ifndef nolint
	cacherep = RC_DOIT;
	writes_todo = 0;
#endif
	s = splsoftnet();
	if (nfsd == (struct nfsd *)0) {
		nsd->nsd_nfsd = nfsd = (struct nfsd *)
			malloc(sizeof (struct nfsd), M_NFSD, M_WAITOK);
		bzero((caddr_t)nfsd, sizeof (struct nfsd));
		nfsd->nfsd_procp = p;
		TAILQ_INSERT_TAIL(&nfsd_head, nfsd, nfsd_chain);
		nfs_numnfsd++;
	}
	PHOLD(p);
	/*
	 * Loop getting rpc requests until SIGKILL.
	 */
	for (;;) {
		if ((nfsd->nfsd_flag & NFSD_REQINPROG) == 0) {
			while (nfsd->nfsd_slp == (struct nfssvc_sock *)0 &&
			    (nfsd_head_flag & NFSD_CHECKSLP) == 0) {
				nfsd->nfsd_flag |= NFSD_WAITING;
				nfsd_waiting++;
				error = tsleep((caddr_t)nfsd, PSOCK | PCATCH,
				    "nfsd", 0);
				nfsd_waiting--;
				if (error)
					goto done;
			}
			if (nfsd->nfsd_slp == NULL &&
			    (nfsd_head_flag & NFSD_CHECKSLP) != 0) {
				for (slp = TAILQ_FIRST(&nfssvc_sockhead);
				    slp != 0; slp = TAILQ_NEXT(slp, ns_chain)) {
				    if ((slp->ns_flag & (SLP_VALID | SLP_DOREC))
					== (SLP_VALID | SLP_DOREC)) {
					    slp->ns_flag &= ~SLP_DOREC;
					    slp->ns_sref++;
					    nfsd->nfsd_slp = slp;
					    break;
				    }
				}
				if (slp == 0)
					nfsd_head_flag &= ~NFSD_CHECKSLP;
			}
			if ((slp = nfsd->nfsd_slp) == (struct nfssvc_sock *)0)
				continue;
			if (slp->ns_flag & SLP_VALID) {
				if (slp->ns_flag & SLP_DISCONN)
					nfsrv_zapsock(slp);
				else if (slp->ns_flag & SLP_NEEDQ) {
					slp->ns_flag &= ~SLP_NEEDQ;
					(void) nfs_sndlock(&slp->ns_solock,
						(struct nfsreq *)0);
					nfsrv_rcv(slp->ns_so, (caddr_t)slp,
						M_WAIT);
					nfs_sndunlock(&slp->ns_solock);
				}
				error = nfsrv_dorec(slp, nfsd, &nd);
				cur_usec = (u_quad_t)time.tv_sec * 1000000 +
					(u_quad_t)time.tv_usec;
				if (error && LIST_FIRST(&slp->ns_tq) &&
				    LIST_FIRST(&slp->ns_tq)->nd_time
				    <= cur_usec) {
					error = 0;
					cacherep = RC_DOIT;
					writes_todo = 1;
				} else
					writes_todo = 0;
				nfsd->nfsd_flag |= NFSD_REQINPROG;
			}
		} else {
			error = 0;
			slp = nfsd->nfsd_slp;
		}
		if (error || (slp->ns_flag & SLP_VALID) == 0) {
			if (nd) {
				free((caddr_t)nd, M_NFSRVDESC);
				nd = NULL;
			}
			nfsd->nfsd_slp = (struct nfssvc_sock *)0;
			nfsd->nfsd_flag &= ~NFSD_REQINPROG;
			nfsrv_slpderef(slp);
			continue;
		}
		splx(s);
		so = slp->ns_so;
		sotype = so->so_type;
		if (so->so_proto->pr_flags & PR_CONNREQUIRED)
			solockp = &slp->ns_solock;
		else
			solockp = (int *)0;
		if (nd) {
		    nd->nd_starttime = time;
		    if (nd->nd_nam2)
			nd->nd_nam = nd->nd_nam2;
		    else
			nd->nd_nam = slp->ns_nam;

		    /*
		     * Check to see if authorization is needed.
		     */
		    if (nfsd->nfsd_flag & NFSD_NEEDAUTH) {
			nfsd->nfsd_flag &= ~NFSD_NEEDAUTH;
			nsd->nsd_haddr = mtod(nd->nd_nam,
			    struct sockaddr_in *)->sin_addr.s_addr;
			nsd->nsd_authlen = nfsd->nfsd_authlen;
			nsd->nsd_verflen = nfsd->nfsd_verflen;
			if (!copyout(nfsd->nfsd_authstr,nsd->nsd_authstr,
				nfsd->nfsd_authlen) &&
			    !copyout(nfsd->nfsd_verfstr, nsd->nsd_verfstr,
				nfsd->nfsd_verflen) &&
			    !copyout((caddr_t)nsd, argp, sizeof (*nsd))) {
			    PRELE(p);
			    return (ENEEDAUTH);
			}
			cacherep = RC_DROPIT;
		    } else
			cacherep = nfsrv_getcache(nd, slp, &mreq);

		    if (nfsd->nfsd_flag & NFSD_AUTHFAIL) {
			    nfsd->nfsd_flag &= ~NFSD_AUTHFAIL;
			    nd->nd_procnum = NFSPROC_NOOP;
			    nd->nd_repstat = (NFSERR_AUTHERR | AUTH_TOOWEAK);
			    cacherep = RC_DOIT;
		    }
		}

		/*
		 * Loop to get all the write rpc relies that have been
		 * gathered together.
		 */
		do {
		    switch (cacherep) {
		    case RC_DOIT:
			if (writes_todo || (nd->nd_procnum == NFSPROC_WRITE &&
			    nfsrvw_procrastinate > 0))
			    error = nfsrv_writegather(&nd, slp,
				nfsd->nfsd_procp, &mreq);
			else
			    error = (*(nfsrv3_procs[nd->nd_procnum]))(nd,
				slp, nfsd->nfsd_procp, &mreq);
			if (mreq == NULL)
				break;
			if (error) {
				nfsstats.srv_errs++;
				nfsrv_updatecache(nd, FALSE, mreq);
				if (nd->nd_nam2)
					m_freem(nd->nd_nam2);
				break;
			}
			nfsstats.srvrpccnt[nd->nd_procnum]++;
			nfsrv_updatecache(nd, TRUE, mreq);
			nd->nd_mrep = (struct mbuf *)0;
		    case RC_REPLY:
			m = mreq;
			siz = 0;
			while (m) {
				siz += m->m_len;
				m = m->m_next;
			}
			if (siz <= 0 || siz > NFS_MAXPACKET) {
				printf("mbuf siz=%d\n",siz);
				panic("Bad nfs svc reply");
			}
			m = mreq;
			m->m_pkthdr.len = siz;
			m->m_pkthdr.rcvif = (struct ifnet *)0;
			/*
			 * For stream protocols, prepend a Sun RPC
			 * Record Mark.
			 */
			if (sotype == SOCK_STREAM) {
				M_PREPEND(m, NFSX_UNSIGNED, M_WAIT);
				*mtod(m, u_int32_t *) = htonl(0x80000000 | siz);
			}
			if (solockp)
				(void) nfs_sndlock(solockp, (struct nfsreq *)0);
			if (slp->ns_flag & SLP_VALID)
			    error = nfs_send(so, nd->nd_nam2, m, NULL);
			else {
			    error = EPIPE;
			    m_freem(m);
			}
			if (nfsrtton)
				nfsd_rt(sotype, nd, cacherep);
			if (nd->nd_nam2)
				MFREE(nd->nd_nam2, m);
			if (nd->nd_mrep)
				m_freem(nd->nd_mrep);
			if (error == EPIPE)
				nfsrv_zapsock(slp);
			if (solockp)
				nfs_sndunlock(solockp);
			if (error == EINTR || error == ERESTART) {
				free((caddr_t)nd, M_NFSRVDESC);
				nfsrv_slpderef(slp);
				s = splsoftnet();
				goto done;
			}
			break;
		    case RC_DROPIT:
			if (nfsrtton)
				nfsd_rt(sotype, nd, cacherep);
			m_freem(nd->nd_mrep);
			m_freem(nd->nd_nam2);
			break;
		    };
		    if (nd) {
			FREE((caddr_t)nd, M_NFSRVDESC);
			nd = NULL;
		    }

		    /*
		     * Check to see if there are outstanding writes that
		     * need to be serviced.
		     */
		    cur_usec = (u_quad_t)time.tv_sec * 1000000 +
			(u_quad_t)time.tv_usec;
		    s = splsoftclock();
		    if (LIST_FIRST(&slp->ns_tq) &&
			LIST_FIRST(&slp->ns_tq)->nd_time <= cur_usec) {
			cacherep = RC_DOIT;
			writes_todo = 1;
		    } else
			writes_todo = 0;
		    splx(s);
		} while (writes_todo);
		s = splsoftnet();
		if (nfsrv_dorec(slp, nfsd, &nd)) {
			nfsd->nfsd_flag &= ~NFSD_REQINPROG;
			nfsd->nfsd_slp = NULL;
			nfsrv_slpderef(slp);
		}
	}
done:
	PRELE(p);
	TAILQ_REMOVE(&nfsd_head, nfsd, nfsd_chain);
	splx(s);
	free((caddr_t)nfsd, M_NFSD);
	nsd->nsd_nfsd = (struct nfsd *)0;
	if (--nfs_numnfsd == 0)
		nfsrv_init(TRUE);	/* Reinitialize everything */
	return (error);
}

/*
 * Shut down a socket associated with an nfssvc_sock structure.
 * Should be called with the send lock set, if required.
 * The trick here is to increment the sref at the start, so that the nfsds
 * will stop using it and clear ns_flag at the end so that it will not be
 * reassigned during cleanup.
 */
void
nfsrv_zapsock(slp)
	struct nfssvc_sock *slp;
{
	struct nfsuid *nuidp, *nnuidp;
	struct nfsrv_descript *nwp, *nnwp;
	struct socket *so;
	struct file *fp;
	struct mbuf *m;
	int s;

	slp->ns_flag &= ~SLP_ALLFLAGS;
	fp = slp->ns_fp;
	if (fp) {
		FREF(fp);
		slp->ns_fp = NULL;
		so = slp->ns_so;
		so->so_upcall = NULL;
		soshutdown(so, 2);
		closef(fp, NULL);
		if (slp->ns_nam)
			MFREE(slp->ns_nam, m);
		m_freem(slp->ns_raw);
		m_freem(slp->ns_rec);
		for (nuidp = TAILQ_FIRST(&slp->ns_uidlruhead); nuidp != NULL;
		    nuidp = nnuidp) {
			nnuidp = TAILQ_NEXT(nuidp, nu_lru);
			LIST_REMOVE(nuidp, nu_hash);
			TAILQ_REMOVE(&slp->ns_uidlruhead, nuidp, nu_lru);
			if (nuidp->nu_flag & NU_NAM)
				m_freem(nuidp->nu_nam);
			free((caddr_t)nuidp, M_NFSUID);
		}
		s = splsoftclock();
		for (nwp = LIST_FIRST(&slp->ns_tq); nwp != NULL; nwp = nnwp) {
			nnwp = LIST_NEXT(nwp, nd_tq);
			LIST_REMOVE(nwp, nd_tq);
			free((caddr_t)nwp, M_NFSRVDESC);
		}
		LIST_INIT(&slp->ns_tq);
		splx(s);
	}
}

/*
 * Derefence a server socket structure. If it has no more references and
 * is no longer valid, you can throw it away.
 */
void
nfsrv_slpderef(slp)
	struct nfssvc_sock *slp;
{
	if (--(slp->ns_sref) == 0 && (slp->ns_flag & SLP_VALID) == 0) {
		TAILQ_REMOVE(&nfssvc_sockhead, slp, ns_chain);
		free((caddr_t)slp, M_NFSSVC);
	}
}

/*
 * Initialize the data structures for the server.
 * Handshake with any new nfsds starting up to avoid any chance of
 * corruption.
 */
void
nfsrv_init(terminating)
	int terminating;
{
	struct nfssvc_sock *slp, *nslp;

	if (nfssvc_sockhead_flag & SLP_INIT)
		panic("nfsd init");
	nfssvc_sockhead_flag |= SLP_INIT;
	if (terminating) {
		for (slp = TAILQ_FIRST(&nfssvc_sockhead); slp != NULL;
		    slp = nslp) {
			nslp = TAILQ_NEXT(slp, ns_chain);
			if (slp->ns_flag & SLP_VALID)
				nfsrv_zapsock(slp);
			TAILQ_REMOVE(&nfssvc_sockhead, slp, ns_chain);
			free((caddr_t)slp, M_NFSSVC);
		}
		nfsrv_cleancache();	/* And clear out server cache */
	}

	TAILQ_INIT(&nfssvc_sockhead);
	nfssvc_sockhead_flag &= ~SLP_INIT;
	if (nfssvc_sockhead_flag & SLP_WANTINIT) {
		nfssvc_sockhead_flag &= ~SLP_WANTINIT;
		wakeup((caddr_t)&nfssvc_sockhead);
	}

	TAILQ_INIT(&nfsd_head);
	nfsd_head_flag &= ~NFSD_CHECKSLP;

	nfs_udpsock = (struct nfssvc_sock *)
	    malloc(sizeof (struct nfssvc_sock), M_NFSSVC, M_WAITOK);
	bzero((caddr_t)nfs_udpsock, sizeof (struct nfssvc_sock));
	TAILQ_INIT(&nfs_udpsock->ns_uidlruhead);
	TAILQ_INSERT_HEAD(&nfssvc_sockhead, nfs_udpsock, ns_chain);

	nfs_cltpsock = (struct nfssvc_sock *)
	    malloc(sizeof (struct nfssvc_sock), M_NFSSVC, M_WAITOK);
	bzero((caddr_t)nfs_cltpsock, sizeof (struct nfssvc_sock));
	TAILQ_INIT(&nfs_cltpsock->ns_uidlruhead);
	TAILQ_INSERT_TAIL(&nfssvc_sockhead, nfs_cltpsock, ns_chain);
}

/*
 * Add entries to the server monitor log.
 */
static void
nfsd_rt(sotype, nd, cacherep)
	int sotype;
	struct nfsrv_descript *nd;
	int cacherep;
{
	struct drt *rt;

	rt = &nfsdrt.drt[nfsdrt.pos];
	if (cacherep == RC_DOIT)
		rt->flag = 0;
	else if (cacherep == RC_REPLY)
		rt->flag = DRT_CACHEREPLY;
	else
		rt->flag = DRT_CACHEDROP;
	if (sotype == SOCK_STREAM)
		rt->flag |= DRT_TCP;
	else if (nd->nd_flag & ND_NFSV3)
		rt->flag |= DRT_NFSV3;
	rt->proc = nd->nd_procnum;
	if (mtod(nd->nd_nam, struct sockaddr *)->sa_family == AF_INET)
	    rt->ipadr = mtod(nd->nd_nam, struct sockaddr_in *)->sin_addr.s_addr;
	else
	    rt->ipadr = INADDR_ANY;
	rt->resptime = ((time.tv_sec - nd->nd_starttime.tv_sec) * 1000000) +
		(time.tv_usec - nd->nd_starttime.tv_usec);
	rt->tstamp = time;
	nfsdrt.pos = (nfsdrt.pos + 1) % NFSRTTLOGSIZ;
}
#endif /* NFSSERVER */

#ifdef NFSCLIENT
/*
 * Asynchronous I/O threads for client nfs.
 * They do read-ahead and write-behind operations on the block I/O cache.
 * Never returns unless it fails or gets killed.
 */
int
nfssvc_iod(p)
	struct proc *p;
{
	struct buf *bp, *nbp;
	int i, myiod;
	struct vnode *vp;
	int error = 0, s;

	/*
	 * Assign my position or return error if too many already running
	 */
	myiod = -1;
	for (i = 0; i < NFS_MAXASYNCDAEMON; i++)
		if (nfs_asyncdaemon[i] == NULL) {
			myiod = i;
			break;
		}
	if (myiod == -1)
		return (EBUSY);
	nfs_asyncdaemon[myiod] = p;
	nfs_numasync++;
	PHOLD(p);
	/*
	 * Just loop around doin our stuff until SIGKILL
	 */
	for (;;) {
	    while (TAILQ_FIRST(&nfs_bufq) == NULL && error == 0) {
		nfs_iodwant[myiod] = p;
		error = tsleep((caddr_t)&nfs_iodwant[myiod],
			PWAIT | PCATCH, "nfsidl", 0);
	    }
	    while ((bp = TAILQ_FIRST(&nfs_bufq)) != NULL) {
		/* Take one off the front of the list */
		TAILQ_REMOVE(&nfs_bufq, bp, b_freelist);
		if (bp->b_flags & B_READ)
		    (void) nfs_doio(bp, NULL);
		else do {
		    /*
		     * Look for a delayed write for the same vnode, so I can do 
		     * it now. We must grab it before calling nfs_doio() to
		     * avoid any risk of the vnode getting vclean()'d while
		     * we are doing the write rpc.
		     */
		    vp = bp->b_vp;
		    s = splbio();
		    for (nbp = LIST_FIRST(&vp->v_dirtyblkhd); nbp != NULL;
			nbp = LIST_NEXT(nbp, b_vnbufs)) {
			if ((nbp->b_flags &
			    (B_BUSY|B_DELWRI|B_NEEDCOMMIT|B_NOCACHE))!=B_DELWRI)
			    continue;
			bremfree(nbp);
			nbp->b_flags |= (B_BUSY|B_ASYNC);
			break;
		    }
		    /*
		     * For the delayed write, do the first part of nfs_bwrite()
		     * up to, but not including nfs_strategy().
		     */
		    if (nbp) {
			nbp->b_flags &= ~(B_READ|B_DONE|B_ERROR);
			buf_undirty(bp);
			nbp->b_vp->v_numoutput++;
		    }
		    splx(s);

		    (void) nfs_doio(bp, NULL);
		} while ((bp = nbp) != NULL);
	    }
	    if (error) {
		PRELE(p);
		nfs_asyncdaemon[myiod] = NULL;
		nfs_numasync--;
		return (error);
	    }
	}
}

void
start_nfsio(arg)
	void *arg;
{
	nfssvc_iod(curproc);
	
	kthread_exit(0);
}

void
nfs_getset_niothreads(set)
	int set;
{
	int i, have, start;
	
	for (have = 0, i = 0; i < NFS_MAXASYNCDAEMON; i++)
		if (nfs_asyncdaemon[i] != NULL)
			have++;

	if (set) {
		/* clamp to sane range */
		nfs_niothreads = max(0, min(nfs_niothreads, NFS_MAXASYNCDAEMON));

		start = nfs_niothreads - have;

		while (start > 0) {
			kthread_create(start_nfsio, NULL, NULL, "nfsio");
			start--;
		}

		for (i = 0; (start < 0) && (i < NFS_MAXASYNCDAEMON); i++)
			if (nfs_asyncdaemon[i] != NULL) {
				psignal(nfs_asyncdaemon[i], SIGKILL);
				start++;
			}
	} else {
		if (nfs_niothreads >= 0)
			nfs_niothreads = have;
	}
}

/*
 * Get an authorization string for the uid by having the mount_nfs sitting
 * on this mount point porpous out of the kernel and do it.
 */
int
nfs_getauth(nmp, rep, cred, auth_str, auth_len, verf_str, verf_len, key)
	struct nfsmount *nmp;
	struct nfsreq *rep;
	struct ucred *cred;
	char **auth_str;
	int *auth_len;
	char *verf_str;
	int *verf_len;
	NFSKERBKEY_T key;		/* return session key */
{
	int error = 0;

	while ((nmp->nm_flag & NFSMNT_WAITAUTH) == 0) {
		nmp->nm_flag |= NFSMNT_WANTAUTH;
		(void) tsleep((caddr_t)&nmp->nm_authtype, PSOCK,
			"nfsauth1", 2 * hz);
		error = nfs_sigintr(nmp, rep, rep->r_procp);
		if (error) {
			nmp->nm_flag &= ~NFSMNT_WANTAUTH;
			return (error);
		}
	}
	nmp->nm_flag &= ~(NFSMNT_WAITAUTH | NFSMNT_WANTAUTH);
	nmp->nm_authstr = *auth_str = (char *)malloc(RPCAUTH_MAXSIZ, M_TEMP, M_WAITOK);
	nmp->nm_authlen = RPCAUTH_MAXSIZ;
	nmp->nm_verfstr = verf_str;
	nmp->nm_verflen = *verf_len;
	nmp->nm_authuid = cred->cr_uid;
	wakeup((caddr_t)&nmp->nm_authstr);

	/*
	 * And wait for mount_nfs to do its stuff.
	 */
	while ((nmp->nm_flag & NFSMNT_HASAUTH) == 0 && error == 0) {
		(void) tsleep((caddr_t)&nmp->nm_authlen, PSOCK,
			"nfsauth2", 2 * hz);
		error = nfs_sigintr(nmp, rep, rep->r_procp);
	}
	if (nmp->nm_flag & NFSMNT_AUTHERR) {
		nmp->nm_flag &= ~NFSMNT_AUTHERR;
		error = EAUTH;
	}
	if (error)
		free((caddr_t)*auth_str, M_TEMP);
	else {
		*auth_len = nmp->nm_authlen;
		*verf_len = nmp->nm_verflen;
		bcopy((caddr_t)nmp->nm_key, (caddr_t)key, sizeof (NFSKERBKEY_T));
	}
	nmp->nm_flag &= ~NFSMNT_HASAUTH;
	nmp->nm_flag |= NFSMNT_WAITAUTH;
	if (nmp->nm_flag & NFSMNT_WANTAUTH) {
		nmp->nm_flag &= ~NFSMNT_WANTAUTH;
		wakeup((caddr_t)&nmp->nm_authtype);
	}
	return (error);
}

/*
 * Get a nickname authenticator and verifier.
 */
int
nfs_getnickauth(nmp, cred, auth_str, auth_len, verf_str, verf_len)
	struct nfsmount *nmp;
	struct ucred *cred;
	char **auth_str;
	int *auth_len;
	char *verf_str;
	int verf_len;
{
	struct nfsuid *nuidp;
	u_int32_t *nickp, *verfp;
	struct timeval ktvin, ktvout;

#ifdef DIAGNOSTIC
	if (verf_len < (4 * NFSX_UNSIGNED))
		panic("nfs_getnickauth verf too small");
#endif
	for (nuidp = NMUIDHASH(nmp, cred->cr_uid)->lh_first;
	    nuidp != NULL; nuidp = LIST_NEXT(nuidp, nu_hash)) {
		if (nuidp->nu_cr.cr_uid == cred->cr_uid)
			break;
	}
	if (!nuidp || nuidp->nu_expire < time.tv_sec)
		return (EACCES);

	/*
	 * Move to the end of the lru list (end of lru == most recently used).
	 */
	TAILQ_REMOVE(&nmp->nm_uidlruhead, nuidp, nu_lru);
	TAILQ_INSERT_TAIL(&nmp->nm_uidlruhead, nuidp, nu_lru);

	nickp = (u_int32_t *)malloc(2 * NFSX_UNSIGNED, M_TEMP, M_WAITOK);
	*nickp++ = txdr_unsigned(RPCAKN_NICKNAME);
	*nickp = txdr_unsigned(nuidp->nu_nickname);
	*auth_str = (char *)nickp;
	*auth_len = 2 * NFSX_UNSIGNED;

	/*
	 * Now we must encrypt the verifier and package it up.
	 */
	verfp = (u_int32_t *)verf_str;
	*verfp++ = txdr_unsigned(RPCAKN_NICKNAME);
	if (time.tv_sec > nuidp->nu_timestamp.tv_sec ||
	    (time.tv_sec == nuidp->nu_timestamp.tv_sec &&
	     time.tv_usec > nuidp->nu_timestamp.tv_usec))
		nuidp->nu_timestamp = time;
	else
		nuidp->nu_timestamp.tv_usec++;
	ktvin.tv_sec = txdr_unsigned(nuidp->nu_timestamp.tv_sec);
	ktvin.tv_usec = txdr_unsigned(nuidp->nu_timestamp.tv_usec);

	*verfp++ = ktvout.tv_sec;
	*verfp++ = ktvout.tv_usec;
	*verfp = 0;
	return (0);
}

/*
 * Save the current nickname in a hash list entry on the mount point.
 */
int
nfs_savenickauth(nmp, cred, len, key, mdp, dposp, mrep)
	struct nfsmount *nmp;
	struct ucred *cred;
	int len;
	NFSKERBKEY_T key;
	struct mbuf **mdp;
	char **dposp;
	struct mbuf *mrep;
{
	struct nfsuid *nuidp;
	u_int32_t *tl;
	int32_t t1;
	struct mbuf *md = *mdp;
	struct timeval ktvin, ktvout;
	u_int32_t nick;
	char *dpos = *dposp, *cp2;
	int deltasec, error = 0;

	if (len == (3 * NFSX_UNSIGNED)) {
		nfsm_dissect(tl, u_int32_t *, 3 * NFSX_UNSIGNED);
		ktvin.tv_sec = *tl++;
		ktvin.tv_usec = *tl++;
		nick = fxdr_unsigned(u_int32_t, *tl);

		ktvout.tv_sec = fxdr_unsigned(long, ktvout.tv_sec);
		ktvout.tv_usec = fxdr_unsigned(long, ktvout.tv_usec);
		deltasec = time.tv_sec - ktvout.tv_sec;
		if (deltasec < 0)
			deltasec = -deltasec;
		/*
		 * If ok, add it to the hash list for the mount point.
		 */
		if (deltasec <= NFS_KERBCLOCKSKEW) {
			if (nmp->nm_numuids < nuidhash_max) {
				nmp->nm_numuids++;
				nuidp = (struct nfsuid *)
				   malloc(sizeof (struct nfsuid), M_NFSUID,
					M_WAITOK);
			} else {
				nuidp = TAILQ_FIRST(&nmp->nm_uidlruhead);
				LIST_REMOVE(nuidp, nu_hash);
				TAILQ_REMOVE(&nmp->nm_uidlruhead, nuidp,
					nu_lru);
			}
			nuidp->nu_flag = 0;
			nuidp->nu_cr.cr_uid = cred->cr_uid;
			nuidp->nu_expire = time.tv_sec + NFS_KERBTTL;
			nuidp->nu_timestamp = ktvout;
			nuidp->nu_nickname = nick;
			bcopy(key, nuidp->nu_key, sizeof (NFSKERBKEY_T));
			TAILQ_INSERT_TAIL(&nmp->nm_uidlruhead, nuidp,
				nu_lru);
			LIST_INSERT_HEAD(NMUIDHASH(nmp, cred->cr_uid),
				nuidp, nu_hash);
		}
	} else
		nfsm_adv(nfsm_rndup(len));
nfsmout:
	*mdp = md;
	*dposp = dpos;
	return (error);
}
#endif /* NFSCLIENT */
