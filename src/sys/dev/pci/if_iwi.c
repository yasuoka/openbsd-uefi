/*	$Id: if_iwi.c,v 1.4 2004/10/28 22:12:00 jcs Exp $  */

/*-
 * Copyright (c) 2004
 *      Damien Bergamini <damien.bergamini@free.fr>. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*-
 * Intel(R) PRO/Wireless 2200BG/2915ABG driver
 * http://www.intel.com/products/mobiletechnology/prowireless.htm
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/sockio.h>
#include <sys/sysctl.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/conf.h>

#include <machine/bus.h>
#include <machine/endian.h>
#include <machine/intr.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif
#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_types.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>

#include <dev/rndvar.h>
#include <crypto/arc4.h>

#include <dev/pci/if_iwireg.h>
#include <dev/pci/if_iwivar.h>

static const struct ieee80211_rateset iwi_rateset_11b =
	{ 4, { 2, 4, 11, 22 } };

static const struct ieee80211_rateset iwi_rateset_11g =
	{ 12, { 2, 4, 11, 22, 12, 18, 24, 36, 48, 72, 96, 108 } };

int iwi_match(struct device *, void *, void *);
void iwi_attach(struct device *, struct device *, void *);
int iwi_detach(struct device *, int);
int iwi_media_change(struct ifnet *);
void iwi_media_status(struct ifnet *, struct ifmediareq *);
u_int16_t iwi_read_prom_word(struct iwi_softc *, u_int8_t);
int iwi_newstate(struct ieee80211com *, enum ieee80211_state, int);
void iwi_fix_channel(struct ieee80211com *, struct mbuf *);
void iwi_frame_intr(struct iwi_softc *, struct iwi_rx_slot *, struct iwi_buf *,
    struct iwi_frame *);
void iwi_notification_intr(struct iwi_softc *, struct iwi_buf *,
    struct iwi_notif *);
void iwi_rx_intr(struct iwi_softc *);
void iwi_tx_intr(struct iwi_softc *, int);
int iwi_intr(void *);
int iwi_cmd(struct iwi_softc *, u_int8_t, void *, u_int8_t, int);
int iwi_tx_start(struct ifnet *, struct mbuf *, struct ieee80211_node *);
void iwi_start(struct ifnet *);
void iwi_watchdog(struct ifnet *);
int iwi_get_table0(struct iwi_softc *, u_int32_t *);
int iwi_get_radio(struct iwi_softc *, int *);
int iwi_ioctl(struct ifnet *, u_long, caddr_t);
int iwi_init_buffers(struct iwi_softc *, int);
void iwi_free_buffers(struct iwi_softc *);
int iwi_init_tx_queue(struct iwi_softc *, int, int);
void iwi_free_tx_queue(struct iwi_softc *, int);
int iwi_init_rx_queue(struct iwi_softc *);
void iwi_free_rx_queue(struct iwi_softc *);
int iwi_init_queues(struct iwi_softc *);
void iwi_free_queues(struct iwi_softc *);
void iwi_stop_master(struct iwi_softc *);
int iwi_reset(struct iwi_softc *);
int iwi_load_ucode(struct iwi_softc *, void *, int);
int iwi_load_firmware(struct iwi_softc *, void *, int);
int iwi_cache_firmware(struct iwi_softc *, void *);
void iwi_free_firmware(struct iwi_softc *);
int iwi_config(struct iwi_softc *);
int iwi_scan(struct iwi_softc *);
int iwi_auth_and_assoc(struct iwi_softc *);
int iwi_init(struct ifnet *);
void iwi_stop(struct ifnet *, int);

static __inline u_int8_t MEM_READ_1(struct iwi_softc *sc, u_int32_t addr)
{
	CSR_WRITE_4(sc, IWI_CSR_INDIRECT_ADDR, addr);
	return CSR_READ_1(sc, IWI_CSR_INDIRECT_DATA);
}

static __inline u_int32_t MEM_READ_4(struct iwi_softc *sc, u_int32_t addr)
{
	CSR_WRITE_4(sc, IWI_CSR_INDIRECT_ADDR, addr);
	return CSR_READ_4(sc, IWI_CSR_INDIRECT_DATA);
}

#define IWI_DEBUG

#ifdef IWI_DEBUG
#define DPRINTF(x)	if (iwi_debug > 0) printf x
#define DPRINTFN(n, x)	if (iwi_debug >= (n)) printf x
int iwi_debug = 2;
#else
#define DPRINTF(x)
#define DPRINTFN(n, x)
#endif

struct cfattach iwi_ca = {
	sizeof (struct iwi_softc), iwi_match, iwi_attach, iwi_detach
};

int
iwi_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = aux;

	if (PCI_VENDOR (pa->pa_id) == PCI_VENDOR_INTEL &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_INTEL_PRO_2200BG_3B)
		return 1;

	return 0;
}

/* Base Address Register */
#define IWI_PCI_BAR0	0x10

void
iwi_attach(struct device *parent, struct device *self, void *aux)
{
	struct iwi_softc *sc = (struct iwi_softc *)self;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct pci_attach_args *pa = aux;
	const char *intrstr;
	bus_space_tag_t memt;
	bus_space_handle_t memh;
	bus_addr_t base;
	pci_intr_handle_t ih;
	pcireg_t data;
	u_int16_t val;
	int error, i;

	sc->sc_pct = pa->pa_pc;

	data = pci_conf_read(sc->sc_pct, pa->pa_tag, 0x40);
	data &= ~0x00ff0000;
	pci_conf_write(sc->sc_pct, pa->pa_tag, 0x40, data);

	/* enable bus-mastering */
	data = pci_conf_read(sc->sc_pct, pa->pa_tag, PCI_COMMAND_STATUS_REG);
	data |= PCI_COMMAND_MASTER_ENABLE;
	pci_conf_write(sc->sc_pct, pa->pa_tag, PCI_COMMAND_STATUS_REG, data);

	/* map the register window */
	error = pci_mapreg_map(pa, IWI_PCI_BAR0, PCI_MAPREG_TYPE_MEM |
	    PCI_MAPREG_MEM_TYPE_32BIT, 0, &memt, &memh, &base, &sc->sc_sz, 0);
	if (error != 0) {
		printf(": could not map memory space\n");
		return;
	}

	sc->sc_st = memt;
	sc->sc_sh = memh;
	sc->sc_dmat = pa->pa_dmat;

	/* disable interrupts */
	CSR_WRITE_4(sc, IWI_CSR_INTR_MASK, 0);

	if (pci_intr_map(pa, &ih) != 0) {
		printf(": could not map interrupt\n");
		return;
	}

	intrstr = pci_intr_string(sc->sc_pct, ih);
	sc->sc_ih = pci_intr_establish(sc->sc_pct, ih, IPL_NET, iwi_intr, sc,
	    sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": could not establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s", intrstr);

	if (iwi_reset(sc) != 0) {
		printf(": could not reset adapter\n");
		return;
	}

	ic->ic_phytype = IEEE80211_T_DS;
	ic->ic_opmode = IEEE80211_M_STA;
	ic->ic_state = IEEE80211_S_INIT;

	/* set device capabilities */
	ic->ic_caps =  IEEE80211_C_IBSS | IEEE80211_C_PMGT |
	    IEEE80211_C_TXPMGT | IEEE80211_C_WEP;

	/* read MAC address from EEPROM */
	val = iwi_read_prom_word(sc, IWI_EEPROM_MAC + 0);
	ic->ic_myaddr[0] = val >> 8;
	ic->ic_myaddr[1] = val & 0xff;
	val = iwi_read_prom_word(sc, IWI_EEPROM_MAC + 1);
	ic->ic_myaddr[2] = val >> 8;
	ic->ic_myaddr[3] = val & 0xff;
	val = iwi_read_prom_word(sc, IWI_EEPROM_MAC + 2);
	ic->ic_myaddr[4] = val >> 8;
	ic->ic_myaddr[5] = val & 0xff;

	printf(", address %s\n", ether_sprintf(ic->ic_myaddr));

	/* set supported .11b rates */
	ic->ic_sup_rates[IEEE80211_MODE_11B] = iwi_rateset_11b;

	/* set supported .11g rates */
	ic->ic_sup_rates[IEEE80211_MODE_11G] = iwi_rateset_11g;

	/* set supported .11b and .11g channels (1 through 14) */
	for (i = 1; i <= 14; i++) {
		ic->ic_channels[i].ic_freq =
		    ieee80211_ieee2mhz(i, IEEE80211_CHAN_2GHZ);
		ic->ic_channels[i].ic_flags =
		    IEEE80211_CHAN_CCK | IEEE80211_CHAN_OFDM |
		    IEEE80211_CHAN_DYN | IEEE80211_CHAN_2GHZ;
	}

	/* Default to authmode OPEN */
	sc->authmode = IEEE80211_AUTH_OPEN;

	/* IBSS channel undefined for now */
	ic->ic_ibss_chan = &ic->ic_channels[0];

	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_init = iwi_init;
	ifp->if_ioctl = iwi_ioctl;
	ifp->if_start = iwi_start;
	ifp->if_watchdog = iwi_watchdog;
	IFQ_SET_READY(&ifp->if_snd);
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);

	if_attach(ifp);
	ieee80211_ifattach(ifp);
	/* override state transition machine */
	sc->sc_newstate = ic->ic_newstate;
	ic->ic_newstate = iwi_newstate;
	ieee80211_media_init(ifp, iwi_media_change, iwi_media_status);

#if NBPFILTER > 0
	bpfattach(&sc->sc_drvbpf, ifp, DLT_IEEE802_11_RADIO,
	    sizeof (struct ieee80211_frame) + 64);

	sc->sc_rxtap_len = sizeof sc->sc_rxtapu;
	sc->sc_rxtap.wr_ihdr.it_len = htole16(sc->sc_rxtap_len);
	sc->sc_rxtap.wr_ihdr.it_present = htole32(IWI_RX_RADIOTAP_PRESENT);

	sc->sc_txtap_len = sizeof sc->sc_txtapu;
	sc->sc_txtap.wt_ihdr.it_len = htole16(sc->sc_txtap_len);
	sc->sc_txtap.wt_ihdr.it_present = htole32(IWI_TX_RADIOTAP_PRESENT);
#endif
}

int
iwi_detach(struct device* self, int flags)
{
	struct iwi_softc *sc = (struct iwi_softc *)self;
	struct ifnet *ifp = &sc->sc_ic.ic_if;

	iwi_stop(ifp, 1);

	if (sc->flags & IWI_FLAG_FW_CACHED)
		iwi_free_firmware(sc);

#if NBPFILTER > 0
	bpfdetach(ifp);
#endif
	ieee80211_ifdetach(ifp);
	if_detach(ifp);

	if (sc->sc_ih != NULL) {
		pci_intr_disestablish(sc->sc_pct, sc->sc_ih);
		sc->sc_ih = NULL;
	}

	bus_space_unmap(sc->sc_st, sc->sc_sh, sc->sc_sz);

	return 0;
}

int
iwi_media_change(struct ifnet *ifp)
{
	int error;

	error = ieee80211_media_change(ifp);
	if (error != ENETRESET)
		return error;

	if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) == (IFF_UP | IFF_RUNNING))
		iwi_init(ifp);

	return 0;
}

void
iwi_media_status(struct ifnet *ifp, struct ifmediareq *imr)
{
	struct iwi_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
#define N(a)	(sizeof (a) / sizeof (a[0]))
	static const struct {
		u_int32_t	val;
		int		rate;
	} rates[] = {
		{ IWI_RATE_DS1,      2 },
		{ IWI_RATE_DS2,      4 },
		{ IWI_RATE_DS5,     11 },
		{ IWI_RATE_DS11,    22 },
		{ IWI_RATE_OFDM6,   12 },
		{ IWI_RATE_OFDM9,   18 },
		{ IWI_RATE_OFDM12,  24 },
		{ IWI_RATE_OFDM18,  36 },
		{ IWI_RATE_OFDM24,  48 },
		{ IWI_RATE_OFDM36,  72 },
		{ IWI_RATE_OFDM48,  96 },
		{ IWI_RATE_OFDM54, 108 },
	};
	u_int32_t val;
	int rate, i;

	imr->ifm_status = IFM_AVALID;
	imr->ifm_active = IFM_IEEE80211;
	if (ic->ic_state == IEEE80211_S_RUN)
		imr->ifm_status |= IFM_ACTIVE;

	/* read current transmission rate from adapter */
	val = CSR_READ_4(sc, IWI_CSR_CURRENT_TX_RATE);

	/* convert rate to 802.11 rate */
	for (i = 0; i < N(rates) && rates[i].val != val; i++);
	rate = (i < N(rates)) ? rates[i].rate : 0;

	imr->ifm_active |= ieee80211_rate2media(ic, rate, ic->ic_curmode);
	switch (ic->ic_opmode) {
	case IEEE80211_M_STA:
		break;

	case IEEE80211_M_IBSS:
		imr->ifm_active |= IFM_IEEE80211_ADHOC;
		break;

	case IEEE80211_M_MONITOR:
		imr->ifm_active |= IFM_IEEE80211_MONITOR;
		break;

	case IEEE80211_M_AHDEMO:
	case IEEE80211_M_HOSTAP:
		/* should not get there */
		break;
	}
#undef N
}

int
iwi_newstate(struct ieee80211com *ic, enum ieee80211_state nstate, int arg)
{
	struct iwi_softc *sc = ic->ic_softc;

	switch (nstate) {
	case IEEE80211_S_SCAN:
		iwi_scan(sc);
		break;

	case IEEE80211_S_AUTH:
		iwi_auth_and_assoc(sc);
		break;

	case IEEE80211_S_INIT:
	case IEEE80211_S_RUN:
	case IEEE80211_S_ASSOC:
		break;
	}

	ic->ic_state = nstate;
	return 0;
}

/*
 * Read 16 bits at address 'addr' from the Microwire EEPROM.
 * DON'T PLAY WITH THIS CODE UNLESS YOU KNOW *EXACTLY* WHAT YOU'RE DOING!
 */
u_int16_t
iwi_read_prom_word(struct iwi_softc *sc, u_int8_t addr)
{
	u_int32_t tmp;
	u_int16_t val;
	int n;

	/* Clock C once before the first command */
	IWI_EEPROM_CTL(sc, 0);
	IWI_EEPROM_CTL(sc, IWI_EEPROM_S);
	IWI_EEPROM_CTL(sc, IWI_EEPROM_S | IWI_EEPROM_C);
	IWI_EEPROM_CTL(sc, IWI_EEPROM_S);

	/* Write start bit (1) */
	IWI_EEPROM_CTL(sc, IWI_EEPROM_S | IWI_EEPROM_D);
	IWI_EEPROM_CTL(sc, IWI_EEPROM_S | IWI_EEPROM_D | IWI_EEPROM_C);

	/* Write READ opcode (10) */
	IWI_EEPROM_CTL(sc, IWI_EEPROM_S | IWI_EEPROM_D);
	IWI_EEPROM_CTL(sc, IWI_EEPROM_S | IWI_EEPROM_D | IWI_EEPROM_C);
	IWI_EEPROM_CTL(sc, IWI_EEPROM_S);
	IWI_EEPROM_CTL(sc, IWI_EEPROM_S | IWI_EEPROM_C);

	/* Write address A7-A0 */
	for (n = 7; n >= 0; n--) {
		IWI_EEPROM_CTL(sc, IWI_EEPROM_S |
		    (((addr >> n) & 1) << IWI_EEPROM_SHIFT_D));
		IWI_EEPROM_CTL(sc, IWI_EEPROM_S |
		    (((addr >> n) & 1) << IWI_EEPROM_SHIFT_D) | IWI_EEPROM_C);
	}

	IWI_EEPROM_CTL(sc, IWI_EEPROM_S);

	/* Read data Q15-Q0 */
	val = 0;
	for (n = 15; n >= 0; n--) {
		IWI_EEPROM_CTL(sc, IWI_EEPROM_S | IWI_EEPROM_C);
		IWI_EEPROM_CTL(sc, IWI_EEPROM_S);
		tmp = MEM_READ_4(sc, IWI_MEM_EEPROM_CTL);
		val |= ((tmp & IWI_EEPROM_Q) >> IWI_EEPROM_SHIFT_Q) << n;
	}

	IWI_EEPROM_CTL(sc, 0);

	/* Clear Chip Select and clock C */
	IWI_EEPROM_CTL(sc, IWI_EEPROM_S);
	IWI_EEPROM_CTL(sc, 0);
	IWI_EEPROM_CTL(sc, IWI_EEPROM_C);

	return betoh16(val);
}

/* XXX Horrible hack to fix channel number of beacons and probe responses */
void
iwi_fix_channel(struct ieee80211com *ic, struct mbuf *m)
{
	struct ieee80211_frame *wh;
	u_int8_t subtype;
	u_int8_t *frm, *efrm;

	wh = mtod(m, struct ieee80211_frame *);

	if ((wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) != IEEE80211_FC0_TYPE_MGT)
		return;

	subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;

	if (subtype != IEEE80211_FC0_SUBTYPE_BEACON &&
	    subtype != IEEE80211_FC0_SUBTYPE_PROBE_RESP)
		return;

	frm = (u_int8_t *)(wh + 1);
	efrm = mtod(m, u_int8_t *) + m->m_len;

	frm += 12;	/* skip tstamp, bintval and capinfo */
	while (frm < efrm) {
		if (*frm == IEEE80211_ELEMID_DSPARMS)
#if IEEE80211_CHAN_MAX < 255
		if (frm[2] <= IEEE80211_CHAN_MAX)
#endif
			ic->ic_bss->ni_chan = &ic->ic_channels[frm[2]];

		frm += frm[1] + 2;
	}
}

void
iwi_frame_intr(struct iwi_softc *sc, struct iwi_rx_slot *slot,
    struct iwi_buf *buf, struct iwi_frame *frame)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct mbuf *m;
	struct ieee80211_frame *wh;
	struct ieee80211_node *ni;
	int error;

	DPRINTFN(5, ("RX!DATA!%u!%u!%u\n", letoh16(frame->len), frame->chan,
	    IWI_RSSI2DBM(frame->rssi, frame->agc)));

	bus_dmamap_sync(sc->sc_dmat, buf->map, sizeof (struct iwi_hdr),
	    sizeof (struct iwi_frame) + letoh16(frame->len),
	    BUS_DMASYNC_POSTREAD);

	bus_dmamap_unload(sc->sc_dmat, buf->map);

	/* Finalize mbuf */
	m = buf->m;
	m->m_pkthdr.rcvif = ifp;
	m->m_pkthdr.len = m->m_len = sizeof (struct iwi_hdr) +
	    sizeof (struct iwi_frame) + letoh16(frame->len);

	m_adj(m, sizeof (struct iwi_hdr) + sizeof (struct iwi_frame));

	wh = mtod(m, struct ieee80211_frame *);
	if (wh->i_fc[1] & IEEE80211_FC1_WEP) {
		/*
		 * Hardware decrypts the frame itself but leaves the WEP bit
		 * set in the 802.11 header and don't remove the iv and crc
		 * fields
		 */
		wh->i_fc[1] &= ~IEEE80211_FC1_WEP;
		bcopy(wh, (char *)wh + IEEE80211_WEP_IVLEN +
		    IEEE80211_WEP_KIDLEN, sizeof (struct ieee80211_frame));
		m_adj(m, IEEE80211_WEP_IVLEN + IEEE80211_WEP_KIDLEN);
		m_adj(m, -IEEE80211_WEP_CRCLEN);
		wh = mtod(m, struct ieee80211_frame *);
	}

#if NBPFILTER > 0
	if (sc->sc_drvbpf != NULL) {
		struct mbuf mb;
		struct iwi_rx_radiotap_header *tap = &sc->sc_rxtap;

		tap->wr_flags = 0;
		tap->wr_rate = frame->rate;
		tap->wr_chan_freq =
		    htole16(ic->ic_channels[frame->chan].ic_freq);
		tap->wr_chan_flags =
		    htole16(ic->ic_channels[frame->chan].ic_flags);
		tap->wr_antsignal = frame->signal;
		tap->wr_antnoise = frame->noise;
		tap->wr_antenna = frame->antenna;

		M_DUP_PKTHDR(&mb, m);
		mb.m_data = (caddr_t)tap;
		mb.m_len = sc->sc_rxtap_len;
		mb.m_next = m;
		mb.m_pkthdr.len += mb.m_len;
		bpf_mtap(sc->sc_drvbpf, &mb);
	}
#endif

	/*
	 * Management frames (beacons or probe responses) received during
	 * scanning have an invalid channel field. Thus these frames are
	 * rejected by the 802.11 layer which breaks AP detection.
	 */
	if (ic->ic_state == IEEE80211_S_SCAN)
		iwi_fix_channel(ic, m);

	ni = ieee80211_find_rxnode(ic, wh);
	
	/* Send it up to the upper layer */
	ieee80211_input(ifp, m, ni, IWI_RSSI2DBM(frame->rssi, frame->agc), 0);

	if (ni == ic->ic_bss)
		ieee80211_unref_node(&ni);
	else
		ieee80211_free_node(ic, ni);

	MGETHDR(m, M_DONTWAIT, MT_DATA);
	if (m == NULL) {
		printf("%s: could not allocate rx mbuf\n",
		    sc->sc_dev.dv_xname);
		return;
	}
	MCLGET(m, M_DONTWAIT);
	if (!(m->m_flags & M_EXT)) {
		m_freem(m);
		printf("%s: could not allocate rx mbuf cluster\n",
		    sc->sc_dev.dv_xname);
		return;
	}

	error = bus_dmamap_load(sc->sc_dmat, buf->map, mtod(m, void *),
	    MCLBYTES, NULL, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not map buffer dma memory\n",
		    sc->sc_dev.dv_xname);
		m_freem(m);
		return;
	}

	buf->m = m;
	CSR_WRITE_4(sc, slot->csr, buf->map->dm_segs[0].ds_addr);
}

void
iwi_notification_intr(struct iwi_softc *sc, struct iwi_buf *buf,
    struct iwi_notif *notif)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct iwi_notif_scan_complete *scan;
	struct iwi_notif_authentication *auth;
	struct iwi_notif_association *assoc;

	DPRINTFN(2, ("RX!NOTIFICATION!%u!%u\n", notif->type,
	    letoh16(notif->len)));

	bus_dmamap_sync(sc->sc_dmat, buf->map, sizeof (struct iwi_hdr),
	    sizeof (struct iwi_notif) + letoh16(notif->len),
	    BUS_DMASYNC_POSTREAD);

	switch (notif->type) {
	case IWI_NOTIF_TYPE_SCAN_CHANNEL:
		break;

	case IWI_NOTIF_TYPE_SCAN_COMPLETE:
		scan = (struct iwi_notif_scan_complete *)(notif + 1);

		DPRINTFN(2, ("Scan completed (%u, %u)\n", scan->nchan,
		    scan->status));

		ieee80211_end_scan(ifp);
		break;

	case IWI_NOTIF_TYPE_AUTHENTICATION:
		auth = (struct iwi_notif_authentication *)(notif + 1);

		DPRINTFN(2, ("Authentication (%u)\n", auth->state));

		switch (auth->state) {
		case IWI_AUTHENTICATED:
			ieee80211_new_state(ic, IEEE80211_S_ASSOC, -1);
			break;

		case IWI_DEAUTHENTICATED:
			/*ieee80211_begin_scan(ifp);*/
			break;

		default:
			printf("%s: unknown authentication state %u\n",
			    sc->sc_dev.dv_xname, auth->state);
		}
		break;

	case IWI_NOTIF_TYPE_ASSOCIATION:
		assoc = (struct iwi_notif_association *)(notif + 1);

		DPRINTFN(2, ("Association (%u, %u)\n", assoc->state,
		    assoc->status));

		switch (assoc->state) {
		case IWI_ASSOCIATED:
			ieee80211_new_state(ic, IEEE80211_S_RUN, -1);
			break;

		case IWI_DEASSOCIATED:
			ieee80211_begin_scan(ifp);
			break;

		default:
			printf("%s: unknown association state %u\n",
			    sc->sc_dev.dv_xname, assoc->state);
		}
		break;

	case IWI_NOTIF_TYPE_CALIBRATION:
	case IWI_NOTIF_TYPE_BEACON:
		break;

	default:
		printf("%s: unknown notification type %u\n",
		    sc->sc_dev.dv_xname, notif->type);
	}
}

void
iwi_rx_intr(struct iwi_softc *sc)
{
	struct iwi_rx_queue *q = &sc->rxqueue;
	struct iwi_rx_slot *slot;
	struct iwi_buf *buf;
	struct iwi_hdr *hdr;
	u_int32_t r, i;

	r = CSR_READ_4(sc, IWI_CSR_RX_READ_INDEX);

	for (i = (q->cur + 1) % q->size; i != r; i = (i + 1) % q->size) {
		slot = &q->slots[i];
		buf = slot->buf;

		bus_dmamap_sync(sc->sc_dmat, buf->map, 0,
		    sizeof (struct iwi_hdr), BUS_DMASYNC_POSTREAD);

		hdr = mtod(buf->m, struct iwi_hdr *);

		switch (hdr->type) {
		case IWI_HDR_TYPE_FRAME:
			iwi_frame_intr(sc, slot, buf,
			    (struct iwi_frame *)(hdr + 1));
			break;

		case IWI_HDR_TYPE_NOTIF:
			iwi_notification_intr(sc, buf,
			    (struct iwi_notif *)(hdr + 1));
			break;

		default:
			printf("%s: unknown hdr type %u\n",
			    sc->sc_dev.dv_xname, hdr->type);
		}
	}

	/* Tell the firmware what we have processed */
	q->cur = (r == 0) ? q->size - 1 : r - 1;
	CSR_WRITE_4(sc, IWI_CSR_RX_WRITE_INDEX, q->cur);
}

void
iwi_tx_intr(struct iwi_softc *sc, int index)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct iwi_tx_queue *q = &sc->txqueue[index];
	struct iwi_buf *buf;
	u_int32_t r, i;

	r = CSR_READ_4(sc, q->csr_read);

	for (i = (q->old + 1) % q->size; i != r; i = (i + 1) % q->size) {
		buf = q->slots[i].buf;

		bus_dmamap_unload(sc->sc_dmat, buf->map);
		m_freem(buf->m);
		buf->m = NULL;

		if (buf->ni != NULL) {
			if (buf->ni != ic->ic_bss)
				ieee80211_free_node(ic, buf->ni);
			buf->ni = NULL;
		}

		/* kill watchdog timer */
		sc->sc_tx_timer = 0;

		TAILQ_INSERT_TAIL(&sc->sc_free_buf, buf, next);
	}

	/* Remember what the firmware has processed */
	q->old = (r == 0) ? q->size - 1 : r - 1;

	/* Call start() since some buffer descriptors have been released */
	ifp->if_flags &= ~IFF_OACTIVE;
	(*ifp->if_start)(ifp);
}

int
iwi_intr(void *arg)
{
	struct iwi_softc *sc = arg;
	u_int32_t r;

	if ((r = CSR_READ_4(sc, IWI_CSR_INTR)) == 0)
		return 0;

	/* Disable interrupts */
	CSR_WRITE_4(sc, IWI_CSR_INTR_MASK, 0);

	DPRINTFN(8, ("INTR!0x%08x\n", r));

	if (r & IWI_INTR_RX_TRANSFER)
		iwi_rx_intr(sc);

	if (r & IWI_INTR_TX0_TRANSFER)
		wakeup(sc);

	if (r & IWI_INTR_TX1_TRANSFER)
		iwi_tx_intr(sc, 1);

	if (r & IWI_INTR_TX2_TRANSFER)
		iwi_tx_intr(sc, 2);

	if (r & IWI_INTR_TX3_TRANSFER)
		iwi_tx_intr(sc, 3);

	if (r & IWI_INTR_TX4_TRANSFER)
		iwi_tx_intr(sc, 4);

	if (r & IWI_INTR_FW_INITED) {
		if (!(r & (IWI_INTR_FATAL_ERROR | IWI_INTR_PARITY_ERROR)))
			wakeup(sc);
	}

	if (r & (IWI_INTR_FATAL_ERROR | IWI_INTR_PARITY_ERROR)) {
		printf("%s: fatal error\n", sc->sc_dev.dv_xname);
		iwi_stop(&sc->sc_ic.ic_if, 1);
	}

	/* Acknowledge interrupts */
	CSR_WRITE_4(sc, IWI_CSR_INTR, r);

	/* Re-enable interrupts */
	CSR_WRITE_4(sc, IWI_CSR_INTR_MASK, IWI_INTR_MASK);

	return 0;
}

int
iwi_cmd(struct iwi_softc *sc, u_int8_t type, void *data, u_int8_t len,
    int flags)
{
	struct iwi_tx_queue *q = &sc->txqueue[0];
	struct iwi_tx_slot *slot;
	struct iwi_hdr *hdr;
	struct iwi_cmd *cmd;

	DPRINTFN(2, ("TX!CMD!%u!%u\n", type, len));

	slot = &q->slots[q->cur];

	hdr = (struct iwi_hdr *)slot->virtaddr;
	hdr->type = IWI_HDR_TYPE_COMMAND;
	hdr->flags = IWI_HDR_FLAG_IRQ;

	cmd = (struct iwi_cmd *)(hdr + 1);
	cmd->type = type;
	cmd->len = len;
	bcopy(data, cmd->data, len);

	bus_dmamap_sync(sc->sc_dmat, q->map, q->cur * IWI_TX_SLOT_SIZE,
	    sizeof (struct iwi_hdr) + sizeof (struct iwi_cmd),
	    BUS_DMASYNC_PREWRITE);

	q->cur = (q->cur + 1) % q->size;
	CSR_WRITE_4(sc, q->csr_write, q->cur);

	if (flags & IWI_ASYNC_CMD)
		return 0;

	return tsleep(sc, 0, "iwicmd", hz);
}

int
iwi_tx_start(struct ifnet *ifp, struct mbuf *m, struct ieee80211_node *ni)
{
	struct iwi_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct iwi_tx_queue *q = &sc->txqueue[1];
	struct ieee80211_frame *wh;
	struct iwi_tx_slot *slot;
	struct iwi_buf *buf;
	struct iwi_hdr *hdr;
	struct iwi_data *data;
	int error, i;

#if NBPFILTER > 0
	if (sc->sc_drvbpf != NULL) {
		struct mbuf mb;
		struct iwi_tx_radiotap_header *tap = &sc->sc_txtap;

		tap->wt_flags = 0;
		tap->wt_chan_freq = htole16(ic->ic_bss->ni_chan->ic_freq);
		tap->wt_chan_flags = htole16(ic->ic_bss->ni_chan->ic_flags);

		M_DUP_PKTHDR(&mb, m);
		mb.m_data = (caddr_t)tap;
		mb.m_len = sc->sc_txtap_len;
		mb.m_next = m;
		mb.m_pkthdr.len += mb.m_len;
		bpf_mtap(sc->sc_drvbpf, &mb);
	}
#endif

	wh = mtod(m, struct ieee80211_frame *);

	/* trim IEEE802.11 header */
	m_adj(m, sizeof (struct ieee80211_frame));

	buf = TAILQ_FIRST(&sc->sc_free_buf);

	error = bus_dmamap_load_mbuf(sc->sc_dmat, buf->map, m, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not map mbuf (error %d)\n",
		    sc->sc_dev.dv_xname, error);
		m_freem(m);
		return error;
	}

	TAILQ_REMOVE(&sc->sc_free_buf, buf, next);

	buf->m = m;
	buf->ni = ni;

	slot = &q->slots[q->cur];
	slot->buf = buf;

	hdr = (struct iwi_hdr *)slot->virtaddr;
	hdr->type = IWI_HDR_TYPE_DATA;
	hdr->flags = IWI_HDR_FLAG_IRQ;

	data = (struct iwi_data *)(hdr + 1);
	data->cmd = IWI_DATA_CMD_TX;
	data->len = htole16(m->m_pkthdr.len);
	data->flags = IWI_DATA_FLAG_NEED_ACK;

	if (ic->ic_flags & IEEE80211_F_WEPON) {
		wh->i_fc[1] |= IEEE80211_FC1_WEP;
		data->wep_txkey = ic->ic_wep_txkey;
	} else
		data->flags |= IWI_DATA_FLAG_NO_WEP;
#if 0
	if (ic->ic_flags & IEEE80211_F_SHPREAMBLE)
		data->flags |= IWI_DATA_FLAG_SHPREAMBLE;
#endif
	bcopy(wh, &data->wh, sizeof (struct ieee80211_frame));
	data->nseg = htole32(buf->map->dm_nsegs);
	for (i = 0; i < buf->map->dm_nsegs; i++) {
		data->seg_addr[i] = htole32(buf->map->dm_segs[i].ds_addr);
		data->seg_len[i]  = htole32(buf->map->dm_segs[i].ds_len);
	}

	bus_dmamap_sync(sc->sc_dmat, q->map, q->cur * IWI_TX_SLOT_SIZE,
	    sizeof (struct iwi_hdr) + sizeof (struct iwi_data),
	    BUS_DMASYNC_PREWRITE);

	bus_dmamap_sync(sc->sc_dmat, buf->map, 0, MCLBYTES,
	    BUS_DMASYNC_PREWRITE);

	DPRINTFN(5, ("TX!DATA!%u!%u\n", data->len, data->nseg));

	/* Inform firmware about this new packet */
	q->cur = (q->cur + 1) % q->size;
	CSR_WRITE_4(sc, q->csr_write, q->cur);

	return 0;
}

void
iwi_start(struct ifnet *ifp)
{
	struct iwi_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct mbuf *m;
	struct ieee80211_node *ni;

	if (ic->ic_state != IEEE80211_S_RUN)
		return;

	for (;;) {
		IF_DEQUEUE(&ifp->if_snd, m);
		if (m == NULL)
			break;

		if (TAILQ_EMPTY(&sc->sc_free_buf)) {
			IF_PREPEND(&ifp->if_snd, m);
			ifp->if_flags |= IFF_OACTIVE;
			break;
		}

#if NBPFILTER > 0
		if (ifp->if_bpf != NULL)
			bpf_mtap(ifp->if_bpf, m);
#endif

		m = ieee80211_encap(ifp, m, &ni);
		if (m == NULL)
			continue;

#if NBPFILTER > 0
		if (ic->ic_rawbpf != NULL)
			bpf_mtap(ic->ic_rawbpf, m);
#endif

		if (iwi_tx_start(ifp, m, ni) != 0) {
			if (ni != NULL && ni != ic->ic_bss)
				ieee80211_free_node(ic, ni);
			break;
		}

		/* start watchdog timer */
		sc->sc_tx_timer = 5;
		ifp->if_timer = 1;
	}
}

void
iwi_watchdog(struct ifnet *ifp)
{
	struct iwi_softc *sc = ifp->if_softc;

	ifp->if_timer = 0;

	if (sc->sc_tx_timer > 0) {
		if (--sc->sc_tx_timer == 0) {
			printf("%s: device timeout\n", sc->sc_dev.dv_xname);
			iwi_stop(ifp, 1);
			return;
		}
		ifp->if_timer = 1;
	}

	ieee80211_watchdog(ifp);
}

int
iwi_get_table0(struct iwi_softc *sc, u_int32_t *tbl)
{
	u_int32_t size, buf[128];

	if (!(sc->flags & IWI_FLAG_FW_INITED))
		return ENOTTY;

	size = CSR_READ_4(sc, IWI_CSR_TABLE0_SIZE);
	CSR_READ_REGION_4(sc, IWI_CSR_TABLE0_BASE, &buf[1], size);

	return copyout(buf, tbl, size * sizeof (u_int32_t));
}

int
iwi_get_radio(struct iwi_softc *sc, int *ret)
{
	int val;

	val = (CSR_READ_4(sc, IWI_CSR_IO) & IWI_IO_RADIO_ENABLED) ? 1 : 0;
	return copyout(&val, ret, sizeof (int));
}

int
iwi_ioctl(struct ifnet *ifp, u_long cmd, caddr_t data)
{
	struct iwi_softc *sc = ifp->if_softc;
	struct ifreq *ifr;
	struct ifaddr *ifa;
	int s, error = 0;

	s = splnet();

	switch (cmd) {
	case SIOCSIFADDR:
		ifa = (struct ifaddr *) data;
		ifp->if_flags |= IFF_UP;
		switch (ifa->ifa_addr->sa_family) {
#ifdef INET
		case AF_INET:
			if (!(ifp->if_flags & IFF_UP))
				iwi_init(ifp);
			arp_ifinit(&sc->sc_ic.ic_ac, ifa);
			break;
#endif
		default:
			iwi_init(ifp);
		}
		break;

	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (!(ifp->if_flags & IFF_RUNNING))
				iwi_init(ifp);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				iwi_stop(ifp, 1);
		}
		break;

	case SIOCGTABLE0:
		ifr = (struct ifreq *)data;
		error = iwi_get_table0(sc, (u_int32_t *)ifr->ifr_data);
		break;

	case SIOCGRADIO:
		ifr = (struct ifreq *)data;
		error = iwi_get_radio(sc, (int *)ifr->ifr_data);
		break;

	case SIOCSLOADFW:
		/* only super-user can do that! */
		if ((error = suser(curproc, 0)) != 0)
			break;

		ifr = (struct ifreq *)data;
		error = iwi_cache_firmware(sc, ifr->ifr_data);
		break;

	case SIOCSKILLFW:
		/* only super-user can do that! */
		if ((error = suser(curproc, 0)) != 0)
			break;

		iwi_stop(ifp, 1);
		iwi_free_firmware(sc);
		break;

	case SIOCG80211AUTH:
		((struct ieee80211_auth *)data)->i_authtype = sc->authmode;
		break;

	case SIOCS80211AUTH:
		/* only super-user can do that! */
		if ((error = suser(curproc, 0)) != 0)
			break;

		sc->authmode = ((struct ieee80211_auth *)data)->i_authtype;
		break;

	default:
		error = ieee80211_ioctl(ifp, cmd, data);
	}

	if (error == ENETRESET && cmd != SIOCADDMULTI) {
		if ((ifp->if_flags & (IFF_UP | IFF_RUNNING)) ==
		    (IFF_UP | IFF_RUNNING))
			iwi_init(ifp);
		error = 0;
	}

	splx(s);
	return error;
}

int
iwi_init_buffers(struct iwi_softc *sc, int nbuf)
{
	struct iwi_buf *buf;
	int error, i;

	sc->nbuf = nbuf;
	sc->buf_list = malloc(nbuf * sizeof (struct iwi_buf), M_DEVBUF,
	    M_NOWAIT);
	if (sc->buf_list == NULL) {
		printf("%s: could not allocate buffers\n",
		    sc->sc_dev.dv_xname);
		error = ENOMEM;
		goto fail;
	}
	bzero(sc->buf_list, nbuf * sizeof (struct iwi_buf));

	TAILQ_INIT(&sc->sc_free_buf);
	for (i = 0, buf = sc->buf_list; i < nbuf; i++, buf++) {
		error = bus_dmamap_create(sc->sc_dmat, nbuf * MCLBYTES, nbuf,
		    MCLBYTES, 0, BUS_DMA_NOWAIT, &buf->map);
		if (error != 0) {
			printf("%s: could not create buffer dma map",
			    sc->sc_dev.dv_xname);
			goto fail;
		}
		TAILQ_INSERT_TAIL(&sc->sc_free_buf, buf, next);
	}

	return 0;

fail:	iwi_free_buffers(sc);
	return error;
}

void
iwi_free_buffers(struct iwi_softc *sc)
{
	struct iwi_buf *buf;
	int i;

	if (sc->buf_list != NULL) {
		for (i = 0, buf = sc->buf_list; i < sc->nbuf; i++, buf++) {
			if (buf->m != NULL) {
				m_freem(buf->m);
				bus_dmamap_unload(sc->sc_dmat, buf->map);
			}
			bus_dmamap_destroy(sc->sc_dmat, buf->map);
		}

		free(sc->buf_list, M_DEVBUF);
		sc->buf_list = NULL;
	}
}

int
iwi_init_tx_queue(struct iwi_softc *sc, int index, int size)
{
	struct iwi_tx_queue *q = &sc->txqueue[index];
	struct iwi_tx_slot *slot;
	int i, nsegs, error;

	/* Allocate DMA memory for transmission slots */
	error = bus_dmamap_create(sc->sc_dmat, size * IWI_TX_SLOT_SIZE, 1,
	    size * IWI_TX_SLOT_SIZE, 0, BUS_DMA_NOWAIT, &q->map);
	if (error != 0) {
		printf("%s: could not create dma map\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_alloc(sc->sc_dmat, size * IWI_TX_SLOT_SIZE,
	    PAGE_SIZE, 0, &q->seg, 1, &nsegs, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not allocate dma memory\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamem_map(sc->sc_dmat, &q->seg, nsegs,
	    size * IWI_TX_SLOT_SIZE, &q->virtaddr, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not map dma memory\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	error = bus_dmamap_load(sc->sc_dmat, q->map, q->virtaddr,
	    size * IWI_TX_SLOT_SIZE, NULL, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not load dma memory\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	bzero(q->virtaddr, size * IWI_TX_SLOT_SIZE);

	q->slots = malloc(size * sizeof (struct iwi_tx_slot), M_DEVBUF,
	    M_NOWAIT);
	if (q->slots == NULL) {
		printf("%s: could not allocate tx slots\n",
		    sc->sc_dev.dv_xname);
		error = ENOMEM;
		goto fail;
	}
	bzero(q->slots, size * sizeof (struct iwi_tx_slot));
	for (i = 0, slot = q->slots; i < size; i++, slot++)
		slot->virtaddr = (u_char *)q->virtaddr + i * IWI_TX_SLOT_SIZE;

	q->size = size;
	q->old = q->size - 1;
	q->cur = 0;
	q->csr_read = IWI_CSR_TX_READ_INDEX + index * 4;
	q->csr_write = IWI_CSR_TX_WRITE_INDEX + index * 4;

	CSR_WRITE_4(sc, IWI_CSR_TX_BASE + index * 8,
	    q->map->dm_segs[0].ds_addr);
	CSR_WRITE_4(sc, IWI_CSR_TX_SIZE + index * 8, q->size);
	CSR_WRITE_4(sc, q->csr_read, 0);
	CSR_WRITE_4(sc, q->csr_write, q->cur);

	return 0;

fail:	iwi_free_tx_queue(sc, index);
	return error;
}

void
iwi_free_tx_queue(struct iwi_softc *sc, int index)
{
	struct iwi_tx_queue *q = &sc->txqueue[index];

	if (q->slots != NULL) {
		free(q->slots, M_DEVBUF);
		q->slots = NULL;
	}

	if (q->virtaddr != NULL) {
		bus_dmamap_unload(sc->sc_dmat, q->map);
		bus_dmamem_unmap(sc->sc_dmat, q->virtaddr,
		    q->size * IWI_TX_SLOT_SIZE);
		bus_dmamem_free(sc->sc_dmat, &q->seg, 1);
		q->virtaddr = NULL;
		bus_dmamap_destroy(sc->sc_dmat, q->map);
		q->map = NULL;
	}
}

int
iwi_init_rx_queue(struct iwi_softc *sc)
{
	struct iwi_rx_queue *q = &sc->rxqueue;
	struct iwi_rx_slot *slot;
	struct iwi_buf *buf;
	int error, i;

	q->slots = malloc(IWI_RX_NSLOTS * sizeof (struct iwi_rx_slot),
	    M_DEVBUF, M_NOWAIT);
	if (q->slots == NULL) {
		printf("%s: could not allocate rx slots\n",
		    sc->sc_dev.dv_xname);
		error = ENOMEM;
		goto fail;
	}
	bzero(q->slots, IWI_RX_NSLOTS * sizeof (struct iwi_rx_slot));
	for (i = 0, slot = q->slots; i < IWI_RX_NSLOTS; i++, slot++) {
		slot->csr = IWI_CSR_RX_SLOT_BASE + i * 4;

		buf = slot->buf = TAILQ_FIRST(&sc->sc_free_buf);
		TAILQ_REMOVE(&sc->sc_free_buf, buf, next);

		MGETHDR(buf->m, M_DONTWAIT, MT_DATA);
		if (buf->m == NULL) {
			printf("%s: could not allocate rx mbuf\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
		MCLGET(buf->m, M_DONTWAIT);
		if (!(buf->m->m_flags & M_EXT)) {
			m_freem(buf->m);
			printf("%s: could not allocate rx mbuf cluster\n",
			    sc->sc_dev.dv_xname);
			error = ENOMEM;
			goto fail;
		}
		
		error = bus_dmamap_load(sc->sc_dmat, buf->map,
		    mtod(buf->m, void *), MCLBYTES, NULL, BUS_DMA_NOWAIT);
		if (error != 0) {
			printf("%s: could not map rxbuf dma memory\n",
			    sc->sc_dev.dv_xname);
			goto fail;
		}

		CSR_WRITE_4(sc, slot->csr, buf->map->dm_segs[0].ds_addr);
	}

	q->size = IWI_RX_NSLOTS;
	q->cur = q->size - 1;

	CSR_WRITE_4(sc, IWI_CSR_RX_READ_INDEX, 0);
	CSR_WRITE_4(sc, IWI_CSR_RX_WRITE_INDEX, q->cur);

	return 0;

fail:	iwi_free_rx_queue(sc);
	return error;
}

void
iwi_free_rx_queue(struct iwi_softc *sc)
{
	struct iwi_rx_queue *q = &sc->rxqueue;

	if (q->slots != NULL) {
		free(q->slots, M_DEVBUF);
		q->slots = NULL;
	}
}

int
iwi_init_queues(struct iwi_softc *sc)
{
	int i, error;

	if ((error = iwi_init_buffers(sc, 64 + IWI_RX_NSLOTS - 2)) != 0)
		goto fail;

	/*
	 * There are 5 different transmission queues. Queue 0 is used for
	 * transmitting host commands while queues 1 through 4 are used for
	 * transmitting data at different QoS levels. Only queues 0 and 1 are
	 * used at this time but firmware requires all queues to be correctly
	 * initialized.
	 */
	for (i = 0; i < 5; i++) {
		error = iwi_init_tx_queue(sc, i, (i != 0) ? 64 : 16);
		if (error != 0) {
			printf("%s: could not initialize tx queue %d\n",
			    sc->sc_dev.dv_xname, i);
			goto fail;
		}
	}

	if ((error = iwi_init_rx_queue(sc)) != 0)
		goto fail;

	return 0;

fail:	iwi_free_queues(sc);
	return error;
}

void
iwi_free_queues(struct iwi_softc *sc)
{
	int i;

	iwi_free_buffers(sc);
	iwi_free_rx_queue(sc);

	for (i = 0; i < 5; i++)
		iwi_free_tx_queue(sc, i);
}

void
iwi_stop_master(struct iwi_softc *sc)
{
	int ntries;

	/* Disable interrupts */
	CSR_WRITE_4(sc, IWI_CSR_INTR_MASK, 0);

	CSR_WRITE_4(sc, IWI_CSR_RST, IWI_RST_STOP_MASTER);
	for (ntries = 0; ntries < 50; ntries++) {
		if (CSR_READ_4(sc, IWI_CSR_RST) & IWI_RST_MASTER_DISABLED)
			break;
		DELAY(10);
	}
	if (ntries == 50)
		printf("%s: timeout waiting for master\n", sc->sc_dev.dv_xname);

	CSR_WRITE_4(sc, IWI_CSR_RST, CSR_READ_4(sc, IWI_CSR_RST) |
	    IWI_RST_PRINCETON_RESET);

	sc->flags &= ~IWI_FLAG_FW_INITED;
}

int
iwi_reset(struct iwi_softc *sc)
{
	int ntries;

	iwi_stop_master(sc);

	/* Move adapter to D0 state */
	CSR_WRITE_4(sc, IWI_CSR_CTL, CSR_READ_4(sc, IWI_CSR_CTL) |
	    IWI_CTL_INIT);

	/* Initialize Phase-Locked Level  (PLL) */
	CSR_WRITE_4(sc, IWI_CSR_READ_INT, IWI_READ_INT_INIT_HOST);

	/* Wait for clock stabilization */
	for (ntries = 0; ntries < 1000; ntries++) {
		if (CSR_READ_4(sc, IWI_CSR_CTL) & IWI_CTL_CLOCK_READY)
			break;
		DELAY(200);
	}
	if (ntries == 1000)
		return EIO;

	CSR_WRITE_4(sc, IWI_CSR_RST, CSR_READ_4(sc, IWI_CSR_RST) |
	    IWI_RST_SW_RESET);

	DELAY(10);

	CSR_WRITE_4(sc, IWI_CSR_CTL, CSR_READ_4(sc, IWI_CSR_CTL) |
	    IWI_CTL_INIT);

	return 0;
}

int
iwi_load_ucode(struct iwi_softc *sc, void *uc, int size)
{
	u_int16_t *w;
	int ntries, i;

	CSR_WRITE_4(sc, IWI_CSR_RST, CSR_READ_4(sc, IWI_CSR_RST) |
	    IWI_RST_STOP_MASTER);
	for (ntries = 0; ntries < 5; ntries++) {
		if (CSR_READ_4(sc, IWI_CSR_RST) & IWI_RST_MASTER_DISABLED)
			break;
		DELAY(10);
	}
	if (ntries == 5) {
		printf("%s: timeout waiting for master\n", sc->sc_dev.dv_xname);
		return EIO;
	}

	MEM_WRITE_4(sc, 0x3000e0, 0x80000000);
	DELAY(5000);
	CSR_WRITE_4(sc, IWI_CSR_RST, CSR_READ_4(sc, IWI_CSR_RST) &
	    ~IWI_RST_PRINCETON_RESET);
	DELAY(5000);
	MEM_WRITE_4(sc, 0x3000e0, 0);
	DELAY(1000);
	MEM_WRITE_4(sc, 0x300004, 1);
	DELAY(1000);
	MEM_WRITE_4(sc, 0x300004, 0);
	DELAY(1000);
	MEM_WRITE_1(sc, 0x200000, 0x00);
	MEM_WRITE_1(sc, 0x200000, 0x40);

	/* Adapter is buggy, we must set the address for each word */
	for (w = uc; size > 0; w++, size -= 2)
		MEM_WRITE_2(sc, 0x200010, *w);

	MEM_WRITE_1(sc, 0x200000, 0x00);
	MEM_WRITE_1(sc, 0x200000, 0x80);

	/* Wait until we get a response in the uc queue */
	for (ntries = 0; ntries < 100; ntries++) {
		if (MEM_READ_1(sc, 0x200000) & 1)
			break;
		DELAY(1000);
	}
	if (ntries == 100) {
		printf("%s: timeout waiting for ucode to initialize\n",
		    sc->sc_dev.dv_xname);
		return EIO;
	}

	/* Empty the uc queue or the firmware will not initialize properly */
	for (i = 0; i < 7; i++)
		MEM_READ_4(sc, 0x200004);

	MEM_WRITE_1(sc, 0x200000, 0x00);

	return 0;
}

/* macro to handle unaligned little endian data in firmware image */
#define GETLE32(p) ((p)[0] | (p)[1] << 8 | (p)[2] << 16 | (p)[3] << 24)
int
iwi_load_firmware(struct iwi_softc *sc, void *fw, int size)
{
	bus_dmamap_t map;
	bus_dma_segment_t seg;
	caddr_t virtaddr;
	u_char *p, *end;
	u_int32_t sentinel, ctl, src, dst, sum, len, mlen;
	int ntries, nsegs, error = 0;

	/* Allocate DMA memory for storing firmware image */
	error = bus_dmamap_create(sc->sc_dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT, &map);
	if (error != 0) {
		printf("%s: could not create fw dma map\n",
		    sc->sc_dev.dv_xname);
		goto fail1;
	}

	/*
	 * We cannot map fw directly because of some hardware constraints on
	 * the mapping address.
	 */
	error = bus_dmamem_alloc(sc->sc_dmat, size, PAGE_SIZE, 0, &seg, 1,
	    &nsegs, BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could allocate fw dma memory\n",
		    sc->sc_dev.dv_xname);
		goto fail2;
	}

	error = bus_dmamem_map(sc->sc_dmat, &seg, nsegs, size, &virtaddr,
	    BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not map fw dma memory\n",
		    sc->sc_dev.dv_xname);
		goto fail3;
	}

	error = bus_dmamap_load(sc->sc_dmat, map, virtaddr, size, NULL,
	    BUS_DMA_NOWAIT);
	if (error != 0) {
		printf("%s: could not load fw dma map\n", sc->sc_dev.dv_xname);
		goto fail4;
	}

        /* Copy firmware image to DMA memory */
        bcopy(fw, virtaddr, size);

	/* Make sure the adapter will get up-to-date values */
	bus_dmamap_sync(sc->sc_dmat, map, 0, size, BUS_DMASYNC_PREWRITE);

	/* Tell the adapter where the command blocks are stored */
	MEM_WRITE_4(sc, 0x3000a0, 0x27000);

	/*
	 * Store command blocks into adapter's internal memory using register
	 * indirections. The adapter will read the firmware image through DMA
	 * using information stored in command blocks.
	 */
	src = map->dm_segs[0].ds_addr;
	p = virtaddr;
	end = p + size;
	CSR_WRITE_4(sc, IWI_CSR_AUTOINC_ADDR, 0x27000);

	while (p < end) {
		dst = GETLE32(p); p += 4; src += 4;
		len = GETLE32(p); p += 4; src += 4;
		p += len;

		while (len > 0) {
			mlen = min(len, IWI_CB_MAXDATALEN);

			ctl = IWI_CB_DEFAULT_CTL | mlen;
			sum = ctl ^ src ^ dst;

			/* Write a command block */
			CSR_WRITE_4(sc, IWI_CSR_AUTOINC_DATA, ctl);
			CSR_WRITE_4(sc, IWI_CSR_AUTOINC_DATA, src);
			CSR_WRITE_4(sc, IWI_CSR_AUTOINC_DATA, dst);
			CSR_WRITE_4(sc, IWI_CSR_AUTOINC_DATA, sum);

			src += mlen;
			dst += mlen;
			len -= mlen;
		}
	}

	/* Write a fictive final command block (sentinel) */
	sentinel = CSR_READ_4(sc, IWI_CSR_AUTOINC_ADDR);
	CSR_WRITE_4(sc, IWI_CSR_AUTOINC_DATA, 0);

	CSR_WRITE_4(sc, IWI_CSR_RST, CSR_READ_4(sc, IWI_CSR_RST) &
	    ~(IWI_RST_MASTER_DISABLED | IWI_RST_STOP_MASTER));

	/* Tell the adapter to start processing command blocks */
	MEM_WRITE_4(sc, 0x3000a4, 0x540100);

	/* Wait until the adapter has processed all command blocks */
	for (ntries = 0; ntries < 400; ntries++) {
		if (MEM_READ_4(sc, 0x3000d0) >= sentinel)
			break;
		DELAY(100);
	}
	if (ntries == 400) {
		printf("%s: timeout processing cb\n", sc->sc_dev.dv_xname);
		error = EIO;
		goto fail5;
	}

	/* We're done with command blocks processing */
	MEM_WRITE_4(sc, 0x3000a4, 0x540c00);

	/* Allow interrupts so we know when the firmware is inited */
	CSR_WRITE_4(sc, IWI_CSR_INTR_MASK, IWI_INTR_MASK);

	/* Tell the adapter to initialize the firmware */
	CSR_WRITE_4(sc, IWI_CSR_RST, 0);
	CSR_WRITE_4(sc, IWI_CSR_CTL, CSR_READ_4(sc, IWI_CSR_CTL) |
	    IWI_CTL_ALLOW_STANDBY);

	/* Wait at most 1 s for firmware initialization to complete */
	if ((error = tsleep(sc, 0, "iwiinit", hz)) != 0) {
		printf("%s: timeout waiting for firmware initialization to "
		    "complete\n", sc->sc_dev.dv_xname);
		goto fail5;
	}

fail5:	bus_dmamap_unload(sc->sc_dmat, map);
fail4:	bus_dmamem_unmap(sc->sc_dmat, virtaddr, size);
fail3:	bus_dmamem_free(sc->sc_dmat, &seg, 1);
fail2:	bus_dmamap_destroy(sc->sc_dmat, map);
	
fail1:	return error;
}

/*
 * Store firmware into kernel memory so we can download it when we need to,
 * e.g when the adapter wakes up from suspend mode.
 */
int
iwi_cache_firmware(struct iwi_softc *sc, void *data)
{
	struct iwi_firmware *kfw = &sc->fw;
	struct iwi_firmware ufw;
	int error;

	if (sc->flags & IWI_FLAG_FW_CACHED)
		iwi_free_firmware(sc);

	if ((error = copyin(data, &ufw, sizeof ufw)) != 0)
		goto fail1;

	kfw->boot_size  = ufw.boot_size;
	kfw->ucode_size = ufw.ucode_size;
	kfw->main_size  = ufw.main_size;

	kfw->boot = malloc(kfw->boot_size, M_DEVBUF, M_NOWAIT);
	if (kfw->boot == NULL) {
		error = ENOMEM;
		goto fail1;
	}

	kfw->ucode = malloc(kfw->ucode_size, M_DEVBUF, M_NOWAIT);
	if (kfw->ucode == NULL) {
		error = ENOMEM;
		goto fail2;
	}

	kfw->main = malloc(kfw->main_size, M_DEVBUF, M_NOWAIT);
	if (kfw->main == NULL) {
		error = ENOMEM;
		goto fail3;
	}

	if ((error = copyin(ufw.boot, kfw->boot, kfw->boot_size)) != 0)
		goto fail4;

	if ((error = copyin(ufw.ucode, kfw->ucode, kfw->ucode_size)) != 0)
		goto fail4;

	if ((error = copyin(ufw.main, kfw->main, kfw->main_size)) != 0)
		goto fail4;

	DPRINTF(("Firmware cached: boot %u, ucode %u, main %u\n",
	    kfw->boot_size, kfw->ucode_size, kfw->main_size));

	sc->flags |= IWI_FLAG_FW_CACHED;

	return 0;

fail4:	free(kfw->boot, M_DEVBUF);
fail3:	free(kfw->ucode, M_DEVBUF);
fail2:	free(kfw->main, M_DEVBUF);
fail1:
	return error;
}

void
iwi_free_firmware(struct iwi_softc *sc)
{
	free(sc->fw.boot, M_DEVBUF);
	free(sc->fw.ucode, M_DEVBUF);
	free(sc->fw.main, M_DEVBUF);

	sc->flags &= ~IWI_FLAG_FW_CACHED;
}

int
iwi_config(struct iwi_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ifnet *ifp = &ic->ic_if;
	struct iwi_configuration config;
	struct iwi_rateset rs;
	struct iwi_txpower power;
	struct ieee80211_wepkey *k;
	struct iwi_wep_key wepkey;
	u_int32_t data;
	int error, i;

	DPRINTF(("Setting adapter MAC to %s\n", ether_sprintf(ic->ic_myaddr)));
	IEEE80211_ADDR_COPY(((struct arpcom *)ifp)->ac_enaddr, ic->ic_myaddr);
	IEEE80211_ADDR_COPY(LLADDR(ifp->if_sadl), ic->ic_myaddr);
	error = iwi_cmd(sc, IWI_CMD_SET_MAC_ADDRESS, ic->ic_myaddr,
	    IEEE80211_ADDR_LEN, 0);
	if (error != 0)
		return error;

	bzero(&config, sizeof config);
	config.bluetooth_coexistence = 1;
	config.enable_multicast = 1;
	config.bg_autodetect = 1;
	config.pass_noise = 1;
	DPRINTF(("Configuring adapter\n"));
	error = iwi_cmd(sc, IWI_CMD_SET_CONFIGURATION, &config, sizeof config,
	    0);
	if (error != 0)
		return error;

	data = htole32(IWI_POWER_MODE_CAM);
	DPRINTF(("Setting adapter power mode to %u\n", data));
	error = iwi_cmd(sc, IWI_CMD_SET_POWER_MODE, &data, sizeof data, 0);
	if (error != 0)
		return error;

	data = htole32(ic->ic_rtsthreshold);
	DPRINTF(("Setting adapter RTS threshold to %u\n", letoh32(data)));
	error = iwi_cmd(sc, IWI_CMD_SET_RTS_THRESHOLD, &data, sizeof data, 0);
	if (error != 0)
		return error;

	power.mode = IWI_MODE_11B;
	power.nchan = 11;
	for (i = 0; i < 11; i++) {
		power.chan[i].chan = i + 1;
		power.chan[i].power = ic->ic_txpower / IWI_TXPOWER_RATIO;
	}
	DPRINTF(("Setting .11b channels tx power\n"));
	error = iwi_cmd(sc, IWI_CMD_SET_TX_POWER, &power, sizeof power, 0);
	if (error != 0)
		return error;

	power.mode = IWI_MODE_11G;
	DPRINTF(("Setting .11g channels tx power\n"));
	error = iwi_cmd(sc, IWI_CMD_SET_TX_POWER, &power, sizeof power, 0);
	if (error != 0)
		return error;

	rs.mode = IWI_MODE_11G;
	rs.type = IWI_RATESET_TYPE_SUPPORTED;
	rs.nrates = ic->ic_sup_rates[IEEE80211_MODE_11G].rs_nrates;
	bcopy(ic->ic_sup_rates[IEEE80211_MODE_11G].rs_rates, rs.rates,
	    rs.nrates);
	DPRINTF(("Setting supported rates\n"));
	error = iwi_cmd(sc, IWI_CMD_SET_RATES, &rs, sizeof rs, 0);
	if (error != 0)
		return error;

	data = htole32(arc4random());
	DPRINTF(("Setting initialization vector to %u\n", data));
	error = iwi_cmd(sc, IWI_CMD_SET_IV, &data, sizeof data, 0);
	if (error != 0)
		return error;

	if (ic->ic_flags & IEEE80211_F_WEPON) {
		k = ic->ic_nw_keys;
		for (i = 0; i < IEEE80211_WEP_NKID; i++, k++) {
			wepkey.cmd = IWI_WEP_KEY_CMD_SETKEY;
			wepkey.idx = i;
			wepkey.len = k->wk_len;
			bzero(wepkey.key, sizeof wepkey.key);
			bcopy(k->wk_key, wepkey.key, k->wk_len);
			DPRINTF(("Setting wep key index %d len %d\n",
			    wepkey.idx, wepkey.len));
			error = iwi_cmd(sc, IWI_CMD_SET_WEP_KEY, &wepkey,
			    sizeof wepkey, 0);
			if (error != 0)
				return error;
		}
	}

	/* Enable adapter */
	DPRINTF(("Enabling adapter\n"));
	return iwi_cmd(sc, IWI_CMD_ENABLE, NULL, 0, 0);
}

int
iwi_scan(struct iwi_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_channel *chan;
	struct iwi_scan scan;
	int count;

	memset(&scan, 0, sizeof scan);
	scan.type = IWI_SCAN_TYPE_BROADCAST;
	scan.intval = htole16(20);

	count = 0;
	for (chan = ic->ic_channels;
	     chan <= &ic->ic_channels[IEEE80211_CHAN_MAX];
	     chan++) {
		if (IEEE80211_IS_CHAN_2GHZ(chan) &&
		    isset(ic->ic_chan_active, ieee80211_chan2ieee(ic, chan)))
			scan.channels[++count] = ieee80211_chan2ieee(ic, chan);
	}
	scan.channels[0] = IWI_CHAN_2GHZ | count;

	DPRINTF(("Start scanning\n"));
	return iwi_cmd(sc, IWI_CMD_SCAN, &scan, sizeof scan, IWI_ASYNC_CMD);
}

int
iwi_auth_and_assoc(struct iwi_softc *sc)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni = ic->ic_bss;
	struct iwi_associate assoc;
	struct iwi_rateset rs;
	u_int32_t data;
	u_int16_t capinfo;
	int error;

#ifdef IWI_DEBUG
	if (iwi_debug > 0) {
		printf("Setting adapter ESSID to ");
		ieee80211_print_essid(ni->ni_essid, ni->ni_esslen);
		printf("\n");
	}
#endif
	error = iwi_cmd(sc, IWI_CMD_SET_ESSID, ni->ni_essid, ni->ni_esslen,
	    IWI_ASYNC_CMD);
	if (error != 0)
		return error;

	/* the rate set has already been "negociated" */
	rs.mode = IWI_MODE_11G;
	rs.type = IWI_RATESET_TYPE_NEGOCIATED;
	rs.nrates = ni->ni_rates.rs_nrates;
	bcopy(ni->ni_rates.rs_rates, rs.rates, rs.nrates);
	DPRINTF(("Setting negociated rates (%u)\n", rs.nrates));
	error = iwi_cmd(sc, IWI_CMD_SET_RATES, &rs, sizeof rs, IWI_ASYNC_CMD);
	if (error != 0)
		return error;

	data = htole32(IWI_RSSI2SENS(ni->ni_rssi));
	DPRINTF(("Setting sensitivity to %d\n", letoh32(data)));
	error = iwi_cmd(sc, IWI_CMD_SET_SENSITIVITY, &data, sizeof data,
	    IWI_ASYNC_CMD);
	if (error != 0)
		return error;

	bzero(&assoc, sizeof assoc);
	assoc.mode = IWI_MODE_11G;
	assoc.chan = ieee80211_chan2ieee(ic, ni->ni_chan);
	if (sc->authmode == IEEE80211_AUTH_SHARED)
		assoc.auth = IWI_AUTH_SHARED | ic->ic_wep_txkey;
	bcopy(ni->ni_tstamp, assoc.tstamp, 8);

	capinfo = 0;
	if (ic->ic_opmode == IEEE80211_M_IBSS)
		capinfo |= IEEE80211_CAPINFO_IBSS;
	else	/* IEEE80211_M_STA */
		capinfo |= IEEE80211_CAPINFO_ESS;
	if (ic->ic_flags & IEEE80211_F_WEPON)
		capinfo |= IEEE80211_CAPINFO_PRIVACY;
	if ((ic->ic_flags & IEEE80211_F_SHPREAMBLE) &&
	    IEEE80211_IS_CHAN_2GHZ(ni->ni_chan))
		capinfo |= IEEE80211_CAPINFO_SHORT_PREAMBLE;
	if (ic->ic_flags & IEEE80211_F_SHSLOT)
		capinfo |= IEEE80211_CAPINFO_SHORT_SLOTTIME;

	assoc.capinfo = htole16(capinfo);
	assoc.lintval = htole16(ic->ic_lintval);
	assoc.intval = htole16(ni->ni_intval);
	IEEE80211_ADDR_COPY(assoc.bssid, ni->ni_bssid);
	IEEE80211_ADDR_COPY(assoc.dst, ni->ni_bssid);
	DPRINTF(("Trying to associate to %s channel %u auth %u\n",
	    ether_sprintf(assoc.bssid), assoc.chan, assoc.auth));
	return iwi_cmd(sc, IWI_CMD_ASSOCIATE, &assoc, sizeof assoc,
	    IWI_ASYNC_CMD);
}

int
iwi_init(struct ifnet *ifp)
{
	struct iwi_softc *sc = ifp->if_softc;
	struct iwi_firmware *fw = &sc->fw;
	int error;

	/* exit immediately if firmware has not been ioctl'd */
	if (!(sc->flags & IWI_FLAG_FW_CACHED)) {
		ifp->if_flags &= ~IFF_UP;
		return EIO;
	}

	if ((error = iwi_reset(sc)) != 0) {
		printf("%s: could not reset adapter\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	if ((error = iwi_load_firmware(sc, fw->boot, fw->boot_size)) != 0) {
		printf("%s: could not load boot firmware\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	if ((error = iwi_load_ucode(sc, fw->ucode, fw->ucode_size)) != 0) {
		printf("%s: could not load microcode\n", sc->sc_dev.dv_xname);
		goto fail;
	}

	iwi_stop_master(sc);

	if ((error = iwi_init_queues(sc)) != 0) {
		printf("%s: could not initialize queues\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	if ((error = iwi_load_firmware(sc, fw->main, fw->main_size)) != 0) {
		printf("%s: could not load main firmware\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	sc->flags |= IWI_FLAG_FW_INITED;

	if ((error = iwi_config(sc)) != 0) {
		printf("%s: device configuration failed\n",
		    sc->sc_dev.dv_xname);
		goto fail;
	}

	ieee80211_begin_scan(ifp);

	ifp->if_flags &= ~IFF_OACTIVE;
	ifp->if_flags |= IFF_RUNNING;

	return 0;

fail:	iwi_stop(ifp, 0);

	return error;
}

void
iwi_stop(struct ifnet *ifp, int disable)
{
	struct iwi_softc *sc = ifp->if_softc;
	struct ieee80211com *ic = &sc->sc_ic;

	iwi_stop_master(sc);
	iwi_free_queues(sc);

	ifp->if_timer = 0;
	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);

	ieee80211_new_state(ic, IEEE80211_S_INIT, -1);
}

struct cfdriver iwi_cd = {
	0, "iwi", DV_IFNET
};

