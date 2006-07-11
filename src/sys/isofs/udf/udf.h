/*	$OpenBSD: udf.h,v 1.10 2006/07/11 22:02:08 pedro Exp $	*/

/*
 * Copyright (c) 2001, 2002 Scott Long <scottl@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
 * $FreeBSD: src/sys/fs/udf/udf.h,v 1.9 2004/10/29 10:40:58 phk Exp $
 */

/*
 * Ported to OpenBSD by Pedro Martelletto <pedro@openbsd.org> in February 2005.
 */

#define UDF_HASHTBLSIZE 100

struct unode {
	LIST_ENTRY(unode) u_le;
	struct vnode *u_vnode;
	struct vnode *u_devvp;
	struct umount *u_ump;
	struct lock u_lock;
	dev_t u_dev;
	ino_t u_ino;
	union {
		long u_diroff;
		long u_vatlen;
	} un_u;
	struct file_entry *u_fentry;
};

#define	u_diroff	un_u.u_diroff
#define	u_vatlen	un_u.u_vatlen

struct umount {
	int um_flags;
	struct mount *um_mountp;
	struct vnode *um_devvp;
	dev_t um_dev;
	int um_bsize;
	int um_bshift;
	int um_bmask;
	uint32_t um_start;
	uint32_t um_len;
	struct unode *um_vat;
	struct long_ad um_root_icb;
	LIST_HEAD(udf_hash_lh, unode) *um_hashtbl;
	u_long um_hashsz;
	struct mutex um_hashmtx;
	int um_psecs;
	int um_stbl_len;
	struct udf_sparing_table *um_stbl;
};

#define	UDF_MNT_FIND_VAT	0x01	/* Indicates a VAT must be found */
#define	UDF_MNT_USES_VAT	0x02	/* Indicates a VAT must be used */

struct udf_dirstream {
	struct unode	*node;
	struct umount	*ump;
	struct buf	*bp;
	uint8_t		*data;
	uint8_t		*buf;
	int		fsize;
	int		off;
	int		this_off;
	int		offset;
	int		size;
	int		error;
	int		fid_fragment;
};

#define	VFSTOUDFFS(mp)	((struct umount *)((mp)->mnt_data))
#define	VTOU(vp)	((struct unode *)((vp)->v_data))

/*
 * The block layer refers to things in terms of 512 byte blocks by default.
 * btodb() is expensive, so speed things up.
 * Can the block layer be forced to use a different block size?
 */
#define	RDSECTOR(devvp, sector, size, bp) \
	bread(devvp, sector << (ump->um_bshift - DEV_BSHIFT), size, NOCRED, bp)

static __inline int
udf_readlblks(struct umount *ump, int sector, int size, struct buf **bp)
{
	return (RDSECTOR(ump->um_devvp, sector,
			 (size + ump->um_bmask) & ~ump->um_bmask, bp));
}

static __inline int
udf_readalblks(struct umount *ump, int lsector, int size, struct buf **bp)
{
	daddr_t rablock, lblk;
	int rasize;

	lblk = (lsector + ump->um_start) << (ump->um_bshift - DEV_BSHIFT);
	rablock = (lblk + 1) << ump->um_bshift;
	rasize = size;

	return (breadn(ump->um_devvp, lblk,
		       (size + ump->um_bmask) & ~ump->um_bmask,
		       &rablock, &rasize, 1,  NOCRED, bp));
}

/*
 * Produce a suitable file number from an ICB.  The passed in ICB is expected
 * to be in little endian (meaning that it hasn't been swapped for big
 * endian machines yet).
 * If the fileno resolves to 0, we might be in big trouble.
 * Assumes the ICB is a long_ad.  This struct is compatible with short_ad,
 *     but not ext_ad.
 */
static __inline ino_t
udf_getid(struct long_ad *icb)
{
	return (letoh32(icb->loc.lb_num));
}

int udf_allocv(struct mount *, struct vnode **, struct proc *);
int udf_hashlookup(struct umount *, ino_t, int, struct vnode **);
int udf_hashins(struct unode *);
int udf_hashrem(struct unode *);
int udf_checktag(struct desc_tag *, uint16_t);

typedef	uint16_t unicode_t;
