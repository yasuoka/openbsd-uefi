/* $OpenBSD: if_wi_pcmcia.c,v 1.44 2003/10/26 15:34:16 drahn Exp $ */
/* $NetBSD: if_wi_pcmcia.c,v 1.14 2001/11/26 04:34:56 ichiro Exp $ */

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
 *	From: if_wi.c,v 1.7 1999/07/04 14:40:22 wpaul Exp $
 */

/*
 * Lucent WaveLAN/IEEE 802.11 PCMCIA driver for OpenBSD.
 *
 * Originally written by Bill Paul <wpaul@ctr.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/timeout.h>
#include <sys/socket.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif

#include <net/if_ieee80211.h>

#include <machine/bus.h>

#include <dev/pcmcia/pcmciareg.h>
#include <dev/pcmcia/pcmciavar.h>
#include <dev/pcmcia/pcmciadevs.h>

#include <dev/ic/if_wireg.h>
#include <dev/ic/if_wi_ieee.h>
#include <dev/ic/if_wivar.h>

int	wi_pcmcia_match(struct device *, void *, void *);
void	wi_pcmcia_attach(struct device *, struct device *, void *);
int	wi_pcmcia_detach(struct device *, int);
int	wi_pcmcia_activate(struct device *, enum devact);

struct wi_pcmcia_softc {
	struct wi_softc sc_wi;

	struct pcmcia_io_handle	sc_pcioh;
	int			sc_io_window;
	struct pcmcia_function	*sc_pf;
};

struct cfattach wi_pcmcia_ca = {
	sizeof (struct wi_pcmcia_softc), wi_pcmcia_match, wi_pcmcia_attach,
	wi_pcmcia_detach, wi_pcmcia_activate
};

static const struct wi_pcmcia_product {
	u_int16_t	pp_vendor;
	u_int16_t	pp_product;
	const char	*pp_cisinfo[4];
	const char	*pp_name;
} wi_pcmcia_products[] = {
	{ PCMCIA_VENDOR_LUCENT,
	  PCMCIA_PRODUCT_LUCENT_WAVELAN_IEEE,
	  PCMCIA_CIS_LUCENT_WAVELAN_IEEE,
	  "WaveLAN/IEEE"
	},
	{ PCMCIA_VENDOR_3COM,
	  PCMCIA_PRODUCT_3COM_3CRWE737A,
	  PCMCIA_CIS_3COM_3CRWE737A,
	  "3Com AirConnect Wireless LAN"
	},
	{ PCMCIA_VENDOR_3COM,
	  PCMCIA_PRODUCT_3COM_3CRWE777A,
	  PCMCIA_CIS_3COM_3CRWE777A,
	  "3Com AirConnect Wireless LAN"
	},
	{ PCMCIA_VENDOR_COREGA,
	  PCMCIA_PRODUCT_COREGA_WIRELESS_LAN_PCC_11,
	  PCMCIA_CIS_COREGA_WIRELESS_LAN_PCC_11,
	  "Corega Wireless LAN PCC-11"
	},
	{ PCMCIA_VENDOR_COREGA,
	  PCMCIA_PRODUCT_COREGA_WIRELESS_LAN_PCCA_11,
	  PCMCIA_CIS_COREGA_WIRELESS_LAN_PCCA_11,
	  "Corega Wireless LAN PCCA-11",
	},
	{ PCMCIA_VENDOR_COREGA,
	  PCMCIA_PRODUCT_COREGA_WIRELESS_LAN_PCCB_11,
	  PCMCIA_CIS_COREGA_WIRELESS_LAN_PCCB_11,
	  "Corega Wireless LAN PCCB-11",
	},
	{ PCMCIA_VENDOR_COREGA,
	  PCMCIA_PRODUCT_COREGA_WIRELESS_LAN_PCCL_11,
	  PCMCIA_CIS_COREGA_WIRELESS_LAN_PCCL_11,
	  "Corega Wireless LAN PCCL-11",
	},
	{ PCMCIA_VENDOR_INTEL,
	  PCMCIA_PRODUCT_INTEL_PRO_WLAN_2011,
	  PCMCIA_CIS_INTEL_PRO_WLAN_2011,
	  "Intel PRO/Wireless 2011",
	},
	{ PCMCIA_VENDOR_INTERSIL,
	  PCMCIA_PRODUCT_INTERSIL_PRISM2,
	  PCMCIA_CIS_INTERSIL_PRISM2,
	  "Intersil Prism II",
	},
	{ PCMCIA_VENDOR_SAMSUNG,
	  PCMCIA_PRODUCT_SAMSUNG_SWL_2000N,
	  PCMCIA_CIS_SAMSUNG_SWL_2000N,
	  "Samsung MagicLAN SWL-2000N",
	},
	{ PCMCIA_VENDOR_LINKSYS2,
	  PCMCIA_PRODUCT_LINKSYS2_IWN,
	  PCMCIA_CIS_LINKSYS2_IWN,
	  "Linksys Instant Wireless Network",
	},
	{ PCMCIA_VENDOR_LINKSYS2,
	  PCMCIA_PRODUCT_LINKSYS2_IWN2,
	  PCMCIA_CIS_LINKSYS2_IWN2,
	  "Linksys Instant Wireless Network",
	},
	{ PCMCIA_VENDOR_LUCENT,
	  PCMCIA_PRODUCT_LUCENT_WAVELAN_IEEE,
	  PCMCIA_CIS_SMC_2632W,
	  "SMC 2632 EZ Connect Wireless PC Card",
	},
	{ PCMCIA_VENDOR_LUCENT,
	  PCMCIA_PRODUCT_LUCENT_WAVELAN_IEEE,
	  PCMCIA_CIS_NANOSPEED_PRISM2,
	  "NANOSPEED ROOT-RZ2000 WLAN Card",
	},
	{ PCMCIA_VENDOR_ELSA,
	  PCMCIA_PRODUCT_ELSA_XI300_IEEE,
	  PCMCIA_CIS_ELSA_XI300_IEEE,
	  "XI300 Wireless LAN",
	},
	{ PCMCIA_VENDOR_ELSA,
	  PCMCIA_PRODUCT_ELSA_XI325_IEEE,
	  PCMCIA_CIS_ELSA_XI325_IEEE,
	  "XI325 Wireless LAN",
	},
	{ PCMCIA_VENDOR_COMPAQ,
	  PCMCIA_PRODUCT_COMPAQ_NC5004,
	  PCMCIA_CIS_COMPAQ_NC5004,
	  "Compaq Agency NC5004 Wireless Card",
	},
	{ PCMCIA_VENDOR_CONTEC,
	  PCMCIA_PRODUCT_CONTEC_FX_DS110_PCC,
	  PCMCIA_CIS_CONTEC_FX_DS110_PCC,
	  "Contec FLEXLAN/FX-DS110-PCC",
	},
	{ PCMCIA_VENDOR_TDK,
	  PCMCIA_PRODUCT_TDK_LAK_CD011WL,
	  PCMCIA_CIS_TDK_LAK_CD011WL,
	  "TDK LAK-CD011WL",
	},
	{ PCMCIA_VENDOR_LUCENT,
	  PCMCIA_PRODUCT_LUCENT_WAVELAN_IEEE,
	  PCMCIA_CIS_NEC_CMZ_RT_WP,
	  "NEC Wireless Card CMZ-RT-WP",
	},
	{ PCMCIA_VENDOR_LUCENT,
	  PCMCIA_PRODUCT_LUCENT_WAVELAN_IEEE,
	  PCMCIA_CIS_NTT_ME_WLAN,
	  "NTT-ME 11Mbps Wireless LAN PC Card",
	},
	{ PCMCIA_VENDOR_ADDTRON,
	  PCMCIA_PRODUCT_ADDTRON_AWP100,
	  PCMCIA_CIS_ADDTRON_AWP100,
	  "Addtron AWP-100",
	},
	{ PCMCIA_VENDOR_LUCENT,
	  PCMCIA_PRODUCT_LUCENT_WAVELAN_IEEE,
	  PCMCIA_CIS_CABLETRON_ROAMABOUT,
	  "Cabletron RoamAbout",
	},
	{ PCMCIA_VENDOR_IODATA2,
	  PCMCIA_PRODUCT_IODATA2_WNB11PCM,
	  PCMCIA_CIS_IODATA2_WNB11PCM,
	  "I-O DATA WN-B11/PCM",
	},
	{ PCMCIA_VENDOR_GEMTEK,
	  PCMCIA_PRODUCT_GEMTEK_WLAN,
	  PCMCIA_CIS_GEMTEK_WLAN,
	  "GEMTEK Prism2_5 WaveLAN Card"
	},
	{ PCMCIA_VENDOR_ELSA,
	  PCMCIA_PRODUCT_ELSA_XI800_IEEE,
	  PCMCIA_CIS_ELSA_XI800_IEEE,
	  "ELSA XI800 CF Wireless LAN"
	},
	{ PCMCIA_VENDOR_BUFFALO,
	  PCMCIA_PRODUCT_BUFFALO_WLI_PCM_S11,
	  PCMCIA_CIS_BUFFALO_WLI_PCM_S11,
	  "BUFFALO AirStation 11Mbps WLAN"
	},
	{ PCMCIA_VENDOR_BUFFALO,
	  PCMCIA_PRODUCT_BUFFALO_WLI_CF_S11G,
	  PCMCIA_CIS_BUFFALO_WLI_CF_S11G,
	  "BUFFALO AirStation 11Mbps CF WLAN"
	},
	{ PCMCIA_VENDOR_EMTAC,
	  PCMCIA_PRODUCT_EMTAC_WLAN,
	  PCMCIA_CIS_EMTAC_WLAN,
	  "EMTAC A2424i 11Mbps WLAN Card"
	},
	{ PCMCIA_VENDOR_SIMPLETECH,
	  PCMCIA_PRODUCT_SIMPLETECH_SPECTRUM24_ALT,
	  PCMCIA_CIS_SIMPLETECH_SPECTRUM24_ALT,
	  "LA4111 Spectrum24 WLAN PC Card"
	},
	{ PCMCIA_VENDOR_ERICSSON,
	  PCMCIA_PRODUCT_ERICSSON_WIRELESSLAN,
	  PCMCIA_CIS_ERICSSON_WIRELESSLAN,
	  "DSSS Wireless LAN PC Card" 
	},
	{ PCMCIA_VENDOR_PROXIM,
	  PCMCIA_PRODUCT_PROXIM_RANGELANDS_8430,
	  PCMCIA_CIS_PROXIM_RANGELANDS_8430,
	  "Proxim RangeLAN-DS/LAN PC CARD",
	},
	{ PCMCIA_VENDOR_ACTIONTEC,
	  PCMCIA_PRODUCT_ACTIONTEC_HWC01170,
	  PCMCIA_CIS_ACTIONTEC_HWC01170,
	  "ACTIONTEC PRISM Wireless LAN PC CARD",
	},
	{ PCMCIA_VENDOR_NOKIA,
	  PCMCIA_PRODUCT_NOKIA_C020_WLAN,
	  PCMCIA_CIS_NOKIA_C020_WLAN,
	  "NOKIA C020 Wireless LAN PC CARD",
	},
	{ PCMCIA_VENDOR_NOKIA,
	  PCMCIA_PRODUCT_NOKIA_C110_WLAN,
	  PCMCIA_CIS_NOKIA_C110_WLAN,
	  "NOKIA C110 Wireless LAN PC CARD",
	},
	{ PCMCIA_VENDOR_NETGEAR2,
	  PCMCIA_PRODUCT_NETGEAR2_MA401RA,
	  PCMCIA_CIS_NETGEAR2_MA401RA,
	  "Netgear MA401RA Wireless LAN PC CARD",
	},
	{ PCMCIA_VENDOR_AIRVAST,
	  PCMCIA_PRODUCT_AIRVAST_WN_100,
	  PCMCIA_CIS_AIRVAST_WN_100,
	  "AirVast WN-100 Wireless LAN PC CARD",
	},
	{ PCMCIA_VENDOR_SIEMENS,
	  PCMCIA_PRODUCT_SIEMENS_SS1021,
	  PCMCIA_CIS_SIEMENS_SS1021,
	  "SpeedStream 1021 Wireless PCMCIA CARD",
	},
	{ 0,
	  0,
	  { NULL, NULL, NULL, NULL },
	  NULL,
	}
};

static const struct wi_pcmcia_product *wi_lookup(struct pcmcia_attach_args *pa);

const struct wi_pcmcia_product *
wi_lookup(pa)
	struct pcmcia_attach_args *pa;
{
	const struct wi_pcmcia_product *pp;

	/*
	 * Several PRISM II-based cards use the Lucent WaveLAN vendor
	 * and product IDs so we match by CIS information first.
	 */
	for (pp = wi_pcmcia_products; pp->pp_name != NULL; pp++) {
		if (pa->card->cis1_info[0] != NULL &&
		    pp->pp_cisinfo[0] != NULL &&
		    strcmp(pa->card->cis1_info[0], pp->pp_cisinfo[0]) == 0 &&
		    pa->card->cis1_info[1] != NULL &&
		    pp->pp_cisinfo[1] != NULL &&
		    strcmp(pa->card->cis1_info[1], pp->pp_cisinfo[1]) == 0)
			return (pp);
	}

	/* Match by vendor/product ID. */
	for (pp = wi_pcmcia_products; pp->pp_name != NULL; pp++) {
		if (pa->manufacturer != PCMCIA_VENDOR_INVALID &&
		    pa->manufacturer == pp->pp_vendor &&
		    pa->product != PCMCIA_PRODUCT_INVALID &&
		    pa->product == pp->pp_product)
			return (pp);
	}

	return (NULL);
}

int
wi_pcmcia_match(parent, match, aux)
	struct device *parent;
	void *match, *aux;
{
	struct pcmcia_attach_args *pa = aux;

	if (wi_lookup(pa) != NULL)
		return (1);
	return (0);
}

void
wi_pcmcia_attach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	struct wi_pcmcia_softc	*psc = (struct wi_pcmcia_softc *)self;
	struct wi_softc		*sc = &psc->sc_wi;
	struct pcmcia_attach_args *pa = aux;
	struct pcmcia_function	*pf = pa->pf;
	struct pcmcia_config_entry *cfe = SIMPLEQ_FIRST(&pf->cfe_head);
	int			state = 0;

	psc->sc_pf = pf;

	/* Enable the card. */
	pcmcia_function_init(pf, cfe);
	if (pcmcia_function_enable(pf)) {
		printf(": function enable failed\n");
		goto bad;
	}
	state++;

	if (pcmcia_io_alloc(pf, 0, WI_IOSIZ, WI_IOSIZ, &psc->sc_pcioh)) {
		printf(": can't alloc i/o space\n");
		goto bad;
	}
	state++;

	if (pcmcia_io_map(pf, PCMCIA_WIDTH_IO16, 0, WI_IOSIZ,
	    &psc->sc_pcioh, &psc->sc_io_window)) {
		printf(": can't map io space\n");
		goto bad;
	}
	state++;

	printf(" port 0x%lx/%d", psc->sc_pcioh.addr, psc->sc_pcioh.size);

	sc->wi_ltag = sc->wi_btag = psc->sc_pcioh.iot;
	sc->wi_lhandle = sc->wi_bhandle = psc->sc_pcioh.ioh;
	sc->wi_cor_offset = WI_COR_OFFSET;
	sc->wi_flags |= WI_FLAGS_BUS_PCMCIA;

	/* Make sure interrupts are disabled. */
	CSR_WRITE_2(sc, WI_INT_EN, 0);
	CSR_WRITE_2(sc, WI_EVENT_ACK, 0xffff);

	/* Establish the interrupt. */
	sc->sc_ih = pcmcia_intr_establish(pa->pf, IPL_NET, wi_intr, psc, "");
	if (sc->sc_ih == NULL) {
		printf("%s: couldn't establish interrupt\n",
		    sc->sc_dev.dv_xname);
		goto bad;
	}

	wi_attach(sc, &wi_func_io);
	return;

bad:
	if (state > 2)
		pcmcia_io_unmap(pf, psc->sc_io_window);
	if (state > 1)
		pcmcia_io_free(pf, &psc->sc_pcioh);
	if (state > 0)
		pcmcia_function_disable(pf);
}

int
wi_pcmcia_detach(dev, flags)
	struct device *dev;
	int flags;
{
	struct wi_pcmcia_softc *psc = (struct wi_pcmcia_softc *)dev;
	struct wi_softc *sc = &psc->sc_wi;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;

	if (!(sc->wi_flags & WI_FLAGS_ATTACHED)) {
		printf("%s: already detached\n", sc->sc_dev.dv_xname);
		return (0);
	}

	wi_detach(sc);

	sc->wi_flags = 0;

	pcmcia_io_unmap(psc->sc_pf, psc->sc_io_window);
	pcmcia_io_free(psc->sc_pf, &psc->sc_pcioh);

	ether_ifdetach(ifp);
	if_detach(ifp);

	return (0);
}

int
wi_pcmcia_activate(dev, act)
	struct device *dev;
	enum devact act;
{
	struct wi_pcmcia_softc *psc = (struct wi_pcmcia_softc *)dev;
	struct wi_softc *sc = &psc->sc_wi;
	struct ifnet *ifp = &sc->sc_arpcom.ac_if;
	int s;

	s = splnet();
	switch (act) {
	case DVACT_ACTIVATE:
		pcmcia_function_enable(psc->sc_pf);
		sc->sc_ih = pcmcia_intr_establish(psc->sc_pf, IPL_NET,
		    wi_intr, sc, sc->sc_dev.dv_xname);
		wi_cor_reset(sc);
		wi_init(sc);
		break;

	case DVACT_DEACTIVATE:
		ifp->if_timer = 0;
		if (ifp->if_flags & IFF_RUNNING)
			wi_stop(sc);
		sc->wi_flags &= ~WI_FLAGS_INITIALIZED;
		pcmcia_intr_disestablish(psc->sc_pf, sc->sc_ih);
		pcmcia_function_disable(psc->sc_pf);
		break;
	}
	splx(s);
	return (0);
}
