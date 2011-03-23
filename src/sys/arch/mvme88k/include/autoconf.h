/*	$OpenBSD: autoconf.h,v 1.18 2011/03/23 16:54:36 pirofti Exp $ */
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

#ifndef _MACHINE_AUTOCONF_H_
#define _MACHINE_AUTOCONF_H_

#include <machine/bus.h>

struct confargs {
	bus_space_tag_t	ca_iot;
	bus_dma_tag_t	ca_dmat;	
	int		ca_bustype;	/* bus type */
	paddr_t		ca_paddr;	/* physical address */
	int		ca_offset;	/* offset from parent */
	int		ca_ipl;		/* interrupt level */
	int		ca_vec;		/* mandatory interrupt vector */
	const char	*ca_name;	/* device name */
};

#define BUS_MAIN      0
#define BUS_PCCTWO    3
#define BUS_VMES      4
#define BUS_VMEL      5
#define BUS_SYSCON    6
#define BUS_BUSSWITCH 7

/* the following are from the prom/bootblocks */
extern paddr_t	bootaddr;	/* PA of boot device */
extern int	bootpart;	/* boot partition (disk) */
extern int	bootbus;	/* scsi bus (disk) */

vaddr_t	mapiodev(paddr_t pa, int size);
void	unmapiodev(vaddr_t kva, int size);

#endif
