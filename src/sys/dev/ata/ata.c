/*      $OpenBSD: ata.c,v 1.5 2000/04/07 11:22:46 niklas Exp $      */
/*      $NetBSD: ata.c,v 1.9 1999/04/15 09:41:09 bouyer Exp $      */
/*
 * Copyright (c) 1998 Manuel Bouyer.  All rights reserved.
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
 *  This product includes software developed by Manuel Bouyer.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/syslog.h>

#include <dev/ic/wdcreg.h>
#include <dev/ata/atareg.h>
#include <dev/ata/atavar.h>

#define DEBUG_FUNCS  0x08
#define DEBUG_PROBE  0x10
#ifdef WDCDEBUG
extern int wdcdebug_mask; /* init'ed in wdc.c */
#define WDCDEBUG_PRINT(args, level) \
        if (wdcdebug_mask & (level)) \
		printf args
#else
#define WDCDEBUG_PRINT(args, level)
#endif

#define ATAPARAMS_SIZE 512

/* Get the disk's parameters */
int
ata_get_params(drvp, flags, prms)
	struct ata_drive_datas *drvp;
	u_int8_t flags;
	struct ataparams *prms;
{
	char tb[ATAPARAMS_SIZE];
	struct wdc_command wdc_c;

	int i;
	u_int16_t *p;

	WDCDEBUG_PRINT(("ata_get_parms\n"), DEBUG_FUNCS);

	bzero(tb, sizeof(tb));
	bzero(prms, sizeof(struct ataparams));
	bzero(&wdc_c, sizeof(struct wdc_command));

	if (drvp->drive_flags & DRIVE_ATA) {
		wdc_c.r_command = WDCC_IDENTIFY;
		wdc_c.r_st_bmask = WDCS_DRDY;
		wdc_c.r_st_pmask = WDCS_DRQ;
		wdc_c.timeout = 1000; /* 1s */
	} else if (drvp->drive_flags & DRIVE_ATAPI) {
		wdc_c.r_command = ATAPI_IDENTIFY_DEVICE;
		wdc_c.r_st_bmask = 0;
		wdc_c.r_st_pmask = WDCS_DRQ;
		wdc_c.timeout = 10000; /* 10s */
	} else {
		return CMD_ERR;
	}
	wdc_c.flags = AT_READ | flags;
	wdc_c.data = tb;
	wdc_c.bcount = ATAPARAMS_SIZE;

	{
		int ret;
		if ((ret = wdc_exec_command(drvp, &wdc_c)) != WDC_COMPLETE) {
			printf ("WDC_EXEC_COMMAND: %d\n");
			return CMD_AGAIN;
		}
	}

	if (wdc_c.flags & (AT_ERROR | AT_TIMEOU | AT_DF)) {
		return CMD_ERR;
	} else {
#if BYTE_ORDER == BIG_ENDIAN
		/* All the fields in the params structure are 16-bit
		   integers except for the ID strings which are char
		   strings.  The 16-bit integers are currently in
		   memory in little-endian, regardless of architecture.
		   So, they need to be swapped on big-endian architectures
		   before they are accessed through the ataparams structure.

		   The swaps below avoid touching the char strings.
		*/
		  
		swap16_multi((u_int16_t *)tb, 10);
		swap16_multi((u_int16_t *)tb + 20, 3);
		swap16_multi((u_int16_t *)tb + 47, ATAPARAMS_SIZE / 2 - 47);
#endif
		/* Read in parameter block. */
		bcopy(tb, prms, sizeof(struct ataparams));

		/*
		 * Shuffle string byte order.
		 * ATAPI Mitsumi and NEC drives don't need this.
		 */
		if ((prms->atap_config & WDC_CFG_ATAPI_MASK) ==
		    WDC_CFG_ATAPI &&
		    ((prms->atap_model[0] == 'N' &&
			prms->atap_model[1] == 'E') ||
		     (prms->atap_model[0] == 'F' &&
			 prms->atap_model[1] == 'X')))
			return 0;
		for (i = 0; i < sizeof(prms->atap_model); i += 2) {
			p = (u_short *)(prms->atap_model + i);
			*p = swap16(*p);
		}
		for (i = 0; i < sizeof(prms->atap_serial); i += 2) {
			p = (u_short *)(prms->atap_serial + i);
			*p = swap16(*p);
		}
		for (i = 0; i < sizeof(prms->atap_revision); i += 2) {
			p = (u_short *)(prms->atap_revision + i);
			*p = swap16(*p);
		}

		return CMD_OK;
	}
}

int
ata_set_mode(drvp, mode, flags)
	struct ata_drive_datas *drvp;
	u_int8_t mode;
	u_int8_t flags;
{
	struct wdc_command wdc_c;

	WDCDEBUG_PRINT(("ata_set_mode=0x%x\n", mode), DEBUG_FUNCS);
	bzero(&wdc_c, sizeof(struct wdc_command));

	wdc_c.r_command = SET_FEATURES;
	wdc_c.r_st_bmask = 0;
	wdc_c.r_st_pmask = 0;
	wdc_c.r_precomp = WDSF_SET_MODE;
	wdc_c.r_count = mode;
	wdc_c.flags = AT_READ | flags;
	wdc_c.timeout = 1000; /* 1s */
	if (wdc_exec_command(drvp, &wdc_c) != WDC_COMPLETE)
		return CMD_AGAIN;
	if (wdc_c.flags & (AT_ERROR | AT_TIMEOU | AT_DF)) {
		return CMD_ERR;
	}
	return CMD_OK;
}

void
ata_perror(drvp, errno, buf)
	struct ata_drive_datas *drvp;
	int errno;
	char *buf;
{
	static char *errstr0_3[] = {"address mark not found",
	    "track 0 not found", "aborted command", "media change requested",
	    "id not found", "media changed", "uncorrectable data error",
	    "bad block detected"};
	static char *errstr4_5[] = {"",
	    "no media/write protected", "aborted command",
	    "media change requested", "id not found", "media changed",
	    "uncorrectable data error", "interface CRC error"};
	char **errstr;
	int i;
	char *sep = "";

	if (drvp->ata_vers >= 4)
		errstr = errstr4_5;
	else
		errstr = errstr0_3;

	if (errno == 0) {
		sprintf(buf, "error not notified");
	}

	for (i = 0; i < 8; i++) {
		if (errno & (1 << i)) {
			buf += sprintf(buf, "%s %s", sep, errstr[i]);
			sep = ",";
		}
	}
}
