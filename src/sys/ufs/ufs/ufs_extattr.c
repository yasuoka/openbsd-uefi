/*	$OpenBSD: ufs_extattr.c,v 1.1 2002/02/22 20:37:46 drahn Exp $ */
/*-
 * Copyright (c) 1999, 2000, 2001, 2002 Robert N. M. Watson
 * Copyright (c) 2002 Networks Associates Technologies, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
 *
 * This software was developed for the FreeBSD Project in part by NAI Labs,
 * the Security Research Division of Network Associates, Inc. under
 * DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA
 * CHATS research program.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the authors may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD: ufs_extattr.c,v 1.44 2002/02/10 04:57:08 rwatson Exp $
 */
/*
 * Developed by the TrustedBSD Project.
 * Support for file system extended attribute: UFS-specific support functions.
 */


#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/namei.h>
#include <sys/malloc.h>
#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/lock.h>
#include <sys/dirent.h>
#include <sys/extattr.h>
#include <sys/sysctl.h>

#include <ufs/ufs/dir.h>
#include <ufs/ufs/extattr.h>
#include <ufs/ufs/quota.h>
#include <ufs/ufs/ufsmount.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufs_extern.h>

#ifdef UFS_EXTATTR
#ifdef __OpenBSD__
#define static 
#endif
#ifndef LK_NOPAUSE 
#define LK_NOPAUSE 0
#endif /* LK_NOPAUSE */

#define	MIN(a,b) (((a)<(b))?(a):(b))

/* -XXX
static MALLOC_DEFINE(M_UFS_EXTATTR, "ufs_extattr", "ufs extended attribute");
*/

static int ufs_extattr_sync = 0;
/* -XXX
SYSCTL_INT(_debug, OID_AUTO, ufs_extattr_sync, CTLFLAG_RW, &ufs_extattr_sync,
    0, "");
*/

static int	ufs_extattr_valid_attrname(int attrnamespace,
    const char *attrname);
static int	ufs_extattr_credcheck(struct vnode *vp,
    struct ufs_extattr_list_entry *uele, struct ucred *cred, struct proc *p,
    int access);
static int	ufs_extattr_enable_with_open(struct ufsmount *ump,
    struct vnode *vp, int attrnamespace, const char *attrname, struct proc *p);
static int	ufs_extattr_enable(struct ufsmount *ump, int attrnamespace,
    const char *attrname, struct vnode *backing_vnode, struct proc *p);
static int	ufs_extattr_disable(struct ufsmount *ump, int attrnamespace,
    const char *attrname, struct proc *p);
static int	ufs_extattr_get(struct vnode *vp, int attrnamespace,
    const char *name, struct uio *uio, size_t *size, struct ucred *cred,
    struct proc *p);
static int	ufs_extattr_set(struct vnode *vp, int attrnamespace,
    const char *name, struct uio *uio, struct ucred *cred, struct proc *p);
static int	ufs_extattr_rm(struct vnode *vp, int attrnamespace,
    const char *name, struct ucred *cred, struct proc *p);

static void ufs_extattr_uepm_lock(struct ufsmount *ump, struct proc *p);
static void ufs_extattr_uepm_unlock(struct ufsmount *ump, struct proc *p);
static struct ufs_extattr_list_entry *
ufs_extattr_find_attr(struct ufsmount *ump, int attrnamespace,
	const char *attrname);
/*
 * Per-FS attribute lock protecting attribute operations.
 * XXX Right now there is a lot of lock contention due to having a single
 * lock per-FS; really, this should be far more fine-grained.
 */
static void
ufs_extattr_uepm_lock(struct ufsmount *ump, struct proc *p)
{

	/* Ideally, LK_CANRECURSE would not be used, here. */
	lockmgr(&ump->um_extattr.uepm_lock, LK_EXCLUSIVE | LK_RETRY |
	    LK_CANRECURSE, 0, p);
}

static void
ufs_extattr_uepm_unlock(struct ufsmount *ump, struct proc *p)
{

	lockmgr(&ump->um_extattr.uepm_lock, LK_RELEASE, 0, p);
}

/*
 * Determine whether the name passed is a valid name for an actual
 * attribute.
 *
 * Invalid currently consists of:
 *	 NULL pointer for attrname
 *	 zero-length attrname (used to retrieve application attribute list)
 */
static int
ufs_extattr_valid_attrname(int attrnamespace, const char *attrname)
{

	if (attrname == NULL)
		return (0);
	if (strlen(attrname) == 0)
		return (0);
	return (1);
}

/*
 * Locate an attribute given a name and mountpoint.
 * Must be holding uepm lock for the mount point.
 */
static struct ufs_extattr_list_entry *
ufs_extattr_find_attr(struct ufsmount *ump, int attrnamespace,
    const char *attrname)
{
	struct ufs_extattr_list_entry	*search_attribute;

	for (search_attribute = LIST_FIRST(&ump->um_extattr.uepm_list);
	    search_attribute;
	    search_attribute = LIST_NEXT(search_attribute, uele_entries)) {
		if (!(strncmp(attrname, search_attribute->uele_attrname,
		    UFS_EXTATTR_MAXEXTATTRNAME)) &&
		    (attrnamespace == search_attribute->uele_attrnamespace)) {
			return (search_attribute);
		}
	}

	return (0);
}

/*
 * Initialize per-FS structures supporting extended attributes.  Do not
 * start extended attributes yet.
 */
void
ufs_extattr_uepm_init(struct ufs_extattr_per_mount *uepm)
{

	uepm->uepm_flags = 0;

	LIST_INIT(&uepm->uepm_list);
	/* XXX is PVFS right, here? */
	lockinit(&uepm->uepm_lock, PVFS, "extattr", 0, 0);
	uepm->uepm_flags |= UFS_EXTATTR_UEPM_INITIALIZED;
}

/*
 * Destroy per-FS structures supporting extended attributes.  Assumes
 * that EAs have already been stopped, and will panic if not.
 */
void
ufs_extattr_uepm_destroy(struct ufs_extattr_per_mount *uepm)
{

	if (!(uepm->uepm_flags & UFS_EXTATTR_UEPM_INITIALIZED))
		panic("ufs_extattr_uepm_destroy: not initialized");

	if ((uepm->uepm_flags & UFS_EXTATTR_UEPM_STARTED))
		panic("ufs_extattr_uepm_destroy: called while still started");

	/*
	 * It's not clear that either order for the next two lines is
	 * ideal, and it should never be a problem if this is only called
	 * during unmount, and with vfs_busy().
	 */
	uepm->uepm_flags &= ~UFS_EXTATTR_UEPM_INITIALIZED;
	/* - XXX
	lockdestroy(&uepm->uepm_lock);
	*/
}

/*
 * Start extended attribute support on an FS.
 */
int
ufs_extattr_start(struct mount *mp, struct proc *p)
{
	struct ufsmount	*ump;
	int	error = 0;

	ump = VFSTOUFS(mp);

	ufs_extattr_uepm_lock(ump, p);

	if (!(ump->um_extattr.uepm_flags & UFS_EXTATTR_UEPM_INITIALIZED)) {
		error = EOPNOTSUPP;
		goto unlock;
	}
	if (ump->um_extattr.uepm_flags & UFS_EXTATTR_UEPM_STARTED) {
		error = EBUSY;
		goto unlock;
	}

	ump->um_extattr.uepm_flags |= UFS_EXTATTR_UEPM_STARTED;

	crhold(p->p_ucred);
	ump->um_extattr.uepm_ucred = p->p_ucred;

unlock:
	ufs_extattr_uepm_unlock(ump, p);

	return (error);
}

#ifdef UFS_EXTATTR_AUTOSTART
static int
ufs_extattr_lookup(struct vnode *start_dvp, int lockparent, char *dirname,
    struct vnode **vp, struct proc *p);
/*
 * Helper routine: given a locked parent directory and filename, return
 * the locked vnode of the inode associated with the name.  Will not
 * follow symlinks, may return any type of vnode.  Lock on parent will
 * be released even in the event of a failure.  In the event that the
 * target is the parent (i.e., "."), there will be two references and
 * one lock, requiring the caller to possibly special-case.
 */
#define	UE_GETDIR_LOCKPARENT	1
#define	UE_GETDIR_LOCKPARENT_DONT	2
static int
ufs_extattr_lookup(struct vnode *start_dvp, int lockparent, char *dirname,
    struct vnode **vp, struct proc *p)
{
	struct vop_lookup_args vargs;
	struct componentname cnp;
	struct vnode *target_vp;
	int error;

	bzero(&cnp, sizeof(cnp));
	cnp.cn_nameiop = LOOKUP;
	cnp.cn_flags = ISLASTCN;
	if (lockparent == UE_GETDIR_LOCKPARENT)
		cnp.cn_flags |= LOCKPARENT;
	cnp.cn_proc = p;
	cnp.cn_cred = p->p_ucred;
	MALLOC(cnp.cn_pnbuf, char *, MAXPATHLEN, M_NAMEI, M_WAITOK);

	cnp.cn_nameptr = cnp.cn_pnbuf;
	error = copystr(dirname, cnp.cn_pnbuf, MAXPATHLEN,
	    (size_t *) &cnp.cn_namelen);
	if (error) {
		if (lockparent == UE_GETDIR_LOCKPARENT_DONT) {
			VOP_UNLOCK(start_dvp, 0, p);
		}
		FREE(cnp.cn_pnbuf, M_NAMEI);
		printf("ufs_extattr_lookup: copystr failed\n");
		return (error);
	}
	cnp.cn_namelen--;	/* trim nul termination */
	vargs.a_desc = NULL;
	vargs.a_dvp = start_dvp;
	vargs.a_vpp = &target_vp;
	vargs.a_cnp = &cnp;
	error = ufs_lookup(&vargs);
	FREE(cnp.cn_pnbuf, M_NAMEI);

	if (error) {
#if 0
	/* -XXX does OpenBSD ufs_lookup always unlock on error? */
		/*
		 * Error condition, may have to release the lock on the parent
		 * if ufs_lookup() didn't.
		 */
		if (!(cnp.cn_flags & PDIRUNLOCK) &&
		    (lockparent == UE_GETDIR_LOCKPARENT_DONT))
			VOP_UNLOCK(start_dvp, 0, p);

		/*
		 * Check that ufs_lookup() didn't release the lock when we
		 * didn't want it to.
		 */
		if ((cnp.cn_flags & PDIRUNLOCK) &&
		    (lockparent == UE_GETDIR_LOCKPARENT))
			panic("ufs_extattr_lookup: lockparent but PDIRUNLOCK");
#endif

		return (error);
	}
/*
	if (target_vp == start_dvp)
		panic("ufs_extattr_lookup: target_vp == start_dvp");
*/

#if 0
	/* PDIRUNLOCK does not exist on OpenBSD */
	if (target_vp != start_dvp &&
	    !(cnp.cn_flags & PDIRUNLOCK) &&
	    (lockparent == UE_GETDIR_LOCKPARENT_DONT))
		panic("ufs_extattr_lookup: !lockparent but !PDIRUNLOCK");

	if ((cnp.cn_flags & PDIRUNLOCK) &&
	    (lockparent == UE_GETDIR_LOCKPARENT))
		panic("ufs_extattr_lookup: lockparent but PDIRUNLOCK");
#endif

	/* printf("ufs_extattr_lookup: success\n"); */
	*vp = target_vp;
	return (0);
}
#endif /* !UFS_EXTATTR_AUTOSTART */

/*
 * Enable an EA using the passed file system, backing vnode, attribute name,
 * namespace, and proc.  Will perform a VOP_OPEN() on the vp, so expects vp
 * to be locked when passed in.  The vnode will be returned unlocked,
 * regardless of success/failure of the function.  As a result, the caller
 * will always need to vrele(), but not vput().
 */
static int
ufs_extattr_enable_with_open(struct ufsmount *ump, struct vnode *vp,
    int attrnamespace, const char *attrname, struct proc *p)
{
	int error;

	error = VOP_OPEN(vp, FREAD|FWRITE, p->p_ucred, p);
	if (error) {
		printf("ufs_extattr_enable_with_open.VOP_OPEN(): failed "
		    "with %d\n", error);
		VOP_UNLOCK(vp, 0, p);
		return (error);
	}

#if 0
	/* - XXX */
	/*
	 * XXX: Note, should VOP_CLOSE() if vfs_object_create() fails, but due
	 * to a similar piece of code in vn_open(), we don't.
	 */
	if (vn_canvmio(vp) == TRUE)
		if ((error = vfs_object_create(vp, p, p->p_ucred)) != 0) {
			/*
			 * XXX: bug replicated from vn_open(): should
			 * VOP_CLOSE() here.
			 */
			VOP_UNLOCK(vp, 0, p);
			return (error);
		}
#endif

	vp->v_writecount++;

	vref(vp);

	VOP_UNLOCK(vp, 0, p);

	error = ufs_extattr_enable(ump, attrnamespace, attrname, vp, p);
	if (error != 0)
		vn_close(vp, FREAD|FWRITE, p->p_ucred, p);
	return (error);
}

#ifdef UFS_EXTATTR_AUTOSTART
static int
ufs_extattr_iterate_directory(struct ufsmount *ump, struct vnode *dvp,
    int attrnamespace, struct proc *p);
/*
 * Given a locked directory vnode, iterate over the names in the directory
 * and use ufs_extattr_lookup() to retrieve locked vnodes of potential
 * attribute files.  Then invoke ufs_extattr_enable_with_open() on each
 * to attempt to start the attribute.  Leaves the directory locked on
 * exit.
 */
static int
ufs_extattr_iterate_directory(struct ufsmount *ump, struct vnode *dvp,
    int attrnamespace, struct proc *p)
{
	struct vop_readdir_args vargs;
	struct dirent *dp, *edp;
	struct vnode *attr_vp;
	struct uio auio;
	struct iovec aiov;
	char *dirbuf;
	int error, eofflag = 0;

	if (dvp->v_type != VDIR)
		return (ENOTDIR);

	MALLOC(dirbuf, char *, DIRBLKSIZ, M_TEMP, M_WAITOK);

	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	auio.uio_rw = UIO_READ;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_procp = p;
	auio.uio_offset = 0;

	vargs.a_desc = NULL;
	vargs.a_vp = dvp;
	vargs.a_uio = &auio;
	vargs.a_cred = p->p_ucred;
	vargs.a_eofflag = &eofflag;
	vargs.a_ncookies = NULL;
	vargs.a_cookies = NULL;

	while (!eofflag) {
		auio.uio_resid = DIRBLKSIZ;
		aiov.iov_base = dirbuf;
		aiov.iov_len = DIRBLKSIZ;
		error = ufs_readdir(&vargs);
		if (error) {
			printf("ufs_extattr_iterate_directory: ufs_readdir "
			    "%d\n", error);
			return (error);
		}

		edp = (struct dirent *)&dirbuf[DIRBLKSIZ];
		for (dp = (struct dirent *)dirbuf; dp < edp; ) {
#if (BYTE_ORDER == LITTLE_ENDIAN)
			dp->d_type = dp->d_namlen;
			dp->d_namlen = 0;
#else
			dp->d_type = 0;
#endif
			if (dp->d_reclen == 0)
				break;
			error = ufs_extattr_lookup(dvp, UE_GETDIR_LOCKPARENT,
			    dp->d_name, &attr_vp, p);
			if (error) {
				printf("ufs_extattr_iterate_directory: lookup "
				    "%s %d\n", dp->d_name, error);
			} else if (attr_vp == dvp) {
				vrele(attr_vp);
			} else if (attr_vp->v_type != VREG) {
/*
 * Eventually, this will be uncommented, but in the mean time, the ".."
 * entry causes unnecessary console warnings.
				printf("ufs_extattr_iterate_directory: "
				    "%s not VREG\n", dp->d_name);
*/
				vput(attr_vp);
			} else {
				error = ufs_extattr_enable_with_open(ump,
				    attr_vp, attrnamespace, dp->d_name, p);
				vrele(attr_vp);
				if (error) {
					printf("ufs_extattr_iterate_directory: "
					    "enable %s %d\n", dp->d_name,
					    error);
				} else {
/*
 * While it's nice to have some visual output here, skip for the time-being.
 * Probably should be enabled by -v at boot.
					printf("Autostarted %s\n", dp->d_name);
 */
printf("Autostarted %s\n", dp->d_name); /* XXX - debug*/
				}
			}
			dp = (struct dirent *) ((char *)dp + dp->d_reclen);
			if (dp >= edp)
				break;
		}
	}
	FREE(dirbuf, M_TEMP);
	
	return (0);
}

/*
 * Auto-start of extended attributes, to be executed (optionally) at
 * mount-time.
 */
int
ufs_extattr_autostart(struct mount *mp, struct proc *p)
{
	struct vnode *rvp, *attr_dvp, *attr_system_dvp, *attr_user_dvp;
	int error;

	/*
	 * Does UFS_EXTATTR_FSROOTSUBDIR exist off the file system root?
	 * If so, automatically start EA's.
	 */
	error = VFS_ROOT(mp, &rvp);
	if (error) {
		printf("ufs_extattr_autostart.VFS_ROOT() returned %d\n", error);
		return (error);
	}

	error = ufs_extattr_lookup(rvp, UE_GETDIR_LOCKPARENT_DONT,
	    UFS_EXTATTR_FSROOTSUBDIR, &attr_dvp, p);
	if (error) {
		/* rvp ref'd but now unlocked */
		vrele(rvp);
		return (error);
	}
	if (rvp == attr_dvp) {
		/* Should never happen. */
		vrele(attr_dvp);
		vput(rvp);
		return (EINVAL);
	}
	vrele(rvp);

	if (attr_dvp->v_type != VDIR) {
		printf("ufs_extattr_autostart: %s != VDIR\n",
		    UFS_EXTATTR_FSROOTSUBDIR);
		goto return_vput_attr_dvp;
	}

	error = ufs_extattr_start(mp, p);
	if (error) {
		printf("ufs_extattr_autostart: ufs_extattr_start failed (%d)\n",
		    error);
		goto return_vput_attr_dvp;
	}

	/*
	 * Look for two subdirectories: UFS_EXTATTR_SUBDIR_SYSTEM,
	 * UFS_EXTATTR_SUBDIR_USER.  For each, iterate over the sub-directory,
	 * and start with appropriate type.  Failures in either don't
	 * result in an over-all failure.  attr_dvp is left locked to
	 * be cleaned up on exit.
	 */
	error = ufs_extattr_lookup(attr_dvp, UE_GETDIR_LOCKPARENT,
	    UFS_EXTATTR_SUBDIR_SYSTEM, &attr_system_dvp, p);
	if (!error) {
		error = ufs_extattr_iterate_directory(VFSTOUFS(mp),
		    attr_system_dvp, EXTATTR_NAMESPACE_SYSTEM, p);
		if (error)
			printf("ufs_extattr_iterate_directory returned %d\n",
			    error);
		vput(attr_system_dvp);
	}

	error = ufs_extattr_lookup(attr_dvp, UE_GETDIR_LOCKPARENT,
	    UFS_EXTATTR_SUBDIR_USER, &attr_user_dvp, p);
	if (!error) {
		error = ufs_extattr_iterate_directory(VFSTOUFS(mp),
		    attr_user_dvp, EXTATTR_NAMESPACE_USER, p);
		if (error)
			printf("ufs_extattr_iterate_directory returned %d\n",
			    error);
		vput(attr_user_dvp);
	}

	/* Mask startup failures in sub-directories. */
	error = 0;

return_vput_attr_dvp:
	vput(attr_dvp);

	return (error);
}
#endif /* !UFS_EXTATTR_AUTOSTART */

/*
 * Stop extended attribute support on an FS.
 */
int
ufs_extattr_stop(struct mount *mp, struct proc *p)
{
	struct ufs_extattr_list_entry	*uele;
	struct ufsmount	*ump = VFSTOUFS(mp);
	int	error = 0;

	ufs_extattr_uepm_lock(ump, p);

	if (!(ump->um_extattr.uepm_flags & UFS_EXTATTR_UEPM_STARTED)) {
		error = EOPNOTSUPP;
		goto unlock;
	}

	while (LIST_FIRST(&ump->um_extattr.uepm_list) != NULL) {
		uele = LIST_FIRST(&ump->um_extattr.uepm_list);
		ufs_extattr_disable(ump, uele->uele_attrnamespace,
		    uele->uele_attrname, p);
	}

	ump->um_extattr.uepm_flags &= ~UFS_EXTATTR_UEPM_STARTED;

	crfree(ump->um_extattr.uepm_ucred);
	ump->um_extattr.uepm_ucred = NULL;

unlock:
	ufs_extattr_uepm_unlock(ump, p);

	return (error);
}

/*
 * Enable a named attribute on the specified file system; provide an
 * unlocked backing vnode to hold the attribute data.
 */
static int
ufs_extattr_enable(struct ufsmount *ump, int attrnamespace,
    const char *attrname, struct vnode *backing_vnode, struct proc *p)
{
	struct ufs_extattr_list_entry	*attribute;
	struct iovec	aiov;
	struct uio	auio;
	int	error = 0;

	if (!ufs_extattr_valid_attrname(attrnamespace, attrname))
		return (EINVAL);
	if (backing_vnode->v_type != VREG)
		return (EINVAL);

	MALLOC(attribute, struct ufs_extattr_list_entry *,
	    sizeof(struct ufs_extattr_list_entry), M_UFS_EXTATTR, M_WAITOK);
	if (attribute == NULL)
		return (ENOMEM);

	if (!(ump->um_extattr.uepm_flags & UFS_EXTATTR_UEPM_STARTED)) {
		error = EOPNOTSUPP;
		goto free_exit;
	}

	if (ufs_extattr_find_attr(ump, attrnamespace, attrname)) {
		error = EEXIST;
		goto free_exit;
	}

	strncpy(attribute->uele_attrname, attrname, UFS_EXTATTR_MAXEXTATTRNAME);
	attribute->uele_attrnamespace = attrnamespace;
	bzero(&attribute->uele_fileheader,
	    sizeof(struct ufs_extattr_fileheader));
	
	attribute->uele_backing_vnode = backing_vnode;

	auio.uio_iov = &aiov;
	auio.uio_iovcnt = 1;
	aiov.iov_base = (caddr_t) &attribute->uele_fileheader;
	aiov.iov_len = sizeof(struct ufs_extattr_fileheader);
	auio.uio_resid = sizeof(struct ufs_extattr_fileheader);
	auio.uio_offset = (off_t) 0;
	auio.uio_segflg = UIO_SYSSPACE;
	auio.uio_rw = UIO_READ;
	auio.uio_procp = p;

	VOP_LEASE(backing_vnode, p, p->p_ucred, LEASE_WRITE);
	vn_lock(backing_vnode, LK_SHARED | LK_NOPAUSE | LK_RETRY, p);
	error = VOP_READ(backing_vnode, &auio, IO_NODELOCKED,
	    ump->um_extattr.uepm_ucred);
	VOP_UNLOCK(backing_vnode, 0, p);

	if (error)
		goto free_exit;

	if (auio.uio_resid != 0) {
		printf("ufs_extattr_enable: malformed attribute header\n");
		error = EINVAL;
		goto free_exit;
	}

	if (attribute->uele_fileheader.uef_magic != UFS_EXTATTR_MAGIC) {
		printf("ufs_extattr_enable: invalid attribute header magic\n");
		error = EINVAL;
		goto free_exit;
	}

	if (attribute->uele_fileheader.uef_version != UFS_EXTATTR_VERSION) {
		printf("ufs_extattr_enable: incorrect attribute header "
		    "version\n");
		error = EINVAL;
		goto free_exit;
	}

	backing_vnode->v_flag |= VSYSTEM;
	LIST_INSERT_HEAD(&ump->um_extattr.uepm_list, attribute, uele_entries);

	return (0);

free_exit:
	FREE(attribute, M_UFS_EXTATTR);
	return (error);
}

/*
 * Disable extended attribute support on an FS.
 */
static int
ufs_extattr_disable(struct ufsmount *ump, int attrnamespace,
    const char *attrname, struct proc *p)
{
	struct ufs_extattr_list_entry	*uele;
	int	error = 0;

	if (!ufs_extattr_valid_attrname(attrnamespace, attrname))
		return (EINVAL);

	uele = ufs_extattr_find_attr(ump, attrnamespace, attrname);
	if (!uele)
		return (ENOENT);

	LIST_REMOVE(uele, uele_entries);

	uele->uele_backing_vnode->v_flag &= ~VSYSTEM;
	error = vn_close(uele->uele_backing_vnode, FREAD|FWRITE, p->p_ucred, p);

	FREE(uele, M_UFS_EXTATTR);

	return (error);
}

/*
 * VFS call to manage extended attributes in UFS.  If filename_vp is
 * non-NULL, it must be passed in locked, and regardless of errors in
 * processing, will be unlocked.
 */
int
ufs_extattrctl(struct mount *mp, int cmd, struct vnode *filename_vp,
    int attrnamespace, const char *attrname, struct proc *p)
{
	struct ufsmount	*ump = VFSTOUFS(mp);
	int	error;

#if 0
	/* jail? -XXX */
	/*
	 * Processes with privilege, but in jail, are not allowed to
	 * configure extended attributes.
	 */
	if ((error = suser_xxx(p->p_ucred, p, 0))) {
		if (filename_vp != NULL)
			VOP_UNLOCK(filename_vp, 0, p);
		return (error);
	}
#endif

	switch(cmd) {
	case UFS_EXTATTR_CMD_START:
		if (filename_vp != NULL) {
			VOP_UNLOCK(filename_vp, 0, p);
			return (EINVAL);
		}
		if (attrname != NULL)
			return (EINVAL);

		error = ufs_extattr_start(mp, p);

		return (error);
		
	case UFS_EXTATTR_CMD_STOP:
		if (filename_vp != NULL) {
			VOP_UNLOCK(filename_vp, 0, p);
			return (EINVAL);
		}
		if (attrname != NULL)
			return (EINVAL);

		error = ufs_extattr_stop(mp, p);

		return (error);

	case UFS_EXTATTR_CMD_ENABLE:

		if (filename_vp == NULL)
			return (EINVAL);
		if (attrname == NULL) {
			VOP_UNLOCK(filename_vp, 0, p);
			return (EINVAL);
		}

		/*
		 * ufs_extattr_enable_with_open() will always unlock the
		 * vnode, regardless of failure.
		 */
		ufs_extattr_uepm_lock(ump, p);
		error = ufs_extattr_enable_with_open(ump, filename_vp,
		    attrnamespace, attrname, p);
		ufs_extattr_uepm_unlock(ump, p);

		return (error);

	case UFS_EXTATTR_CMD_DISABLE:

		if (filename_vp != NULL) {
			VOP_UNLOCK(filename_vp, 0, p);
			return (EINVAL);
		}
		if (attrname == NULL)
			return (EINVAL);

		ufs_extattr_uepm_lock(ump, p);
		error = ufs_extattr_disable(ump, attrnamespace, attrname, p);
		ufs_extattr_uepm_unlock(ump, p);

		return (error);

	default:
		return (EINVAL);
	}
}

/*
 * Credential check based on process requesting service, and per-attribute
 * permissions.
 */
static int
ufs_extattr_credcheck(struct vnode *vp, struct ufs_extattr_list_entry *uele,
    struct ucred *cred, struct proc *p, int access)
{

	/*
	 * Kernel-invoked always succeeds.
	 */
	if (cred == NULL)
		return (0);

	/*
	 * Do not allow privileged processes in jail to directly
	 * manipulate system attributes.
	 *
	 * XXX What capability should apply here?
	 * Probably CAP_SYS_SETFFLAG.
	 */
	switch (uele->uele_attrnamespace) {
	case EXTATTR_NAMESPACE_SYSTEM:
		return (suser(cred, &p->p_acflag));
	case EXTATTR_NAMESPACE_USER:
		return (VOP_ACCESS(vp, access, cred, p));
	default:
		return (EPERM);
	}
}

/*
 * Vnode operating to retrieve a named extended attribute.
 */
int
ufs_vop_getextattr(void *v)
{
	struct vop_getextattr_args /* {
		IN struct vnode *a_vp;
		IN int a_attrnamespace;
		IN const char *a_name;
		INOUT struct uio *a_uio;
		OUT struct size_t *a_size;
		IN struct ucred *a_cred;
		IN struct proc *a_p;
	} */ *ap = v;
	struct mount	*mp = ap->a_vp->v_mount;
	struct ufsmount	*ump = VFSTOUFS(mp);
	int	error;

	ufs_extattr_uepm_lock(ump, ap->a_p);

	error = ufs_extattr_get(ap->a_vp, ap->a_attrnamespace, ap->a_name,
	    ap->a_uio, ap->a_size, ap->a_cred, ap->a_p);

	ufs_extattr_uepm_unlock(ump, ap->a_p);

	return (error);
}

/*
 * Real work associated with retrieving a named attribute--assumes that
 * the attribute lock has already been grabbed.
 */
static int
ufs_extattr_get(struct vnode *vp, int attrnamespace, const char *name,
    struct uio *uio, size_t *size, struct ucred *cred, struct proc *p)
{
	struct ufs_extattr_list_entry	*attribute;
	struct ufs_extattr_header	ueh;
	struct iovec	local_aiov;
	struct uio	local_aio;
	struct mount	*mp = vp->v_mount;
	struct ufsmount	*ump = VFSTOUFS(mp);
	struct inode	*ip = VTOI(vp);
	off_t	base_offset;
	size_t	len, old_len;
	int	error = 0;

	if (!(ump->um_extattr.uepm_flags & UFS_EXTATTR_UEPM_STARTED))
		return (EOPNOTSUPP);

	if (strlen(name) == 0) {
		/* XXX retrieve attribute lists. */
		/* XXX should probably be checking for name == NULL? */
		return (EINVAL);
	}

	attribute = ufs_extattr_find_attr(ump, attrnamespace, name);
	if (!attribute)
		/* XXX: ENOENT here will eventually be ENOATTR. */
		return (ENOENT);

	if ((error = ufs_extattr_credcheck(vp, attribute, cred, p, IREAD)))
		return (error);

	/*
	 * Allow only offsets of zero to encourage the read/replace
	 * extended attribute semantic.  Otherwise we can't guarantee
	 * atomicity, as we don't provide locks for extended attributes.
	 */
	if (uio != NULL && uio->uio_offset != 0)
		return (ENXIO);

	/*
	 * Find base offset of header in file based on file header size, and
	 * data header size + maximum data size, indexed by inode number.
	 */
	base_offset = sizeof(struct ufs_extattr_fileheader) +
	    ip->i_number * (sizeof(struct ufs_extattr_header) +
	    attribute->uele_fileheader.uef_size);

	/*
	 * Read in the data header to see if the data is defined, and if so
	 * how much.
	 */
	bzero(&ueh, sizeof(struct ufs_extattr_header));
	local_aiov.iov_base = (caddr_t) &ueh;
	local_aiov.iov_len = sizeof(struct ufs_extattr_header);
	local_aio.uio_iov = &local_aiov;
	local_aio.uio_iovcnt = 1;
	local_aio.uio_rw = UIO_READ;
	local_aio.uio_segflg = UIO_SYSSPACE;
	local_aio.uio_procp = p;
	local_aio.uio_offset = base_offset;
	local_aio.uio_resid = sizeof(struct ufs_extattr_header);
	
	/*
	 * Acquire locks.
	 */
	VOP_LEASE(attribute->uele_backing_vnode, p, cred, LEASE_READ);
	/*
	 * Don't need to get a lock on the backing file if the getattr is
	 * being applied to the backing file, as the lock is already held.
	 */
	if (attribute->uele_backing_vnode != vp)
		vn_lock(attribute->uele_backing_vnode, LK_SHARED |
		    LK_NOPAUSE | LK_RETRY, p);

	error = VOP_READ(attribute->uele_backing_vnode, &local_aio,
	    IO_NODELOCKED, ump->um_extattr.uepm_ucred);
	if (error)
		goto vopunlock_exit;

	/* Defined? */
	if ((ueh.ueh_flags & UFS_EXTATTR_ATTR_FLAG_INUSE) == 0) {
		/* XXX: ENOENT here will eventually be ENOATTR. */
		error = ENOENT;
		goto vopunlock_exit;
	}

	/* Valid for the current inode generation? */
	if (ueh.ueh_i_gen != ip->i_ffs_gen) {
		/*
		 * The inode itself has a different generation number
		 * than the attribute data.  For now, the best solution
		 * is to coerce this to undefined, and let it get cleaned
		 * up by the next write or extattrctl clean.
		 */
		printf("ufs_extattr_get: inode number inconsistency (%d, %d)\n",
		    ueh.ueh_i_gen, ip->i_ffs_gen);
		/* XXX: ENOENT here will eventually be ENOATTR. */
		error = ENOENT;
		goto vopunlock_exit;
	}

	/* Local size consistency check. */
	if (ueh.ueh_len > attribute->uele_fileheader.uef_size) {
		error = ENXIO;
		goto vopunlock_exit;
	}

	/* Return full data size if caller requested it. */
	if (size != NULL)
		*size = ueh.ueh_len;

	/* Return data if the caller requested it. */
	if (uio != NULL) {
		/* Allow for offset into the attribute data. */
		uio->uio_offset = base_offset + sizeof(struct
		    ufs_extattr_header);

		/*
		 * Figure out maximum to transfer -- use buffer size and
		 * local data limit.
		 */
		len = MIN(uio->uio_resid, ueh.ueh_len);
		old_len = uio->uio_resid;
		uio->uio_resid = len;
 
		error = VOP_READ(attribute->uele_backing_vnode, uio,
		    IO_NODELOCKED, ump->um_extattr.uepm_ucred);
		if (error)
			goto vopunlock_exit;

		uio->uio_resid = old_len - (len - uio->uio_resid);
	}

vopunlock_exit:

	if (uio != NULL)
		uio->uio_offset = 0;

	if (attribute->uele_backing_vnode != vp)
		VOP_UNLOCK(attribute->uele_backing_vnode, 0, p);

	return (error);
}

/*
 * Vnode operation to set a named attribute.
 */
int
ufs_vop_setextattr(void *v)
{
	struct vop_setextattr_args /* {
		IN struct vnode *a_vp;
		IN int a_attrnamespace;
		IN const char *a_name;
		INOUT struct uio *a_uio;
		IN struct ucred *a_cred;
		IN struct proc *a_p;
	} */ *ap = v;
	struct mount	*mp = ap->a_vp->v_mount;
	struct ufsmount	*ump = VFSTOUFS(mp); 

	int	error;

	ufs_extattr_uepm_lock(ump, ap->a_p);

	if (ap->a_uio != NULL)
		error = ufs_extattr_set(ap->a_vp, ap->a_attrnamespace,
		    ap->a_name, ap->a_uio, ap->a_cred, ap->a_p);
	else
		error = ufs_extattr_rm(ap->a_vp, ap->a_attrnamespace,
		    ap->a_name, ap->a_cred, ap->a_p);

	ufs_extattr_uepm_unlock(ump, ap->a_p);

	return (error);
}

/*
 * Real work associated with setting a vnode's extended attributes;
 * assumes that the attribute lock has already been grabbed.
 */
static int
ufs_extattr_set(struct vnode *vp, int attrnamespace, const char *name,
    struct uio *uio, struct ucred *cred, struct proc *p)
{
	struct ufs_extattr_list_entry	*attribute;
	struct ufs_extattr_header	ueh;
	struct iovec	local_aiov;
	struct uio	local_aio;
	struct mount	*mp = vp->v_mount;
	struct ufsmount	*ump = VFSTOUFS(mp);
	struct inode	*ip = VTOI(vp);
	off_t	base_offset;
	int	error = 0, ioflag;

	if (vp->v_mount->mnt_flag & MNT_RDONLY)
		return (EROFS);
	if (!(ump->um_extattr.uepm_flags & UFS_EXTATTR_UEPM_STARTED))
		return (EOPNOTSUPP);
	if (!ufs_extattr_valid_attrname(attrnamespace, name))
		return (EINVAL);

	attribute = ufs_extattr_find_attr(ump, attrnamespace, name);
	if (!attribute)
		/* XXX: ENOENT here will eventually be ENOATTR. */
		return (ENOENT);

	if ((error = ufs_extattr_credcheck(vp, attribute, cred, p, IWRITE)))
		return (error);

	/*
	 * Early rejection of invalid offsets/length.
	 * Reject: any offset but 0 (replace)
	 *	 Any size greater than attribute size limit
 	 */
	if (uio->uio_offset != 0 ||
	    uio->uio_resid > attribute->uele_fileheader.uef_size)
		return (ENXIO);

	/*
	 * Find base offset of header in file based on file header size, and
	 * data header size + maximum data size, indexed by inode number.
	 */
	base_offset = sizeof(struct ufs_extattr_fileheader) +
	    ip->i_number * (sizeof(struct ufs_extattr_header) +
	    attribute->uele_fileheader.uef_size);

	/*
	 * Write out a data header for the data.
	 */
	ueh.ueh_len = uio->uio_resid;
	ueh.ueh_flags = UFS_EXTATTR_ATTR_FLAG_INUSE;
	ueh.ueh_i_gen = ip->i_ffs_gen;
	local_aiov.iov_base = (caddr_t) &ueh;
	local_aiov.iov_len = sizeof(struct ufs_extattr_header);
	local_aio.uio_iov = &local_aiov;
	local_aio.uio_iovcnt = 1;
	local_aio.uio_rw = UIO_WRITE;
	local_aio.uio_segflg = UIO_SYSSPACE;
	local_aio.uio_procp = p;
	local_aio.uio_offset = base_offset;
	local_aio.uio_resid = sizeof(struct ufs_extattr_header);

	/*
	 * Acquire locks.
	 */
	VOP_LEASE(attribute->uele_backing_vnode, p, cred, LEASE_WRITE);

	/*
	 * Don't need to get a lock on the backing file if the setattr is
	 * being applied to the backing file, as the lock is already held.
	 */
	if (attribute->uele_backing_vnode != vp)
		vn_lock(attribute->uele_backing_vnode, 
		    LK_EXCLUSIVE | LK_NOPAUSE | LK_RETRY, p);

	ioflag = IO_NODELOCKED;
	if (ufs_extattr_sync)
		ioflag |= IO_SYNC;
	error = VOP_WRITE(attribute->uele_backing_vnode, &local_aio, ioflag,
	    ump->um_extattr.uepm_ucred);
	if (error)
		goto vopunlock_exit;

	if (local_aio.uio_resid != 0) {
		error = ENXIO;
		goto vopunlock_exit;
	}

	/*
	 * Write out user data.
	 */
	uio->uio_offset = base_offset + sizeof(struct ufs_extattr_header);

	ioflag = IO_NODELOCKED;
	if (ufs_extattr_sync)
		ioflag |= IO_SYNC;
	error = VOP_WRITE(attribute->uele_backing_vnode, uio, ioflag,
	    ump->um_extattr.uepm_ucred);

vopunlock_exit:
	uio->uio_offset = 0;

	if (attribute->uele_backing_vnode != vp)
		VOP_UNLOCK(attribute->uele_backing_vnode, 0, p);

	return (error);
}

/*
 * Real work associated with removing an extended attribute from a vnode.
 * Assumes the attribute lock has already been grabbed.
 */
static int
ufs_extattr_rm(struct vnode *vp, int attrnamespace, const char *name,
    struct ucred *cred, struct proc *p)
{
	struct ufs_extattr_list_entry	*attribute;
	struct ufs_extattr_header	ueh;
	struct iovec	local_aiov;
	struct uio	local_aio;
	struct mount	*mp = vp->v_mount;
	struct ufsmount	*ump = VFSTOUFS(mp);
	struct inode	*ip = VTOI(vp);
	off_t	base_offset;
	int	error = 0, ioflag;

	if (vp->v_mount->mnt_flag & MNT_RDONLY)  
		return (EROFS);
	if (!(ump->um_extattr.uepm_flags & UFS_EXTATTR_UEPM_STARTED))
		return (EOPNOTSUPP);
	if (!ufs_extattr_valid_attrname(attrnamespace, name))
		return (EINVAL);

	attribute = ufs_extattr_find_attr(ump, attrnamespace, name);
	if (!attribute)
		/* XXX: ENOENT here will eventually be ENOATTR. */
		return (ENOENT);

	if ((error = ufs_extattr_credcheck(vp, attribute, cred, p, IWRITE)))
		return (error);

	/*
	 * Find base offset of header in file based on file header size, and
	 * data header size + maximum data size, indexed by inode number.
	 */
	base_offset = sizeof(struct ufs_extattr_fileheader) +
	    ip->i_number * (sizeof(struct ufs_extattr_header) +
	    attribute->uele_fileheader.uef_size);

	/*
	 * Check to see if currently defined.
	 */
	bzero(&ueh, sizeof(struct ufs_extattr_header));

	local_aiov.iov_base = (caddr_t) &ueh;
	local_aiov.iov_len = sizeof(struct ufs_extattr_header);
	local_aio.uio_iov = &local_aiov;
	local_aio.uio_iovcnt = 1;
	local_aio.uio_rw = UIO_READ;
	local_aio.uio_segflg = UIO_SYSSPACE;
	local_aio.uio_procp = p;
	local_aio.uio_offset = base_offset;
	local_aio.uio_resid = sizeof(struct ufs_extattr_header);

	VOP_LEASE(attribute->uele_backing_vnode, p, cred, LEASE_WRITE);

	/*
	 * Don't need to get the lock on the backing vnode if the vnode we're
	 * modifying is it, as we already hold the lock.
	 */
	if (attribute->uele_backing_vnode != vp)
		vn_lock(attribute->uele_backing_vnode,
		    LK_EXCLUSIVE | LK_NOPAUSE | LK_RETRY, p);

	error = VOP_READ(attribute->uele_backing_vnode, &local_aio,
	    IO_NODELOCKED, ump->um_extattr.uepm_ucred);
	if (error)
		goto vopunlock_exit;

	/* Defined? */
	if ((ueh.ueh_flags & UFS_EXTATTR_ATTR_FLAG_INUSE) == 0) {
		/* XXX: ENOENT here will eventually be ENOATTR. */
		error = ENOENT;
		goto vopunlock_exit;
	}

	/* Valid for the current inode generation? */
	if (ueh.ueh_i_gen != ip->i_ffs_gen) {
		/*
		 * The inode itself has a different generation number than
		 * the attribute data.  For now, the best solution is to
		 * coerce this to undefined, and let it get cleaned up by
		 * the next write or extattrctl clean.
		 */
		printf("ufs_extattr_rm: inode number inconsistency (%d, %d)\n",
		    ueh.ueh_i_gen, ip->i_ffs_gen);
		/* XXX: ENOENT here will eventually be ENOATTR. */
		error = ENOENT;
		goto vopunlock_exit;
	}

	/* Flag it as not in use. */
	ueh.ueh_flags = 0;
	ueh.ueh_len = 0;

	local_aiov.iov_base = (caddr_t) &ueh;
	local_aiov.iov_len = sizeof(struct ufs_extattr_header);
	local_aio.uio_iov = &local_aiov;
	local_aio.uio_iovcnt = 1;
	local_aio.uio_rw = UIO_WRITE;
	local_aio.uio_segflg = UIO_SYSSPACE;
	local_aio.uio_procp = p;
	local_aio.uio_offset = base_offset;
	local_aio.uio_resid = sizeof(struct ufs_extattr_header);

	ioflag = IO_NODELOCKED;
	if (ufs_extattr_sync)
		ioflag |= IO_SYNC;
	error = VOP_WRITE(attribute->uele_backing_vnode, &local_aio, ioflag,
	    ump->um_extattr.uepm_ucred);
	if (error)
		goto vopunlock_exit;

	if (local_aio.uio_resid != 0)
		error = ENXIO;

vopunlock_exit:
	VOP_UNLOCK(attribute->uele_backing_vnode, 0, p);

	return (error);
}

/*
 * Called by UFS when an inode is no longer active and should have its
 * attributes stripped.
 */
void
ufs_extattr_vnode_inactive(struct vnode *vp, struct proc *p)
{
	struct ufs_extattr_list_entry	*uele;
	struct mount	*mp = vp->v_mount;
	struct ufsmount	*ump = VFSTOUFS(mp);

	/*
	 * In that case, we cannot lock. We should not have any active vnodes
	 * on the fs if this is not yet initialized but is going to be, so
	 * this can go unlocked.
	 */
	if (!(ump->um_extattr.uepm_flags & UFS_EXTATTR_UEPM_INITIALIZED))
		return;

	ufs_extattr_uepm_lock(ump, p);

	if (!(ump->um_extattr.uepm_flags & UFS_EXTATTR_UEPM_STARTED)) {
		ufs_extattr_uepm_unlock(ump, p);
		return;
	}

	LIST_FOREACH(uele, &ump->um_extattr.uepm_list, uele_entries)
		ufs_extattr_rm(vp, uele->uele_attrnamespace,
		    uele->uele_attrname, NULL, p);

	ufs_extattr_uepm_unlock(ump, p);
}

#endif /* !UFS_EXTATTR */
