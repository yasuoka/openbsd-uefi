/*	$NetBSD: lfs_extern.h,v 1.3 1994/12/14 13:03:47 mycroft Exp $	*/

/*-
 * Copyright (c) 1991, 1993, 1994
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
 *	@(#)lfs_extern.h	8.3 (Berkeley) 6/16/94
 */

struct fid;
struct mount;
struct nameidata;
struct proc;
struct statfs;
struct timeval;
struct inode;
struct uio;
struct mbuf;

__BEGIN_DECLS
u_long	 cksum __P((void *, size_t));				/* XXX */
int	 lfs_balloc __P((struct vnode *, u_long, daddr_t, struct buf **));
int	 lfs_blkatoff __P((struct vop_blkatoff_args *));
int	 lfs_bwrite __P((struct vop_bwrite_args *));
int	 lfs_check __P((struct vnode *, daddr_t));
int	 lfs_close __P((struct vop_close_args *));
int	 lfs_create __P((struct vop_create_args *));
int	 lfs_fhtovp __P((struct mount *, struct fid *, struct mbuf *,
	    struct vnode **, int *, struct ucred **));
int	 lfs_fsync __P((struct vop_fsync_args *));
int	 lfs_getattr __P((struct vop_getattr_args *));
struct dinode *
	 lfs_ifind __P((struct lfs *, ino_t, struct dinode *));
int	 lfs_inactive __P((struct vop_inactive_args *));
int	 lfs_init __P((void));
int	 lfs_initseg __P((struct lfs *));
int	 lfs_link __P((struct vop_link_args *));
int	 lfs_makeinode __P((int, struct nameidata *, struct inode **));
int	 lfs_mkdir __P((struct vop_mkdir_args *));
int	 lfs_mknod __P((struct vop_mknod_args *));
int	 lfs_mount __P((struct mount *,
	    char *, caddr_t, struct nameidata *, struct proc *));
int	 lfs_mountroot __P((void));
struct buf *
	 lfs_newbuf __P((struct vnode *, daddr_t, size_t));
int	 lfs_read __P((struct vop_read_args *));
int	 lfs_reclaim __P((struct vop_reclaim_args *));
int	 lfs_remove __P((struct vop_remove_args *));
int	 lfs_rmdir __P((struct vop_rmdir_args *));
int	 lfs_rename __P((struct vop_rename_args *));
void	 lfs_seglock __P((struct lfs *, unsigned long flags));
void	 lfs_segunlock __P((struct lfs *));
int	 lfs_segwrite __P((struct mount *, int));
int	 lfs_statfs __P((struct mount *, struct statfs *, struct proc *));
int	 lfs_symlink __P((struct vop_symlink_args *));
int	 lfs_sync __P((struct mount *, int, struct ucred *, struct proc *));
int	 lfs_truncate __P((struct vop_truncate_args *));
int	 lfs_unmount __P((struct mount *, int, struct proc *));
int	 lfs_update __P((struct vop_update_args *));
int	 lfs_valloc __P((struct vop_valloc_args *));
int	 lfs_vcreate __P((struct mount *, ino_t, struct vnode **));
int	 lfs_vfree __P((struct vop_vfree_args *));
int	 lfs_vflush __P((struct vnode *));
int	 lfs_vget __P((struct mount *, ino_t, struct vnode **));
int	 lfs_vptofh __P((struct vnode *, struct fid *));
int	 lfs_vref __P((struct vnode *));
void	 lfs_vunref __P((struct vnode *));
int	 lfs_write __P((struct vop_write_args *));
#ifdef DEBUG
void	lfs_dump_dinode __P((struct dinode *));
void	lfs_dump_super __P((struct lfs *));
#endif
__END_DECLS
extern int (**lfs_vnodeop_p)();
extern int (**lfs_specop_p)();
#ifdef FIFO
extern int (**lfs_fifoop_p)();
#define LFS_FIFOOPS lfs_fifoop_p
#else
#define LFS_FIFOOPS NULL
#endif
