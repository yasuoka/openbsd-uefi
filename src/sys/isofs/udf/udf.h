/*	$OpenBSD: udf.h,v 1.7 2006/07/08 23:11:59 pedro Exp $	*/

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
	struct udf_mnt *u_ump;
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

struct udf_mnt {
	int			im_flags;
	struct mount		*im_mountp;
	struct vnode		*im_devvp;
	dev_t			im_dev;
	int			bsize;
	int			bshift;
	int			bmask;
	uint32_t		part_start;
	uint32_t		part_len;
	struct vnode		*im_vat;
	struct vnode		*root_vp;
	struct long_ad		root_icb;
	LIST_HEAD(udf_hash_lh, unode)	*hashtbl;
	u_long			hashsz;
	struct mutex		hash_mtx;
	int			p_sectors;
	int			s_table_entries;
	struct udf_sparing_table *s_table;
};

#define	UDF_MNT_FIND_VAT	0x01	/* Indicates a VAT must be found */
#define	UDF_MNT_USES_VAT	0x02	/* Indicates a VAT must be used */

struct udf_dirstream {
	struct unode	*node;
	struct udf_mnt	*udfmp;
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

#define	VFSTOUDFFS(mp)	((struct udf_mnt *)((mp)->mnt_data))
#define	VTOU(vp)	((struct unode *)((vp)->v_data))

/*
 * The block layer refers to things in terms of 512 byte blocks by default.
 * btodb() is expensive, so speed things up.
 * Can the block layer be forced to use a different block size?
 */
#define	RDSECTOR(devvp, sector, size, bp) \
	bread(devvp, sector << (udfmp->bshift - DEV_BSHIFT), size, NOCRED, bp)

static __inline int
udf_readlblks(struct udf_mnt *udfmp, int sector, int size, struct buf **bp)
{
	return (RDSECTOR(udfmp->im_devvp, sector,
			 (size + udfmp->bmask) & ~udfmp->bmask, bp));
}

static __inline int
udf_readalblks(struct udf_mnt *udfmp, int lsector, int size, struct buf **bp)
{
	daddr_t rablock, lblk;
	int rasize;

	lblk = (lsector + udfmp->part_start) << (udfmp->bshift - DEV_BSHIFT);
	rablock = (lblk + 1) << udfmp->bshift;
	rasize = size;

	return (breadn(udfmp->im_devvp, lblk,
		       (size + udfmp->bmask) & ~udfmp->bmask,
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
int udf_hashlookup(struct udf_mnt *, ino_t, int, struct vnode **);
int udf_hashins(struct unode *);
int udf_hashrem(struct unode *);
int udf_checktag(struct desc_tag *, uint16_t);

typedef	uint16_t unicode_t;
