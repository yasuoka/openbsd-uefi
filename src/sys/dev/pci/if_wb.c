/*	$OpenBSD: if_wb.c,v 1.12 2001/08/12 20:03:49 mickey Exp $	*/

/*
 * Copyright (c) 1997, 1998
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
 * $FreeBSD: src/sys/pci/if_wb.c,v 1.26 1999/09/25 17:29:02 wpaul Exp $
 */

/*
 * Winbond fast ethernet PCI NIC driver
 *
 * Supports various cheap network adapters based on the Winbond W89C840F
 * fast ethernet controller chip. This includes adapters manufactured by
 * Winbond itself and some made by Linksys.
 *
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */

/*
 * The Winbond W89C840F chip is a bus master; in some ways it resembles
 * a DEC 'tulip' chip, only not as complicated. Unfortunately, it has
 * one major difference which is that while the registers do many of
 * the same things as a tulip adapter, the offsets are different: where
 * tulip registers are typically spaced 8 bytes apart, the Winbond
 * registers are spaced 4 bytes apart. The receiver filter is also
 * programmed differently.
 * 
 * Like the tulip, the Winbond chip uses small descriptors containing
 * a status word, a control word and 32-bit areas that can either be used
 * to point to two external data blocks, or to point to a single block
 * and another descriptor in a linked list. Descriptors can be grouped
 * together in blocks to form fixed length rings or can be chained
 * together in linked lists. A single packet may be spread out over
 * several descriptors if necessary.
 *
 * For the receive ring, this driver uses a linked list of descriptors,
 * each pointing to a single mbuf cluster buffer, which us large enough
 * to hold an entire packet. The link list is looped back to created a
 * closed ring.
 *
 * For transmission, the driver creates a linked list of 'super descriptors'
 * which each contain several individual descriptors linked toghether.
 * Each 'super descriptor' contains WB_MAXFRAGS descriptors, which we
 * abuse as fragment pointers. This allows us to use a buffer managment
 * scheme very similar to that used in the ThunderLAN and Etherlink XL
 * drivers.
 *
 * Autonegotiation is performed using the external PHY via the MII bus.
 * The sample boards I have all use a Davicom PHY.
 *
 * Note: the author of the Linux driver for the Winbond chip alludes
 * to some sort of flaw in the chip's design that seems to mandate some
 * drastic workaround which signigicantly impairs transmit performance.
 * I have no idea what he's on about: transmit performance with all
 * three of my test boards seems fine.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/device.h>
#include <sys/queue.h>
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

#include <vm/vm.h>		/* for vtophys */
#include <vm/vm_kern.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#define WB_USEIOSPACE

/* #define WB_BACKGROUND_AUTONEG */

#include <dev/pci/if_wbreg.h>

int wb_probe		__P((struct device *, void *, void *));
void wb_attach		__P((struct device *, struct device *, void *));

void wb_bfree		__P((caddr_t, u_int, void *));
int wb_newbuf		__P((struct wb_softc *, struct wb_chain_onefrag *,
    struct mbuf *));
int wb_encap		__P((struct wb_softc *, struct wb_chain *,
    struct mbuf *));

void wb_rxeof		__P((struct wb_softc *));
void wb_rxeoc		__P((struct wb_softc *));
void wb_txeof		__P((struct wb_softc *));
void wb_txeoc		__P((struct wb_softc *));
int wb_intr		__P((void *));
void wb_tick		__P((void *));
void wb_start		__P((struct ifnet *));
int wb_ioctl		__P((struct ifnet *, u_long, caddr_t));
void wb_init		__P((void *));
void wb_stop		__P((struct wb_softc *));
void wb_watchdog	__P((struct ifnet *));
void wb_shutdown	__P((void *));
int wb_ifmedia_upd	__P((struct ifnet *));
void wb_ifmedia_sts	__P((struct ifnet *, struct ifmediareq *));

void wb_eeprom_putbyte	__P((struct wb_softc *, int));
void wb_eeprom_getword	__P((struct wb_softc *, int, u_int16_t *));
void wb_read_eeprom	__P((struct wb_softc *, caddr_t, int, int, int));
void wb_mii_sync	__P((struct wb_softc *));
void wb_mii_send	__P((struct wb_softc *, u_int32_t, int));
int wb_mii_readreg	__P((struct wb_softc *, struct wb_mii_frame *));
int wb_mii_writereg	__P((struct wb_softc *, struct wb_mii_frame *));

void wb_setcfg		__P((struct wb_softc *, u_int32_t));
u_int8_t wb_calchash	__P((caddr_t));
void wb_setmulti	__P((struct wb_softc *));
void wb_reset		__P((struct wb_softc *));
void wb_fixmedia	__P((struct wb_softc *));
int wb_list_rx_init	__P((struct wb_softc *));
int wb_list_tx_init	__P((struct wb_softc *));

int wb_miibus_readreg	__P((struct device *, int, int));
void wb_miibus_writereg	__P((struct device *, int, int, int));
void wb_miibus_statchg	__P((struct device *));

#define WB_SETBIT(sc, reg, x)				\
	CSR_WRITE_4(sc, reg,				\
		CSR_READ_4(sc, reg) | x)

#define WB_CLRBIT(sc, reg, x)				\
	CSR_WRITE_4(sc, reg,				\
		CSR_READ_4(sc, reg) & ~x)

#define SIO_SET(x)					\
	CSR_WRITE_4(sc, WB_SIO,				\
		CSR_READ_4(sc, WB_SIO) | x)

#define SIO_CLR(x)					\
	CSR_WRITE_4(sc, WB_SIO,				\
		CSR_READ_4(sc, WB_SIO) & ~x)

/*
 * Send a read command and address to the EEPROM, check for ACK.
 */
void wb_eeprom_putbyte(sc, addr)
	struct wb_softc		*sc;
	int			addr;
{
	register int		d, i;

	d = addr | WB_EECMD_READ;

	/*
	 * Feed in each bit and stobe the clock.
	 */
	for (i = 0x400; i; i >>= 1) {
		if (d & i) {
			SIO_SET(WB_SIO_EE_DATAIN);
		} else {
			SIO_CLR(WB_SIO_EE_DATAIN);
		}
		DELAY(100);
		SIO_SET(WB_SIO_EE_CLK);
		DELAY(150);
		SIO_CLR(WB_SIO_EE_CLK);
		DELAY(100);
	}

	return;
}

/*
 * Read a word of data stored in the EEPROM at address 'addr.'
 */
void wb_eeprom_getword(sc, addr, dest)
	struct wb_softc		*sc;
	int			addr;
	u_int16_t		*dest;
{
	register int		i;
	u_int16_t		word = 0;

	/* Enter EEPROM access mode. */
	CSR_WRITE_4(sc, WB_SIO, WB_SIO_EESEL|WB_SIO_EE_CS);

	/*
	 * Send address of word we want to read.
	 */
	wb_eeprom_putbyte(sc, addr);

	CSR_WRITE_4(sc, WB_SIO, WB_SIO_EESEL|WB_SIO_EE_CS);

	/*
	 * Start reading bits from EEPROM.
	 */
	for (i = 0x8000; i; i >>= 1) {
		SIO_SET(WB_SIO_EE_CLK);
		DELAY(100);
		if (CSR_READ_4(sc, WB_SIO) & WB_SIO_EE_DATAOUT)
			word |= i;
		SIO_CLR(WB_SIO_EE_CLK);
		DELAY(100);
	}

	/* Turn off EEPROM access mode. */
	CSR_WRITE_4(sc, WB_SIO, 0);

	*dest = word;

	return;
}

/*
 * Read a sequence of words from the EEPROM.
 */
void wb_read_eeprom(sc, dest, off, cnt, swap)
	struct wb_softc		*sc;
	caddr_t			dest;
	int			off;
	int			cnt;
	int			swap;
{
	int			i;
	u_int16_t		word = 0, *ptr;

	for (i = 0; i < cnt; i++) {
		wb_eeprom_getword(sc, off + i, &word);
		ptr = (u_int16_t *)(dest + (i * 2));
		if (swap)
			*ptr = ntohs(word);
		else
			*ptr = word;
	}

	return;
}

/*
 * Sync the PHYs by setting data bit and strobing the clock 32 times.
 */
void wb_mii_sync(sc)
	struct wb_softc		*sc;
{
	register int		i;

	SIO_SET(WB_SIO_MII_DIR|WB_SIO_MII_DATAIN);

	for (i = 0; i < 32; i++) {
		SIO_SET(WB_SIO_MII_CLK);
		DELAY(1);
		SIO_CLR(WB_SIO_MII_CLK);
		DELAY(1);
	}

	return;
}

/*
 * Clock a series of bits through the MII.
 */
void wb_mii_send(sc, bits, cnt)
	struct wb_softc		*sc;
	u_int32_t		bits;
	int			cnt;
{
	int			i;

	SIO_CLR(WB_SIO_MII_CLK);

	for (i = (0x1 << (cnt - 1)); i; i >>= 1) {
                if (bits & i) {
			SIO_SET(WB_SIO_MII_DATAIN);
                } else {
			SIO_CLR(WB_SIO_MII_DATAIN);
                }
		DELAY(1);
		SIO_CLR(WB_SIO_MII_CLK);
		DELAY(1);
		SIO_SET(WB_SIO_MII_CLK);
	}
}

/*
 * Read an PHY register through the MII.
 */
int wb_mii_readreg(sc, frame)
	struct wb_softc		*sc;
	struct wb_mii_frame	*frame;
	
{
	int			i, ack, s;

	s = splimp();

	/*
	 * Set up frame for RX.
	 */
	frame->mii_stdelim = WB_MII_STARTDELIM;
	frame->mii_opcode = WB_MII_READOP;
	frame->mii_turnaround = 0;
	frame->mii_data = 0;
	
	CSR_WRITE_4(sc, WB_SIO, 0);

	/*
 	 * Turn on data xmit.
	 */
	SIO_SET(WB_SIO_MII_DIR);

	wb_mii_sync(sc);

	/*
	 * Send command/address info.
	 */
	wb_mii_send(sc, frame->mii_stdelim, 2);
	wb_mii_send(sc, frame->mii_opcode, 2);
	wb_mii_send(sc, frame->mii_phyaddr, 5);
	wb_mii_send(sc, frame->mii_regaddr, 5);

	/* Idle bit */
	SIO_CLR((WB_SIO_MII_CLK|WB_SIO_MII_DATAIN));
	DELAY(1);
	SIO_SET(WB_SIO_MII_CLK);
	DELAY(1);

	/* Turn off xmit. */
	SIO_CLR(WB_SIO_MII_DIR);
	/* Check for ack */
	SIO_CLR(WB_SIO_MII_CLK);
	DELAY(1);
	SIO_SET(WB_SIO_MII_CLK);
	DELAY(1);
	ack = CSR_READ_4(sc, WB_SIO) & WB_SIO_MII_DATAOUT;
	SIO_CLR(WB_SIO_MII_CLK);
	DELAY(1);
	SIO_SET(WB_SIO_MII_CLK);
	DELAY(1);

	/*
	 * Now try reading data bits. If the ack failed, we still
	 * need to clock through 16 cycles to keep the PHY(s) in sync.
	 */
	if (ack) {
		for(i = 0; i < 16; i++) {
			SIO_CLR(WB_SIO_MII_CLK);
			DELAY(1);
			SIO_SET(WB_SIO_MII_CLK);
			DELAY(1);
		}
		goto fail;
	}

	for (i = 0x8000; i; i >>= 1) {
		SIO_CLR(WB_SIO_MII_CLK);
		DELAY(1);
		if (!ack) {
			if (CSR_READ_4(sc, WB_SIO) & WB_SIO_MII_DATAOUT)
				frame->mii_data |= i;
			DELAY(1);
		}
		SIO_SET(WB_SIO_MII_CLK);
		DELAY(1);
	}

fail:

	SIO_CLR(WB_SIO_MII_CLK);
	DELAY(1);
	SIO_SET(WB_SIO_MII_CLK);
	DELAY(1);

	splx(s);

	if (ack)
		return(1);
	return(0);
}

/*
 * Write to a PHY register through the MII.
 */
int wb_mii_writereg(sc, frame)
	struct wb_softc		*sc;
	struct wb_mii_frame	*frame;
	
{
	int			s;

	s = splimp();
	/*
	 * Set up frame for TX.
	 */

	frame->mii_stdelim = WB_MII_STARTDELIM;
	frame->mii_opcode = WB_MII_WRITEOP;
	frame->mii_turnaround = WB_MII_TURNAROUND;
	
	/*
 	 * Turn on data output.
	 */
	SIO_SET(WB_SIO_MII_DIR);

	wb_mii_sync(sc);

	wb_mii_send(sc, frame->mii_stdelim, 2);
	wb_mii_send(sc, frame->mii_opcode, 2);
	wb_mii_send(sc, frame->mii_phyaddr, 5);
	wb_mii_send(sc, frame->mii_regaddr, 5);
	wb_mii_send(sc, frame->mii_turnaround, 2);
	wb_mii_send(sc, frame->mii_data, 16);

	/* Idle bit. */
	SIO_SET(WB_SIO_MII_CLK);
	DELAY(1);
	SIO_CLR(WB_SIO_MII_CLK);
	DELAY(1);

	/*
	 * Turn off xmit.
	 */
	SIO_CLR(WB_SIO_MII_DIR);

	splx(s);

	return(0);
}

int
wb_miibus_readreg(dev, phy, reg)
	struct device *dev;
	int phy, reg;
{
	struct wb_softc *sc = (struct wb_softc *)dev;
	struct wb_mii_frame frame;

	bzero((char *)&frame, sizeof(frame));

	frame.mii_phyaddr = phy;
	frame.mii_regaddr = reg;
	wb_mii_readreg(sc, &frame);

	return(frame.mii_data);
}

void
wb_miibus_writereg(dev, phy, reg, data)
	struct device *dev;
	int phy, reg, data;
{
	struct wb_softc *sc = (struct wb_softc *)dev;
	struct wb_mii_frame frame;

	bzero((char *)&frame, sizeof(frame));

	frame.mii_phyaddr = phy;
	frame.mii_regaddr = reg;
	frame.mii_data = data;

	wb_mii_writereg(sc, &frame);

	return;
}

void
wb_miibus_statchg(dev)
	struct device *dev;
{
	struct wb_softc *sc = (struct wb_softc *)dev;

	wb_setcfg(sc, sc->sc_mii.mii_media_active);
}

u_int8_t wb_calchash(addr)
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

	/*
	 * return the filter bit position
	 * Note: I arrived at the following nonsense
	 * through experimentation. It's not the usual way to
	 * generate the bit position but it's the only thing
	 * I could come up with that works.
	 */
	return(~(crc >> 26) & 0x0000003F);
}

/*
 * Program the 64-bit multicast hash filter.
 */
void wb_setmulti(sc)
	struct wb_softc		*sc;
{
	struct ifnet		*ifp;
	int			h = 0;
	u_int32_t		hashes[2] = { 0, 0 };
	struct arpcom		*ac = &sc->arpcom;
	struct ether_multi	*enm;
	struct ether_multistep	step;
	u_int32_t		rxfilt;
	int			mcnt = 0;

	ifp = &sc->arpcom.ac_if;

	rxfilt = CSR_READ_4(sc, WB_NETCFG);

	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		rxfilt |= WB_NETCFG_RX_MULTI;
		CSR_WRITE_4(sc, WB_NETCFG, rxfilt);
		CSR_WRITE_4(sc, WB_MAR0, 0xFFFFFFFF);
		CSR_WRITE_4(sc, WB_MAR1, 0xFFFFFFFF);
		return;
	}

	/* first, zot all the existing hash bits */
	CSR_WRITE_4(sc, WB_MAR0, 0);
	CSR_WRITE_4(sc, WB_MAR1, 0);

	/* now program new ones */
	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		h = wb_calchash(enm->enm_addrlo);
		if (h < 32)
			hashes[0] |= (1 << h);
		else
			hashes[1] |= (1 << (h - 32));
		mcnt++;
		ETHER_NEXT_MULTI(step, enm);
	}

	if (mcnt)
		rxfilt |= WB_NETCFG_RX_MULTI;
	else
		rxfilt &= ~WB_NETCFG_RX_MULTI;

	CSR_WRITE_4(sc, WB_MAR0, hashes[0]);
	CSR_WRITE_4(sc, WB_MAR1, hashes[1]);
	CSR_WRITE_4(sc, WB_NETCFG, rxfilt);

	return;
}

/*
 * The Winbond manual states that in order to fiddle with the
 * 'full-duplex' and '100Mbps' bits in the netconfig register, we
 * first have to put the transmit and/or receive logic in the idle state.
 */
void
wb_setcfg(sc, media)
	struct wb_softc *sc;
	u_int32_t media;
{
	int			i, restart = 0;

	if (CSR_READ_4(sc, WB_NETCFG) & (WB_NETCFG_TX_ON|WB_NETCFG_RX_ON)) {
		restart = 1;
		WB_CLRBIT(sc, WB_NETCFG, (WB_NETCFG_TX_ON|WB_NETCFG_RX_ON));

		for (i = 0; i < WB_TIMEOUT; i++) {
			DELAY(10);
			if ((CSR_READ_4(sc, WB_ISR) & WB_ISR_TX_IDLE) &&
				(CSR_READ_4(sc, WB_ISR) & WB_ISR_RX_IDLE))
				break;
		}

		if (i == WB_TIMEOUT)
			printf("%s: failed to force tx and "
				"rx to idle state\n", sc->sc_dev.dv_xname);
	}

	if (IFM_SUBTYPE(media) == IFM_10_T)
		WB_CLRBIT(sc, WB_NETCFG, WB_NETCFG_100MBPS);
	else
		WB_SETBIT(sc, WB_NETCFG, WB_NETCFG_100MBPS);

	if ((media & IFM_GMASK) == IFM_FDX)
		WB_SETBIT(sc, WB_NETCFG, WB_NETCFG_FULLDUPLEX);
	else
		WB_CLRBIT(sc, WB_NETCFG, WB_NETCFG_FULLDUPLEX);

	if (restart)
		WB_SETBIT(sc, WB_NETCFG, WB_NETCFG_TX_ON|WB_NETCFG_RX_ON);

	return;
}

void
wb_reset(sc)
	struct wb_softc *sc;
{
	register int i;
	struct mii_data *mii = &sc->sc_mii;

	CSR_WRITE_4(sc, WB_NETCFG, 0);
	CSR_WRITE_4(sc, WB_BUSCTL, 0);
	CSR_WRITE_4(sc, WB_TXADDR, 0);
	CSR_WRITE_4(sc, WB_RXADDR, 0);

	WB_SETBIT(sc, WB_BUSCTL, WB_BUSCTL_RESET);
	WB_SETBIT(sc, WB_BUSCTL, WB_BUSCTL_RESET);

	for (i = 0; i < WB_TIMEOUT; i++) {
		DELAY(10);
		if (!(CSR_READ_4(sc, WB_BUSCTL) & WB_BUSCTL_RESET))
			break;
	}
	if (i == WB_TIMEOUT)
		printf("%s: reset never completed!\n", sc->sc_dev.dv_xname);

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(1000);

	if (mii->mii_instance) {
		struct mii_softc *miisc;
		for (miisc = LIST_FIRST(&mii->mii_phys); miisc != NULL;
		    miisc = LIST_NEXT(miisc, mii_list))
			mii_phy_reset(miisc);
	}
}

void
wb_fixmedia(sc)
	struct wb_softc *sc;
{
	struct mii_data *mii = &sc->sc_mii;
	u_int32_t media;

	if (LIST_FIRST(&mii->mii_phys) == NULL)
		return;

	mii_pollstat(mii);
	if (IFM_SUBTYPE(mii->mii_media_active) == IFM_10_T) {
		media = mii->mii_media_active & ~IFM_10_T;
		media |= IFM_100_TX;
	} if (IFM_SUBTYPE(mii->mii_media_active) == IFM_100_TX) {
		media = mii->mii_media_active & ~IFM_100_TX;
		media |= IFM_10_T;
	} else
		return;

	ifmedia_set(&mii->mii_media, media);
}

/*
 * Probe for a Winbond chip. Check the PCI vendor and device
 * IDs against our list and return a device name if we find a match.
 */
int
wb_probe(parent, match, aux)
	struct device *parent;
	void *match, *aux;
{
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_WINBOND) {
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_WINBOND_W89C840F:
			return (1);
		}
	}

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_COMPEX) {
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_COMPEX_RL100ATX:
			return (1);
		}
	}

	return (0);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
void
wb_attach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	struct wb_softc *sc = (struct wb_softc *)self;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pci_intr_handle_t ih;
	const char *intrstr = NULL;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	bus_addr_t iobase;
	bus_size_t iosize;
	int s, rseg;
	u_int32_t command;
	bus_dma_segment_t seg;
	bus_dmamap_t dmamap;
	caddr_t kva;

	s = splimp();

	/*
	 * Handle power management nonsense.
	 */

	command = pci_conf_read(pc, pa->pa_tag, WB_PCI_CAPID) & 0x000000FF;
	if (command == 0x01) {

		command = pci_conf_read(pc, pa->pa_tag, WB_PCI_PWRMGMTCTRL);
		if (command & WB_PSTATE_MASK) {
			u_int32_t		io, mem, irq;

			/* Save important PCI config data. */
			io = pci_conf_read(pc, pa->pa_tag, WB_PCI_LOIO);
			mem = pci_conf_read(pc, pa->pa_tag, WB_PCI_LOMEM);
			irq = pci_conf_read(pc, pa->pa_tag, WB_PCI_INTLINE);

			/* Reset the power state. */
			printf("%s: chip is in D%d power mode "
			    "-- setting to D0\n", sc->sc_dev.dv_xname,
			    command & WB_PSTATE_MASK);
			command &= 0xFFFFFFFC;
			pci_conf_write(pc, pa->pa_tag, WB_PCI_PWRMGMTCTRL,
			    command);

			/* Restore PCI config data. */
			pci_conf_write(pc, pa->pa_tag, WB_PCI_LOIO, io);
			pci_conf_write(pc, pa->pa_tag, WB_PCI_LOMEM, mem);
			pci_conf_write(pc, pa->pa_tag, WB_PCI_INTLINE, irq);
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

#ifdef WB_USEIOSPACE
	if (!(command & PCI_COMMAND_IO_ENABLE)) {
		printf(": failed to enable I/O ports!\n");
		goto fail;
	}
	if (pci_io_find(pc, pa->pa_tag, WB_PCI_LOIO, &iobase, &iosize)) {
		printf(": can't find i/o space\n");
		goto fail;
	}
	if (bus_space_map(pa->pa_iot, iobase, iosize, 0, &sc->wb_bhandle)) {
		printf(": can't map i/o space\n");
		goto fail;
	}
	sc->wb_btag = pa->pa_iot;
#else
	if (!(command & PCI_COMMAND_MEM_ENABLE)) {
		printf(": failed to enable memory mapping!\n");
		goto fail;
	}
	if (pci_mem_find(pc, pa->pa_tag, WB_PCI_LOMEM, &iobase, &iosize, NULL)){
		printf(": can't find mem space\n");
		goto fail;
	}
	if (bus_space_map(pa->pa_memt, iobase, iosize, 0, &sc->wb_bhandle)) {
		printf(": can't map mem space\n");
		goto fail;
	}
	sc->wb_btag = pa->pa_memt;
#endif

	/* Allocate interrupt */
	if (pci_intr_map(pc, pa->pa_intrtag, pa->pa_intrpin,
	    pa->pa_intrline, &ih)) {
		printf(": couldn't map interrupt\n");
		goto fail;
	}
	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_NET, wb_intr, sc,
	    self->dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto fail;
	}
	printf(": %s", intrstr);

	sc->wb_cachesize = pci_conf_read(pc, pa->pa_tag, WB_PCI_CACHELEN)&0xff;

	/* Reset the adapter. */
	wb_reset(sc);

	/*
	 * Get station address from the EEPROM.
	 */
	wb_read_eeprom(sc, (caddr_t)&sc->arpcom.ac_enaddr, 0, 3, 0);
	printf(" address %s\n", ether_sprintf(sc->arpcom.ac_enaddr));

	if (bus_dmamem_alloc(pa->pa_dmat, sizeof(struct wb_list_data),
	    PAGE_SIZE, 0, &seg, 1, &rseg, BUS_DMA_NOWAIT)) {
		printf("%s: can't alloc list data\n", sc->sc_dev.dv_xname);
		goto fail;
	}
	if (bus_dmamem_map(pa->pa_dmat, &seg, rseg,
	    sizeof(struct wb_list_data), &kva, BUS_DMA_NOWAIT)) {
		printf("%s: can't map list data, size %d\n",
		    sc->sc_dev.dv_xname, sizeof(struct wb_list_data));
		bus_dmamem_free(pa->pa_dmat, &seg, rseg);
		goto fail;
	}
	if (bus_dmamap_create(pa->pa_dmat, sizeof(struct wb_list_data), 1,
	    sizeof(struct wb_list_data), 0, BUS_DMA_NOWAIT, &dmamap)) {
		printf("%s: can't create dma map\n", sc->sc_dev.dv_xname);
		bus_dmamem_unmap(pa->pa_dmat, kva,
		    sizeof(struct wb_list_data));
		bus_dmamem_free(pa->pa_dmat, &seg, rseg);
		goto fail;
	}
	if (bus_dmamap_load(pa->pa_dmat, dmamap, kva,
	    sizeof(struct wb_list_data), NULL, BUS_DMA_NOWAIT)) {
		printf("%s: can't load dma map\n", sc->sc_dev.dv_xname);
		bus_dmamap_destroy(pa->pa_dmat, dmamap);
		bus_dmamem_unmap(pa->pa_dmat, kva,
		    sizeof(struct wb_list_data));
		bus_dmamem_free(pa->pa_dmat, &seg, rseg);
		goto fail;
	}
	sc->wb_ldata = (struct wb_list_data *)kva;
	bzero(sc->wb_ldata, sizeof(struct wb_list_data));

	ifp->if_softc = sc;
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = wb_ioctl;
	ifp->if_output = ether_output;
	ifp->if_start = wb_start;
	ifp->if_watchdog = wb_watchdog;
	ifp->if_baudrate = 10000000;
	IFQ_SET_MAXLEN(&ifp->if_snd, WB_TX_LIST_CNT - 1);
	IFQ_SET_READY(&ifp->if_snd);

	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);

	/*
	 * Do ifmedia setup.
	 */
	wb_stop(sc);

	ifmedia_init(&sc->sc_mii.mii_media, 0, wb_ifmedia_upd, wb_ifmedia_sts);
	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = wb_miibus_readreg;
	sc->sc_mii.mii_writereg = wb_miibus_writereg;
	sc->sc_mii.mii_statchg = wb_miibus_statchg;
	mii_attach(self, &sc->sc_mii, 0xffffffff, MII_PHY_ANY, MII_OFFSET_ANY,
	    0);
	if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
		ifmedia_add(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE,0,NULL);
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE);
	} else
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_AUTO);

	/*
	 * Call MI attach routines.
	 */
	if_attach(ifp);
	ether_ifattach(ifp);

	shutdownhook_establish(wb_shutdown, sc);

fail:
	splx(s);
	return;
}

/*
 * Initialize the transmit descriptors.
 */
int wb_list_tx_init(sc)
	struct wb_softc		*sc;
{
	struct wb_chain_data	*cd;
	struct wb_list_data	*ld;
	int			i;

	cd = &sc->wb_cdata;
	ld = sc->wb_ldata;

	for (i = 0; i < WB_TX_LIST_CNT; i++) {
		cd->wb_tx_chain[i].wb_ptr = &ld->wb_tx_list[i];
		if (i == (WB_TX_LIST_CNT - 1)) {
			cd->wb_tx_chain[i].wb_nextdesc =
				&cd->wb_tx_chain[0];
		} else {
			cd->wb_tx_chain[i].wb_nextdesc =
				&cd->wb_tx_chain[i + 1];
		}
	}

	cd->wb_tx_free = &cd->wb_tx_chain[0];
	cd->wb_tx_tail = cd->wb_tx_head = NULL;

	return(0);
}


/*
 * Initialize the RX descriptors and allocate mbufs for them. Note that
 * we arrange the descriptors in a closed ring, so that the last descriptor
 * points back to the first.
 */
int wb_list_rx_init(sc)
	struct wb_softc		*sc;
{
	struct wb_chain_data	*cd;
	struct wb_list_data	*ld;
	int			i;

	cd = &sc->wb_cdata;
	ld = sc->wb_ldata;

	for (i = 0; i < WB_RX_LIST_CNT; i++) {
		cd->wb_rx_chain[i].wb_ptr =
			(struct wb_desc *)&ld->wb_rx_list[i];
		cd->wb_rx_chain[i].wb_buf = (void *)&ld->wb_rxbufs[i];
		if (wb_newbuf(sc, &cd->wb_rx_chain[i], NULL) == ENOBUFS)
			return(ENOBUFS);
		if (i == (WB_RX_LIST_CNT - 1)) {
			cd->wb_rx_chain[i].wb_nextdesc = &cd->wb_rx_chain[0];
			ld->wb_rx_list[i].wb_next = 
					vtophys(&ld->wb_rx_list[0]);
		} else {
			cd->wb_rx_chain[i].wb_nextdesc =
					&cd->wb_rx_chain[i + 1];
			ld->wb_rx_list[i].wb_next =
					vtophys(&ld->wb_rx_list[i + 1]);
		}
	}

	cd->wb_rx_head = &cd->wb_rx_chain[0];

	return(0);
}

void
wb_bfree(buf, size, arg)
	caddr_t			buf;
	u_int			size;
	void *arg;
{
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */
int
wb_newbuf(sc, c, m)
	struct wb_softc *sc;
	struct wb_chain_onefrag *c;
	struct mbuf *m;
{
	struct mbuf		*m_new = NULL;

	if (m == NULL) {
		MGETHDR(m_new, M_DONTWAIT, MT_DATA);
		if (m_new == NULL)
			return(ENOBUFS);
		m_new->m_data = m_new->m_ext.ext_buf = c->wb_buf;
		m_new->m_flags |= M_EXT;
		m_new->m_ext.ext_size = m_new->m_pkthdr.len =
		    m_new->m_len = WB_BUFBYTES;
		m_new->m_ext.ext_free = wb_bfree;
		m_new->m_ext.ext_arg = NULL;
		MCLINITREFERENCE(m_new);
	} else {
		m_new = m;
		m_new->m_len = m_new->m_pkthdr.len = WB_BUFBYTES;
		m_new->m_data = m_new->m_ext.ext_buf;
	}

	m_adj(m_new, sizeof(u_int64_t));

	c->wb_mbuf = m_new;
	c->wb_ptr->wb_data = vtophys(mtod(m_new, caddr_t));
	c->wb_ptr->wb_ctl = WB_RXCTL_RLINK | 1536;
	c->wb_ptr->wb_status = WB_RXSTAT;

	return(0);
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
void wb_rxeof(sc)
	struct wb_softc		*sc;
{
        struct mbuf		*m = NULL;
        struct ifnet		*ifp;
	struct wb_chain_onefrag	*cur_rx;
	int			total_len = 0;
	u_int32_t		rxstat;

	ifp = &sc->arpcom.ac_if;

	while(!((rxstat = sc->wb_cdata.wb_rx_head->wb_ptr->wb_status) &
							WB_RXSTAT_OWN)) {
		struct mbuf *m0 = NULL;

		cur_rx = sc->wb_cdata.wb_rx_head;
		sc->wb_cdata.wb_rx_head = cur_rx->wb_nextdesc;

		m = cur_rx->wb_mbuf;

		if ((rxstat & WB_RXSTAT_MIIERR) ||
		    (WB_RXBYTES(cur_rx->wb_ptr->wb_status) < WB_MIN_FRAMELEN) ||
		    (WB_RXBYTES(cur_rx->wb_ptr->wb_status) > 1536) ||
		    !(rxstat & WB_RXSTAT_LASTFRAG) ||
		    !(rxstat & WB_RXSTAT_RXCMP)) {
			ifp->if_ierrors++;
			wb_newbuf(sc, cur_rx, m);
			printf("%s: receiver babbling: possible chip "
				"bug, forcing reset\n", sc->sc_dev.dv_xname);
			wb_fixmedia(sc);
			wb_reset(sc);
			wb_init(sc);
			return;
		}

		if (rxstat & WB_RXSTAT_RXERR) {
			ifp->if_ierrors++;
			wb_newbuf(sc, cur_rx, m);
			break;
		}

		/* No errors; receive the packet. */	
		total_len = WB_RXBYTES(cur_rx->wb_ptr->wb_status);

		/*
		 * XXX The Winbond chip includes the CRC with every
		 * received frame, and there's no way to turn this
		 * behavior off (at least, I can't find anything in
	 	 * the manual that explains how to do it) so we have
		 * to trim off the CRC manually.
		 */
		total_len -= ETHER_CRC_LEN;

		m0 = m_devget(mtod(m, char *) - ETHER_ALIGN,
		    total_len + ETHER_ALIGN, 0, ifp, NULL);
		wb_newbuf(sc, cur_rx, m);
		if (m0 == NULL) {
			ifp->if_ierrors++;
			break;
		}
		m_adj(m0, ETHER_ALIGN);
		m = m0;

		ifp->if_ipackets++;

#if NBPFILTER > 0
		/*
		 * Handle BPF listeners. Let the BPF user see the packet.
		 */
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, m);
#endif
		/* pass it on. */
		ether_input_mbuf(ifp, m);
	}

	return;
}

void wb_rxeoc(sc)
	struct wb_softc		*sc;
{
	wb_rxeof(sc);

	WB_CLRBIT(sc, WB_NETCFG, WB_NETCFG_RX_ON);
	CSR_WRITE_4(sc, WB_RXADDR, vtophys(&sc->wb_ldata->wb_rx_list[0]));
	WB_SETBIT(sc, WB_NETCFG, WB_NETCFG_RX_ON);
	if (CSR_READ_4(sc, WB_ISR) & WB_RXSTATE_SUSPEND)
		CSR_WRITE_4(sc, WB_RXSTART, 0xFFFFFFFF);

	return;
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */
void wb_txeof(sc)
	struct wb_softc		*sc;
{
	struct wb_chain		*cur_tx;
	struct ifnet		*ifp;

	ifp = &sc->arpcom.ac_if;

	/* Clear the timeout timer. */
	ifp->if_timer = 0;

	if (sc->wb_cdata.wb_tx_head == NULL)
		return;

	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been transmitted.
	 */
	while(sc->wb_cdata.wb_tx_head->wb_mbuf != NULL) {
		u_int32_t		txstat;

		cur_tx = sc->wb_cdata.wb_tx_head;
		txstat = WB_TXSTATUS(cur_tx);

		if ((txstat & WB_TXSTAT_OWN) || txstat == WB_UNSENT)
			break;

		if (txstat & WB_TXSTAT_TXERR) {
			ifp->if_oerrors++;
			if (txstat & WB_TXSTAT_ABORT)
				ifp->if_collisions++;
			if (txstat & WB_TXSTAT_LATECOLL)
				ifp->if_collisions++;
		}

		ifp->if_collisions += (txstat & WB_TXSTAT_COLLCNT) >> 3;

		ifp->if_opackets++;
		m_freem(cur_tx->wb_mbuf);
		cur_tx->wb_mbuf = NULL;

		if (sc->wb_cdata.wb_tx_head == sc->wb_cdata.wb_tx_tail) {
			sc->wb_cdata.wb_tx_head = NULL;
			sc->wb_cdata.wb_tx_tail = NULL;
			break;
		}

		sc->wb_cdata.wb_tx_head = cur_tx->wb_nextdesc;
	}

	return;
}

/*
 * TX 'end of channel' interrupt handler.
 */
void wb_txeoc(sc)
	struct wb_softc		*sc;
{
	struct ifnet		*ifp;

	ifp = &sc->arpcom.ac_if;

	ifp->if_timer = 0;

	if (sc->wb_cdata.wb_tx_head == NULL) {
		ifp->if_flags &= ~IFF_OACTIVE;
		sc->wb_cdata.wb_tx_tail = NULL;
	} else {
		if (WB_TXOWN(sc->wb_cdata.wb_tx_head) == WB_UNSENT) {
			WB_TXOWN(sc->wb_cdata.wb_tx_head) = WB_TXSTAT_OWN;
			ifp->if_timer = 5;
			CSR_WRITE_4(sc, WB_TXSTART, 0xFFFFFFFF);
		}
	}

	return;
}

int wb_intr(arg)
	void			*arg;
{
	struct wb_softc		*sc;
	struct ifnet		*ifp;
	u_int32_t		status;
	int			r = 0;

	sc = arg;
	ifp = &sc->arpcom.ac_if;

	if (!(ifp->if_flags & IFF_UP))
		return (r);

	/* Disable interrupts. */
	CSR_WRITE_4(sc, WB_IMR, 0x00000000);

	for (;;) {

		status = CSR_READ_4(sc, WB_ISR);
		if (status)
			CSR_WRITE_4(sc, WB_ISR, status);

		if ((status & WB_INTRS) == 0)
			break;

		r = 1;

		if ((status & WB_ISR_RX_NOBUF) || (status & WB_ISR_RX_ERR)) {
			ifp->if_ierrors++;
			wb_reset(sc);
			if (status & WB_ISR_RX_ERR)
				wb_fixmedia(sc);
			wb_init(sc);
			continue;
		}

		if (status & WB_ISR_RX_OK)
			wb_rxeof(sc);

		if (status & WB_ISR_RX_IDLE)
			wb_rxeoc(sc);

		if (status & WB_ISR_TX_OK)
			wb_txeof(sc);

		if (status & WB_ISR_TX_NOBUF)
			wb_txeoc(sc);

		if (status & WB_ISR_TX_IDLE) {
			wb_txeof(sc);
			if (sc->wb_cdata.wb_tx_head != NULL) {
				WB_SETBIT(sc, WB_NETCFG, WB_NETCFG_TX_ON);
				CSR_WRITE_4(sc, WB_TXSTART, 0xFFFFFFFF);
			}
		}

		if (status & WB_ISR_TX_UNDERRUN) {
			ifp->if_oerrors++;
			wb_txeof(sc);
			WB_CLRBIT(sc, WB_NETCFG, WB_NETCFG_TX_ON);
			/* Jack up TX threshold */
			sc->wb_txthresh += WB_TXTHRESH_CHUNK;
			WB_CLRBIT(sc, WB_NETCFG, WB_NETCFG_TX_THRESH);
			WB_SETBIT(sc, WB_NETCFG, WB_TXTHRESH(sc->wb_txthresh));
			WB_SETBIT(sc, WB_NETCFG, WB_NETCFG_TX_ON);
		}

		if (status & WB_ISR_BUS_ERR) {
			wb_reset(sc);
			wb_init(sc);
		}

	}

	/* Re-enable interrupts. */
	CSR_WRITE_4(sc, WB_IMR, WB_INTRS);

	if (!IFQ_IS_EMPTY(&ifp->if_snd)) {
		wb_start(ifp);
	}

	return (r);
}

void
wb_tick(xsc)
	void *xsc;
{
	struct wb_softc *sc = xsc;
	int s;

	s = splimp();
	mii_tick(&sc->sc_mii);
	splx(s);
	timeout_add(&sc->wb_tick_tmo, hz);
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
int wb_encap(sc, c, m_head)
	struct wb_softc		*sc;
	struct wb_chain		*c;
	struct mbuf		*m_head;
{
	int			frag = 0;
	struct wb_desc		*f = NULL;
	int			total_len;
	struct mbuf		*m;

	/*
 	 * Start packing the mbufs in this chain into
	 * the fragment pointers. Stop when we run out
 	 * of fragments or hit the end of the mbuf chain.
	 */
	m = m_head;
	total_len = 0;

	for (m = m_head, frag = 0; m != NULL; m = m->m_next) {
		if (m->m_len != 0) {
			if (frag == WB_MAXFRAGS)
				break;
			total_len += m->m_len;
			f = &c->wb_ptr->wb_frag[frag];
			f->wb_ctl = WB_TXCTL_TLINK | m->m_len;
			if (frag == 0) {
				f->wb_ctl |= WB_TXCTL_FIRSTFRAG;
				f->wb_status = 0;
			} else
				f->wb_status = WB_TXSTAT_OWN;
			f->wb_next = vtophys(&c->wb_ptr->wb_frag[frag + 1]);
			f->wb_data = vtophys(mtod(m, vm_offset_t));
			frag++;
		}
	}

	/*
	 * Handle special case: we used up all 16 fragments,
	 * but we have more mbufs left in the chain. Copy the
	 * data into an mbuf cluster. Note that we don't
	 * bother clearing the values in the other fragment
	 * pointers/counters; it wouldn't gain us anything,
	 * and would waste cycles.
	 */
	if (m != NULL) {
		struct mbuf		*m_new = NULL;

		MGETHDR(m_new, M_DONTWAIT, MT_DATA);
		if (m_new == NULL)
			return(1);
		if (m_head->m_pkthdr.len > MHLEN) {
			MCLGET(m_new, M_DONTWAIT);
			if (!(m_new->m_flags & M_EXT)) {
				m_freem(m_new);
				return(1);
			}
		}
		m_copydata(m_head, 0, m_head->m_pkthdr.len,	
					mtod(m_new, caddr_t));
		m_new->m_pkthdr.len = m_new->m_len = m_head->m_pkthdr.len;
		m_freem(m_head);
		m_head = m_new;
		f = &c->wb_ptr->wb_frag[0];
		f->wb_status = 0;
		f->wb_data = vtophys(mtod(m_new, caddr_t));
		f->wb_ctl = total_len = m_new->m_len;
		f->wb_ctl |= WB_TXCTL_TLINK|WB_TXCTL_FIRSTFRAG;
		frag = 1;
	}

	if (total_len < WB_MIN_FRAMELEN) {
		f = &c->wb_ptr->wb_frag[frag];
		f->wb_ctl = WB_MIN_FRAMELEN - total_len;
		f->wb_data = vtophys(&sc->wb_cdata.wb_pad);
		f->wb_ctl |= WB_TXCTL_TLINK;
		f->wb_status = WB_TXSTAT_OWN;
		frag++;
	}

	c->wb_mbuf = m_head;
	c->wb_lastdesc = frag - 1;
	WB_TXCTL(c) |= WB_TXCTL_LASTFRAG;
	WB_TXNEXT(c) = vtophys(&c->wb_nextdesc->wb_ptr->wb_frag[0]);

	return(0);
}

/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit lists. We also save a
 * copy of the pointers since the transmit list fragment pointers are
 * physical addresses.
 */

void wb_start(ifp)
	struct ifnet		*ifp;
{
	struct wb_softc		*sc;
	struct mbuf		*m_head = NULL;
	struct wb_chain		*cur_tx = NULL, *start_tx;

	sc = ifp->if_softc;

	/*
	 * Check for an available queue slot. If there are none,
	 * punt.
	 */
	if (sc->wb_cdata.wb_tx_free->wb_mbuf != NULL) {
		ifp->if_flags |= IFF_OACTIVE;
		return;
	}

	start_tx = sc->wb_cdata.wb_tx_free;

	while(sc->wb_cdata.wb_tx_free->wb_mbuf == NULL) {
		IFQ_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;

		/* Pick a descriptor off the free list. */
		cur_tx = sc->wb_cdata.wb_tx_free;
		sc->wb_cdata.wb_tx_free = cur_tx->wb_nextdesc;

		/* Pack the data into the descriptor. */
		wb_encap(sc, cur_tx, m_head);

		if (cur_tx != start_tx)
			WB_TXOWN(cur_tx) = WB_TXSTAT_OWN;

#if NBPFILTER > 0
		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, cur_tx->wb_mbuf);
#endif
	}

	/*
	 * If there are no packets queued, bail.
	 */
	if (cur_tx == NULL)
		return;

	/*
	 * Place the request for the upload interrupt
	 * in the last descriptor in the chain. This way, if
	 * we're chaining several packets at once, we'll only
	 * get an interupt once for the whole chain rather than
	 * once for each packet.
	 */
	WB_TXCTL(cur_tx) |= WB_TXCTL_FINT;
	cur_tx->wb_ptr->wb_frag[0].wb_ctl |= WB_TXCTL_FINT;
	sc->wb_cdata.wb_tx_tail = cur_tx;

	if (sc->wb_cdata.wb_tx_head == NULL) {
		sc->wb_cdata.wb_tx_head = start_tx;
		WB_TXOWN(start_tx) = WB_TXSTAT_OWN;
		CSR_WRITE_4(sc, WB_TXSTART, 0xFFFFFFFF);
	} else {
		/*
		 * We need to distinguish between the case where
		 * the own bit is clear because the chip cleared it
		 * and where the own bit is clear because we haven't
		 * set it yet. The magic value WB_UNSET is just some
		 * ramdomly chosen number which doesn't have the own
	 	 * bit set. When we actually transmit the frame, the
		 * status word will have _only_ the own bit set, so
		 * the txeoc handler will be able to tell if it needs
		 * to initiate another transmission to flush out pending
		 * frames.
		 */
		WB_TXOWN(start_tx) = WB_UNSENT;
	}

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 5;

	return;
}

void wb_init(xsc)
	void			*xsc;
{
	struct wb_softc *sc = xsc;
	struct ifnet *ifp = &sc->arpcom.ac_if;
	int s, i;

	s = splimp();

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	wb_stop(sc);
	wb_reset(sc);

	sc->wb_txthresh = WB_TXTHRESH_INIT;

	/*
	 * Set cache alignment and burst length.
	 */
#ifdef foo
	CSR_WRITE_4(sc, WB_BUSCTL, WB_BUSCTL_CONFIG);
	WB_CLRBIT(sc, WB_NETCFG, WB_NETCFG_TX_THRESH);
	WB_SETBIT(sc, WB_NETCFG, WB_TXTHRESH(sc->wb_txthresh));
#endif

	CSR_WRITE_4(sc, WB_BUSCTL, WB_BUSCTL_MUSTBEONE|WB_BUSCTL_ARBITRATION);
	WB_SETBIT(sc, WB_BUSCTL, WB_BURSTLEN_16LONG);
	switch(sc->wb_cachesize) {
	case 32:
		WB_SETBIT(sc, WB_BUSCTL, WB_CACHEALIGN_32LONG);
		break;
	case 16:
		WB_SETBIT(sc, WB_BUSCTL, WB_CACHEALIGN_16LONG);
		break;
	case 8:
		WB_SETBIT(sc, WB_BUSCTL, WB_CACHEALIGN_8LONG);
		break;
	case 0:
	default:
		WB_SETBIT(sc, WB_BUSCTL, WB_CACHEALIGN_NONE);
		break;
	}

	/* This doesn't tend to work too well at 100Mbps. */
	WB_CLRBIT(sc, WB_NETCFG, WB_NETCFG_TX_EARLY_ON);

	/* Init our MAC address */
	for (i = 0; i < ETHER_ADDR_LEN; i++) {
		CSR_WRITE_1(sc, WB_NODE0 + i, sc->arpcom.ac_enaddr[i]);
	}

	/* Init circular RX list. */
	if (wb_list_rx_init(sc) == ENOBUFS) {
		printf("%s: initialization failed: no "
			"memory for rx buffers\n", sc->sc_dev.dv_xname);
		wb_stop(sc);
		(void)splx(s);
		return;
	}

	/* Init TX descriptors. */
	wb_list_tx_init(sc);

	/* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC) {
		WB_SETBIT(sc, WB_NETCFG, WB_NETCFG_RX_ALLPHYS);
	} else {
		WB_CLRBIT(sc, WB_NETCFG, WB_NETCFG_RX_ALLPHYS);
	}

	/*
	 * Set capture broadcast bit to capture broadcast frames.
	 */
	if (ifp->if_flags & IFF_BROADCAST) {
		WB_SETBIT(sc, WB_NETCFG, WB_NETCFG_RX_BROAD);
	} else {
		WB_CLRBIT(sc, WB_NETCFG, WB_NETCFG_RX_BROAD);
	}

	/*
	 * Program the multicast filter, if necessary.
	 */
	wb_setmulti(sc);

	/*
	 * Load the address of the RX list.
	 */
	WB_CLRBIT(sc, WB_NETCFG, WB_NETCFG_RX_ON);
	CSR_WRITE_4(sc, WB_RXADDR, vtophys(&sc->wb_ldata->wb_rx_list[0]));

	/*
	 * Enable interrupts.
	 */
	CSR_WRITE_4(sc, WB_IMR, WB_INTRS);
	CSR_WRITE_4(sc, WB_ISR, 0xFFFFFFFF);

	/* Enable receiver and transmitter. */
	WB_SETBIT(sc, WB_NETCFG, WB_NETCFG_RX_ON);
	CSR_WRITE_4(sc, WB_RXSTART, 0xFFFFFFFF);

	WB_CLRBIT(sc, WB_NETCFG, WB_NETCFG_TX_ON);
	CSR_WRITE_4(sc, WB_TXADDR, vtophys(&sc->wb_ldata->wb_tx_list[0]));
	WB_SETBIT(sc, WB_NETCFG, WB_NETCFG_TX_ON);

	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;

	(void)splx(s);

	timeout_set(&sc->wb_tick_tmo, wb_tick, sc);
	timeout_add(&sc->wb_tick_tmo, hz);

	return;
}

/*
 * Set media options.
 */
int
wb_ifmedia_upd(ifp)
	struct ifnet *ifp;
{
	struct wb_softc *sc = ifp->if_softc;

	if (ifp->if_flags & IFF_UP)
		wb_init(sc);

	return(0);
}

/*
 * Report current media status.
 */
void
wb_ifmedia_sts(ifp, ifmr)
	struct ifnet		*ifp;
	struct ifmediareq	*ifmr;
{
	struct wb_softc *sc = ifp->if_softc;
	struct mii_data *mii = &sc->sc_mii;

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

int wb_ioctl(ifp, command, data)
	struct ifnet		*ifp;
	u_long			command;
	caddr_t			data;
{
	struct wb_softc		*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *) data;
	struct ifaddr		*ifa = (struct ifaddr *)data;
	int			s, error = 0;

	s = splimp();

	if ((error = ether_ioctl(ifp, &sc->arpcom, command, data)) > 0) {
		splx(s);
		return (error);
	}

	switch(command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		switch (ifa->ifa_addr->sa_family) {
#ifdef INET
		case AF_INET:
			wb_init(sc);
			arp_ifinit(&sc->arpcom, ifa);
			break;
#endif /* INET */
		default:
			wb_init(sc);
		}
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			wb_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				wb_stop(sc);
		}
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
			wb_setmulti(sc);
			error = 0;
		}
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		error = ifmedia_ioctl(ifp, ifr, &sc->sc_mii.mii_media, command);
		break;
	default:
		error = EINVAL;
		break;
	}

	(void)splx(s);

	return(error);
}

void wb_watchdog(ifp)
	struct ifnet		*ifp;
{
	struct wb_softc		*sc;

	sc = ifp->if_softc;

	ifp->if_oerrors++;
	printf("%s: watchdog timeout\n", sc->sc_dev.dv_xname);

#ifdef foo
	if (!(wb_phy_readreg(sc, PHY_BMSR) & PHY_BMSR_LINKSTAT))
		printf("%s: no carrier - transceiver cable problem?\n",
		    sc->sc_dev.dv_xname);
#endif
	wb_stop(sc);
	wb_reset(sc);
	wb_init(sc);

	if (!IFQ_IS_EMPTY(&ifp->if_snd))
		wb_start(ifp);

	return;
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
void wb_stop(sc)
	struct wb_softc		*sc;
{
	register int		i;
	struct ifnet		*ifp;

	ifp = &sc->arpcom.ac_if;
	ifp->if_timer = 0;

	timeout_del(&sc->wb_tick_tmo);

	WB_CLRBIT(sc, WB_NETCFG, (WB_NETCFG_RX_ON|WB_NETCFG_TX_ON));
	CSR_WRITE_4(sc, WB_IMR, 0x00000000);
	CSR_WRITE_4(sc, WB_TXADDR, 0x00000000);
	CSR_WRITE_4(sc, WB_RXADDR, 0x00000000);

	/*
	 * Free data in the RX lists.
	 */
	for (i = 0; i < WB_RX_LIST_CNT; i++) {
		if (sc->wb_cdata.wb_rx_chain[i].wb_mbuf != NULL) {
			m_freem(sc->wb_cdata.wb_rx_chain[i].wb_mbuf);
			sc->wb_cdata.wb_rx_chain[i].wb_mbuf = NULL;
		}
	}
	bzero((char *)&sc->wb_ldata->wb_rx_list,
		sizeof(sc->wb_ldata->wb_rx_list));

	/*
	 * Free the TX list buffers.
	 */
	for (i = 0; i < WB_TX_LIST_CNT; i++) {
		if (sc->wb_cdata.wb_tx_chain[i].wb_mbuf != NULL) {
			m_freem(sc->wb_cdata.wb_tx_chain[i].wb_mbuf);
			sc->wb_cdata.wb_tx_chain[i].wb_mbuf = NULL;
		}
	}

	bzero((char *)&sc->wb_ldata->wb_tx_list,
		sizeof(sc->wb_ldata->wb_tx_list));

	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);

	return;
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
void wb_shutdown(arg)
	void			*arg;
{
	struct wb_softc		*sc = (struct wb_softc *)arg;

	wb_stop(sc);

	return;
}

struct cfattach wb_ca = {
	sizeof(struct wb_softc), wb_probe, wb_attach
};

struct cfdriver wb_cd = {
	0, "wb", DV_IFNET
};
