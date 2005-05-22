/*	$OpenBSD: sd_atapi.c,v 1.7 2005/05/22 21:11:31 krw Exp $	*/
/*	$NetBSD: sd_atapi.c,v 1.3 1998/08/31 22:28:07 cgd Exp $	*/

/*
 * Copyright 1998
 * Digital Equipment Corporation. All rights reserved.
 *
 * This software is furnished under license and may be used and
 * copied only in accordance with the following terms and conditions.
 * Subject to these conditions, you may download, copy, install,
 * use, modify and distribute this software in source and/or binary
 * form. No title or ownership is transferred hereby.
 *
 * 1) Any source code used, modified or distributed must reproduce
 *    and retain this copyright notice and list of conditions as
 *    they appear in the source file.
 *
 * 2) No right is granted to use any trade name, trademark, or logo of
 *    Digital Equipment Corporation. Neither the "Digital Equipment
 *    Corporation" name nor any trademark or logo of Digital Equipment
 *    Corporation may be used to endorse or promote products derived
 *    from this software without the prior written permission of
 *    Digital Equipment Corporation.
 *
 * 3) This software is provided "AS-IS" and any express or implied
 *    warranties, including but not limited to, any implied warranties
 *    of merchantability, fitness for a particular purpose, or
 *    non-infringement are disclaimed. In no event shall DIGITAL be
 *    liable for any damages whatsoever, and in particular, DIGITAL
 *    shall not be liable for special, indirect, consequential, or
 *    incidental damages or damages for lost profits, loss of
 *    revenue or loss of use, whether such damages arise in contract,
 *    negligence, tort, under statute, in equity, at law or otherwise,
 *    even if advised of the possibility of such damage.
 */

/*
 * ATAPI disk attachment for the 'sd' driver.
 *
 * Chris Demetriou, January 10, 1998.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/buf.h>
#include <sys/malloc.h>
#include <sys/errno.h>
#include <sys/device.h>
#include <sys/disk.h>
#include <sys/conf.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>
#include <scsi/atapi_disk.h>
#include <scsi/sdvar.h>

static int	sd_atapibus_get_parms(struct sd_softc *,
		    struct disk_parms *, int);

const struct sd_ops sd_atapibus_ops = {
	sd_atapibus_get_parms,
};

static int
sd_atapibus_get_parms(sd, dp, flags)
	struct sd_softc *sd;
	struct disk_parms *dp;
	int flags;
{
	struct atapi_read_format_capacities scsi_cmd;
	struct atapi_capacity_descriptor *descp;
	struct atapi_sd_mode_data sense_data;
	char capacity_data[ATAPI_CAP_DESC_SIZE(1)];
	u_int16_t rpm;
	int error;

	bzero(&scsi_cmd, sizeof scsi_cmd);
	scsi_cmd.opcode = ATAPI_READ_FORMAT_CAPACITIES;
	_lto2b(ATAPI_CAP_DESC_SIZE(1), scsi_cmd.length);

	error = scsi_scsi_cmd(sd->sc_link,
	    (struct scsi_generic *)&scsi_cmd, sizeof(scsi_cmd),
	    (void *)capacity_data, ATAPI_CAP_DESC_SIZE(1), SDRETRIES, 20000,
	    NULL, flags | SCSI_DATA_IN);
	SC_DEBUG(sd->sc_link, SDEV_DB2,
	    ("sd_atapibus_get_parms: read format capacities error=%d\n",
	    error));
	if (error != 0)
		return (SDGP_RESULT_OFFLINE);

	descp = (struct atapi_capacity_descriptor *)
	    &capacity_data[ATAPI_CAP_DESC_OFFSET_DESC(0)];

	switch (descp->byte5 & ATAPI_CAP_DESC_CODE_MASK) {
	case ATAPI_CAP_DESC_CODE_UNFORMATTED:
		return SDGP_RESULT_UNFORMATTED;

	case ATAPI_CAP_DESC_CODE_FORMATTED:
		break;

	case ATAPI_CAP_DESC_CODE_NONE:
		return SDGP_RESULT_OFFLINE;

	default:
#ifdef DIAGNOSTIC
		printf("%s: strange capacity descriptor byte5 0x%x\n",
		    sd->sc_dev.dv_xname, (u_int)descp->byte5);
#endif
		break;
	}

	/*
	 * Try to determine disk size, block size and geometry.  If disk size
	 * (in sectors) can be determined, then fake the rest if necessary.
	 *
	 * XXX Rigid geometry page?
	 */
	dp->rot_rate = 3600;
	dp->blksize = _3btol(descp->blklen);
	if (dp->blksize == 0)
		dp->blksize = 512;
	dp->disksize = _4btol(descp->nblks);
	if (dp->disksize == 0)
		return (SDGP_RESULT_OFFLINE);

	bzero(&sense_data, sizeof(sense_data));
	error = scsi_mode_sense_big(sd->sc_link, 0, ATAPI_FLEX_GEOMETRY_PAGE,
	    (struct scsi_mode_header_big *)&sense_data, sizeof(sense_data),
	    flags, 20000);
	SC_DEBUG(sd->sc_link, SDEV_DB2,
	    ("sd_atapibus_get_parms: mode sense (flex) error=%d\n", error));
	if (error == 0) {
		dp->heads = sense_data.pages.flex_geometry.nheads;
		dp->sectors = sense_data.pages.flex_geometry.ph_sec_tr;
		dp->cyls = _2btol(sense_data.pages.flex_geometry.ncyl);
		rpm = _2btol(sense_data.pages.flex_geometry.rpm);
		if (rpm)
			dp->rot_rate = rpm;
	} else {
		dp->heads = 64;
		dp->sectors = 32;
		dp->cyls = dp->disksize / (64 * 32);
	}
	
	return (SDGP_RESULT_OK);
}
