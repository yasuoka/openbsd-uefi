/*	$OpenBSD: rf_raid5.h,v 1.3 2002/12/16 07:01:04 tdeval Exp $	*/
/*	$NetBSD: rf_raid5.h,v 1.3 1999/02/05 00:06:16 oster Exp $	*/

/*
 * Copyright (c) 1995 Carnegie-Mellon University.
 * All rights reserved.
 *
 * Author: Mark Holland
 *
 * Permission to use, copy, modify and distribute this software and
 * its documentation is hereby granted, provided that both the copyright
 * notice and this permission notice appear in all copies of the
 * software, derivative works or modified versions, and any portions
 * thereof, and that both notices appear in supporting documentation.
 *
 * CARNEGIE MELLON ALLOWS FREE USE OF THIS SOFTWARE IN ITS "AS IS"
 * CONDITION.  CARNEGIE MELLON DISCLAIMS ANY LIABILITY OF ANY KIND
 * FOR ANY DAMAGES WHATSOEVER RESULTING FROM THE USE OF THIS SOFTWARE.
 *
 * Carnegie Mellon requests users of this software to return to
 *
 *  Software Distribution Coordinator  or  Software.Distribution@CS.CMU.EDU
 *  School of Computer Science
 *  Carnegie Mellon University
 *  Pittsburgh PA 15213-3890
 *
 * any improvements or extensions that they make and grant Carnegie the
 * rights to redistribute these changes.
 */

/* rf_raid5.h - Header file for RAID Level 5. */

#ifndef _RF__RF_RAID5_H_
#define _RF__RF_RAID5_H_

int  rf_ConfigureRAID5(RF_ShutdownList_t **, RF_Raid_t *, RF_Config_t *);
int  rf_GetDefaultNumFloatingReconBuffersRAID5(RF_Raid_t *);
RF_HeadSepLimit_t rf_GetDefaultHeadSepLimitRAID5(RF_Raid_t *);
void rf_MapSectorRAID5(RF_Raid_t *, RF_RaidAddr_t,
	RF_RowCol_t *, RF_RowCol_t *, RF_SectorNum_t *, int);
void rf_MapParityRAID5(RF_Raid_t *, RF_RaidAddr_t,
	RF_RowCol_t *, RF_RowCol_t *, RF_SectorNum_t *, int);
void rf_IdentifyStripeRAID5(RF_Raid_t *, RF_RaidAddr_t,
	RF_RowCol_t **, RF_RowCol_t *);
void rf_MapSIDToPSIDRAID5(RF_RaidLayout_t *,
	RF_StripeNum_t, RF_StripeNum_t *, RF_ReconUnitNum_t *);
void rf_RaidFiveDagSelect(RF_Raid_t *, RF_IoType_t, RF_AccessStripeMap_t *,
	RF_VoidFuncPtr *);

#endif	/* !_RF__RF_RAID5_H_ */
