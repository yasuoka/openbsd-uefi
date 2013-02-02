/*	$OpenBSD: autoconf.c,v 1.10 2013/02/02 13:34:29 miod Exp $	*/
/*	$NetBSD: autoconf.c,v 1.12 1997/01/30 10:32:51 thorpej Exp $	*/

/*
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department.
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
 *
 * from: Utah Hdr: autoconf.c 1.16 92/05/29
 *
 *	@(#)autoconf.c	8.1 (Berkeley) 6/10/93
 */

#include <sys/param.h>
#include <sys/reboot.h>

#include <lib/libsa/stand.h>

#include "samachdep.h"
#include "consdefs.h"
#include "rominfo.h"
#include "device.h"

#include <hp300/dev/dioreg.h>
#include <hp300/dev/diodevs.h>
#include <hp300/dev/diofbreg.h>

void	configure(void);
void	find_devs(void);
u_long	msustobdev(void);
void	printrominfo(void);

/*
 * Mapping of ROM MSUS types to BSD major device numbers
 * WARNING: major numbers must match bdevsw indices in hp300/conf.c.
 */
char rom2mdev[] = {
	0, 0, 						/* 0-1: none */
	6,	/* 2: network device; special */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,		/* 3-13: none */
	4,	/* 14: SCSI disk */
	0,	/* 15: none */
	2,	/* 16: CS/80 device on HPIB */
	2,	/* 17: CS/80 device on HPIB */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,	/* 18-31: none */
};

struct hp_hw sc_table[MAXCTLRS];
int cpuspeed;

extern int internalhpib;

void	find_devs(void);

#ifdef PRINTROMINFO
void
printrominfo()
{
	struct rominfo *rp = (struct rominfo *)ROMADDR;

	printf("boottype %lx, name %s, lowram %lx, sysflag %x\n",
	       rp->boottype, rp->name, rp->lowram, rp->sysflag&0xff);
	printf("rambase %lx, ndrives %x, sysflag2 %x, msus %lx\n",
	       rp->rambase, rp->ndrives, rp->sysflag2&0xff, rp->msus);
}
#endif

void
configure()
{
	switch (machineid) {
	case HP_320:
	case HP_330:
	case HP_340:
		cpuspeed = MHZ_16;
		break;
	case HP_350:
	case HP_36X:
		cpuspeed = MHZ_25;
		break;
	case HP_370:
		cpuspeed = MHZ_33;
		break;
	case HP_380:
	case HP_382:
	case HP_425:
		cpuspeed = MHZ_25 * 2;	/* XXX */
		break;
	case HP_385:
	case HP_433:
		cpuspeed = MHZ_33 * 2;	/* XXX */
		break;
	case HP_345:
	case HP_375:
	case HP_400:
	default:	/* assume the fastest (largest delay value) */
		cpuspeed = MHZ_50;
		break;
	}
	find_devs();
	cninit();
#ifdef PRINTROMINFO
	printrominfo();
#endif
	hpibinit();
	scsiinit();
	if ((bootdev & B_MAGICMASK) != B_DEVMAGIC)
		bootdev = msustobdev();
}

/*
 * Convert HP MSUS to a valid bootdev layout:
 *	TYPE comes from MSUS device type as mapped by rom2mdev
 *	PARTITION is set to 0 ('a')
 *	UNIT comes from MSUS unit (almost always 0)
 *	CONTROLLER comes from MSUS primary address
 *	ADAPTER comes from SCSI/HPIB driver logical unit number (hw_ctrl)
 */
u_long
msustobdev()
{
	struct rominfo *rp = (struct rominfo *)ROMADDR;
	u_long bdev = 0;
	struct hp_hw *hw;
	int sc, type, ctlr, slave, punit;

	sc = (rp->msus >> 8) & 0xFF;
	for (hw = sc_table; hw < &sc_table[MAXCTLRS]; hw++)
		if (hw->hw_sc == sc)
			break;

	type  = rom2mdev[(rp->msus >> 24) & 0x1F];
	ctlr  = hw->hw_ctrl;
	slave = (rp->msus & 0xFF);
	punit = ((rp->msus >> 16) & 0xFF);

	bdev  = MAKEBOOTDEV(type, ctlr, slave, punit, 0);

#ifdef PRINTROMINFO
	printf("msus %lx -> bdev %lx\n", rp->msus, bdev);
#endif
	return (bdev);
}

u_long
sctoaddr(int sc)
{
	if (sc == -1)
		return (GRFIADDR);
	if (sc == 7 && internalhpib)
		return (internalhpib);
	if (DIO_ISDIO(sc))
		return (DIO_BASE + sc * DIO_DEVSIZE);
	if (DIO_ISDIOII(sc))
		return (DIOII_BASE + (sc - DIOII_SCBASE) * DIOII_DEVSIZE);
	return 0;
}

/*
 * Probe all DIO select codes (0 - 32), the internal display address,
 * and DIO-II select codes (132 - 256). SGC frame buffers are probed
 * separately.
 *
 * Note that we only care about displays, LANCEs, SCSIs and HP-IBs.
 */
void
find_devs()
{
	short sc, sctop;
	u_char *id_reg;
	caddr_t addr;
	struct hp_hw *hw;

	hw = sc_table;
	sctop = machineid == HP_320 ? 32 : 256;	/* DIO_SCMAX(machineid); */
	/* starting at -1 to probe the intio framebuffer, if any */
	for (sc = -1; sc < sctop; sc++) {
		if (sc >= 32 && sc < DIOII_SCBASE)
			continue;
		addr = (caddr_t)sctoaddr(sc);
		if (badaddr(addr))
			continue;

		id_reg = (u_char *)addr;
		hw->hw_kva = addr;
		hw->hw_type = 0;
		hw->hw_sc = sc;
		hw->hw_ctrl = 0;

		/*
		 * Not all internal HP-IBs respond rationally to id requests
		 * so we just go by the "internal HPIB" indicator in SYSFLAG.
		 */
		if (sc == 7 && internalhpib) {
			hw->hw_type = C_HPIB;
			hw++;
			continue;
		}

		switch (id_reg[DIO_IDOFF]) {
		case DIO_DEVICE_ID_DCM:
		case DIO_DEVICE_ID_DCMREM:
			hw->hw_type = D_COMMDCM;
			break;
		case DIO_DEVICE_ID_FHPIB:
		case DIO_DEVICE_ID_NHPIB:
			hw->hw_type = C_HPIB;
			break;
		case DIO_DEVICE_ID_LAN:
		case DIO_DEVICE_ID_LANREM:	/* does this even make sense? */
			hw->hw_type = D_LAN;
			break;
		case DIO_DEVICE_ID_FRAMEBUFFER:
			hw->hw_type = D_BITMAP;
			switch(id_reg[DIO_SECIDOFF]) {
			case DIO_DEVICE_SECID_RENAISSANCE:
			case DIO_DEVICE_SECID_DAVINCI:
				sc += 2 - 1;	/* occupy 2 select codes */
				break;
			case DIO_DEVICE_SECID_TIGERSHARK:
				sc += 3 - 1;	/* occupy 3 select codes */
				break;
			case DIO_DEVICE_SECID_FB3X2_A:
			case DIO_DEVICE_SECID_FB3X2_B:
				sc += 4 - 1;	/* occupy 4 select codes */
				break;
			}
			break;
		case DIO_DEVICE_ID_SCSI0:
		case DIO_DEVICE_ID_SCSI1:
		case DIO_DEVICE_ID_SCSI2:
		case DIO_DEVICE_ID_SCSI3:
			hw->hw_type = C_SCSI;
			break;
		default:	/* who cares */
			break;
		}
		if (hw->hw_type == 0)
			continue;
		hw++;
		if (hw == sc_table + MAXCTLRS)
			break;	/* oflows are so boring */
	}
}
