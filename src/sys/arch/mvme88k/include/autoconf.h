/*	$OpenBSD: autoconf.h,v 1.5 1999/02/09 06:36:25 smurph Exp $ */
/*
 * Copyright (c) 1999, Steve Murphree, Jr.
 * Copyright (c) 1996 Nivas Madhur
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
 *      This product includes software developed by Nivas Madhur.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
 *
 */
/*
 * Autoconfiguration information.
 */

#ifndef _MVME88K_AUTOCONF_H_
#define _MVME88K_AUTOCONF_H_

#if 0
struct confargs {
	int	ca_bustype;
	caddr_t	ca_parent;
	caddr_t	ca_vaddr;
	caddr_t	ca_paddr;
#define ca_len ca_size
	int	ca_size;
	int	ca_ipl;
	int	ca_vec;
};
#else
struct confargs {
	int	ca_bustype;
	void	*ca_vaddr;
	void	*ca_paddr;
	int	ca_offset;
	int	ca_len;
	int	ca_ipl;
	int	ca_vec;
	char	*ca_name;
	void	*ca_master;	/* points to bus-dependent data */
};
#endif

#define BUS_MAIN	0
#define BUS_MC		1
#define BUS_PCC		2
#define BUS_PCCTWO	3
#define BUS_VMES	4
#define BUS_VMEL	5

#if 0
/* From mvme68k autoconf.h */
#define BUS_MAIN	1
#define BUS_PCC		2	/* VME147 PCC chip */
#define BUS_MC		3	/* VME162 MC chip */
#define BUS_PCCTWO	4	/* VME166/167/177 PCC2 chip */
#define BUS_VMES	5	/* 16 bit VME access */
#define BUS_VMEL	6	/* 32 bit VME access */
#define BUS_IP		7	/* VME162 IP module bus */

#endif

int always_match __P((struct device *, struct cfdata *, void *));

#define DEVICE_UNIT(device) (device->dv_unit)
#define CFDATA_LOC(cfdata) (cfdata->cf_loc)

/* the following are from the prom/bootblocks */
void	*bootaddr;	/* PA of boot device */
int	bootctrllun;	/* ctrl_lun of boot device */
int	bootdevlun;	/* dev_lun of boot device */
int	bootpart;	/* boot partition (disk) */

struct	device *bootdv; /* boot device */

/* PARTITIONSHIFT from disklabel.h */
#define PARTITIONMASK   ((1 << PARTITIONSHIFT) - 1) 

void	*mapiodev __P((void *pa, int size));
void	unmapiodev __P((void *kva, int size));

#endif
