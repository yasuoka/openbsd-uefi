/*	$OpenBSD: ntfs_vnops.c,v 1.27 2013/01/03 16:06:01 jsing Exp $	*/
/*	$NetBSD: ntfs_vnops.c,v 1.6 2003/04/10 21:57:26 jdolecek Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * John Heidemann of the UCLA Ficus project.
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
 *	Id: ntfs_vnops.c,v 1.5 1999/05/12 09:43:06 semenu Exp
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/namei.h>
#include <sys/malloc.h>
#include <sys/buf.h>
#include <sys/dirent.h>
#include <sys/specdev.h>

/*#define NTFS_DEBUG 1*/
#include <ntfs/ntfs.h>
#include <ntfs/ntfs_inode.h>
#include <ntfs/ntfs_subr.h>

#include <sys/unistd.h> /* for pathconf(2) constants */

static int	ntfs_read(void *);
static int	ntfs_write(void *);
static int	ntfs_getattr(void *);
static int	ntfs_inactive(void *);
static int	ntfs_print(void *);
static int	ntfs_reclaim(void *);
static int	ntfs_strategy(void *);
static int	ntfs_access(void *);
static int	ntfs_open(void *);
static int	ntfs_close(void *);
static int	ntfs_readdir(void *);
static int	ntfs_lookup(void *);
static int	ntfs_bmap(void *);
static int	ntfs_fsync(void *);
static int	ntfs_pathconf(void *);

int	ntfs_prtactive = 0;	/* 1 => print out reclaim of active vnodes */

/*
 * This is a noop, simply returning what one has been given.
 */
int
ntfs_bmap(void *v)
{
	struct vop_bmap_args *ap = v;
	dprintf(("ntfs_bmap: vn: %p, blk: %d\n", ap->a_vp,(u_int32_t)ap->a_bn));
	if (ap->a_vpp != NULL)
		*ap->a_vpp = ap->a_vp;
	if (ap->a_bnp != NULL)
		*ap->a_bnp = ap->a_bn;
	if (ap->a_runp != NULL)
		*ap->a_runp = 0;
	return (0);
}

static int
ntfs_read(void *v)
{
	struct vop_read_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct fnode *fp = VTOF(vp);
	struct ntnode *ip = FTONT(fp);
	struct uio *uio = ap->a_uio;
	struct ntfsmount *ntmp = ip->i_mp;
	u_int64_t toread;
	int error;

	dprintf(("ntfs_read: ino: %d, off: %d resid: %d, segflg: %d\n",ip->i_number,(u_int32_t)uio->uio_offset,uio->uio_resid,uio->uio_segflg));

	dprintf(("ntfs_read: filesize: %d",(u_int32_t)fp->f_size));

	/* don't allow reading after end of file */
	if (uio->uio_offset > fp->f_size)
		toread = 0;
	else
		toread = MIN(uio->uio_resid, fp->f_size - uio->uio_offset );

	dprintf((", toread: %d\n",(u_int32_t)toread));

	if (toread == 0)
		return (0);

	error = ntfs_readattr(ntmp, ip, fp->f_attrtype,
		fp->f_attrname, uio->uio_offset, toread, NULL, uio);
	if (error) {
		printf("ntfs_read: ntfs_readattr failed: %d\n",error);
		return (error);
	}

	return (0);
}

static int
ntfs_getattr(void *v)
{
	struct vop_getattr_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct fnode *fp = VTOF(vp);
	struct ntnode *ip = FTONT(fp);
	struct vattr *vap = ap->a_vap;

	dprintf(("ntfs_getattr: %d, flags: %d\n",ip->i_number,ip->i_flag));

	vap->va_fsid = ip->i_dev;
	vap->va_fileid = ip->i_number;
	vap->va_mode = ip->i_mp->ntm_mode;
	vap->va_nlink = ip->i_nlink;
	vap->va_uid = ip->i_mp->ntm_uid;
	vap->va_gid = ip->i_mp->ntm_gid;
	vap->va_rdev = 0;				/* XXX UNODEV ? */
	vap->va_size = fp->f_size;
	vap->va_bytes = fp->f_allocated;
	vap->va_atime = ntfs_nttimetounix(fp->f_times.t_access);
	vap->va_mtime = ntfs_nttimetounix(fp->f_times.t_write);
	vap->va_ctime = ntfs_nttimetounix(fp->f_times.t_create);
	vap->va_flags = ip->i_flag;
	vap->va_gen = 0;
	vap->va_blocksize = ip->i_mp->ntm_spc * ip->i_mp->ntm_bps;
	vap->va_type = vp->v_type;
	vap->va_filerev = 0;

	/*
	 * Ensure that a directory link count is always 1 so that things
	 * like fts_read() do not try to be smart and end up skipping over
	 * directories. Additionally, ip->i_nlink will not be initialised
	 * until the ntnode has been loaded for the file.
	 */
	if (vp->v_type == VDIR || ip->i_nlink < 1)
		vap->va_nlink = 1;

	return (0);
}


/*
 * Last reference to an ntnode.  If necessary, write or delete it.
 */
int
ntfs_inactive(void *v)
{
	struct vop_inactive_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct proc *p = ap->a_p;
#ifdef NTFS_DEBUG
	struct ntnode *ip = VTONT(vp);
#endif

	dprintf(("ntfs_inactive: vnode: %p, ntnode: %d\n", vp, ip->i_number));

#ifdef DIAGNOSTIC
	if (ntfs_prtactive && vp->v_usecount != 0)
		vprint("ntfs_inactive: pushing active", vp);
#endif

	VOP_UNLOCK(vp, 0, p);

	/* XXX since we don't support any filesystem changes
	 * right now, nothing more needs to be done
	 */
	return (0);
}

/*
 * Reclaim an fnode/ntnode so that it can be used for other purposes.
 */
int
ntfs_reclaim(void *v)
{
	struct vop_reclaim_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct fnode *fp = VTOF(vp);
	struct ntnode *ip = FTONT(fp);
	struct proc *p = ap->a_p;
	int error;

	dprintf(("ntfs_reclaim: vnode: %p, ntnode: %d\n", vp, ip->i_number));

#ifdef DIAGNOSTIC
	if (ntfs_prtactive && vp->v_usecount != 0)
		vprint("ntfs_reclaim: pushing active", vp);
#endif

	if ((error = ntfs_ntget(ip, p)) != 0)
		return (error);
	
	/* Purge old data structures associated with the inode. */
	cache_purge(vp);

	ntfs_frele(fp);
	ntfs_ntput(ip, p);

	vp->v_data = NULL;

	return (0);
}

static int
ntfs_print(void *v)
{
	struct vop_print_args *ap = v;
	struct ntnode *ip = VTONT(ap->a_vp);

	printf("tag VT_NTFS, ino %u, flag %#x, usecount %d, nlink %ld\n",
	    ip->i_number, ip->i_flag, ip->i_usecount, ip->i_nlink);

	return (0);
}

/*
 * Calculate the logical to physical mapping if not done already,
 * then call the device strategy routine.
 */
int
ntfs_strategy(void *v)
{
	struct vop_strategy_args *ap = v;
	struct buf *bp = ap->a_bp;
	struct vnode *vp = bp->b_vp;
	struct fnode *fp = VTOF(vp);
	struct ntnode *ip = FTONT(fp);
	struct ntfsmount *ntmp = ip->i_mp;
	int error, s;

	dprintf(("ntfs_strategy: blkno: %d, lblkno: %d\n",
		(u_int32_t)bp->b_blkno,
		(u_int32_t)bp->b_lblkno));

	dprintf(("strategy: bcount: %u flags: 0x%x\n", 
		(u_int32_t)bp->b_bcount,bp->b_flags));

	if (bp->b_flags & B_READ) {
		u_int32_t toread;

		if (ntfs_cntob(bp->b_blkno) >= fp->f_size) {
			clrbuf(bp);
			error = 0;
		} else {
			toread = MIN(bp->b_bcount,
				 fp->f_size - ntfs_cntob(bp->b_blkno));
			dprintf(("ntfs_strategy: toread: %d, fsize: %d\n",
				toread,(u_int32_t)fp->f_size));

			error = ntfs_readattr(ntmp, ip, fp->f_attrtype,
				fp->f_attrname, ntfs_cntob(bp->b_blkno),
				toread, bp->b_data, NULL);

			if (error) {
				printf("ntfs_strategy: ntfs_readattr failed\n");
				bp->b_error = error;
				bp->b_flags |= B_ERROR;
			}

			bzero(bp->b_data + toread, bp->b_bcount - toread);
		}
	} else {
		size_t tmp;
		u_int32_t towrite;

		if (ntfs_cntob(bp->b_blkno) + bp->b_bcount >= fp->f_size) {
			printf("ntfs_strategy: CAN'T EXTEND FILE\n");
			bp->b_error = error = EFBIG;
			bp->b_flags |= B_ERROR;
		} else {
			towrite = MIN(bp->b_bcount,
				fp->f_size - ntfs_cntob(bp->b_blkno));
			dprintf(("ntfs_strategy: towrite: %d, fsize: %d\n",
				towrite,(u_int32_t)fp->f_size));

			error = ntfs_writeattr_plain(ntmp, ip, fp->f_attrtype,	
				fp->f_attrname, ntfs_cntob(bp->b_blkno),towrite,
				bp->b_data, &tmp, NULL);

			if (error) {
				printf("ntfs_strategy: ntfs_writeattr fail\n");
				bp->b_error = error;
				bp->b_flags |= B_ERROR;
			}
		}
	}
	s = splbio();
	biodone(bp);
	splx(s);
	return (error);
}

static int
ntfs_write(void *v)
{
	struct vop_write_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct fnode *fp = VTOF(vp);
	struct ntnode *ip = FTONT(fp);
	struct uio *uio = ap->a_uio;
	struct ntfsmount *ntmp = ip->i_mp;
	u_int64_t towrite;
	size_t written;
	int error;

	dprintf(("ntfs_write: ino: %d, off: %d resid: %d, segflg: %d\n",ip->i_number,(u_int32_t)uio->uio_offset,uio->uio_resid,uio->uio_segflg));
	dprintf(("ntfs_write: filesize: %d",(u_int32_t)fp->f_size));

	if (uio->uio_resid + uio->uio_offset > fp->f_size) {
		printf("ntfs_write: CAN'T WRITE BEYOND END OF FILE\n");
		return (EFBIG);
	}

	towrite = MIN(uio->uio_resid, fp->f_size - uio->uio_offset);

	dprintf((", towrite: %d\n",(u_int32_t)towrite));

	error = ntfs_writeattr_plain(ntmp, ip, fp->f_attrtype,
		fp->f_attrname, uio->uio_offset, towrite, NULL, &written, uio);
#ifdef NTFS_DEBUG
	if (error)
		printf("ntfs_write: ntfs_writeattr failed: %d\n", error);
#endif

	return (error);
}

int
ntfs_access(void *v)
{
	struct vop_access_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct ntnode *ip = VTONT(vp);
	struct ucred *cred = ap->a_cred;
	mode_t mask, mode = ap->a_mode;
	gid_t *gp;
	int i;

	dprintf(("ntfs_access: %d\n",ip->i_number));

	/*
	 * Disallow write attempts on read-only file systems;
	 * unless the file is a socket, fifo, or a block or
	 * character device resident on the file system.
	 */
	if (mode & VWRITE) {
		switch ((int)vp->v_type) {
		case VDIR:
		case VLNK:
		case VREG:
			if (vp->v_mount->mnt_flag & MNT_RDONLY)
				return (EROFS);
			break;
		}
	}

	/* Otherwise, user id 0 always gets access. */
	if (cred->cr_uid == 0)
		return (0);

	mask = 0;

	/* Otherwise, check the owner. */
	if (cred->cr_uid == ip->i_mp->ntm_uid) {
		if (mode & VEXEC)
			mask |= S_IXUSR;
		if (mode & VREAD)
			mask |= S_IRUSR;
		if (mode & VWRITE)
			mask |= S_IWUSR;
		return ((ip->i_mp->ntm_mode & mask) == mask ? 0 : EACCES);
	}

	/* Otherwise, check the groups. */
	for (i = 0, gp = cred->cr_groups; i < cred->cr_ngroups; i++, gp++)
		if (ip->i_mp->ntm_gid == *gp) {
			if (mode & VEXEC)
				mask |= S_IXGRP;
			if (mode & VREAD)
				mask |= S_IRGRP;
			if (mode & VWRITE)
				mask |= S_IWGRP;
			return ((ip->i_mp->ntm_mode&mask) == mask ? 0 : EACCES);
		}

	/* Otherwise, check everyone else. */
	if (mode & VEXEC)
		mask |= S_IXOTH;
	if (mode & VREAD)
		mask |= S_IROTH;
	if (mode & VWRITE)
		mask |= S_IWOTH;
	return ((ip->i_mp->ntm_mode & mask) == mask ? 0 : EACCES);
}

/*
 * Open called.
 *
 * Nothing to do.
 */
/* ARGSUSED */
static int
ntfs_open(void *v)
{
#if NTFS_DEBUG
	struct vop_open_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct ntnode *ip = VTONT(vp);

	printf("ntfs_open: %d\n",ip->i_number);
#endif

	/*
	 * Files marked append-only must be opened for appending.
	 */

	return (0);
}

/*
 * Close called.
 *
 * Update the times on the inode.
 */
/* ARGSUSED */
static int
ntfs_close(void *v)
{
#if NTFS_DEBUG
	struct vop_close_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct ntnode *ip = VTONT(vp);

	printf("ntfs_close: %d\n",ip->i_number);
#endif

	return (0);
}

int
ntfs_readdir(void *v)
{
	struct vop_readdir_args *ap = v;
	struct vnode *vp = ap->a_vp;
	struct fnode *fp = VTOF(vp);
	struct ntnode *ip = FTONT(fp);
	struct uio *uio = ap->a_uio;
	struct ntfsmount *ntmp = ip->i_mp;
	int i, error = 0;
	u_int32_t faked = 0, num;
	int ncookies = 0;
	struct dirent *cde;
	off_t off;

	dprintf(("ntfs_readdir %d off: %d resid: %d\n",ip->i_number,(u_int32_t)uio->uio_offset,uio->uio_resid));

	off = uio->uio_offset;

	cde = malloc(sizeof(struct dirent), M_TEMP, M_WAITOK);

	/* Simulate . in every dir except ROOT */
	if (ip->i_number != NTFS_ROOTINO
	    && uio->uio_offset < sizeof(struct dirent)) {
		cde->d_fileno = ip->i_number;
		cde->d_reclen = sizeof(struct dirent);
		cde->d_type = DT_DIR;
		cde->d_namlen = 1;
		strncpy(cde->d_name, ".", 2);
		error = uiomove((void *)cde, sizeof(struct dirent), uio);
		if (error)
			goto out;

		ncookies++;
	}

	/* Simulate .. in every dir including ROOT */
	if (uio->uio_offset < 2 * sizeof(struct dirent)) {
		cde->d_fileno = NTFS_ROOTINO;	/* XXX */
		cde->d_reclen = sizeof(struct dirent);
		cde->d_type = DT_DIR;
		cde->d_namlen = 2;
		strncpy(cde->d_name, "..", 3);

		error = uiomove((void *) cde, sizeof(struct dirent), uio);
		if (error)
			goto out;

		ncookies++;
	}

	faked = (ip->i_number == NTFS_ROOTINO) ? 1 : 2;
	num = uio->uio_offset / sizeof(struct dirent) - faked;

	while (uio->uio_resid >= sizeof(struct dirent)) {
		struct attr_indexentry *iep;
		char *fname;
		size_t remains;
		int sz;

		error = ntfs_ntreaddir(ntmp, fp, num, &iep, uio->uio_procp);
		if (error)
			goto out;

		if (NULL == iep)
			break;

		for(; !(iep->ie_flag & NTFS_IEFLAG_LAST) && (uio->uio_resid >= sizeof(struct dirent));
			iep = NTFS_NEXTREC(iep, struct attr_indexentry *))
		{
			if(!ntfs_isnamepermitted(ntmp,iep))
				continue;

			remains = sizeof(cde->d_name) - 1;
			fname = cde->d_name;
			for(i=0; i<iep->ie_fnamelen; i++) {
				sz = (*ntmp->ntm_wput)(fname, remains,
						iep->ie_fname[i]);
				fname += sz;
				remains -= sz;
			}
			*fname = '\0';
			dprintf(("ntfs_readdir: elem: %d, fname:[%s] type: %d, flag: %d, ",
				num, cde->d_name, iep->ie_fnametype,
				iep->ie_flag));
			cde->d_namlen = fname - (char *) cde->d_name;
			cde->d_fileno = iep->ie_number;
			cde->d_type = (iep->ie_fflag & NTFS_FFLAG_DIR) ? DT_DIR : DT_REG;
			cde->d_reclen = sizeof(struct dirent);
			dprintf(("%s\n", (cde->d_type == DT_DIR) ? "dir":"reg"));

			error = uiomove((void *)cde, sizeof(struct dirent), uio);
			if (error)
				goto out;

			ncookies++;
			num++;
		}
	}

	dprintf(("ntfs_readdir: %d entries (%d bytes) read\n",
		ncookies,(u_int)(uio->uio_offset - off)));
	dprintf(("ntfs_readdir: off: %d resid: %d\n",
		(u_int32_t)uio->uio_offset,uio->uio_resid));

	if (!error && ap->a_ncookies != NULL) {
		struct dirent* dpStart;
		struct dirent* dp;
		u_long *cookies;
		u_long *cookiep;

		dprintf(("ntfs_readdir: %d cookies\n",ncookies));
		if (uio->uio_segflg != UIO_SYSSPACE || uio->uio_iovcnt != 1)
			panic("ntfs_readdir: unexpected uio from NFS server");
		dpStart = (struct dirent *)
		     ((caddr_t)uio->uio_iov->iov_base -
			 (uio->uio_offset - off));
		cookies = malloc(ncookies * sizeof(*cookies), M_TEMP, M_WAITOK);
		for (dp = dpStart, cookiep = cookies, i=0;
		     i < ncookies;
		     dp = (struct dirent *)((caddr_t) dp + dp->d_reclen), i++) {
			off += dp->d_reclen;
			*cookiep++ = off;
		}
		*ap->a_ncookies = ncookies;
		*ap->a_cookies = cookies;
	}
/*
	if (ap->a_eofflag)
	    *ap->a_eofflag = VTONT(ap->a_vp)->i_size <= uio->uio_offset;
*/
    out:
	free(cde, M_TEMP);
	return (error);
}

int
ntfs_lookup(void *v)
{
	struct vop_lookup_args *ap = v;
	struct vnode *dvp = ap->a_dvp;
	struct ntnode *dip = VTONT(dvp);
	struct ntfsmount *ntmp = dip->i_mp;
	struct componentname *cnp = ap->a_cnp;
	struct ucred *cred = cnp->cn_cred;
	int error;
	int lockparent = cnp->cn_flags & LOCKPARENT;
	struct proc *p = cnp->cn_proc;
#if NTFS_DEBUG
	int wantparent = cnp->cn_flags & (LOCKPARENT|WANTPARENT);
#endif
	dprintf(("ntfs_lookup: \"%.*s\" (%ld bytes) in %d, lp: %d, wp: %d \n",
		(int)cnp->cn_namelen, cnp->cn_nameptr, cnp->cn_namelen,
		dip->i_number, lockparent, wantparent));

	error = VOP_ACCESS(dvp, VEXEC, cred, cnp->cn_proc);
	if(error)
		return (error);

	if ((cnp->cn_flags & ISLASTCN) &&
	    (dvp->v_mount->mnt_flag & MNT_RDONLY) &&
	    (cnp->cn_nameiop == DELETE || cnp->cn_nameiop == RENAME))
		return (EROFS);

	/*
	 * We now have a segment name to search for, and a directory
	 * to search.
	 *
	 * Before tediously performing a linear scan of the directory,
	 * check the name cache to see if the directory/name pair
	 * we are looking for is known already.
	 */
	if ((error = cache_lookup(ap->a_dvp, ap->a_vpp, cnp)) >= 0)
		return (error);

	if(cnp->cn_namelen == 1 && cnp->cn_nameptr[0] == '.') {
		dprintf(("ntfs_lookup: faking . directory in %d\n",
			dip->i_number));

		vref(dvp);
		*ap->a_vpp = dvp;
		error = 0;
	} else if (cnp->cn_flags & ISDOTDOT) {
		struct ntvattr *vap;

		dprintf(("ntfs_lookup: faking .. directory in %d\n",
			 dip->i_number));

		VOP_UNLOCK(dvp, 0, p);
		cnp->cn_flags |= PDIRUNLOCK;

		error = ntfs_ntvattrget(ntmp, dip, NTFS_A_NAME, NULL, 0, &vap);
		if(error)
			return (error);

		dprintf(("ntfs_lookup: parentdir: %d\n",
			 vap->va_a_name->n_pnumber));
		error = VFS_VGET(ntmp->ntm_mountp,
				 vap->va_a_name->n_pnumber,ap->a_vpp); 
		ntfs_ntvattrrele(vap);
		if (error) {
			if (vn_lock(dvp, LK_EXCLUSIVE | LK_RETRY, p) == 0)
				cnp->cn_flags &= ~PDIRUNLOCK;
			return (error);
		}

		if (lockparent && (cnp->cn_flags & ISLASTCN)) {
			error = vn_lock(dvp, LK_EXCLUSIVE, p);
			if (error) {
				vput( *(ap->a_vpp) );
				return (error);
			}
			cnp->cn_flags &= ~PDIRUNLOCK;
		}
	} else {
		error = ntfs_ntlookupfile(ntmp, dvp, cnp, ap->a_vpp, p);
		if (error) {
			dprintf(("ntfs_ntlookupfile: returned %d\n", error));
			return (error);
		}

		dprintf(("ntfs_lookup: found ino: %d\n", 
			VTONT(*ap->a_vpp)->i_number));

		if(!lockparent || (cnp->cn_flags & ISLASTCN) == 0) {
			VOP_UNLOCK(dvp, 0, p);
			cnp->cn_flags |= PDIRUNLOCK;
		}
	}

	if (cnp->cn_flags & MAKEENTRY)
		cache_enter(dvp, *ap->a_vpp, cnp);

	return (error);
}

/*
 * Flush the blocks of a file to disk.
 *
 * This function is worthless for vnodes that represent directories. Maybe we
 * could just do a sync if they try an fsync on a directory file.
 */
static int
ntfs_fsync(void *v)
{
	return (0);
}

/*
 * Return POSIX pathconf information applicable to NTFS filesystem
 */
static int
ntfs_pathconf(void *v)
{
	struct vop_pathconf_args *ap = v;
	int error = 0;

	switch (ap->a_name) {
	case _PC_LINK_MAX:
		*ap->a_retval = 1;
		break;
	case _PC_NAME_MAX:
		*ap->a_retval = NTFS_MAXFILENAME;
		break;
	case _PC_PATH_MAX:
		*ap->a_retval = PATH_MAX;
		break;
	case _PC_CHOWN_RESTRICTED:
		*ap->a_retval = 1;
		break;
	case _PC_NO_TRUNC:
		*ap->a_retval = 0;
		break;
	default:
		error = EINVAL;
		break;
	}

	return (error);
}

/*
 * Global vfs data structures
 */
struct vops ntfs_vops = {
	.vop_getattr	= ntfs_getattr,
	.vop_inactive	= ntfs_inactive,
	.vop_reclaim	= ntfs_reclaim,
	.vop_print	= ntfs_print,
	.vop_pathconf	= ntfs_pathconf,
	.vop_islocked	= vop_generic_islocked,
	.vop_unlock	= vop_generic_unlock,
	.vop_lock	= vop_generic_lock,
	.vop_lookup	= ntfs_lookup,
	.vop_access	= ntfs_access,
	.vop_close	= ntfs_close,
	.vop_open	= ntfs_open,
	.vop_readdir	= ntfs_readdir,
	.vop_fsync	= ntfs_fsync,
	.vop_bmap	= ntfs_bmap,
	.vop_strategy	= ntfs_strategy,
	.vop_bwrite	= vop_generic_bwrite,
	.vop_read	= ntfs_read,
	.vop_write	= ntfs_write,
};
