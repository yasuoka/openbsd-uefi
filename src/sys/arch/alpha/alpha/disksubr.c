/*	$OpenBSD: disksubr.c,v 1.9 1997/06/30 11:50:59 niklas Exp $	*/
/*	$NetBSD: disksubr.c,v 1.21 1996/05/03 19:42:03 christos Exp $	*/

/*
 * Copyright (c) 1996 Theo de Raadt
 * Copyright (c) 1982, 1986, 1988 Regents of the University of California.
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
 *	@(#)ufs_disksubr.c	7.16 (Berkeley) 5/4/91
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/device.h>
#include <sys/disklabel.h>
#include <sys/syslog.h>
#include <sys/disk.h>

#define	b_cylin	b_resid

#define BOOT_MAGIC 0xAA55
#define BOOT_MAGIC_OFF (DOSPARTOFF+NDOSPART*sizeof(struct dos_partition))

char   *readbsdlabel __P((struct buf *, void (*) __P((struct buf *)), int, int,
    int, struct disklabel *));
char   *readdoslabel __P((struct buf *, void (*) __P((struct buf *)),
    struct disklabel *, struct cpu_disklabel *));

static enum disklabel_tag probe_order[] = { LABELPROBES, -1 };

void
dk_establish(dk, dev)
	struct disk *dk;
	struct device *dev;
{
}

/*
 * Try to read a standard BSD disklabel at a certain sector.
 */
char *
readbsdlabel(bp, strat, cyl, sec, off, lp)
	struct buf *bp;
	void (*strat) __P((struct buf *));
	int cyl, sec, off;
	struct disklabel *lp;
{
	struct disklabel *dlp;
	char *msg = NULL;

	bp->b_blkno = sec;
	bp->b_cylin = cyl;
	bp->b_bcount = lp->d_secsize;
	bp->b_flags = B_BUSY | B_READ;
	(*strat)(bp);

	/* if successful, locate disk label within block and validate */
	if (biowait(bp)) {
		/* XXX we return the faked label built so far */
		msg = "disk label I/O error";
		return (msg);
	}

	/*
	 * If off is negative, search until the end of the sector for
	 * the label, otherwise, just look at the specific location
	 * we're given.
	 */
	dlp = (struct disklabel *)(bp->b_data + (off >= 0 ? off : 0));
	do {
		if (dlp->d_magic != DISKMAGIC || dlp->d_magic2 != DISKMAGIC) {
			if (msg == NULL)
				msg = "no disk label";
		} else if (dlp->d_npartitions > MAXPARTITIONS ||
		    dkcksum(dlp) != 0)
			msg = "disk label corrupted";
		else {
			*lp = *dlp;
			msg = NULL;
			break;
		}
		if (off >= 0)
			break;
		dlp = (struct disklabel *)((char *)dlp + sizeof(int32_t));
	} while (dlp <= (struct disklabel *)(bp->b_data + lp->d_secsize -
	    sizeof(*dlp)));
	return (msg);
}

/*
 * Attempt to read a disk label from a device
 * using the indicated stategy routine.
 * The label must be partly set up before this:
 * secpercyl, secsize and anything required for a block i/o read
 * operation in the driver's strategy/start routines
 * must be filled in before calling us.
 *
 * Returns null on success and an error string on failure.
 */
char *
readdisklabel(dev, strat, lp, osdep)
	dev_t dev;
	void (*strat) __P((struct buf *));
	struct disklabel *lp;
	struct cpu_disklabel *osdep;
{
	struct buf *bp;
	char *msg = "no disk label";
	enum disklabel_tag *tp;
	int i;
	struct disklabel savedlabel;

	/* minimal requirements for archtypal disk label */
	if (lp->d_secperunit == 0)
		lp->d_secsize = DEV_BSIZE;
	if (lp->d_secperunit == 0)
		lp->d_secperunit = 0x1fffffff;
	lp->d_npartitions = RAW_PART + 1;
	for (i = 0; i < RAW_PART; i++) {
		lp->d_partitions[i].p_size = 0;
		lp->d_partitions[i].p_offset = 0;
	}
	if (lp->d_partitions[i].p_size == 0)
		lp->d_partitions[i].p_size = 0x1fffffff;
	lp->d_partitions[i].p_offset = 0;
	savedlabel = *lp;

	/* get a buffer and initialize it */
	bp = geteblk((int)lp->d_secsize);
	bp->b_dev = dev;

	for (tp = probe_order; msg && *tp != -1; tp++) {
		switch (*tp) {
		case DLT_ALPHA:
#if defined(DISKLABEL_ALPHA) || defined(DISKLABEL_ALL) || defined(__alpha__)
			msg = readbsdlabel(bp, strat, 0, ALPHA_LABELSECTOR,
			    ALPHA_LABELOFFSET, lp);
			if (msg == NULL)
				lp->d_spare[4] = ALPHA_LABELSECTOR; /* XXX */
#endif
			break;

		case DLT_I386:
#if defined(DISKLABEL_I386) || defined(DISKLABEL_ALL) || defined(__i386__) || defined(__arc__)
			msg = readdoslabel(bp, strat, lp, osdep);
			if (msg == NULL)
				lp->d_spare[4] = bp->b_blkno;	/* XXX */
#endif
			break;

		default:
			panic("unrecognized disklabel tag %d", *tp);
		}
		if (msg)
			*lp = savedlabel;
	}

#if defined(CD9660)
	if (msg && iso_disklabelspoof(dev, strat, lp) == 0)
		msg = NULL;
#endif

	bp->b_flags |= B_INVAL;
	brelse(bp);
	return (msg);
}

/*
 * If dos partition table requested, attempt to load it and
 * find disklabel inside a DOS partition. Also, if bad block
 * table needed, attempt to extract it as well. Return buffer
 * for use in signalling errors if requested.
 *
 * We would like to check if each MBR has a valid BOOT_MAGIC, but
 * we cannot because it doesn't always exist. So.. we assume the
 * MBR is valid.
 */
char *
readdoslabel(bp, strat, lp, osdep)
	struct buf *bp;
	void (*strat) __P((struct buf *));
	struct disklabel *lp;
	struct cpu_disklabel *osdep;
{
	struct dos_partition *dp = osdep->dosparts, *dp2;
	struct dkbad *db, *bdp = &osdep->bad;
	char *msg = NULL, *cp;
	int dospartoff, cyl, i, ourpart = -1;
	dev_t dev;

	/* do dos partitions in the process of getting disklabel? */
	dospartoff = 0;
	cyl = I386_LABELSECTOR / lp->d_secpercyl;
	if (dp) {
	        daddr_t part_blkno = DOSBBSECTOR;
		unsigned long extoff = 0;
		int wander = 1, n = 0, loop = 0;

		/*
		 * Read dos partition table, follow extended partitions.
		 * Map the partitions to disklabel entries i-p
		 */
		while (wander && n < 8 && loop < 8) {
		        loop++;
			wander = 0;

			/* read boot record */
			bp->b_blkno = part_blkno;
			bp->b_bcount = lp->d_secsize;
			bp->b_flags = B_BUSY | B_READ;
			bp->b_cylin = part_blkno / lp->d_secpercyl;
			(*strat)(bp);
		     
			/* if successful, wander through dos partition table */
			if (biowait(bp)) {
				msg = "dos partition I/O error";
				return (msg);
			}
			bcopy(bp->b_data + DOSPARTOFF, dp,
			    NDOSPART * sizeof(*dp));

			if (ourpart == -1) {
				/* Search for our MBR partition */
				for (dp2=dp, i=0;
				    i < NDOSPART && ourpart == -1; i++, dp2++)
					if (dp2->dp_size &&
					    dp2->dp_typ == DOSPTYP_OPENBSD)
						ourpart = i;
				for (dp2=dp, i=0;
				    i < NDOSPART && ourpart == -1; i++, dp2++)
					if (dp2->dp_size &&
					    dp2->dp_typ == DOSPTYP_386BSD)
						ourpart = i;
				if (ourpart == -1)
					goto donot;
				/*
				 * This is our MBR partition. need sector
				 * address for SCSI/IDE, cylinder for
				 * ESDI/ST506/RLL
				 */
				dp2 = &dp[ourpart];
				dospartoff = dp2->dp_start + part_blkno;
				cyl = DPCYL(dp2->dp_scyl, dp2->dp_ssect);

				/* XXX build a temporary disklabel */
				lp->d_partitions[0].p_size = dp2->dp_size;
				lp->d_partitions[0].p_offset = dp2->dp_start +
				    part_blkno;
				if (lp->d_ntracks == 0)
					lp->d_ntracks = dp2->dp_ehd + 1;
				if (lp->d_nsectors == 0)
					lp->d_nsectors = DPSECT(dp2->dp_esect);
				if (lp->d_secpercyl == 0)
					lp->d_secpercyl = lp->d_ntracks *
					    lp->d_nsectors;
			}
donot:
			/*
			 * In case the disklabel read below fails, we want to
			 * provide a fake label in i-p.
			 */
			for (dp2=dp, i=0; i < NDOSPART && n < 8; i++, dp2++) {
				struct partition *pp = &lp->d_partitions[8+n];

				if (dp2->dp_size)
					pp->p_size = dp2->dp_size;
				if (dp2->dp_start)
					pp->p_offset =
					    dp2->dp_start + part_blkno;

				switch (dp2->dp_typ) {
				case DOSPTYP_UNUSED:
					for (cp = (char *)dp2;
					    cp < (char *)(dp2 + 1); cp++)
						if (*cp)
							break;
					/*
					 * Was it all zeroes?  If so, it is
					 * an unused entry that we don't
					 * want to show.
					 */
					if (cp == (char *)(dp2 + 1))
					    continue;
					lp->d_partitions[8 + n++].p_fstype =
					    FS_UNUSED;
					break;

				case DOSPTYP_LINUX:
					pp->p_fstype = FS_EXT2FS;
					n++;
					break;

				case DOSPTYP_FAT12:
				case DOSPTYP_FAT16S:
				case DOSPTYP_FAT16B:
				case DOSPTYP_FAT16C:
					pp->p_fstype = FS_MSDOS;
					n++;
					break;
				case DOSPTYP_EXTEND:
					part_blkno = dp2->dp_start + extoff;
					if (!extoff)
						extoff = dp2->dp_start;
					wander = 1;
					break;
				default:
					pp->p_fstype = FS_OTHER;
					n++;
					break;
				}
			}
		}
		lp->d_bbsize = 8192;
		lp->d_sbsize = 64*1024;		/* XXX ? */
		lp->d_npartitions = MAXPARTITIONS;
	}

	/* next, dig out disk label */
	msg = readbsdlabel(bp, strat, cyl, dospartoff + I386_LABELSECTOR, -1,
	    lp);
	if (msg)
		return (msg);

	/* obtain bad sector table if requested and present */
	if (bdp && (lp->d_flags & D_BADSECT)) {
		/*
		 * get a new buffer and initialize it as callers trust the
		 * buffer given to us, to point at the disklabel sector.
		 */
		dev = bp->b_dev;
		bp = geteblk((int)lp->d_secsize);
		bp->b_dev = dev;

		i = 0;
		do {
			/* read a bad sector table */
			bp->b_flags = B_BUSY | B_READ;
			bp->b_blkno = lp->d_secperunit - lp->d_nsectors + i;
			if (lp->d_secsize > DEV_BSIZE)
				bp->b_blkno *= lp->d_secsize / DEV_BSIZE;
			else
				bp->b_blkno /= DEV_BSIZE / lp->d_secsize;
			bp->b_bcount = lp->d_secsize;
			bp->b_cylin = lp->d_ncylinders - 1;
			(*strat)(bp);

			/* if successful, validate, otherwise try another */
			if (biowait(bp))
				msg = "bad sector table I/O error";
			else {
				db = (struct dkbad *)(bp->b_data);
#define DKBAD_MAGIC 0x4321
				if (db->bt_mbz == 0 &&
				    db->bt_flag == DKBAD_MAGIC) {
					msg = NULL;
					*bdp = *db;
					break;
				} else
					msg = "bad sector table corrupted";
			}
		} while ((bp->b_flags & B_ERROR) && (i += 2) < 10 &&
		    i < lp->d_nsectors);

		/* Give back the bad block buffer.  */
		bp->b_flags |= B_INVAL;
		brelse(bp);
	}
	return (msg);
}

/*
 * Check new disk label for sensibility
 * before setting it.
 */
int
setdisklabel(olp, nlp, openmask, osdep)
	struct disklabel *olp, *nlp;
	u_long openmask;
	struct cpu_disklabel *osdep;
{
	register i;
	register struct partition *opp, *npp;

	/* sanity clause */
	if (nlp->d_secpercyl == 0 || nlp->d_secsize == 0 ||
	    (nlp->d_secsize % DEV_BSIZE) != 0)
		return(EINVAL);

	/* XXX is this needed at all?  NetBSD/alpha doesn't have it.  */
	/* special case to allow disklabel to be invalidated */
	if (nlp->d_magic == 0xffffffff) {
		*olp = *nlp;
		return (0);
	}

	if (nlp->d_magic != DISKMAGIC || nlp->d_magic2 != DISKMAGIC ||
	    dkcksum(nlp) != 0)
		return (EINVAL);

	/* XXX missing check if other dos partitions will be overwritten */

	while (openmask != 0) {
		i = ffs((long)openmask) - 1;
		openmask &= ~(1 << i);
		if (nlp->d_npartitions <= i)
			return (EBUSY);
		opp = &olp->d_partitions[i];
		npp = &nlp->d_partitions[i];
		if (npp->p_offset != opp->p_offset ||
		    npp->p_size < opp->p_size)
			return (EBUSY);
		/*
		 * Copy internally-set partition information
		 * if new label doesn't include it.		XXX
		 */
		if (npp->p_fstype == FS_UNUSED && opp->p_fstype != FS_UNUSED) {
			npp->p_fstype = opp->p_fstype;
			npp->p_fsize = opp->p_fsize;
			npp->p_frag = opp->p_frag;
			npp->p_cpg = opp->p_cpg;
		}
	}
	nlp->d_checksum = 0;
	nlp->d_checksum = dkcksum(nlp);
	*olp = *nlp;
	return (0);
}


/*
 * Write disk label back to device after modification.
 * XXX cannot handle OpenBSD partitions in extended partitions!
 */
int
writedisklabel(dev, strat, lp, osdep)
	dev_t dev;
	void (*strat) __P((struct buf *));
	struct disklabel *lp;
	struct cpu_disklabel *osdep;
{
	enum disklabel_tag *tp;
	char *msg = "no disk label";
	struct buf *bp;
	struct disklabel dl;
#if defined(DISKLABEL_I386) || defined(DISKLABEL_ALL) || defined(__i386__) || defined(__arc__)
	struct cpu_disklabel cdl;
#endif
	int labeloffset, error, i;
	u_int64_t csum, *p;

	/* get a buffer and initialize it */
	bp = geteblk((int)lp->d_secsize);
	bp->b_dev = dev;

	for (tp = probe_order; msg && *tp != -1; tp++) {
		dl = *lp;
		switch (*tp) {
		case DLT_ALPHA:
#if defined(DISKLABEL_ALPHA) || defined(DISKLABEL_ALL) || defined(__alpha__)
			msg = readbsdlabel(bp, strat, 0, ALPHA_LABELSECTOR,
			    ALPHA_LABELOFFSET, &dl);
			labeloffset = ALPHA_LABELOFFSET;
#endif
			break;

		case DLT_I386:
#if defined(DISKLABEL_I386) || defined(DISKLABEL_ALL) || defined(__i386__) || defined(__arc__)
			msg = readdoslabel(bp, strat, &dl, &cdl);
			labeloffset = I386_LABELOFFSET;
#endif
			break;

		default:
			panic("unrecognized disklabel tag %d", *tp);
		}
	}

	if (msg) {
		error = ESRCH;
		goto done;
	}

	*(struct disklabel *)(bp->b_data + labeloffset) = *lp;

	/* Alpha bootblocks are checksummed.  */
	if (*tp == DLT_ALPHA) {
		for (csum = i = 0, p = (u_int64_t *)bp->b_data; i < 63; i++)
			csum += *p++;
		*p = csum;
	}

	bp->b_flags = B_BUSY | B_WRITE;
	(*strat)(bp);
	error = biowait(bp);

done:
	bp->b_flags |= B_INVAL;
	brelse(bp);
	return (error);
}

/*
 * Determine the size of the transfer, and make sure it is
 * within the boundaries of the partition. Adjust transfer
 * if needed, and signal errors or early completion.
 */
int
bounds_check_with_label(bp, lp, wlabel)
	struct buf *bp;
	struct disklabel *lp;
	int wlabel;
{
#define blockpersec(count, lp) ((count) * (((lp)->d_secsize) / DEV_BSIZE))
	struct partition *p = lp->d_partitions + DISKPART(bp->b_dev);
	int labelsector = blockpersec(lp->d_partitions[RAW_PART].p_offset, lp) +
	    lp->d_spare[4];	/* XXX */
	int sz = howmany(bp->b_bcount, DEV_BSIZE);

	if (bp->b_blkno + sz > blockpersec(p->p_size, lp)) {
		sz = blockpersec(p->p_size, lp) - bp->b_blkno;
		if (sz == 0) {
			/* If exactly at end of disk, return EOF. */
			bp->b_resid = bp->b_bcount;
			goto done;
		}
		if (sz < 0) {
			/* If past end of disk, return EINVAL. */
			bp->b_error = EINVAL;
			goto bad;
		}
		/* Otherwise, truncate request. */
		bp->b_bcount = sz << DEV_BSHIFT;
	}

	/* Overwriting disk label? */
	if (bp->b_blkno + blockpersec(p->p_offset, lp) <= labelsector &&
	    bp->b_blkno + blockpersec(p->p_offset, lp) + sz > labelsector &&
	    (bp->b_flags & B_READ) == 0 && !wlabel) {
		bp->b_error = EROFS;
		goto bad;
	}

	/* calculate cylinder for disksort to order transfers with */
	bp->b_cylin = (bp->b_blkno + blockpersec(p->p_offset, lp)) /
	    lp->d_secpercyl;
	return (1);

bad:
	bp->b_flags |= B_ERROR;
done:
	return (0);
}
