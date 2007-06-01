/*	$OpenBSD: procfs_vnops.c,v 1.39 2007/06/01 23:47:57 deraadt Exp $	*/
/*	$NetBSD: procfs_vnops.c,v 1.40 1996/03/16 23:52:55 christos Exp $	*/

/*
 * Copyright (c) 1993 Jan-Simon Pendry
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
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
 *	@(#)procfs_vnops.c	8.8 (Berkeley) 6/15/94
 */

/*
 * procfs vnode interface
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/vnode.h>
#include <sys/namei.h>
#include <sys/malloc.h>
#include <sys/dirent.h>
#include <sys/resourcevar.h>
#include <sys/poll.h>
#include <sys/ptrace.h>
#include <sys/stat.h>

#include <uvm/uvm_extern.h>	/* for PAGE_SIZE */

#include <machine/reg.h>

#include <miscfs/procfs/procfs.h>

/*
 * Vnode Operations.
 *
 */
static int procfs_validfile_linux(struct proc *, struct mount *);

/*
 * This is a list of the valid names in the
 * process-specific sub-directories.  It is
 * used in procfs_lookup and procfs_readdir
 */
struct proc_target {
	u_char	pt_type;
	u_char	pt_namlen;
	char	*pt_name;
	pfstype	pt_pfstype;
	int	(*pt_valid)(struct proc *p, struct mount *mp);
} proc_targets[] = {
#define N(s) sizeof(s)-1, s
	/*	  name		type		validp */
	{ DT_DIR, N("."),	Pproc,		NULL },
	{ DT_DIR, N(".."),	Proot,		NULL },
	{ DT_REG, N("file"),	Pfile,		procfs_validfile },
	{ DT_REG, N("mem"),	Pmem,		NULL },
	{ DT_REG, N("ctl"),	Pctl,		NULL },
	{ DT_REG, N("status"),	Pstatus,	NULL },
	{ DT_REG, N("note"),	Pnote,		NULL },
	{ DT_REG, N("notepg"),	Pnotepg,	NULL },
	{ DT_REG, N("cmdline"), Pcmdline,	NULL },
	{ DT_REG, N("exe"),	Pfile,		procfs_validfile_linux },
#undef N
};
static int nproc_targets = sizeof(proc_targets) / sizeof(proc_targets[0]);

/*
 * List of files in the root directory.  Note: the validate function
 * will be called with p == NULL for these
 */
struct proc_target proc_root_targets[] = {
#define N(s) sizeof(s)-1, s
	/*	  name		type		validp */
	{ DT_REG, N("meminfo"),	Pmeminfo,	procfs_validfile_linux },
	{ DT_REG, N("cpuinfo"),	Pcpuinfo,	procfs_validfile_linux },
#undef N
};
static int nproc_root_targets =
    sizeof(proc_root_targets) / sizeof(proc_root_targets[0]);

static pid_t atopid(const char *, u_int);

/*
 * Prototypes for procfs vnode ops
 */
int	procfs_badop(void *);

int	procfs_lookup(void *);
#define	procfs_create	procfs_badop
#define	procfs_mknod	procfs_badop
int	procfs_open(void *);
int	procfs_close(void *);
int	procfs_access(void *);
int	procfs_getattr(void *);
int	procfs_setattr(void *);
#define	procfs_read	procfs_rw
#define	procfs_write	procfs_rw
int	procfs_ioctl(void *);
#define	procfs_fsync	procfs_badop
#define	procfs_remove	procfs_badop
int	procfs_link(void *);
#define	procfs_rename	procfs_badop
#define	procfs_mkdir	procfs_badop
#define	procfs_rmdir	procfs_badop
int	procfs_symlink(void *);
int	procfs_readdir(void *);
int	procfs_readlink(void *);
int	procfs_inactive(void *);
int	procfs_reclaim(void *);
#define	procfs_lock	nullop
#define	procfs_unlock	nullop
int	procfs_bmap(void *);
#define	procfs_strategy	procfs_badop
int	procfs_print(void *);
int	procfs_pathconf(void *);
#define	procfs_islocked	nullop
#define	procfs_advlock	procfs_badop

static pid_t atopid(const char *, u_int);

/*
 * procfs vnode operations.
 */
int (**procfs_vnodeop_p)(void *);
struct vnodeopv_entry_desc procfs_vnodeop_entries[] = {
	{ &vop_default_desc, vn_default_error },
	{ &vop_lookup_desc, procfs_lookup },		/* lookup */
	{ &vop_create_desc, procfs_create },		/* create */
	{ &vop_mknod_desc, procfs_mknod },		/* mknod */
	{ &vop_open_desc, procfs_open },		/* open */
	{ &vop_close_desc, procfs_close },		/* close */
	{ &vop_access_desc, procfs_access },		/* access */
	{ &vop_getattr_desc, procfs_getattr },		/* getattr */
	{ &vop_setattr_desc, procfs_setattr },		/* setattr */
	{ &vop_read_desc, procfs_read },		/* read */
	{ &vop_write_desc, procfs_write },		/* write */
	{ &vop_ioctl_desc, procfs_ioctl },		/* ioctl */
	{ &vop_poll_desc, procfs_poll },		/* poll */
	{ &vop_fsync_desc, procfs_fsync },		/* fsync */
	{ &vop_remove_desc, procfs_remove },		/* remove */
	{ &vop_link_desc, procfs_link },		/* link */
	{ &vop_rename_desc, procfs_rename },		/* rename */
	{ &vop_mkdir_desc, procfs_mkdir },		/* mkdir */
	{ &vop_rmdir_desc, procfs_rmdir },		/* rmdir */
	{ &vop_symlink_desc, procfs_symlink },		/* symlink */
	{ &vop_readdir_desc, procfs_readdir },		/* readdir */
	{ &vop_readlink_desc, procfs_readlink },	/* readlink */
	{ &vop_abortop_desc, vop_generic_abortop },	/* abortop */
	{ &vop_inactive_desc, procfs_inactive },	/* inactive */
	{ &vop_reclaim_desc, procfs_reclaim },		/* reclaim */
	{ &vop_lock_desc, procfs_lock },		/* lock */
	{ &vop_unlock_desc, procfs_unlock },		/* unlock */
	{ &vop_bmap_desc, procfs_bmap },		/* bmap */
	{ &vop_strategy_desc, procfs_strategy },	/* strategy */
	{ &vop_print_desc, procfs_print },		/* print */
	{ &vop_islocked_desc, procfs_islocked },	/* islocked */
	{ &vop_pathconf_desc, procfs_pathconf },	/* pathconf */
	{ &vop_advlock_desc, procfs_advlock },		/* advlock */
	{ NULL, NULL }
};
struct vnodeopv_desc procfs_vnodeop_opv_desc =
	{ &procfs_vnodeop_p, procfs_vnodeop_entries };
/*
 * set things up for doing i/o on
 * the pfsnode (vp).  (vp) is locked
 * on entry, and should be left locked
 * on exit.
 *
 * for procfs we don't need to do anything
 * in particular for i/o.  all that is done
 * is to support exclusive open on process
 * memory images.
 */
int
procfs_open(v)
	void *v;
{
	struct vop_open_args *ap = v;
	struct pfsnode *pfs = VTOPFS(ap->a_vp);
	struct proc *p1 = ap->a_p;	/* tracer */
	struct proc *p2;		/* traced */
	int error;

	if ((p2 = pfind(pfs->pfs_pid)) == 0)
		return (ENOENT);	/* was ESRCH, jsp */

	switch (pfs->pfs_type) {
	case Pmem:
		if (((pfs->pfs_flags & FWRITE) && (ap->a_mode & O_EXCL)) ||
		    ((pfs->pfs_flags & O_EXCL) && (ap->a_mode & FWRITE)))
			return (EBUSY);

		if ((error = process_checkioperm(p1, p2)) != 0)
			return (error);

		if (ap->a_mode & FWRITE)
			pfs->pfs_flags = ap->a_mode & (FWRITE|O_EXCL);

		return (0);

	default:
		break;
	}

	return (0);
}

/*
 * close the pfsnode (vp) after doing i/o.
 * (vp) is not locked on entry or exit.
 *
 * nothing to do for procfs other than undo
 * any exclusive open flag (see _open above).
 */
int
procfs_close(v)
	void *v;
{
	struct vop_close_args *ap = v;
	struct pfsnode *pfs = VTOPFS(ap->a_vp);

	switch (pfs->pfs_type) {
	case Pmem:
		if ((ap->a_fflag & FWRITE) && (pfs->pfs_flags & O_EXCL))
			pfs->pfs_flags &= ~(FWRITE|O_EXCL);
		break;
	case Pctl:
	case Pstatus:
	case Pnotepg:
	case Pnote:
	case Proot:
	case Pcurproc:
	case Pself:
	case Pproc:
	case Pfile:
	case Pregs:
	case Pfpregs:
	case Pcmdline:
	case Pmeminfo:
	case Pcpuinfo:
		break;
	}

	return (0);
}

/*
 * do an ioctl operation on pfsnode (vp).
 * (vp) is not locked on entry or exit.
 */
/*ARGSUSED*/
int
procfs_ioctl(v)
	void *v;
{

	return (ENOTTY);
}

/*
 * do block mapping for pfsnode (vp).
 * since we don't use the buffer cache
 * for procfs this function should never
 * be called.  in any case, it's not clear
 * what part of the kernel ever makes use
 * of this function.  for sanity, this is the
 * usual no-op bmap, although returning
 * (EIO) would be a reasonable alternative.
 */
int
procfs_bmap(v)
	void *v;
{
	struct vop_bmap_args *ap = v;

	if (ap->a_vpp != NULL)
		*ap->a_vpp = ap->a_vp;
	if (ap->a_bnp != NULL)
		*ap->a_bnp = ap->a_bn;
	return (0);
}

/*
 * _inactive is called when the pfsnode
 * is vrele'd and the reference count goes
 * to zero.  (vp) will be on the vnode free
 * list, so to get it back vget() must be
 * used.
 *
 * for procfs, check if the process is still
 * alive and if it isn't then just throw away
 * the vnode by calling vgone().  this may
 * be overkill and a waste of time since the
 * chances are that the process will still be
 * there and pfind is not free.
 *
 * (vp) is not locked on entry or exit.
 */
int
procfs_inactive(v)
	void *v;
{
	struct vop_inactive_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct pfsnode *pfs = VTOPFS(vp);

	if (pfind(pfs->pfs_pid) == NULL && !(vp->v_flag & VXLOCK))
		vgone(vp);

	return (0);
}

/*
 * _reclaim is called when getnewvnode()
 * wants to make use of an entry on the vnode
 * free list.  at this time the filesystem needs
 * to free any private data and remove the node
 * from any private lists.
 */
int
procfs_reclaim(v)
	void *v;
{
	struct vop_reclaim_args *ap = v;

	return (procfs_freevp(ap->a_vp));
}

/*
 * Return POSIX pathconf information applicable to special devices.
 */
int
procfs_pathconf(v)
	void *v;
{
	struct vop_pathconf_args *ap = v;

	switch (ap->a_name) {
	case _PC_LINK_MAX:
		*ap->a_retval = LINK_MAX;
		return (0);
	case _PC_MAX_CANON:
		*ap->a_retval = MAX_CANON;
		return (0);
	case _PC_MAX_INPUT:
		*ap->a_retval = MAX_INPUT;
		return (0);
	case _PC_PIPE_BUF:
		*ap->a_retval = PIPE_BUF;
		return (0);
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		return (0);
	case _PC_VDISABLE:
		*ap->a_retval = _POSIX_VDISABLE;
		return (0);
	default:
		return (EINVAL);
	}
	/* NOTREACHED */
}

/*
 * _print is used for debugging.
 * just print a readable description
 * of (vp).
 */
int
procfs_print(v)
	void *v;
{
	struct vop_print_args *ap = v;
	struct pfsnode *pfs = VTOPFS(ap->a_vp);

	printf("tag VT_PROCFS, type %d, pid %d, mode %x, flags %lx\n",
	    pfs->pfs_type, pfs->pfs_pid, pfs->pfs_mode, pfs->pfs_flags);
	return 0;
}

int
procfs_link(v)
	void *v;
{
	struct vop_link_args *ap = v;

	VOP_ABORTOP(ap->a_dvp, ap->a_cnp);
	vput(ap->a_dvp);
	return (EROFS);
}

int
procfs_symlink(v)
	void *v;
{
	struct vop_symlink_args *ap = v;

	VOP_ABORTOP(ap->a_dvp, ap->a_cnp);
	vput(ap->a_dvp);
	return (EROFS);
}


/*
 * generic entry point for unsupported operations
 */
/*ARGSUSED*/
int
procfs_badop(v)
	void *v;
{

	return (EIO);
}

/*
 * Invent attributes for pfsnode (vp) and store
 * them in (vap).
 * Directories lengths are returned as zero since
 * any real length would require the genuine size
 * to be computed, and nothing cares anyway.
 *
 * this is relatively minimal for procfs.
 */
int
procfs_getattr(v)
	void *v;
{
	struct vop_getattr_args *ap = v;
	struct pfsnode *pfs = VTOPFS(ap->a_vp);
	struct vattr *vap = ap->a_vap;
	struct proc *procp;
	int error;

	/* first check the process still exists */
	switch (pfs->pfs_type) {
	case Proot:
	case Pcurproc:
	case Pcpuinfo:
	case Pmeminfo:
		procp = 0;
		break;

	default:
		procp = pfind(pfs->pfs_pid);
		if (procp == 0)
			return (ENOENT);
	}

	error = 0;

	/* start by zeroing out the attributes */
	VATTR_NULL(vap);

	/* next do all the common fields */
	vap->va_type = ap->a_vp->v_type;
	vap->va_mode = pfs->pfs_mode;
	vap->va_fileid = pfs->pfs_fileno;
	vap->va_flags = 0;
	vap->va_blocksize = PAGE_SIZE;
	vap->va_bytes = vap->va_size = 0;

	/*
	 * Make all times be current TOD.
	 * It would be possible to get the process start
	 * time from the p_stat structure, but there's
	 * no "file creation" time stamp anyway, and the
	 * p_stat structure is not addressible if u. gets
	 * swapped out for that process.
	 */
	getnanotime(&vap->va_ctime);
	vap->va_atime = vap->va_mtime = vap->va_ctime;

	switch (pfs->pfs_type) {
	case Pregs:
	case Pfpregs:
#ifndef PTRACE
		break;
#endif
	case Pmem:
		/*
		 * If the process has exercised some setuid or setgid
		 * privilege, then rip away read/write permission so
		 * that only root can gain access.
		 */
		if (procp->p_flag & P_SUGID)
			vap->va_mode &= ~(S_IRUSR|S_IWUSR);
		/* FALLTHROUGH */
	case Pctl:
	case Pstatus:
	case Pnote:
	case Pnotepg:
	case Pcmdline:
		vap->va_nlink = 1;
		vap->va_uid = procp->p_ucred->cr_uid;
		vap->va_gid = procp->p_ucred->cr_gid;
		break;
	case Pmeminfo:
	case Pcpuinfo:
		vap->va_nlink = 1;
		vap->va_uid = vap->va_gid = 0;
		break;
	case Pproc:
	case Pfile:
	case Proot:
	case Pcurproc:
	case Pself:
		break;
	}

	/*
	 * now do the object specific fields
	 *
	 * The size could be set from struct reg, but it's hardly
	 * worth the trouble, and it puts some (potentially) machine
	 * dependent data into this machine-independent code.  If it
	 * becomes important then this function should break out into
	 * a per-file stat function in the corresponding .c file.
	 */

	switch (pfs->pfs_type) {
	case Proot:
		/*
		 * Set nlink to 1 to tell fts(3) we don't actually know.
		 */
		vap->va_nlink = 1;
		vap->va_uid = 0;
		vap->va_gid = 0;
		vap->va_size = vap->va_bytes = DEV_BSIZE;
		break;

	case Pcurproc: {
		char buf[16];		/* should be enough */
		int len;

		len = snprintf(buf, sizeof buf, "%ld", (long)curproc->p_pid);
		if (len == -1 || len >= sizeof buf) {
			error = EINVAL;
			break;
		}
		vap->va_nlink = 1;
		vap->va_uid = 0;
		vap->va_gid = 0;
		vap->va_size = vap->va_bytes = len;
		break;
	}

	case Pself:
		vap->va_nlink = 1;
		vap->va_uid = 0;
		vap->va_gid = 0;
		vap->va_size = vap->va_bytes = sizeof("curproc");
		break;

	case Pproc:
		vap->va_nlink = 2;
		vap->va_uid = procp->p_ucred->cr_uid;
		vap->va_gid = procp->p_ucred->cr_gid;
		vap->va_size = vap->va_bytes = DEV_BSIZE;
		break;

	case Pfile:
		error = EOPNOTSUPP;
		break;

	case Pmem:
		vap->va_bytes = vap->va_size =
			ctob(procp->p_vmspace->vm_tsize +
				    procp->p_vmspace->vm_dsize +
				    procp->p_vmspace->vm_ssize);
		break;

	case Pregs:
#ifdef PTRACE
		vap->va_bytes = vap->va_size = sizeof(struct reg);
#endif
		break;

	case Pfpregs:
#if defined(PT_GETFPREGS) || defined(PT_SETFPREGS)
#ifdef PTRACE
		vap->va_bytes = vap->va_size = sizeof(struct fpreg);
#endif
#endif
		break;

	case Pctl:
	case Pstatus:
	case Pnote:
	case Pnotepg:
	case Pcmdline:
	case Pmeminfo:
	case Pcpuinfo:
		vap->va_bytes = vap->va_size = 0;
		break;

#ifdef DIAGNOSTIC
	default:
		panic("procfs_getattr");
#endif
	}

	return (error);
}

/*ARGSUSED*/
int
procfs_setattr(v)
	void *v;
{
	/*
	 * just fake out attribute setting
	 * it's not good to generate an error
	 * return, otherwise things like creat()
	 * will fail when they try to set the
	 * file length to 0.  worse, this means
	 * that echo $note > /proc/$pid/note will fail.
	 */

	return (0);
}

/*
 * implement access checking.
 *
 * actually, the check for super-user is slightly
 * broken since it will allow read access to write-only
 * objects.  this doesn't cause any particular trouble
 * but does mean that the i/o entry points need to check
 * that the operation really does make sense.
 */
int
procfs_access(v)
	void *v;
{
	struct vop_access_args *ap = v;
	struct vattr va;
	int error;

	if ((error = VOP_GETATTR(ap->a_vp, &va, ap->a_cred, ap->a_p)) != 0)
		return (error);

	return (vaccess(va.va_mode, va.va_uid, va.va_gid, ap->a_mode,
			ap->a_cred));
}

/*
 * lookup.  this is incredibly complicated in the
 * general case, however for most pseudo-filesystems
 * very little needs to be done.
 *
 * unless you want to get a migraine, just make sure your
 * filesystem doesn't do any locking of its own.  otherwise
 * read and inwardly digest ufs_lookup().
 */
int
procfs_lookup(v)
	void *v;
{
	struct vop_lookup_args *ap = v;
	struct componentname *cnp = ap->a_cnp;
	struct vnode **vpp = ap->a_vpp;
	struct vnode *dvp = ap->a_dvp;
	char *pname = cnp->cn_nameptr;
	struct proc *curp = curproc;
	struct proc_target *pt;
	struct vnode *fvp;
	pid_t pid;
	struct pfsnode *pfs;
	struct proc *p = NULL;
	int i, error, wantpunlock, iscurproc = 0, isself = 0;

	*vpp = NULL;
	cnp->cn_flags &= ~PDIRUNLOCK;

	if (cnp->cn_nameiop == DELETE || cnp->cn_nameiop == RENAME)
		return (EROFS);

	if (cnp->cn_namelen == 1 && *pname == '.') {
		*vpp = dvp;
		VREF(dvp);
		return (0);
	}

	wantpunlock = (~cnp->cn_flags & (LOCKPARENT | ISLASTCN));
	pfs = VTOPFS(dvp);
	switch (pfs->pfs_type) {
	case Proot:
		if (cnp->cn_flags & ISDOTDOT)
			return (EIO);

		iscurproc = CNEQ(cnp, "curproc", 7);
		isself = CNEQ(cnp, "self", 4);

		if (iscurproc || isself) {
			error = procfs_allocvp(dvp->v_mount, vpp, 0,
			    iscurproc ? Pcurproc : Pself);
			if ((error == 0) && (wantpunlock)) {
				VOP_UNLOCK(dvp, 0, curp);
				cnp->cn_flags |= PDIRUNLOCK;
			}
			return (error);
		}

		for (i = 0; i < nproc_root_targets; i++) {
			pt = &proc_root_targets[i];
			if (cnp->cn_namelen == pt->pt_namlen &&
			    memcmp(pt->pt_name, pname, cnp->cn_namelen) == 0 &&
			    (pt->pt_valid == NULL ||
			     (*pt->pt_valid)(p, dvp->v_mount)))
				break;
		}

		if (i != nproc_root_targets) {
			error = procfs_allocvp(dvp->v_mount, vpp, 0,
			    pt->pt_pfstype);
			if ((error == 0) && (wantpunlock)) {
				VOP_UNLOCK(dvp, 0, curp);
				cnp->cn_flags |= PDIRUNLOCK;
			}
			return (error);
		}

		pid = atopid(pname, cnp->cn_namelen);
		if (pid == NO_PID)
			break;

		p = pfind(pid);
		if (p == 0)
			break;

		error = procfs_allocvp(dvp->v_mount, vpp, pid, Pproc);
		if ((error == 0) && wantpunlock) {
			VOP_UNLOCK(dvp, 0, curp);
			cnp->cn_flags |= PDIRUNLOCK;
		}
		return (error);

	case Pproc:
		/*
		 * do the .. dance. We unlock the directory, and then
		 * get the root dir. That will automatically return ..
		 * locked. Then if the caller wanted dvp locked, we
		 * re-lock.
		 */
		if (cnp->cn_flags & ISDOTDOT) {
			VOP_UNLOCK(dvp, 0, p);
			cnp->cn_flags |= PDIRUNLOCK;
			error = procfs_root(dvp->v_mount, vpp);
			if ((error == 0) && (wantpunlock == 0) &&
			    ((error = vn_lock(dvp, LK_EXCLUSIVE, curp)) == 0))
				cnp->cn_flags &= ~PDIRUNLOCK;
			return (error);
		}

		p = pfind(pfs->pfs_pid);
		if (p == 0)
			break;

		for (pt = proc_targets, i = 0; i < nproc_targets; pt++, i++) {
			if (cnp->cn_namelen == pt->pt_namlen &&
			    bcmp(pt->pt_name, pname, cnp->cn_namelen) == 0 &&
			    (pt->pt_valid == NULL ||
			     (*pt->pt_valid)(p, dvp->v_mount)))
				goto found;
		}
		break;

	found:
		if (pt->pt_pfstype == Pfile) {
			fvp = p->p_textvp;
			/* We already checked that it exists. */
			VREF(fvp);
			vn_lock(fvp, LK_EXCLUSIVE | LK_RETRY, curp);
			if (wantpunlock) {
				VOP_UNLOCK(dvp, 0, curp);
				cnp->cn_flags |= PDIRUNLOCK;
			}
			*vpp = fvp;
			return (0);
		}

		error =  procfs_allocvp(dvp->v_mount, vpp, pfs->pfs_pid,
		    pt->pt_pfstype);
		if ((error == 0) && (wantpunlock)) {
			VOP_UNLOCK(dvp, 0, curp);
			cnp->cn_flags |= PDIRUNLOCK;
		}
		return (error);

	default:
		return (ENOTDIR);
	}

	return (cnp->cn_nameiop == LOOKUP ? ENOENT : EROFS);
}

int
procfs_validfile(p, mp)
	struct proc *p;
	struct mount *mp;
{

	return (p->p_textvp != NULLVP);
}

int
procfs_validfile_linux(p, mp)
	struct proc *p;
	struct mount *mp;
{
	int flags;

	flags = VFSTOPROC(mp)->pmnt_flags;
	return ((flags & PROCFSMNT_LINUXCOMPAT) &&
	    (p == NULL || procfs_validfile(p, mp)));
}

/*
 * readdir returns directory entries from pfsnode (vp).
 *
 * the strategy here with procfs is to generate a single
 * directory entry at a time (struct dirent) and then
 * copy that out to userland using uiomove.  a more efficent
 * though more complex implementation, would try to minimize
 * the number of calls to uiomove().  for procfs, this is
 * hardly worth the added code complexity.
 *
 * this should just be done through read()
 */
int
procfs_readdir(v)
	void *v;
{
	struct vop_readdir_args *ap = v;
	struct uio *uio = ap->a_uio;
	struct dirent d;
	struct pfsnode *pfs;
	struct vnode *vp;
	int i;
	int error;

	vp = ap->a_vp;
	pfs = VTOPFS(vp);

	if (uio->uio_resid < UIO_MX)
		return (EINVAL);

	error = 0;
	i = uio->uio_offset;
	if (i < 0)
		return (EINVAL);
	bzero(&d, UIO_MX);
	d.d_reclen = UIO_MX;

	switch (pfs->pfs_type) {
	/*
	 * this is for the process-specific sub-directories.
	 * all that is needed to is copy out all the entries
	 * from the procent[] table (top of this file).
	 */
	case Pproc: {
		struct proc *p;
		struct proc_target *pt;

		p = pfind(pfs->pfs_pid);
		if (p == NULL)
			break;

		for (pt = &proc_targets[i];
		     uio->uio_resid >= UIO_MX && i < nproc_targets; pt++, i++) {
			if (pt->pt_valid &&
			    (*pt->pt_valid)(p, vp->v_mount) == 0)
				continue;
			
			d.d_fileno = PROCFS_FILENO(pfs->pfs_pid, pt->pt_pfstype);
			d.d_namlen = pt->pt_namlen;
			bcopy(pt->pt_name, d.d_name, pt->pt_namlen + 1);
			d.d_type = pt->pt_type;

			if ((error = uiomove(&d, UIO_MX, uio)) != 0)
				break;
		}

	    	break;
	}

	/*
	 * this is for the root of the procfs filesystem
	 * what is needed is a special entry for "curproc"
	 * followed by an entry for each process on allproc
#ifdef PROCFS_ZOMBIE
	 * and zombproc.
#endif
	 */

	case Proot: {
#ifdef PROCFS_ZOMBIE
		int doingzomb = 0;
#endif
		int pcnt = i;
		volatile struct proc *p = LIST_FIRST(&allproc);

		if (pcnt > 3)
			pcnt = 3;
#ifdef PROCFS_ZOMBIE
	again:
#endif
		for (; p && uio->uio_resid >= UIO_MX; i++, pcnt++) {
			switch (i) {
			case 0:		/* `.' */
			case 1:		/* `..' */
				d.d_fileno = PROCFS_FILENO(0, Proot);
				d.d_namlen = i + 1;
				bcopy("..", d.d_name, d.d_namlen);
				d.d_name[i + 1] = '\0';
				d.d_type = DT_DIR;
				break;

			case 2:
				d.d_fileno = PROCFS_FILENO(0, Pcurproc);
				d.d_namlen = 7;
				bcopy("curproc", d.d_name, 8);
				d.d_type = DT_LNK;
				break;

			case 3:
				d.d_fileno = PROCFS_FILENO(0, Pself);
				d.d_namlen = 4;
				bcopy("self", d.d_name, 5);
				d.d_type = DT_LNK;
				break;

			case 4:
				if (VFSTOPROC(vp->v_mount)->pmnt_flags &
				    PROCFSMNT_LINUXCOMPAT) {
					d.d_fileno = PROCFS_FILENO(0, Pcpuinfo);
					d.d_namlen = 7;
					bcopy("cpuinfo", d.d_name, 8);
					d.d_type = DT_REG;
					break;
				}
				/* fall through */

			case 5:
				if (VFSTOPROC(vp->v_mount)->pmnt_flags &
				    PROCFSMNT_LINUXCOMPAT) {
					d.d_fileno = PROCFS_FILENO(0, Pmeminfo);
					d.d_namlen = 7;
					bcopy("meminfo", d.d_name, 8);
					d.d_type = DT_REG;
					break;
				}
				/* fall through */

			default:
				while (pcnt < i) {
					pcnt++;
					p = LIST_NEXT(p, p_list);
					if (!p)
						goto done;
				}
				d.d_fileno = PROCFS_FILENO(p->p_pid, Pproc);
				d.d_namlen = snprintf(d.d_name, sizeof(d.d_name),
				    "%ld", (long)p->p_pid);
				d.d_type = DT_REG;
				p = LIST_NEXT(p, p_list);
				break;
			}

			if ((error = uiomove(&d, UIO_MX, uio)) != 0)
				break;
		}
	done:

#ifdef PROCFS_ZOMBIE
		if (p == 0 && doingzomb == 0) {
			doingzomb = 1;
			p = LIST_FIRST(&zombproc);
			goto again;
		}
#endif

		break;

	}

	default:
		error = ENOTDIR;
		break;
	}

	uio->uio_offset = i;
	return (error);
}

/*
 * readlink reads the link of `curproc'
 */
int
procfs_readlink(v)
	void *v;
{
	struct vop_readlink_args *ap = v;
	char buf[16];		/* should be enough */
	int len;

	if (VTOPFS(ap->a_vp)->pfs_fileno == PROCFS_FILENO(0, Pcurproc))
		len = snprintf(buf, sizeof buf, "%ld", (long)curproc->p_pid);
	else if (VTOPFS(ap->a_vp)->pfs_fileno == PROCFS_FILENO(0, Pself))
		len = strlcpy(buf, "curproc", sizeof buf);
	else
		return (EINVAL);
	if (len == -1 || len >= sizeof buf)
		return (EINVAL);

	return (uiomove(buf, len, ap->a_uio));
}

/*
 * convert decimal ascii to pid_t
 */
static pid_t
atopid(b, len)
	const char *b;
	u_int len;
{
	pid_t p = 0;

	while (len--) {
		char c = *b++;
		if (c < '0' || c > '9')
			return (NO_PID);
		p = 10 * p + (c - '0');
		if (p > PID_MAX)
			return (NO_PID);
	}

	return (p);
}
int
procfs_poll(v)
	void *v;
{
	struct vop_poll_args *ap = v;

	return (ap->a_events & (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM));
}
