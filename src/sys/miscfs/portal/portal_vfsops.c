/*	$OpenBSD: portal_vfsops.c,v 1.18 2003/08/14 07:46:39 mickey Exp $	*/
/*	$NetBSD: portal_vfsops.c,v 1.14 1996/02/09 22:40:41 christos Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software donated to Berkeley by
 * Jan-Simon Pendry.
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
 *	from: Id: portal_vfsops.c,v 1.5 1992/05/30 10:25:27 jsp Exp
 *	@(#)portal_vfsops.c	8.6 (Berkeley) 1/21/94
 */

/*
 * Portal Filesystem
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/filedesc.h>
#include <sys/file.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/protosw.h>
#include <sys/domain.h>
#include <sys/un.h>
#include <miscfs/portal/portal.h>

#define portal_init ((int (*)(struct vfsconf *))nullop)

int	portal_mount(struct mount *, const char *, void *,
			  struct nameidata *, struct proc *);
int	portal_start(struct mount *, int, struct proc *);
int	portal_unmount(struct mount *, int, struct proc *);
int	portal_root(struct mount *, struct vnode **);
int	portal_statfs(struct mount *, struct statfs *, struct proc *);


/*
 * Mount the per-process file descriptors (/dev/fd)
 */
int
portal_mount(mp, path, data, ndp, p)
	struct mount *mp;
	const char *path;
	void *data;
	struct nameidata *ndp;
	struct proc *p;
{
	struct file *fp;
	struct portal_args args;
	struct portalmount *fmp;
	struct socket *so;
	struct vnode *rvp;
	size_t size;
	int error;

	/*
	 * Update is a no-op
	 */
	if (mp->mnt_flag & MNT_UPDATE)
		return (EOPNOTSUPP);

	error = copyin(data, &args, sizeof(struct portal_args));
	if (error)
		return (error);

	if ((error = getsock(p->p_fd, args.pa_socket, &fp)) != 0)
		return (error);
	so = (struct socket *) fp->f_data;
	if (so->so_proto->pr_domain->dom_family != AF_UNIX) {
		FRELE(fp);
		return (ESOCKTNOSUPPORT);
	}

	error = getnewvnode(VT_PORTAL, mp, portal_vnodeop_p, &rvp); /* XXX */
	if (error) {
		FRELE(fp);
		return (error);
	}
	MALLOC(rvp->v_data, void *, sizeof(struct portalnode),
		M_TEMP, M_WAITOK);

	fmp = (struct portalmount *) malloc(sizeof(struct portalmount),
				 M_MISCFSMNT, M_WAITOK);
	rvp->v_type = VDIR;
	rvp->v_flag |= VROOT;
	VTOPORTAL(rvp)->pt_arg = 0;
	VTOPORTAL(rvp)->pt_size = 0;
	VTOPORTAL(rvp)->pt_fileid = PORTAL_ROOTFILEID;
	fmp->pm_root = rvp;
	fmp->pm_server = fp;
	fp->f_count++;
	FRELE(fp);

	mp->mnt_flag |= MNT_LOCAL;
	mp->mnt_data = (qaddr_t)fmp;
	vfs_getnewfsid(mp);

	(void) copyinstr(path, mp->mnt_stat.f_mntonname, MNAMELEN - 1, &size);
	bzero(mp->mnt_stat.f_mntonname + size, MNAMELEN - size);
	(void) copyinstr(args.pa_config, mp->mnt_stat.f_mntfromname,
	    MNAMELEN - 1, &size);
	bzero(mp->mnt_stat.f_mntfromname + size, MNAMELEN - size);
	return (0);
}

int
portal_start(mp, flags, p)
	struct mount *mp;
	int flags;
	struct proc *p;
{

	return (0);
}

int
portal_unmount(mp, mntflags, p)
	struct mount *mp;
	int mntflags;
	struct proc *p;
{
	struct vnode *rvp = VFSTOPORTAL(mp)->pm_root;
	int error, flags = 0;

	if (mntflags & MNT_FORCE) {
		flags |= FORCECLOSE;
	}

	/*
	 * Clear out buffer cache.  I don't think we
	 * ever get anything cached at this level at the
	 * moment, but who knows...
	 */
#ifdef notyet
	mntflushbuf(mp, 0); 
	if (mntinvalbuf(mp, 1))
		return (EBUSY);
#endif
	if (rvp->v_usecount > 1)
		return (EBUSY);
	if ((error = vflush(mp, rvp, flags)) != 0)
		return (error);

	/*
	 * Release reference on underlying root vnode
	 */
	vrele(rvp);
	/*
	 * And blow it away for future re-use
	 */
	vgone(rvp);
	/*
	 * Shutdown the socket.  This will cause the select in the
	 * daemon to wake up, and then the accept will get ECONNABORTED
	 * which it interprets as a request to go and bury itself.
	 */
	FREF(VFSTOPORTAL(mp)->pm_server);
	soshutdown((struct socket *) VFSTOPORTAL(mp)->pm_server->f_data, 2);
	/*
	 * Discard reference to underlying file.  Must call closef because
	 * this may be the last reference.
	 */
	closef(VFSTOPORTAL(mp)->pm_server, NULL);
	/*
	 * Finally, throw away the portalmount structure
	 */
	free(mp->mnt_data, M_MISCFSMNT);
	mp->mnt_data = 0;
	return (0);
}

int
portal_root(mp, vpp)
	struct mount *mp;
	struct vnode **vpp;
{
	struct vnode *vp;
	struct proc *p = curproc;

	/*
	 * Return locked reference to root.
	 */
	vp = VFSTOPORTAL(mp)->pm_root;
	VREF(vp);
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);
	*vpp = vp;
	return (0);
}

int
portal_statfs(mp, sbp, p)
	struct mount *mp;
	struct statfs *sbp;
	struct proc *p;
{

	sbp->f_bsize = DEV_BSIZE;
	sbp->f_iosize = DEV_BSIZE;
	sbp->f_blocks = 2;		/* 1K to keep df happy */
	sbp->f_bfree = 0;
	sbp->f_bavail = 0;
	sbp->f_files = 1;		/* Allow for "." */
	sbp->f_ffree = 0;		/* See comments above */
	if (sbp != &mp->mnt_stat) {
		bcopy(&mp->mnt_stat.f_fsid, &sbp->f_fsid, sizeof(sbp->f_fsid));
		bcopy(mp->mnt_stat.f_mntonname, sbp->f_mntonname, MNAMELEN);
		bcopy(mp->mnt_stat.f_mntfromname, sbp->f_mntfromname, MNAMELEN);
	}
	strncpy(sbp->f_fstypename, mp->mnt_vfc->vfc_name, MFSNAMELEN);
	return (0);
}


#define portal_sync ((int (*)(struct mount *, int, struct ucred *, \
				  struct proc *))nullop)

#define portal_fhtovp ((int (*)(struct mount *, struct fid *, \
	    struct vnode **))eopnotsupp)
#define portal_quotactl ((int (*)(struct mount *, int, uid_t, caddr_t, \
	    struct proc *))eopnotsupp)
#define portal_sysctl ((int (*)(int *, u_int, void *, size_t *, void *, \
	    size_t, struct proc *))eopnotsupp)
#define portal_vget ((int (*)(struct mount *, ino_t, struct vnode **)) \
	    eopnotsupp)
#define portal_vptofh ((int (*)(struct vnode *, struct fid *))eopnotsupp)
#define portal_checkexp ((int (*)(struct mount *, struct mbuf *,	\
	int *, struct ucred **))eopnotsupp)

const struct vfsops portal_vfsops = {
	portal_mount,
	portal_start,
	portal_unmount,
	portal_root,
	portal_quotactl,
	portal_statfs,
	portal_sync,
	portal_vget,
	portal_fhtovp,
	portal_vptofh,
	portal_init,
	portal_sysctl,
	portal_checkexp
};
