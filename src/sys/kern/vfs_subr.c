/*	$OpenBSD: vfs_subr.c,v 1.16 1997/11/24 22:42:38 niklas Exp $	*/
/*	$NetBSD: vfs_subr.c,v 1.53 1996/04/22 01:39:13 christos Exp $	*/

/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
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
 *	@(#)vfs_subr.c	8.13 (Berkeley) 4/18/94
 */

/*
 * External virtual filesystem routines
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/time.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/vnode.h>
#include <sys/stat.h>
#include <sys/namei.h>
#include <sys/ucred.h>
#include <sys/buf.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/domain.h>
#include <sys/mbuf.h>
#include <sys/syscallargs.h>

#include <vm/vm.h>
#include <sys/sysctl.h>

#include <miscfs/specfs/specdev.h>

enum vtype iftovt_tab[16] = {
	VNON, VFIFO, VCHR, VNON, VDIR, VNON, VBLK, VNON,
	VREG, VNON, VLNK, VNON, VSOCK, VNON, VNON, VBAD,
};
int	vttoif_tab[9] = {
	0, S_IFREG, S_IFDIR, S_IFBLK, S_IFCHR, S_IFLNK,
	S_IFSOCK, S_IFIFO, S_IFMT,
};

int doforce = 1;		/* 1 => permit forcible unmounting */
int prtactive = 0;		/* 1 => print out reclaim of active vnodes */
int suid_clear = 1;		/* 1 => clear SUID / SGID on owner change */

/*
 * Insq/Remq for the vnode usage lists.
 */
#define	bufinsvn(bp, dp)	LIST_INSERT_HEAD(dp, bp, b_vnbufs)
#define	bufremvn(bp) {							\
	LIST_REMOVE(bp, b_vnbufs);					\
	(bp)->b_vnbufs.le_next = NOLIST;				\
}  

struct freelst vnode_hold_list;   /* list of vnodes referencing buffers */
struct freelst vnode_free_list;   /* vnode free list */

struct mntlist mountlist;			/* mounted filesystem list */
struct simplelock mountlist_slock;
static struct simplelock mntid_slock;
struct simplelock mntvnode_slock;
struct simplelock vnode_free_list_slock;
static struct simplelock spechash_slock;

/*
 * The workitem queue.
  */ 
#define SYNCER_MAXDELAY       32
int syncer_maxdelay = SYNCER_MAXDELAY;        /* maximum delay time */
time_t syncdelay = 30;                        /* time to delay syncing vnodes */
 
static int syncer_delayno = 0;
static long syncer_mask;
LIST_HEAD(synclist, vnode);
static struct synclist *syncer_workitem_pending;

int vfs_lock __P((struct mount *));
void vfs_unlock __P((struct mount *));
void insmntque __P((struct vnode *, struct mount *));
int getdevvp __P((dev_t, struct vnode **, enum vtype));
int vunref __P((struct vnode *));

int vfs_hang_addrlist __P((struct mount *, struct netexport *,
				  struct export_args *));
int vfs_free_netcred __P((struct radix_node *, void *));
void vfs_free_addrlist __P((struct netexport *));

#ifdef DEBUG
void printlockedvnodes __P((void));
#endif

/*
 * Initialize the vnode management data structures.
 */
void
vntblinit()
{

	simple_lock_init(&mntvnode_slock);
	simple_lock_init(&mntid_slock);
	simple_lock_init(&spechash_slock);
	TAILQ_INIT(&vnode_hold_list);
	TAILQ_INIT(&vnode_free_list);
	simple_lock_init(&vnode_free_list_slock);
	CIRCLEQ_INIT(&mountlist);
	/*
	 * Initialize the filesystem syncer.
	 */
	syncer_workitem_pending = hashinit(syncer_maxdelay, M_VNODE,
					   &syncer_mask);
	syncer_maxdelay = syncer_mask + 1;

}


/*
 * Mark a mount point as busy. Used to synchornize access and to delay
 * unmounting. Interlock is not released n failure.
 */

int
vfs_busy(mp, flags, interlkp, p)
	struct mount *mp;
	int flags;
	struct simplelock *interlkp;
	struct proc *p;
{
	int lkflags;

	if (mp->mnt_flag & MNT_UNMOUNT) {
		if (flags & LK_NOWAIT)
			return (ENOENT);
		mp->mnt_flag |= MNT_MWAIT;
		if (interlkp)
			simple_unlock(interlkp);
		/*
		 * Since all busy locks are shared except the exclusive
		 * lock granted when unmounting, the only place that a
		 * wakeup needs to be done is at the release of the
		 * exclusive lock at the end of dounmount.
		 */
 		sleep((caddr_t)mp, PVFS);
		if (interlkp)
			simple_lock(interlkp);
		return (ENOENT);
	}
	lkflags = LK_SHARED;
	if (interlkp)
		lkflags |= LK_INTERLOCK;
	if (lockmgr(&mp->mnt_lock, lkflags, interlkp, p))
		panic("vfs_busy: unexpected lock failure");
        return (0);
}


/*
 * Free a busy file system
 */
void
vfs_unbusy(mp, p)
	struct mount *mp;
	struct proc *p;
{
	lockmgr(&mp->mnt_lock, LK_RELEASE, NULL, p);
}

/*
 * Lookup a filesystem type, and if found allocate and initialize
 * a mount structure for it.
 *
 * Devname is usually updated by mount(8) after booting.
 */

int
vfs_rootmountalloc(fstypename, devname, mpp)
	char *fstypename;
	char *devname;
	struct mount **mpp;
 {
	struct proc *p = curproc;	/* XXX */
	struct vfsconf *vfsp;
	struct mount *mp;
 
	for (vfsp = vfsconf; vfsp; vfsp = vfsp->vfc_next)
		if (!strcmp(vfsp->vfc_name, fstypename))
			break;
	if (vfsp == NULL)
		return (ENODEV);
	mp = malloc((u_long)sizeof(struct mount), M_MOUNT, M_WAITOK);
	bzero((char *)mp, (u_long)sizeof(struct mount));
	lockinit(&mp->mnt_lock, PVFS, "vfslock", 0, 0);
	(void)vfs_busy(mp, LK_NOWAIT, 0, p);
	LIST_INIT(&mp->mnt_vnodelist);
	mp->mnt_vfc = vfsp;
	mp->mnt_op = vfsp->vfc_vfsops;
	mp->mnt_flag = MNT_RDONLY;
	mp->mnt_vnodecovered = NULLVP;
	vfsp->vfc_refcount++;
	mp->mnt_stat.f_type = vfsp->vfc_typenum;
	mp->mnt_flag |= vfsp->vfc_flags & MNT_VISFLAGMASK;
	strncpy(mp->mnt_stat.f_fstypename, vfsp->vfc_name, MFSNAMELEN);
	mp->mnt_stat.f_mntonname[0] = '/';
	(void) copystr(devname, mp->mnt_stat.f_mntfromname, MNAMELEN - 1, 0);
	*mpp = mp;
 	return (0);
 }

/*
 * Find an appropriate filesystem to use for the root. If a filesystem
 * has not been preselected, walk through the list of known filesystems
 * trying those that have mountroot routines, and try them until one
 * works or we have tried them all.
  */
int
vfs_mountroot()
{
	struct vfsconf *vfsp;
	extern int (*mountroot)(void);
	int error;
 
	if (mountroot != NULL)
		return ((*mountroot)());
	for (vfsp = vfsconf; vfsp; vfsp = vfsp->vfc_next) {
		if (vfsp->vfc_mountroot == NULL)
			continue;
		if ((error = (*vfsp->vfc_mountroot)()) == 0)
			return (0);
		printf("%s_mountroot failed: %d\n", vfsp->vfc_name, error);
 	}
	return (ENODEV);
}
 
/*
 * Lookup a mount point by filesystem identifier.
 */
struct mount *
vfs_getvfs(fsid)
	fsid_t *fsid;
{
	register struct mount *mp;

	simple_lock(&mountlist_slock);
	for (mp = mountlist.cqh_first; mp != (void *)&mountlist;
	     mp = mp->mnt_list.cqe_next) {
		if (mp->mnt_stat.f_fsid.val[0] == fsid->val[0] &&
		    mp->mnt_stat.f_fsid.val[1] == fsid->val[1]) {
			simple_unlock(&mountlist_slock);
			return (mp);
		}
	}
	simple_unlock(&mountlist_slock);
	return ((struct mount *)0);
}


/*
 * Get a new unique fsid
 */
void
vfs_getnewfsid(mp)
	struct mount *mp;
{
	static u_short xxxfs_mntid;

	fsid_t tfsid;
	int mtype;

	simple_lock(&mntid_slock);
	mtype = mp->mnt_vfc->vfc_typenum;
	mp->mnt_stat.f_fsid.val[0] = makedev(nblkdev + mtype, 0);
	mp->mnt_stat.f_fsid.val[1] = mtype;
	if (xxxfs_mntid == 0)
		++xxxfs_mntid;
	tfsid.val[0] = makedev(nblkdev + mtype, xxxfs_mntid);
	tfsid.val[1] = mtype;
	if (mountlist.cqh_first != (void *)&mountlist) {
		while (vfs_getvfs(&tfsid)) {
			tfsid.val[0]++;
			xxxfs_mntid++;
		}
	}
	mp->mnt_stat.f_fsid.val[0] = tfsid.val[0];
	simple_unlock(&mntid_slock);
}

/*
 * Make a 'unique' number from a mount type name.
 * Note that this is no longer used for ffs which
 * now has an on-disk filesystem id.
 */
long
makefstype(type)
	char *type;
{
	long rv;

	for (rv = 0; *type; type++) {
		rv <<= 2;
		rv ^= *type;
	}
	return rv;
}

/*
 * Set vnode attributes to VNOVAL
 */
void
vattr_null(vap)
	register struct vattr *vap;
{

	vap->va_type = VNON;
	/* XXX These next two used to be one line, but for a GCC bug. */
	vap->va_size = VNOVAL;
	vap->va_bytes = VNOVAL;
	vap->va_mode = vap->va_nlink = vap->va_uid = vap->va_gid =
		vap->va_fsid = vap->va_fileid =
		vap->va_blocksize = vap->va_rdev =
		vap->va_atime.tv_sec = vap->va_atime.tv_nsec =
		vap->va_mtime.tv_sec = vap->va_mtime.tv_nsec =
		vap->va_ctime.tv_sec = vap->va_ctime.tv_nsec =
		vap->va_flags = vap->va_gen = VNOVAL;
	vap->va_vaflags = 0;
}

/*
 * Routines having to do with the management of the vnode table.
 */
extern int (**dead_vnodeop_p) __P((void *));
long numvnodes;

/*
 * Return the next vnode from the free list.
 */
int
getnewvnode(tag, mp, vops, vpp)
	enum vtagtype tag;
	struct mount *mp;
	int (**vops) __P((void *));
	struct vnode **vpp;
{
	struct proc *p = curproc;			/* XXX */
	struct freelst *listhd;
	static int toggle;
	struct vnode *vp;
#ifdef DIAGNOSTIC
	int s;
#endif

	/*
	 * We must choose whether to allocate a new vnode or recycle an
	 * existing one. The criterion for allocating a new one is that
	 * the total number of vnodes is less than the number desired or
	 * there are no vnodes on either free list. Generally we only
	 * want to recycle vnodes that have no buffers associated with
	 * them, so we look first on the vnode_free_list. If it is empty,
	 * we next consider vnodes with referencing buffers on the
	 * vnode_hold_list. The toggle ensures that half the time we
	 * will use a buffer from the vnode_hold_list, and half the time
	 * we will allocate a new one unless the list has grown to twice
	 * the desired size. We are reticent to recycle vnodes from the
	 * vnode_hold_list because we will lose the identity of all its
	 * referencing buffers.
	 */
	toggle ^= 1;
	if (numvnodes > 2 * desiredvnodes)
		toggle = 0;


	simple_lock(&vnode_free_list_slock);
	if ((numvnodes < desiredvnodes) ||
	    ((TAILQ_FIRST(listhd = &vnode_free_list) == NULL) &&
	     ((TAILQ_FIRST(listhd = &vnode_hold_list) == NULL) || toggle))) {
		simple_unlock(&vnode_free_list_slock);
		vp = (struct vnode *)malloc((u_long)sizeof *vp,
		    M_VNODE, M_WAITOK);
		bzero((char *)vp, sizeof *vp);
		numvnodes++;
	} else {
		for (vp = TAILQ_FIRST(listhd); vp != NULLVP;
		     vp = TAILQ_NEXT(vp, v_freelist)) {
			if (simple_lock_try(&vp->v_interlock))
				break;
		}
		/*
		 * Unless this is a bad time of the month, at most
		 * the first NCPUS items on the free list are
		 * locked, so this is close enough to being empty.
		 */
		if (vp == NULLVP) {
			simple_unlock(&vnode_free_list_slock);
			tablefull("vnode");
			*vpp = 0;
			return (ENFILE);
		}
		if (vp->v_usecount) {
			vprint("free vnode", vp);
			panic("free vnode isn't");
		}
		TAILQ_REMOVE(listhd, vp, v_freelist);
		/* see comment on why 0xdeadb is set at end of vgone (below) */
		vp->v_flag |= VGONEHACK;
		simple_unlock(&vnode_free_list_slock);
		vp->v_lease = NULL;
		if (vp->v_type != VBAD)
			vgonel(vp, p);
		else
			simple_unlock(&vp->v_interlock);
#ifdef DIAGNOSTIC
		if (vp->v_data) {
			vprint("cleaned vnode", vp);
			panic("cleaned vnode isn't");
		}
		s = splbio();
		if (vp->v_numoutput)
			panic("Clean vnode has pending I/O's");
		splx(s);
#endif
		vp->v_flag = 0;
		vp->v_lastr = 0;
		vp->v_ralen = 0;
		vp->v_maxra = 0;
		vp->v_lastw = 0;
		vp->v_lasta = 0;
		vp->v_cstart = 0;
		vp->v_clen = 0;
		vp->v_socket = 0;
	}
	vp->v_type = VNON;
	cache_purge(vp);
	vp->v_tag = tag;
	vp->v_op = vops;
	insmntque(vp, mp);
	*vpp = vp;
	vp->v_usecount = 1;
	vp->v_data = 0;
	return (0);
}

/*
 * Move a vnode from one mount queue to another.
 */
void
insmntque(vp, mp)
	register struct vnode *vp;
	register struct mount *mp;
{
	simple_lock(&mntvnode_slock);
	/*
	 * Delete from old mount point vnode list, if on one.
	 */
	
	if (vp->v_mount != NULL)
		LIST_REMOVE(vp, v_mntvnodes);
	/*
	 * Insert into list of vnodes for the new mount point, if available.
	 */
	if ((vp->v_mount = mp) != NULL)
		LIST_INSERT_HEAD(&mp->mnt_vnodelist, vp, v_mntvnodes);
	simple_unlock(&mntvnode_slock);
}

/*
 * Update outstanding I/O count and do wakeup if requested.
 */
void
vwakeup(bp)
	register struct buf *bp;
{
	register struct vnode *vp;

	bp->b_flags &= ~B_WRITEINPROG;
	if ((vp = bp->b_vp) != NULL) {
		if (--vp->v_numoutput < 0)
			panic("vwakeup: neg numoutput");
		if ((vp->v_flag & VBWAIT) && vp->v_numoutput <= 0) {
			vp->v_flag &= ~VBWAIT;
			wakeup((caddr_t)&vp->v_numoutput);
		}
	}
}

/*
 * Flush out and invalidate all buffers associated with a vnode.
 * Called with the underlying object locked.
 */
int
vinvalbuf(vp, flags, cred, p, slpflag, slptimeo)
	register struct vnode *vp;
	int flags;
	struct ucred *cred;
	struct proc *p;
	int slpflag, slptimeo;
{
	register struct buf *bp;
	struct buf *nbp, *blist;
	int s, error;

	if ((flags & V_SAVE) && vp->v_dirtyblkhd.lh_first != NULL) {
		if ((error = VOP_FSYNC(vp, cred, MNT_WAIT, p)) != 0)
			return (error);
		if (vp->v_dirtyblkhd.lh_first != NULL)
			panic("vinvalbuf: dirty bufs");
	}
	for (;;) {
		if ((blist = vp->v_cleanblkhd.lh_first) && 
		    (flags & V_SAVEMETA))
			while (blist && blist->b_lblkno < 0)
				blist = blist->b_vnbufs.le_next;
		if (!blist && (blist = vp->v_dirtyblkhd.lh_first) &&
		    (flags & V_SAVEMETA))
			while (blist && blist->b_lblkno < 0)
				blist = blist->b_vnbufs.le_next;
		if (!blist)
			break;

		for (bp = blist; bp; bp = nbp) {
			nbp = bp->b_vnbufs.le_next;
			if (flags & V_SAVEMETA && bp->b_lblkno < 0)
				continue;
			s = splbio();
			if (bp->b_flags & B_BUSY) {
				bp->b_flags |= B_WANTED;
				error = tsleep((caddr_t)bp,
					slpflag | (PRIBIO + 1), "vinvalbuf",
					slptimeo);
				splx(s);
				if (error)
					return (error);
				break;
			}
			bp->b_flags |= B_BUSY | B_VFLUSH;
			splx(s);
			/*
			 * XXX Since there are no node locks for NFS, I believe
			 * there is a slight chance that a delayed write will
			 * occur while sleeping just above, so check for it.
			 */
			if ((bp->b_flags & B_DELWRI) && (flags & V_SAVE)) {
				(void) VOP_BWRITE(bp);
				break;
			}
			bp->b_flags |= B_INVAL;
			brelse(bp);
		}
	}
	if (!(flags & V_SAVEMETA) &&
	    (vp->v_dirtyblkhd.lh_first || vp->v_cleanblkhd.lh_first))
		panic("vinvalbuf: flush failed");
	return (0);
}

void
vflushbuf(vp, sync)
	register struct vnode *vp;
	int sync;
{
	register struct buf *bp, *nbp;
	int s;

loop:
	s = splbio();
	for (bp = vp->v_dirtyblkhd.lh_first; bp; bp = nbp) {
		nbp = bp->b_vnbufs.le_next;
		if ((bp->b_flags & B_BUSY))
			continue;
		if ((bp->b_flags & B_DELWRI) == 0)
			panic("vflushbuf: not dirty");
		bp->b_flags |= B_BUSY | B_VFLUSH;
		splx(s);
		/*
		 * Wait for I/O associated with indirect blocks to complete,
		 * since there is no way to quickly wait for them below.
		 */
		if (bp->b_vp == vp || sync == 0)
			(void) bawrite(bp);
		else
			(void) bwrite(bp);
		goto loop;
	}
	if (sync == 0) {
		splx(s);
		return;
	}
	while (vp->v_numoutput) {
		vp->v_flag |= VBWAIT;
		tsleep((caddr_t)&vp->v_numoutput, PRIBIO + 1, "vflushbuf", 0);
	}
	splx(s);
	if (vp->v_dirtyblkhd.lh_first != NULL) {
		vprint("vflushbuf: dirty", vp);
		goto loop;
	}
}

/*
 * Associate a buffer with a vnode.
 */
void
bgetvp(vp, bp)
	register struct vnode *vp;
	register struct buf *bp;
{

	if (bp->b_vp)
		panic("bgetvp: not free");
	VHOLD(vp);
	bp->b_vp = vp;
	if (vp->v_type == VBLK || vp->v_type == VCHR)
		bp->b_dev = vp->v_rdev;
	else
		bp->b_dev = NODEV;
	/*
	 * Insert onto list for new vnode.
	 */
	bufinsvn(bp, &vp->v_cleanblkhd);
}

/*
 * Disassociate a buffer from a vnode.
 */
void
brelvp(bp)
	register struct buf *bp;
{
	struct vnode *vp;
	struct buf *wasdirty;

	if ((vp = bp->b_vp) == (struct vnode *) 0)
		panic("brelvp: NULL");
	/*
	 * Delete from old vnode list, if on one.
	 */
	wasdirty = vp->v_dirtyblkhd.lh_first;
	if (bp->b_vnbufs.le_next != NOLIST)
		bufremvn(bp);
	if (wasdirty && LIST_FIRST(&vp->v_dirtyblkhd) == NULL)
		LIST_REMOVE(vp, v_synclist);
	bp->b_vp = (struct vnode *) 0;
	HOLDRELE(vp);
}

/*
 * The workitem queue.
 * 
 * It is useful to delay writes of file data and filesystem metadata
 * for tens of seconds so that quickly created and deleted files need
 * not waste disk bandwidth being created and removed. To realize this,
 * we append vnodes to a "workitem" queue. When running with a soft
 * updates implementation, most pending metadata dependencies should
 * not wait for more than a few seconds. Thus, mounted on block devices
 * are delayed only about a half the time that file data is delayed.
 * Similarly, directory updates are more critical, so are only delayed
 * about a third the time that file data is delayed. Thus, there are
 * SYNCER_MAXDELAY queues that are processed round-robin at a rate of
 * one each second (driven off the filesystem syner process). The
 * syncer_delayno variable indicates the next queue that is to be processed.
 * Items that need to be processed soon are placed in this queue:
 *
 *	syncer_workitem_pending[syncer_delayno]
 *
 * A delay of fifteen seconds is done by placing the request fifteen
 * entries later in the queue:
 *
 *	syncer_workitem_pending[(syncer_delayno + 15) & syncer_mask]
 *
 */

/*
 * Add an item to the syncer work queue.
 */
void
vn_syncer_add_to_worklist(vp, delay)
	struct vnode *vp;
	int delay;
{
	int s, slot;

	s = splbio();
	if (delay > syncer_maxdelay - 2)
		delay = syncer_maxdelay - 2;
	slot = (syncer_delayno + delay) & syncer_mask;
	LIST_INSERT_HEAD(&syncer_workitem_pending[slot], vp, v_synclist);
	splx(s);
}

/*
 * System filesystem synchronizer daemon.
 */

extern int lbolt;

void 
sched_sync(p)
	struct proc *p;
{
	struct synclist *slp;
	struct vnode *vp;
	long starttime;
	int s;

	for (;;) {
		starttime = time.tv_sec;

		/*
		 * Push files whose dirty time has expired.
		 */
		s = splbio();
		slp = &syncer_workitem_pending[syncer_delayno];
		syncer_delayno += 1;
		if (syncer_delayno == syncer_maxdelay)
			syncer_delayno = 0;
		splx(s);
		while ((vp = LIST_FIRST(slp)) != NULL) {
			vn_lock(vp, LK_EXCLUSIVE | LK_RETRY, p);
			(void) VOP_FSYNC(vp, p->p_ucred, MNT_LAZY, p);
			VOP_UNLOCK(vp, 0, p);
			if (LIST_FIRST(slp) == vp) {
				if (LIST_FIRST(&vp->v_dirtyblkhd) == NULL)
					panic("sched_sync: fsync failed");
				/*
				 * Move ourselves to the back of the sync list.
				 */
				LIST_REMOVE(vp, v_synclist);
				vn_syncer_add_to_worklist(vp, syncdelay);
			}
		}

		/*
		 * Do soft update processing.
		 */
		if (bioops.io_sync)
			(*bioops.io_sync)(NULL);

		/*
		 * If it has taken us less than a second to process the
		 * current work, then wait. Otherwise start right over
		 * again. We can still lose time if any single round
		 * takes more than two seconds, but it does not really
		 * matter as we are just trying to generally pace the
		 * filesystem activity.
		 */
		if (time.tv_sec == starttime)
			tsleep(&lbolt, PPAUSE, "syncer", 0);
	}
}

/*
 * Reassign a buffer from one vnode to another. Used to assign buffers
 * to the appropriate clean or dirty list and to add newly dirty vnodes
 * to the appropriate filesystem syncer list.
 */
void
reassignbuf(bp, newvp)
	register struct buf *bp;
	register struct vnode *newvp;
{
	struct buflists *listheadp;
	struct buf *wasdirty;
	int delay;

	if (newvp == NULL) {
		printf("reassignbuf: NULL");
		return;
	}
	/*
	 * Delete from old vnode list, if on one.
	 */
	wasdirty = newvp->v_dirtyblkhd.lh_first;
	if (bp->b_vnbufs.le_next != NOLIST)
		bufremvn(bp);
	/*
	 * If dirty, put on list of dirty buffers;
	 * otherwise insert onto list of clean buffers.
	 */
	if ((bp->b_flags & B_DELWRI) == 0) {
		listheadp = &newvp->v_cleanblkhd;
		if (wasdirty && LIST_FIRST(&newvp->v_dirtyblkhd) == NULL)
			LIST_REMOVE(newvp, v_synclist);
	} else {
		listheadp = &newvp->v_dirtyblkhd;
		if (LIST_FIRST(listheadp) == NULL) {
			switch (newvp->v_type) {
			case VDIR:
				delay = syncdelay / 3;
				break;
			case VBLK:
				if (newvp->v_specmountpoint != NULL) {
					delay = syncdelay / 2;
					break;
				}
				/* fall through */
			default:
				delay = syncdelay;
			}
			vn_syncer_add_to_worklist(newvp, delay);
		}
	}
	bufinsvn(bp, listheadp);
}

/*
 * Create a vnode for a block device.
 * Used for root filesystem, argdev, and swap areas.
 * Also used for memory file system special devices.
 */
int
bdevvp(dev, vpp)
	dev_t dev;
	struct vnode **vpp;
{

	return (getdevvp(dev, vpp, VBLK));
}

/*
 * Create a vnode for a character device.
 * Used for kernfs and some console handling.
 */
int
cdevvp(dev, vpp)
	dev_t dev;
	struct vnode **vpp;
{

	return (getdevvp(dev, vpp, VCHR));
}

/*
 * Create a vnode for a device.
 * Used by bdevvp (block device) for root file system etc.,
 * and by cdevvp (character device) for console and kernfs.
 */
int
getdevvp(dev, vpp, type)
	dev_t dev;
	struct vnode **vpp;
	enum vtype type;
{
	register struct vnode *vp;
	struct vnode *nvp;
	int error;

	if (dev == NODEV) {
		*vpp = NULLVP;
		return (0);
	}
	error = getnewvnode(VT_NON, NULL, spec_vnodeop_p, &nvp);
	if (error) {
		*vpp = NULLVP;
		return (error);
	}
	vp = nvp;
	vp->v_type = type;
	if ((nvp = checkalias(vp, dev, NULL)) != 0) {
		vput(vp);
		vp = nvp;
	}
	*vpp = vp;
	return (0);
}

/*
 * Check to see if the new vnode represents a special device
 * for which we already have a vnode (either because of
 * bdevvp() or because of a different vnode representing
 * the same block device). If such an alias exists, deallocate
 * the existing contents and return the aliased vnode. The
 * caller is responsible for filling it with its new contents.
 */
struct vnode *
checkalias(nvp, nvp_rdev, mp)
	register struct vnode *nvp;
	dev_t nvp_rdev;
	struct mount *mp;
{
	struct proc *p = curproc;
	register struct vnode *vp;
	struct vnode **vpp;

	if (nvp->v_type != VBLK && nvp->v_type != VCHR)
		return (NULLVP);

	vpp = &speclisth[SPECHASH(nvp_rdev)];
loop:
	simple_lock(&spechash_slock);
	for (vp = *vpp; vp; vp = vp->v_specnext) {
		simple_lock(&vp->v_interlock);
		if (nvp_rdev != vp->v_rdev || nvp->v_type != vp->v_type)
			continue;
		/*
		 * Alias, but not in use, so flush it out.
		 */
		if (vp->v_usecount == 0) {
			simple_unlock(&spechash_slock);
			vgonel(vp, p);
			goto loop;
		}
		if (vget(vp, LK_EXCLUSIVE | LK_INTERLOCK, p)) {
			simple_unlock(&spechash_slock);
			goto loop;
		}
		break;
	}
	if (vp == NULL || vp->v_tag != VT_NON || vp->v_type != VBLK) {
		MALLOC(nvp->v_specinfo, struct specinfo *,
			sizeof(struct specinfo), M_VNODE, M_WAITOK);
		nvp->v_rdev = nvp_rdev;
		nvp->v_hashchain = vpp;
		nvp->v_specnext = *vpp;
		nvp->v_specmountpoint = NULL;
		nvp->v_speclockf = NULL;
		simple_unlock(&spechash_slock);
		*vpp = nvp;
		if (vp != NULLVP) {
			nvp->v_flag |= VALIASED;
			vp->v_flag |= VALIASED;
			vput(vp);
		}
		return (NULLVP);
	}
	simple_unlock(&spechash_slock);
	VOP_UNLOCK(vp, 0, p);
	simple_lock(&vp->v_interlock);
	vclean(vp, 0, p);
	vp->v_op = nvp->v_op;
	vp->v_tag = nvp->v_tag;
	nvp->v_type = VNON;
	insmntque(vp, mp);
	return (vp);
}

/*
 * Grab a particular vnode from the free list, increment its
 * reference count and lock it. The vnode lock bit is set the
 * vnode is being eliminated in vgone. The process is awakened
 * when the transition is completed, and an error returned to
 * indicate that the vnode is no longer usable (possibly having
 * been changed to a new file system type).
 */
int
vget(vp, flags, p)
        struct vnode *vp;
	int flags;
	struct proc *p;
{
	int error;
	/*
	 * If the vnode is in the process of being cleaned out for
	 * another use, we wait for the cleaning to finish and then
	 * return failure. Cleaning is determined by checking that
	 * the VXLOCK flag is set.
	 */
	if ((flags & LK_INTERLOCK) == 0)
		simple_lock(&vp->v_interlock);
	if (vp->v_flag & VXLOCK) {
 		vp->v_flag |= VXWANT;
		simple_unlock(&vp->v_interlock);
		tsleep((caddr_t)vp, PINOD, "vget", 0);
		return (ENOENT);
 	}
	if (vp->v_usecount == 0) {
		simple_lock(&vnode_free_list_slock);
		if (vp->v_holdcnt > 0)
			TAILQ_REMOVE(&vnode_hold_list, vp, v_freelist);
		else
			TAILQ_REMOVE(&vnode_free_list, vp, v_freelist);
		simple_unlock(&vnode_free_list_slock);
	}
 	vp->v_usecount++;
	if (flags & LK_TYPE_MASK) {
		if ((error = vn_lock(vp, flags | LK_INTERLOCK, p)) != 0) {
			vunref(vp);
			simple_unlock(&vp->v_interlock);
		}
		return (error);
	}
	simple_unlock(&vp->v_interlock);
	return (0);
}

/*
 * Stubs to use when there is no locking to be done on the underlying object.
 * A minimal shared lock is necessary to ensure that the underlying object
 * is not revoked while an operation is in progress. So, an active shared
 * count is maintained in an auxillary vnode lock structure.
 */
int
vop_nolock(v)
	void *v;
{
	struct vop_lock_args /* {
		struct vnode *a_vp;
		int a_flags;
		struct proc *a_p;
	} */ *ap = v;

#ifdef notyet
	/*
	 * This code cannot be used until all the non-locking filesystems
	 * (notably NFS) are converted to properly lock and release nodes.
	 * Also, certain vnode operations change the locking state within
	 * the operation (create, mknod, remove, link, rename, mkdir, rmdir,
	 * and symlink). Ideally these operations should not change the
	 * lock state, but should be changed to let the caller of the
	 * function unlock them. Otherwise all intermediate vnode layers
	 * (such as union, umapfs, etc) must catch these functions to do
	 * the necessary locking at their layer. Note that the inactive
	 * and lookup operations also change their lock state, but this 
	 * cannot be avoided, so these two operations will always need
	 * to be handled in intermediate layers.
	 */
	struct vnode *vp = ap->a_vp;
	int vnflags, flags = ap->a_flags;

	if (vp->v_vnlock == NULL) {
		if ((flags & LK_TYPE_MASK) == LK_DRAIN)
			return (0);
		MALLOC(vp->v_vnlock, struct lock *, sizeof(struct lock),
		    M_VNODE, M_WAITOK);
		lockinit(vp->v_vnlock, PVFS, "vnlock", 0, 0);
	}
	switch (flags & LK_TYPE_MASK) {
	case LK_DRAIN:
		vnflags = LK_DRAIN;
		break;
	case LK_EXCLUSIVE:
	case LK_SHARED:
		vnflags = LK_SHARED;
		break;
	case LK_UPGRADE:
	case LK_EXCLUPGRADE:
	case LK_DOWNGRADE:
		return (0);
	case LK_RELEASE:
	default:
		panic("vop_nolock: bad operation %d", flags & LK_TYPE_MASK);
	}
	if (flags & LK_INTERLOCK)
		vnflags |= LK_INTERLOCK;
	return(lockmgr(vp->v_vnlock, vnflags, &vp->v_interlock, ap->a_p));
#else /* for now */
        /*
         * Since we are not using the lock manager, we must clear
         * the interlock here.
         */
        if (ap->a_flags & LK_INTERLOCK)
                simple_unlock(&ap->a_vp->v_interlock);
        return (0);
#endif
}
 
/*
 * Decrement the active use count.
 */

int
vop_nounlock(v)
	void *v;
{
	struct vop_unlock_args /* {
		struct vnode *a_vp;
		int a_flags;
		struct proc *a_p;
	} */ *ap = v;

	struct vnode *vp = ap->a_vp;

	if (vp->v_vnlock == NULL)
		return (0);
	return (lockmgr(vp->v_vnlock, LK_RELEASE, NULL, ap->a_p));
}

/*
 * Return whether or not the node is in use.
 */
int
vop_noislocked(v)
	void *v;
{
	struct vop_islocked_args /* {
		struct vnode *a_vp;
	} */ *ap = v;

	struct vnode *vp = ap->a_vp;

	if (vp->v_vnlock == NULL)
		return (0);
	return (lockstatus(vp->v_vnlock));
}

#ifdef DIAGNOSTIC
/*
 * Vnode reference.
 */
void
vref(vp)
	struct vnode *vp;
{
	simple_lock(&vp->v_interlock);
	if (vp->v_usecount <= 0)
		panic("vref used where vget required");
	vp->v_usecount++;
	simple_unlock(&vp->v_interlock);
}
#endif /* DIAGNOSTIC */

int
vunref(vp)
	struct vnode *vp;
{
#ifdef DIAGNOSTIC
	if (vp == NULL)
		panic("vrele: null vp");
#endif
	simple_lock (&vp->v_interlock);
	vp->v_usecount--;
	if (vp->v_usecount > 0) {
		simple_unlock(&vp->v_interlock);
		return (vp->v_usecount);
	}
#ifdef DIAGNOSTIC
	if (vp->v_usecount < 0 || vp->v_writecount != 0) {
		vprint("vrele: bad ref count", vp);
		panic("vrele: ref cnt");
	}
#endif
	/*
	 * insert at tail of LRU list
	 */
	simple_lock(&vnode_free_list_slock);
	if (vp->v_holdcnt > 0)
		TAILQ_INSERT_TAIL(&vnode_hold_list, vp, v_freelist);
	else
		TAILQ_INSERT_TAIL(&vnode_free_list, vp, v_freelist);
	simple_unlock(&vnode_free_list_slock);

	return (0);
}

/*
 * vput(), just unlock and vrele()
 */
void
vput(vp)
	register struct vnode *vp;
{
	struct proc *p = curproc;	/* XXX */

#ifdef DIGANOSTIC
	if (vp == NULL)
		panic("vput: null vp");
#endif
	simple_lock(&vp->v_interlock);
	vp->v_usecount--;
	if (vp->v_usecount > 0) {
		simple_unlock(&vp->v_interlock);
		VOP_UNLOCK(vp, 0, p);
		return;
	}
#ifdef DIAGNOSTIC
	if (vp->v_usecount < 0 || vp->v_writecount != 0) {
		vprint("vput: bad ref count", vp);
		panic("vput: ref cnt");
	}
#endif
	/*
	 * insert at tail of LRU list
	 */
	simple_lock(&vnode_free_list_slock);
	if (vp->v_holdcnt > 0)
		TAILQ_INSERT_TAIL(&vnode_hold_list, vp, v_freelist);
	else
		TAILQ_INSERT_TAIL(&vnode_free_list, vp, v_freelist);
	simple_unlock(&vnode_free_list_slock);
	simple_unlock(&vp->v_interlock);
	VOP_INACTIVE(vp, p);
}

/*
 * Vnode release - use for active VNODES.
 * If count drops to zero, call inactive routine and return to freelist.
 */
void
vrele(vp)
	register struct vnode *vp;
{
	struct proc *p = curproc;

	if (vunref(vp) == 0 &&
	    vn_lock(vp, LK_EXCLUSIVE |LK_INTERLOCK, p) == 0)
		VOP_INACTIVE(vp, p);
}

#ifdef DIAGNOSTIC
/*
 * Page or buffer structure gets a reference.
 */
void
vhold(vp)
	register struct vnode *vp;
{

	/*
	 * If it is on the freelist and the hold count is currently
	 * zero, move it to the hold list.
	 *
	 * The VGONEHACK flag reflects a call from getnewvnode,
	 * which will remove the vnode from the free list, but
	 * will not increment the ref count until after it calls vgone
	 * If the ref count we're incremented first, vgone would
	 * (incorrectly) try to close the previous instance of the
	 * underlying object.
	 */
  	simple_lock(&vp->v_interlock);
	if (!(vp->v_flag & VGONEHACK) &&
	    vp->v_holdcnt == 0 && vp->v_usecount == 0) {
		simple_lock(&vnode_free_list_slock);
		TAILQ_REMOVE(&vnode_free_list, vp, v_freelist);
		TAILQ_INSERT_TAIL(&vnode_hold_list, vp, v_freelist);
		simple_unlock(&vnode_free_list_slock);
	}
	vp->v_holdcnt++;
	simple_unlock(&vp->v_interlock);
}

/*
 * Page or buffer structure frees a reference.
 */
void
holdrele(vp)
	register struct vnode *vp;
{

	simple_lock(&vp->v_interlock);
	if (vp->v_holdcnt <= 0)
		panic("holdrele: holdcnt");
	vp->v_holdcnt--;
	/*
	 * If it is on the holdlist and the hold count drops to
	 * zero, move it to the free list. 
	 *
	 * See above for VGONEHACK
	 */
	if (!(vp->v_flag & VGONEHACK) &&
	    vp->v_holdcnt == 0 && vp->v_usecount == 0) {
		simple_lock(&vnode_free_list_slock);
		TAILQ_REMOVE(&vnode_hold_list, vp, v_freelist);
		TAILQ_INSERT_TAIL(&vnode_free_list, vp, v_freelist);
		simple_unlock(&vnode_free_list_slock);
	}
	simple_unlock(&vp->v_interlock);
}
#endif /* DIAGNOSTIC */

/*
 * Remove any vnodes in the vnode table belonging to mount point mp.
 *
 * If MNT_NOFORCE is specified, there should not be any active ones,
 * return error if any are found (nb: this is a user error, not a
 * system error). If MNT_FORCE is specified, detach any active vnodes
 * that are found.
 */
#ifdef DEBUG
int busyprt = 0;	/* print out busy vnodes */
struct ctldebug debug1 = { "busyprt", &busyprt };
#endif

int
vflush(mp, skipvp, flags)
	struct mount *mp;
	struct vnode *skipvp;
	int flags;
{
	struct proc *p = curproc;
	register struct vnode *vp, *nvp;
	int busy = 0;

	simple_lock(&mntvnode_slock);
loop:
	for (vp = mp->mnt_vnodelist.lh_first; vp; vp = nvp) {
		if (vp->v_mount != mp)
			goto loop;
		nvp = vp->v_mntvnodes.le_next;
		/*
		 * Skip over a selected vnode.
		 */
		if (vp == skipvp)
			continue;
		
		simple_lock(&vp->v_interlock);
		/*
		 * Skip over a vnodes marked VSYSTEM.
		 */
		if ((flags & SKIPSYSTEM) && (vp->v_flag & VSYSTEM)) {
			simple_unlock(&vp->v_interlock);
			continue;
		}
		/*
		 * If WRITECLOSE is set, only flush out regular file
		 * vnodes open for writing.
		 */
		if ((flags & WRITECLOSE) &&
		    (vp->v_writecount == 0 || vp->v_type != VREG)) {
			simple_unlock(&vp->v_interlock);
			continue;
		}
		/*
		 * With v_usecount == 0, all we need to do is clear
		 * out the vnode data structures and we are done.
		 */
		if (vp->v_usecount == 0) {
			simple_unlock(&mntvnode_slock);
			vgonel(vp, p);
			simple_lock(&mntvnode_slock);
			continue;
		}
		/*
		 * If FORCECLOSE is set, forcibly close the vnode.
		 * For block or character devices, revert to an
		 * anonymous device. For all other files, just kill them.
		 */
		if (flags & FORCECLOSE) {
			simple_unlock(&mntvnode_slock);
			if (vp->v_type != VBLK && vp->v_type != VCHR) {
				vgonel(vp, p);
			} else {
				vclean(vp, 0, p);
				vp->v_op = spec_vnodeop_p;
				insmntque(vp, (struct mount *)0);
			}
			simple_lock(&mntvnode_slock);
			continue;
		}
#ifdef DEBUG
		if (busyprt)
			vprint("vflush: busy vnode", vp);
#endif
		simple_unlock(&vp->v_interlock);
		busy++;
	}
	simple_unlock(&mntvnode_slock);
	if (busy)
		return (EBUSY);
	return (0);
}

/*
 * Disassociate the underlying file system from a vnode.
 * The vnode interlock is held on entry.
 */
void
vclean(vp, flags, p)
	register struct vnode *vp;
	int flags;
	struct proc *p;
{
	int active;

	/*
	 * Check to see if the vnode is in use.
	 * If so we have to reference it before we clean it out
	 * so that its count cannot fall to zero and generate a
	 * race against ourselves to recycle it.
	 */
	if ((active = vp->v_usecount) != 0)
		vp->v_usecount++;

	/*
	 * Prevent the vnode from being recycled or
	 * brought into use while we clean it out.
	 */
	if (vp->v_flag & VXLOCK)
		panic("vclean: deadlock");
	vp->v_flag |= VXLOCK;

	
	/*
	 * Even if the count is zero, the VOP_INACTIVE routine may still
	 * have the object locked while it cleans it out. The VOP_LOCK
	 * ensures that the VOP_INACTIVE routine is done with its work.
	 * For active vnodes, it ensures that no other activity can
	 * occur while the underlying object is being cleaned out.
	 */
	VOP_LOCK(vp, LK_DRAIN | LK_INTERLOCK, p);

	/*
	 * Clean out any buffers associated with the vnode.
	 */
	if (flags & DOCLOSE)
		vinvalbuf(vp, V_SAVE, NOCRED, p, 0, 0);
	/*
	 * If purging an active vnode, it must be closed and
	 * deactivated before being reclaimed. Note that the
	 * VOP_INACTIVE will unlock the vnode
	 */
	if (active) {
		if (flags & DOCLOSE)
			VOP_CLOSE(vp, FNONBLOCK, NOCRED, p);
		VOP_INACTIVE(vp, p);
	} else {
		/*
		 * Any other processes trying to obtain this lock must first
		 * wait for VXLOCK to clear, then call the new lock operation.
		 */
		VOP_UNLOCK(vp, 0, p);
	}

	/*
	 * Reclaim the vnode.
	 */
	if (VOP_RECLAIM(vp, p))
		panic("vclean: cannot reclaim");
	if (active) {
		if (vunref(vp) == 0 &&
		    vp->v_holdcnt > 0)
			panic("vclean: not clean");
		simple_unlock(&vp->v_interlock);
	}
	cache_purge(vp);
	if (vp->v_vnlock) {
		if ((vp->v_vnlock->lk_flags & LK_DRAINED) == 0)
			vprint("vclean: lock not drained", vp);
		FREE(vp->v_vnlock, M_VNODE);
		vp->v_vnlock = NULL;
	}

	/*
	 * Done with purge, notify sleepers of the grim news.
	 */
	vp->v_op = dead_vnodeop_p;
	vp->v_tag = VT_NON;
	vp->v_flag &= ~VXLOCK;
	if (vp->v_flag & VXWANT) {
		vp->v_flag &= ~VXWANT;
		wakeup((caddr_t)vp);
	}
}

/*
 * Eliminate all activity associated with  the requested vnode
 * and with all vnodes aliased to the requested vnode.
 */
int
vop_revoke(v)
	void *v;
{
	struct vop_revoke_args /* {
	        struct vnode *a_vp;
		int a_flags;
	} */ *ap = v;
	struct vnode *vp, *vq;
	struct proc *p = curproc;

#ifdef DIAGNOSTIC
	if ((ap->a_flags & REVOKEALL) == 0)
		panic("vop_revoke");
#endif

	vp = ap->a_vp;
	simple_lock(&vp->v_interlock);
 
	if (vp->v_flag & VALIASED) {
		/*
		 * If a vgone (or vclean) is already in progress,
		 * wait until it is done and return.
		 */
		if (vp->v_flag & VXLOCK) {
			vp->v_flag |= VXWANT;
			simple_unlock(&vp->v_interlock);
			tsleep((caddr_t)vp, PINOD, "vop_revokeall", 0);
			return(0);
		}
		/*
		 * Ensure that vp will not be vgone'd while we
		 * are eliminating its aliases.
		 */
		vp->v_flag |= VXLOCK;
		simple_unlock(&vp->v_interlock);
		while (vp->v_flag & VALIASED) {
			simple_lock(&spechash_slock);
			for (vq = *vp->v_hashchain; vq; vq = vq->v_specnext) {
				if (vq->v_rdev != vp->v_rdev ||
				    vq->v_type != vp->v_type || vp == vq)
					continue;
				simple_unlock(&spechash_slock);
				vgone(vq);
				break;
			}
		}
		/*
		 * Remove the lock so that vgone below will
		 * really eliminate the vnode after which time
		 * vgone will awaken any sleepers.
		 */
		simple_lock(&vp->v_interlock);
		vp->v_flag &= ~VXLOCK;
	}
	vgonel(vp, p);
	return (0);
}


/*
 * Recycle an unused vnode to the front of the free list.
 * Release the passed interlock if the vnode will be recycled.
 */
int
vrecycle(vp, inter_lkp, p)
	struct vnode *vp;
	struct simplelock *inter_lkp;
	struct proc *p;
{

	simple_lock(&vp->v_interlock);
	if (vp->v_usecount == 0) {
		if (inter_lkp)
			simple_unlock(inter_lkp);
		vgonel(vp, p);
		return (1);
	}
	simple_unlock(&vp->v_interlock);
	return (0);
}

/*
 * Eliminate all activity associated with a vnode
 * in preparation for reuse.
 */
void
vgone(vp)
	register struct vnode *vp;
{
	struct proc *p = curproc;

	simple_lock (&vp->v_interlock);
	vgonel(vp, p);
}

/*
 * vgone, with the vp interlock held.
 */
void
vgonel(vp, p)
	struct vnode *vp;
	struct proc *p;
{
	register struct vnode *vq;
	struct vnode *vx;

	/*
	 * If a vgone (or vclean) is already in progress,
	 * wait until it is done and return.
	 */
	if (vp->v_flag & VXLOCK) {
		vp->v_flag |= VXWANT;
		simple_unlock(&vp->v_interlock);
		tsleep((caddr_t)vp, PINOD, "vgone", 0);
		return;
	}
	/*
	 * Clean out the filesystem specific data.
	 */
	vclean(vp, DOCLOSE, p);
	/*
	 * Delete from old mount point vnode list, if on one.
	 */
	if (vp->v_mount != NULL)
		insmntque(vp, (struct mount *)0);
	/*
	 * If special device, remove it from special device alias list 
	 * if it is on one.
	 */
	if ((vp->v_type == VBLK || vp->v_type == VCHR) && vp->v_specinfo != 0) {
		simple_lock(&spechash_slock);
		if (*vp->v_hashchain == vp) {
			*vp->v_hashchain = vp->v_specnext;
		} else {
			for (vq = *vp->v_hashchain; vq; vq = vq->v_specnext) {
				if (vq->v_specnext != vp)
					continue;
				vq->v_specnext = vp->v_specnext;
				break;
			}
			if (vq == NULL)
				panic("missing bdev");
		}
		if (vp->v_flag & VALIASED) {
			vx = NULL;
			for (vq = *vp->v_hashchain; vq; vq = vq->v_specnext) {
				if (vq->v_rdev != vp->v_rdev ||
				    vq->v_type != vp->v_type)
					continue;
				if (vx)
					break;
				vx = vq;
			}
			if (vx == NULL)
				panic("missing alias");
			if (vq == NULL)
				vx->v_flag &= ~VALIASED;
			vp->v_flag &= ~VALIASED;
		}
		simple_unlock(&spechash_slock);
		FREE(vp->v_specinfo, M_VNODE);
		vp->v_specinfo = NULL;
	}
	/*
	 * If it is on the freelist and not already at the head,
	 * move it to the head of the list. 
	 *
	 * See above about the VGONEHACK
	 */
	if (vp->v_usecount == 0) {
		simple_lock(&vnode_free_list_slock);
		if (vp->v_holdcnt > 0)
			panic("vgonel: not clean");		
		if (!(vp->v_flag & VGONEHACK) &&
		    TAILQ_FIRST(&vnode_free_list) != vp) {
			TAILQ_REMOVE(&vnode_free_list, vp, v_freelist);
			TAILQ_INSERT_HEAD(&vnode_free_list, vp, v_freelist);
		}
		simple_unlock(&vnode_free_list_slock);
	}
	vp->v_type = VBAD;
}

/*
 * Lookup a vnode by device number.
 */
int
vfinddev(dev, type, vpp)
	dev_t dev;
	enum vtype type;
	struct vnode **vpp;
{
	register struct vnode *vp;
	int rc =0;

	simple_lock(&spechash_slock);
	for (vp = speclisth[SPECHASH(dev)]; vp; vp = vp->v_specnext) {
		if (dev != vp->v_rdev || type != vp->v_type)
			continue;
		*vpp = vp;
		rc = 1;
		break;
	}
	simple_unlock(&spechash_slock);
	return (rc);
}

/*
 * Calculate the total number of references to a special device.
 */
int
vcount(vp)
	struct vnode *vp;
{
	struct vnode *vq, *vnext;
	int count;

loop:
	if ((vp->v_flag & VALIASED) == 0)
		return (vp->v_usecount);
	simple_lock(&spechash_slock);
	for (count = 0, vq = *vp->v_hashchain; vq; vq = vnext) {
		vnext = vq->v_specnext;
		if (vq->v_rdev != vp->v_rdev || vq->v_type != vp->v_type)
			continue;
		/*
		 * Alias, but not in use, so flush it out.
		 */
		if (vq->v_usecount == 0 && vq != vp) {
			simple_unlock(&spechash_slock);
			vgone(vq);
			goto loop;
		}
		count += vq->v_usecount;
	}
	simple_unlock(&spechash_slock);
	return (count);
}

/*
 * Print out a description of a vnode.
 */
static char *typename[] =
   { "VNON", "VREG", "VDIR", "VBLK", "VCHR", "VLNK", "VSOCK", "VFIFO", "VBAD" };

void
vprint(label, vp)
	char *label;
	register struct vnode *vp;
{
	char buf[64];

	if (label != NULL)
		printf("%s: ", label);
	printf("type %s, usecount %d, writecount %d, refcount %ld,",
		typename[vp->v_type], vp->v_usecount, vp->v_writecount,
		vp->v_holdcnt);
	buf[0] = '\0';
	if (vp->v_flag & VROOT)
		strcat(buf, "|VROOT");
	if (vp->v_flag & VTEXT)
		strcat(buf, "|VTEXT");
	if (vp->v_flag & VSYSTEM)
		strcat(buf, "|VSYSTEM");
	if (vp->v_flag & VXLOCK)
		strcat(buf, "|VXLOCK");
	if (vp->v_flag & VXWANT)
		strcat(buf, "|VXWANT");
	if (vp->v_flag & VBWAIT)
		strcat(buf, "|VBWAIT");
	if (vp->v_flag & VALIASED)
		strcat(buf, "|VALIASED");
	if (buf[0] != '\0')
		printf(" flags (%s)", &buf[1]);
	if (vp->v_data == NULL) {
		printf("\n");
	} else {
		printf("\n\t");
		VOP_PRINT(vp);
	}
}

#ifdef DEBUG
/*
 * List all of the locked vnodes in the system.
 * Called when debugging the kernel.
 */
void
printlockedvnodes()
{
	struct proc *p = curproc;
	register struct mount *mp, *nmp;
	register struct vnode *vp;

	printf("Locked vnodes\n");
	simple_lock(&mountlist_slock);
	for (mp = CIRCLEQ_FIRST(&mountlist); mp != (void *)&mountlist;
	     mp = nmp) {
		if (vfs_busy(mp, LK_NOWAIT, &mountlist_slock, p)) { 
			nmp = CIRCLEQ_NEXT(mp, mnt_list);
			continue;
		}
		for (vp = mp->mnt_vnodelist.lh_first;
		     vp != NULL;
		     vp = vp->v_mntvnodes.le_next) {
			if (VOP_ISLOCKED(vp))
				vprint((char *)0, vp);
		}
		simple_lock(&mountlist_slock);
		nmp = CIRCLEQ_NEXT(mp, mnt_list);
		vfs_unbusy(mp, p);
 	}
	simple_unlock(&mountlist_slock);

}
#endif

/*
 * Top level filesystem related information gathering.
 */
int
vfs_sysctl(name, namelen, oldp, oldlenp, newp, newlen, p)
	int *name;
	u_int namelen;
	void *oldp;
	size_t *oldlenp;
	void *newp;
	size_t newlen;
	struct proc *p;
{
	struct vfsconf *vfsp;

	/* all sysctl names at this level are at least name and field */
	if (namelen < 2)
		return (ENOTDIR);		/* overloaded */
	if (name[0] != VFS_GENERIC) {
		for (vfsp = vfsconf; vfsp; vfsp = vfsp->vfc_next)
			if (vfsp->vfc_typenum == name[0])
				break;
		if (vfsp == NULL)
			return (EOPNOTSUPP);
		return ((*vfsp->vfc_vfsops->vfs_sysctl)(&name[1], namelen - 1,
		    oldp, oldlenp, newp, newlen, p));
	}
	switch (name[1]) {
	case VFS_MAXTYPENUM:
		return (sysctl_rdint(oldp, oldlenp, newp, maxvfsconf));
	case VFS_CONF:
		if (namelen < 3)
			return (ENOTDIR);	/* overloaded */
		for (vfsp = vfsconf; vfsp; vfsp = vfsp->vfc_next)
			if (vfsp->vfc_typenum == name[2])
				break;
		if (vfsp == NULL)
			return (EOPNOTSUPP);
		return (sysctl_rdstruct(oldp, oldlenp, newp, vfsp,
		    sizeof(struct vfsconf)));
	}
	return (EOPNOTSUPP);
}


int kinfo_vdebug = 1;
int kinfo_vgetfailed;
#define KINFO_VNODESLOP	10
/*
 * Dump vnode list (via sysctl).
 * Copyout address of vnode followed by vnode.
 */
/* ARGSUSED */
int
sysctl_vnode(where, sizep, p)
	char *where;
	size_t *sizep;
	struct proc *p;
{
	register struct mount *mp, *nmp;
	struct vnode *vp, *nvp;
	register char *bp = where, *savebp;
	char *ewhere;
	int error;

#define VPTRSZ	sizeof (struct vnode *)
#define VNODESZ	sizeof (struct vnode)
	if (where == NULL) {
		*sizep = (numvnodes + KINFO_VNODESLOP) * (VPTRSZ + VNODESZ);
		return (0);
	}
	ewhere = where + *sizep;
		
	simple_lock(&mountlist_slock);
	for (mp = mountlist.cqh_first; mp != (void *)&mountlist; mp = nmp) {
		if (vfs_busy(mp, LK_NOWAIT, &mountlist_slock, p)) {
			nmp = mp->mnt_list.cqe_next;
			continue;
		}
		savebp = bp;
again:
		for (vp = mp->mnt_vnodelist.lh_first;
		     vp != NULL;
		     vp = nvp) {
			/*
			 * Check that the vp is still associated with
			 * this filesystem.  RACE: could have been
			 * recycled onto the same filesystem.
			 */
			if (vp->v_mount != mp) {
				simple_unlock(&mntvnode_slock);
				if (kinfo_vdebug)
					printf("kinfo: vp changed\n");
				bp = savebp;
				goto again;
			}
			nvp = vp->v_mntvnodes.le_next;
			if (bp + VPTRSZ + VNODESZ > ewhere) {
				simple_unlock(&mntvnode_slock);
				*sizep = bp - where;
				return (ENOMEM);
			}
			if ((error = copyout((caddr_t)&vp, bp, VPTRSZ)) ||
			   (error = copyout((caddr_t)vp, bp + VPTRSZ, VNODESZ)))
				return (error);
			bp += VPTRSZ + VNODESZ;
			simple_lock(&mntvnode_slock);
		}

		simple_unlock(&mntvnode_slock);
		simple_lock(&mountlist_slock);
		nmp = mp->mnt_list.cqe_next;
		vfs_unbusy(mp, p);
	}

	simple_unlock(&mountlist_slock);

	*sizep = bp - where;
	return (0);
}

/*
 * Check to see if a filesystem is mounted on a block device.
 */
int
vfs_mountedon(vp)
	register struct vnode *vp;
{
	register struct vnode *vq;
	int error = 0;

 	if (vp->v_specmountpoint != NULL)
		return (EBUSY);
	if (vp->v_flag & VALIASED) {
		simple_lock(&spechash_slock);
		for (vq = *vp->v_hashchain; vq; vq = vq->v_specnext) {
			if (vq->v_rdev != vp->v_rdev ||
			    vq->v_type != vp->v_type)
				continue;
			if (vq->v_specmountpoint != NULL) {
				error = EBUSY;
				break;
			}
 		}
		simple_unlock(&spechash_slock);
	}
	return (error);
}

/*
 * Build hash lists of net addresses and hang them off the mount point.
 * Called by ufs_mount() to set up the lists of export addresses.
 */
int
vfs_hang_addrlist(mp, nep, argp)
	struct mount *mp;
	struct netexport *nep;
	struct export_args *argp;
{
	register struct netcred *np;
	register struct radix_node_head *rnh;
	register int i;
	struct radix_node *rn;
	struct sockaddr *saddr, *smask = 0;
	struct domain *dom;
	int error;

	if (argp->ex_addrlen == 0) {
		if (mp->mnt_flag & MNT_DEFEXPORTED)
			return (EPERM);
		np = &nep->ne_defexported;
		np->netc_exflags = argp->ex_flags;
		np->netc_anon = argp->ex_anon;
		np->netc_anon.cr_ref = 1;
		mp->mnt_flag |= MNT_DEFEXPORTED;
		return (0);
	}
	i = sizeof(struct netcred) + argp->ex_addrlen + argp->ex_masklen;
	np = (struct netcred *)malloc(i, M_NETADDR, M_WAITOK);
	bzero((caddr_t)np, i);
	saddr = (struct sockaddr *)(np + 1);
	error = copyin(argp->ex_addr, (caddr_t)saddr, argp->ex_addrlen);
	if (error)
		goto out;
	if (saddr->sa_len > argp->ex_addrlen)
		saddr->sa_len = argp->ex_addrlen;
	if (argp->ex_masklen) {
		smask = (struct sockaddr *)((caddr_t)saddr + argp->ex_addrlen);
		error = copyin(argp->ex_mask, (caddr_t)smask, argp->ex_masklen);
		if (error)
			goto out;
		if (smask->sa_len > argp->ex_masklen)
			smask->sa_len = argp->ex_masklen;
	}
	i = saddr->sa_family;
	if ((rnh = nep->ne_rtable[i]) == 0) {
		/*
		 * Seems silly to initialize every AF when most are not
		 * used, do so on demand here
		 */
		for (dom = domains; dom; dom = dom->dom_next)
			if (dom->dom_family == i && dom->dom_rtattach) {
				dom->dom_rtattach((void **)&nep->ne_rtable[i],
					dom->dom_rtoffset);
				break;
			}
		if ((rnh = nep->ne_rtable[i]) == 0) {
			error = ENOBUFS;
			goto out;
		}
	}
	rn = (*rnh->rnh_addaddr)((caddr_t)saddr, (caddr_t)smask, rnh,
		np->netc_rnodes);
	if (rn == 0 || np != (struct netcred *)rn) { /* already exists */
		error = EPERM;
		goto out;
	}
	np->netc_exflags = argp->ex_flags;
	np->netc_anon = argp->ex_anon;
	np->netc_anon.cr_ref = 1;
	return (0);
out:
	free(np, M_NETADDR);
	return (error);
}

/* ARGSUSED */
int
vfs_free_netcred(rn, w)
	struct radix_node *rn;
	void *w;
{
	register struct radix_node_head *rnh = (struct radix_node_head *)w;

	(*rnh->rnh_deladdr)(rn->rn_key, rn->rn_mask, rnh);
	free((caddr_t)rn, M_NETADDR);
	return (0);
}

/*
 * Free the net address hash lists that are hanging off the mount points.
 */
void
vfs_free_addrlist(nep)
	struct netexport *nep;
{
	register int i;
	register struct radix_node_head *rnh;

	for (i = 0; i <= AF_MAX; i++)
		if ((rnh = nep->ne_rtable[i]) != NULL) {
			(*rnh->rnh_walktree)(rnh, vfs_free_netcred, rnh);
			free((caddr_t)rnh, M_RTABLE);
			nep->ne_rtable[i] = 0;
		}
}

int
vfs_export(mp, nep, argp)
	struct mount *mp;
	struct netexport *nep;
	struct export_args *argp;
{
	int error;

	if (argp->ex_flags & MNT_DELEXPORT) {
		vfs_free_addrlist(nep);
		mp->mnt_flag &= ~(MNT_EXPORTED | MNT_DEFEXPORTED);
	}
	if (argp->ex_flags & MNT_EXPORTED) {
		if ((error = vfs_hang_addrlist(mp, nep, argp)) != 0)
			return (error);
		mp->mnt_flag |= MNT_EXPORTED;
	}
	return (0);
}

struct netcred *
vfs_export_lookup(mp, nep, nam)
	register struct mount *mp;
	struct netexport *nep;
	struct mbuf *nam;
{
	register struct netcred *np;
	register struct radix_node_head *rnh;
	struct sockaddr *saddr;

	np = NULL;
	if (mp->mnt_flag & MNT_EXPORTED) {
		/*
		 * Lookup in the export list first.
		 */
		if (nam != NULL) {
			saddr = mtod(nam, struct sockaddr *);
			rnh = nep->ne_rtable[saddr->sa_family];
			if (rnh != NULL) {
				np = (struct netcred *)
					(*rnh->rnh_matchaddr)((caddr_t)saddr,
							      rnh);
				if (np && np->netc_rnodes->rn_flags & RNF_ROOT)
					np = NULL;
			}
		}
		/*
		 * If no address match, use the default if it exists.
		 */
		if (np == NULL && mp->mnt_flag & MNT_DEFEXPORTED)
			np = &nep->ne_defexported;
	}
	return (np);
}

/*
 * Do the usual access checking.
 * file_mode, uid and gid are from the vnode in question,
 * while acc_mode and cred are from the VOP_ACCESS parameter list
 */
int
vaccess(file_mode, uid, gid, acc_mode, cred)
	mode_t file_mode;
	uid_t uid;
	gid_t gid;
	mode_t acc_mode;
	struct ucred *cred;
{
	mode_t mask;
	
	/* User id 0 always gets access. */
	if (cred->cr_uid == 0)
		return 0;
	
	mask = 0;
	
	/* Otherwise, check the owner. */
	if (cred->cr_uid == uid) {
		if (acc_mode & VEXEC)
			mask |= S_IXUSR;
		if (acc_mode & VREAD)
			mask |= S_IRUSR;
		if (acc_mode & VWRITE)
			mask |= S_IWUSR;
		return (file_mode & mask) == mask ? 0 : EACCES;
	}
	
	/* Otherwise, check the groups. */
	if (cred->cr_gid == gid || groupmember(gid, cred)) {
		if (acc_mode & VEXEC)
			mask |= S_IXGRP;
		if (acc_mode & VREAD)
			mask |= S_IRGRP;
		if (acc_mode & VWRITE)
			mask |= S_IWGRP;
		return (file_mode & mask) == mask ? 0 : EACCES;
	}
	
	/* Otherwise, check everyone else. */
	if (acc_mode & VEXEC)
		mask |= S_IXOTH;
	if (acc_mode & VREAD)
		mask |= S_IROTH;
	if (acc_mode & VWRITE)
		mask |= S_IWOTH;
	return (file_mode & mask) == mask ? 0 : EACCES;
}

/*
 * Unmount all file systems.
 * We traverse the list in reverse order under the assumption that doing so
 * will avoid needing to worry about dependencies.
 */
void
vfs_unmountall()
{
	register struct mount *mp, *nmp;
	int allerror, error;

	for (allerror = 0,
	     mp = mountlist.cqh_last; mp != (void *)&mountlist; mp = nmp) {
		nmp = mp->mnt_list.cqe_prev;
		if ((error = dounmount(mp, MNT_FORCE, curproc)) != 0) {
			printf("unmount of %s failed with error %d\n",
			    mp->mnt_stat.f_mntonname, error);
			allerror = 1;
		}
	}
	if (allerror)
		printf("WARNING: some file systems would not unmount\n");
}

/*
 * Sync and unmount file systems before shutting down.
 */
void
vfs_shutdown()
{
	register struct buf *bp;
	int iter, nbusy;

	/* XXX Should suspend scheduling. */
	(void) spl0();

	printf("syncing disks... ");

	if (panicstr == 0) {
		/* Release inodes held by texts before update. */
		vnode_pager_umount(NULL);
#ifdef notdef
		vnshutdown();
#endif

		/* Sync before unmount, in case we hang on something. */
		sys_sync(&proc0, (void *)0, (register_t *)0);

		/* Unmount file systems. */
		vfs_unmountall();
	}

	/* Sync again after unmount, just in case. */
	sys_sync(&proc0, (void *)0, (register_t *)0);

	/* Wait for sync to finish. */
	for (iter = 0; iter < 20; iter++) {
		nbusy = 0;
		for (bp = &buf[nbuf]; --bp >= buf; )
			if ((bp->b_flags & (B_BUSY|B_INVAL)) == B_BUSY)
				nbusy++;
		if (nbusy == 0)
			break;
		printf("%d ", nbusy);
		DELAY(40000 * iter);
	}
	if (nbusy)
		printf("giving up\n");
	else
		printf("done\n");
}

/*
 * posix file system related system variables.
 */
int
fs_posix_sysctl(name, namelen, oldp, oldlenp, newp, newlen, p)
	int *name;
	u_int namelen;
	void *oldp;
	size_t *oldlenp;
	void *newp;
	size_t newlen;
	struct proc *p;
{
	/* all sysctl names at this level are terminal */
	if (namelen != 1)
		return (ENOTDIR);

	switch (name[0]) {
	case FS_POSIX_SETUID:
		if (newp && securelevel > 0)
			return (EPERM);
		return(sysctl_int(oldp, oldlenp, newp, newlen, &suid_clear));
	default:
		return (EOPNOTSUPP);
	}
	/* NOTREACHED */
}

/*
 * file system related system variables.
 */
int
fs_sysctl(name, namelen, oldp, oldlenp, newp, newlen, p)
	int *name;
	u_int namelen;
	void *oldp;
	size_t *oldlenp;
	void *newp;
	size_t newlen;
	struct proc *p;
{
	sysctlfn *fn;

	switch (name[0]) {
	case FS_POSIX:
		fn = fs_posix_sysctl;
		break;
	default:
		return (EOPNOTSUPP);
	}
	return (*fn)(name + 1, namelen - 1, oldp, oldlenp, newp, newlen, p);
}

/*
 * Routine to create and manage a filesystem syncer vnode.
 */
#define sync_close nullop
int   sync_fsync __P((void *));
int   sync_inactive __P((void *));
#define sync_reclaim nullop
#define sync_lock vop_nolock
#define sync_unlock vop_nounlock
int   sync_print __P((void *));
#define sync_islocked vop_noislocked
 
int (**sync_vnodeop_p) __P((void *));
struct vnodeopv_entry_desc sync_vnodeop_entries[] = {
      { &vop_default_desc, vn_default_error },
      { &vop_close_desc, sync_close },                /* close */
      { &vop_fsync_desc, sync_fsync },                /* fsync */
      { &vop_inactive_desc, sync_inactive },          /* inactive */
      { &vop_reclaim_desc, sync_reclaim },            /* reclaim */
      { &vop_lock_desc, sync_lock },                  /* lock */
      { &vop_unlock_desc, sync_unlock },              /* unlock */
      { &vop_print_desc, sync_print },                /* print */
      { &vop_islocked_desc, sync_islocked },          /* islocked */
      { (struct vnodeop_desc*)NULL, (int(*) __P((void *)))NULL }
};
struct vnodeopv_desc sync_vnodeop_opv_desc =
      { &sync_vnodeop_p, sync_vnodeop_entries };

/*
 * Create a new filesystem syncer vnode for the specified mount point.
 */
int
vfs_allocate_syncvnode(mp)
      struct mount *mp;
{
      struct vnode *vp;
      static long start, incr, next;
      int error;

      /* Allocate a new vnode */
      if ((error = getnewvnode(VT_VFS, mp, sync_vnodeop_p, &vp)) != 0) {
              mp->mnt_syncer = NULL;
              return (error);
      }
      vp->v_writecount = 1;
      vp->v_type = VNON;
      /*
       * Place the vnode onto the syncer worklist. We attempt to
       * scatter them about on the list so that they will go off
       * at evenly distributed times even if all the filesystems
       * are mounted at once.
       */
      next += incr;
      if (next == 0 || next > syncer_maxdelay) {
              start /= 2;
              incr /= 2;
              if (start == 0) {
                      start = syncer_maxdelay / 2;
                      incr = syncer_maxdelay;
              }
              next = start;
      }
      vn_syncer_add_to_worklist(vp, next);
      mp->mnt_syncer = vp;
      return (0);
}

/*
 * Do a lazy sync of the filesystem.
 */
int
sync_fsync(v)
	void *v;
{
      struct vop_fsync_args /* {
              struct vnode *a_vp;
              struct ucred *a_cred;
              int a_waitfor;
              struct proc *a_p;
      } */ *ap = v;

      struct vnode *syncvp = ap->a_vp;
      struct mount *mp = syncvp->v_mount;
      int asyncflag;

      /*
       * We only need to do something if this is a lazy evaluation.
       */
      if (ap->a_waitfor != MNT_LAZY)
              return (0);

      /*
       * Move ourselves to the back of the sync list.
       */
      LIST_REMOVE(syncvp, v_synclist);
      vn_syncer_add_to_worklist(syncvp, syncdelay);

      /*
       * Walk the list of vnodes pushing all that are dirty and
       * not already on the sync list.
       */
      simple_lock(&mountlist_slock);
      if (vfs_busy(mp, LK_NOWAIT, &mountlist_slock, ap->a_p) == 0) {
              asyncflag = mp->mnt_flag & MNT_ASYNC;
              mp->mnt_flag &= ~MNT_ASYNC;
              VFS_SYNC(mp, MNT_LAZY, ap->a_cred, ap->a_p);
              if (asyncflag)
                      mp->mnt_flag |= MNT_ASYNC;
              vfs_unbusy(mp, ap->a_p);
      }
      return (0);
}

/*
 * The syncer vnode is no longer needed and is being decommissioned.
 */
int
sync_inactive(v)
	void *v;
	
{
      struct vop_inactive_args /* {
               struct vnode *a_vp;
               struct proc *a_p;
      } */ *ap = v;

      struct vnode *vp = ap->a_vp;

      if (vp->v_usecount == 0)
              return (0);
      vp->v_mount->mnt_syncer = NULL;
      LIST_REMOVE(vp, v_synclist);
      vp->v_writecount = 0;
      vput(vp);
      return (0);
}

/*
 * Print out a syncer vnode.
 */
int
sync_print(v)
	void *v;

{
      struct vop_print_args /* {
              struct vnode *a_vp;
      } */ *ap = v;
      struct vnode *vp = ap->a_vp;

      printf("syncer vnode");
      if (vp->v_vnlock != NULL)
              lockmgr_printinfo(vp->v_vnlock);
      printf("\n");
      return (0);
}

