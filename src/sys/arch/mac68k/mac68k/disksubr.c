/*	$OpenBSD: disksubr.c,v 1.57 2009/05/21 23:45:48 krw Exp $	*/
/*	$NetBSD: disksubr.c,v 1.22 1997/11/26 04:18:20 briggs Exp $	*/

/*
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
 */

/*-
 * Copyright (C) 1993	Allen K. Briggs, Chris P. Caputo,
 *			Michael L. Finch, Bradley A. Grantham, and
 *			Lawrence A. Kesteloot
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
 *	This product includes software developed by the Alice Group.
 * 4. The names of the Alice Group or any of its members may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE ALICE GROUP ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE ALICE GROUP BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/* rewritten, 2-5-93 MLF */
/* it's a lot cleaner now, and adding support for new partition types
 * isn't a bitch anymore
 * known bugs:
 * 1) when only an HFS_PART part exists on a drive it gets assigned to "B"
 * this is because of line 623 of sd.c, I think this line should go.
 * 2) /sbin/disklabel expects the whole disk to be in "D", we put it in
 * "C" (I think) and we don't set that position in the disklabel structure
 * as used.  Again, not my fault.
 */
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/disk.h>
#include <sys/disklabel.h>
#include <sys/malloc.h>
#include <sys/syslog.h>

#include <mac68k/mac68k/dpme.h>	/* MF the structure of a mac partition entry */

#define NUM_PARTS_PROBED 32

#define ROOT_PART	1
#define UFS_PART	2
#define SWAP_PART	3
#define HFS_PART	4
#define SCRATCH_PART	5

int getFreeLabelEntry(struct disklabel *);
int whichType(struct partmapentry *);
int fixPartTable(struct partmapentry *, long, char *);
void setPart(struct partmapentry *, struct disklabel *, int, int);
int getNamedType(struct partmapentry *, int, struct disklabel *, int, int, int *);
char *read_mac_label(char *, struct disklabel *);

/*
 * Find an entry in the disk label that is unused and return it
 * or -1 if no entry
 */
int
getFreeLabelEntry(struct disklabel *lp)
{
	int i;

	for (i = 0; i < MAXPARTITIONS; i++) {
		if (i != RAW_PART &&
		    lp->d_partitions[i].p_fstype == FS_UNUSED)
			return i;
	}
	return -1;
}

/*
 * figure out what the type of the given part is and return it
 */
int
whichType(struct partmapentry *part)
{
	struct blockzeroblock *bzb;

	if (part->pmPartType[0] == '\0')
		return 0;

	if (strcmp(PART_DRIVER_TYPE, (char *)part->pmPartType) == 0 ||
	    strcmp(PART_DRIVER43_TYPE, (char *)part->pmPartType) == 0 ||
	    strcmp(PART_DRIVERATA_TYPE, (char *)part->pmPartType) == 0 ||
	    strcmp(PART_FWB_COMPONENT_TYPE, (char *)part->pmPartType) == 0 ||
	    strcmp(PART_PARTMAP_TYPE, (char *)part->pmPartType) == 0)
		return 0;
	if (strcmp(PART_UNIX_TYPE, (char *)part->pmPartType) == 0) {
		/* unix part, swap, root, usr */
		bzb = (struct blockzeroblock *)(&part->pmBootArgs);
		if (bzb->bzbMagic != BZB_MAGIC)
			return 0;

		if (bzb->bzbFlags & BZB_ROOTFS)
			return ROOT_PART;

		if ((bzb->bzbFlags & BZB_USRFS) ||
		    (bzb->bzbFlags & BZB_EXFS4) ||
		    (bzb->bzbFlags & BZB_EXFS5) ||
		    (bzb->bzbFlags & BZB_EXFS6))
			return UFS_PART;

		if (bzb->bzbType == BZB_TYPESWAP)
			return SWAP_PART;

		return SCRATCH_PART;
	}
	if (strcmp(PART_MAC_TYPE, (char *)part->pmPartType) == 0)
		return HFS_PART;
/*
	if (strcmp(PART_SCRATCH, (char *)part->pmPartType) == 0)
		return SCRATCH_PART;
*/
	return SCRATCH_PART;	/* no known type, but label it, anyway */
}

/*
 * Take part table in crappy form, place it in a structure we can depend
 * upon.  Make sure names are NUL terminated.  Capitalize the names
 * of part types.
 */
int
fixPartTable(struct partmapentry *partTable, long size, char *base)
{
	struct partmapentry *pmap;
	char *s;
	int i;

	for (i = 0; i < NUM_PARTS_PROBED; i++) {
		pmap = (struct partmapentry *)((i * size) + base + DEV_BSIZE);
		partTable[i] = *pmap;
		pmap = &partTable[i];

		if (pmap->pmSig != DPME_MAGIC) { /* this is not valid */
			pmap->pmPartType[0] = '\0';
			return i;
		}

		pmap->pmPartName[31] = '\0';
		pmap->pmPartType[31] = '\0';

		for (s = pmap->pmPartType; *s; s++)
			if ((*s >= 'a') && (*s <= 'z'))
				*s = (*s - 'a' + 'A');
	}
	return NUM_PARTS_PROBED;
}

void
setPart(struct partmapentry *part, struct disklabel *lp, int fstype, int slot)
{
	DL_SETPSIZE(&lp->d_partitions[slot], part->pmPartBlkCnt);
	DL_SETPOFFSET(&lp->d_partitions[slot], part->pmPyPartStart);
	lp->d_partitions[slot].p_fstype = fstype;
	part->pmPartType[0] = '\0';
}

int
getNamedType(struct partmapentry *part, int num_parts, struct disklabel *lp,
    int type, int alt, int *maxslot)
{
	struct blockzeroblock *bzb;
	int i;

	for (i = 0; i < num_parts; i++) {
		if (whichType(&(part[i])) == type) {
			switch (type) {
			case ROOT_PART:
				bzb = (struct blockzeroblock *)
				    (&part[i].pmBootArgs);
				if (alt >= 0 && alt != bzb->bzbCluster)
					goto skip;
				setPart(&(part[i]), lp, FS_BSDFFS, 0);
				break;
			case UFS_PART:
				bzb = (struct blockzeroblock *)
				    (&part[i].pmBootArgs);
				if (alt >= 0 && alt != bzb->bzbCluster)
					goto skip;
				setPart(&(part[i]), lp, FS_BSDFFS, 6);
				if (*maxslot < 6)
					*maxslot = 6;
				break;
			case SWAP_PART:
				setPart(&(part[i]), lp, FS_SWAP, 1);
				if (*maxslot < 1)
					*maxslot = 1;
				break;
			default:
				printf("disksubr.c: can't do type %d\n", type);
				break;
			}
			return 0;
		}
skip:
	}
	return -1;
}

/*
 * read in the entire diskpartition table, it may be bigger or smaller
 * than NUM_PARTS_PROBED but read that many entries.  Each entry has a magic
 * number so we'll know if an entry is crap.
 * next fill in the disklabel with info like this
 * next fill in the root, usr, and swap parts.
 * then look for anything else and fit it in.
 *	A: root
 *	B: Swap
 *	C: Whole disk
 *
 * AKB -- I added to Mike's original algorithm by searching for a bzbCluster
 *	of zero for root, first.  This allows A/UX to live on cluster 1 and
 *	NetBSD to live on cluster 0--regardless of the actual order on the
 *	disk.  This whole algorithm should probably be changed in the future.
 */
char *
read_mac_label(char *dlbuf, struct disklabel *lp)
{
	int i, num_parts, maxslot = RAW_PART;
	struct partmapentry *pmap;

	pmap = (struct partmapentry *)malloc(NUM_PARTS_PROBED *
	    sizeof(struct partmapentry), M_DEVBUF, M_NOWAIT);
	if (pmap == NULL)
		return ("out of memory");

	num_parts = fixPartTable(pmap, lp->d_secsize, dlbuf);
	if (getNamedType(pmap, num_parts, lp, ROOT_PART, 0, &maxslot))
		getNamedType(pmap, num_parts, lp, ROOT_PART, -1, &maxslot);
	getNamedType(pmap, num_parts, lp, SWAP_PART, -1, &maxslot);
	if (getNamedType(pmap, num_parts, lp, UFS_PART, 0, &maxslot))
		getNamedType(pmap, num_parts, lp, UFS_PART, -1, &maxslot);
	for (i = 0; i < num_parts; i++) {
		int partType, slot;

		slot = getFreeLabelEntry(lp);
		if (slot < 0)
			break;

		partType = whichType(&(pmap[i]));

		switch (partType) {
		case ROOT_PART:
		case UFS_PART:
			setPart(&(pmap[i]), lp, FS_BSDFFS, slot);
			if (slot > maxslot)
				maxslot = slot;
			break;
		case SWAP_PART:
			setPart(&(pmap[i]), lp, FS_SWAP, slot);
			if (slot > maxslot)
				maxslot = slot;
			break;
		case HFS_PART:
			setPart(&(pmap[i]), lp, FS_HFS, slot);
			if (slot > maxslot)
				maxslot = slot;
			break;
		case SCRATCH_PART:
			setPart(&(pmap[i]), lp, FS_OTHER, slot);
			if (slot > maxslot)
				maxslot = slot;
			break;
		default:
			break;
		}
	}
	lp->d_npartitions = MAXPARTITIONS;
	lp->d_version = 1;
	lp->d_checksum = 0;
	lp->d_checksum = dkcksum(lp);
	free(pmap, M_DEVBUF);
	return (checkdisklabel(lp, lp));
}

/*
 * Attempt to read a disk label from a device using the indicated strategy
 * routine.  The label must be partly set up before this: secpercyl and
 * anything required in the strategy routine (e.g., sector size) must be
 * filled in before calling us.  Returns null on success and an error
 * string on failure.
 */
char *
readdisklabel(dev_t dev, void (*strat)(struct buf *),
    struct disklabel *lp, int spoofonly)
{
	struct buf *bp = NULL;
	u_int16_t *sbSigp;
	int size;
	char *msg;

	if ((msg = initdisklabel(lp)))
		goto done;

	size = roundup((NUM_PARTS_PROBED + 1) << DEV_BSHIFT, lp->d_secsize);
	bp = geteblk(size);
	bp->b_dev = dev;

	if (spoofonly)
		goto doslabel;

	bp->b_blkno = LABELSECTOR;
	bp->b_bcount = size;
	bp->b_flags = B_BUSY | B_READ | B_RAW;
	(*strat)(bp);
	if (biowait(bp)) {
		msg = "disk label I/O error";
		goto done;
	}

	sbSigp = (u_int16_t *)bp->b_data;
	if (*sbSigp == 0x4552) {
		msg = read_mac_label(bp->b_data, lp);
		if (msg == NULL)
			goto done;
	}

	/* Get a MI label */
	bp->b_blkno = LABELSECTOR;
	bp->b_bcount = lp->d_secsize;
	bp->b_flags = B_BUSY | B_READ | B_RAW;
	(*strat)(bp);
	if (biowait(bp)) {
		msg = "disk label I/O error";
		goto done;
	}

	msg = checkdisklabel(bp->b_data + LABELOFFSET, lp);
	if (msg == NULL)
		goto done;

doslabel:
	msg = readdoslabel(bp, strat, lp, NULL, spoofonly);
	if (msg == NULL)
		goto done;

#if defined(CD9660)
	if (iso_disklabelspoof(dev, strat, lp) == 0) {
		msg = NULL;
		goto done;
	}
#endif
#if defined(UDF)
	if (udf_disklabelspoof(dev, strat, lp) == 0) {
		msg = NULL;
		goto done;
	}
#endif

done:
	if (bp) {
		bp->b_flags |= B_INVAL;
		brelse(bp);
	}
	return (msg);
}

/*
 * Write disk label back to device after modification.
 *
 * To avoid spreading havoc into the MacOS partition structures, we will
 * refuse to write a disklabel if the media has a MacOS signature.
 */
int
writedisklabel(dev_t dev, void (*strat)(struct buf *), struct disklabel *lp)
{
	struct buf *bp = NULL;
	struct disklabel *dlp;
	int error = 0;
	u_int16_t *sbSigp;

	/* get a buffer and initialize it */
	bp = geteblk((int)lp->d_secsize);
	bp->b_dev = dev;

	bp->b_blkno = LABELSECTOR;
	bp->b_bcount = lp->d_secsize;
	bp->b_flags = B_BUSY | B_READ | B_RAW;
	(*strat)(bp);
	if ((error = biowait(bp)) != 0)
		goto done;

	/* Check for MacOS fingerprints */
	sbSigp = (u_int16_t *)bp->b_data;
	if (*sbSigp == 0x4552) {
		/* XXX AND THEN DO NOT WRITE?? */
		goto done;
	}

	dlp = (struct disklabel *)(bp->b_data + LABELOFFSET);
	*dlp = *lp;
	bp->b_flags = B_BUSY | B_WRITE | B_RAW;
	(*strat)(bp);
	error = biowait(bp);

done:
	if (bp) {
		bp->b_flags |= B_INVAL;
		brelse(bp);
	}
	return (error);
}
