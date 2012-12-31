/*	$OpenBSD: prtvid.c,v 1.8 2012/12/31 21:35:32 miod Exp $	*/

/*
 * Copyright (c) 1995 Dale Rahn <drahn@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/disklabel.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define __DBINTERFACE_PRIVATE
#include <db.h>

static void
swabvid(struct mvmedisklabel *cdl)
{
	M_32_SWAP(cdl->vid_oss);
	M_16_SWAP(cdl->vid_osl);
	M_16_SWAP(cdl->vid_osa_u);
	M_16_SWAP(cdl->vid_osa_l);
	M_32_SWAP(cdl->vid_cas);
}

static void
swabcfg(struct mvmedisklabel *cdl)
{
	printf("swapping cfg\n");

	M_16_SWAP(cdl->cfg_atm);
	M_16_SWAP(cdl->cfg_prm);
	M_16_SWAP(cdl->cfg_atm);
	M_16_SWAP(cdl->cfg_rec);
	M_16_SWAP(cdl->cfg_trk);
	M_16_SWAP(cdl->cfg_psm);
	M_16_SWAP(cdl->cfg_shd);
	M_16_SWAP(cdl->cfg_pcom);
	M_16_SWAP(cdl->cfg_rwcc);
	M_16_SWAP(cdl->cfg_ecc);
	M_16_SWAP(cdl->cfg_eatm);
	M_16_SWAP(cdl->cfg_eprm);
	M_16_SWAP(cdl->cfg_eatw);
	M_16_SWAP(cdl->cfg_rsvc1);
	M_16_SWAP(cdl->cfg_rsvc2);
}

int
main(int argc, char *argv[])
{
	struct mvmedisklabel *cdl;

	cdl = (struct mvmedisklabel *) malloc(sizeof (struct mvmedisklabel));

	fread(cdl, sizeof(struct mvmedisklabel), 1, stdin);

	if (BYTE_ORDER != BIG_ENDIAN)
		swabvid(cdl);

	printf("vid_id		%s	%lx\n", cdl->vid_id,
	    (char *)&(cdl->vid_id[4]) - (char *)cdl);
	printf("vid_oss		%x	%lx\n", cdl->vid_oss,
	    (char *)&(cdl->vid_oss) - (char *)cdl);
	printf("vid_osl		%x	%lx\n", cdl->vid_osl,
	    (char *)&(cdl->vid_osl) - (char *)cdl);
	printf("vid_osa_u	%x	%lx\n", cdl->vid_osa_u,
	    (char *)&(cdl->vid_osa_u) - (char *)cdl);
	printf("vid_osa_l	%x	%lx\n", cdl->vid_osa_l,
	    (char *)&(cdl->vid_osa_l) - (char *)cdl);
	printf("vid_vd %lx\n",
	    (char *)&(cdl->vid_vd) - (char *)cdl);
	printf("vid_cas		%x	%lx\n", cdl->vid_cas,
	    (char *)&(cdl->vid_cas) - (char *)cdl);
	printf("vid_cal		%x	%lx\n", cdl->vid_cal,
	    (char *)&(cdl->vid_cal) - (char *)cdl);
	printf("vid_moto	%s	%lx\n", cdl->vid_mot,
	    (char *)&(cdl->vid_mot[0]) - (char *)cdl);

	if (BYTE_ORDER != BIG_ENDIAN)
		swabcfg(cdl);

	printf("cfg_atm		%x	%lx\n", cdl->cfg_atm,
	    (char *)&(cdl->cfg_atm) - (char *)(cdl));
	printf("cfg_prm		%x	%lx\n", cdl->cfg_prm,
	    (char *)&(cdl->cfg_prm) - (char *)(cdl));
	printf("cfg_atw		%x	%lx\n", cdl->cfg_atw,
	    (char *)&(cdl->cfg_atw) - (char *)(cdl));
	printf("cfg_rec		%lx	%lx\n",(long)cdl->cfg_rec,
	    (char *)&(cdl->cfg_rec) - (char *)(cdl));
	printf("cfg_spt		%x	%lx\n", cdl->cfg_spt,
	    (char *)&(cdl->cfg_spt) - (char *)(cdl));
	printf("cfg_hds		%x	%lx\n", cdl->cfg_hds,
	    (char *)&(cdl->cfg_hds) - (char *)(cdl));
	printf("cfg_trk		%x	%lx\n", cdl->cfg_trk,
	    (char *)&(cdl->cfg_trk) - (char *)(cdl));
	printf("cfg_ilv		%x	%lx\n", cdl->cfg_ilv,
	    (char *)&(cdl->cfg_ilv) - (char *)(cdl));
	printf("cfg_sof		%x	%lx\n", cdl->cfg_sof,
	    (char *)&(cdl->cfg_sof) - (char *)(cdl));
	printf("cfg_psm		%x	%lx\n", cdl->cfg_psm,
	    (char *)&(cdl->cfg_psm) - (char *)(cdl));
	printf("cfg_shd		%x	%lx\n", cdl->cfg_shd,
	    (char *)&(cdl->cfg_shd) - (char *)(cdl));
	printf("cfg_pcom	%x	%lx\n", cdl->cfg_pcom,
	    (char *)&(cdl->cfg_pcom) - (char *)(cdl));
	printf("cfg_ssr 	%x	%lx\n", cdl->cfg_ssr,
	    (char *)&(cdl->cfg_ssr) - (char *)(cdl));
	printf("cfg_rwcc	%x	%lx\n", cdl->cfg_rwcc,
	    (char *)&(cdl->cfg_rwcc) - (char *)(cdl));
	printf("cfg_ecc 	%x	%lx\n", cdl->cfg_ecc,
	    (char *)&(cdl->cfg_ecc) - (char *)(cdl));
	printf("cfg_eatm	%x	%lx\n", cdl->cfg_eatm,
	    (char *)&(cdl->cfg_eatm) - (char *)(cdl));
	printf("cfg_eprm	%x	%lx\n", cdl->cfg_eprm,
	    (char *)&(cdl->cfg_eprm) - (char *)(cdl));
	printf("cfg_eatw	%x	%lx\n", cdl->cfg_eatw,
	    (char *)&(cdl->cfg_eatw) - (char *)(cdl));
	printf("cfg_gpb1	%x	%lx\n", cdl->cfg_gpb1,
	    (char *)&(cdl->cfg_gpb1) - (char *)(cdl));
	printf("cfg_gpb2	%x	%lx\n", cdl->cfg_gpb2,
	    (char *)&(cdl->cfg_gpb2) - (char *)(cdl));
	printf("cfg_gpb3	%x	%lx\n", cdl->cfg_gpb3,
	    (char *)&(cdl->cfg_gpb3) - (char *)(cdl));
	printf("cfg_gpb4	%x	%lx\n", cdl->cfg_gpb4,
	    (char *)&(cdl->cfg_gpb4) - (char *)(cdl));
	printf("cfg_ssc		%x	%lx\n", cdl->cfg_ssc,
	    (char *)&(cdl->cfg_ssc) - (char *)(cdl));
	printf("cfg_runit	%x	%lx\n", cdl->cfg_runit,
	    (char *)&(cdl->cfg_runit) - (char *)(cdl));
	printf("cfg_rsvc1	%x	%lx\n", cdl->cfg_rsvc1,
	    (char *)&(cdl->cfg_rsvc1) - (char *)(cdl));
	printf("cfg_rsvc2	%x	%lx\n", cdl->cfg_rsvc2,
	    (char *)&(cdl->cfg_rsvc2) - (char *)(cdl));

	return 0;
}
