/*	$OpenBSD: ffs_balloc.c,v 1.16 2001/11/13 00:10:56 art Exp $	*/
/*	$NetBSD: ffs_balloc.c,v 1.3 1996/02/09 22:22:21 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1989, 1993
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
 *	@(#)ffs_balloc.c	8.4 (Berkeley) 9/23/93
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <sys/mount.h>
#include <sys/vnode.h>

#include <uvm/uvm_extern.h>

#include <ufs/ufs/quota.h>
#include <ufs/ufs/inode.h>
#include <ufs/ufs/ufs_extern.h>

#include <ufs/ffs/fs.h>
#include <ufs/ffs/ffs_extern.h>

/*
 * Balloc defines the structure of file system storage
 * by allocating the physical blocks on a device given
 * the inode and the logical block number in a file.
 */
int
ffs_balloc(struct inode *ip, off_t startoffset, int size, struct ucred *cred,
    int flags, struct buf **bpp)
{
	daddr_t lbn;
	struct fs *fs;
	daddr_t nb;
	struct buf *bp, *nbp;
	struct vnode *vp;
	struct indir indirs[NIADDR + 2];
	daddr_t newb, *bap, pref;
	int deallocated, osize, nsize, num, i, error;
	daddr_t *allocib, *blkp, *allocblk, allociblk[NIADDR+1];
	int unwindidx = -1;

	vp = ITOV(ip);
	fs = ip->i_fs;
	lbn = lblkno(fs, startoffset);
	size = blkoff(fs, startoffset) + size;
	if (size > fs->fs_bsize)
		panic("ffs_balloc: blk too big");
	*bpp = NULL;
	if (lbn < 0)
		return (EFBIG);

	/*
	 * If the next write will extend the file into a new block,
	 * and the file is currently composed of a fragment
	 * this fragment has to be extended to be a full block.
	 */
	nb = lblkno(fs, ip->i_ffs_size);
	if (nb < NDADDR && nb < lbn) {
		osize = blksize(fs, ip, nb);
		if (osize < fs->fs_bsize && osize > 0) {
			error = ffs_realloccg(ip, nb,
			    ffs_blkpref(ip, nb, (int)nb, &ip->i_ffs_db[0]),
			    osize, (int)fs->fs_bsize, cred, &bp, &newb);
			if (error)
				return (error);
			if (DOINGSOFTDEP(vp))
				softdep_setup_allocdirect(ip, nb, newb,
				    ip->i_ffs_db[nb], fs->fs_bsize, osize, bp);

			ip->i_ffs_size = lblktosize(fs, nb + 1);
			uvm_vnp_setsize(vp, ip->i_ffs_size);
			ip->i_ffs_db[nb] = newb;
			ip->i_flag |= IN_CHANGE | IN_UPDATE;
			if (flags & B_SYNC)
				bwrite(bp);
			else
				bawrite(bp);
		}
	}
	/*
	 * The first NDADDR blocks are direct blocks
	 */
	if (lbn < NDADDR) {
		nb = ip->i_ffs_db[lbn];
		if (nb != 0 && ip->i_ffs_size >= lblktosize(fs, lbn + 1)) {
			error = bread(vp, lbn, fs->fs_bsize, NOCRED, &bp);
			if (error) {
				brelse(bp);
				return (error);
			}
			*bpp = bp;
			return (0);
		}
		if (nb != 0) {
			/*
			 * Consider need to reallocate a fragment.
			 */
			osize = fragroundup(fs, blkoff(fs, ip->i_ffs_size));
			nsize = fragroundup(fs, size);
			if (nsize <= osize) {
				error = bread(vp, lbn, osize, NOCRED, &bp);
				if (error) {
					brelse(bp);
					return (error);
				}
			} else {
				error = ffs_realloccg(ip, lbn,
				    ffs_blkpref(ip, lbn, (int)lbn,
					&ip->i_ffs_db[0]),
				    osize, nsize, cred, &bp, &newb);
				if (error)
					return (error);
				if (DOINGSOFTDEP(vp))
					softdep_setup_allocdirect(ip, lbn,
					    newb, nb, nsize, osize, bp);
			}
		} else {
			if (ip->i_ffs_size < lblktosize(fs, lbn + 1))
				nsize = fragroundup(fs, size);
			else
				nsize = fs->fs_bsize;
			error = ffs_alloc(ip, lbn,
			    ffs_blkpref(ip, lbn, (int)lbn, &ip->i_ffs_db[0]),
			    nsize, cred, &newb);
			if (error)
				return (error);
			bp = getblk(vp, lbn, nsize, 0, 0);
			bp->b_blkno = fsbtodb(fs, newb);
			if (flags & B_CLRBUF)
				clrbuf(bp);
			if (DOINGSOFTDEP(vp))
				softdep_setup_allocdirect(ip, lbn, newb, 0,
				    nsize, 0, bp);
		}
		ip->i_ffs_db[lbn] = dbtofsb(fs, bp->b_blkno);
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		*bpp = bp;
		return (0);
	}
	/*
	 * Determine the number of levels of indirection.
	 */
	pref = 0;
	if ((error = ufs_getlbns(vp, lbn, indirs, &num)) != 0)
		return(error);
#ifdef DIAGNOSTIC
	if (num < 1)
		panic ("ffs_balloc: ufs_bmaparray returned indirect block");
#endif
	/*
	 * Fetch the first indirect block allocating if necessary.
	 */
	--num;
	nb = ip->i_ffs_ib[indirs[0].in_off];

	allocib = NULL;
	allocblk = allociblk;
	if (nb == 0) {
		pref = ffs_blkpref(ip, lbn, 0, (daddr_t *)0);
	        error = ffs_alloc(ip, lbn, pref, (int)fs->fs_bsize,
				  cred, &newb);
		if (error)
			return (error);
		nb = newb;

		*allocblk++ = nb;
		bp = getblk(vp, indirs[1].in_lbn, fs->fs_bsize, 0, 0);
		bp->b_blkno = fsbtodb(fs, nb);
		clrbuf(bp);

		if (DOINGSOFTDEP(vp)) {
			softdep_setup_allocdirect(ip, NDADDR + indirs[0].in_off,
			    newb, 0, fs->fs_bsize, 0, bp);
			bdwrite(bp);
		} else {
			/*
			 * Write synchronously so that indirect blocks
			 * never point at garbage.
			 */
			if ((error = bwrite(bp)) != 0)
				goto fail;
		}
		allocib = &ip->i_ffs_ib[indirs[0].in_off];
		*allocib = nb;
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
	}
	/*
	 * Fetch through the indirect blocks, allocating as necessary.
	 */
	for (i = 1;;) {
		error = bread(vp,
		    indirs[i].in_lbn, (int)fs->fs_bsize, NOCRED, &bp);
		if (error) {
			brelse(bp);
			goto fail;
		}
		bap = (daddr_t *)bp->b_data;
		nb = bap[indirs[i].in_off];
		if (i == num)
			break;
		i++;
		if (nb != 0) {
			brelse(bp);
			continue;
		}
		if (pref == 0)
			pref = ffs_blkpref(ip, lbn, 0, (daddr_t *)0);
		error = ffs_alloc(ip, lbn, pref, (int)fs->fs_bsize, cred,
				  &newb);
		if (error) {
			brelse(bp);
			goto fail;
		}
		nb = newb;
		*allocblk++ = nb;
		nbp = getblk(vp, indirs[i].in_lbn, fs->fs_bsize, 0, 0);
		nbp->b_blkno = fsbtodb(fs, nb);
		clrbuf(nbp);

		if (DOINGSOFTDEP(vp)) {
			softdep_setup_allocindir_meta(nbp, ip, bp,
			    indirs[i - 1].in_off, nb);
			bdwrite(nbp);
		} else {
			/*
			 * Write synchronously so that indirect blocks
			 * never point at garbage.
			 */
			if ((error = bwrite(nbp)) != 0) {
				brelse(bp);
				goto fail;
			}
		}
		bap[indirs[i - 1].in_off] = nb;
		if (allocib == NULL && unwindidx < 0)
			unwindidx = i - 1;
		/*
		 * If required, write synchronously, otherwise use
		 * delayed write.
		 */
		if (flags & B_SYNC) {
			bwrite(bp);
		} else {
			bdwrite(bp);
		}
	}
	/*
	 * Get the data block, allocating if necessary.
	 */
	if (nb == 0) {
		pref = ffs_blkpref(ip, lbn, indirs[i].in_off, &bap[0]);
		error = ffs_alloc(ip, lbn, pref, (int)fs->fs_bsize, cred,
				  &newb);
		if (error) {
			brelse(bp);
			goto fail;
		}
		nb = newb;
		*allocblk++ = nb;
		nbp = getblk(vp, lbn, fs->fs_bsize, 0, 0);
		nbp->b_blkno = fsbtodb(fs, nb);
		if (flags & B_CLRBUF)
			clrbuf(nbp);
		if (DOINGSOFTDEP(vp))
			softdep_setup_allocindir_page(ip, lbn, bp,
			    indirs[i].in_off, nb, 0, nbp);
		bap[indirs[i].in_off] = nb;
		/*
		 * If required, write synchronously, otherwise use
		 * delayed write.
		 */
		if (flags & B_SYNC) {
			bwrite(bp);
		} else {
			bdwrite(bp);
		}
		*bpp = nbp;
		return (0);
	}
	brelse(bp);
	if (flags & B_CLRBUF) {
		error = bread(vp, lbn, (int)fs->fs_bsize, NOCRED, &nbp);
		if (error) {
			brelse(nbp);
			goto fail;
		}
	} else {
		nbp = getblk(vp, lbn, fs->fs_bsize, 0, 0);
		nbp->b_blkno = fsbtodb(fs, nb);
	}
	*bpp = nbp;
	return (0);

fail:
	/*
	 * If we have failed part way through block allocation, we
	 * have to deallocate any indirect blocks that we have allocated.
	 */
	for (deallocated = 0, blkp = allociblk; blkp < allocblk; blkp++) {
		ffs_blkfree(ip, *blkp, fs->fs_bsize);
		deallocated += fs->fs_bsize;
	}
	if (allocib != NULL) {
		*allocib = 0;
	} else if (unwindidx >= 0) {
		int r;

		r = bread(vp, indirs[unwindidx].in_lbn, 
		    (int)fs->fs_bsize, NOCRED, &bp);
		if (r)
			panic("Could not unwind indirect block, error %d", r);
		bap = (ufs_daddr_t *)bp->b_data;
		bap[indirs[unwindidx].in_off] = 0;
		if (flags & B_SYNC) {
			bwrite(bp);
		} else {
			bdwrite(bp);
		}
	}
	if (deallocated) {
#ifdef QUOTA
		/*
		 * Restore user's disk quota because allocation failed.
		 */
		(void)chkdq(ip, (long)-btodb(deallocated), cred, FORCE);
#endif
		ip->i_ffs_blocks -= btodb(deallocated);
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
	}

	return (error);
}
