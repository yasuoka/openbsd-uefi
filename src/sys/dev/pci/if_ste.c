/*	$OpenBSD: if_ste.c,v 1.18 2003/01/15 06:31:24 art Exp $ */
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
 * $FreeBSD: src/sys/pci/if_ste.c,v 1.14 1999/12/07 20:14:42 wpaul Exp $
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
#include <sys/timeout.h>

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

#include <sys/device.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#define STE_USEIOSPACE

#include <dev/pci/if_stereg.h>

int ste_probe(struct device *, void *, void *);
void ste_attach(struct device *, struct device *, void *);
int ste_intr(void *);
void ste_shutdown(void *);
void ste_init(void *);
void ste_rxeof(struct ste_softc *);
void ste_txeoc(struct ste_softc *);
void ste_txeof(struct ste_softc *);
void ste_stats_update(void *);
void ste_stop(struct ste_softc *);
void ste_reset(struct ste_softc *);
int ste_ioctl(struct ifnet *, u_long, caddr_t);
int ste_encap(struct ste_softc *, struct ste_chain *,
					struct mbuf *);
void ste_start(struct ifnet *);
void ste_watchdog(struct ifnet *);
int ste_newbuf(struct ste_softc *,
					struct ste_chain_onefrag *,
					struct mbuf *);
int ste_ifmedia_upd(struct ifnet *);
void ste_ifmedia_sts(struct ifnet *, struct ifmediareq *);

void ste_mii_sync(struct ste_softc *);
void ste_mii_send(struct ste_softc *, u_int32_t, int);
int ste_mii_readreg(struct ste_softc *,
					struct ste_mii_frame *);
int ste_mii_writereg(struct ste_softc *,
					struct ste_mii_frame *);
int ste_miibus_readreg(struct device *, int, int);
void ste_miibus_writereg(struct device *, int, int, int);
void ste_miibus_statchg(struct device *);

int ste_eeprom_wait(struct ste_softc *);
int ste_read_eeprom(struct ste_softc *, caddr_t, int,
							int, int);
void ste_wait(struct ste_softc *);
u_int8_t ste_calchash(caddr_t);
void ste_setmulti(struct ste_softc *);
int ste_init_rx_list(struct ste_softc *);
void ste_init_tx_list(struct ste_softc *);

#define STE_SETBIT4(sc, reg, x)				\
	CSR_WRITE_4(sc, reg, CSR_READ_4(sc, reg) | x)

#define STE_CLRBIT4(sc, reg, x)				\
	CSR_WRITE_4(sc, reg, CSR_READ_4(sc, reg) & ~x)

#define STE_SETBIT2(sc, reg, x)				\
	CSR_WRITE_2(sc, reg, CSR_READ_2(sc, reg) | x)

#define STE_CLRBIT2(sc, reg, x)				\
	CSR_WRITE_2(sc, reg, CSR_READ_2(sc, reg) & ~x)

#define STE_SETBIT1(sc, reg, x)				\
	CSR_WRITE_1(sc, reg, CSR_READ_1(sc, reg) | x)

#define STE_CLRBIT1(sc, reg, x)				\
	CSR_WRITE_1(sc, reg, CSR_READ_1(sc, reg) & ~x)


#define MII_SET(x)		STE_SETBIT1(sc, STE_PHYCTL, x)
#define MII_CLR(x)		STE_CLRBIT1(sc, STE_PHYCTL, x) 

/*
 * Sync the PHYs by setting data bit and strobing the clock 32 times.
 */
void ste_mii_sync(sc)
	struct ste_softc		*sc;
{
	register int		i;

	MII_SET(STE_PHYCTL_MDIR|STE_PHYCTL_MDATA);

	for (i = 0; i < 32; i++) {
		MII_SET(STE_PHYCTL_MCLK);
		DELAY(1);
		MII_CLR(STE_PHYCTL_MCLK);
		DELAY(1);
	}

	return;
}

/*
 * Clock a series of bits through the MII.
 */
void ste_mii_send(sc, bits, cnt)
	struct ste_softc		*sc;
	u_int32_t		bits;
	int			cnt;
{
	int			i;

	MII_CLR(STE_PHYCTL_MCLK);

	for (i = (0x1 << (cnt - 1)); i; i >>= 1) {
                if (bits & i) {
			MII_SET(STE_PHYCTL_MDATA);
                } else {
			MII_CLR(STE_PHYCTL_MDATA);
                }
		DELAY(1);
		MII_CLR(STE_PHYCTL_MCLK);
		DELAY(1);
		MII_SET(STE_PHYCTL_MCLK);
	}
}

/*
 * Read an PHY register through the MII.
 */
int ste_mii_readreg(sc, frame)
	struct ste_softc		*sc;
	struct ste_mii_frame	*frame;
	
{
	int			i, ack, s;

	s = splimp();

	/*
	 * Set up frame for RX.
	 */
	frame->mii_stdelim = STE_MII_STARTDELIM;
	frame->mii_opcode = STE_MII_READOP;
	frame->mii_turnaround = 0;
	frame->mii_data = 0;
	
	CSR_WRITE_2(sc, STE_PHYCTL, 0);
	/*
 	 * Turn on data xmit.
	 */
	MII_SET(STE_PHYCTL_MDIR);

	ste_mii_sync(sc);

	/*
	 * Send command/address info.
	 */
	ste_mii_send(sc, frame->mii_stdelim, 2);
	ste_mii_send(sc, frame->mii_opcode, 2);
	ste_mii_send(sc, frame->mii_phyaddr, 5);
	ste_mii_send(sc, frame->mii_regaddr, 5);

	/* Turn off xmit. */
	MII_CLR(STE_PHYCTL_MDIR);

	/* Idle bit */
	MII_CLR((STE_PHYCTL_MCLK|STE_PHYCTL_MDATA));
	DELAY(1);
	MII_SET(STE_PHYCTL_MCLK);
	DELAY(1);

	/* Check for ack */
	MII_CLR(STE_PHYCTL_MCLK);
	DELAY(1);
	MII_SET(STE_PHYCTL_MCLK);
	DELAY(1);
	ack = CSR_READ_2(sc, STE_PHYCTL) & STE_PHYCTL_MDATA;

	/*
	 * Now try reading data bits. If the ack failed, we still
	 * need to clock through 16 cycles to keep the PHY(s) in sync.
	 */
	if (ack) {
		for(i = 0; i < 16; i++) {
			MII_CLR(STE_PHYCTL_MCLK);
			DELAY(1);
			MII_SET(STE_PHYCTL_MCLK);
			DELAY(1);
		}
		goto fail;
	}

	for (i = 0x8000; i; i >>= 1) {
		MII_CLR(STE_PHYCTL_MCLK);
		DELAY(1);
		if (!ack) {
			if (CSR_READ_2(sc, STE_PHYCTL) & STE_PHYCTL_MDATA)
				frame->mii_data |= i;
			DELAY(1);
		}
		MII_SET(STE_PHYCTL_MCLK);
		DELAY(1);
	}

fail:

	MII_CLR(STE_PHYCTL_MCLK);
	DELAY(1);
	MII_SET(STE_PHYCTL_MCLK);
	DELAY(1);

	splx(s);

	if (ack)
		return(1);
	return(0);
}

/*
 * Write to a PHY register through the MII.
 */
int ste_mii_writereg(sc, frame)
	struct ste_softc		*sc;
	struct ste_mii_frame	*frame;
	
{
	int			s;

	s = splimp();
	/*
	 * Set up frame for TX.
	 */

	frame->mii_stdelim = STE_MII_STARTDELIM;
	frame->mii_opcode = STE_MII_WRITEOP;
	frame->mii_turnaround = STE_MII_TURNAROUND;
	
	/*
 	 * Turn on data output.
	 */
	MII_SET(STE_PHYCTL_MDIR);

	ste_mii_sync(sc);

	ste_mii_send(sc, frame->mii_stdelim, 2);
	ste_mii_send(sc, frame->mii_opcode, 2);
	ste_mii_send(sc, frame->mii_phyaddr, 5);
	ste_mii_send(sc, frame->mii_regaddr, 5);
	ste_mii_send(sc, frame->mii_turnaround, 2);
	ste_mii_send(sc, frame->mii_data, 16);

	/* Idle bit. */
	MII_SET(STE_PHYCTL_MCLK);
	DELAY(1);
	MII_CLR(STE_PHYCTL_MCLK);
	DELAY(1);

	/*
	 * Turn off xmit.
	 */
	MII_CLR(STE_PHYCTL_MDIR);

	splx(s);

	return(0);
}

int ste_miibus_readreg(self, phy, reg)
	struct device		*self;
	int			phy, reg;
{
	struct ste_softc	*sc = (struct ste_softc *)self;
	struct ste_mii_frame	frame;

	bzero((char *)&frame, sizeof(frame));

	frame.mii_phyaddr = phy;
	frame.mii_regaddr = reg;
	ste_mii_readreg(sc, &frame);

	return(frame.mii_data);
}

void ste_miibus_writereg(self, phy, reg, data)
	struct device		*self;
	int			phy, reg, data;
{
	struct ste_softc	*sc = (struct ste_softc *)self;
	struct ste_mii_frame	frame;

	bzero((char *)&frame, sizeof(frame));

	frame.mii_phyaddr = phy;
	frame.mii_regaddr = reg;
	frame.mii_data = data;

	ste_mii_writereg(sc, &frame);

	return;
}

void ste_miibus_statchg(self)
	struct device		*self;
{
	struct ste_softc	*sc = (struct ste_softc *)self;
	struct mii_data		*mii;

	mii = &sc->sc_mii;

	if ((mii->mii_media_active & IFM_GMASK) == IFM_FDX) {
		STE_SETBIT2(sc, STE_MACCTL0, STE_MACCTL0_FULLDUPLEX);
	} else {
		STE_CLRBIT2(sc, STE_MACCTL0, STE_MACCTL0_FULLDUPLEX);
	}

	return;
}
 
int ste_ifmedia_upd(ifp)
	struct ifnet		*ifp;
{
	struct ste_softc	*sc;
	struct mii_data		*mii;

	sc = ifp->if_softc;
	mii = &sc->sc_mii;
	sc->ste_link = 0;
	if (mii->mii_instance) {
		struct mii_softc	*miisc;
		for (miisc = LIST_FIRST(&mii->mii_phys); miisc != NULL;
		    miisc = LIST_NEXT(miisc, mii_list))
			mii_phy_reset(miisc);
	}
	mii_mediachg(mii);

	return(0);
}

void ste_ifmedia_sts(ifp, ifmr)
	struct ifnet		*ifp;
	struct ifmediareq	*ifmr;
{
	struct ste_softc	*sc;
	struct mii_data		*mii;

	sc = ifp->if_softc;
	mii = &sc->sc_mii;

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;

	return;
}

void ste_wait(sc)
	struct ste_softc		*sc;
{
	register int		i;

	for (i = 0; i < STE_TIMEOUT; i++) {
		if (!(CSR_READ_4(sc, STE_DMACTL) & STE_DMACTL_DMA_HALTINPROG))
			break;
	}

	if (i == STE_TIMEOUT)
		printf("ste%d: command never completed!\n", sc->ste_unit);

	return;
}

/*
 * The EEPROM is slow: give it time to come ready after issuing
 * it a command.
 */
int ste_eeprom_wait(sc)
	struct ste_softc		*sc;
{
	int			i;

	DELAY(1000);

	for (i = 0; i < 100; i++) {
		if (CSR_READ_2(sc, STE_EEPROM_CTL) & STE_EECTL_BUSY)
			DELAY(1000);
		else
			break;
	}

	if (i == 100) {
		printf("ste%d: eeprom failed to come ready\n", sc->ste_unit);
		return(1);
	}

	return(0);
}

/*
 * Read a sequence of words from the EEPROM. Note that ethernet address
 * data is stored in the EEPROM in network byte order.
 */
int ste_read_eeprom(sc, dest, off, cnt, swap)
	struct ste_softc		*sc;
	caddr_t			dest;
	int			off;
	int			cnt;
	int			swap;
{
	int			err = 0, i;
	u_int16_t		word = 0, *ptr;

	if (ste_eeprom_wait(sc))
		return(1);

	for (i = 0; i < cnt; i++) {
		CSR_WRITE_2(sc, STE_EEPROM_CTL, STE_EEOPCODE_READ | (off + i));
		err = ste_eeprom_wait(sc);
		if (err)
			break;
		word = CSR_READ_2(sc, STE_EEPROM_DATA);
		ptr = (u_int16_t *)(dest + (i * 2));
		if (swap)
			*ptr = ntohs(word);
		else
			*ptr = word;	
	}

	return(err ? 1 : 0);
}

u_int8_t ste_calchash(addr)
	caddr_t			addr;
{

	u_int32_t		crc, carry;
	int			i, j;
	u_int8_t		c;

	/* Compute CRC for the address value. */
	crc = 0xFFFFFFFF; /* initial value */

	for (i = 0; i < 6; i++) {
		c = *(addr + i);
		for (j = 0; j < 8; j++) {
			carry = ((crc & 0x80000000) ? 1 : 0) ^ (c & 0x01);
			crc <<= 1;
			c >>= 1;
			if (carry)
				crc = (crc ^ 0x04c11db6) | carry;
		}
	}

	/* return the filter bit position */
	return(crc & 0x0000003F);
}

void ste_setmulti(sc)
	struct ste_softc	*sc;
{
	struct ifnet		*ifp;
	struct arpcom		*ac = &sc->arpcom;
	struct ether_multi	*enm;
	struct ether_multistep	step;
	int			h = 0;
	u_int32_t		hashes[2] = { 0, 0 };

	ifp = &sc->arpcom.ac_if;
	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		STE_SETBIT1(sc, STE_RX_MODE, STE_RXMODE_ALLMULTI);
		STE_CLRBIT1(sc, STE_RX_MODE, STE_RXMODE_MULTIHASH);
		return;
	}

	/* first, zot all the existing hash bits */
	CSR_WRITE_4(sc, STE_MAR0, 0);
	CSR_WRITE_4(sc, STE_MAR1, 0);

	/* now program new ones */
	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		h = ste_calchash(enm->enm_addrlo);
		if (h < 32)
			hashes[0] |= (1 << h);
		else
			hashes[1] |= (1 << (h - 32));
		ETHER_NEXT_MULTI(step, enm);
	}

	CSR_WRITE_4(sc, STE_MAR0, hashes[0]);
	CSR_WRITE_4(sc, STE_MAR1, hashes[1]);
	STE_CLRBIT1(sc, STE_RX_MODE, STE_RXMODE_ALLMULTI);
	STE_SETBIT1(sc, STE_RX_MODE, STE_RXMODE_MULTIHASH);

	return;
}

int ste_intr(xsc)
	void			*xsc;
{
	struct ste_softc	*sc;
	struct ifnet		*ifp;
	u_int16_t		status;
	int			claimed = 0;

	sc = xsc;
	ifp = &sc->arpcom.ac_if;

	/* See if this is really our interrupt. */
	if (!(CSR_READ_2(sc, STE_ISR) & STE_ISR_INTLATCH))
		return claimed;

	for (;;) {
		status = CSR_READ_2(sc, STE_ISR_ACK);

		if (!(status & STE_INTRS))
			break;

		claimed = 1;

		if (status & STE_ISR_RX_DMADONE)
			ste_rxeof(sc);

		if (status & STE_ISR_TX_DMADONE)
			ste_txeof(sc);

		if (status & STE_ISR_TX_DONE)
			ste_txeoc(sc);

		if (status & STE_ISR_STATS_OFLOW) {
			timeout_del(&sc->sc_stats_tmo);
			ste_stats_update(sc);
		}

		if (status & STE_ISR_HOSTERR) {
			ste_reset(sc);
			ste_init(sc);
		}
	}

	/* Re-enable interrupts */
	CSR_WRITE_2(sc, STE_IMR, STE_INTRS);

	if (IFQ_IS_EMPTY(&ifp->if_snd) == 0)
		ste_start(ifp);

	return claimed;
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
void ste_rxeof(sc)
	struct ste_softc		*sc;
{
        struct mbuf		*m;
        struct ifnet		*ifp;
	struct ste_chain_onefrag	*cur_rx;
	int			total_len = 0;
	u_int32_t		rxstat;

	ifp = &sc->arpcom.ac_if;

again:

	while((rxstat = sc->ste_cdata.ste_rx_head->ste_ptr->ste_status)) {
		cur_rx = sc->ste_cdata.ste_rx_head;
		sc->ste_cdata.ste_rx_head = cur_rx->ste_next;

		/*
		 * If an error occurs, update stats, clear the
		 * status word and leave the mbuf cluster in place:
		 * it should simply get re-used next time this descriptor
	 	 * comes up in the ring.
		 */
		if (rxstat & STE_RXSTAT_FRAME_ERR) {
			ifp->if_ierrors++;
			cur_rx->ste_ptr->ste_status = 0;
			continue;
		}

		/*
		 * If there error bit was not set, the upload complete
		 * bit should be set which means we have a valid packet.
		 * If not, something truly strange has happened.
		 */
		if (!(rxstat & STE_RXSTAT_DMADONE)) {
			printf("ste%d: bad receive status -- packet dropped",
							sc->ste_unit);
			ifp->if_ierrors++;
			cur_rx->ste_ptr->ste_status = 0;
			continue;
		}

		/* No errors; receive the packet. */	
		m = cur_rx->ste_mbuf;
		total_len = cur_rx->ste_ptr->ste_status & STE_RXSTAT_FRAMELEN;

		/*
		 * Try to conjure up a new mbuf cluster. If that
		 * fails, it means we have an out of memory condition and
		 * should leave the buffer in place and continue. This will
		 * result in a lost packet, but there's little else we
		 * can do in this situation.
		 */
		if (ste_newbuf(sc, cur_rx, NULL) == ENOBUFS) {
			ifp->if_ierrors++;
			cur_rx->ste_ptr->ste_status = 0;
			continue;
		}

		ifp->if_ipackets++;
		m->m_pkthdr.rcvif = ifp;
		m->m_pkthdr.len = m->m_len = total_len;

#if NBPFILTER > 0
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m);
#endif

		/* pass it on. */
		ether_input_mbuf(ifp, m);
	}

	/*
	 * Handle the 'end of channel' condition. When the upload
	 * engine hits the end of the RX ring, it will stall. This
	 * is our cue to flush the RX ring, reload the uplist pointer
	 * register and unstall the engine.
	 * XXX This is actually a little goofy. With the ThunderLAN
	 * chip, you get an interrupt when the receiver hits the end
	 * of the receive ring, which tells you exactly when you
	 * you need to reload the ring pointer. Here we have to
	 * fake it. I'm mad at myself for not being clever enough
	 * to avoid the use of a goto here.
	 */
	if (CSR_READ_4(sc, STE_RX_DMALIST_PTR) == 0 ||
		CSR_READ_4(sc, STE_DMACTL) & STE_DMACTL_RXDMA_STOPPED) {
		STE_SETBIT4(sc, STE_DMACTL, STE_DMACTL_RXDMA_STALL);
		ste_wait(sc);
		CSR_WRITE_4(sc, STE_RX_DMALIST_PTR,
			vtophys(&sc->ste_ldata->ste_rx_list[0]));
		sc->ste_cdata.ste_rx_head = &sc->ste_cdata.ste_rx_chain[0];
		STE_SETBIT4(sc, STE_DMACTL, STE_DMACTL_RXDMA_UNSTALL);
		goto again;
	}

	return;
}

void ste_txeoc(sc)
	struct ste_softc	*sc;
{
	u_int8_t		txstat;
	struct ifnet		*ifp;

	ifp = &sc->arpcom.ac_if;

	while ((txstat = CSR_READ_1(sc, STE_TX_STATUS)) &
	    STE_TXSTATUS_TXDONE) {
		if (txstat & STE_TXSTATUS_UNDERRUN ||
		    txstat & STE_TXSTATUS_EXCESSCOLLS ||
		    txstat & STE_TXSTATUS_RECLAIMERR) {
			ifp->if_oerrors++;
			printf("ste%d: transmission error: %x\n",
			    sc->ste_unit, txstat);

			ste_reset(sc);
			ste_init(sc);

			if (txstat & STE_TXSTATUS_UNDERRUN &&
			    sc->ste_tx_thresh < STE_PACKET_SIZE) {
				sc->ste_tx_thresh += STE_MIN_FRAMELEN;
				printf("ste%d: tx underrun, increasing tx"
				    " start threshold to %d bytes\n",
				    sc->ste_unit, sc->ste_tx_thresh);
			}
			CSR_WRITE_2(sc, STE_TX_STARTTHRESH, sc->ste_tx_thresh);
			CSR_WRITE_2(sc, STE_TX_RECLAIM_THRESH,
			    (STE_PACKET_SIZE >> 4));
		}
		ste_init(sc);
		CSR_WRITE_2(sc, STE_TX_STATUS, txstat);
	}

	return;
}

void ste_txeof(sc)
	struct ste_softc	*sc;
{
	struct ste_chain	*cur_tx = NULL;
	struct ifnet		*ifp;
	int			idx;

	ifp = &sc->arpcom.ac_if;

	idx = sc->ste_cdata.ste_tx_cons;
	while(idx != sc->ste_cdata.ste_tx_prod) {
		cur_tx = &sc->ste_cdata.ste_tx_chain[idx];

		if (!(cur_tx->ste_ptr->ste_ctl & STE_TXCTL_DMADONE))
			break;

		if (cur_tx->ste_mbuf != NULL) {
			m_freem(cur_tx->ste_mbuf);
			cur_tx->ste_mbuf = NULL;
		}

		ifp->if_opackets++;

		sc->ste_cdata.ste_tx_cnt--;
		STE_INC(idx, STE_TX_LIST_CNT);
		ifp->if_timer = 0;
	}

	sc->ste_cdata.ste_tx_cons = idx;

	if (cur_tx != NULL)
		ifp->if_flags &= ~IFF_OACTIVE;

	return;
}

void ste_stats_update(xsc)
	void			*xsc;
{
	struct ste_softc	*sc;
	struct ste_stats	stats;
	struct ifnet		*ifp;
	struct mii_data		*mii;
	int			i, s;
	u_int8_t		*p;

	s = splimp();

	sc = xsc;
	ifp = &sc->arpcom.ac_if;
	mii = &sc->sc_mii;

	p = (u_int8_t *)&stats;

	for (i = 0; i < sizeof(stats); i++) {
		*p = CSR_READ_1(sc, STE_STATS + i);
		p++;
	}

	ifp->if_collisions += stats.ste_single_colls +
	    stats.ste_multi_colls + stats.ste_late_colls;

	mii_tick(mii);
	if (!sc->ste_link) {
		mii_pollstat(mii);
		if (mii->mii_media_status & IFM_ACTIVE &&
		    IFM_SUBTYPE(mii->mii_media_active) != IFM_NONE)
			sc->ste_link++;
		if (IFQ_IS_EMPTY(&ifp->if_snd) == 0)
			ste_start(ifp);
	}

	timeout_add(&sc->sc_stats_tmo, hz);
	splx(s);

	return;
}

const struct pci_matchid ste_devices[] = {
	{ PCI_VENDOR_SUNDANCE, PCI_PRODUCT_SUNDANCE_ST201 },
	{ PCI_VENDOR_DLINK, PCI_PRODUCT_DLINK_550TX },
};

/*
 * Probe for a Sundance ST201 chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
int ste_probe(parent, match, aux)
	struct device		*parent;
	void			*match, *aux;
{
	return (pci_matchbyid((struct pci_attach_args *)aux, ste_devices,
	    sizeof(ste_devices)/sizeof(ste_devices[0])));
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
void ste_attach(parent, self, aux)
	struct device		*parent, *self;
	void			*aux;
{
	int			s;
	const char		*intrstr = NULL;
	u_int32_t		command;
	struct ste_softc	*sc = (struct ste_softc *)self;
	struct pci_attach_args	*pa = aux;
	pci_chipset_tag_t	pc = pa->pa_pc;
	pci_intr_handle_t	ih;
	struct ifnet		*ifp;
	bus_addr_t		iobase;
	bus_size_t		iosize;

	s = splimp();
	sc->ste_unit = sc->sc_dev.dv_unit;

	/*
	 * Handle power management nonsense.
	 */
	command = pci_conf_read(pc, pa->pa_tag, STE_PCI_CAPID) & 0x000000FF;
	if (command == 0x01) {

		command = pci_conf_read(pc, pa->pa_tag, STE_PCI_PWRMGMTCTRL);
		if (command & STE_PSTATE_MASK) {
			u_int32_t		iobase, membase, irq;

			/* Save important PCI config data. */
			iobase = pci_conf_read(pc, pa->pa_tag, STE_PCI_LOIO);
			membase = pci_conf_read(pc, pa->pa_tag, STE_PCI_LOMEM);
			irq = pci_conf_read(pc, pa->pa_tag, STE_PCI_INTLINE);

			/* Reset the power state. */
			printf("ste%d: chip is in D%d power mode "
			"-- setting to D0\n", sc->ste_unit, command & STE_PSTATE_MASK);
			command &= 0xFFFFFFFC;
			pci_conf_write(pc, pa->pa_tag, STE_PCI_PWRMGMTCTRL, command);

			/* Restore PCI config data. */
			pci_conf_write(pc, pa->pa_tag, STE_PCI_LOIO, iobase);
			pci_conf_write(pc, pa->pa_tag, STE_PCI_LOMEM, membase);
			pci_conf_write(pc, pa->pa_tag, STE_PCI_INTLINE, irq);
		}
	}

	/*
	 * Map control/status registers.
	 */
	command = pci_conf_read(pc, pa->pa_tag, PCI_COMMAND_STATUS_REG);
	command |= PCI_COMMAND_IO_ENABLE | PCI_COMMAND_MEM_ENABLE |
	    PCI_COMMAND_MASTER_ENABLE;
	pci_conf_write(pc, pa->pa_tag, PCI_COMMAND_STATUS_REG, command);
	command = pci_conf_read(pc, pa->pa_tag, PCI_COMMAND_STATUS_REG);

#ifdef STE_USEIOSPACE
	if (!(command & PCI_COMMAND_IO_ENABLE)) {
		printf(": failed to enable I/O ports\n");
		goto fail;
	}
	if (pci_io_find(pc, pa->pa_tag, STE_PCI_LOIO, &iobase, &iosize)) {
		printf(": can't find I/O space\n");
		goto fail;
	}
	if (bus_space_map(pa->pa_iot, iobase, iosize, 0, &sc->ste_bhandle)) {
		printf(": can't map I/O space\n");
		goto fail;
	}
	sc->ste_btag = pa->pa_iot;
#else
	if (!(command & PCI_COMMAND_MEM_ENABLE)) {
		printf(": failed to enable memory mapping\n");
		goto fail;
	}
	if (pci_mem_find(pc, pa->pa_tag, STE_PCI_LOMEM, &iobase, &iosize,NULL)){
		printf(": can't find mem space\n");
		goto fail;
	}
	if (bus_space_map(pa->pa_memt, iobase, iosize, 0, &sc->ste_bhandle)) {
		printf(": can't map mem space\n");
		goto fail;
	}
	sc->ste_btag = pa->pa_memt;
#endif

	/* Allocate interrupt */
	if (pci_intr_map(pa, &ih)) {
		printf(": couldn't map interrupt\n");
		goto fail;
	}
	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_NET, ste_intr, sc,
	    self->dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto fail;
	}
	printf(": %s", intrstr);

	/* Reset the adapter. */
	ste_reset(sc);

	/*
	 * Get station address from the EEPROM.
	 */
	ste_read_eeprom(sc,(caddr_t)&sc->arpcom.ac_enaddr,STE_EEADDR_NODE0,3,0);

	printf(" address %s\n", ether_sprintf(sc->arpcom.ac_enaddr));

	sc->ste_ldata_ptr = malloc(sizeof(struct ste_list_data) + 8,
				M_DEVBUF, M_NOWAIT);
	if (sc->ste_ldata_ptr == NULL) {
		printf("%s: no memory for list buffers!\n", sc->ste_unit);
		goto fail;
	}

	sc->ste_ldata = (struct ste_list_data *)sc->ste_ldata_ptr;
	bzero(sc->ste_ldata, sizeof(struct ste_list_data));

	ifp = &sc->arpcom.ac_if;
	ifp->if_softc = sc;
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = ste_ioctl;
	ifp->if_output = ether_output;
	ifp->if_start = ste_start;
	ifp->if_watchdog = ste_watchdog;
	ifp->if_baudrate = 10000000;
	IFQ_SET_MAXLEN(&ifp->if_snd, STE_TX_LIST_CNT - 1);
	IFQ_SET_READY(&ifp->if_snd);
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);

	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = ste_miibus_readreg;
	sc->sc_mii.mii_writereg = ste_miibus_writereg;
	sc->sc_mii.mii_statchg = ste_miibus_statchg;
	ifmedia_init(&sc->sc_mii.mii_media, 0, ste_ifmedia_upd,ste_ifmedia_sts);
	mii_attach(self, &sc->sc_mii, 0xffffffff, MII_PHY_ANY, MII_OFFSET_ANY,
	    0);
	if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
		ifmedia_add(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE, 0, NULL);
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE);
	} else
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_AUTO);

	/*
	 * Call MI attach routines.
	 */
	if_attach(ifp);
	ether_ifattach(ifp);

	shutdownhook_establish(ste_shutdown, sc);

fail:
	splx(s);
	return;
}

int ste_newbuf(sc, c, m)
	struct ste_softc	*sc;
	struct ste_chain_onefrag	*c;
	struct mbuf		*m;
{
	struct mbuf		*m_new = NULL;

	if (m == NULL) {
		MGETHDR(m_new, M_DONTWAIT, MT_DATA);
		if (m_new == NULL) {
			printf("ste%d: no memory for rx list -- "
			    "packet dropped\n", sc->ste_unit);
			return(ENOBUFS);
		}
		MCLGET(m_new, M_DONTWAIT);
		if (!(m_new->m_flags & M_EXT)) {
			printf("ste%d: no memory for rx list -- "
			    "packet dropped\n", sc->ste_unit);
			m_freem(m_new);
			return(ENOBUFS);
		}
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
	} else {
		m_new = m;
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
		m_new->m_data = m_new->m_ext.ext_buf;
	}

	m_adj(m_new, ETHER_ALIGN);

	c->ste_mbuf = m_new;
	c->ste_ptr->ste_status = 0;
	c->ste_ptr->ste_frag.ste_addr = vtophys(mtod(m_new, caddr_t));
	c->ste_ptr->ste_frag.ste_len = 1536 | STE_FRAG_LAST;

	return(0);
}

int ste_init_rx_list(sc)
	struct ste_softc	*sc;
{
	struct ste_chain_data	*cd;
	struct ste_list_data	*ld;
	int			i;

	cd = &sc->ste_cdata;
	ld = sc->ste_ldata;

	for (i = 0; i < STE_RX_LIST_CNT; i++) {
		cd->ste_rx_chain[i].ste_ptr = &ld->ste_rx_list[i];
		if (ste_newbuf(sc, &cd->ste_rx_chain[i], NULL) == ENOBUFS)
			return(ENOBUFS);
		if (i == (STE_RX_LIST_CNT - 1)) {
			cd->ste_rx_chain[i].ste_next =
			    &cd->ste_rx_chain[0];
			ld->ste_rx_list[i].ste_next =
			    vtophys(&ld->ste_rx_list[0]);
		} else {
			cd->ste_rx_chain[i].ste_next =
			    &cd->ste_rx_chain[i + 1];
			ld->ste_rx_list[i].ste_next =
			    vtophys(&ld->ste_rx_list[i + 1]);
		}

	}

	cd->ste_rx_head = &cd->ste_rx_chain[0];

	return(0);
}

void ste_init_tx_list(sc)
	struct ste_softc	*sc;
{
	struct ste_chain_data	*cd;
	struct ste_list_data	*ld;
	int			i;

	cd = &sc->ste_cdata;
	ld = sc->ste_ldata;
	for (i = 0; i < STE_TX_LIST_CNT; i++) {
		cd->ste_tx_chain[i].ste_ptr = &ld->ste_tx_list[i];
		cd->ste_tx_chain[i].ste_phys = vtophys(&ld->ste_tx_list[i]);
		if (i == (STE_TX_LIST_CNT - 1))
			cd->ste_tx_chain[i].ste_next =
			    &cd->ste_tx_chain[0];
		else
			cd->ste_tx_chain[i].ste_next =
			    &cd->ste_tx_chain[i + 1];
		if (i == 0)
			cd->ste_tx_chain[i].ste_prev =
			    &cd->ste_tx_chain[STE_TX_LIST_CNT - 1];
		else
			cd->ste_tx_chain[i].ste_prev =
			    &cd->ste_tx_chain[i - 1];
	}

	bzero((char *)ld->ste_tx_list,
	    sizeof(struct ste_desc) * STE_TX_LIST_CNT);

	cd->ste_tx_prod = 0;
	cd->ste_tx_cons = 0;
	cd->ste_tx_cnt = 0;

	return;
}

void ste_init(xsc)
	void			*xsc;
{
	struct ste_softc	*sc = (struct ste_softc *)xsc;
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	struct mii_data		*mii;
	int			i, s;

	s = splimp();

	ste_stop(sc);

	mii = &sc->sc_mii;

	/* Init our MAC address */
	for (i = 0; i < ETHER_ADDR_LEN; i++) {
		CSR_WRITE_1(sc, STE_PAR0 + i, sc->arpcom.ac_enaddr[i]);
	}

	/* Init RX list */
	if (ste_init_rx_list(sc) == ENOBUFS) {
		printf("ste%d: initialization failed: no "
		    "memory for RX buffers\n", sc->ste_unit);
		ste_stop(sc);
		splx(s);
		return;
	}

	/* Init TX descriptors */
	ste_init_tx_list(sc);

	/* Set the TX freethresh value */
	CSR_WRITE_1(sc, STE_TX_DMABURST_THRESH, STE_PACKET_SIZE >> 8);

	/* Set the TX start threshold for best performance. */
	CSR_WRITE_2(sc, STE_TX_STARTTHRESH, sc->ste_tx_thresh);

	/* Set the TX reclaim threshold. */
	CSR_WRITE_1(sc, STE_TX_RECLAIM_THRESH, (STE_PACKET_SIZE >> 4));

	/* Set up the RX filter. */
	CSR_WRITE_1(sc, STE_RX_MODE, STE_RXMODE_UNICAST);

	/* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC) {
		STE_SETBIT1(sc, STE_RX_MODE, STE_RXMODE_PROMISC);
	} else {
		STE_CLRBIT1(sc, STE_RX_MODE, STE_RXMODE_PROMISC);
	}

	/* Set capture broadcast bit to accept broadcast frames. */
	if (ifp->if_flags & IFF_BROADCAST) {
		STE_SETBIT1(sc, STE_RX_MODE, STE_RXMODE_BROADCAST);
	} else {
		STE_CLRBIT1(sc, STE_RX_MODE, STE_RXMODE_BROADCAST);
	}

	ste_setmulti(sc);

	/* Load the address of the RX list. */
	STE_SETBIT4(sc, STE_DMACTL, STE_DMACTL_RXDMA_STALL);
	ste_wait(sc);
	CSR_WRITE_4(sc, STE_RX_DMALIST_PTR,
	    vtophys(&sc->ste_ldata->ste_rx_list[0]));
	STE_SETBIT4(sc, STE_DMACTL, STE_DMACTL_RXDMA_UNSTALL);
	STE_SETBIT4(sc, STE_DMACTL, STE_DMACTL_RXDMA_UNSTALL);

	/* Set TX polling interval */
	CSR_WRITE_1(sc, STE_TX_DMAPOLL_PERIOD, 64);

	/* Load address of the TX list */
	STE_SETBIT4(sc, STE_DMACTL, STE_DMACTL_TXDMA_STALL);
	ste_wait(sc);
	CSR_WRITE_4(sc, STE_TX_DMALIST_PTR,
	    vtophys(&sc->ste_ldata->ste_tx_list[0]));
	STE_SETBIT4(sc, STE_DMACTL, STE_DMACTL_TXDMA_UNSTALL);
	STE_SETBIT4(sc, STE_DMACTL, STE_DMACTL_TXDMA_UNSTALL);
	ste_wait(sc);

	/* Enable receiver and transmitter */
	CSR_WRITE_2(sc, STE_MACCTL0, 0);
	STE_SETBIT2(sc, STE_MACCTL1, STE_MACCTL1_TX_ENABLE);
	STE_SETBIT2(sc, STE_MACCTL1, STE_MACCTL1_RX_ENABLE);

	/* Enable stats counters. */
	STE_SETBIT2(sc, STE_MACCTL1, STE_MACCTL1_STATS_ENABLE);

	/* Enable interrupts. */
	CSR_WRITE_2(sc, STE_ISR, 0xFFFF);
	CSR_WRITE_2(sc, STE_IMR, STE_INTRS);

	ste_ifmedia_upd(ifp);

	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;

	splx(s);

	timeout_set(&sc->sc_stats_tmo, ste_stats_update, sc);
	timeout_add(&sc->sc_stats_tmo, hz);

	return;
}

void ste_stop(sc)
	struct ste_softc	*sc;
{
	int			i;
	struct ifnet		*ifp;

	ifp = &sc->arpcom.ac_if;

	timeout_del(&sc->sc_stats_tmo);

	CSR_WRITE_2(sc, STE_IMR, 0);
	STE_SETBIT2(sc, STE_MACCTL1, STE_MACCTL1_TX_DISABLE);
	STE_SETBIT2(sc, STE_MACCTL1, STE_MACCTL1_RX_DISABLE);
	STE_SETBIT2(sc, STE_MACCTL1, STE_MACCTL1_STATS_DISABLE);
	STE_SETBIT2(sc, STE_DMACTL, STE_DMACTL_TXDMA_STALL);
	STE_SETBIT2(sc, STE_DMACTL, STE_DMACTL_RXDMA_STALL);
	ste_wait(sc);

	sc->ste_link = 0;

	for (i = 0; i < STE_RX_LIST_CNT; i++) {
		if (sc->ste_cdata.ste_rx_chain[i].ste_mbuf != NULL) {
			m_freem(sc->ste_cdata.ste_rx_chain[i].ste_mbuf);
			sc->ste_cdata.ste_rx_chain[i].ste_mbuf = NULL;
		}
	}

	for (i = 0; i < STE_TX_LIST_CNT; i++) {
		if (sc->ste_cdata.ste_tx_chain[i].ste_mbuf != NULL) {
			m_freem(sc->ste_cdata.ste_tx_chain[i].ste_mbuf);
			sc->ste_cdata.ste_tx_chain[i].ste_mbuf = NULL;
		}
	}

	ifp->if_flags &= ~(IFF_RUNNING|IFF_OACTIVE);

	return;
}

void ste_reset(sc)
	struct ste_softc	*sc;
{
	int			i;

	STE_SETBIT4(sc, STE_ASICCTL,
	    STE_ASICCTL_GLOBAL_RESET|STE_ASICCTL_RX_RESET|
	    STE_ASICCTL_TX_RESET|STE_ASICCTL_DMA_RESET|
	    STE_ASICCTL_FIFO_RESET|STE_ASICCTL_NETWORK_RESET|
	    STE_ASICCTL_AUTOINIT_RESET|STE_ASICCTL_HOST_RESET|
	    STE_ASICCTL_EXTRESET_RESET);

	DELAY(100000);

	for (i = 0; i < STE_TIMEOUT; i++) {
		if (!(CSR_READ_4(sc, STE_ASICCTL) & STE_ASICCTL_RESET_BUSY))
			break;
	}

	if (i == STE_TIMEOUT)
		printf("ste%d: global reset never completed\n", sc->ste_unit);

	return;
}

int ste_ioctl(ifp, command, data)
	struct ifnet		*ifp;
	u_long			command;
	caddr_t			data;
{
	struct ste_softc	*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *) data;
	struct ifaddr		*ifa = (struct ifaddr *)data;
	struct mii_data		*mii;
	int			s, error = 0;

	s = splimp();

	if ((error = ether_ioctl(ifp, &sc->arpcom, command, data)) > 0) {
		splx(s);
		return error;
	}

	switch(command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		switch (ifa->ifa_addr->sa_family) {
		case AF_INET:
			ste_init(sc);
			arp_ifinit(&sc->arpcom, ifa);
			break;
		default:
			ste_init(sc);
			break;
		}
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING &&
			    ifp->if_flags & IFF_PROMISC &&
			    !(sc->ste_if_flags & IFF_PROMISC)) {
				STE_SETBIT1(sc, STE_RX_MODE,
				    STE_RXMODE_PROMISC);
			} else if (ifp->if_flags & IFF_RUNNING &&
			    !(ifp->if_flags & IFF_PROMISC) &&
			    sc->ste_if_flags & IFF_PROMISC) {
				STE_CLRBIT1(sc, STE_RX_MODE,
				    STE_RXMODE_PROMISC);
			} else if (!(ifp->if_flags & IFF_RUNNING)) {
				sc->ste_tx_thresh = STE_MIN_FRAMELEN;
				ste_init(sc);
			}
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				ste_stop(sc);
		}
		sc->ste_if_flags = ifp->if_flags;
		error = 0;
		break;
	case SIOCADDMULTI:
	case SIOCDELMULTI:
		error = (command == SIOCADDMULTI) ?
		    ether_addmulti(ifr, &sc->arpcom) :
		    ether_delmulti(ifr, &sc->arpcom);

		if (error == ENETRESET) {
			/*
			 * Multicast list has changed; set the hardware
			 * filter accordingly.
			 */
			ste_setmulti(sc);
			error = 0;
		}
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		mii = &sc->sc_mii;
		error = ifmedia_ioctl(ifp, ifr, &mii->mii_media, command);
		break;
	default:
		error = EINVAL;
		break;
	}

	splx(s);

	return(error);
}

int ste_encap(sc, c, m_head)
	struct ste_softc	*sc;
	struct ste_chain	*c;
	struct mbuf		*m_head;
{
	int			frag = 0;
	struct ste_frag		*f = NULL;
	struct mbuf		*m;
	struct ste_desc		*d;
	int			total_len = 0;

	d = c->ste_ptr;
	d->ste_ctl = 0;
	d->ste_next = 0;

	for (m = m_head, frag = 0; m != NULL; m = m->m_next) {
		if (m->m_len != 0) {
			if (frag == STE_MAXFRAGS)
				break;
			total_len += m->m_len;
			f = &c->ste_ptr->ste_frags[frag];
			f->ste_addr = vtophys(mtod(m, vaddr_t));
			f->ste_len = m->m_len;
			frag++;
		}
	}

	c->ste_mbuf = m_head;
	c->ste_ptr->ste_frags[frag - 1].ste_len |= STE_FRAG_LAST;
	c->ste_ptr->ste_ctl = total_len;

	return(0);
}

void ste_start(ifp)
	struct ifnet		*ifp;
{
	struct ste_softc	*sc;
	struct mbuf		*m_head = NULL;
	struct ste_chain	*prev = NULL, *cur_tx = NULL, *start_tx;
	int			idx;

	sc = ifp->if_softc;

	if (!sc->ste_link)
		return;

	if (ifp->if_flags & IFF_OACTIVE)
		return;

	idx = sc->ste_cdata.ste_tx_prod;
	start_tx = &sc->ste_cdata.ste_tx_chain[idx];

	while(sc->ste_cdata.ste_tx_chain[idx].ste_mbuf == NULL) {
		if ((STE_TX_LIST_CNT - sc->ste_cdata.ste_tx_cnt) < 3) {
			ifp->if_flags |= IFF_OACTIVE;
			break;
		}

		IFQ_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;

		cur_tx = &sc->ste_cdata.ste_tx_chain[idx];

		ste_encap(sc, cur_tx, m_head);

		if (prev != NULL)
			prev->ste_ptr->ste_next = cur_tx->ste_phys;
		prev = cur_tx;

#if NBPFILTER > 0
		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
	 	 */
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, cur_tx->ste_mbuf);
#endif

		STE_INC(idx, STE_TX_LIST_CNT);
		sc->ste_cdata.ste_tx_cnt++;
	}

	if (cur_tx == NULL)
		return;

	cur_tx->ste_ptr->ste_ctl |= STE_TXCTL_DMAINTR;

	/* Start transmission */
	sc->ste_cdata.ste_tx_prod = idx;
	start_tx->ste_prev->ste_ptr->ste_next = start_tx->ste_phys;

	ifp->if_timer = 5;

	return;
}

void ste_watchdog(ifp)
	struct ifnet		*ifp;
{
	struct ste_softc	*sc;

	sc = ifp->if_softc;

	ifp->if_oerrors++;
	printf("ste%d: watchdog timeout\n", sc->ste_unit);

	ste_txeoc(sc);
	ste_txeof(sc);
	ste_rxeof(sc);
	ste_reset(sc);
	ste_init(sc);

	if (IFQ_IS_EMPTY(&ifp->if_snd) == 0)
		ste_start(ifp);

	return;
}

void ste_shutdown(v)
	void			*v;
{
	struct ste_softc	*sc = (struct ste_softc *)v;

	ste_stop(sc);
}

struct cfattach ste_ca = {
	sizeof(struct ste_softc), ste_probe, ste_attach
};

struct cfdriver ste_cd = {
	0, "ste", DV_IFNET
};

