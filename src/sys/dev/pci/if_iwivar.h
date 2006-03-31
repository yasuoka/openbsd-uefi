/*	$OpenBSD: if_iwivar.h,v 1.15 2006/03/31 17:18:37 pedro Exp $	*/

/*-
 * Copyright (c) 2004-2006
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

struct iwi_rx_radiotap_header {
	struct ieee80211_radiotap_header wr_ihdr;
	uint8_t		wr_flags;
	uint8_t		wr_rate;
	uint16_t	wr_chan_freq;
	uint16_t	wr_chan_flags;
	uint8_t		wr_antsignal;
	uint8_t		wr_antenna;
} __packed;

#define IWI_RX_RADIOTAP_PRESENT						\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_RATE) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL) |				\
	 (1 << IEEE80211_RADIOTAP_DB_ANTSIGNAL) |			\
	 (1 << IEEE80211_RADIOTAP_ANTENNA))

struct iwi_tx_radiotap_header {
	struct ieee80211_radiotap_header wt_ihdr;
	uint8_t		wt_flags;
	uint16_t	wt_chan_freq;
	uint16_t	wt_chan_flags;
} __packed;

#define IWI_TX_RADIOTAP_PRESENT						\
	((1 << IEEE80211_RADIOTAP_FLAGS) |				\
	 (1 << IEEE80211_RADIOTAP_CHANNEL))

struct iwi_softc {
	struct device		sc_dev;

	struct ieee80211com	sc_ic;
	int			(*sc_newstate)(struct ieee80211com *,
				    enum ieee80211_state, int);

	uint32_t		flags;
#define IWI_FLAG_FW_INITED	(1 << 0)

	bus_dma_tag_t		sc_dmat;

	struct iwi_tx_desc	*tx_desc;
	bus_dmamap_t		tx_ring_map;
	bus_dma_segment_t	tx_ring_seg;

	struct iwi_tx_buf {
		bus_dmamap_t		map;
		struct mbuf		*m;
		struct ieee80211_node	*ni;
	} tx_buf[IWI_TX_RING_SIZE];

	int			tx_cur;
	int			tx_old;
	int			tx_queued;

	struct iwi_cmd_desc	*cmd_desc;
	bus_dmamap_t		cmd_ring_map;
	bus_dma_segment_t	cmd_ring_seg;
	int			cmd_cur;

	struct iwi_rx_buf {
		bus_dmamap_t	map;
		struct mbuf	*m;
	} rx_buf[IWI_RX_RING_SIZE];

	int			rx_cur;

#define IWI_MAX_NODE	32
	uint8_t			sta[IWI_MAX_NODE][IEEE80211_ADDR_LEN];
	uint8_t			nsta;

	bus_space_tag_t		sc_st;
	bus_space_handle_t	sc_sh;
	void 			*sc_ih;
	pci_chipset_tag_t	sc_pct;
	pcitag_t		sc_pcitag;
	bus_size_t		sc_sz;

	int			authmode;

	int			sc_tx_timer;

	void			*powerhook;

#if NBPFILTER > 0
	caddr_t			sc_drvbpf;

	union {
		struct iwi_rx_radiotap_header th;
		uint8_t	pad[64];
	} sc_rxtapu;
#define sc_rxtap	sc_rxtapu.th
	int			sc_rxtap_len;

	union {
		struct iwi_tx_radiotap_header th;
		uint8_t	pad[64];
	} sc_txtapu;
#define sc_txtap	sc_txtapu.th
	int			sc_txtap_len;
#endif
};
