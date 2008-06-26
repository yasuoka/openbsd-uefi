/*	$OpenBSD: i82093var.h,v 1.6 2008/06/26 05:42:10 ray Exp $	*/
/* $NetBSD: i82093var.h,v 1.1 2003/02/26 21:26:10 fvdl Exp $ */

/*-
 * Copyright (c) 2000 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by RedBack Networks Inc.
 *
 * Author: Bill Sommerfeld
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _I386_I82093VAR_H_
#define _I386_I82093VAR_H_

#include <machine/apicvar.h>

struct ioapic_pin 
{
	struct intrhand		*ip_handler; 
	struct ioapic_pin	*ip_next; /* next pin on this vector */
	struct mp_intr_map 	*ip_map;
	int			ip_vector; /* IDT vector */
	int			ip_type;
	int			ip_minlevel;
	int			ip_maxlevel;
};

struct ioapic_softc {
	struct 			device sc_dev;	/* generic device glue */
	struct ioapic_softc	*sc_next;
	int			sc_apicid;
	int			sc_apic_vers;
	int			sc_apic_vecbase; /* global int base if ACPI */
	int			sc_apic_sz;	/* apic size*/
	int			sc_flags;
	paddr_t			sc_pa;		/* PA of ioapic */
	volatile u_int32_t	*sc_reg;	/* KVA of ioapic addr */
	volatile u_int32_t	*sc_data;	/* KVA of ioapic data */
	struct ioapic_pin	*sc_pins;	/* sc_apic_sz entries */
};      

/*
 * MP: intr_handle_t is bitfielded.
 * ih&0xff -> line number.
 * ih&0x10000000 -> if 0, old-style isa irq; if 1, routed via ioapic.
 * (ih&0xff0000)>>16 -> ioapic id.
 * (ih&0x00ff00)>>8 -> ioapic line.
 */

#define APIC_INT_VIA_APIC	0x10000000
#define APIC_INT_APIC_MASK	0x00ff0000
#define APIC_INT_APIC_SHIFT	16
#define APIC_INT_PIN_MASK	0x0000ff00
#define APIC_INT_PIN_SHIFT	8
#define APIC_INT_LINE_MASK	0x000000ff

#define APIC_IRQ_APIC(x) ((x & APIC_INT_APIC_MASK) >> APIC_INT_APIC_SHIFT)
#define APIC_IRQ_PIN(x) ((x & APIC_INT_PIN_MASK) >> APIC_INT_PIN_SHIFT)

/* I/O APIC ID remapping helper macros. */
#define IOAPIC_REMAP_MASK	(IOAPIC_ID_MASK >> IOAPIC_ID_SHIFT)
#define IOAPIC_REMAP_FLAG	((IOAPIC_REMAP_MASK + 1) << 1)
#define IOAPIC_REMAP(old_id, new_id) \
    (ioapic_id_remap[(old_id)] = IOAPIC_REMAP_FLAG | (new_id))
#define IOAPIC_REMAPPED(id)	(ioapic_id_remap[(id)] & IOAPIC_REMAP_FLAG)
#define IOAPIC_REMAPPED_ID(id)	\
    (IOAPIC_REMAPPED(id) ? ioapic_id_remap[(id)] & IOAPIC_REMAP_MASK : (id))

void   *apic_intr_establish(int, int, int, int (*)(void *), void *, char *); 
void	apic_intr_disestablish(void *);

void	ioapic_print_redir(struct ioapic_softc *, char *, int);
void	ioapic_format_redir(char *, char *, int, u_int32_t, u_int32_t);
struct ioapic_softc *ioapic_find(int);
struct ioapic_softc *ioapic_find_bybase(int);

void	ioapic_enable(void);
void	lapic_vectorset(void); /* XXX */

extern int ioapic_bsp_id;
extern int nioapics;
extern struct ioapic_softc *ioapics;
extern u_int16_t ioapic_id_map;
extern u_int8_t ioapic_id_remap[];

#endif /* !_I386_I82093VAR_H_ */
