/*
 * Copyright (c) 1995 - 2000 Kungliga Tekniska H�gskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <xfs/xfs_locl.h>

RCSID("$Id: xfs_vfsops-bsd.c,v 1.8 2002/06/07 04:10:32 hin Exp $");

/*
 * XFS vfs operations.
 */

#include <xfs/xfs_common.h>
#include <xfs/xfs_message.h>
#include <xfs/xfs_fs.h>
#include <xfs/xfs_dev.h>
#include <xfs/xfs_deb.h>
#include <xfs/xfs_vfsops.h>
#include <xfs/xfs_vfsops-bsd.h>
#include <xfs/xfs_vnodeops.h>

int
xfs_mount(struct mount *mp,
	  const char *user_path,
#ifdef __OpenBSD__
	  void *user_data,
#else
	  caddr_t user_data,
#endif
	  struct nameidata *ndp,
	  struct proc *p)
{
    return xfs_mount_common(mp, user_path, user_data, ndp, p);
}

int
xfs_start(struct mount * mp, int flags, struct proc * p)
{
    XFSDEB(XDEBVFOPS, ("xfs_start mp = %lx, flags = %d, proc = %lx\n", 
		       (unsigned long)mp, flags, (unsigned long)p));
    return 0;
}


int
xfs_unmount(struct mount * mp, int mntflags, struct proc *p)
{
    XFSDEB(XDEBVFOPS, ("xfs_umount: mp = %lx, mntflags = %d, proc = %lx\n", 
		       (unsigned long)mp, mntflags, (unsigned long)p));
    return xfs_unmount_common(mp, mntflags);
}

int
xfs_root(struct mount *mp, struct vnode **vpp)
{
    XFSDEB(XDEBVFOPS, ("xfs_root mp = %lx\n", (unsigned long)mp));
    return xfs_root_common(mp, vpp, xfs_curproc(), xfs_curproc()->p_ucred);
}

int
xfs_quotactl(struct mount *mp, int cmd, uid_t uid, caddr_t arg, struct proc *p)
{
    XFSDEB(XDEBVFOPS, ("xfs_quotactl: mp = %lx, cmd = %d, uid = %u, "
		       "arg = %lx, proc = %lx\n", 
		       (unsigned long)mp, cmd, uid,
		       (unsigned long)arg, (unsigned long)p));
    return EOPNOTSUPP;
}

int
xfs_statfs(struct mount *mp, struct statfs *sbp, struct proc *p)
{
    XFSDEB(XDEBVFOPS, ("xfs_statfs: mp = %lx, sbp = %lx, proc = %lx\n", 
		       (unsigned long)mp,
		       (unsigned long)sbp,
		       (unsigned long)p));
    bcopy(&mp->mnt_stat, sbp, sizeof(*sbp));
    return 0;
}

int
xfs_sync(struct mount *mp, int waitfor, struct ucred *cred, struct proc *p)
{
    XFSDEB(XDEBVFOPS, ("xfs_sync: mp = %lx, waitfor = %d, "
		       "cred = %lx, proc = %lx\n",
		       (unsigned long)mp,
		       waitfor,
		       (unsigned long)cred,
		       (unsigned long)p));
    return 0;
}

int
xfs_vget(struct mount * mp,
#ifdef __APPLE__
	 void *ino,
#else
	 ino_t ino,
#endif
	 struct vnode ** vpp)
{
    XFSDEB(XDEBVFOPS, ("xfs_vget\n"));
    return EOPNOTSUPP;
}

#ifdef HAVE_STRUCT_VFSOPS_VFS_CHECKEXP
int
xfs_fhtovp(struct mount * mp,
	   struct fid * fhp,
	   struct vnode ** vpp)
#else
int
xfs_fhtovp(struct mount * mp,
	   struct fid * fhp,
	   struct mbuf * nam,
	   struct vnode ** vpp,
	   int *exflagsp,
	   struct ucred ** credanonp)
#endif
{
#ifdef ARLA_KNFS
    static struct ucred fhtovpcred;
    struct netcred *np = NULL;
    struct xfs_node *xn;
    struct vnode *vp;
    xfs_handle handle;
    int error;

    XFSDEB(XDEBVFOPS, ("xfs_fhtovp\n"));

    if (fhp->fid_len != 16) {
	printf("xfs_fhtovp: *PANIC* got a invalid length of a fid\n");
	return EINVAL;
    }

    /* XXX: Should see if we is exported to this client */
#if 0
    np = vfs_export_lookup(mp, &ump->um_export, nam);
    if (np == NULL)
	return EACCES;
#endif

    memcpy(&handle, fhp->fid_data, sizeof(handle));
    XFSDEB(XDEBVFOPS, ("xfs_fhtovp: fid: %d.%d.%d.%d\n", 
		       handle.a, handle.d, handle.c, handle.d));

    XFSDEB(XDEBVFOPS, ("xfs_fhtovp: xfs_vnode_find\n"));
    xn = xfs_node_find(&xfs[0], &handle); /* XXX: 0 */

    if (xn == NULL) {
	struct xfs_message_getattr msg;

        error = xfs_getnewvnode(xfs[0].mp, &vp, &handle);
        if (error)
            return error;
	
	xfs_do_vget(vp, 0, curproc);

    } else {
	/* XXX access ? */
	vp = XNODE_TO_VNODE(xn);

	/* XXX wrong ? (we tell arla below) */
        if (vp->v_usecount <= 0) 
	    xfs_do_vget(vp, 0, curproc);
	else
	    VREF(vp);
	error = 0;
    }

    if (error == 0) {
	fhtovpcred.cr_uid = 0;
	fhtovpcred.cr_gid = 0;
	fhtovpcred.cr_ngroups = 0;
      
	*vpp = vp;
#ifdef MNT_EXPUBLIC
	*exflagsp = MNT_EXPUBLIC;
#else
	*exflagsp = 0;
#endif
	*credanonp = &fhtovpcred;

	XFSDEB(XDEBVFOPS, ("xfs_fhtovp done\n"));

	/* 
	 * XXX tell arla about this node is hold by nfsd.
	 * There need to be code in xfs_write too.
	 */
    } else
	XFSDEB(XDEBVFOPS, ("xfs_fhtovp failed (%d)\n", error));

    return error;
#else
    return EOPNOTSUPP;
#endif
}

int
xfs_checkexp (struct mount *mp,
#ifdef __FreeBSD__
	      struct sockaddr *nam,
#else
	      struct mbuf *nam,
#endif
	      int *exflagsp,
	      struct ucred **credanonp)
{
    XFSDEB(XDEBVFOPS, ("xfs_checkexp\n"));
    return EOPNOTSUPP;
}

int
xfs_vptofh(struct vnode * vp,
	   struct fid * fhp)
{
#ifdef ARLA_KNFS
    struct xfs_node *xn;
    XFSDEB(XDEBVFOPS, ("xfs_vptofh\n"));

    if (MAXFIDSZ < 16)
	return EOPNOTSUPP;

    xn = VNODE_TO_XNODE(vp);

    if (xn == NULL)
	return EINVAL;

    fhp->fid_len = 16;
    memcpy(fhp->fid_data, &xn->handle,  16);

    return 0;
#else
    XFSDEB(XDEBVFOPS, ("xfs_vptofh\n"));
    return EOPNOTSUPP;
#endif
}

/* 
 * xfs complete dead vnodes implementation.
 *
 * this is because the dead_vnodeops_p is _not_ filesystem, but rather
 * a part of the vfs-layer.  
 */

int
xfs_dead_lookup(struct vop_lookup_args * ap)
     /* struct vop_lookup_args {
	struct vnodeop_desc *a_desc;
	struct vnode *a_dvp;
	struct vnode **a_vpp;
	struct componentname *a_cnp;
}; */
{
    *ap->a_vpp = NULL;
    return ENOTDIR;
}

/* 
 * Given `fsid', `fileid', and `gen', return in `vpp' a locked and
 * ref'ed vnode from that file system with that id and generation.
 * All is done in the context of `proc'.  Returns 0 if succesful, and
 * error otherwise.  
 */

int
xfs_fhlookup (struct proc *proc,
	      struct xfs_fhandle_t *fhp,
	      struct vnode **vpp)
{
    int error;
    struct mount *mp;
#if !(defined(HAVE_GETFH) && defined(HAVE_FHOPEN))
    struct ucred *cred = proc->p_ucred;
    struct vattr vattr;
    fsid_t fsid;
    struct xfs_fh_args *fh_args = (struct xfs_fh_args *)fhp->fhdata;

    XFSDEB(XDEBVFOPS, ("xfs_fhlookup (xfs)\n"));

    error = xfs_suser (proc);
    if (error)
	return EPERM;

    if (fhp->len < sizeof(struct xfs_fh_args))
	return EINVAL;
    
    fsid = SCARG(fh_args, fsid);

    mp = xfs_vfs_getvfs (&fsid);
    if (mp == NULL)
	return ENXIO;

#ifdef __APPLE__
    {
	u_int32_t ino = SCARG(fh_args, fileid);
	error = VFS_VGET(mp, &ino, vpp);
    }
#else
    error = VFS_VGET(mp, SCARG(fh_args, fileid), vpp);
#endif

    if (error)
	return error;

    if (*vpp == NULL)
	return ENOENT;

    error = VOP_GETATTR(*vpp, &vattr, cred, proc);
    if (error) {
	vput(*vpp);
	return error;
    }

    if (vattr.va_gen != SCARG(fh_args, gen)) {
	vput(*vpp);
	return ENOENT;
    }
#else /* HAVE_GETFH && HAVE_FHOPEN */
    {
	fhandle_t *fh = (fhandle_t *) fhp;

	XFSDEB(XDEBVFOPS, ("xfs_fhlookup (native)\n"));

	mp = xfs_vfs_getvfs (&fh->fh_fsid);
	if (mp == NULL)
	    return ESTALE;

	if ((error = VFS_FHTOVP(mp, &fh->fh_fid, vpp)) != 0) {
	    *vpp = NULL;
	    return error;
	}
    }
#endif  /* HAVE_GETFH && HAVE_FHOPEN */

#ifdef HAVE_KERNEL_VFS_OBJECT_CREATE
    if ((*vpp)->v_type == VREG && (*vpp)->v_object == NULL)
	xfs_vfs_object_create (*vpp, proc, proc->p_ucred);
#elif __APPLE__
    if ((*vpp)->v_type == VREG && (!UBCINFOEXISTS(*vpp))) {
        ubc_info_init(*vpp);
    }
    ubc_hold(*vpp);
#endif
    return 0;
}



/*
 * Perform an open operation on the vnode identified by a `xfs_fhandle_t'
 * (see xfs_fhlookup) with flags `user_flags'.  Returns 0 or
 * error.  If succsesful, the file descriptor is returned in `retval'.
 */

extern struct fileops vnops;	/* sometimes declared in <file.h> */

int
xfs_fhopen (struct proc *proc,
	    struct xfs_fhandle_t *fhp,
	    int user_flags,
	    register_t *retval)
{
    int error;
    struct vnode *vp;
    struct ucred *cred = proc->p_ucred;
    int flags = FFLAGS(user_flags);
    int index;
    struct file *fp;
    int mode;
    struct xfs_fhandle_t fh;

    XFSDEB(XDEBVFOPS, ("xfs_fhopen: flags = %d\n", user_flags));

    panic("Pj�xa");

    error = copyin (fhp, &fh, sizeof(fh));
    if (error)
	return error;

    error = xfs_fhlookup (proc, &fh, &vp);
    XFSDEB(XDEBVFOPS, ("xfs_fhlookup returned %d\n", error));
    if (error)
	return error;

    switch (vp->v_type) {
    case VDIR :
    case VREG :
	break;
    case VLNK :
	error = EMLINK;
	goto out;
    default :
	error = EOPNOTSUPP;
	break;
    }

    mode = 0;
    if (flags & FWRITE) {
	switch (vp->v_type) {
	case VREG :
	    break;
	case VDIR :
	    error = EISDIR;
	    goto out;
	default :
	    error = EOPNOTSUPP;
	    break;
	}

	error = vn_writechk (vp);
	if (error)
	    goto out;

	mode |= VWRITE;
    }
    if (flags & FREAD)
	mode |= VREAD;

    if (mode) {
	error = VOP_ACCESS(vp, mode, cred, proc);
	if (error)
	    goto out;
    }

    error = VOP_OPEN(vp, flags, cred, proc);
    if (error)
	goto out;

    error = falloc(proc, &fp, &index);
    if (error)
	goto out;

    if (flags & FWRITE)
        vp->v_writecount++;

#if defined(__FreeBSD_version) && __FreeBSD_version >= 300000
    if (vp->v_type == VREG) {
	error = xfs_vfs_object_create(vp, proc, proc->p_cred->pc_ucred);
	if (error)
	    goto out;
    }
#endif

    fp->f_flag = flags & FMASK;
    fp->f_type = DTYPE_VNODE;
    fp->f_ops  = &vnops;
    fp->f_data = (caddr_t)vp;
    xfs_vfs_unlock(vp, proc);
    *retval = index;
#ifdef FILE_UNUSE
    FILE_UNUSE(fp, proc);
#endif
#ifdef __APPLE__
    *fdflags(proc, index) &= ~UF_RESERVED;
#endif
    return 0;
out:
    XFSDEB(XDEBVFOPS, ("xfs_fhopen: error = %d\n", error));
    vput(vp);
    return error;
}
