/*	$OpenBSD: uipc_syscalls.c,v 1.41 2001/09/17 19:37:59 jason Exp $	*/
/*	$NetBSD: uipc_syscalls.c,v 1.19 1996/02/09 19:00:48 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1989, 1990, 1993
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
 *	@(#)uipc_syscalls.c	8.4 (Berkeley) 2/21/94
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/filedesc.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/buf.h>
#include <sys/malloc.h>
#include <sys/event.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/signalvar.h>
#include <sys/unpcb.h>
#include <sys/un.h>
#ifdef KTRACE
#include <sys/ktrace.h>
#endif

#include <sys/mount.h>
#include <sys/syscallargs.h>

/*
 * System call interface to the socket abstraction.
 */
extern	struct fileops socketops;

int
sys_socket(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	register struct sys_socket_args /* {
		syscallarg(int) domain;
		syscallarg(int) type;
		syscallarg(int) protocol;
	} */ *uap = v;
	struct filedesc *fdp = p->p_fd;
	struct socket *so;
	struct file *fp;
	int fd, error;

	if ((error = falloc(p, &fp, &fd)) != 0)
		return (error);
	fp->f_flag = FREAD|FWRITE;
	fp->f_type = DTYPE_SOCKET;
	fp->f_ops = &socketops;
	error = socreate(SCARG(uap, domain), &so, SCARG(uap, type),
			 SCARG(uap, protocol));
	if (error) {
		fdremove(fdp, fd);
		ffree(fp);
	} else {
		fp->f_data = (caddr_t)so;
		*retval = fd;
	}
	return (error);
}

/* ARGSUSED */
int
sys_bind(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	register struct sys_bind_args /* {
		syscallarg(int) s;
		syscallarg(struct sockaddr *) name;
		syscallarg(socklen_t) namelen;
	} */ *uap = v;
	struct file *fp;
	struct mbuf *nam;
	int error;

	if ((error = getsock(p->p_fd, SCARG(uap, s), &fp)) != 0)
		return (error);
	error = sockargs(&nam, (caddr_t)SCARG(uap, name), SCARG(uap, namelen),
			 MT_SONAME);
	if (error)
		return (error);
	error = sobind((struct socket *)fp->f_data, nam);
	m_freem(nam);
	return (error);
}

/* ARGSUSED */
int
sys_listen(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	register struct sys_listen_args /* {
		syscallarg(int) s;
		syscallarg(int) backlog;
	} */ *uap = v;
	struct file *fp;
	int error;

	if ((error = getsock(p->p_fd, SCARG(uap, s), &fp)) != 0)
		return (error);
	return (solisten((struct socket *)fp->f_data, SCARG(uap, backlog)));
}

int
sys_accept(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	register struct sys_accept_args /* {
		syscallarg(int) s;
		syscallarg(struct sockaddr *) name;
		syscallarg(socklen_t *) anamelen;
	} */ *uap = v;
	struct file *fp;
	struct mbuf *nam;
	socklen_t namelen;
	int error, s, tmpfd;
	register struct socket *so;

	if (SCARG(uap, name) && (error = copyin((caddr_t)SCARG(uap, anamelen),
	    (caddr_t)&namelen, sizeof (namelen))))
		return (error);
	if ((error = getsock(p->p_fd, SCARG(uap, s), &fp)) != 0)
		return (error);
	s = splsoftnet();
	so = (struct socket *)fp->f_data;
	if ((so->so_options & SO_ACCEPTCONN) == 0) {
		splx(s);
		return (EINVAL);
	}
	if ((so->so_state & SS_NBIO) && so->so_qlen == 0) {
		splx(s);
		return (EWOULDBLOCK);
	}
	while (so->so_qlen == 0 && so->so_error == 0) {
		if (so->so_state & SS_CANTRCVMORE) {
			so->so_error = ECONNABORTED;
			break;
		}
		error = tsleep((caddr_t)&so->so_timeo, PSOCK | PCATCH,
		    netcon, 0);
		if (error) {
			splx(s);
			return (error);
		}
	}
	if (so->so_error) {
		error = so->so_error;
		so->so_error = 0;
		splx(s);
		return (error);
	}
	if ((error = falloc(p, &fp, &tmpfd)) != 0) {
		splx(s);
		return (error);
	}
	*retval = tmpfd;

	/* connection has been removed from the listen queue */
	KNOTE(&so->so_rcv.sb_sel.si_note, 0);

	{ struct socket *aso = so->so_q;
	  if (soqremque(aso, 1) == 0)
		panic("accept");
	  so = aso;
	}

	fp->f_type = DTYPE_SOCKET;
	fp->f_flag = FREAD|FWRITE;
	fp->f_ops = &socketops;
	fp->f_data = (caddr_t)so;
	nam = m_get(M_WAIT, MT_SONAME);
	error = soaccept(so, nam);
	if (!error && SCARG(uap, name)) {
		if (namelen > nam->m_len)
			namelen = nam->m_len;
		/* SHOULD COPY OUT A CHAIN HERE */
		if ((error = copyout(mtod(nam, caddr_t),
		    (caddr_t)SCARG(uap, name), namelen)) == 0)
			error = copyout((caddr_t)&namelen,
			    (caddr_t)SCARG(uap, anamelen),
			    sizeof (*SCARG(uap, anamelen)));
	}
	/* if an error occured, free the file descriptor */
	if (error) {
		fdremove(p->p_fd, tmpfd);
		ffree(fp);
	}
	m_freem(nam);
	splx(s);
	return (error);
}

/* ARGSUSED */
int
sys_connect(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	register struct sys_connect_args /* {
		syscallarg(int) s;
		syscallarg(struct sockaddr *) name;
		syscallarg(socklen_t) namelen;
	} */ *uap = v;
	struct file *fp;
	register struct socket *so;
	struct mbuf *nam;
	int error, s;

	if ((error = getsock(p->p_fd, SCARG(uap, s), &fp)) != 0)
		return (error);
	so = (struct socket *)fp->f_data;
	if ((so->so_state & SS_NBIO) && (so->so_state & SS_ISCONNECTING))
		return (EALREADY);
	error = sockargs(&nam, (caddr_t)SCARG(uap, name), SCARG(uap, namelen),
			 MT_SONAME);
	if (error)
		return (error);
	error = soconnect(so, nam);
	if (error)
		goto bad;
	if ((so->so_state & SS_NBIO) && (so->so_state & SS_ISCONNECTING)) {
		m_freem(nam);
		return (EINPROGRESS);
	}
	s = splsoftnet();
	while ((so->so_state & SS_ISCONNECTING) && so->so_error == 0) {
		error = tsleep((caddr_t)&so->so_timeo, PSOCK | PCATCH,
		    netcon, 0);
		if (error)
			break;
	}
	if (error == 0) {
		error = so->so_error;
		so->so_error = 0;
	}
	splx(s);
bad:
	so->so_state &= ~SS_ISCONNECTING;
	m_freem(nam);
	if (error == ERESTART)
		error = EINTR;
	return (error);
}

int
sys_socketpair(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	register struct sys_socketpair_args /* {
		syscallarg(int) domain;
		syscallarg(int) type;
		syscallarg(int) protocol;
		syscallarg(int *) rsv;
	} */ *uap = v;
	register struct filedesc *fdp = p->p_fd;
	struct file *fp1, *fp2;
	struct socket *so1, *so2;
	int fd, error, sv[2];

	error = socreate(SCARG(uap, domain), &so1, SCARG(uap, type),
			 SCARG(uap, protocol));
	if (error)
		return (error);
	error = socreate(SCARG(uap, domain), &so2, SCARG(uap, type),
			 SCARG(uap, protocol));
	if (error)
		goto free1;
	if ((error = falloc(p, &fp1, &fd)) != 0)
		goto free2;
	sv[0] = fd;
	fp1->f_flag = FREAD|FWRITE;
	fp1->f_type = DTYPE_SOCKET;
	fp1->f_ops = &socketops;
	fp1->f_data = (caddr_t)so1;
	if ((error = falloc(p, &fp2, &fd)) != 0)
		goto free3;
	fp2->f_flag = FREAD|FWRITE;
	fp2->f_type = DTYPE_SOCKET;
	fp2->f_ops = &socketops;
	fp2->f_data = (caddr_t)so2;
	sv[1] = fd;
	if ((error = soconnect2(so1, so2)) != 0)
		goto free4;
	if (SCARG(uap, type) == SOCK_DGRAM) {
		/*
		 * Datagram socket connection is asymmetric.
		 */
		 if ((error = soconnect2(so2, so1)) != 0)
			goto free4;
	}
	error = copyout((caddr_t)sv, (caddr_t)SCARG(uap, rsv),
	    2 * sizeof (int));
	if (error == 0)
		return (error);
free4:
	ffree(fp2);
	fdremove(fdp, sv[1]);
free3:
	ffree(fp1);
	fdremove(fdp, sv[0]);
free2:
	(void)soclose(so2);
free1:
	(void)soclose(so1);
	return (error);
}

int
sys_sendto(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	register struct sys_sendto_args /* {
		syscallarg(int) s;
		syscallarg(caddr_t) buf;
		syscallarg(size_t) len;
		syscallarg(int) flags;
		syscallarg(struct sockaddr *) to;
		syscallarg(socklen_t) tolen;
	} */ *uap = v;
	struct msghdr msg;
	struct iovec aiov;

	msg.msg_name = (caddr_t)SCARG(uap, to);
	msg.msg_namelen = SCARG(uap, tolen);
	msg.msg_iov = &aiov;
	msg.msg_iovlen = 1;
	msg.msg_control = 0;
#ifdef COMPAT_OLDSOCK
	msg.msg_flags = 0;
#endif
	aiov.iov_base = (char *)SCARG(uap, buf);
	aiov.iov_len = SCARG(uap, len);
	return (sendit(p, SCARG(uap, s), &msg, SCARG(uap, flags), retval));
}

int
sys_sendmsg(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	register struct sys_sendmsg_args /* {
		syscallarg(int) s;
		syscallarg(caddr_t) msg;
		syscallarg(int) flags;
	} */ *uap = v;
	struct msghdr msg;
	struct iovec aiov[UIO_SMALLIOV], *iov;
	int error;

	error = copyin(SCARG(uap, msg), (caddr_t)&msg, sizeof (msg));
	if (error)
		return (error);
	if (msg.msg_iovlen <= 0 || msg.msg_iovlen > IOV_MAX)
		return (EMSGSIZE);
	if (msg.msg_iovlen > UIO_SMALLIOV)
		iov = malloc(sizeof(struct iovec) * msg.msg_iovlen,
		    M_IOV, M_WAITOK);
	else
		iov = aiov;
	if (msg.msg_iovlen &&
	    (error = copyin((caddr_t)msg.msg_iov, (caddr_t)iov,
	    (unsigned)(msg.msg_iovlen * sizeof (struct iovec)))))
		goto done;
	msg.msg_iov = iov;
#ifdef COMPAT_OLDSOCK
	msg.msg_flags = 0;
#endif
	error = sendit(p, SCARG(uap, s), &msg, SCARG(uap, flags), retval);
done:
	if (iov != aiov)
		free(iov, M_IOV);
	return (error);
}

int
sendit(p, s, mp, flags, retsize)
	register struct proc *p;
	int s;
	register struct msghdr *mp;
	int flags;
	register_t *retsize;
{
	struct file *fp;
	struct uio auio;
	register struct iovec *iov;
	register int i;
	struct mbuf *to, *control;
	int len, error;
#ifdef KTRACE
	struct iovec *ktriov = NULL;
#endif

	if ((error = getsock(p->p_fd, s, &fp)) != 0)
		return (error);
	auio.uio_iov = mp->msg_iov;
	auio.uio_iovcnt = mp->msg_iovlen;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_rw = UIO_WRITE;
	auio.uio_procp = p;
	auio.uio_offset = 0;			/* XXX */
	auio.uio_resid = 0;
	iov = mp->msg_iov;
	for (i = 0; i < mp->msg_iovlen; i++, iov++) {
		/* Don't allow sum > SSIZE_MAX */
		if (iov->iov_len > SSIZE_MAX ||
		    (auio.uio_resid += iov->iov_len) > SSIZE_MAX)
			return (EINVAL);
	}
	if (mp->msg_name) {
		error = sockargs(&to, mp->msg_name, mp->msg_namelen,
				 MT_SONAME);
		if (error)
			return (error);
	} else
		to = 0;
	if (mp->msg_control) {
		if (mp->msg_controllen < sizeof(struct cmsghdr)
#ifdef COMPAT_OLDSOCK
		    && mp->msg_flags != MSG_COMPAT
#endif
		) {
			error = EINVAL;
			goto bad;
		}
		error = sockargs(&control, mp->msg_control,
				 mp->msg_controllen, MT_CONTROL);
		if (error)
			goto bad;
#ifdef COMPAT_OLDSOCK
		if (mp->msg_flags == MSG_COMPAT) {
			register struct cmsghdr *cm;

			M_PREPEND(control, sizeof(*cm), M_WAIT);
			cm = mtod(control, struct cmsghdr *);
			cm->cmsg_len = control->m_len;
			cm->cmsg_level = SOL_SOCKET;
			cm->cmsg_type = SCM_RIGHTS;
		}
#endif
	} else
		control = 0;
#ifdef KTRACE
	if (KTRPOINT(p, KTR_GENIO)) {
		int iovlen = auio.uio_iovcnt * sizeof (struct iovec);

		ktriov = malloc(iovlen, M_TEMP, M_WAITOK);
		bcopy((caddr_t)auio.uio_iov, (caddr_t)ktriov, iovlen);
	}
#endif
	len = auio.uio_resid;
	error = sosend((struct socket *)fp->f_data, to, &auio,
	    NULL, control, flags);
	if (error) {
		if (auio.uio_resid != len && (error == ERESTART ||
		    error == EINTR || error == EWOULDBLOCK))
			error = 0;
		if (error == EPIPE)
			psignal(p, SIGPIPE);
	}
	if (error == 0)
		*retsize = len - auio.uio_resid;
#ifdef KTRACE
	if (ktriov != NULL) {
		if (error == 0)
			ktrgenio(p, s, UIO_WRITE, ktriov, *retsize, error);
		free(ktriov, M_TEMP);
	}
#endif
bad:
	if (to)
		m_freem(to);
	return (error);
}

int
sys_recvfrom(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	register struct sys_recvfrom_args /* {
		syscallarg(int) s;
		syscallarg(caddr_t) buf;
		syscallarg(size_t) len;
		syscallarg(int) flags;
		syscallarg(struct sockaddr *) from;
		syscallarg(socklen_t *) fromlenaddr;
	} */ *uap = v;
	struct msghdr msg;
	struct iovec aiov;
	int error;

	if (SCARG(uap, fromlenaddr)) {
		error = copyin((caddr_t)SCARG(uap, fromlenaddr),
		    (caddr_t)&msg.msg_namelen, sizeof (msg.msg_namelen));
		if (error)
			return (error);
	} else
		msg.msg_namelen = 0;
	msg.msg_name = (caddr_t)SCARG(uap, from);
	msg.msg_iov = &aiov;
	msg.msg_iovlen = 1;
	aiov.iov_base = SCARG(uap, buf);
	aiov.iov_len = SCARG(uap, len);
	msg.msg_control = 0;
	msg.msg_flags = SCARG(uap, flags);
	return (recvit(p, SCARG(uap, s), &msg,
	    (caddr_t)SCARG(uap, fromlenaddr), retval));
}

int
sys_recvmsg(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	register struct sys_recvmsg_args /* {
		syscallarg(int) s;
		syscallarg(struct msghdr *) msg;
		syscallarg(int) flags;
	} */ *uap = v;
	struct msghdr msg;
	struct iovec aiov[UIO_SMALLIOV], *uiov, *iov;
	register int error;

	error = copyin((caddr_t)SCARG(uap, msg), (caddr_t)&msg,
	    sizeof (msg));
	if (error)
		return (error);
	if (msg.msg_iovlen <= 0 || msg.msg_iovlen > IOV_MAX)
		return (EMSGSIZE);
	if (msg.msg_iovlen > UIO_SMALLIOV)
		iov = malloc(sizeof(struct iovec) * msg.msg_iovlen,
		    M_IOV, M_WAITOK);
	else
		iov = aiov;
#ifdef COMPAT_OLDSOCK
	msg.msg_flags = SCARG(uap, flags) &~ MSG_COMPAT;
#else
	msg.msg_flags = SCARG(uap, flags);
#endif
	uiov = msg.msg_iov;
	msg.msg_iov = iov;
	error = copyin((caddr_t)uiov, (caddr_t)iov,
	    (unsigned)(msg.msg_iovlen * sizeof (struct iovec)));
	if (error)
		goto done;
	if ((error = recvit(p, SCARG(uap, s), &msg, (caddr_t)0, retval)) == 0) {
		msg.msg_iov = uiov;
		error = copyout((caddr_t)&msg, (caddr_t)SCARG(uap, msg),
		    sizeof(msg));
	}
done:
	if (iov != aiov)
		free(iov, M_IOV);
	return (error);
}

int
recvit(p, s, mp, namelenp, retsize)
	register struct proc *p;
	int s;
	register struct msghdr *mp;
	caddr_t namelenp;
	register_t *retsize;
{
	struct file *fp;
	struct uio auio;
	register struct iovec *iov;
	register int i;
	size_t len;
	int error;
	struct mbuf *from = 0, *control = 0;
#ifdef KTRACE
	struct iovec *ktriov = NULL;
#endif

	if ((error = getsock(p->p_fd, s, &fp)) != 0)
		return (error);
	auio.uio_iov = mp->msg_iov;
	auio.uio_iovcnt = mp->msg_iovlen;
	auio.uio_segflg = UIO_USERSPACE;
	auio.uio_rw = UIO_READ;
	auio.uio_procp = p;
	auio.uio_offset = 0;			/* XXX */
	auio.uio_resid = 0;
	iov = mp->msg_iov;
	for (i = 0; i < mp->msg_iovlen; i++, iov++) {
		/* Don't allow sum > SSIZE_MAX */
		if (iov->iov_len > SSIZE_MAX ||
		    (auio.uio_resid += iov->iov_len) > SSIZE_MAX)
			return (EINVAL);
	}
#ifdef KTRACE
	if (KTRPOINT(p, KTR_GENIO)) {
		int iovlen = auio.uio_iovcnt * sizeof (struct iovec);

		ktriov = malloc(iovlen, M_TEMP, M_WAITOK);
		bcopy((caddr_t)auio.uio_iov, (caddr_t)ktriov, iovlen);
	}
#endif
	len = auio.uio_resid;
	error = soreceive((struct socket *)fp->f_data, &from, &auio,
			  NULL, mp->msg_control ? &control : NULL,
			  &mp->msg_flags);
	if (error) {
		if (auio.uio_resid != len && (error == ERESTART ||
		    error == EINTR || error == EWOULDBLOCK))
			error = 0;
	}
#ifdef KTRACE
	if (ktriov != NULL) {
		if (error == 0)
			ktrgenio(p, s, UIO_READ,
				ktriov, len - auio.uio_resid, error);
		free(ktriov, M_TEMP);
	}
#endif
	if (error)
		goto out;
	*retsize = len - auio.uio_resid;
	if (mp->msg_name) {
		socklen_t alen;

		if (from == 0)
			alen = 0;
		else {
			/* save sa_len before it is destroyed by MSG_COMPAT */
			alen = mp->msg_namelen;
			if (alen > from->m_len)
				alen = from->m_len;
			/* else if alen < from->m_len ??? */
#ifdef COMPAT_OLDSOCK
			if (mp->msg_flags & MSG_COMPAT)
				mtod(from, struct osockaddr *)->sa_family =
				    mtod(from, struct sockaddr *)->sa_family;
#endif
			error = copyout(mtod(from, caddr_t),
			    (caddr_t)mp->msg_name, alen);
			if (error)
				goto out;
		}
		mp->msg_namelen = alen;
		if (namelenp &&
		    (error = copyout((caddr_t)&alen, namelenp, sizeof(alen)))) {
#ifdef COMPAT_OLDSOCK
			if (mp->msg_flags & MSG_COMPAT)
				error = 0;	/* old recvfrom didn't check */
			else
#endif
			goto out;
		}
	}
	if (mp->msg_control) {
#ifdef COMPAT_OLDSOCK
		/*
		 * We assume that old recvmsg calls won't receive access
		 * rights and other control info, esp. as control info
		 * is always optional and those options didn't exist in 4.3.
		 * If we receive rights, trim the cmsghdr; anything else
		 * is tossed.
		 */
		if (control && mp->msg_flags & MSG_COMPAT) {
			if (mtod(control, struct cmsghdr *)->cmsg_level !=
			    SOL_SOCKET ||
			    mtod(control, struct cmsghdr *)->cmsg_type !=
			    SCM_RIGHTS) {
				mp->msg_controllen = 0;
				goto out;
			}
			control->m_len -= sizeof (struct cmsghdr);
			control->m_data += sizeof (struct cmsghdr);
		}
#endif
		len = mp->msg_controllen;
		if (len <= 0 || control == 0)
			len = 0;
		else {
			struct mbuf *m = control;
			caddr_t p = (caddr_t)mp->msg_control;

			do {
				i = m->m_len;
				if (len < i) {
					mp->msg_flags |= MSG_CTRUNC;
					i = len;
				}
				error = copyout(mtod(m, caddr_t), p,
				    (unsigned)i);
				if (m->m_next)
					i = ALIGN(i);
				p += i;
				len -= i;
				if (error != 0 || len <= 0)
					break;
			} while ((m = m->m_next) != NULL);
			len = p - (caddr_t)mp->msg_control;
		}
		mp->msg_controllen = len;
	}
out:
	if (from)
		m_freem(from);
	if (control)
		m_freem(control);
	return (error);
}

/* ARGSUSED */
int
sys_shutdown(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	register struct sys_shutdown_args /* {
		syscallarg(int) s;
		syscallarg(int) how;
	} */ *uap = v;
	struct file *fp;
	int error;

	if ((error = getsock(p->p_fd, SCARG(uap, s), &fp)) != 0)
		return (error);
	return (soshutdown((struct socket *)fp->f_data, SCARG(uap, how)));
}

/* ARGSUSED */
int
sys_setsockopt(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	register struct sys_setsockopt_args /* {
		syscallarg(int) s;
		syscallarg(int) level;
		syscallarg(int) name;
		syscallarg(caddr_t) val;
		syscallarg(socklen_t) valsize;
	} */ *uap = v;
	struct file *fp;
	struct mbuf *m = NULL;
	int error;

	if ((error = getsock(p->p_fd, SCARG(uap, s), &fp)) != 0)
		return (error);
	if (SCARG(uap, valsize) > MCLBYTES)
		return (EINVAL);
	if (SCARG(uap, val)) {
		m = m_get(M_WAIT, MT_SOOPTS);
		if (SCARG(uap, valsize) > MLEN) {
			MCLGET(m, M_DONTWAIT);
			if ((m->m_flags & M_EXT) == 0) {
				m_freem(m);
				return (ENOBUFS);
			}
		}
		if (m == NULL)
			return (ENOBUFS);
		error = copyin(SCARG(uap, val), mtod(m, caddr_t),
		    SCARG(uap, valsize));
		if (error) {
			(void) m_free(m);
			return (error);
		}
		m->m_len = SCARG(uap, valsize);
	}
	return (sosetopt((struct socket *)fp->f_data, SCARG(uap, level),
			 SCARG(uap, name), m));
}

/* ARGSUSED */
int
sys_getsockopt(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	register struct sys_getsockopt_args /* {
		syscallarg(int) s;
		syscallarg(int) level;
		syscallarg(int) name;
		syscallarg(caddr_t) val;
		syscallarg(socklen_t *) avalsize;
	} */ *uap = v;
	struct file *fp;
	struct mbuf *m = NULL;
	socklen_t valsize;
	int error;

	if ((error = getsock(p->p_fd, SCARG(uap, s), &fp)) != 0)
		return (error);
	if (SCARG(uap, val)) {
		error = copyin((caddr_t)SCARG(uap, avalsize),
		    (caddr_t)&valsize, sizeof (valsize));
		if (error)
			return (error);
	} else
		valsize = 0;
	if ((error = sogetopt((struct socket *)fp->f_data, SCARG(uap, level),
	    SCARG(uap, name), &m)) == 0 && SCARG(uap, val) && valsize &&
	    m != NULL) {
		if (valsize > m->m_len)
			valsize = m->m_len;
		error = copyout(mtod(m, caddr_t), SCARG(uap, val), valsize);
		if (error == 0)
			error = copyout((caddr_t)&valsize,
			    (caddr_t)SCARG(uap, avalsize), sizeof (valsize));
	}
	if (m != NULL)
		(void) m_free(m);
	return (error);
}

int
sys_pipe(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	register struct sys_pipe_args /* {
		syscallarg(int *) fdp;
	} */ *uap = v;
	int error, fds[2];
	register_t rval[2];

	if ((error = sys_opipe(p, v, rval)) != 0)
		return (error);

	fds[0] = rval[0];
	fds[1] = rval[1];
	error = copyout((caddr_t)fds, (caddr_t)SCARG(uap, fdp),
	    2 * sizeof (int));
	if (error) {
		fdrelease(p, retval[0]);
		fdrelease(p, retval[1]);
	}
	return (error);
}

/*
 * Get socket name.
 */
/* ARGSUSED */
int
sys_getsockname(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	register struct sys_getsockname_args /* {
		syscallarg(int) fdes;
		syscallarg(caddr_t) asa;
		syscallarg(socklen_t *) alen;
	} */ *uap = v;
	struct file *fp;
	register struct socket *so;
	struct mbuf *m;
	socklen_t len;
	int error;

	if ((error = getsock(p->p_fd, SCARG(uap, fdes), &fp)) != 0)
		return (error);
	error = copyin((caddr_t)SCARG(uap, alen), (caddr_t)&len, sizeof (len));
	if (error)
		return (error);
	so = (struct socket *)fp->f_data;
	m = m_getclr(M_WAIT, MT_SONAME);
	error = (*so->so_proto->pr_usrreq)(so, PRU_SOCKADDR, 0, m, 0);
	if (error)
		goto bad;
	if (len > m->m_len)
		len = m->m_len;
	error = copyout(mtod(m, caddr_t), (caddr_t)SCARG(uap, asa), len);
	if (error == 0)
		error = copyout((caddr_t)&len, (caddr_t)SCARG(uap, alen),
		    sizeof (len));
bad:
	m_freem(m);
	return (error);
}

/*
 * Get name of peer for connected socket.
 */
/* ARGSUSED */
int
sys_getpeername(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	register struct sys_getpeername_args /* {
		syscallarg(int) fdes;
		syscallarg(caddr_t) asa;
		syscallarg(socklen_t *) alen;
	} */ *uap = v;
	struct file *fp;
	register struct socket *so;
	struct mbuf *m;
	socklen_t len;
	int error;

	if ((error = getsock(p->p_fd, SCARG(uap, fdes), &fp)) != 0)
		return (error);
	so = (struct socket *)fp->f_data;
	if ((so->so_state & (SS_ISCONNECTED|SS_ISCONFIRMING)) == 0)
		return (ENOTCONN);
	error = copyin((caddr_t)SCARG(uap, alen), (caddr_t)&len, sizeof (len));
	if (error)
		return (error);
	m = m_getclr(M_WAIT, MT_SONAME);
	error = (*so->so_proto->pr_usrreq)(so, PRU_PEERADDR, 0, m, 0);
	if (error)
		goto bad;
	if (len > m->m_len)
		len = m->m_len;
	error = copyout(mtod(m, caddr_t), (caddr_t)SCARG(uap, asa), len);
	if (error == 0)
		error = copyout((caddr_t)&len, (caddr_t)SCARG(uap, alen),
		    sizeof (len));
bad:
	m_freem(m);
	return (error);
}

/*
 * Get eid of peer for connected socket.
 */
/* ARGSUSED */
int
sys_getpeereid(p, v, retval)
	struct proc *p;
	void *v;
	register_t *retval;
{
	register struct sys_getpeereid_args /* {
		syscallarg(int) fdes;
		syscallarg(uid_t *) euid;
		syscallarg(gid_t *) egid;
	} */ *uap = v;
	struct file *fp;
	register struct socket *so;
	struct mbuf *m;
	struct unpcbid *id;
	int error;

	if ((error = getsock(p->p_fd, SCARG(uap, fdes), &fp)) != 0)
		return (error);
	so = (struct socket *)fp->f_data;
	if (so->so_proto != pffindtype(AF_LOCAL, SOCK_STREAM))
		return (EOPNOTSUPP);
	m = m_getclr(M_WAIT, MT_SONAME);
	if (m == NULL)
		return (ENOBUFS);
	error = (*so->so_proto->pr_usrreq)(so, PRU_PEEREID, 0, m, 0);
	if (!error && m->m_len != sizeof(struct unpcbid))
		error = EOPNOTSUPP;
	if (error)
		goto bad;
	id = mtod(m, struct unpcbid *);
	error = copyout((caddr_t)&(id->unp_euid),
		(caddr_t)SCARG(uap, euid), sizeof(uid_t));
	if (error == 0)
		error = copyout((caddr_t)&(id->unp_egid),
		    (caddr_t)SCARG(uap, egid), sizeof(gid_t));
bad:
	m_freem(m);
	return (error);
}

int
sockargs(mp, buf, buflen, type)
	struct mbuf **mp;
	caddr_t buf;
	socklen_t buflen;
	int type;
{
	register struct sockaddr *sa;
	register struct mbuf *m;
	int error;

	/*
	 * We can't allow socket names > UCHAR_MAX in length, since that
	 * will overflow sa_len.
	 */
	if (type == MT_SONAME && (u_int)buflen > UCHAR_MAX)
		return (EINVAL);
	if ((u_int)buflen > MCLBYTES)
		return (EINVAL);

	/* Allocate an mbuf to hold the arguments. */
	m = m_get(M_WAIT, type);
	if ((u_int)buflen > MLEN) {
		MCLGET(m, M_WAITOK);
		if ((m->m_flags & M_EXT) == 0) {
			m_free(m);
			return ENOBUFS;
		}
	}
	m->m_len = buflen;
	error = copyin(buf, mtod(m, caddr_t), buflen);
	if (error) {
		(void) m_free(m);
		return (error);
	}
	*mp = m;
	if (type == MT_SONAME) {
		sa = mtod(m, struct sockaddr *);

#if defined(COMPAT_OLDSOCK) && BYTE_ORDER != BIG_ENDIAN
		if (sa->sa_family == 0 && sa->sa_len < AF_MAX)
			sa->sa_family = sa->sa_len;
#endif
		sa->sa_len = buflen;
	}
	return (0);
}

int
getsock(fdp, fdes, fpp)
	struct filedesc *fdp;
	int fdes;
	struct file **fpp;
{
	register struct file *fp;

	if ((unsigned)fdes >= fdp->fd_nfiles ||
	    (fp = fdp->fd_ofiles[fdes]) == NULL)
		return (EBADF);
	if (fp->f_type != DTYPE_SOCKET)
		return (ENOTSOCK);
	*fpp = fp;
	return (0);
}
