/*	$OpenBSD: ahc_eisa.c,v 1.9 2000/03/22 02:55:40 smurph Exp $	*/
/*	$NetBSD: ahc_eisa.c,v 1.10 1996/10/21 22:30:58 thorpej Exp $	*/

/*
 * Product specific probe and attach routines for:
 * 	27/284X and aic7770 motherboard SCSI controllers
 *
 * Copyright (c) 1994, 1995, 1996 Justin T. Gibbs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	$Id: ahc_eisa.c,v 1.9 2000/03/22 02:55:40 smurph Exp $
 */

#include "eisa.h"
#if NEISA > 0

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <machine/bus.h>
#include <machine/intr.h>

#include <scsi/scsi_all.h>
#include <scsi/scsiconf.h>

#include <dev/eisa/eisareg.h>
#include <dev/eisa/eisavar.h>
#include <dev/eisa/eisadevs.h>
#include <dev/ic/aic7xxxreg.h>
#include <dev/ic/aic7xxxvar.h>

#define AHC_EISA_SLOT_OFFSET	0xc00
#define AHC_EISA_IOSIZE		0x100
#define INTDEF			0x5cul	/* Interrupt Definition Register */

/*
 * Under normal circumstances, these messages are unnecessary
 * and not terribly cosmetic.
 */
#ifdef DEBUG
#define bootverbose	1
#else
#define bootverbose	1
#endif

int   ahc_eisa_irq __P((bus_space_tag_t, bus_space_handle_t));
int   ahc_eisa_match __P((struct device *, void *, void *));
void  ahc_eisa_attach __P((struct device *, struct device *, void *));


struct cfattach ahc_eisa_ca = {
	sizeof(struct ahc_softc), ahc_eisa_match, ahc_eisa_attach
};

/*
 * Return irq setting of the board, otherwise -1.
 */
int
ahc_eisa_irq(iot, ioh)
bus_space_tag_t iot;
bus_space_handle_t ioh;
{
	int irq;
	u_char intdef;
	u_char hcntrl;
	
	/* Pause the card preseving the IRQ type */
	hcntrl = bus_space_read_1(iot, ioh, HCNTRL) & IRQMS;
	bus_space_write_1(iot, ioh, HCNTRL, hcntrl | PAUSE);
	
	intdef = bus_space_read_1(iot, ioh, INTDEF);
	switch (irq = (intdef & 0xf)) {
	case 9:
	case 10:
	case 11:
	case 12:
	case 14:
	case 15:
		break;
	default:
		printf("ahc_eisa_irq: illegal irq setting %d\n", intdef);
		return -1;
	}

	/* Note that we are going and return (to probe) */
	return irq;
}

/*
 * Check the slots looking for a board we recognise
 * If we find one, note it's address (slot) and call
 * the actual probe routine to check it out.
 */
int
ahc_eisa_match(parent, match, aux)
struct device *parent;
void *match, *aux;
{
	struct eisa_attach_args *ea = aux;
	bus_space_tag_t iot = ea->ea_iot;
	bus_space_handle_t ioh;
	int irq;

	/* must match one of our known ID strings */
	if (strcmp(ea->ea_idstring, "ADP7770") &&
		 strcmp(ea->ea_idstring, "ADP7771")
#if 0
		 && strcmp(ea->ea_idstring, "ADP7756")	/* not EISA, but VL */
		 && strcmp(ea->ea_idstring, "ADP7757")	/* not EISA, but VL */
#endif
		)
		return (0);

	if (bus_space_map(iot, EISA_SLOT_ADDR(ea->ea_slot) +
			  AHC_EISA_SLOT_OFFSET, AHC_EISA_IOSIZE, 0, &ioh))
		return (0);

	irq = ahc_eisa_irq(iot, ioh);

	bus_space_unmap(iot, ioh, AHC_EISA_IOSIZE);

	return (irq >= 0);
}

void
ahc_eisa_attach(parent, self, aux)
struct device *parent, *self;
void *aux;
{
	ahc_chip chip;
	u_char channel = 'A'; /* Only one channel */
	struct ahc_softc *ahc = (void *)self;
	struct eisa_attach_args *ea = aux;
	bus_space_tag_t iot = ea->ea_iot;
	bus_space_handle_t ioh;
	int irq;
	eisa_chipset_tag_t ec = ea->ea_ec;
	eisa_intr_handle_t ih;
	const char *model, *intrstr;
	u_int biosctrl;
	u_int scsiconf;
	u_int scsiconf1;
	
	ahc->sc_dmat = ea->ea_dmat;
	if (bus_space_map(iot, EISA_SLOT_ADDR(ea->ea_slot) +
			  AHC_EISA_SLOT_OFFSET, AHC_EISA_IOSIZE, 0, &ioh))
		panic("ahc_eisa_attach: could not map I/O addresses");
	if ((irq = ahc_eisa_irq(iot, ioh)) < 0)
		panic("ahc_eisa_attach: ahc_eisa_irq failed!");

	if (strcmp(ea->ea_idstring, "ADP7770") == 0) {
		model = EISA_PRODUCT_ADP7770;
		chip = AHC_AIC7770|AHC_EISA;
	} else if (strcmp(ea->ea_idstring, "ADP7771") == 0) {
		model = EISA_PRODUCT_ADP7771;
		chip = AHC_AIC7770|AHC_EISA;
	} else {
		panic("ahc_eisa_attach: Unknown device type %s",
				ea->ea_idstring);
	}
	printf(": %s\n", model);

	ahc_construct(ahc, iot, ioh, chip, AHC_FNONE, AHC_AIC7770_FE, channel);
	
	ahc->channel = 'A';
	ahc->channel_b = 'B';
	if (ahc_reset(ahc) != 0)
		return;
	
	if (eisa_intr_map(ec, irq, &ih)) {
		printf("%s: couldn't map interrupt (%d)\n",
		       ahc->sc_dev.dv_xname, irq);
		return;
	}

	/*
	 * Tell the user what type of interrupts we're using.
	 * usefull for debugging irq problems
	 */
	if (bootverbose) {
		printf("%s: Using %s Interrupts\n",
		       ahc_name(ahc),
		       ahc->pause & IRQMS ?
		       "Level Sensitive" : "Edge Triggered");
	}

	/*
	 * Now that we know we own the resources we need, do the 
	 * card initialization.
	 *
	 * First, the aic7770 card specific setup.
	 */
	biosctrl = ahc_inb(ahc, HA_274_BIOSCTRL);
	scsiconf = ahc_inb(ahc, SCSICONF);
	scsiconf1 = ahc_inb(ahc, SCSICONF + 1);
	
	/* Get the primary channel information */
	if ((biosctrl & CHANNEL_B_PRIMARY) != 0)
		ahc->flags |= AHC_CHANNEL_B_PRIMARY;

	if ((biosctrl & BIOSMODE) == BIOSDISABLED) {
		ahc->flags |= AHC_USEDEFAULTS;
	} else if ((ahc->features & AHC_WIDE) != 0) {
		ahc->our_id = scsiconf1 & HWSCSIID;
		if (scsiconf & TERM_ENB)
			ahc->flags |= AHC_TERM_ENB_A;
	} else {
		ahc->our_id = scsiconf & HSCSIID;
		ahc->our_id_b = scsiconf1 & HSCSIID;
		if (scsiconf & TERM_ENB)
			ahc->flags |= AHC_TERM_ENB_A;
		if (scsiconf1 & TERM_ENB)
			ahc->flags |= AHC_TERM_ENB_B;
	}
	/*
	 * We have no way to tell, so assume extended
	 * translation is enabled.
	 */
	
	ahc->flags |= AHC_EXTENDED_TRANS_A|AHC_EXTENDED_TRANS_B;
	
	/*      
	 * See if we have a Rev E or higher aic7770. Anything below a
	 * Rev E will have a R/O autoflush disable configuration bit.
	 * It's still not clear exactly what is differenent about the Rev E.
	 * We think it allows 8 bit entries in the QOUTFIFO to support
	 * "paging" SCBs so you can have more than 4 commands active at
	 * once.
	 */
	{
		char *id_string;
		u_char sblkctl;
		u_char sblkctl_orig;

		sblkctl_orig = AHC_INB(ahc, SBLKCTL);
		sblkctl = sblkctl_orig ^ AUTOFLUSHDIS;
		AHC_OUTB(ahc, SBLKCTL, sblkctl);
		sblkctl = AHC_INB(ahc, SBLKCTL);
		if (sblkctl != sblkctl_orig) {
			id_string = "aic7770 >= Rev E, ";
			/*
			 * Ensure autoflush is enabled
			 */
			sblkctl &= ~AUTOFLUSHDIS;
			AHC_OUTB(ahc, SBLKCTL, sblkctl);

			/* Allow paging on this adapter */
			ahc->flags |= AHC_PAGESCBS;
		} else
			id_string = "aic7770 <= Rev C, ";

		printf("%s: %s", ahc_name(ahc), id_string);
	}

	/* Setup the FIFO threshold and the bus off time */
	{
		u_char hostconf = AHC_INB(ahc, HOSTCONF);
		AHC_OUTB(ahc, BUSSPD, hostconf & DFTHRSH);
		AHC_OUTB(ahc, BUSTIME, (hostconf << 2) & BOFF);
	}

	/*
	 * Generic aic7xxx initialization.
	 */
	if (ahc_init(ahc)) {
		ahc_free(ahc);
		return;
	}
 
	/*
	 * Enable the board's BUS drivers
	 */
	AHC_OUTB(ahc, BCTL, ENABLE);

	intrstr = eisa_intr_string(ec, ih);
	/*
	 * The IRQMS bit enables level sensitive interrupts only allow
	 * IRQ sharing if its set.
	 */
	ahc->sc_ih = eisa_intr_establish(ec, ih, ahc->pause & IRQMS
					 ? IST_LEVEL : IST_EDGE,
					 IPL_BIO, ahc_intr, ahc,
					 ahc->sc_dev.dv_xname);
	if (ahc->sc_ih == NULL) {
		printf("%s: couldn't establish interrupt",
		       ahc->sc_dev.dv_xname);
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		ahc_free(ahc);
		return;
	}
	if (intrstr != NULL)
		printf("%s: interrupting at %s\n", ahc->sc_dev.dv_xname,
		       intrstr);

	/* Attach sub-devices - always succeeds */
	ahc_attach(ahc);

}
#endif /* NEISA > 0 */
