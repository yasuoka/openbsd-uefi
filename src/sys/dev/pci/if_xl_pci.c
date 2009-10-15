/*	$OpenBSD: if_xl_pci.c,v 1.26 2009/10/15 17:54:56 deraadt Exp $	*/

/*
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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
 * $FreeBSD: if_xl.c,v 1.72 2000/01/09 21:12:59 wpaul Exp $
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
#include <sys/proc.h>   /* only for declaration of wakeup() used by vm.h */
#include <sys/device.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/if_media.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#endif

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

/*
 * The following #define causes the code to use PIO to access the
 * chip's registers instead of memory mapped mode. The reason PIO mode
 * is on by default is that the Etherlink XL manual seems to indicate
 * that only the newer revision chips (3c905B) support both PIO and
 * memory mapped access. Since we want to be compatible with the older
 * bus master chips, we use PIO here. If you comment this out, the
 * driver will use memory mapped I/O, which may be faster but which
 * might not work on some devices.
 */
#define XL_USEIOSPACE

#define XL_PCI_FUNCMEM		0x0018
#define XL_PCI_INTR		0x0004
#define XL_PCI_INTRACK		0x8000

#include <dev/ic/xlreg.h>

int xl_pci_match(struct device *, void *, void *);
void xl_pci_attach(struct device *, struct device *, void *);
int xl_pci_detach(struct device *, int);
void xl_pci_intr_ack(struct xl_softc *);

struct xl_pci_softc {
	struct xl_softc		psc_softc;
	pci_chipset_tag_t	psc_pc;
	bus_size_t		psc_iosize;
};

struct cfattach xl_pci_ca = {
	sizeof(struct xl_pci_softc), xl_pci_match, xl_pci_attach, xl_pci_detach
};

const struct pci_matchid xl_pci_devices[] = {
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3CSOHO100TX },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C900TPO },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C900COMBO },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C900B },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C900BCOMBO },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C900BTPC },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C900BFL },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C905TX },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C905T4 },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C905BTX },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C905BT4 },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C905BCOMBO },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C905BFX },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C980TX },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C980CTX },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C905CTX },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C450 },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C555 },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C556 },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C556B },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C9201 },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C920BEMBW },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3C575 },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3CCFE575BT },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3CCFE575CT },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3CCFEM656 },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3CCFEM656B },
	{ PCI_VENDOR_3COM, PCI_PRODUCT_3COM_3CCFEM656C },
};

int
xl_pci_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, xl_pci_devices,
	    sizeof(xl_pci_devices)/sizeof(xl_pci_devices[0])));
}

void
xl_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct xl_pci_softc *psc = (void *)self;
	struct xl_softc *sc = &psc->psc_softc;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pci_intr_handle_t ih;
	const char *intrstr = NULL;
	bus_size_t iosize, funsize;
	u_int32_t command;

	psc->psc_pc = pc;
	sc->sc_dmat = pa->pa_dmat;

	sc->xl_flags = 0;

	/* set required flags */
	switch (PCI_PRODUCT(pa->pa_id)) {
	case TC_DEVICEID_HURRICANE_555:
		sc->xl_flags |= XL_FLAG_EEPROM_OFFSET_30 | XL_FLAG_8BITROM;
		break;
	case TC_DEVICEID_HURRICANE_556:
		sc->xl_flags |= XL_FLAG_FUNCREG | XL_FLAG_PHYOK |
		    XL_FLAG_EEPROM_OFFSET_30 | XL_FLAG_WEIRDRESET;
		sc->xl_flags |= XL_FLAG_INVERT_LED_PWR|XL_FLAG_INVERT_MII_PWR;
		sc->xl_flags |= XL_FLAG_8BITROM;
		break;
	case TC_DEVICEID_HURRICANE_556B:
		sc->xl_flags |= XL_FLAG_FUNCREG | XL_FLAG_PHYOK |
		    XL_FLAG_EEPROM_OFFSET_30 | XL_FLAG_WEIRDRESET;
		sc->xl_flags |= XL_FLAG_INVERT_LED_PWR|XL_FLAG_INVERT_MII_PWR;
		break;
	case PCI_PRODUCT_3COM_3C9201:
	case PCI_PRODUCT_3COM_3C920BEMBW:
		sc->xl_flags |= XL_FLAG_PHYOK;
		break;
	case TC_DEVICEID_BOOMERANG_10_100BT:
		sc->xl_flags |= XL_FLAG_NO_MMIO;
		break;
	case PCI_PRODUCT_3COM_3C575:
		sc->xl_flags |= XL_FLAG_PHYOK | XL_FLAG_EEPROM_OFFSET_30 |
		   XL_FLAG_8BITROM;
		break;
	case PCI_PRODUCT_3COM_3CCFE575BT:
		sc->xl_flags = XL_FLAG_PHYOK | XL_FLAG_EEPROM_OFFSET_30 |
		    XL_FLAG_8BITROM | XL_FLAG_INVERT_LED_PWR;
		break;
	case PCI_PRODUCT_3COM_3CCFE575CT:
		sc->xl_flags = XL_FLAG_PHYOK | XL_FLAG_EEPROM_OFFSET_30 |
		    XL_FLAG_8BITROM | XL_FLAG_INVERT_MII_PWR;
		break;
	case PCI_PRODUCT_3COM_3CCFEM656:
		sc->xl_flags = XL_FLAG_PHYOK | XL_FLAG_EEPROM_OFFSET_30 |
		    XL_FLAG_8BITROM | XL_FLAG_INVERT_LED_PWR |
		    XL_FLAG_INVERT_MII_PWR;
		break;
	case PCI_PRODUCT_3COM_3CCFEM656B:
		sc->xl_flags = XL_FLAG_PHYOK | XL_FLAG_EEPROM_OFFSET_30 |
		    XL_FLAG_8BITROM | XL_FLAG_INVERT_LED_PWR |
		    XL_FLAG_INVERT_MII_PWR;
		break;
	case PCI_PRODUCT_3COM_3CCFEM656C:
		sc->xl_flags = XL_FLAG_PHYOK | XL_FLAG_EEPROM_OFFSET_30 |
		    XL_FLAG_8BITROM | XL_FLAG_INVERT_MII_PWR;
		break;
	default:
		break;
	}

	/*
	 * If this is a 3c905B, we have to check one extra thing.
	 * The 905B supports power management and may be placed in
	 * a low-power mode (D3 mode), typically by certain operating
	 * systems which shall not be named. The PCI BIOS is supposed
	 * to reset the NIC and bring it out of low-power mode, but  
	 * some do not. Consequently, we have to see if this chip    
	 * supports power management, and if so, make sure it's not  
	 * in low-power mode. If power management is available, the  
	 * capid byte will be 0x01.
	 * 
	 * I _think_ that what actually happens is that the chip
	 * loses its PCI configuration during the transition from
	 * D3 back to D0; this means that it should be possible for
	 * us to save the PCI iobase, membase and IRQ, put the chip
	 * back in the D0 state, then restore the PCI config ourselves.
	 */
	command = pci_conf_read(pc, pa->pa_tag, XL_PCI_CAPID) & 0xff;
	if (command == 0x01) {

		command = pci_conf_read(pc, pa->pa_tag,
		    XL_PCI_PWRMGMTCTRL);
		if (command & XL_PSTATE_MASK) {
			u_int32_t io, mem, irq;

			/* Save PCI config */
			io = pci_conf_read(pc, pa->pa_tag, XL_PCI_LOIO);
			mem = pci_conf_read(pc, pa->pa_tag, XL_PCI_LOMEM);
			irq = pci_conf_read(pc, pa->pa_tag, XL_PCI_INTLINE);

			/* Reset the power state. */
			printf("%s: chip is in D%d power mode "
			    "-- setting to D0\n",
			    sc->sc_dev.dv_xname, command & XL_PSTATE_MASK);
			command &= 0xFFFFFFFC;
			pci_conf_write(pc, pa->pa_tag,
			    XL_PCI_PWRMGMTCTRL, command);

			pci_conf_write(pc, pa->pa_tag, XL_PCI_LOIO, io);
			pci_conf_write(pc, pa->pa_tag, XL_PCI_LOMEM, mem);
			pci_conf_write(pc, pa->pa_tag, XL_PCI_INTLINE, irq);
		}
	}

	/*
	 * Map control/status registers.
	 */
#ifdef XL_USEIOSPACE
	if (pci_mapreg_map(pa, XL_PCI_LOIO, PCI_MAPREG_TYPE_IO, 0,
	    &sc->xl_btag, &sc->xl_bhandle, NULL, &iosize, 0)) {
		printf(": can't map i/o space\n");
		return;
	}
#else
	if (pci_mapreg_map(pa, XL_PCI_LOMEM, PCI_MAPREG_TYPE_MEM, 0,
	    &sc->xl_btag, &sc->xl_bhandle, NULL, &iosize, 0)) {
		printf(": can't map i/o space\n");
		return;
	}
#endif

	if (sc->xl_flags & XL_FLAG_FUNCREG) {
		if (pci_mapreg_map(pa, XL_PCI_FUNCMEM, PCI_MAPREG_TYPE_MEM, 0,
		    &sc->xl_funct, &sc->xl_funch, NULL, &funsize, 0)) {
			printf(": can't map i/o space\n");
			bus_space_unmap(sc->xl_btag, sc->xl_bhandle, iosize);
			return;
		}
		sc->intr_ack = xl_pci_intr_ack;
	}

	/*
	 * Allocate our interrupt.
	 */
	if (pci_intr_map(pa, &ih)) {
		printf(": couldn't map interrupt\n");
		bus_space_unmap(sc->xl_btag, sc->xl_bhandle, iosize);
		if (sc->xl_flags & XL_FLAG_FUNCREG)
			bus_space_unmap(sc->xl_funct, sc->xl_funch, funsize);
		return;
	}

	intrstr = pci_intr_string(pc, ih);
	sc->xl_intrhand = pci_intr_establish(pc, ih, IPL_NET, xl_intr, sc,
	    self->dv_xname);
	if (sc->xl_intrhand == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		bus_space_unmap(sc->xl_btag, sc->xl_bhandle, iosize);
		if (sc->xl_flags & XL_FLAG_FUNCREG)
			bus_space_unmap(sc->xl_funct, sc->xl_funch, funsize);
		return;
	}
	psc->psc_iosize = iosize;
	printf(": %s", intrstr);

	xl_attach(sc);
}

int
xl_pci_detach(struct device *self, int flags)
{
	struct xl_pci_softc *psc = (void *)self;
	struct xl_softc *sc = &psc->psc_softc;

	pci_intr_disestablish(psc->psc_pc, sc->xl_intrhand);
	xl_detach(sc);
	bus_space_unmap(sc->xl_btag, sc->xl_bhandle, psc->psc_iosize);
	return (0);
}

void            
xl_pci_intr_ack(struct xl_softc *sc)
{
	bus_space_write_4(sc->xl_funct, sc->xl_funch, XL_PCI_INTR,
	    XL_PCI_INTRACK);
}
