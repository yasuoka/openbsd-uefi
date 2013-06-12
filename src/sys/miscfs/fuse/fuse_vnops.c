/* $OpenBSD: fuse_vnops.c,v 1.3 2013/06/12 22:55:02 tedu Exp $ */
/*
 * Copyright (c) 2012-2013 Sylvestre Gallon <ccna.syl@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/dirent.h>
#include <sys/fcntl.h>
#include <sys/lockf.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/poll.h>
#include <sys/pool.h>
#include <sys/specdev.h>
#include <sys/statvfs.h>
#include <sys/vnode.h>
#include <sys/fusebuf.h>

#include "fusefs_node.h"
#include "fusefs.h"

#ifdef	FUSE_DEBUG_VNOP
#define	DPRINTF(fmt, arg...)	printf("fuse vnop: " fmt, ##arg)
#else
#define	DPRINTF(fmt, arg...)
#endif

/* Prototypes for fusefs vnode ops */
int	fusefs_lookup(void *);
int	fusefs_open(void *);
int	fusefs_close(void *);
int	fusefs_access(void *);
int	fusefs_getattr(void *);
int	fusefs_setattr(void *);
int	fusefs_ioctl(void *);
int	fusefs_link(void *);
int	fusefs_symlink(void *);
int	fusefs_readdir(void *);
int	fusefs_readlink(void *);
int	fusefs_inactive(void *);
int	fusefs_reclaim(void *);
int	fusefs_print(void *);
int	fusefs_create(void *);
int	fusefs_mknod(void *);
int	fusefs_read(void *);
int	fusefs_write(void *);
int	fusefs_poll(void *);
int	fusefs_remove(void *);
int	fusefs_rename(void *);
int	fusefs_mkdir(void *);
int	fusefs_rmdir(void *);
int	fusefs_strategy(void *);
int	fusefs_lock(void *);
int	fusefs_unlock(void *);
int	fusefs_islocked(void *);
int	fusefs_advlock(void *);

struct vops fusefs_vops = {
	.vop_lookup	= fusefs_lookup,
	.vop_create	= fusefs_create,
	.vop_mknod	= fusefs_mknod,
	.vop_open	= fusefs_open,
	.vop_close	= fusefs_close,
	.vop_access	= fusefs_access,
	.vop_getattr	= fusefs_getattr,
	.vop_setattr	= fusefs_setattr,
	.vop_read	= fusefs_read,
	.vop_write	= fusefs_write,
	.vop_ioctl	= fusefs_ioctl,
	.vop_poll	= fusefs_poll,
	.vop_fsync	= nullop,
	.vop_remove	= fusefs_remove,
	.vop_link	= fusefs_link,
	.vop_rename	= fusefs_rename,
	.vop_mkdir	= fusefs_mkdir,
	.vop_rmdir	= fusefs_rmdir,
	.vop_symlink	= fusefs_symlink,
	.vop_readdir	= fusefs_readdir,
	.vop_readlink	= fusefs_readlink,
	.vop_abortop	= vop_generic_abortop,
	.vop_inactive	= fusefs_inactive,
	.vop_reclaim	= fusefs_reclaim,
	.vop_lock	= fusefs_lock,
	.vop_unlock	= fusefs_unlock,
	.vop_bmap	= vop_generic_bmap,
	.vop_strategy	= fusefs_strategy,
	.vop_print	= fusefs_print,
	.vop_islocked	= fusefs_islocked,
	.vop_pathconf	= spec_pathconf,
	.vop_advlock	= fusefs_advlock,
};

void
update_vattr(struct mount *mp, struct vattr *v)
{
	v->va_fsid = mp->mnt_stat.f_fsid.val[0];
	v->va_type = IFTOVT(v->va_mode);
#if (S_BLKSIZE == 512)
	v->va_bytes = v->va_bytes << 9;
#else
	v->va_bytes = v->va_bytes * S_BLKSIZE;
#endif
	v->va_mode = v->va_mode & ~S_IFMT;
}

int
fusefs_open(void *v)
{
	struct vop_open_args *ap;
	struct fusefs_node *ip;
	struct fusefs_mnt *fmp;
	enum fufh_type fufh_type = FUFH_RDONLY;
	int flags = O_RDONLY;
	int error;
	int isdir;

	DPRINTF("fusefs_open\n");

	ap = v;
	ip = VTOI(ap->a_vp);
	fmp = (struct fusefs_mnt *)ip->ufs_ino.i_ump;

	if (!fmp->sess_init)
		return (0);

	DPRINTF("inode = %i mode=0x%x\n", ip->ufs_ino.i_number, ap->a_mode);

	isdir = 0;
	if (ip->vtype == VDIR)
		isdir = 1;
	else {
		if ((ap->a_mode & FREAD) && (ap->a_mode & FWRITE)) {
			fufh_type = FUFH_RDWR;
			flags = O_RDWR;
		} else if (ap->a_mode  & (FWRITE)) {
			fufh_type = FUFH_WRONLY;
			flags = O_WRONLY;
		}
	}

	/* already open i think all is ok */
	if (ip->fufh[fufh_type].fh_type != FUFH_INVALID)
		return (0);

	error = fusefs_file_open(fmp, ip, fufh_type, flags, isdir, ap->a_p);
	if (error)
		return (error);

	DPRINTF("file open fd : %i\n", ip->fufh[fufh_type].fh_id);

	return (error);
}

int
fusefs_close(void *v)
{
	struct vop_close_args *ap;
	struct fusefs_node *ip;
	struct fusefs_mnt *fmp;
	enum fufh_type fufh_type = FUFH_RDONLY;
	int isdir, i;

	DPRINTF("fusefs_close\n");

	ap = v;
	ip = VTOI(ap->a_vp);
	fmp = (struct fusefs_mnt *)ip->ufs_ino.i_ump;

	if (!fmp->sess_init)
		return (0);

	if (ip->vtype == VDIR) {
		isdir = 1;

		if (ip->fufh[fufh_type].fh_type != FUFH_INVALID)
			return (fusefs_file_close(fmp, ip, fufh_type, O_RDONLY,
			    isdir, ap->a_p));
	} else {
		if (ap->a_fflag & IO_NDELAY)
			return (0);

		if ((ap->a_fflag & FREAD) && (ap->a_fflag & FWRITE))
			fufh_type = FUFH_RDWR;
		else if (ap->a_fflag  & (FWRITE))
			fufh_type = FUFH_WRONLY;
	}

	/*
	 * if fh not valid lookup for another valid fh in vnode.
	 * Do we need panic if there's not a valid fh ?
	 */
	if (ip->fufh[fufh_type].fh_type != FUFH_INVALID) {
		for (i = 0; i < FUFH_MAXTYPE; i++)
			if (ip->fufh[fufh_type].fh_type != FUFH_INVALID)
				break;
		return (0);
	}

	return (0);
}

int
fusefs_access(void *v)
{
	struct vop_access_args *ap;
	struct fusefs_node *ip;
	struct fusefs_mnt *fmp;
	struct fusebuf *fbuf;
	struct proc *p;
	uint32_t mask = 0;
	int error = 0;

	DPRINTF("fusefs_access\n");

	ap = v;
	p = ap->a_p;
	ip = VTOI(ap->a_vp);
	fmp = (struct fusefs_mnt *)ip->ufs_ino.i_ump;

	if (!fmp->sess_init || (fmp->undef_op & UNDEF_ACCESS))
		goto system_check;

	if (ap->a_vp->v_type == VLNK)
		goto system_check;

	if (ap->a_vp->v_type == VREG && (ap->a_mode & VWRITE & VEXEC))
		goto system_check;

	if ((ap->a_mode & VWRITE) && (fmp->mp->mnt_flag & MNT_RDONLY))
		return (EACCES);

	if ((ap->a_mode & VWRITE) != 0)
		mask |= 0x2;

	if ((ap->a_mode & VREAD) != 0)
		mask |= 0x4;

	if ((ap->a_mode & VEXEC) != 0)
		mask |= 0x1;

	fbuf = fb_setup(FUSEFDSIZE, ip->ufs_ino.i_number, FBT_ACCESS, p);
	fbuf->fb_io_mode = mask;

	error = fb_queue(fmp->dev, fbuf);
	if (error) {
		if (error == ENOSYS) {
			fmp->undef_op |= UNDEF_ACCESS;
			pool_put(&fusefs_fbuf_pool, fbuf);
			goto system_check;
		}

		DPRINTF("access error %i\n", error);
		pool_put(&fusefs_fbuf_pool, fbuf);
		return (error);
	}

	pool_put(&fusefs_fbuf_pool, fbuf);
	return (error);

system_check:
	return (vaccess(ap->a_vp->v_type, ip->cached_attrs.va_mode & ALLPERMS,
	    ip->cached_attrs.va_uid, ip->cached_attrs.va_gid, ap->a_mode,
	    ap->a_cred));
}

int
fusefs_getattr(void *v)
{
	struct vop_getattr_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct fusefs_mnt *fmp;
	struct vattr *vap = ap->a_vap;
	struct proc *p = ap->a_p;
	struct fusefs_node *ip;
	struct fusebuf *fbuf;
	int error = 0;

	DPRINTF("fusefs_getattr\n");

	ip = VTOI(vp);
	fmp = (struct fusefs_mnt *)ip->ufs_ino.i_ump;

	if (!fmp->sess_init)
		goto fake;

	fbuf = fb_setup(0, ip->ufs_ino.i_number, FBT_GETATTR, p);

	error = fb_queue(fmp->dev, fbuf);
	if (error) {
		DPRINTF("getattr error\n");
		pool_put(&fusefs_fbuf_pool, fbuf);
		return (error);
	}

	/* check if we got a response */
	if (fbuf->fb_len == 0) {
		pool_put(&fusefs_fbuf_pool, fbuf);
		goto fake;
	}

	update_vattr(fmp->mp, &fbuf->fb_vattr);
	memcpy(vap, &fbuf->fb_vattr, sizeof(*vap));
	memcpy(&ip->cached_attrs, vap, sizeof(*vap));
	pool_put(&fusefs_fbuf_pool, fbuf);
	return (error);
fake:
	bzero(vap, sizeof(*vap));
	vap->va_type = vp->v_type;
	return (0);
}

int
fusefs_setattr(void *v)
{
	struct vop_setattr_args *ap = v;
	struct vattr *vap = ap->a_vap;
	struct vnode *vp = ap->a_vp;
	struct fusefs_node *ip = VTOI(vp);
	struct proc *p = ap->a_p;
	struct fusefs_mnt *fmp;
	struct fusebuf *fbuf;
	struct fb_io *io;
	int error = 0;

	DPRINTF("fusefs_setattr\n");
	fmp = (struct fusefs_mnt *)ip->ufs_ino.i_ump;
	/*
	 * Check for unsettable attributes.
	 */
	if ((vap->va_type != VNON) || (vap->va_nlink != VNOVAL) ||
	    (vap->va_fsid != VNOVAL) || (vap->va_fileid != VNOVAL) ||
	    (vap->va_blocksize != VNOVAL) || (vap->va_rdev != VNOVAL) ||
	    ((int)vap->va_bytes != VNOVAL) || (vap->va_gen != VNOVAL))
		return (EINVAL);

	if (!fmp->sess_init || (fmp->undef_op & UNDEF_SETATTR))
		return (ENXIO);

	fbuf = fb_setup(FUSEFDSIZE + sizeof(*io), ip->ufs_ino.i_number,
	    FBT_SETATTR, p);
	io = fbtod(fbuf, struct fb_io *);
	io->fi_flags = 0;

	if (vap->va_uid != (uid_t)VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY) {
			error = EROFS;
			goto out;
		}
		fbuf->fb_vattr.va_uid = vap->va_uid;
		io->fi_flags |= FUSE_FATTR_UID;
	}

	if (vap->va_gid != (gid_t)VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY) {
			error = EROFS;
			goto out;
		}
		fbuf->fb_vattr.va_gid = vap->va_gid;
		io->fi_flags |= FUSE_FATTR_GID;
	}

	if (vap->va_size != VNOVAL) {
		switch (vp->v_type) {
		case VDIR:
			error = EISDIR;
			goto out;
		case VLNK:
		case VREG:
			if (vp->v_mount->mnt_flag & MNT_RDONLY) {
				error = EROFS;
				goto out;
			}
		default:
			break;
		}

		/*XXX to finish*/
	}

	if (vap->va_atime.tv_sec != VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY) {
			error = EROFS;
			goto out;
		}
		fbuf->fb_vattr.va_atime.tv_sec = vap->va_atime.tv_sec;
		fbuf->fb_vattr.va_atime.tv_nsec = vap->va_atime.tv_nsec;
		io->fi_flags |= FUSE_FATTR_ATIME;
	}

	if (vap->va_mtime.tv_sec != VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY) {
			error = EROFS;
			goto out;
		}
		fbuf->fb_vattr.va_mtime.tv_sec = vap->va_mtime.tv_sec;
		fbuf->fb_vattr.va_mtime.tv_nsec = vap->va_mtime.tv_nsec;
		io->fi_flags |= FUSE_FATTR_MTIME;
	}

	if (vap->va_mode != (mode_t)VNOVAL) {
		if (vp->v_mount->mnt_flag & MNT_RDONLY) {
			error = EROFS;
			goto out;
		}
		fbuf->fb_vattr.va_mode = vap->va_mode & ALLPERMS;
		io->fi_flags |= FUSE_FATTR_MODE;
	}

	if (!io->fi_flags) {
		goto out;
	}

	if (io->fi_flags & FUSE_FATTR_SIZE && vp->v_type == VDIR) {
		error = EISDIR;
		goto out;
	}

	error = fb_queue(fmp->dev, fbuf);

	if (error) {
		if (error == ENOSYS)
			fmp->undef_op |= UNDEF_SETATTR;
		goto out;
	}

	update_vattr(fmp->mp, &fbuf->fb_vattr);
	memcpy(vap, &fbuf->fb_vattr, sizeof(*vap));

out:
	pool_put(&fusefs_fbuf_pool, fbuf);
	return (error);
}

int
fusefs_ioctl(void *v)
{
	DPRINTF("fusefs_ioctl\n");
	return (ENOTTY);
}

int
fusefs_link(void *v)
{
	struct vop_link_args *ap = v;
	struct vnode *dvp = ap->a_dvp;
	struct vnode *vp = ap->a_vp;
	struct componentname *cnp = ap->a_cnp;
	struct proc *p = cnp->cn_proc;
	struct fusefs_mnt *fmp;
	struct fusefs_node *ip;
	struct fusefs_node *dip;
	struct fusebuf *fbuf;
	int error = 0;

	DPRINTF("fusefs_link\n");

	if (vp->v_type == VDIR) {
		VOP_ABORTOP(dvp, cnp);
		error = EISDIR;
		goto out2;
	}
	if (dvp->v_mount != vp->v_mount) {
		VOP_ABORTOP(dvp, cnp);
		error = EXDEV;
		goto out2;
	}
	if (dvp != vp && (error = vn_lock(vp, LK_EXCLUSIVE, p))) {
		VOP_ABORTOP(dvp, cnp);
		goto out2;
	}

	ip = VTOI(vp);
	dip = VTOI(dvp);
	fmp = (struct fusefs_mnt *)ip->ufs_ino.i_ump;

	if (!fmp->sess_init || (fmp->undef_op & UNDEF_LINK))
		goto out1;

	fbuf = fb_setup(FUSEFDSIZE + cnp->cn_namelen + 1,
	    dip->ufs_ino.i_number, FBT_LINK, p);

	fbuf->fb_io_ino = ip->ufs_ino.i_number;
	memcpy(fbuf->fb_dat, cnp->cn_nameptr, cnp->cn_namelen);
	fbuf->fb_dat[cnp->cn_namelen] = '\0';

	error = fb_queue(fmp->dev, fbuf);

	if (error) {
		if (error == ENOSYS)
			fmp->undef_op |= UNDEF_LINK;

		pool_put(&fusefs_fbuf_pool, fbuf);
		goto out1;
	}

	pool_put(&fusefs_fbuf_pool, fbuf);

out1:
	if (dvp != vp)
		VOP_UNLOCK(vp, 0, p);
out2:
	vput(dvp);
	return (error);
}

int
fusefs_symlink(void *v)
{
	struct vop_symlink_args *ap = v;
	struct vnode **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	struct vnode *dvp = ap->a_dvp;
	struct proc *p = cnp->cn_proc;
	char *target = ap->a_target;
	struct fusefs_node *dp;
	struct fusefs_mnt *fmp;
	struct fusebuf *fbuf;
	struct vnode *tdp;
	int error = 0;
	int len;

	DPRINTF("fusefs_symlink\n");
	dp = VTOI(dvp);
	fmp = (struct fusefs_mnt *)dp->ufs_ino.i_ump;

	if (!fmp->sess_init  || (fmp->undef_op & UNDEF_SYMLINK)) {
		error = ENOSYS;
		goto bad;
	}

	len = strlen(target) + 1;

	fbuf = fb_setup(FUSEFDSIZE + len + cnp->cn_namelen + 1,
	    dp->ufs_ino.i_number, FBT_SYMLINK, p);

	memcpy(fbuf->fb_dat, cnp->cn_nameptr, cnp->cn_namelen);
	fbuf->fb_dat[cnp->cn_namelen] = '\0';
	memcpy(&fbuf->fb_dat[cnp->cn_namelen + 1], target, len);

	error = fb_queue(fmp->dev, fbuf);
	if (error) {
		if (error == ENOSYS)
			fmp->undef_op |= UNDEF_SYMLINK;

		pool_put(&fusefs_fbuf_pool, fbuf);
		goto bad;
	}

	if ((error = VFS_VGET(fmp->mp, fbuf->fb_ino, &tdp))) {
		pool_put(&fusefs_fbuf_pool, fbuf);
		goto bad;
	}

	tdp->v_type = VLNK;
	VTOI(tdp)->vtype = tdp->v_type;
	VTOI(tdp)->parent = dp->ufs_ino.i_number;

	*vpp = tdp;
	pool_put(&fusefs_fbuf_pool, fbuf);
	vput(tdp);
bad:
	vput(dvp);
	return (error);
}

int
fusefs_readdir(void *v)
{
	struct vop_readdir_args *ap = v;
	struct fusefs_node *ip;
	struct fusefs_mnt *fmp;
	struct fusebuf *fbuf;
	struct vnode *vp;
	struct proc *p;
	struct uio *uio;
	int error = 0;

	vp = ap->a_vp;
	uio = ap->a_uio;
	p = uio->uio_procp;

	ip = VTOI(vp);
	fmp = (struct fusefs_mnt *)ip->ufs_ino.i_ump;

	if (!fmp->sess_init)
		return (0);

	DPRINTF("fusefs_readdir\n");
	DPRINTF("uio resid 0x%x\n", uio->uio_resid);

	if (uio->uio_resid < sizeof(struct dirent))
		return (EINVAL);

	while (uio->uio_resid > 0) {
		fbuf = fb_setup(FUSEFDSIZE, ip->ufs_ino.i_number, FBT_READDIR,
		    p);

		if (ip->fufh[FUFH_RDONLY].fh_type == FUFH_INVALID) {
			/* TODO open the file */
			pool_put(&fusefs_fbuf_pool, fbuf);
			return (error);
		}
		fbuf->fb_io_fd = ip->fufh[FUFH_RDONLY].fh_id;
		fbuf->fb_io_off = uio->uio_offset;
		fbuf->fb_io_len = MIN(uio->uio_resid, FUSELEN);

		error = fb_queue(fmp->dev, fbuf);

		if (error) {
			pool_put(&fusefs_fbuf_pool, fbuf);
			break;
		}

		/*ack end of readdir */
		if (fbdatsize(fbuf) == 0) {
			pool_put(&fusefs_fbuf_pool, fbuf);
			break;
		}

		if ((error = uiomove(fbuf->fb_dat, fbdatsize(fbuf), uio))) {
			pool_put(&fusefs_fbuf_pool, fbuf);
			break;
		}

		pool_put(&fusefs_fbuf_pool, fbuf);
	}

	return (error);
}

int
fusefs_inactive(void *v)
{
	struct vop_inactive_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct proc *p = ap->a_p;
	struct fusefs_node *ip = VTOI(vp);
	struct fusefs_filehandle *fufh = NULL;
	struct fusefs_mnt *fmp;
	int error = 0;
	int type;

	DPRINTF("fusefs_inactive\n");
	fmp = (struct fusefs_mnt *)ip->ufs_ino.i_ump;

	for (type = 0; type < FUFH_MAXTYPE; type++) {
		fufh = &(ip->fufh[type]);
		if (fufh->fh_type != FUFH_INVALID)
			fusefs_file_close(fmp, ip, fufh->fh_type, type,
			    (ip->vtype == VDIR), ap->a_p);
	}

	VOP_UNLOCK(vp, 0, p);

	/* not sure if it is ok to do like that ...*/
	if (ip->cached_attrs.va_mode == 0)
		vrecycle(vp, p);

	return (error);
}

int
fusefs_readlink(void *v)
{
	struct vop_readlink_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct fusefs_node *ip;
	struct fusefs_mnt *fmp;
	struct fusebuf *fbuf;
	struct uio *uio;
	struct proc *p;
	int error = 0;

	DPRINTF("fusefs_readlink\n");

	ip = VTOI(vp);
	fmp = (struct fusefs_mnt *)ip->ufs_ino.i_ump;
	uio = ap->a_uio;
	p = uio->uio_procp;

	if (!fmp->sess_init || (fmp->undef_op & UNDEF_READLINK)) {
		error = ENOSYS;
		goto out;
	}

	fbuf = fb_setup(0, ip->ufs_ino.i_number, FBT_READLINK, p);

	error = fb_queue(fmp->dev, fbuf);

	if (error) {
		if (error == ENOSYS)
			fmp->undef_op |= UNDEF_READLINK;

		pool_put(&fusefs_fbuf_pool, fbuf);
		goto out;
	}

	error = uiomove(fbuf->fb_dat, fbdatsize(fbuf), uio);
	pool_put(&fusefs_fbuf_pool, fbuf);
out:
	return (error);
}

int
fusefs_reclaim(void *v)
{
	struct vop_reclaim_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct fusefs_node *ip = VTOI(vp);
	struct fusefs_filehandle *fufh = NULL;
	struct fusefs_mnt *fmp;
	int type;

	DPRINTF("fusefs_reclaim\n");
	fmp = (struct fusefs_mnt *)ip->ufs_ino.i_ump;

	/*close opened files*/
	for (type = 0; type < FUFH_MAXTYPE; type++) {
		fufh = &(ip->fufh[type]);
		if (fufh->fh_type != FUFH_INVALID) {
			printf("FUSE: vnode being reclaimed is valid");
			fusefs_file_close(fmp, ip, fufh->fh_type, type,
			    (ip->vtype == VDIR), ap->a_p);
		}
	}
	/*
	 * Purge old data structures associated with the inode.
	 */
	ip->parent = 0;

	/*
	 * Remove the inode from its hash chain.
	 */
	ufs_ihashrem(&ip->ufs_ino);
	cache_purge(vp);

	free(ip, M_FUSEFS);
	vp->v_data = NULL;
	return (0);
}

int
fusefs_print(void *v)
{
	struct vop_print_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct fusefs_node *ip = VTOI(vp);

	/* Complete the information given by vprint(). */
	printf("tag VT_FUSE, hash id %u ", ip->ufs_ino.i_number);
	lockmgr_printinfo(&ip->ufs_ino.i_lock);
	printf("\n");
	return (0);
}

int
fusefs_create(void *v)
{
	struct vop_create_args *ap = v;
	struct componentname *cnp = ap->a_cnp;
	struct vnode **vpp = ap->a_vpp;
	struct vnode *dvp = ap->a_dvp;
	struct vattr *vap = ap->a_vap;
	struct proc *p = cnp->cn_proc;
	struct vnode *tdp = NULL;
	struct fusefs_mnt *fmp;
	struct fusefs_node *ip;
	struct fusebuf *fbuf;
	int error = 0;
	mode_t mode;

	DPRINTF("fusefs_create(cnp %08x, vap %08x\n", cnp, vap);

	ip = VTOI(dvp);
	fmp = (struct fusefs_mnt *)ip->ufs_ino.i_ump;
	mode = MAKEIMODE(vap->va_type, vap->va_mode);

	if (!fmp->sess_init || (fmp->undef_op & UNDEF_CREATE)) {
		error = ENOSYS;
		goto out;
	}

	fbuf = fb_setup(FUSEFDSIZE + cnp->cn_namelen + 1,
	    ip->ufs_ino.i_number, FBT_CREATE, p);

	fbuf->fb_io_mode = mode;
	fbuf->fb_io_flags = O_CREAT | O_RDWR;

	memcpy(fbuf->fb_dat, cnp->cn_nameptr, cnp->cn_namelen);
	fbuf->fb_dat[cnp->cn_namelen] = '\0';

	error = fb_queue(fmp->dev, fbuf);
	if (error) {
		if (error == ENOSYS)
			fmp->undef_op |= UNDEF_CREATE;

		pool_put(&fusefs_fbuf_pool, fbuf);
		goto out;
	}

	if ((error = VFS_VGET(fmp->mp, fbuf->fb_ino, &tdp))) {
		pool_put(&fusefs_fbuf_pool, fbuf);
		goto out;
	}

	tdp->v_type = IFTOVT(fbuf->fb_io_mode);
	VTOI(tdp)->vtype = tdp->v_type;

	if (dvp != NULL && dvp->v_type == VDIR)
		VTOI(tdp)->parent = ip->ufs_ino.i_number;

	*vpp = tdp;
	VN_KNOTE(ap->a_dvp, NOTE_WRITE);
	pool_put(&fusefs_fbuf_pool, fbuf);

out:
	vput(ap->a_dvp);
	return (error);
}

int
fusefs_mknod(void *v)
{
	struct vop_mknod_args *ap = v;

	VN_KNOTE(ap->a_dvp, NOTE_WRITE);
	vput(ap->a_dvp);
	return (EINVAL);
}

int
fusefs_read(void *v)
{
	struct vop_read_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct proc *p = uio->uio_procp;
	struct fusefs_node *ip;
	struct fusefs_mnt *fmp;
	struct fusebuf *fbuf = NULL;
	size_t size;
	int error=0;

	DPRINTF("fusefs_read\n");

	ip = VTOI(vp);
	fmp = (struct fusefs_mnt *)ip->ufs_ino.i_ump;

	DPRINTF("read inode=%i, offset=%llu, resid=%x\n",
	    ip->ufs_ino.i_number, uio->uio_offset, uio->uio_resid);

	if (uio->uio_resid == 0)
		return (error);
	if (uio->uio_offset < 0)
		return (EINVAL);

	while (uio->uio_resid > 0) {
		fbuf = fb_setup(FUSEFDSIZE, ip->ufs_ino.i_number, FBT_READ, p);

		size = MIN(uio->uio_resid, FUSELEN);
		fbuf->fb_io_fd = fusefs_fd_get(ip, FUFH_RDONLY);
		fbuf->fb_io_off = uio->uio_offset;
		fbuf->fb_io_len = size;

		error = fb_queue(fmp->dev, fbuf);

		if (error)
			break;

		error = uiomove(fbuf->fb_dat, MIN(size, fbdatsize(fbuf)), uio);
		if (error)
			break;

		if (fbdatsize(fbuf) < size)
			break;

		pool_put(&fusefs_fbuf_pool, fbuf);
		fbuf = NULL;
	}

	if (fbuf)
		pool_put(&fusefs_fbuf_pool, fbuf);

	return (error);
}

int
fusefs_write(void *v)
{
	struct vop_write_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct uio *uio = ap->a_uio;
	struct proc *p = uio->uio_procp;
	struct fusefs_node *ip;
	struct fusefs_mnt *fmp;
	struct fusebuf *fbuf = NULL;
	size_t len, diff;
	int error=0;

	DPRINTF("fusefs_write\n");

	ip = VTOI(vp);
	fmp = (struct fusefs_mnt *)ip->ufs_ino.i_ump;

	DPRINTF("write inode=%i, offset=%llu, resid=%x\n",
	    ip->ufs_ino.i_number, uio->uio_offset, uio->uio_resid);

	if (uio->uio_resid == 0)
		return (error);

	while (uio->uio_resid > 0) {
		len = MIN(uio->uio_resid, FUSELEN);
		fbuf = fb_setup(FUSEFDSIZE + len, ip->ufs_ino.i_number,
		    FBT_WRITE, p);

		fbuf->fb_io_fd = fusefs_fd_get(ip, FUFH_WRONLY);
		fbuf->fb_io_off = uio->uio_offset;
		fbuf->fb_io_len = len;

		if ((error = uiomove(fbuf->fb_dat, len, uio))) {
			DPRINTF("uio error %i", error);
			break;
		}

		error = fb_queue(fmp->dev, fbuf);

		if (error)
			break;

		diff = len - fbuf->fb_io_len;
		if (diff < 0) {
			error = EINVAL;
			break;
		}

		uio->uio_resid += diff;
		uio->uio_offset -= diff;

		pool_put(&fusefs_fbuf_pool, fbuf);
		fbuf = NULL;
	}

	if (fbuf)
		pool_put(&fusefs_fbuf_pool, fbuf);
	return (error);
}

int
fusefs_poll(void *v)
{
	struct vop_poll_args *ap = v;

	DPRINTF("fusefs_poll\n");

	/*
	 * We should really check to see if I/O is possible.
	 */
	return (ap->a_events & (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM));
}

int
fusefs_rename(void *v)
{
	struct vop_rename_args *ap = v;
	struct vnode *tvp = ap->a_tvp;
	struct vnode *tdvp = ap->a_tdvp;
	struct vnode *fvp = ap->a_fvp;
	struct vnode *fdvp = ap->a_fdvp;
	struct componentname *tcnp = ap->a_tcnp;
	struct componentname *fcnp = ap->a_fcnp;
	struct proc *p = fcnp->cn_proc;
	struct fusefs_node *ip, *dp;
	struct fusefs_mnt *fmp;
	struct fusebuf *fbuf;
	int error = 0;

	DPRINTF("fusefs_rename\n");

#ifdef DIAGNOSTIC
	if ((tcnp->cn_flags & HASBUF) == 0 ||
	    (fcnp->cn_flags & HASBUF) == 0)
		panic("fusefs_rename: no name");
#endif
	/*
	 * Check for cross-device rename.
	 */
	if ((fvp->v_mount != tdvp->v_mount) ||
	    (tvp && (fvp->v_mount != tvp->v_mount))) {
		error = EXDEV;
abortit:
		VOP_ABORTOP(tdvp, tcnp); /* XXX, why not in NFS? */
		if (tdvp == tvp)
			vrele(tdvp);
		else
			vput(tdvp);
		if (tvp)
			vput(tvp);
		VOP_ABORTOP(fdvp, fcnp); /* XXX, why not in NFS? */
		vrele(fdvp);
		vrele(fvp);
		return (error);
	}

	/*
	 * If source and dest are the same, do nothing.
	 */
	if (tvp == fvp) {
		error = 0;
		goto abortit;
	}

	if ((error = vn_lock(fvp, LK_EXCLUSIVE | LK_RETRY, p)) != 0)
		goto abortit;
	dp = VTOI(fdvp);
	ip = VTOI(fvp);
	fmp = (struct fusefs_mnt *)ip->ufs_ino.i_ump;

	/*
	 * Be sure we are not renaming ".", "..", or an alias of ".". This
	 * leads to a crippled directory tree.  It's pretty tough to do a
	 * "ls" or "pwd" with the "." directory entry missing, and "cd .."
	 * doesn't work if the ".." entry is missing.
	 */
	if (ip->vtype == VDIR) {
		/*
		 * Avoid ".", "..", and aliases of "." for obvious reasons.
		 */
		if ((fcnp->cn_namelen == 1 && fcnp->cn_nameptr[0] == '.') ||
		    dp == ip ||
		    (fcnp->cn_flags & ISDOTDOT) ||
		    (tcnp->cn_flags & ISDOTDOT)) {
			VOP_UNLOCK(fvp, 0, p);
			error = EINVAL;
			goto abortit;
		}
	}
	VN_KNOTE(fdvp, NOTE_WRITE);	/* XXX right place? */

	if (!fmp->sess_init || (fmp->undef_op & UNDEF_RENAME)) {
		error = ENOSYS;
		VOP_UNLOCK(fvp, 0, p);
		goto abortit;
	}

	fbuf = fb_setup(FUSEFDSIZE + fcnp->cn_namelen + tcnp->cn_namelen + 2,
	    dp->ufs_ino.i_number, FBT_RENAME, p);

	memcpy(fbuf->fb_dat, fcnp->cn_nameptr, fcnp->cn_namelen);
	fbuf->fb_dat[fcnp->cn_namelen] = '\0';
	memcpy(fbuf->fb_dat + fcnp->cn_namelen + 1, tcnp->cn_nameptr,
	    tcnp->cn_namelen);
	fbuf->fb_dat[fcnp->cn_namelen + tcnp->cn_namelen + 1] = '\0';
	fbuf->fb_io_ino = VTOI(tdvp)->ufs_ino.i_number;

	error = fb_queue(fmp->dev, fbuf);

	if (error) {
		if (error == ENOSYS) {
			fmp->undef_op |= UNDEF_RENAME;
		}

		pool_put(&fusefs_fbuf_pool, fbuf);
		VOP_UNLOCK(fvp, 0, p);
		goto abortit;
	}

	VN_KNOTE(fvp, NOTE_RENAME);

	VOP_UNLOCK(fvp, 0, p);
	if (tdvp == tvp)
		vrele(tdvp);
	else
		vput(tdvp);
	vrele(fdvp);
	vrele(fvp);

	return (error);
}

int
fusefs_mkdir(void *v)
{
	struct vop_mkdir_args *ap = v;
	struct vnode *dvp = ap->a_dvp;
	struct vnode **vpp = ap->a_vpp;
	struct componentname *cnp = ap->a_cnp;
	struct vattr *vap = ap->a_vap;
	struct proc *p = cnp->cn_proc;
	struct vnode *tdp = NULL;
	struct fusefs_node *ip;
	struct fusefs_mnt *fmp;
	struct fusebuf *fbuf;
	int error = 0;

	DPRINTF("fusefs_mkdir %s\n", cnp->cn_nameptr);

	ip = VTOI(dvp);
	fmp = (struct fusefs_mnt *)ip->ufs_ino.i_ump;


	if (!fmp->sess_init || (fmp->undef_op & UNDEF_MKDIR)) {
		error = ENOSYS;
		goto out;
	}

	fbuf = fb_setup(FUSEFDSIZE + cnp->cn_namelen + 1, ip->ufs_ino.i_number,
	    FBT_MKDIR, p);

	fbuf->fb_io_mode = MAKEIMODE(vap->va_type, vap->va_mode);
	memcpy(fbuf->fb_dat, cnp->cn_nameptr, cnp->cn_namelen);
	fbuf->fb_dat[cnp->cn_namelen] = '\0';

	error = fb_queue(fmp->dev, fbuf);
	if (error) {
		if (error == ENOSYS)
			fmp->undef_op |= UNDEF_MKDIR;

		pool_put(&fusefs_fbuf_pool, fbuf);
		goto out;
	}

	if ((error = VFS_VGET(fmp->mp, fbuf->fb_ino, &tdp))) {
		pool_put(&fusefs_fbuf_pool, fbuf);
		goto out;
	}

	tdp->v_type = IFTOVT(fbuf->fb_io_mode);
	VTOI(tdp)->vtype = tdp->v_type;

	if (dvp != NULL && dvp->v_type == VDIR)
		VTOI(tdp)->parent = ip->ufs_ino.i_number;

	*vpp = tdp;
	VN_KNOTE(ap->a_dvp, NOTE_WRITE | NOTE_LINK);
	pool_put(&fusefs_fbuf_pool, fbuf);
out:
	vput(dvp);
	return (error);
}

int
fusefs_rmdir(void *v)
{
	struct vop_rmdir_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp = ap->a_dvp;
	struct componentname *cnp = ap->a_cnp;
	struct proc *p = cnp->cn_proc;
	struct fusefs_node *ip, *dp;
	struct fusefs_mnt *fmp;
	struct fusebuf *fbuf;
	int error;

	DPRINTF("fusefs_rmdir\n");

	ip = VTOI(vp);
	dp = VTOI(dvp);
	fmp = (struct fusefs_mnt *)ip->ufs_ino.i_ump;

	/*
	 * No rmdir "." please.
	 */
	if (dp == ip) {
		vrele(dvp);
		vput(vp);
		return (EINVAL);
	}

	if (!fmp->sess_init || (fmp->undef_op & UNDEF_RMDIR)) {
		error = ENOSYS;
		goto out;
	}

	VN_KNOTE(dvp, NOTE_WRITE | NOTE_LINK);

	fbuf = fb_setup(FUSEFDSIZE + cnp->cn_namelen + 1, dp->ufs_ino.i_number,
	    FBT_RMDIR, p);
	memcpy(fbuf->fb_dat, cnp->cn_nameptr, cnp->cn_namelen);
	fbuf->fb_dat[cnp->cn_namelen] = '\0';

	error = fb_queue(fmp->dev, fbuf);

	if (error) {
		if (error == ENOSYS)
			fmp->undef_op |= UNDEF_RMDIR;
		if (error != ENOTEMPTY)
			VN_KNOTE(dvp, NOTE_WRITE | NOTE_LINK);

		pool_put(&fusefs_fbuf_pool, fbuf);
		goto out;
	}

	VN_KNOTE(dvp, NOTE_WRITE | NOTE_LINK);

	cache_purge(dvp);
	vput(dvp);
	dvp = NULL;

	cache_purge(ITOV(ip));
	pool_put(&fusefs_fbuf_pool, fbuf);
out:
	if (dvp)
		vput(dvp);
	VN_KNOTE(vp, NOTE_DELETE);
	vput(vp);
	return (error);
}

int
fusefs_remove(void *v)
{
	struct vop_remove_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct vnode *dvp = ap->a_dvp;
	struct componentname *cnp = ap->a_cnp;
	struct proc *p = cnp->cn_proc;
	struct fusefs_node *ip;
	struct fusefs_node *dp;
	struct fusefs_mnt *fmp;
	struct fusebuf *fbuf;
	int error = 0;

	DPRINTF("fusefs_remove\n");

	ip = VTOI(vp);
	dp = VTOI(dvp);
	fmp = (struct fusefs_mnt *)ip->ufs_ino.i_ump;

	if (!fmp->sess_init || (fmp->undef_op & UNDEF_REMOVE)) {
		error = ENOSYS;
		goto out;
	}

	fbuf = fb_setup(FUSEFDSIZE + cnp->cn_namelen + 1, dp->ufs_ino.i_number,
	    FBT_UNLINK, p);
	memcpy(fbuf->fb_dat, cnp->cn_nameptr, cnp->cn_namelen);
	fbuf->fb_dat[cnp->cn_namelen] = '\0';

	error = fb_queue(fmp->dev, fbuf);
	if (error) {
		if (error == ENOSYS)
			fmp->undef_op |= UNDEF_REMOVE;

		pool_put(&fusefs_fbuf_pool, fbuf);
		goto out;
	}

	VN_KNOTE(vp, NOTE_DELETE);
	VN_KNOTE(dvp, NOTE_WRITE);
	pool_put(&fusefs_fbuf_pool, fbuf);
out:
	if (dvp == vp)
		vrele(vp);
	else
		vput(vp);
	vput(dvp);
	return (error);
}

int
fusefs_strategy(void *v)
{
	DPRINTF("fusefs_strategy\n");
	return (0);
}

int
fusefs_lock(void *v)
{
	struct vop_lock_args *ap = v;
	struct vnode *vp = ap->a_vp;

	DPRINTF("fusefs_lock\n");
	return (lockmgr(&VTOI(vp)->ufs_ino.i_lock, ap->a_flags, NULL));
}

int
fusefs_unlock(void *v)
{
	struct vop_unlock_args *ap = v;
	struct vnode *vp = ap->a_vp;

	DPRINTF("fusefs_unlock\n");
	return (lockmgr(&VTOI(vp)->ufs_ino.i_lock, ap->a_flags | LK_RELEASE,
	    NULL));
}

int
fusefs_islocked(void *v)
{
	struct vop_islocked_args *ap = v;

	DPRINTF("fusefs_islocked\n");
	return (lockstatus(&VTOI(ap->a_vp)->ufs_ino.i_lock));
}

int
fusefs_advlock(void *v)
{
	struct vop_advlock_args *ap = v;
	struct fusefs_node *ip = VTOI(ap->a_vp);

	DPRINTF("fusefs_advlock\n");
	return (lf_advlock(&ip->ufs_ino.i_lockf, ip->filesize, ap->a_id,
	    ap->a_op, ap->a_fl, ap->a_flags));
}
