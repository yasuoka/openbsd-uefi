/*	$OpenBSD: if_dc_pci.c,v 1.25 2002/02/17 05:27:39 nate Exp $	*/

/*
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ee.columbia.edu>.  All rights reserved.
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
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/pci/if_dc.c,v 1.5 2000/01/12 22:24:05 wpaul Exp $
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#endif

#include <net/if_media.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <uvm/uvm_extern.h>              /* for vtophys */

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#define DC_USEIOSPACE

#include <dev/ic/dcreg.h>

/*
 * Various supported device vendors/types and their names.
 */
struct dc_type dc_devs[] = {
	{ PCI_VENDOR_DEC, PCI_PRODUCT_DEC_21140 },
	{ PCI_VENDOR_DEC, PCI_PRODUCT_DEC_21142 },
	{ PCI_VENDOR_DAVICOM, PCI_PRODUCT_DAVICOM_DM9100 },
	{ PCI_VENDOR_DAVICOM, PCI_PRODUCT_DAVICOM_DM9102 },
	{ PCI_VENDOR_ADMTEK, PCI_PRODUCT_ADMTEK_AL981 },
	{ PCI_VENDOR_ADMTEK, PCI_PRODUCT_ADMTEK_AN983 },
	{ PCI_VENDOR_ASIX, PCI_PRODUCT_ASIX_AX88140A },
	{ PCI_VENDOR_MACRONIX, PCI_PRODUCT_MACRONIX_MX98713 },
	{ PCI_VENDOR_MACRONIX, PCI_PRODUCT_MACRONIX_MX98715 },
	{ PCI_VENDOR_MACRONIX, PCI_PRODUCT_MACRONIX_MX98727 },
	{ PCI_VENDOR_COMPEX, PCI_PRODUCT_COMPEX_98713 },
	{ PCI_VENDOR_LITEON, PCI_PRODUCT_LITEON_PNIC },
	{ PCI_VENDOR_LITEON, PCI_PRODUCT_LITEON_PNICII },
	{ PCI_VENDOR_ACCTON, PCI_PRODUCT_ACCTON_EN1217 },
	{ PCI_VENDOR_ACCTON, PCI_PRODUCT_ACCTON_EN2242 },
	{ 0, 0 }
};

int dc_pci_match		__P((struct device *, void *, void *));
void dc_pci_attach		__P((struct device *, struct device *, void *));
void dc_pci_acpi		__P((struct device *, void *));

extern void dc_eeprom_width	__P((struct dc_softc *));
extern void dc_read_srom	__P((struct dc_softc *, int));
extern void dc_parse_21143_srom	__P((struct dc_softc *));

/*
 * Probe for a 21143 or clone chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
int
dc_pci_match(parent, match, aux)
	struct device *parent;
	void *match, *aux;
{
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	struct dc_type *t;

        if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_DEC &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_DEC_21142 &&
	    PCI_REVISION(pa->pa_class) == 0x21)
		return (1);

	for (t = dc_devs; t->dc_vid != 0; t++) {
		if ((PCI_VENDOR(pa->pa_id) == t->dc_vid) &&
		    (PCI_PRODUCT(pa->pa_id) == t->dc_did)) {
			return (2);
		}
	}

	return (0);
}

void dc_pci_acpi(self, aux)
	struct device *self;
	void *aux;
{
	struct dc_softc		*sc = (struct dc_softc *)self;
	struct pci_attach_args	*pa = (struct pci_attach_args *)aux;
	pci_chipset_tag_t	pc = pa->pa_pc;
	u_int32_t		r, cptr;
	int			unit;

	unit = sc->dc_unit;

	/* Find the location of the capabilities block */
	cptr = pci_conf_read(pc, pa->pa_tag, DC_PCI_CCAP) & 0xFF;

	r = pci_conf_read(pc, pa->pa_tag, cptr) & 0xFF;
	if (r == 0x01) {

		r = pci_conf_read(pc, pa->pa_tag, cptr + 4);
		if (r & DC_PSTATE_D3) {
			u_int32_t		iobase, membase, irq;

			/* Save important PCI config data. */
			iobase = pci_conf_read(pc, pa->pa_tag, DC_PCI_CFBIO);
			membase = pci_conf_read(pc, pa->pa_tag, DC_PCI_CFBMA);
			irq = pci_conf_read(pc, pa->pa_tag, DC_PCI_CFIT);

			/* Reset the power state. */
			printf("dc%d: chip is in D%d power mode "
			    "-- setting to D0\n", unit, r & DC_PSTATE_D3);
			r &= 0xFFFFFFFC;
			pci_conf_write(pc, pa->pa_tag, cptr + 4, r);

			/* Restore PCI config data. */
			pci_conf_write(pc, pa->pa_tag, DC_PCI_CFBIO, iobase);
			pci_conf_write(pc, pa->pa_tag, DC_PCI_CFBMA, membase);
			pci_conf_write(pc, pa->pa_tag, DC_PCI_CFIT, irq);
		}
	}
	return;
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
void dc_pci_attach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	int			s;
	const char		*intrstr = NULL;
	u_int32_t		command;
	struct dc_softc		*sc = (struct dc_softc *)self;
	struct pci_attach_args	*pa = aux;
	pci_chipset_tag_t	pc = pa->pa_pc;
	pci_intr_handle_t	ih;
	bus_addr_t		iobase;
	bus_size_t		iosize;
	u_int32_t		revision;
	int			found = 0;

	s = splimp();
	sc->sc_dmat = pa->pa_dmat;
	sc->dc_unit = sc->sc_dev.dv_unit;

	/*
	 * Handle power management nonsense.
	 */
	dc_pci_acpi(self, aux);

	/*
	 * Map control/status registers.
	 */
	command = pci_conf_read(pc, pa->pa_tag, PCI_COMMAND_STATUS_REG);
	command |= PCI_COMMAND_IO_ENABLE | PCI_COMMAND_MEM_ENABLE |
	    PCI_COMMAND_MASTER_ENABLE;
	pci_conf_write(pc, pa->pa_tag, PCI_COMMAND_STATUS_REG, command);
	command = pci_conf_read(pc, pa->pa_tag, PCI_COMMAND_STATUS_REG);

	sc->dc_csid = pci_conf_read(pc, pa->pa_tag, PCI_SUBSYS_ID_REG);

#ifdef DC_USEIOSPACE
	if (!(command & PCI_COMMAND_IO_ENABLE)) {
		printf(": failed to enable I/O ports\n");
		goto fail;
	}
	if (pci_io_find(pc, pa->pa_tag, DC_PCI_CFBIO, &iobase, &iosize)) {
		printf(": can't find I/O space\n");
		goto fail;
	}
	if (bus_space_map(pa->pa_iot, iobase, iosize, 0, &sc->dc_bhandle)) {
		printf(": can't map I/O space\n");
		goto fail;
	}
	sc->dc_btag = pa->pa_iot;
#else
	if (!(command & PCI_COMMAND_MEM_ENABLE)) {
		printf(": failed to enable memory mapping\n");
		goto fail;
	}
	if (pci_mem_find(pc, pa->pa_tag, DC_PCI_CFBMA, &iobase, &iosize, NULL)){
		printf(": can't find mem space\n");
		goto fail;
	}
	if (bus_space_map(pa->pa_memt, iobase, iosize, 0, &sc->dc_bhandle)) {
		printf(": can't map mem space\n");
		goto fail;
	}
	sc->dc_btag = pa->pa_memt;
#endif

	/* Allocate interrupt */
	if (pci_intr_map(pa, &ih)) {
		printf(": couldn't map interrupt\n");
		goto fail;
	}
	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_NET, dc_intr, sc,
	    self->dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto fail;
	}
	printf(": %s", intrstr);

	/* Need this info to decide on a chip type. */
	sc->dc_revision = revision = PCI_REVISION(pa->pa_class);

	switch (PCI_VENDOR(pa->pa_id)) {
	case PCI_VENDOR_DEC:
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_DEC_21140 ||
		    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_DEC_21142) {
			found = 1;
			sc->dc_type = DC_TYPE_21143;
			sc->dc_flags |= DC_TX_POLL|DC_TX_USE_TX_INTR;
			sc->dc_flags |= DC_REDUCED_MII_POLL;
			dc_eeprom_width(sc);
			dc_read_srom(sc, sc->dc_romwidth);
		}
		break;
	case PCI_VENDOR_DAVICOM:
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_DAVICOM_DM9100 ||
		    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_DAVICOM_DM9102) {
			found = 1;
			sc->dc_type = DC_TYPE_DM9102;
			sc->dc_flags |= DC_TX_COALESCE|DC_TX_INTR_ALWAYS;
			sc->dc_flags |= DC_REDUCED_MII_POLL|DC_TX_STORENFWD;
			sc->dc_pmode = DC_PMODE_MII;

			/* Increase the latency timer value. */
			command = pci_conf_read(pc, pa->pa_tag, DC_PCI_CFLT);
			command &= 0xFFFF00FF;
			command |= 0x00008000;
			pci_conf_write(pc, pa->pa_tag, DC_PCI_CFLT, command);
		}
		break;
	case PCI_VENDOR_ADMTEK:
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ADMTEK_AL981) {
			found = 1;
			sc->dc_type = DC_TYPE_AL981;
			sc->dc_flags |= DC_TX_USE_TX_INTR;
			sc->dc_flags |= DC_TX_ADMTEK_WAR;
			sc->dc_pmode = DC_PMODE_MII;
		}
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ADMTEK_AN983) {
			found = 1;
			sc->dc_type = DC_TYPE_AN983;
			sc->dc_flags |= DC_TX_USE_TX_INTR;
			sc->dc_flags |= DC_TX_ADMTEK_WAR;
			sc->dc_pmode = DC_PMODE_MII;
		}
		dc_eeprom_width(sc);
		dc_read_srom(sc, sc->dc_romwidth);
		break;
	case PCI_VENDOR_MACRONIX:
	case PCI_VENDOR_ACCTON:
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ACCTON_EN2242) {
			found = 1;
			sc->dc_type = DC_TYPE_AN983;
			sc->dc_flags |= DC_TX_USE_TX_INTR;
			sc->dc_flags |= DC_TX_ADMTEK_WAR;
			sc->dc_pmode = DC_PMODE_MII;

			dc_eeprom_width(sc);
			dc_read_srom(sc, sc->dc_romwidth);
		}
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_MACRONIX_MX98713) {
			found = 1;
			if (revision < DC_REVISION_98713A) {
				sc->dc_type = DC_TYPE_98713;
			}
			if (revision >= DC_REVISION_98713A) {
				sc->dc_type = DC_TYPE_98713A;
				sc->dc_flags |= DC_21143_NWAY;
			}
			sc->dc_flags |= DC_REDUCED_MII_POLL;
			sc->dc_flags |= DC_TX_POLL|DC_TX_USE_TX_INTR;
		}
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_MACRONIX_MX98715 ||
		    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ACCTON_EN1217) {
			found = 1;
			if (revision >= DC_REVISION_98715AEC_C &&
			    revision < DC_REVISION_98725)
				sc->dc_flags |= DC_128BIT_HASH;
			sc->dc_type = DC_TYPE_987x5;
			sc->dc_flags |= DC_TX_POLL|DC_TX_USE_TX_INTR;
			sc->dc_flags |= DC_REDUCED_MII_POLL|DC_21143_NWAY;
		}
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_MACRONIX_MX98727) {
			found = 1;
			sc->dc_type = DC_TYPE_987x5;
			sc->dc_flags |= DC_TX_POLL|DC_TX_USE_TX_INTR;
			sc->dc_flags |= DC_REDUCED_MII_POLL|DC_21143_NWAY;
		}
		break;
	case PCI_VENDOR_COMPEX:
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_COMPEX_98713) {
			found = 1;
			if (revision < DC_REVISION_98713A) {
				sc->dc_type = DC_TYPE_98713;
				sc->dc_flags |= DC_REDUCED_MII_POLL;
			}
			if (revision >= DC_REVISION_98713A)
				sc->dc_type = DC_TYPE_98713A;
			sc->dc_flags |= DC_TX_POLL|DC_TX_USE_TX_INTR;
		}
		break;
	case PCI_VENDOR_LITEON:
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_LITEON_PNICII) {
			found = 1;
			sc->dc_type = DC_TYPE_PNICII;
			sc->dc_flags |= DC_TX_POLL|DC_TX_USE_TX_INTR;
			sc->dc_flags |= DC_REDUCED_MII_POLL|DC_21143_NWAY;
			sc->dc_flags |= DC_128BIT_HASH;
		}
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_LITEON_PNIC) {
			found = 1;
			sc->dc_type = DC_TYPE_PNIC;
			sc->dc_flags |= DC_TX_STORENFWD|DC_TX_INTR_ALWAYS;
			sc->dc_flags |= DC_PNIC_RX_BUG_WAR;
			sc->dc_pnic_rx_buf = malloc(DC_RXLEN * 5, M_DEVBUF,
			    M_NOWAIT);
			if (sc->dc_pnic_rx_buf == NULL)
				panic("dc_pci_attach");
			if (revision < DC_REVISION_82C169)
				sc->dc_pmode = DC_PMODE_SYM;
		}
		break;
	case PCI_VENDOR_ASIX:
		if (PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ASIX_AX88140A) {
			found = 1;
			sc->dc_type = DC_TYPE_ASIX;
			sc->dc_flags |= DC_TX_USE_TX_INTR|DC_TX_INTR_FIRSTFRAG;
			sc->dc_flags |= DC_REDUCED_MII_POLL;
			sc->dc_pmode = DC_PMODE_MII;
		}
		break;
	}
	if (found == 0) {
		/* This shouldn't happen if probe has done it's job... */
		printf(": unknown device: %x:%x\n",
		    PCI_VENDOR(pa->pa_id), PCI_PRODUCT(pa->pa_id));
		goto fail;
	}

	/* Save the cache line size. */
	if (DC_IS_DAVICOM(sc))
		sc->dc_cachesize = 0;
	else
		sc->dc_cachesize = pci_conf_read(pc, pa->pa_tag,
		    DC_PCI_CFLT) & 0xFF;

	/* Reset the adapter. */
	dc_reset(sc);

	/* Take 21143 out of snooze mode */
	if (DC_IS_INTEL(sc)) {
		command = pci_conf_read(pc, pa->pa_tag, DC_PCI_CFDD);
		command &= ~(DC_CFDD_SNOOZE_MODE|DC_CFDD_SLEEP_MODE);
		pci_conf_write(pc, pa->pa_tag, DC_PCI_CFDD, command);
	}

	/*
	 * If we discover later (in dc_attach) that we have an
	 * MII with no PHY, we need to have the 21143 drive the LEDs.
	 * Except there are some systems like the NEC VersaPro NoteBook PC
	 * which have no LEDs, and twiddling these bits has adverse effects
	 * on them. (I.e. you suddenly can't get a link.)
	 *
	 * If mii_attach() returns an error, we leave the DC_TULIP_LEDS
	 * bit set, else we clear it. Since our dc(4) driver is split into
	 * bus-dependent and bus-independent parts, we must do set this bit
	 * here while we are able to do PCI configuration reads.
	 */
	if (DC_IS_INTEL(sc)) {
		if (pci_conf_read(pc, pa->pa_tag, DC_PCI_CSID) != 0x80281033)
			sc->dc_flags |= DC_TULIP_LEDS;
	}

	/*
	 * Try to learn something about the supported media.
	 * We know that ASIX and ADMtek and Davicom devices
	 * will *always* be using MII media, so that's a no-brainer.
	 * The tricky ones are the Macronix/PNIC II and the
	 * Intel 21143.
	 */
	if (DC_IS_INTEL(sc))
		dc_parse_21143_srom(sc);
	else if (DC_IS_MACRONIX(sc) || DC_IS_PNICII(sc)) {
		if (sc->dc_type == DC_TYPE_98713)
			sc->dc_pmode = DC_PMODE_MII;
		else
			sc->dc_pmode = DC_PMODE_SYM;
	} else if (!sc->dc_pmode)
		sc->dc_pmode = DC_PMODE_MII;

#ifdef __sparc64__
	{
		extern void myetheraddr __P((u_char *));
		myetheraddr(sc->arpcom.ac_enaddr);
		sc->sc_hasmac = 1;
	}
#endif

#ifdef SRM_MEDIA
	sc->dc_srm_media = 0;

	/* Remember the SRM console media setting */
	if (DC_IS_INTEL(sc)) {
		command = pci_conf_read(pc, pa->pa_tag, DC_PCI_CFDD);
		command &= ~(DC_CFDD_SNOOZE_MODE|DC_CFDD_SLEEP_MODE);
		switch ((command >> 8) & 0xff) {
		case 3: 
			sc->dc_srm_media = IFM_10_T;
			break;
		case 4: 
			sc->dc_srm_media = IFM_10_T | IFM_FDX;
			break;
		case 5: 
			sc->dc_srm_media = IFM_100_TX;
			break;
		case 6: 
			sc->dc_srm_media = IFM_100_TX | IFM_FDX;
			break;
		}
		if (sc->dc_srm_media)
			sc->dc_srm_media |= IFM_ACTIVE | IFM_ETHER;
	}
#endif
	dc_eeprom_width(sc);
	dc_attach(sc);

fail:
	splx(s);
}

struct cfattach dc_pci_ca = {
	sizeof(struct dc_softc), dc_pci_match, dc_pci_attach
};
