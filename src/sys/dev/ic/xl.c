/*	$OpenBSD: xl.c,v 1.26 2001/07/02 01:28:21 jason Exp $	*/

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
 * $FreeBSD: if_xl.c,v 1.77 2000/08/28 20:40:03 wpaul Exp $
 */

/*
 * 3Com 3c90x Etherlink XL PCI NIC driver
 *
 * Supports the 3Com "boomerang", "cyclone", and "hurricane" PCI
 * bus-master chips (3c90x cards and embedded controllers) including
 * the following:
 *
 * 3Com 3c900-TPO	10Mbps/RJ-45
 * 3Com 3c900-COMBO	10Mbps/RJ-45,AUI,BNC
 * 3Com 3c905-TX	10/100Mbps/RJ-45
 * 3Com 3c905-T4	10/100Mbps/RJ-45
 * 3Com 3c900B-TPO	10Mbps/RJ-45
 * 3Com 3c900B-COMBO	10Mbps/RJ-45,AUI,BNC
 * 3Com 3c900B-TPC	10Mbps/RJ-45,BNC
 * 3Com 3c900B-FL	10Mbps/Fiber-optic
 * 3Com 3c905B-COMBO	10/100Mbps/RJ-45,AUI,BNC
 * 3Com 3c905B-TX	10/100Mbps/RJ-45
 * 3Com 3c900-FL/FX	10/100Mbps/Fiber-optic
 * 3Com 3c905C-TX	10/100Mbps/RJ-45 (Tornado ASIC)
 * 3Com 3c450-TX	10/100Mbps/RJ-45 (Tornado ASIC)
 * 3Com 3c555		10/100Mbps/RJ-45 (MiniPCI, Hurricane ASIC)
 * 3Com 3c556		10/100Mbps/RJ-45 (MiniPCI, Hurricane ASIC)
 * 3Com 3c556B		10/100Mbps/RJ-45 (MiniPCI, Hurricane ASIC)
 * 3Com 3c980-TX	10/100Mbps server adapter (Hurricane ASIC)
 * 3Com 3c980C-TX	10/100Mbps server adapter (Tornado ASIC)
 * 3Com 3C575TX		10/100Mbps LAN CardBus PC Card
 * 3Com 3CCFE575BT	10/100Mbps LAN CardBus PC Card
 * 3Com 3CCFE575CT	10/100Mbps LAN CardBus PC Card
 * 3Com 3C3FE575CT	10/100Mbps LAN CardBus Type III PC Card
 * 3Com 3CCFEM656	10/100Mbps LAN+56k Modem CardBus PC Card
 * 3Com 3CCFEM656B	10/100Mbps LAN+56k Modem CardBus PC Card
 * 3Com 3CCFEM656C	10/100Mbps LAN+56k Global Modem CardBus PC Card
 * 3Com 3C3FEM656C	10/100Mbps LAN+56k Global Modem CardBus Type III PC Card
 * 3Com 3cSOHO100-TX	10/100Mbps/RJ-45 (Hurricane ASIC)
 * Dell Optiplex GX1 on-board 3c918 10/100Mbps/RJ-45
 * Dell on-board 3c920	10/100Mbps/RJ-45
 * Dell Precision on-board 3c905B 10/100Mbps/RJ-45
 * Dell Latitude laptop docking station embedded 3c905-TX
 *
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */

/*
 * The 3c90x series chips use a bus-master DMA interface for transfering
 * packets to and from the controller chip. Some of the "vortex" cards
 * (3c59x) also supported a bus master mode, however for those chips
 * you could only DMA packets to/from a contiguous memory buffer. For
 * transmission this would mean copying the contents of the queued mbuf
 * chain into a an mbuf cluster and then DMAing the cluster. This extra
 * copy would sort of defeat the purpose of the bus master support for
 * any packet that doesn't fit into a single mbuf.
 *
 * By contrast, the 3c90x cards support a fragment-based bus master
 * mode where mbuf chains can be encapsulated using TX descriptors.
 * This is similar to other PCI chips such as the Texas Instruments
 * ThunderLAN and the Intel 82557/82558.
 *
 * The "vortex" driver (if_vx.c) happens to work for the "boomerang"
 * bus master chips because they maintain the old PIO interface for
 * backwards compatibility, but starting with the 3c905B and the
 * "cyclone" chips, the compatibility interface has been dropped.
 * Since using bus master DMA is a big win, we use this driver to
 * support the PCI "boomerang" chips even though they work with the
 * "vortex" driver in order to obtain better performance.
 *
 * This driver is in the /sys/pci directory because it only supports
 * PCI-based NICs.
 */

#include "bpfilter.h"
#include "vlan.h"

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

#include <machine/bus.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#include <vm/vm.h>              /* for vtophys */
#include <vm/pmap.h>            /* for vtophys */

#include <dev/ic/xlreg.h>

int xl_newbuf		__P((struct xl_softc *, struct xl_chain_onefrag *));
void xl_stats_update	__P((void *));
int xl_encap		__P((struct xl_softc *, struct xl_chain *,
    struct mbuf * ));
int xl_encap_90xB	__P((struct xl_softc *, struct xl_chain *,
    struct mbuf * ));
void xl_rxeof		__P((struct xl_softc *));
int xl_rx_resync	__P((struct xl_softc *));
void xl_txeof		__P((struct xl_softc *));
void xl_txeof_90xB	__P((struct xl_softc *));
void xl_txeoc		__P((struct xl_softc *));
int xl_intr		__P((void *));
void xl_start		__P((struct ifnet *));
void xl_start_90xB	__P((struct ifnet *));
int xl_ioctl		__P((struct ifnet *, u_long, caddr_t));
void xl_init		__P((void *));
void xl_stop		__P((struct xl_softc *));
void xl_freetxrx	__P((struct xl_softc *));
void xl_watchdog	__P((struct ifnet *));
void xl_shutdown	__P((void *));
int xl_ifmedia_upd	__P((struct ifnet *));
void xl_ifmedia_sts	__P((struct ifnet *, struct ifmediareq *));

int xl_eeprom_wait	__P((struct xl_softc *));
int xl_read_eeprom	__P((struct xl_softc *, caddr_t, int, int, int));
void xl_mii_sync	__P((struct xl_softc *));
void xl_mii_send	__P((struct xl_softc *, u_int32_t, int));
int xl_mii_readreg	__P((struct xl_softc *, struct xl_mii_frame *));
int xl_mii_writereg	__P((struct xl_softc *, struct xl_mii_frame *));

void xl_setcfg		__P((struct xl_softc *));
void xl_setmode		__P((struct xl_softc *, int));
u_int8_t xl_calchash	__P((caddr_t));
void xl_setmulti	__P((struct xl_softc *));
void xl_setmulti_hash	__P((struct xl_softc *));
void xl_reset		__P((struct xl_softc *, int));
int xl_list_rx_init	__P((struct xl_softc *));
int xl_list_tx_init	__P((struct xl_softc *));
int xl_list_tx_init_90xB	__P((struct xl_softc *));
void xl_wait		__P((struct xl_softc *));
void xl_mediacheck	__P((struct xl_softc *));
void xl_choose_xcvr	__P((struct xl_softc *, int));
#ifdef notdef
void xl_testpacket	__P((struct xl_softc *));
#endif

int xl_miibus_readreg	__P((struct device *, int, int));
void xl_miibus_writereg	__P((struct device *, int, int, int));
void xl_miibus_statchg	__P((struct device *));

/*
 * Murphy's law says that it's possible the chip can wedge and
 * the 'command in progress' bit may never clear. Hence, we wait
 * only a finite amount of time to avoid getting caught in an
 * infinite loop. Normally this delay routine would be a macro,
 * but it isn't called during normal operation so we can afford
 * to make it a function.
 */
void xl_wait(sc)
	struct xl_softc		*sc;
{
	register int		i;

	for (i = 0; i < XL_TIMEOUT; i++) {
		if (!(CSR_READ_2(sc, XL_STATUS) & XL_STAT_CMDBUSY))
			break;
	}

#ifdef DIAGNOSTIC
	if (i == XL_TIMEOUT)
		printf("xl%d: command never completed!\n", sc->xl_unit);
#endif

	return;
}

/*
 * MII access routines are provided for adapters with external
 * PHYs (3c905-TX, 3c905-T4, 3c905B-T4) and those with built-in
 * autoneg logic that's faked up to look like a PHY (3c905B-TX).
 * Note: if you don't perform the MDIO operations just right,
 * it's possible to end up with code that works correctly with
 * some chips/CPUs/processor speeds/bus speeds/etc but not
 * with others.
 */
#define MII_SET(x)					\
	CSR_WRITE_2(sc, XL_W4_PHY_MGMT,			\
		CSR_READ_2(sc, XL_W4_PHY_MGMT) | x)

#define MII_CLR(x)					\
	CSR_WRITE_2(sc, XL_W4_PHY_MGMT,			\
		CSR_READ_2(sc, XL_W4_PHY_MGMT) & ~x)

/*
 * Sync the PHYs by setting data bit and strobing the clock 32 times.
 */
void xl_mii_sync(sc)
	struct xl_softc		*sc;
{
	register int		i;

	XL_SEL_WIN(4);
	MII_SET(XL_MII_DIR|XL_MII_DATA);

	for (i = 0; i < 32; i++) {
		MII_SET(XL_MII_CLK);
		DELAY(1);
		MII_CLR(XL_MII_CLK);
		DELAY(1);
	}

	return;
}

/*
 * Clock a series of bits through the MII.
 */
void xl_mii_send(sc, bits, cnt)
	struct xl_softc		*sc;
	u_int32_t		bits;
	int			cnt;
{
	int			i;

	XL_SEL_WIN(4);
	MII_CLR(XL_MII_CLK);

	for (i = (0x1 << (cnt - 1)); i; i >>= 1) {
                if (bits & i) {
			MII_SET(XL_MII_DATA);
                } else {
			MII_CLR(XL_MII_DATA);
                }
		DELAY(1);
		MII_CLR(XL_MII_CLK);
		DELAY(1);
		MII_SET(XL_MII_CLK);
	}
}

/*
 * Read an PHY register through the MII.
 */
int xl_mii_readreg(sc, frame)
	struct xl_softc		*sc;
	struct xl_mii_frame	*frame;
	
{
	int			i, ack, s;

	s = splimp();

	/*
	 * Set up frame for RX.
	 */
	frame->mii_stdelim = XL_MII_STARTDELIM;
	frame->mii_opcode = XL_MII_READOP;
	frame->mii_turnaround = 0;
	frame->mii_data = 0;
	
	/*
	 * Select register window 4.
	 */

	XL_SEL_WIN(4);

	CSR_WRITE_2(sc, XL_W4_PHY_MGMT, 0);
	/*
 	 * Turn on data xmit.
	 */
	MII_SET(XL_MII_DIR);

	xl_mii_sync(sc);

	/*
	 * Send command/address info.
	 */
	xl_mii_send(sc, frame->mii_stdelim, 2);
	xl_mii_send(sc, frame->mii_opcode, 2);
	xl_mii_send(sc, frame->mii_phyaddr, 5);
	xl_mii_send(sc, frame->mii_regaddr, 5);

	/* Idle bit */
	MII_CLR((XL_MII_CLK|XL_MII_DATA));
	DELAY(1);
	MII_SET(XL_MII_CLK);
	DELAY(1);

	/* Turn off xmit. */
	MII_CLR(XL_MII_DIR);

	/* Check for ack */
	MII_CLR(XL_MII_CLK);
	DELAY(1);
	MII_SET(XL_MII_CLK);
	DELAY(1);
	ack = CSR_READ_2(sc, XL_W4_PHY_MGMT) & XL_MII_DATA;

	/*
	 * Now try reading data bits. If the ack failed, we still
	 * need to clock through 16 cycles to keep the PHY(s) in sync.
	 */
	if (ack) {
		for(i = 0; i < 16; i++) {
			MII_CLR(XL_MII_CLK);
			DELAY(1);
			MII_SET(XL_MII_CLK);
			DELAY(1);
		}
		goto fail;
	}

	for (i = 0x8000; i; i >>= 1) {
		MII_CLR(XL_MII_CLK);
		DELAY(1);
		if (!ack) {
			if (CSR_READ_2(sc, XL_W4_PHY_MGMT) & XL_MII_DATA)
				frame->mii_data |= i;
			DELAY(1);
		}
		MII_SET(XL_MII_CLK);
		DELAY(1);
	}

fail:

	MII_CLR(XL_MII_CLK);
	DELAY(1);
	MII_SET(XL_MII_CLK);
	DELAY(1);

	splx(s);

	if (ack)
		return(1);
	return(0);
}

/*
 * Write to a PHY register through the MII.
 */
int xl_mii_writereg(sc, frame)
	struct xl_softc		*sc;
	struct xl_mii_frame	*frame;
	
{
	int			s;

	s = splimp();
	/*
	 * Set up frame for TX.
	 */

	frame->mii_stdelim = XL_MII_STARTDELIM;
	frame->mii_opcode = XL_MII_WRITEOP;
	frame->mii_turnaround = XL_MII_TURNAROUND;
	
	/*
	 * Select the window 4.
	 */
	XL_SEL_WIN(4);

	/*
 	 * Turn on data output.
	 */
	MII_SET(XL_MII_DIR);

	xl_mii_sync(sc);

	xl_mii_send(sc, frame->mii_stdelim, 2);
	xl_mii_send(sc, frame->mii_opcode, 2);
	xl_mii_send(sc, frame->mii_phyaddr, 5);
	xl_mii_send(sc, frame->mii_regaddr, 5);
	xl_mii_send(sc, frame->mii_turnaround, 2);
	xl_mii_send(sc, frame->mii_data, 16);

	/* Idle bit. */
	MII_SET(XL_MII_CLK);
	DELAY(1);
	MII_CLR(XL_MII_CLK);
	DELAY(1);

	/*
	 * Turn off xmit.
	 */
	MII_CLR(XL_MII_DIR);

	splx(s);

	return(0);
}

int
xl_miibus_readreg(self, phy, reg)
	struct device *self;
	int phy, reg;
{
	struct xl_softc *sc = (struct xl_softc *)self;
	struct xl_mii_frame	frame;

	if (!(sc->xl_flags & XL_FLAG_PHYOK) && phy != 24)
		return (0);

	bzero((char *)&frame, sizeof(frame));

	frame.mii_phyaddr = phy;
	frame.mii_regaddr = reg;
	xl_mii_readreg(sc, &frame);

	return(frame.mii_data);
}

void
xl_miibus_writereg(self, phy, reg, data)
	struct device *self;
	int phy, reg, data;
{
	struct xl_softc *sc = (struct xl_softc *)self;
	struct xl_mii_frame	frame;

	if (!(sc->xl_flags & XL_FLAG_PHYOK) && phy != 24)
		return;

	bzero((char *)&frame, sizeof(frame));

	frame.mii_phyaddr = phy;
	frame.mii_regaddr = reg;
	frame.mii_data = data;

	xl_mii_writereg(sc, &frame);
}

void
xl_miibus_statchg(self)
	struct device *self;
{
	struct xl_softc *sc = (struct xl_softc *)self;

	xl_setcfg(sc);

	XL_SEL_WIN(3);
	if ((sc->sc_mii.mii_media_active & IFM_GMASK) == IFM_FDX)
		CSR_WRITE_1(sc, XL_W3_MAC_CTRL, XL_MACCTRL_DUPLEX);
	else
		CSR_WRITE_1(sc, XL_W3_MAC_CTRL,
		    (CSR_READ_1(sc, XL_W3_MAC_CTRL) & ~XL_MACCTRL_DUPLEX));
}

/*
 * The EEPROM is slow: give it time to come ready after issuing
 * it a command.
 */
int xl_eeprom_wait(sc)
	struct xl_softc		*sc;
{
	int			i;

	for (i = 0; i < 100; i++) {
		if (CSR_READ_2(sc, XL_W0_EE_CMD) & XL_EE_BUSY)
			DELAY(162);
		else
			break;
	}

	if (i == 100) {
		printf("xl%d: eeprom failed to come ready\n", sc->xl_unit);
		return(1);
	}

	return(0);
}

/*
 * Read a sequence of words from the EEPROM. Note that ethernet address
 * data is stored in the EEPROM in network byte order.
 */
int xl_read_eeprom(sc, dest, off, cnt, swap)
	struct xl_softc		*sc;
	caddr_t			dest;
	int			off;
	int			cnt;
	int			swap;
{
	int			err = 0, i;
	u_int16_t		word = 0, *ptr;
#define EEPROM_5BIT_OFFSET(A) ((((A) << 2) & 0x7F00) | ((A) & 0x003F))
	/* WARNING! DANGER!
	 * It's easy to accidentally overwrite the rom content!
	 * Note: the 3c575 uses 8bit EEPROM offsets.
	 */
	XL_SEL_WIN(0);

	if (xl_eeprom_wait(sc))
		return(1);

	if (sc->xl_flags & XL_FLAG_EEPROM_OFFSET_30)
		off += 0x30;

	for (i = 0; i < cnt; i++) {
		if (sc->xl_flags & XL_FLAG_8BITROM)
			CSR_WRITE_2(sc, XL_W0_EE_CMD, (2<<8) | (off + i ));
		else
			CSR_WRITE_2(sc, XL_W0_EE_CMD,
			    XL_EE_READ | EEPROM_5BIT_OFFSET(off + i));
		err = xl_eeprom_wait(sc);
		if (err)
			break;
		word = CSR_READ_2(sc, XL_W0_EE_DATA);
		ptr = (u_int16_t *)(dest + (i * 2));
		if (swap)
			*ptr = ntohs(word);
		else
			*ptr = word;	
	}

	return(err ? 1 : 0);
}

/*
 * This routine is taken from the 3Com Etherlink XL manual,
 * page 10-7. It calculates a CRC of the supplied multicast
 * group address and returns the lower 8 bits, which are used
 * as the multicast filter position.
 * Note: the 3c905B currently only supports a 64-bit hash table,
 * which means we really only need 6 bits, but the manual indicates
 * that future chip revisions will have a 256-bit hash table,
 * hence the routine is set up to calculate 8 bits of position
 * info in case we need it some day.
 * Note II, The Sequel: _CURRENT_ versions of the 3c905B have a
 * 256 bit hash table. This means we have to use all 8 bits regardless.
 * On older cards, the upper 2 bits will be ignored. Grrrr....
 */
u_int8_t xl_calchash(addr)
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
	return(crc & 0x000000FF);
}

/*
 * NICs older than the 3c905B have only one multicast option, which
 * is to enable reception of all multicast frames.
 */
void xl_setmulti(sc)
	struct xl_softc		*sc;
{
	struct ifnet		*ifp;
	struct arpcom *ac = &sc->arpcom;
	struct ether_multi *enm;
	struct ether_multistep step;
	u_int8_t		rxfilt;
	int			mcnt = 0;

	ifp = &sc->arpcom.ac_if;

	XL_SEL_WIN(5);
	rxfilt = CSR_READ_1(sc, XL_W5_RX_FILTER);

	if (ifp->if_flags & IFF_ALLMULTI) {
		rxfilt |= XL_RXFILTER_ALLMULTI;
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_SET_FILT|rxfilt);
		return;
	}

	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		mcnt++;
		ETHER_NEXT_MULTI(step, enm);
	}

	if (mcnt)
		rxfilt |= XL_RXFILTER_ALLMULTI;
	else
		rxfilt &= ~XL_RXFILTER_ALLMULTI;

	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_SET_FILT|rxfilt);

	return;
}

/*
 * 3c905B adapters have a hash filter that we can program.
 */
void xl_setmulti_hash(sc)
	struct xl_softc		*sc;
{
	struct ifnet		*ifp;
	int			h = 0, i;
	struct arpcom *ac = &sc->arpcom;
	struct ether_multi *enm;
	struct ether_multistep step;
	u_int8_t		rxfilt;
	int			mcnt = 0;

	ifp = &sc->arpcom.ac_if;

	XL_SEL_WIN(5);
	rxfilt = CSR_READ_1(sc, XL_W5_RX_FILTER);

	if (ifp->if_flags & IFF_ALLMULTI) {
allmulti:
		rxfilt |= XL_RXFILTER_ALLMULTI;
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_SET_FILT|rxfilt);
		return;
	} else
		rxfilt &= ~XL_RXFILTER_ALLMULTI;


	/* first, zot all the existing hash bits */
	for (i = 0; i < XL_HASHFILT_SIZE; i++)
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_SET_HASH|i);

	/* now program new ones */
	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		if (bcmp(enm->enm_addrlo, enm->enm_addrhi, ETHER_ADDR_LEN)) {
			ifp->if_flags |= IFF_ALLMULTI;
			goto allmulti;
		}
		h = xl_calchash(enm->enm_addrlo);
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_SET_HASH|XL_HASH_SET|h);
		mcnt++;
		ETHER_NEXT_MULTI(step, enm);
	}

	if (mcnt)
		rxfilt |= XL_RXFILTER_MULTIHASH;
	else
		rxfilt &= ~XL_RXFILTER_MULTIHASH;

	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_SET_FILT|rxfilt);

	return;
}

#ifdef notdef
void xl_testpacket(sc)
	struct xl_softc		*sc;
{
	struct mbuf		*m;
	struct ifnet		*ifp;
	int			error;

	ifp = &sc->arpcom.ac_if;

	MGETHDR(m, M_DONTWAIT, MT_DATA);

	if (m == NULL)
		return;

	bcopy(&sc->arpcom.ac_enaddr,
		mtod(m, struct ether_header *)->ether_dhost, ETHER_ADDR_LEN);
	bcopy(&sc->arpcom.ac_enaddr,
		mtod(m, struct ether_header *)->ether_shost, ETHER_ADDR_LEN);
	mtod(m, struct ether_header *)->ether_type = htons(3);
	mtod(m, unsigned char *)[14] = 0;
	mtod(m, unsigned char *)[15] = 0;
	mtod(m, unsigned char *)[16] = 0xE3;
	m->m_len = m->m_pkthdr.len = sizeof(struct ether_header) + 3;
	IFQ_ENQUEUE(&ifp->if_snd, m, NULL, error);
	xl_start(ifp);

	return;
}
#endif

void xl_setcfg(sc)
	struct xl_softc *sc;
{
	u_int32_t icfg;

	XL_SEL_WIN(3);
	icfg = CSR_READ_4(sc, XL_W3_INTERNAL_CFG);
	icfg &= ~XL_ICFG_CONNECTOR_MASK;
	if (sc->xl_media & XL_MEDIAOPT_MII ||
	    sc->xl_media & XL_MEDIAOPT_BT4)
		icfg |= (XL_XCVR_MII << XL_ICFG_CONNECTOR_BITS);
	if (sc->xl_media & XL_MEDIAOPT_BTX)
		icfg |= (XL_XCVR_AUTO << XL_ICFG_CONNECTOR_BITS);

	CSR_WRITE_4(sc, XL_W3_INTERNAL_CFG, icfg);
	CSR_WRITE_4(sc, XL_COMMAND, XL_CMD_COAX_STOP);
}

void xl_setmode(sc, media)
	struct xl_softc *sc;
	int media;
{
	u_int32_t icfg;
	u_int16_t mediastat;

	printf("xl%d: selecting ", sc->xl_unit);

	XL_SEL_WIN(4);
	mediastat = CSR_READ_2(sc, XL_W4_MEDIA_STATUS);
	XL_SEL_WIN(3);
	icfg = CSR_READ_4(sc, XL_W3_INTERNAL_CFG);

	if (sc->xl_media & XL_MEDIAOPT_BT) {
		if (IFM_SUBTYPE(media) == IFM_10_T) {
			printf("10baseT transceiver, ");
			sc->xl_xcvr = XL_XCVR_10BT;
			icfg &= ~XL_ICFG_CONNECTOR_MASK;
			icfg |= (XL_XCVR_10BT << XL_ICFG_CONNECTOR_BITS);
			mediastat |= XL_MEDIASTAT_LINKBEAT|
					XL_MEDIASTAT_JABGUARD;
			mediastat &= ~XL_MEDIASTAT_SQEENB;
		}
	}

	if (sc->xl_media & XL_MEDIAOPT_BFX) {
		if (IFM_SUBTYPE(media) == IFM_100_FX) {
			printf("100baseFX port, ");
			sc->xl_xcvr = XL_XCVR_100BFX;
			icfg &= ~XL_ICFG_CONNECTOR_MASK;
			icfg |= (XL_XCVR_100BFX << XL_ICFG_CONNECTOR_BITS);
			mediastat |= XL_MEDIASTAT_LINKBEAT;
			mediastat &= ~XL_MEDIASTAT_SQEENB;
		}
	}

	if (sc->xl_media & (XL_MEDIAOPT_AUI|XL_MEDIAOPT_10FL)) {
		if (IFM_SUBTYPE(media) == IFM_10_5) {
			printf("AUI port, ");
			sc->xl_xcvr = XL_XCVR_AUI;
			icfg &= ~XL_ICFG_CONNECTOR_MASK;
			icfg |= (XL_XCVR_AUI << XL_ICFG_CONNECTOR_BITS);
			mediastat &= ~(XL_MEDIASTAT_LINKBEAT|
					XL_MEDIASTAT_JABGUARD);
			mediastat |= ~XL_MEDIASTAT_SQEENB;
		}
		if (IFM_SUBTYPE(media) == IFM_10_FL) {
			printf("10baseFL transceiver, ");
			sc->xl_xcvr = XL_XCVR_AUI;
			icfg &= ~XL_ICFG_CONNECTOR_MASK;
			icfg |= (XL_XCVR_AUI << XL_ICFG_CONNECTOR_BITS);
			mediastat &= ~(XL_MEDIASTAT_LINKBEAT|
					XL_MEDIASTAT_JABGUARD);
			mediastat |= ~XL_MEDIASTAT_SQEENB;
		}
	}

	if (sc->xl_media & XL_MEDIAOPT_BNC) {
		if (IFM_SUBTYPE(media) == IFM_10_2) {
			printf("BNC port, ");
			sc->xl_xcvr = XL_XCVR_COAX;
			icfg &= ~XL_ICFG_CONNECTOR_MASK;
			icfg |= (XL_XCVR_COAX << XL_ICFG_CONNECTOR_BITS);
			mediastat &= ~(XL_MEDIASTAT_LINKBEAT|
					XL_MEDIASTAT_JABGUARD|
					XL_MEDIASTAT_SQEENB);
		}
	}

	if ((media & IFM_GMASK) == IFM_FDX ||
			IFM_SUBTYPE(media) == IFM_100_FX) {
		printf("full duplex\n");
		XL_SEL_WIN(3);
		CSR_WRITE_1(sc, XL_W3_MAC_CTRL, XL_MACCTRL_DUPLEX);
	} else {
		printf("half duplex\n");
		XL_SEL_WIN(3);
		CSR_WRITE_1(sc, XL_W3_MAC_CTRL,
			(CSR_READ_1(sc, XL_W3_MAC_CTRL) & ~XL_MACCTRL_DUPLEX));
	}

	if (IFM_SUBTYPE(media) == IFM_10_2)
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_COAX_START);
	else
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_COAX_STOP);
	CSR_WRITE_4(sc, XL_W3_INTERNAL_CFG, icfg);
	XL_SEL_WIN(4);
	CSR_WRITE_2(sc, XL_W4_MEDIA_STATUS, mediastat);
	DELAY(800);
	XL_SEL_WIN(7);
}

void xl_reset(sc, hard)
	struct xl_softc		*sc;
{
	register int		i;

	XL_SEL_WIN(0);
	if (hard || (sc->xl_flags & XL_FLAG_WEIRDRESET)) {
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RESET |
		    ((sc->xl_flags & XL_FLAG_WEIRDRESET)?0xFF:0));
	}
	else
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RESET | 0x0010);
	xl_wait(sc);

	for (i = 0; i < XL_TIMEOUT; i++) {
		DELAY(10);
		if (!(CSR_READ_2(sc, XL_STATUS) & XL_STAT_CMDBUSY))
			break;
	}

	DELAY(100000);

	/* Reset TX and RX. */
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_RESET);
	xl_wait(sc);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_TX_RESET);
	xl_wait(sc);

	if (sc->xl_flags & XL_FLAG_WEIRDRESET) {
		XL_SEL_WIN(2);
		CSR_WRITE_2(sc, XL_W2_RESET_OPTIONS, CSR_READ_2(sc,
		    XL_W2_RESET_OPTIONS) | 0x4010);
	}

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(100000);
        return;
}

/*
 * This routine is a kludge to work around possible hardware faults
 * or manufacturing defects that can cause the media options register
 * (or reset options register, as it's called for the first generation
 * 3c90x adapters) to return an incorrect result. I have encountered
 * one Dell Latitude laptop docking station with an integrated 3c905-TX
 * which doesn't have any of the 'mediaopt' bits set. This screws up
 * the attach routine pretty badly because it doesn't know what media
 * to look for. If we find ourselves in this predicament, this routine
 * will try to guess the media options values and warn the user of a
 * possible manufacturing defect with his adapter/system/whatever.
 */
void xl_mediacheck(sc)
	struct xl_softc		*sc;
{
	/*
	 * If some of the media options bits are set, assume they are
	 * correct. If not, try to figure it out down below.
	 * XXX I should check for 10baseFL, but I don't have an adapter
	 * to test with.
	 */
	if (sc->xl_media & (XL_MEDIAOPT_MASK & ~XL_MEDIAOPT_VCO)) {
		/*
	 	 * Check the XCVR value. If it's not in the normal range
	 	 * of values, we need to fake it up here.
	 	 */
		if (sc->xl_xcvr <= XL_XCVR_AUTO)
			return;
		else {
			printf("xl%d: bogus xcvr value "
			"in EEPROM (%x)\n", sc->xl_unit, sc->xl_xcvr);
			printf("xl%d: choosing new default based "
				"on card type\n", sc->xl_unit);
		}
	} else {
		if (sc->xl_type == XL_TYPE_905B &&
		    sc->xl_media & XL_MEDIAOPT_10FL)
			return;
		printf("xl%d: WARNING: no media options bits set in "
			"the media options register!!\n", sc->xl_unit);
		printf("xl%d: this could be a manufacturing defect in "
			"your adapter or system\n", sc->xl_unit);
		printf("xl%d: attempting to guess media type; you "
			"should probably consult your vendor\n", sc->xl_unit);
	}

	xl_choose_xcvr(sc, 1);
}

void xl_choose_xcvr(sc, verbose)
	struct xl_softc *sc;
	int verbose;
{
	u_int16_t devid;

	/*
	 * Read the device ID from the EEPROM.
	 * This is what's loaded into the PCI device ID register, so it has
	 * to be correct otherwise we wouldn't have gotten this far.
	 */
	xl_read_eeprom(sc, (caddr_t)&devid, XL_EE_PRODID, 1, 0);

	switch(devid) {
	case TC_DEVICEID_BOOMERANG_10BT:	/* 3c900-TPO */
	case TC_DEVICEID_KRAKATOA_10BT:		/* 3c900B-TPO */
		sc->xl_media = XL_MEDIAOPT_BT;
		sc->xl_xcvr = XL_XCVR_10BT;
		if (verbose)
			printf("xl%d: guessing 10BaseT transceiver\n",
			    sc->xl_unit);
		break;
	case TC_DEVICEID_BOOMERANG_10BT_COMBO:	/* 3c900-COMBO */
	case TC_DEVICEID_KRAKATOA_10BT_COMBO:	/* 3c900B-COMBO */
		sc->xl_media = XL_MEDIAOPT_BT|XL_MEDIAOPT_BNC|XL_MEDIAOPT_AUI;
		sc->xl_xcvr = XL_XCVR_10BT;
		if (verbose)
			printf("xl%d: guessing COMBO (AUI/BNC/TP)\n",
			    sc->xl_unit);
		break;
	case TC_DEVICEID_KRAKATOA_10BT_TPC:	/* 3c900B-TPC */
		sc->xl_media = XL_MEDIAOPT_BT|XL_MEDIAOPT_BNC;
		sc->xl_xcvr = XL_XCVR_10BT;
		if (verbose)
			printf("xl%d: guessing TPC (BNC/TP)\n", sc->xl_unit);
		break;
	case TC_DEVICEID_CYCLONE_10FL:		/* 3c900B-FL */
		sc->xl_media = XL_MEDIAOPT_10FL;
		sc->xl_xcvr = XL_XCVR_AUI;
		if (verbose)
			printf("xl%d: guessing 10baseFL\n", sc->xl_unit);
		break;
	case TC_DEVICEID_BOOMERANG_10_100BT:	/* 3c905-TX */
	case TC_DEVICEID_HURRICANE_555:		/* 3c555 */
	case TC_DEVICEID_HURRICANE_556:		/* 3c556 */
	case TC_DEVICEID_HURRICANE_556B:	/* 3c556B */
		sc->xl_media = XL_MEDIAOPT_MII;
		sc->xl_xcvr = XL_XCVR_MII;
		if (verbose)
			printf("xl%d: guessing MII\n", sc->xl_unit);
		break;
	case TC_DEVICEID_BOOMERANG_100BT4:	/* 3c905-T4 */
	case TC_DEVICEID_CYCLONE_10_100BT4:	/* 3c905B-T4 */
		sc->xl_media = XL_MEDIAOPT_BT4;
		sc->xl_xcvr = XL_XCVR_MII;
		if (verbose)
			printf("xl%d: guessing 100BaseT4/MII\n", sc->xl_unit);
		break;
	case TC_DEVICEID_HURRICANE_10_100BT:	/* 3c905B-TX */
	case TC_DEVICEID_HURRICANE_10_100BT_SERV:/* 3c980-TX */
	case TC_DEVICEID_TORNADO_10_100BT_SERV:	/* 3c980C-TX */
	case TC_DEVICEID_HURRICANE_SOHO100TX:	/* 3cSOHO100-TX */
	case TC_DEVICEID_TORNADO_10_100BT:	/* 3c905C-TX */
	case TC_DEVICEID_TORNADO_HOMECONNECT:	/* 3c450-TX */
		sc->xl_media = XL_MEDIAOPT_BTX;
		sc->xl_xcvr = XL_XCVR_AUTO;
		if (verbose)
			printf("xl%d: guessing 10/100 internal\n",
			    sc->xl_unit);
		break;
	case TC_DEVICEID_CYCLONE_10_100_COMBO:	/* 3c905B-COMBO */
		sc->xl_media = XL_MEDIAOPT_BTX|XL_MEDIAOPT_BNC|XL_MEDIAOPT_AUI;
		sc->xl_xcvr = XL_XCVR_AUTO;
		if (verbose)
			printf("xl%d: guessing 10/100 plus BNC/AUI\n",
			    sc->xl_unit);
		break;
	case TC_DEVICEID_3C575_CARDBUS:
	case TC_DEVICEID_3CCFE575BT_CARDBUS:
	case TC_DEVICEID_3CCFE575CT_CARDBUS:
	case TC_DEVICEID_3CCFEM656_CARDBUS:
	case TC_DEVICEID_3CCFEM656B_CARDBUS:
	case TC_DEVICEID_3CCFEM656C_CARDBUS:
		sc->xl_media = XL_MEDIAOPT_MII;
		sc->xl_xcvr = XL_XCVR_MII;
		break;
	default:
		printf("xl%d: unknown device ID: %x -- "
			"defaulting to 10baseT\n", sc->xl_unit, devid);
		sc->xl_media = XL_MEDIAOPT_BT;
		break;
	}

	return;
}

/*
 * Initialize the transmit descriptors.
 */
int xl_list_tx_init(sc)
	struct xl_softc		*sc;
{
	struct xl_chain_data	*cd;
	struct xl_list_data	*ld;
	int			i;

	cd = &sc->xl_cdata;
	ld = sc->xl_ldata;
	for (i = 0; i < XL_TX_LIST_CNT; i++) {
		cd->xl_tx_chain[i].xl_ptr = &ld->xl_tx_list[i];
		if (i == (XL_TX_LIST_CNT - 1))
			cd->xl_tx_chain[i].xl_next = NULL;
		else
			cd->xl_tx_chain[i].xl_next = &cd->xl_tx_chain[i + 1];
	}

	cd->xl_tx_free = &cd->xl_tx_chain[0];
	cd->xl_tx_tail = cd->xl_tx_head = NULL;

	return(0);
}

/*
 * Initialize the transmit desriptors.
 */
int
xl_list_tx_init_90xB(sc)
	struct xl_softc *sc;
{
	struct xl_chain_data *cd;
	struct xl_list_data *ld;
	int i;

	cd = &sc->xl_cdata;
	ld = sc->xl_ldata;
	for (i = 0; i < XL_TX_LIST_CNT; i++) {
		cd->xl_tx_chain[i].xl_ptr = &ld->xl_tx_list[i];
		cd->xl_tx_chain[i].xl_phys = vtophys(&ld->xl_tx_list[i]);
		if (i == (XL_TX_LIST_CNT - 1))
			cd->xl_tx_chain[i].xl_next = &cd->xl_tx_chain[0];
		else
			cd->xl_tx_chain[i].xl_next = &cd->xl_tx_chain[i + 1];
		if (i == 0)
			cd->xl_tx_chain[i].xl_prev =
			    &cd->xl_tx_chain[XL_TX_LIST_CNT - 1];
		else
			cd->xl_tx_chain[i].xl_prev =
			    &cd->xl_tx_chain[i - 1];
	}

	bzero((char *)ld->xl_tx_list, sizeof(struct xl_list) * XL_TX_LIST_CNT);
	ld->xl_tx_list[0].xl_status = XL_TXSTAT_EMPTY;

	cd->xl_tx_prod = 1;
	cd->xl_tx_cons = 1;
	cd->xl_tx_cnt = 0;

	return (0);
}

/*
 * Initialize the RX descriptors and allocate mbufs for them. Note that
 * we arrange the descriptors in a closed ring, so that the last descriptor
 * points back to the first.
 */
int xl_list_rx_init(sc)
	struct xl_softc		*sc;
{
	struct xl_chain_data	*cd;
	struct xl_list_data	*ld;
	int			i;

	cd = &sc->xl_cdata;
	ld = sc->xl_ldata;

	for (i = 0; i < XL_RX_LIST_CNT; i++) {
		cd->xl_rx_chain[i].xl_ptr =
			(struct xl_list_onefrag *)&ld->xl_rx_list[i];
		if (xl_newbuf(sc, &cd->xl_rx_chain[i]) == ENOBUFS)
			return(ENOBUFS);
		if (i == (XL_RX_LIST_CNT - 1)) {
			cd->xl_rx_chain[i].xl_next = &cd->xl_rx_chain[0];
			ld->xl_rx_list[i].xl_next =
			    vtophys(&ld->xl_rx_list[0]);
		} else {
			cd->xl_rx_chain[i].xl_next = &cd->xl_rx_chain[i + 1];
			ld->xl_rx_list[i].xl_next =
			    vtophys(&ld->xl_rx_list[i + 1]);
		}
	}

	cd->xl_rx_head = &cd->xl_rx_chain[0];

	return(0);
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 */
int xl_newbuf(sc, c)
	struct xl_softc		*sc;
	struct xl_chain_onefrag	*c;
{
	struct mbuf		*m_new = NULL;

	MGETHDR(m_new, M_DONTWAIT, MT_DATA);
	if (m_new == NULL)
		return(ENOBUFS);

	MCLGET(m_new, M_DONTWAIT);
	if (!(m_new->m_flags & M_EXT)) {
		m_freem(m_new);
		return(ENOBUFS);
	}

	m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;

	/* Force longword alignment for packet payload. */
	m_adj(m_new, ETHER_ALIGN);

	c->xl_mbuf = m_new;
	c->xl_ptr->xl_frag.xl_addr = vtophys(mtod(m_new, caddr_t));
	c->xl_ptr->xl_frag.xl_len = MCLBYTES | XL_LAST_FRAG;
	c->xl_ptr->xl_status = 0;

	return(0);
}

int xl_rx_resync(sc)
	struct xl_softc *sc;
{
	struct xl_chain_onefrag *pos;
	int i;

	pos = sc->xl_cdata.xl_rx_head;

	for (i = 0; i < XL_RX_LIST_CNT; i++) {
		if (pos->xl_ptr->xl_status)
			break;
		pos = pos->xl_next;
	}

	if (i == XL_RX_LIST_CNT)
		return (0);

	sc->xl_cdata.xl_rx_head = pos;

	return (EAGAIN);
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
void xl_rxeof(sc)
	struct xl_softc		*sc;
{
        struct mbuf		*m;
        struct ifnet		*ifp;
	struct xl_chain_onefrag	*cur_rx;
	int			total_len = 0;
	u_int16_t		rxstat;

	ifp = &sc->arpcom.ac_if;

again:

	while((rxstat = sc->xl_cdata.xl_rx_head->xl_ptr->xl_status)) {
		cur_rx = sc->xl_cdata.xl_rx_head;
		sc->xl_cdata.xl_rx_head = cur_rx->xl_next;

		/*
		 * If an error occurs, update stats, clear the
		 * status word and leave the mbuf cluster in place:
		 * it should simply get re-used next time this descriptor
	 	 * comes up in the ring.
		 */
		if (rxstat & XL_RXSTAT_UP_ERROR) {
			ifp->if_ierrors++;
			cur_rx->xl_ptr->xl_status = 0;
			continue;
		}

		/*
		 * If there error bit was not set, the upload complete
		 * bit should be set which means we have a valid packet.
		 * If not, something truly strange has happened.
		 */
		if (!(rxstat & XL_RXSTAT_UP_CMPLT)) {
			printf("xl%d: bad receive status -- "
			    "packet dropped", sc->xl_unit);
			ifp->if_ierrors++;
			cur_rx->xl_ptr->xl_status = 0;
			continue;
		}

		/* No errors; receive the packet. */	
		m = cur_rx->xl_mbuf;
		total_len = cur_rx->xl_ptr->xl_status & XL_RXSTAT_LENMASK;

		/*
		 * Try to conjure up a new mbuf cluster. If that
		 * fails, it means we have an out of memory condition and
		 * should leave the buffer in place and continue. This will
		 * result in a lost packet, but there's little else we
		 * can do in this situation.
		 */
		if (xl_newbuf(sc, cur_rx) == ENOBUFS) {
			ifp->if_ierrors++;
			cur_rx->xl_ptr->xl_status = 0;
			continue;
		}

		ifp->if_ipackets++;
		m->m_pkthdr.rcvif = ifp;
		m->m_pkthdr.len = m->m_len = total_len;
#if NBPFILTER > 0
		/*
		 * Handle BPF listeners. Let the BPF user see the packet.
		 */
		if (ifp->if_bpf) {
			bpf_mtap(ifp->if_bpf, m);
		}
#endif
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
	if (CSR_READ_4(sc, XL_UPLIST_PTR) == 0 ||
		CSR_READ_4(sc, XL_UPLIST_STATUS) & XL_PKTSTAT_UP_STALLED) {
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_UP_STALL);
		xl_wait(sc);
		CSR_WRITE_4(sc, XL_UPLIST_PTR,
			vtophys(&sc->xl_ldata->xl_rx_list[0]));
		sc->xl_cdata.xl_rx_head = &sc->xl_cdata.xl_rx_chain[0];
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_UP_UNSTALL);
		goto again;
	}

	return;
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */
void xl_txeof(sc)
	struct xl_softc		*sc;
{
	struct xl_chain		*cur_tx;
	struct ifnet		*ifp;

	ifp = &sc->arpcom.ac_if;

	/* Clear the timeout timer. */
	ifp->if_timer = 0;

	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been uploaded. Note: the 3c905B
	 * sets a special bit in the status word to let us
	 * know that a frame has been downloaded, but the
	 * original 3c900/3c905 adapters don't do that.
	 * Consequently, we have to use a different test if
	 * xl_type != XL_TYPE_905B.
	 */
	while(sc->xl_cdata.xl_tx_head != NULL) {
		cur_tx = sc->xl_cdata.xl_tx_head;

		if (CSR_READ_4(sc, XL_DOWNLIST_PTR))
			break;

		sc->xl_cdata.xl_tx_head = cur_tx->xl_next;
		m_freem(cur_tx->xl_mbuf);
		cur_tx->xl_mbuf = NULL;
		ifp->if_opackets++;

		cur_tx->xl_next = sc->xl_cdata.xl_tx_free;
		sc->xl_cdata.xl_tx_free = cur_tx;
	}

	if (sc->xl_cdata.xl_tx_head == NULL) {
		ifp->if_flags &= ~IFF_OACTIVE;
		sc->xl_cdata.xl_tx_tail = NULL;
	} else {
		if (CSR_READ_4(sc, XL_DMACTL) & XL_DMACTL_DOWN_STALLED ||
			!CSR_READ_4(sc, XL_DOWNLIST_PTR)) {
			CSR_WRITE_4(sc, XL_DOWNLIST_PTR,
				vtophys(sc->xl_cdata.xl_tx_head->xl_ptr));
			CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_DOWN_UNSTALL);
		}
	}

	return;
}

void
xl_txeof_90xB(sc)
	struct xl_softc *sc;
{
	struct xl_chain *cur_tx = NULL;
	struct ifnet *ifp;
	int idx;

	ifp = &sc->arpcom.ac_if;

	idx = sc->xl_cdata.xl_tx_cons;
	while(idx != sc->xl_cdata.xl_tx_prod) {

		cur_tx = &sc->xl_cdata.xl_tx_chain[idx];

		if (!(cur_tx->xl_ptr->xl_status & XL_TXSTAT_DL_COMPLETE))
			break;

		if (cur_tx->xl_mbuf != NULL) {
			m_freem(cur_tx->xl_mbuf);
			cur_tx->xl_mbuf = NULL;
		}

		ifp->if_opackets++;

		sc->xl_cdata.xl_tx_cnt--;
		XL_INC(idx, XL_TX_LIST_CNT);
		ifp->if_timer = 0;
	}

	sc->xl_cdata.xl_tx_cons = idx;

	if (cur_tx != NULL)
		ifp->if_flags &= ~IFF_OACTIVE;
}

/*
 * TX 'end of channel' interrupt handler. Actually, we should
 * only get a 'TX complete' interrupt if there's a transmit error,
 * so this is really TX error handler.
 */
void xl_txeoc(sc)
	struct xl_softc		*sc;
{
	u_int8_t		txstat;

	while((txstat = CSR_READ_1(sc, XL_TX_STATUS))) {
		if (txstat & XL_TXSTATUS_UNDERRUN ||
			txstat & XL_TXSTATUS_JABBER ||
			txstat & XL_TXSTATUS_RECLAIM) {
			if (txstat != 0x90) {
				printf("xl%d: transmission error: %x\n",
				    sc->xl_unit, txstat);
			}
			CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_TX_RESET);
			xl_wait(sc);
			if (sc->xl_type == XL_TYPE_905B) {
				int i;
				struct xl_chain *c;
				i = sc->xl_cdata.xl_tx_cons;
				c = &sc->xl_cdata.xl_tx_chain[i];
				CSR_WRITE_4(sc, XL_DOWNLIST_PTR, c->xl_phys);
				CSR_WRITE_1(sc, XL_DOWN_POLL, 64);
			} else {
				if (sc->xl_cdata.xl_tx_head != NULL)
					CSR_WRITE_4(sc, XL_DOWNLIST_PTR,
					    vtophys(sc->xl_cdata.xl_tx_head->xl_ptr));
			}
			/*
			 * Remember to set this for the
			 * first generation 3c90X chips.
			 */
			CSR_WRITE_1(sc, XL_TX_FREETHRESH, XL_PACKET_SIZE >> 8);
			if (txstat & XL_TXSTATUS_UNDERRUN &&
			    sc->xl_tx_thresh < XL_PACKET_SIZE) {
				sc->xl_tx_thresh += XL_MIN_FRAMELEN;
#ifdef notdef
				printf("xl%d: tx underrun, increasing tx start"
				    " threshold to %d\n", sc->xl_unit,
				    sc->xl_tx_thresh);
#endif
			}
			CSR_WRITE_2(sc, XL_COMMAND,
			    XL_CMD_TX_SET_START|sc->xl_tx_thresh);
			if (sc->xl_type == XL_TYPE_905B) {
				CSR_WRITE_2(sc, XL_COMMAND,
				XL_CMD_SET_TX_RECLAIM|(XL_PACKET_SIZE >> 4));
			}
			CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_TX_ENABLE);
			CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_DOWN_UNSTALL);
		} else {
			CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_TX_ENABLE);
			CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_DOWN_UNSTALL);
		}
		/*
		 * Write an arbitrary byte to the TX_STATUS register
	 	 * to clear this interrupt/error and advance to the next.
		 */
		CSR_WRITE_1(sc, XL_TX_STATUS, 0x01);
	}

	return;
}

int xl_intr(arg)
	void			*arg;
{
	struct xl_softc		*sc;
	struct ifnet		*ifp;
	u_int16_t		status;
	int claimed = 0;

	sc = arg;
	ifp = &sc->arpcom.ac_if;

	while ((status = CSR_READ_2(sc, XL_STATUS)) & XL_INTRS) {

		claimed = 1;

		CSR_WRITE_2(sc, XL_COMMAND,
		    XL_CMD_INTR_ACK|(status & XL_INTRS));

		if (sc->intr_ack)
			(*sc->intr_ack)(sc);

		if (status & XL_STAT_UP_COMPLETE) {
			int curpkts;

			curpkts = ifp->if_ipackets;
			xl_rxeof(sc);
			if (curpkts == ifp->if_ipackets) {
				while (xl_rx_resync(sc))
					xl_rxeof(sc);
			}
		}

		if (status & XL_STAT_DOWN_COMPLETE) {
			if (sc->xl_type == XL_TYPE_905B)
				xl_txeof_90xB(sc);
			else
				xl_txeof(sc);
		}

		if (status & XL_STAT_TX_COMPLETE) {
			ifp->if_oerrors++;
			xl_txeoc(sc);
		}

		if (status & XL_STAT_ADFAIL) {
			xl_reset(sc, 0);
			xl_init(sc);
		}

		if (status & XL_STAT_STATSOFLOW) {
			sc->xl_stats_no_timeout = 1;
			xl_stats_update(sc);
			sc->xl_stats_no_timeout = 0;
		}
	}

	if (!IFQ_IS_EMPTY(&ifp->if_snd))
		(*ifp->if_start)(ifp);

	return (claimed);
}

void xl_stats_update(xsc)
	void			*xsc;
{
	struct xl_softc		*sc;
	struct ifnet		*ifp;
	struct xl_stats		xl_stats;
	u_int8_t		*p;
	int			i;
	struct mii_data		*mii = NULL;

	bzero((char *)&xl_stats, sizeof(struct xl_stats));

	sc = xsc;
	ifp = &sc->arpcom.ac_if;
	if (sc->xl_hasmii)
		mii = &sc->sc_mii;

	p = (u_int8_t *)&xl_stats;

	/* Read all the stats registers. */
	XL_SEL_WIN(6);

	for (i = 0; i < 16; i++)
		*p++ = CSR_READ_1(sc, XL_W6_CARRIER_LOST + i);

	ifp->if_ierrors += xl_stats.xl_rx_overrun;

	ifp->if_collisions += xl_stats.xl_tx_multi_collision +
				xl_stats.xl_tx_single_collision +
				xl_stats.xl_tx_late_collision;

	/*
	 * Boomerang and cyclone chips have an extra stats counter
	 * in window 4 (BadSSD). We have to read this too in order
	 * to clear out all the stats registers and avoid a statsoflow
	 * interrupt.
	 */
	XL_SEL_WIN(4);
	CSR_READ_1(sc, XL_W4_BADSSD);

	if (mii != NULL)
		mii_tick(mii);

	XL_SEL_WIN(7);

	if (!sc->xl_stats_no_timeout)
		timeout_add(&sc->xl_stsup_tmo, hz);

	return;
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
int xl_encap(sc, c, m_head)
	struct xl_softc		*sc;
	struct xl_chain		*c;
	struct mbuf		*m_head;
{
	int			frag = 0;
	struct xl_frag		*f = NULL;
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
			if (frag == XL_MAXFRAGS)
				break;
			total_len+= m->m_len;
			c->xl_ptr->xl_frag[frag].xl_addr =
					vtophys(mtod(m, vm_offset_t));
			c->xl_ptr->xl_frag[frag].xl_len = m->m_len;
			frag++;
		}
	}

	/*
	 * Handle special case: we used up all 63 fragments,
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
		f = &c->xl_ptr->xl_frag[0];
		f->xl_addr = vtophys(mtod(m_new, caddr_t));
		f->xl_len = total_len = m_new->m_len;
		frag = 1;
	}

	c->xl_mbuf = m_head;
	c->xl_ptr->xl_frag[frag - 1].xl_len |=  XL_LAST_FRAG;
	c->xl_ptr->xl_status = total_len;
	c->xl_ptr->xl_next = 0;

	return(0);
}

/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit lists. We also save a
 * copy of the pointers since the transmit list fragment pointers are
 * physical addresses.
 */
void xl_start(ifp)
	struct ifnet		*ifp;
{
	struct xl_softc		*sc;
	struct mbuf		*m_head = NULL;
	struct xl_chain		*prev = NULL, *cur_tx = NULL, *start_tx;

	sc = ifp->if_softc;

	/*
	 * Check for an available queue slot. If there are none,
	 * punt.
	 */
	if (sc->xl_cdata.xl_tx_free == NULL) {
		xl_txeoc(sc);
		xl_txeof(sc);
		if (sc->xl_cdata.xl_tx_free == NULL) {
			ifp->if_flags |= IFF_OACTIVE;
			return;
		}
	}

	start_tx = sc->xl_cdata.xl_tx_free;

	while(sc->xl_cdata.xl_tx_free != NULL) {
		IFQ_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;

		/* Pick a descriptor off the free list. */
		cur_tx = sc->xl_cdata.xl_tx_free;
		sc->xl_cdata.xl_tx_free = cur_tx->xl_next;

		cur_tx->xl_next = NULL;

		/* Pack the data into the descriptor. */
		xl_encap(sc, cur_tx, m_head);

		/* Chain it together. */
		if (prev != NULL) {
			prev->xl_next = cur_tx;
			prev->xl_ptr->xl_next = vtophys(cur_tx->xl_ptr);
		}
		prev = cur_tx;

#if NBPFILTER > 0
		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, cur_tx->xl_mbuf);
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
	cur_tx->xl_ptr->xl_status |= XL_TXSTAT_DL_INTR;

	/*
	 * Queue the packets. If the TX channel is clear, update
	 * the downlist pointer register.
	 */
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_DOWN_STALL);
	xl_wait(sc);

	if (sc->xl_cdata.xl_tx_head != NULL) {
		sc->xl_cdata.xl_tx_tail->xl_next = start_tx;
		sc->xl_cdata.xl_tx_tail->xl_ptr->xl_next =
					vtophys(start_tx->xl_ptr);
		sc->xl_cdata.xl_tx_tail->xl_ptr->xl_status &=
					~XL_TXSTAT_DL_INTR;
		sc->xl_cdata.xl_tx_tail = cur_tx;
	} else {
		sc->xl_cdata.xl_tx_head = start_tx;
		sc->xl_cdata.xl_tx_tail = cur_tx;
	}
	if (!CSR_READ_4(sc, XL_DOWNLIST_PTR))
		CSR_WRITE_4(sc, XL_DOWNLIST_PTR, vtophys(start_tx->xl_ptr));

	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_DOWN_UNSTALL);

	XL_SEL_WIN(7);

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 5;

	/*
	 * XXX Under certain conditions, usually on slower machines
	 * where interrupts may be dropped, it's possible for the
	 * adapter to chew up all the buffers in the receive ring
	 * and stall, without us being able to do anything about it.
	 * To guard against this, we need to make a pass over the
	 * RX queue to make sure there aren't any packets pending.
	 * Doing it here means we can flush the receive ring at the
	 * same time the chip is DMAing the transmit descriptors we
	 * just gave it.
 	 *
	 * 3Com goes to some lengths to emphasize the Parallel Tasking (tm)
	 * nature of their chips in all their marketing literature;
	 * we may as well take advantage of it. :)
	 */
	xl_rxeof(sc);

	return;
}

int xl_encap_90xB(sc, c, m_head)
	struct xl_softc *sc;
	struct xl_chain *c;
	struct mbuf *m_head;
{
	int frag = 0;
	struct xl_frag *f = NULL;
	struct mbuf *m;
	struct xl_list *d;

	/*
	 * Start packing the mbufs in this chain into
	 * the fragment pointers. Stop when we run out
	 * of fragments or hit the end of the mbuf chain.
	 */
	d = c->xl_ptr;
	d->xl_status = 0;
	d->xl_next = 0;

	for (m = m_head, frag = 0; m != NULL; m = m->m_next) {
		if (m->m_len != 0) {
			if (frag == XL_MAXFRAGS)
				break;
			f = &d->xl_frag[frag];
			f->xl_addr = vtophys(mtod(m, vm_offset_t));
			f->xl_len = m->m_len;
			frag++;
		}
	}

	c->xl_mbuf = m_head;
	c->xl_ptr->xl_frag[frag - 1].xl_len |= XL_LAST_FRAG;
	c->xl_ptr->xl_status = XL_TXSTAT_RND_DEFEAT;

	return(0);
}

void
xl_start_90xB(ifp)
	struct ifnet *ifp;
{
	struct xl_softc *sc;
	struct mbuf *m_head = NULL;
	struct xl_chain *prev = NULL, *cur_tx = NULL, *start_tx;
	int idx;

	sc = ifp->if_softc;

	if (ifp->if_flags & IFF_OACTIVE)
		return;

	idx = sc->xl_cdata.xl_tx_prod;
	start_tx = &sc->xl_cdata.xl_tx_chain[idx];

	while (sc->xl_cdata.xl_tx_chain[idx].xl_mbuf == NULL) {

		if ((XL_TX_LIST_CNT - sc->xl_cdata.xl_tx_cnt) < 3) {
			ifp->if_flags |= IFF_OACTIVE;
			break;
		}

		IFQ_DEQUEUE(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;

		cur_tx = &sc->xl_cdata.xl_tx_chain[idx];

		/* Pack the data into the descriptor. */
		xl_encap_90xB(sc, cur_tx, m_head);

		/* Chain it together. */
		if (prev != NULL)
			prev->xl_ptr->xl_next = cur_tx->xl_phys;
		prev = cur_tx;

#if NBPFILTER > 0
		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, cur_tx->xl_mbuf);
#endif

		XL_INC(idx, XL_TX_LIST_CNT);
		sc->xl_cdata.xl_tx_cnt++;
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
	cur_tx->xl_ptr->xl_status |= XL_TXSTAT_DL_INTR;

	/* Start transmission */
	sc->xl_cdata.xl_tx_prod = idx;
	start_tx->xl_prev->xl_ptr->xl_next = start_tx->xl_phys;

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 5;
}

void xl_init(xsc)
	void			*xsc;
{
	struct xl_softc		*sc = xsc;
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	int			s, i;
	u_int16_t		rxfilt = 0;
	struct mii_data		*mii = NULL;

	s = splimp();

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	xl_stop(sc);

	if (sc->xl_hasmii)
		mii = &sc->sc_mii;

	if (mii == NULL) {
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_RESET);
		xl_wait(sc);
	}
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_TX_RESET);
	xl_wait(sc);
	DELAY(10000);


	/* Init our MAC address */
	XL_SEL_WIN(2);
	for (i = 0; i < ETHER_ADDR_LEN; i++) {
		CSR_WRITE_1(sc, XL_W2_STATION_ADDR_LO + i,
				sc->arpcom.ac_enaddr[i]);
	}

	/* Clear the station mask. */
	for (i = 0; i < 3; i++)
		CSR_WRITE_2(sc, XL_W2_STATION_MASK_LO + (i * 2), 0);
#ifdef notdef
	/* Reset TX and RX. */
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_RESET);
	xl_wait(sc);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_TX_RESET);
	xl_wait(sc);
#endif
	/* Init circular RX list. */
	if (xl_list_rx_init(sc) == ENOBUFS) {
		printf("xl%d: initialization failed: no "
			"memory for rx buffers\n", sc->xl_unit);
		xl_stop(sc);
		splx(s);
		return;
	}

	/* Init TX descriptors. */
	if (sc->xl_type == XL_TYPE_905B)
		xl_list_tx_init_90xB(sc);
	else
		xl_list_tx_init(sc);

	/*
	 * Set the TX freethresh value.
	 * Note that this has no effect on 3c905B "cyclone"
	 * cards but is required for 3c900/3c905 "boomerang"
	 * cards in order to enable the download engine.
	 */
	CSR_WRITE_1(sc, XL_TX_FREETHRESH, XL_PACKET_SIZE >> 8);

	/* Set the TX start threshold for best performance. */
	sc->xl_tx_thresh = XL_MIN_FRAMELEN;
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_TX_SET_START|sc->xl_tx_thresh);

	/*
	 * If this is a 3c905B, also set the tx reclaim threshold.
	 * This helps cut down on the number of tx reclaim errors
	 * that could happen on a busy network. The chip multiplies
	 * the register value by 16 to obtain the actual threshold
	 * in bytes, so we divide by 16 when setting the value here.
	 * The existing threshold value can be examined by reading
	 * the register at offset 9 in window 5.
	 */
	if (sc->xl_type == XL_TYPE_905B) {
		CSR_WRITE_2(sc, XL_COMMAND,
		    XL_CMD_SET_TX_RECLAIM|(XL_PACKET_SIZE >> 4));
	}

	/* Set RX filter bits. */
	XL_SEL_WIN(5);
	rxfilt = CSR_READ_1(sc, XL_W5_RX_FILTER);

	/* Set the individual bit to receive frames for this host only. */
	rxfilt |= XL_RXFILTER_INDIVIDUAL;

	/* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC) {
		rxfilt |= XL_RXFILTER_ALLFRAMES;
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_SET_FILT|rxfilt);
	} else {
		rxfilt &= ~XL_RXFILTER_ALLFRAMES;
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_SET_FILT|rxfilt);
	}

	/*
	 * Set capture broadcast bit to capture broadcast frames.
	 */
	if (ifp->if_flags & IFF_BROADCAST) {
		rxfilt |= XL_RXFILTER_BROADCAST;
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_SET_FILT|rxfilt);
	} else {
		rxfilt &= ~XL_RXFILTER_BROADCAST;
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_SET_FILT|rxfilt);
	}

	/*
	 * Program the multicast filter, if necessary.
	 */
#if 0
	if (sc->xl_type == XL_TYPE_905B)
#else
	if (0)	/* xl_setmulti_hash() does not work right */
#endif
		xl_setmulti_hash(sc);
	else
		xl_setmulti(sc);

	/*
	 * Load the address of the RX list. We have to
	 * stall the upload engine before we can manipulate
	 * the uplist pointer register, then unstall it when
	 * we're finished. We also have to wait for the
	 * stall command to complete before proceeding.
	 * Note that we have to do this after any RX resets
	 * have completed since the uplist register is cleared
	 * by a reset.
	 */
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_UP_STALL);
	xl_wait(sc);
	CSR_WRITE_4(sc, XL_UPLIST_PTR, vtophys(&sc->xl_ldata->xl_rx_list[0]));
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_UP_UNSTALL);
	xl_wait(sc);

	if (sc->xl_type == XL_TYPE_905B) {
		/* Set polling interval */
		CSR_WRITE_1(sc, XL_DOWN_POLL, 64);
		/* Load the address of the TX list */
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_DOWN_STALL);
		xl_wait(sc);
		CSR_WRITE_4(sc, XL_DOWNLIST_PTR,
		    vtophys(&sc->xl_ldata->xl_tx_list[0]));
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_DOWN_UNSTALL);
		xl_wait(sc);
	}

	/*
	 * If the coax transceiver is on, make sure to enable
	 * the DC-DC converter.
 	 */
	XL_SEL_WIN(3);
	if (sc->xl_xcvr == XL_XCVR_COAX)
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_COAX_START);
	else
		CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_COAX_STOP);

#if NVLAN > 0
	/* Set max packet size to handle VLAN frames, only on 3c905B */
	if (sc->xl_type == XL_TYPE_905B)
		CSR_WRITE_2(sc, XL_W3_MAX_PKT_SIZE, 1514 + 4);
#endif

	/* Clear out the stats counters. */
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_STATS_DISABLE);
	sc->xl_stats_no_timeout = 1;
	xl_stats_update(sc);
	sc->xl_stats_no_timeout = 0;
	XL_SEL_WIN(4);
	CSR_WRITE_2(sc, XL_W4_NET_DIAG, XL_NETDIAG_UPPER_BYTES_ENABLE);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_STATS_ENABLE);

	/*
	 * Enable interrupts.
	 */
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_INTR_ACK|0xFF);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_STAT_ENB|XL_INTRS);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_INTR_ENB|XL_INTRS);

	if (sc->intr_ack)
		(*sc->intr_ack)(sc);

	/* Set the RX early threshold */
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_SET_THRESH|(XL_PACKET_SIZE >>2));
	CSR_WRITE_2(sc, XL_DMACTL, XL_DMACTL_UP_RX_EARLY);

	/* Enable receiver and transmitter. */
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_TX_ENABLE);
	xl_wait(sc);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_ENABLE);
	xl_wait(sc);

	/* Restore state of BMCR */
	if (mii != NULL)
		mii_mediachg(mii);

	/* Select window 7 for normal operations. */
	XL_SEL_WIN(7);

	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;

	(void)splx(s);

	timeout_add(&sc->xl_stsup_tmo, hz);

	return;
}

/*
 * Set media options.
 */
int xl_ifmedia_upd(ifp)
	struct ifnet		*ifp;
{
	struct xl_softc		*sc;
	struct ifmedia		*ifm = NULL;
	struct mii_data		*mii = NULL;

	sc = ifp->if_softc;

	if (sc->xl_hasmii)
		mii = &sc->sc_mii;
	if (mii == NULL)
		ifm = &sc->ifmedia;
	else
		ifm = &mii->mii_media;

	switch(IFM_SUBTYPE(ifm->ifm_media)) {
	case IFM_100_FX:
	case IFM_10_FL:
	case IFM_10_2:
	case IFM_10_5:
		xl_setmode(sc, ifm->ifm_media);
		return (0);
		break;
	default:
		break;
	}

	if (sc->xl_media & XL_MEDIAOPT_MII || sc->xl_media & XL_MEDIAOPT_BTX
		|| sc->xl_media & XL_MEDIAOPT_BT4) {
		xl_init(sc);
	} else {
		xl_setmode(sc, ifm->ifm_media);
	}

	return(0);
}

/*
 * Report current media status.
 */
void xl_ifmedia_sts(ifp, ifmr)
	struct ifnet		*ifp;
	struct ifmediareq	*ifmr;
{
	struct xl_softc		*sc;
	u_int32_t		icfg;
	struct mii_data		*mii = NULL;

	sc = ifp->if_softc;
	if (sc->xl_hasmii != 0)
		mii = &sc->sc_mii;

	XL_SEL_WIN(3);
	icfg = CSR_READ_4(sc, XL_W3_INTERNAL_CFG) & XL_ICFG_CONNECTOR_MASK;
	icfg >>= XL_ICFG_CONNECTOR_BITS;

	ifmr->ifm_active = IFM_ETHER;

	switch(icfg) {
	case XL_XCVR_10BT:
		ifmr->ifm_active = IFM_ETHER|IFM_10_T;
		if (CSR_READ_1(sc, XL_W3_MAC_CTRL) & XL_MACCTRL_DUPLEX)
			ifmr->ifm_active |= IFM_FDX;
		else
			ifmr->ifm_active |= IFM_HDX;
		break;
	case XL_XCVR_AUI:
		if (sc->xl_type == XL_TYPE_905B &&
		    sc->xl_media == XL_MEDIAOPT_10FL) {
			ifmr->ifm_active = IFM_ETHER|IFM_10_FL;
			if (CSR_READ_1(sc, XL_W3_MAC_CTRL) & XL_MACCTRL_DUPLEX)
				ifmr->ifm_active |= IFM_FDX;
			else
				ifmr->ifm_active |= IFM_FDX;
		} else
			ifmr->ifm_active = IFM_ETHER|IFM_10_5;
		break;
	case XL_XCVR_COAX:
		ifmr->ifm_active = IFM_ETHER|IFM_10_2;
		break;
	/*
	 * XXX MII and BTX/AUTO should be separate cases.
	 */

	case XL_XCVR_100BTX:
	case XL_XCVR_AUTO:
	case XL_XCVR_MII:
		if (mii != NULL) {
			mii_pollstat(mii);
			ifmr->ifm_active = mii->mii_media_active;
			ifmr->ifm_status = mii->mii_media_status;
		}
		break;
	case XL_XCVR_100BFX:
		ifmr->ifm_active = IFM_ETHER|IFM_100_FX;
		break;
	default:
		printf("xl%d: unknown XCVR type: %d\n", sc->xl_unit, icfg);
		break;
	}

	return;
}

int
xl_ioctl(ifp, command, data)
	struct ifnet *ifp;
	u_long command;
	caddr_t data;
{
	struct xl_softc *sc = ifp->if_softc;
	struct ifreq *ifr = (struct ifreq *)data;
	struct ifaddr *ifa = (struct ifaddr *)data;
	int s, error = 0;
	struct mii_data *mii = NULL;
	u_int8_t rxfilt;

	s = splimp();

	if ((error = ether_ioctl(ifp, &sc->arpcom, command, data)) > 0) {
		splx(s);
		return error;
	}

	switch(command) {
	case SIOCSIFADDR:
		ifp->if_flags |= IFF_UP;
		switch (ifa->ifa_addr->sa_family) {
#ifdef INET
		case AF_INET:
			xl_init(sc);
			arp_ifinit(&sc->arpcom, ifa);
			break;
#endif /* INET */
		default:
			xl_init(sc);
			break;
		}
		break;

	case SIOCSIFMTU:
		if(ifr->ifr_mtu > ETHERMTU || ifr->ifr_mtu < ETHERMIN) {
			error = EINVAL;
		} else if (ifp->if_mtu != ifr->ifr_mtu) {
			ifp->if_mtu = ifr->ifr_mtu;
		}
		break;

	case SIOCSIFFLAGS:
		XL_SEL_WIN(5);
		rxfilt = CSR_READ_1(sc, XL_W5_RX_FILTER);
		if (ifp->if_flags & IFF_UP) {
			if (ifp->if_flags & IFF_RUNNING &&
			    ifp->if_flags & IFF_PROMISC &&
			    !(sc->xl_if_flags & IFF_PROMISC)) {
				rxfilt |= XL_RXFILTER_ALLFRAMES;
				CSR_WRITE_2(sc, XL_COMMAND,
				    XL_CMD_RX_SET_FILT|rxfilt);
				XL_SEL_WIN(7);
			} else if (ifp->if_flags & IFF_RUNNING &&
			    !(ifp->if_flags & IFF_PROMISC) &&
			    sc->xl_if_flags & IFF_PROMISC) {
				rxfilt &= ~XL_RXFILTER_ALLFRAMES;
				CSR_WRITE_2(sc, XL_COMMAND,
				    XL_CMD_RX_SET_FILT|rxfilt);
				XL_SEL_WIN(7);
			} else
				xl_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				xl_stop(sc);
		}
		sc->xl_if_flags = ifp->if_flags;
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
#if 0
			if (sc->xl_type == XL_TYPE_905B)
#else
			if (0)	/* xl_setmulti_hash() does not work right */
#endif
				xl_setmulti_hash(sc);
			else
				xl_setmulti(sc);
			error = 0;
		}
		break;
	case SIOCGIFMEDIA:
	case SIOCSIFMEDIA:
		if (sc->xl_hasmii != 0)
			mii = &sc->sc_mii;
		if (mii == NULL)
			error = ifmedia_ioctl(ifp, ifr,
			    &sc->ifmedia, command);
		else
			error = ifmedia_ioctl(ifp, ifr,
			    &mii->mii_media, command);
		break;
	default:
		error = EINVAL;
		break;
	}

	(void)splx(s);

	return(error);
}

void xl_watchdog(ifp)
	struct ifnet		*ifp;
{
	struct xl_softc		*sc;
	u_int16_t		status = 0;

	sc = ifp->if_softc;

	ifp->if_oerrors++;
	XL_SEL_WIN(4);
	status = CSR_READ_2(sc, XL_W4_MEDIA_STATUS);
	printf("xl%d: watchdog timeout\n", sc->xl_unit);

	if (status & XL_MEDIASTAT_CARRIER)
		printf("xl%d: no carrier - transceiver cable problem?\n",
								sc->xl_unit);
	xl_txeoc(sc);
	xl_txeof(sc);
	xl_rxeof(sc);
	xl_reset(sc, 0);
	xl_init(sc);

	if (!IFQ_IS_EMPTY(&ifp->if_snd))
		(*ifp->if_start)(ifp);

	return;
}

void
xl_freetxrx(sc)
	struct xl_softc *sc;
{
	int i;

	/*
	 * Free data in the RX lists.
	 */
	for (i = 0; i < XL_RX_LIST_CNT; i++) {
		if (sc->xl_cdata.xl_rx_chain[i].xl_mbuf != NULL) {
			m_freem(sc->xl_cdata.xl_rx_chain[i].xl_mbuf);
			sc->xl_cdata.xl_rx_chain[i].xl_mbuf = NULL;
		}
	}
	bzero((char *)&sc->xl_ldata->xl_rx_list,
		sizeof(sc->xl_ldata->xl_rx_list));
	/*
	 * Free the TX list buffers.
	 */
	for (i = 0; i < XL_TX_LIST_CNT; i++) {
		if (sc->xl_cdata.xl_tx_chain[i].xl_mbuf != NULL) {
			m_freem(sc->xl_cdata.xl_tx_chain[i].xl_mbuf);
			sc->xl_cdata.xl_tx_chain[i].xl_mbuf = NULL;
		}
	}
	bzero((char *)&sc->xl_ldata->xl_tx_list,
		sizeof(sc->xl_ldata->xl_tx_list));
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
void xl_stop(sc)
	struct xl_softc *sc;
{
	struct ifnet *ifp;

	ifp = &sc->arpcom.ac_if;
	ifp->if_timer = 0;

	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_DISABLE);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_STATS_DISABLE);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_INTR_ENB);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_DISCARD);
	xl_wait(sc);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_TX_DISABLE);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_COAX_STOP);
	DELAY(800);

#ifdef foo
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_RX_RESET);
	xl_wait(sc);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_TX_RESET);
	xl_wait(sc);
#endif

	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_INTR_ACK|XL_STAT_INTLATCH);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_STAT_ENB|0);
	CSR_WRITE_2(sc, XL_COMMAND, XL_CMD_INTR_ENB|0);

	if (sc->intr_ack)
		(*sc->intr_ack)(sc);

	/* Stop the stats updater. */
	timeout_del(&sc->xl_stsup_tmo);

	xl_freetxrx(sc);

	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);

	return;
}

void
xl_attach(sc)
	struct xl_softc *sc;
{
	u_int8_t enaddr[ETHER_ADDR_LEN];
	struct ifnet *ifp = &sc->arpcom.ac_if;
	caddr_t roundptr;
	u_int round;
	int i, media = IFM_ETHER|IFM_100_TX|IFM_FDX;
	struct ifmedia *ifm;

	sc->xl_unit = sc->sc_dev.dv_unit;
	xl_reset(sc, 1);

	/*
	 * Get station address from the EEPROM.
	 */
	if (xl_read_eeprom(sc, (caddr_t)&enaddr, XL_EE_OEM_ADR0, 3, 1)) {
		printf("\n%s: failed to read station address\n",
		    sc->sc_dev.dv_xname);
		return;
	}
	bcopy(enaddr, (char *)&sc->arpcom.ac_enaddr, ETHER_ADDR_LEN);

	printf(" address %s\n", ether_sprintf(sc->arpcom.ac_enaddr));

	if (sc->xl_flags & (XL_FLAG_INVERT_LED_PWR|XL_FLAG_INVERT_MII_PWR)) {
		u_int16_t n;

		XL_SEL_WIN(2);
		n = CSR_READ_2(sc, 12);

		if (sc->xl_flags & XL_FLAG_INVERT_LED_PWR)
			n |= 0x0010;

		if (sc->xl_flags & XL_FLAG_INVERT_MII_PWR)
			n |= 0x4000;

		CSR_WRITE_2(sc, 12, n);
	}

	sc->xl_ldata_ptr = malloc(sizeof(struct xl_list_data) + 8,
	    M_DEVBUF, M_NOWAIT);
	if (sc->xl_ldata_ptr == NULL) {
		printf("%s: no memory for list buffers\n",sc->sc_dev.dv_xname);
		return;
	}

	sc->xl_ldata = (struct xl_list_data *)sc->xl_ldata_ptr;
#ifdef __alpha__
	round = (u_int64_t)sc->xl_ldata_ptr & 0xf;
#else
	round = (u_int32_t)sc->xl_ldata_ptr & 0xf;
#endif
	roundptr = sc->xl_ldata_ptr;
	for (i = 0; i < 8; i++) {
		if (round % 8) {
			round++;
			roundptr++;
		} else
			break;
	}
	sc->xl_ldata = (struct xl_list_data *)roundptr;
	bzero(sc->xl_ldata, sizeof(struct xl_list_data));

	/*
	 * Figure out the card type. 3c905B adapters have the
	 * 'supportsNoTxLength' bit set in the capabilities
	 * word in the EEPROM.
	 */
	xl_read_eeprom(sc, (caddr_t)&sc->xl_caps, XL_EE_CAPS, 1, 0);
	if (sc->xl_caps & XL_CAPS_NO_TXLENGTH)
		sc->xl_type = XL_TYPE_905B;
	else
		sc->xl_type = XL_TYPE_90X;

	timeout_set(&sc->xl_stsup_tmo, xl_stats_update, sc);

	ifp->if_softc = sc;
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = xl_ioctl;
	ifp->if_output = ether_output;
	if (sc->xl_type == XL_TYPE_905B)
		ifp->if_start = xl_start_90xB;
	else
		ifp->if_start = xl_start;
	ifp->if_watchdog = xl_watchdog;
	ifp->if_baudrate = 10000000;
	IFQ_SET_MAXLEN(&ifp->if_snd, XL_TX_LIST_CNT - 1);
	IFQ_SET_READY(&ifp->if_snd);
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);

	XL_SEL_WIN(3);
	sc->xl_media = CSR_READ_2(sc, XL_W3_MEDIA_OPT);

	xl_read_eeprom(sc, (char *)&sc->xl_xcvr, XL_EE_ICFG_0, 2, 0);
	sc->xl_xcvr &= XL_ICFG_CONNECTOR_MASK;
	sc->xl_xcvr >>= XL_ICFG_CONNECTOR_BITS;

	DELAY(100000);

	xl_mediacheck(sc);

	if (sc->xl_flags & XL_FLAG_INVERT_MII_PWR) {
		XL_SEL_WIN(2);
		CSR_WRITE_2(sc, 12, 0x4000 | CSR_READ_2(sc, 12));
	}

	DELAY(100000);

	if (sc->xl_media & XL_MEDIAOPT_MII || sc->xl_media & XL_MEDIAOPT_BTX
	    || sc->xl_media & XL_MEDIAOPT_BT4) {
		ifmedia_init(&sc->sc_mii.mii_media, 0,
		    xl_ifmedia_upd, xl_ifmedia_sts);
		sc->xl_hasmii = 1;
		sc->sc_mii.mii_ifp = ifp;
		sc->sc_mii.mii_readreg = xl_miibus_readreg;
		sc->sc_mii.mii_writereg = xl_miibus_writereg;
		sc->sc_mii.mii_statchg = xl_miibus_statchg;
		xl_setcfg(sc);
		mii_attach((struct device *)sc, &sc->sc_mii, 0xffffffff,
		    MII_PHY_ANY, MII_OFFSET_ANY, 0);

		if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
			ifmedia_add(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE,
			    0, NULL);
			ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE);
		}
		else {
			ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_AUTO);
		}
		ifm = &sc->sc_mii.mii_media;
	}
	else {
		ifmedia_init(&sc->ifmedia, 0, xl_ifmedia_upd, xl_ifmedia_sts);
		sc->xl_hasmii = 0;
		ifm = &sc->ifmedia;
	}

	/*
	 * Sanity check. If the user has selected "auto" and this isn't
	 * a 10/100 card of some kind, we need to force the transceiver
	 * type to something sane.
	 */
	if (sc->xl_xcvr == XL_XCVR_AUTO) {
		xl_choose_xcvr(sc, 0);
		xl_reset(sc, 0);
	}

	if (sc->xl_media & XL_MEDIAOPT_BT) {
		ifmedia_add(ifm, IFM_ETHER|IFM_10_T, 0, NULL);
		ifmedia_add(ifm, IFM_ETHER|IFM_10_T|IFM_HDX, 0, NULL);
		if (sc->xl_caps & XL_CAPS_FULL_DUPLEX)
			ifmedia_add(ifm, IFM_ETHER|IFM_10_T|IFM_FDX, 0, NULL);
	}

	if (sc->xl_media & (XL_MEDIAOPT_AUI|XL_MEDIAOPT_10FL)) {
		/*
		 * Check for a 10baseFL board in disguise.
		 */
		if (sc->xl_type == XL_TYPE_905B &&
		    sc->xl_media == XL_MEDIAOPT_10FL) {
			ifmedia_add(ifm, IFM_ETHER|IFM_10_FL, 0, NULL);
			ifmedia_add(ifm, IFM_ETHER|IFM_10_FL|IFM_HDX,
			    0, NULL);
			if (sc->xl_caps & XL_CAPS_FULL_DUPLEX)
				ifmedia_add(ifm,
				    IFM_ETHER|IFM_10_FL|IFM_FDX, 0, NULL);
		} else {
			ifmedia_add(ifm, IFM_ETHER|IFM_10_5, 0, NULL);
		}
	}

	if (sc->xl_media & XL_MEDIAOPT_BNC) {
		ifmedia_add(ifm, IFM_ETHER|IFM_10_2, 0, NULL);
	}

	if (sc->xl_media & XL_MEDIAOPT_BFX) {
		ifp->if_baudrate = 100000000;
		ifmedia_add(ifm, IFM_ETHER|IFM_100_FX, 0, NULL);
	}

	/* Choose a default media. */
	switch(sc->xl_xcvr) {
	case XL_XCVR_10BT:
		media = IFM_ETHER|IFM_10_T;
		xl_setmode(sc, media);
		break;
	case XL_XCVR_AUI:
		if (sc->xl_type == XL_TYPE_905B &&
		    sc->xl_media == XL_MEDIAOPT_10FL) {
			media = IFM_ETHER|IFM_10_FL;
			xl_setmode(sc, media);
		} else {
			media = IFM_ETHER|IFM_10_5;
			xl_setmode(sc, media);
		}
		break;
	case XL_XCVR_COAX:
		media = IFM_ETHER|IFM_10_2;
		xl_setmode(sc, media);
		break;
	case XL_XCVR_AUTO:
	case XL_XCVR_100BTX:
	case XL_XCVR_MII:
		/* Chosen by miibus */
		break;
	case XL_XCVR_100BFX:
		media = IFM_ETHER|IFM_100_FX;
		xl_setmode(sc, media);
		break;
	default:
		printf("xl%d: unknown XCVR type: %d\n", sc->xl_unit,
							sc->xl_xcvr);
		/*
		 * This will probably be wrong, but it prevents
		 * the ifmedia code from panicking.
		 */
		media = IFM_ETHER | IFM_10_T;
		break;
	}

	if (sc->xl_hasmii == 0)
		ifmedia_set(&sc->ifmedia, media);

	/*
	 * Call MI attach routines.
	 */
	if_attach(ifp);
	ether_ifattach(ifp);

	sc->sc_sdhook = shutdownhook_establish(xl_shutdown, sc);
}

int
xl_detach(sc)
	struct xl_softc *sc;
{
	struct ifnet *ifp = &sc->arpcom.ac_if;

	/* Unhook our tick handler. */
	timeout_del(&sc->xl_stsup_tmo);

	xl_freetxrx(sc);

	/* Detach all PHYs */
	if (sc->xl_hasmii)
		mii_detach(&sc->sc_mii, MII_PHY_ANY, MII_OFFSET_ANY);

	/* Delete all remaining media. */
	ifmedia_delete_instance(&sc->sc_mii.mii_media, IFM_INST_ANY);

	ether_ifdetach(ifp);
	if_detach(ifp);

	shutdownhook_disestablish(sc->sc_sdhook);

	return (0);
}

void
xl_shutdown(v)
	void *v;
{
	struct xl_softc	*sc = (struct xl_softc *)v;

	xl_reset(sc, 1);
	xl_stop(sc);
}

struct cfdriver xl_cd = {
	0, "xl", DV_IFNET
};
