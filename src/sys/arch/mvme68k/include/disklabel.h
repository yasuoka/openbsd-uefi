/*	$OpenBSD: disklabel.h,v 1.2 1996/04/28 10:56:12 deraadt Exp $ */

/*
 * Copyright (c) 1995 Dale Rahn.
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
 *   This product includes software developed by Dale Rahn.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */  

#ifndef _MACHINE_DISKLABEL_H_
#define _MACHINE_DISKLABEL_H_

/* number of boot pieces , ie xxboot bootxx */
#define NUMBOOT		2

#define	PARTITIONSHIFT	4

#define LABELSECTOR     0                       /* sector containing label */
#define LABELOFFSET	0			/* offset of label in sector */
#define MAXPARTITIONS	(1 << PARTITIONSHIFT)	/* number of partitions */
#define RAW_PART	2			/* raw partition: xx?c */

/* 
 * used to encode disk minor numbers
 * this should probably be moved to sys/disklabel.h
 */
#define DISKUNIT(dev)	(minor(dev) / MAXPARTITIONS)
#define DISKPART(dev)	(minor(dev) % MAXPARTITIONS)
#define MAKEDISKDEV(maj, unit, part) \
    (makedev((maj), ((unit) * MAXPARTITIONS) + (part)))

struct cpu_disklabel {
	/* VID */
	u_char		vid_id[4];
	u_char		vid_0[16];
	u_int		vid_oss;
	u_short		vid_osl;
	u_char		vid_1[4];
	u_short		vid_osa_u;
	u_short		vid_osa_l;
	u_char		vid_2[2];
	u_short		partitions;
	u_char		vid_vd[16];
	u_long		bbsize;
	u_long		magic1;		/* 4 */
	u_short		type;		/* 2 */
	u_short		subtype;	/* 2 */
	u_char		packname[16];	/* 16 */
	u_long		flags;		/* 4 */
	u_long		drivedata[5];	/* 4 */
	u_long		spare[5];	/* 4 */
	u_short		checksum;	/* 2 */

	u_long		secpercyl;	/* 4 */
	u_long		secperunit;	/* 4 */
	u_long		headswitch;	/* 4 */

	u_char		vid_3[4];
	u_int		vid_cas;
	u_char		vid_cal;
	u_char		vid_4_0[3];
	u_char		vid_4[64];
	u_char		vid_4_1[28];
	u_long		sbsize;
	u_char		vid_mot[8];

	/* CFG */
	u_char		cfg_0[4];
	u_short		cfg_atm;
	u_short		cfg_prm;
	u_short		cfg_atw;
	u_short		cfg_rec;

	u_short		sparespertrack;
	u_short		sparespercyl;
	u_long		acylinders;
	u_short		rpm;
	u_short		cylskew;

	u_char		cfg_spt;
	u_char		cfg_hds;
	u_short		cfg_trk;
	u_char		cfg_ilv;
	u_char		cfg_sof;
	u_short		cfg_psm;
	u_short		cfg_shd;
	u_char		cfg_2[2];
	u_short		cfg_pcom;
	u_char		cfg_3;
	u_char		cfg_ssr;
	u_short		cfg_rwcc;
	u_short		cfg_ecc;
	u_short		cfg_eatm;
	u_short		cfg_eprm;
	u_short		cfg_eatw;
	u_char		cfg_gpb1;
	u_char		cfg_gpb2;
	u_char		cfg_gpb3;
	u_char		cfg_gpb4;
	u_char		cfg_ssc;
	u_char		cfg_runit;
	u_short		cfg_rsvc1;
	u_short		cfg_rsvc2;
	u_long		magic2;
	u_char		cfg_4[192];
};
#endif _MACHINE_DISKLABEL_H_
