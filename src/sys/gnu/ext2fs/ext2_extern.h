/*	$OpenBSD: ext2_extern.h,v 1.2 1996/06/27 06:48:40 downsj Exp $	*/

/*
 *  modified for EXT2FS support in Lites 1.1
 *
 *  Aug 1995, Godmar Back (gback@cs.utah.edu)
 *  University of Utah, Department of Computer Science
 */
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
 *	@(#)ffs_extern.h	8.3 (Berkeley) 4/16/94
 */

#ifndef _SYS_GNU_EXT2FS_EXT2_EXTERN_H_
#define	_SYS_GNU_EXT2FS_EXT2_EXTERN_H_

struct dinode;
struct ext2_inode;
struct inode;
struct mount;
struct vnode;

int	ext2_alloc __P((struct inode *,
	    daddr_t, daddr_t, int, struct ucred *, daddr_t *));
int	ext2_balloc __P((struct inode *,
	    daddr_t, int, struct ucred *, struct buf **, int));
int	ext2_blkatoff __P((void *));
void	ext2_blkfree __P((struct inode *, daddr_t, long));
daddr_t	ext2_blkpref __P((struct inode *, daddr_t, int, daddr_t *, daddr_t));
int	ext2_bmap __P((void *));
void	ext2_init __P((void));
int	ext2_reallocblks __P((void *));
int	ext2_reclaim __P((void *));
void	ext2_setblock __P((struct ext2_sb_info *, u_char *, daddr_t));
int	ext2_truncate __P((void *));
int	ext2_update __P((void *));
int	ext2_valloc __P((void *));
int	ext2_vfree __P((void *));
int 	ext2_lookup __P((void *));
int 	ext2_readdir __P((void *));
void	ext2_print_dinode __P((struct dinode *));
void	ext2_print_inode __P((struct inode *));
int	ext2_direnter __P((struct inode *, 
		struct vnode *, struct componentname *));
int	ext2_dirremove __P((struct vnode *, struct componentname *));
int	ext2_dirrewrite __P((struct inode *,
		struct inode *, struct componentname *));
int	ext2_dirempty __P((struct inode *, ino_t, struct ucred *));
int	ext2_checkpath __P((struct inode *, struct inode *, struct ucred *));
struct  ext2_group_desc * get_group_desc __P((struct mount * , 
		unsigned int , struct buf ** ));
void	ext2_discard_prealloc __P((struct inode *));
int	ext2_inactive __P((void *));
int 	ll_w_block __P((struct buf *, int ));
int	ext2_new_block __P ((struct mount * mp, u_int32_t goal,
			    int32_t * prealloc_count,
			    int32_t * prealloc_block));
ino_t	ext2_new_inode __P ((const struct inode * dir, int mode));
unsigned long ext2_count_free __P((struct buf *map, unsigned int numchars));
void	ext2_free_blocks (struct mount * mp, unsigned long block,
		       unsigned long count);
void	ext2_free_inode (struct inode * inode);
void	ext2_ei2di __P((struct ext2_inode *ei, struct dinode *di));
void	ext2_di2ei __P((struct dinode *di, struct ext2_inode *ei));
void	mark_buffer_dirty __P((struct buf *bh));

/*
 * This macro allows the ufs code to distinguish between an EXT2 and a
 * non-ext2(FFS/LFS) vnode.  We shouldn't need this!
 */
#define	IS_EXT2_VNODE(vp) (VFSTOUFS(vp->v_mount)->um_dirops->dirremove == ext2_dirremove)

#ifdef DIAGNOSTIC
void	ext2_checkoverlap __P((struct buf *, struct inode *));
#endif

extern int (**ext2_vnodeop_p) __P((void *));
extern int (**ext2_specop_p) __P((void *));
#ifdef FIFO
extern int (**ext2_fifoop_p) __P((void *));
#define EXT2_FIFOOPS ext2_fifoop_p
#else
#define EXT2_FIFOOPS NULL
#endif

#endif /* !_SYS_GNU_EXT2FS_EXT2_EXTERN_H_ */
