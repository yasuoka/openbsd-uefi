/*	$OpenBSD: if_vr.c,v 1.23 2002/03/12 09:51:20 kjc Exp $	*/

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
 * $FreeBSD: src/sys/pci/if_vr.c,v 1.40 2001/02/06 10:11:48 phk Exp $
 */

/*
 * VIA Rhine fast ethernet PCI NIC driver
 *
 * Supports various network adapters based on the VIA Rhine
 * and Rhine II PCI controllers, including the D-Link DFE530TX.
 * Datasheets are available at http://www.via.com.tw.
 *
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */

/*
 * The VIA Rhine controllers are similar in some respects to the
 * the DEC tulip chips, except less complicated. The controller
 * uses an MII bus and an external physical layer interface. The
 * receiver has a one entry perfect filter and a 64-bit hash table
 * multicast filter. Transmit and receive descriptors are similar
 * to the tulip.
 *
 * The Rhine has a serious flaw in its transmit DMA mechanism:
 * transmit buffers must be longword aligned. Unfortunately,
 * FreeBSD doesn't guarantee that mbufs will be filled in starting
 * at longword boundaries, so we have to do a buffer copy before
 * transmission.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sockio.h>
#include <sys/mbuf.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/socket.h>

#include <net/if.h>
#include <sys/device.h>
#ifdef INET
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#endif	/* INET */
#include <net/if_dl.h>
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

#define VR_USEIOSPACE

#include <dev/pci/if_vrreg.h>

int vr_probe			__P((struct device *, void *, void *));
void vr_attach			__P((struct device *, struct device *, void *));

struct cfattach vr_ca = {
	sizeof(struct vr_softc), vr_probe, vr_attach
};
struct cfdriver vr_cd = {
	0, "vr", DV_IFNET
};

int vr_newbuf			__P((struct vr_softc *,
				     struct vr_chain_onefrag *,
				     struct mbuf *));
int vr_encap			__P((struct vr_softc *, struct vr_chain *,
				     struct mbuf * ));

void vr_rxeof			__P((struct vr_softc *));
void vr_rxeoc			__P((struct vr_softc *));
void vr_txeof			__P((struct vr_softc *));
void vr_txeoc			__P((struct vr_softc *));
void vr_tick			__P((void *));
int vr_intr			__P((void *));
void vr_start			__P((struct ifnet *));
int vr_ioctl			__P((struct ifnet *, u_long, caddr_t));
void vr_init			__P((void *));
void vr_stop			__P((struct vr_softc *));
void vr_watchdog		__P((struct ifnet *));
void vr_shutdown		__P((void *));
int vr_ifmedia_upd		__P((struct ifnet *));
void vr_ifmedia_sts		__P((struct ifnet *, struct ifmediareq *));

void vr_mii_sync		__P((struct vr_softc *));
void vr_mii_send		__P((struct vr_softc *, u_int32_t, int));
int vr_mii_readreg		__P((struct vr_softc *, struct vr_mii_frame *));
int vr_mii_writereg		__P((struct vr_softc *, struct vr_mii_frame *));
int vr_miibus_readreg		__P((struct device *, int, int));
void vr_miibus_writereg		__P((struct device *, int, int, int));
void vr_miibus_statchg		__P((struct device *));

void vr_setcfg			__P((struct vr_softc *, int));
u_int8_t vr_calchash		__P((u_int8_t *));
void vr_setmulti		__P((struct vr_softc *));
void vr_reset			__P((struct vr_softc *));
int vr_list_rx_init		__P((struct vr_softc *));
int vr_list_tx_init		__P((struct vr_softc *));

#define VR_SETBIT(sc, reg, x)				\
	CSR_WRITE_1(sc, reg,				\
		CSR_READ_1(sc, reg) | x)

#define VR_CLRBIT(sc, reg, x)				\
	CSR_WRITE_1(sc, reg,				\
		CSR_READ_1(sc, reg) & ~x)

#define VR_SETBIT16(sc, reg, x)				\
	CSR_WRITE_2(sc, reg,				\
		CSR_READ_2(sc, reg) | x)

#define VR_CLRBIT16(sc, reg, x)				\
	CSR_WRITE_2(sc, reg,				\
		CSR_READ_2(sc, reg) & ~x)

#define VR_SETBIT32(sc, reg, x)				\
	CSR_WRITE_4(sc, reg,				\
		CSR_READ_4(sc, reg) | x)

#define VR_CLRBIT32(sc, reg, x)				\
	CSR_WRITE_4(sc, reg,				\
		CSR_READ_4(sc, reg) & ~x)

#define SIO_SET(x)					\
	CSR_WRITE_1(sc, VR_MIICMD,			\
		CSR_READ_1(sc, VR_MIICMD) | x)

#define SIO_CLR(x)					\
	CSR_WRITE_1(sc, VR_MIICMD,			\
		CSR_READ_1(sc, VR_MIICMD) & ~x)

/*
 * Sync the PHYs by setting data bit and strobing the clock 32 times.
 */
void
vr_mii_sync(sc)
	struct vr_softc		*sc;
{
	register int		i;

	SIO_SET(VR_MIICMD_DIR|VR_MIICMD_DATAIN);

	for (i = 0; i < 32; i++) {
		SIO_SET(VR_MIICMD_CLK);
		DELAY(1);
		SIO_CLR(VR_MIICMD_CLK);
		DELAY(1);
	}

	return;
}

/*
 * Clock a series of bits through the MII.
 */
void
vr_mii_send(sc, bits, cnt)
	struct vr_softc		*sc;
	u_int32_t		bits;
	int			cnt;
{
	int			i;

	SIO_CLR(VR_MIICMD_CLK);

	for (i = (0x1 << (cnt - 1)); i; i >>= 1) {
                if (bits & i) {
			SIO_SET(VR_MIICMD_DATAIN);
                } else {
			SIO_CLR(VR_MIICMD_DATAIN);
                }
		DELAY(1);
		SIO_CLR(VR_MIICMD_CLK);
		DELAY(1);
		SIO_SET(VR_MIICMD_CLK);
	}
}

/*
 * Read an PHY register through the MII.
 */
int
vr_mii_readreg(sc, frame)
	struct vr_softc		*sc;
	struct vr_mii_frame	*frame;
	
{
	int			i, ack, s;

	s = splimp();

	/*
	 * Set up frame for RX.
	 */
	frame->mii_stdelim = VR_MII_STARTDELIM;
	frame->mii_opcode = VR_MII_READOP;
	frame->mii_turnaround = 0;
	frame->mii_data = 0;
	
	CSR_WRITE_1(sc, VR_MIICMD, 0);
	VR_SETBIT(sc, VR_MIICMD, VR_MIICMD_DIRECTPGM);

	/*
 	 * Turn on data xmit.
	 */
	SIO_SET(VR_MIICMD_DIR);

	vr_mii_sync(sc);

	/*
	 * Send command/address info.
	 */
	vr_mii_send(sc, frame->mii_stdelim, 2);
	vr_mii_send(sc, frame->mii_opcode, 2);
	vr_mii_send(sc, frame->mii_phyaddr, 5);
	vr_mii_send(sc, frame->mii_regaddr, 5);

	/* Idle bit */
	SIO_CLR((VR_MIICMD_CLK|VR_MIICMD_DATAIN));
	DELAY(1);
	SIO_SET(VR_MIICMD_CLK);
	DELAY(1);

	/* Turn off xmit. */
	SIO_CLR(VR_MIICMD_DIR);

	/* Check for ack */
	SIO_CLR(VR_MIICMD_CLK);
	DELAY(1);
	SIO_SET(VR_MIICMD_CLK);
	DELAY(1);
	ack = CSR_READ_4(sc, VR_MIICMD) & VR_MIICMD_DATAOUT;

	/*
	 * Now try reading data bits. If the ack failed, we still
	 * need to clock through 16 cycles to keep the PHY(s) in sync.
	 */
	if (ack) {
		for(i = 0; i < 16; i++) {
			SIO_CLR(VR_MIICMD_CLK);
			DELAY(1);
			SIO_SET(VR_MIICMD_CLK);
			DELAY(1);
		}
		goto fail;
	}

	for (i = 0x8000; i; i >>= 1) {
		SIO_CLR(VR_MIICMD_CLK);
		DELAY(1);
		if (!ack) {
			if (CSR_READ_4(sc, VR_MIICMD) & VR_MIICMD_DATAOUT)
				frame->mii_data |= i;
			DELAY(1);
		}
		SIO_SET(VR_MIICMD_CLK);
		DELAY(1);
	}

fail:

	SIO_CLR(VR_MIICMD_CLK);
	DELAY(1);
	SIO_SET(VR_MIICMD_CLK);
	DELAY(1);

	splx(s);

	if (ack)
		return(1);
	return(0);
}

/*
 * Write to a PHY register through the MII.
 */
int
vr_mii_writereg(sc, frame)
	struct vr_softc		*sc;
	struct vr_mii_frame	*frame;
	
{
	int			s;

	s = splimp();

	CSR_WRITE_1(sc, VR_MIICMD, 0);
	VR_SETBIT(sc, VR_MIICMD, VR_MIICMD_DIRECTPGM);

	/*
	 * Set up frame for TX.
	 */

	frame->mii_stdelim = VR_MII_STARTDELIM;
	frame->mii_opcode = VR_MII_WRITEOP;
	frame->mii_turnaround = VR_MII_TURNAROUND;
	
	/*
 	 * Turn on data output.
	 */
	SIO_SET(VR_MIICMD_DIR);

	vr_mii_sync(sc);

	vr_mii_send(sc, frame->mii_stdelim, 2);
	vr_mii_send(sc, frame->mii_opcode, 2);
	vr_mii_send(sc, frame->mii_phyaddr, 5);
	vr_mii_send(sc, frame->mii_regaddr, 5);
	vr_mii_send(sc, frame->mii_turnaround, 2);
	vr_mii_send(sc, frame->mii_data, 16);

	/* Idle bit. */
	SIO_SET(VR_MIICMD_CLK);
	DELAY(1);
	SIO_CLR(VR_MIICMD_CLK);
	DELAY(1);

	/*
	 * Turn off xmit.
	 */
	SIO_CLR(VR_MIICMD_DIR);

	splx(s);

	return(0);
}

int
vr_miibus_readreg(dev, phy, reg)
	struct device *dev;
	int phy, reg;
{
	struct vr_softc *sc = (struct vr_softc *)dev;
	struct vr_mii_frame frame;

	bzero((char *)&frame, sizeof(frame));

	frame.mii_phyaddr = phy;
	frame.mii_regaddr = reg;
	vr_mii_readreg(sc, &frame);

	return(frame.mii_data);
}

void
vr_miibus_writereg(dev, phy, reg, data)
	struct device *dev;
	int phy, reg, data;
{
	struct vr_softc *sc = (struct vr_softc *)dev;
	struct vr_mii_frame frame;

	bzero((char *)&frame, sizeof(frame));

	frame.mii_phyaddr = phy;
	frame.mii_regaddr = reg;
	frame.mii_data = data;

	vr_mii_writereg(sc, &frame);

	return;
}

void
vr_miibus_statchg(dev)
	struct device *dev;
{
	struct vr_softc *sc = (struct vr_softc *)dev;

	vr_setcfg(sc, sc->sc_mii.mii_media_active);
}

/*
 * Calculate CRC of a multicast group address, return the lower 6 bits.
 */
u_int8_t
vr_calchash(addr)
	u_int8_t		*addr;
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
	return((crc >> 26) & 0x0000003F);
}

/*
 * Program the 64-bit multicast hash filter.
 */
void
vr_setmulti(sc)
	struct vr_softc		*sc;
{
	struct ifnet		*ifp;
	int			h = 0;
	u_int32_t		hashes[2] = { 0, 0 };
	struct arpcom *ac = &sc->arpcom;
	struct ether_multi *enm;
	struct ether_multistep step;
	u_int8_t		rxfilt;
	int			mcnt = 0;

	ifp = &sc->arpcom.ac_if;

	rxfilt = CSR_READ_1(sc, VR_RXCFG);

	if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
		rxfilt |= VR_RXCFG_RX_MULTI;
		CSR_WRITE_1(sc, VR_RXCFG, rxfilt);
		CSR_WRITE_4(sc, VR_MAR0, 0xFFFFFFFF);
		CSR_WRITE_4(sc, VR_MAR1, 0xFFFFFFFF);
		return;
	}

	/* first, zot all the existing hash bits */
	CSR_WRITE_4(sc, VR_MAR0, 0);
	CSR_WRITE_4(sc, VR_MAR1, 0);

	/* now program new ones */
	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		h = vr_calchash(enm->enm_addrlo);
		if (h < 32)
			hashes[0] |= (1 << h);
		else
			hashes[1] |= (1 << (h - 32));
		mcnt++;

		ETHER_NEXT_MULTI(step, enm);
	}

	if (mcnt)
		rxfilt |= VR_RXCFG_RX_MULTI;
	else
		rxfilt &= ~VR_RXCFG_RX_MULTI;

	CSR_WRITE_4(sc, VR_MAR0, hashes[0]);
	CSR_WRITE_4(sc, VR_MAR1, hashes[1]);
	CSR_WRITE_1(sc, VR_RXCFG, rxfilt);

	return;
}

/*
 * In order to fiddle with the
 * 'full-duplex' and '100Mbps' bits in the netconfig register, we
 * first have to put the transmit and/or receive logic in the idle state.
 */
void
vr_setcfg(sc, media)
	struct vr_softc *sc;
	int media;
{
	int restart = 0;

	if (CSR_READ_2(sc, VR_COMMAND) & (VR_CMD_TX_ON|VR_CMD_RX_ON)) {
		restart = 1;
		VR_CLRBIT16(sc, VR_COMMAND, (VR_CMD_TX_ON|VR_CMD_RX_ON));
	}

	if ((media & IFM_GMASK) == IFM_FDX)
		VR_SETBIT16(sc, VR_COMMAND, VR_CMD_FULLDUPLEX);
	else
		VR_CLRBIT16(sc, VR_COMMAND, VR_CMD_FULLDUPLEX);

	if (restart)
		VR_SETBIT16(sc, VR_COMMAND, VR_CMD_TX_ON|VR_CMD_RX_ON);

	return;
}

void
vr_reset(sc)
	struct vr_softc		*sc;
{
	register int		i;

	VR_SETBIT16(sc, VR_COMMAND, VR_CMD_RESET);

	for (i = 0; i < VR_TIMEOUT; i++) {
		DELAY(10);
		if (!(CSR_READ_2(sc, VR_COMMAND) & VR_CMD_RESET))
			break;
	}
	if (i == VR_TIMEOUT)
		printf("%s: reset never completed!\n", sc->sc_dev.dv_xname);

	/* Wait a little while for the chip to get its brains in order. */
	DELAY(1000);

        return;
}

/*
 * Probe for a VIA Rhine chip.
 */
int
vr_probe(parent, match, aux)
	struct device *parent;
	void *match, *aux;
{
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_VIATECH) {
		switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_VIATECH_RHINE:
		case PCI_PRODUCT_VIATECH_RHINEII:
		case PCI_PRODUCT_VIATECH_RHINEII_2:
			return (1);
		}
	}

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_DELTA &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_DELTA_RHINEII)
		return (1);

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_ADDTRON &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ADDTRON_RHINEII)
		return (1);

	return (0);
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
void
vr_attach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	int			s, i;
	u_int32_t		command;
	struct vr_softc		*sc = (struct vr_softc *)self;
	struct pci_attach_args 	*pa = aux;
	pci_chipset_tag_t	pc = pa->pa_pc;
	pci_intr_handle_t	ih;
	const char		*intrstr = NULL;
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	bus_addr_t		iobase;
	bus_size_t		iosize;
	bus_dma_segment_t	seg;
	bus_dmamap_t		dmamap;
	int rseg;
	caddr_t kva;

	s = splimp();

	/*
	 * Handle power management nonsense.
	 */
	command = pci_conf_read(pa->pa_pc, pa->pa_tag,
					VR_PCI_CAPID) & 0x000000FF;
	if (command == 0x01) {
		command = pci_conf_read(pa->pa_pc, pa->pa_tag,
					VR_PCI_PWRMGMTCTRL);
		if (command & VR_PSTATE_MASK) {
			u_int32_t		iobase, membase, irq;

			/* Save important PCI config data. */
			iobase = pci_conf_read(pa->pa_pc, pa->pa_tag,
						VR_PCI_LOIO);
			membase = pci_conf_read(pa->pa_pc, pa->pa_tag,
						VR_PCI_LOMEM);
			irq = pci_conf_read(pa->pa_pc, pa->pa_tag,
						VR_PCI_INTLINE);

			/* Reset the power state. */
			command &= 0xFFFFFFFC;
			pci_conf_write(pa->pa_pc, pa->pa_tag,
						VR_PCI_PWRMGMTCTRL, command);

			/* Restore PCI config data. */
			pci_conf_write(pa->pa_pc, pa->pa_tag,
						VR_PCI_LOIO, iobase);
			pci_conf_write(pa->pa_pc, pa->pa_tag,
						VR_PCI_LOMEM, membase);
			pci_conf_write(pa->pa_pc, pa->pa_tag,
						VR_PCI_INTLINE, irq);
		}
	}

	/*
	 * Map control/status registers.
	 */
	command = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_COMMAND_STATUS_REG);

#ifdef VR_USEIOSPACE
	if (!(command & PCI_COMMAND_IO_ENABLE)) {
		printf(": failed to enable I/O ports\n");
		goto fail;
	}
	if (pci_io_find(pc, pa->pa_tag, VR_PCI_LOIO, &iobase, &iosize)) {
		printf(": failed to find i/o space\n");
		goto fail;
	}
	if (bus_space_map(pa->pa_iot, iobase, iosize, 0, &sc->vr_bhandle)) {
		printf(": failed map i/o space\n");
		goto fail;
	}
	sc->vr_btag = pa->pa_iot;
#else
	if (!(command & PCI_COMMAND_MEM_ENABLE)) {
		printf(": failed to enable memory mapping\n");
		goto fail;
	}
	if (pci_mem_find(pc, pa->pa_tag, VR_PCI_LOMEM, &iobase, &iosize)) {
		printf(": failed to find memory space\n");
		goto fail;
	}
	if (bus_space_map(pa->pa_memt, iobase, iosize, 0, &sc->vr_bhandle)) {
		printf(": failed map memory space\n");
		goto fail;
	}
	sc->vr_btag = pa->pa_memt;
#endif

	/* Allocate interrupt */
	if (pci_intr_map(pa, &ih)) {
		printf(": couldn't map interrupt\n");
		goto fail;
	}
	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_NET, vr_intr, sc,
				       self->dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": could not establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto fail;
	}
	printf(": %s", intrstr);

	/*
	 * Windows may put the chip in suspend mode when it
	 * shuts down. Be sure to kick it in the head to wake it
	 * up again.
	 */
	VR_CLRBIT(sc, VR_STICKHW, (VR_STICKHW_DS0|VR_STICKHW_DS1));

	/* Reset the adapter. */
	vr_reset(sc);

	/*
	 * Get station address. The way the Rhine chips work,
	 * you're not allowed to directly access the EEPROM once
	 * they've been programmed a special way. Consequently,
	 * we need to read the node address from the PAR0 and PAR1
	 * registers.
	 */
	VR_SETBIT(sc, VR_EECSR, VR_EECSR_LOAD);
	DELAY(1000);
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		sc->arpcom.ac_enaddr[i] = CSR_READ_1(sc, VR_PAR0 + i);

	/*
	 * A Rhine chip was detected. Inform the world.
	 */
	printf(" address %s\n", ether_sprintf(sc->arpcom.ac_enaddr));

	sc->sc_dmat = pa->pa_dmat;
	if (bus_dmamem_alloc(sc->sc_dmat, sizeof(struct vr_list_data),
	    PAGE_SIZE, 0, &seg, 1, &rseg, BUS_DMA_NOWAIT)) {
		printf("%s: can't alloc list\n", sc->sc_dev.dv_xname);
		goto fail;
	}
	if (bus_dmamem_map(sc->sc_dmat, &seg, rseg, sizeof(struct vr_list_data),
	    &kva, BUS_DMA_NOWAIT)) {
		printf("%s: can't map dma buffers (%d bytes)\n",
		    sc->sc_dev.dv_xname, sizeof(struct vr_list_data));
		bus_dmamem_free(sc->sc_dmat, &seg, rseg);
		goto fail;
	}
	if (bus_dmamap_create(sc->sc_dmat, sizeof(struct vr_list_data), 1,
	    sizeof(struct vr_list_data), 0, BUS_DMA_NOWAIT, &dmamap)) {
		printf("%s: can't create dma map\n", sc->sc_dev.dv_xname);
		bus_dmamem_unmap(sc->sc_dmat, kva, sizeof(struct vr_list_data));
		bus_dmamem_free(sc->sc_dmat, &seg, rseg);
		goto fail;
	}
	if (bus_dmamap_load(sc->sc_dmat, dmamap, kva,
	    sizeof(struct vr_list_data), NULL, BUS_DMA_NOWAIT)) {
		printf("%s: can't load dma map\n", sc->sc_dev.dv_xname);
		bus_dmamap_destroy(sc->sc_dmat, dmamap);
		bus_dmamem_unmap(sc->sc_dmat, kva, sizeof(struct vr_list_data));
		bus_dmamem_free(sc->sc_dmat, &seg, rseg);
		goto fail;
	}
	sc->vr_ldata = (struct vr_list_data *)kva;
	bzero(sc->vr_ldata, sizeof(struct vr_list_data));

	ifp = &sc->arpcom.ac_if;
	ifp->if_softc = sc;
	ifp->if_mtu = ETHERMTU;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
	ifp->if_ioctl = vr_ioctl;
	ifp->if_output = ether_output;
	ifp->if_start = vr_start;
	ifp->if_watchdog = vr_watchdog;
	ifp->if_baudrate = 10000000;
	IFQ_SET_READY(&ifp->if_snd);
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);

	/*
	 * Do MII setup.
	 */
	sc->sc_mii.mii_ifp = ifp;
	sc->sc_mii.mii_readreg = vr_miibus_readreg;
	sc->sc_mii.mii_writereg = vr_miibus_writereg;
	sc->sc_mii.mii_statchg = vr_miibus_statchg;
	ifmedia_init(&sc->sc_mii.mii_media, 0, vr_ifmedia_upd, vr_ifmedia_sts);
	mii_attach(self, &sc->sc_mii, 0xffffffff, MII_PHY_ANY, MII_OFFSET_ANY,
	    0);
	if (LIST_FIRST(&sc->sc_mii.mii_phys) == NULL) {
		ifmedia_add(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE, 0, NULL);
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_NONE);
	} else
		ifmedia_set(&sc->sc_mii.mii_media, IFM_ETHER|IFM_AUTO);
	timeout_set(&sc->sc_to, vr_tick, sc);

	/*
	 * Call MI attach routines.
	 */
	if_attach(ifp);
	ether_ifattach(ifp);

	shutdownhook_establish(vr_shutdown, sc);

fail:
	splx(s);
	return;
}

/*
 * Initialize the transmit descriptors.
 */
int
vr_list_tx_init(sc)
	struct vr_softc		*sc;
{
	struct vr_chain_data	*cd;
	struct vr_list_data	*ld;
	int			i;

	cd = &sc->vr_cdata;
	ld = sc->vr_ldata;
	for (i = 0; i < VR_TX_LIST_CNT; i++) {
		cd->vr_tx_chain[i].vr_ptr = &ld->vr_tx_list[i];
		if (i == (VR_TX_LIST_CNT - 1))
			cd->vr_tx_chain[i].vr_nextdesc = 
				&cd->vr_tx_chain[0];
		else
			cd->vr_tx_chain[i].vr_nextdesc =
				&cd->vr_tx_chain[i + 1];
	}

	cd->vr_tx_free = &cd->vr_tx_chain[0];
	cd->vr_tx_tail = cd->vr_tx_head = NULL;

	return(0);
}


/*
 * Initialize the RX descriptors and allocate mbufs for them. Note that
 * we arrange the descriptors in a closed ring, so that the last descriptor
 * points back to the first.
 */
int
vr_list_rx_init(sc)
	struct vr_softc		*sc;
{
	struct vr_chain_data	*cd;
	struct vr_list_data	*ld;
	int			i;

	cd = &sc->vr_cdata;
	ld = sc->vr_ldata;

	for (i = 0; i < VR_RX_LIST_CNT; i++) {
		cd->vr_rx_chain[i].vr_ptr =
			(struct vr_desc *)&ld->vr_rx_list[i];
		if (vr_newbuf(sc, &cd->vr_rx_chain[i], NULL) == ENOBUFS)
			return(ENOBUFS);
		if (i == (VR_RX_LIST_CNT - 1)) {
			cd->vr_rx_chain[i].vr_nextdesc =
					&cd->vr_rx_chain[0];
			ld->vr_rx_list[i].vr_next =
					vtophys(&ld->vr_rx_list[0]);
		} else {
			cd->vr_rx_chain[i].vr_nextdesc =
					&cd->vr_rx_chain[i + 1];
			ld->vr_rx_list[i].vr_next =
					vtophys(&ld->vr_rx_list[i + 1]);
		}
	}

	cd->vr_rx_head = &cd->vr_rx_chain[0];

	return(0);
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 * Note: the length fields are only 11 bits wide, which means the
 * largest size we can specify is 2047. This is important because
 * MCLBYTES is 2048, so we have to subtract one otherwise we'll
 * overflow the field and make a mess.
 */
int
vr_newbuf(sc, c, m)
	struct vr_softc		*sc;
	struct vr_chain_onefrag	*c;
	struct mbuf		*m;
{
	struct mbuf		*m_new = NULL;

	if (m == NULL) {
		MGETHDR(m_new, M_DONTWAIT, MT_DATA);
		if (m_new == NULL)
			return(ENOBUFS);

		MCLGET(m_new, M_DONTWAIT);
		if (!(m_new->m_flags & M_EXT)) {
			m_freem(m_new);
			return(ENOBUFS);
		}
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
	} else {
		m_new = m;
		m_new->m_len = m_new->m_pkthdr.len = MCLBYTES;
		m_new->m_data = m_new->m_ext.ext_buf;
	}

	m_adj(m_new, sizeof(u_int64_t));

	c->vr_mbuf = m_new;
	c->vr_ptr->vr_status = VR_RXSTAT;
	c->vr_ptr->vr_data = vtophys(mtod(m_new, caddr_t));
	c->vr_ptr->vr_ctl = VR_RXCTL | VR_RXLEN;

	return(0);
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
void
vr_rxeof(sc)
	struct vr_softc		*sc;
{
        struct mbuf		*m;
        struct ifnet		*ifp;
	struct vr_chain_onefrag	*cur_rx;
	int			total_len = 0;
	u_int32_t		rxstat;

	ifp = &sc->arpcom.ac_if;

	while(!((rxstat = sc->vr_cdata.vr_rx_head->vr_ptr->vr_status) &
							VR_RXSTAT_OWN)) {
		struct mbuf		*m0 = NULL;

		cur_rx = sc->vr_cdata.vr_rx_head;
		sc->vr_cdata.vr_rx_head = cur_rx->vr_nextdesc;
		m = cur_rx->vr_mbuf;

		/*
		 * If an error occurs, update stats, clear the
		 * status word and leave the mbuf cluster in place:
		 * it should simply get re-used next time this descriptor
	 	 * comes up in the ring.
		 */
		if (rxstat & VR_RXSTAT_RXERR) {
			ifp->if_ierrors++;
			printf("%s: rx error: ", sc->sc_dev.dv_xname);
			switch(rxstat & 0x000000FF) {
			case VR_RXSTAT_CRCERR:
				printf("crc error\n");
				break;
			case VR_RXSTAT_FRAMEALIGNERR:
				printf("frame alignment error\n");
				break;
			case VR_RXSTAT_FIFOOFLOW:
				printf("FIFO overflow\n");
				break;
			case VR_RXSTAT_GIANT:
				printf("received giant packet\n");
				break;
			case VR_RXSTAT_RUNT:
				printf("received runt packet\n");
				break;
			case VR_RXSTAT_BUSERR:
				printf("system bus error\n");
				break;
			case VR_RXSTAT_BUFFERR:
				printf("rx buffer error\n");
				break;
			default:
				printf("unknown rx error\n");
				break;
			}
			vr_newbuf(sc, cur_rx, m);
			continue;
		}

		/* No errors; receive the packet. */	
		total_len = VR_RXBYTES(cur_rx->vr_ptr->vr_status);

		/*
		 * XXX The VIA Rhine chip includes the CRC with every
		 * received frame, and there's no way to turn this
		 * behavior off (at least, I can't find anything in
	 	 * the manual that explains how to do it) so we have
		 * to trim off the CRC manually.
		 */
		total_len -= ETHER_CRC_LEN;

		m0 = m_devget(mtod(m, char *) - ETHER_ALIGN,
		    total_len + ETHER_ALIGN, 0, ifp, NULL);
		vr_newbuf(sc, cur_rx, m);
		if (m0 == NULL) {
			ifp->if_ierrors++;
			continue;
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

void
vr_rxeoc(sc)
	struct vr_softc		*sc;
{

	vr_rxeof(sc);
	VR_CLRBIT16(sc, VR_COMMAND, VR_CMD_RX_ON);
	CSR_WRITE_4(sc, VR_RXADDR, vtophys(sc->vr_cdata.vr_rx_head->vr_ptr));
	VR_SETBIT16(sc, VR_COMMAND, VR_CMD_RX_ON);
	VR_SETBIT16(sc, VR_COMMAND, VR_CMD_RX_GO);

	return;
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */

void
vr_txeof(sc)
	struct vr_softc		*sc;
{
	struct vr_chain		*cur_tx;
	struct ifnet		*ifp;

	ifp = &sc->arpcom.ac_if;

	/* Clear the timeout timer. */
	ifp->if_timer = 0;

	/* Sanity check. */
	if (sc->vr_cdata.vr_tx_head == NULL)
		return;

	/*
	 * Go through our tx list and free mbufs for those
	 * frames that have been transmitted.
	 */
	while(sc->vr_cdata.vr_tx_head->vr_mbuf != NULL) {
		u_int32_t		txstat;

		cur_tx = sc->vr_cdata.vr_tx_head;
		txstat = cur_tx->vr_ptr->vr_status;

		if (txstat & VR_TXSTAT_OWN)
			break;

		if (txstat & VR_TXSTAT_ERRSUM) {
			ifp->if_oerrors++;
			if (txstat & VR_TXSTAT_DEFER)
				ifp->if_collisions++;
			if (txstat & VR_TXSTAT_LATECOLL)
				ifp->if_collisions++;
		}

		ifp->if_collisions +=(txstat & VR_TXSTAT_COLLCNT) >> 3;

		ifp->if_opackets++;
		if (cur_tx->vr_mbuf != NULL) {
			m_freem(cur_tx->vr_mbuf);
			cur_tx->vr_mbuf = NULL;
		}

		if (sc->vr_cdata.vr_tx_head == sc->vr_cdata.vr_tx_tail) {
			sc->vr_cdata.vr_tx_head = NULL;
			sc->vr_cdata.vr_tx_tail = NULL;
			break;
		}

		sc->vr_cdata.vr_tx_head = cur_tx->vr_nextdesc;
	}

	return;
}

/*
 * TX 'end of channel' interrupt handler.
 */
void
vr_txeoc(sc)
	struct vr_softc		*sc;
{
	struct ifnet		*ifp;

	ifp = &sc->arpcom.ac_if;

	ifp->if_timer = 0;

	if (sc->vr_cdata.vr_tx_head == NULL) {
		ifp->if_flags &= ~IFF_OACTIVE;
		sc->vr_cdata.vr_tx_tail = NULL;
	}

	return;
}

void
vr_tick(xsc)
	void *xsc;
{
	struct vr_softc *sc = xsc;
	int s;

	s = splimp();
	mii_tick(&sc->sc_mii);
	timeout_add(&sc->sc_to, hz);
	splx(s);
}

int
vr_intr(arg)
	void			*arg;
{
	struct vr_softc		*sc;
	struct ifnet		*ifp;
	u_int16_t		status;
	int claimed = 0;

	sc = arg;
	ifp = &sc->arpcom.ac_if;

	/* Supress unwanted interrupts. */
	if (!(ifp->if_flags & IFF_UP)) {
		vr_stop(sc);
		return 0;
	}

	/* Disable interrupts. */
	CSR_WRITE_2(sc, VR_IMR, 0x0000);

	for (;;) {

		status = CSR_READ_2(sc, VR_ISR);
		if (status)
			CSR_WRITE_2(sc, VR_ISR, status);

		if ((status & VR_INTRS) == 0)
			break;

		claimed = 1;

		if (status & VR_ISR_RX_OK)
			vr_rxeof(sc);

		if ((status & VR_ISR_RX_ERR) || (status & VR_ISR_RX_NOBUF) ||
		    (status & VR_ISR_RX_NOBUF) || (status & VR_ISR_RX_OFLOW) ||
		    (status & VR_ISR_RX_DROPPED)) {
			vr_rxeof(sc);
			vr_rxeoc(sc);
		}

		if (status & VR_ISR_TX_OK) {
			vr_txeof(sc);
			vr_txeoc(sc);
		}

		if ((status & VR_ISR_TX_UNDERRUN)||(status & VR_ISR_TX_ABRT)){ 
			ifp->if_oerrors++;
			vr_txeof(sc);
			if (sc->vr_cdata.vr_tx_head != NULL) {
				VR_SETBIT16(sc, VR_COMMAND, VR_CMD_TX_ON);
				VR_SETBIT16(sc, VR_COMMAND, VR_CMD_TX_GO);
			}
		}

		if (status & VR_ISR_BUSERR) {
			vr_reset(sc);
			vr_init(sc);
		}
	}

	/* Re-enable interrupts. */
	CSR_WRITE_2(sc, VR_IMR, VR_INTRS);

	if (!IFQ_IS_EMPTY(&ifp->if_snd)) {
		vr_start(ifp);
	}

	return (claimed);
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
int
vr_encap(sc, c, m_head)
	struct vr_softc		*sc;
	struct vr_chain		*c;
	struct mbuf		*m_head;
{
	int			frag = 0;
	struct vr_desc		*f = NULL;
	int			total_len;
	struct mbuf		*m;

	m = m_head;
	total_len = 0;

	/*
	 * The VIA Rhine wants packet buffers to be longword
	 * aligned, but very often our mbufs aren't. Rather than
	 * waste time trying to decide when to copy and when not
	 * to copy, just do it all the time.
	 */
	if (m != NULL) {
		struct mbuf		*m_new = NULL;

		MGETHDR(m_new, M_DONTWAIT, MT_DATA);
		if (m_new == NULL) {
			return(1);
		}
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
		/*
		 * The Rhine chip doesn't auto-pad, so we have to make
		 * sure to pad short frames out to the minimum frame length
		 * ourselves.
		 */
		if (m_head->m_len < VR_MIN_FRAMELEN) {
			m_new->m_pkthdr.len += VR_MIN_FRAMELEN - m_new->m_len;
			m_new->m_len = m_new->m_pkthdr.len;
		}
		f = c->vr_ptr;
		f->vr_data = vtophys(mtod(m_new, caddr_t));
		f->vr_ctl = total_len = m_new->m_len;
		f->vr_ctl |= VR_TXCTL_TLINK|VR_TXCTL_FIRSTFRAG;
		f->vr_status = 0;
		frag = 1;
	}

	c->vr_mbuf = m_head;
	c->vr_ptr->vr_ctl |= VR_TXCTL_LASTFRAG|VR_TXCTL_FINT;
	c->vr_ptr->vr_next = vtophys(c->vr_nextdesc->vr_ptr);

	return(0);
}

/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit lists. We also save a
 * copy of the pointers since the transmit list fragment pointers are
 * physical addresses.
 */

void
vr_start(ifp)
	struct ifnet		*ifp;
{
	struct vr_softc		*sc;
	struct mbuf		*m_head = NULL;
	struct vr_chain		*cur_tx = NULL, *start_tx;

	sc = ifp->if_softc;

	if (ifp->if_flags & IFF_OACTIVE)
		return;

	/*
	 * Check for an available queue slot. If there are none,
	 * punt.
	 */
	if (sc->vr_cdata.vr_tx_free->vr_mbuf != NULL) {
		ifp->if_flags |= IFF_OACTIVE;
		return;
	}

	start_tx = sc->vr_cdata.vr_tx_free;

	while(sc->vr_cdata.vr_tx_free->vr_mbuf == NULL) {
		IFQ_POLL(&ifp->if_snd, m_head);
		if (m_head == NULL)
			break;

		/* Pick a descriptor off the free list. */
		cur_tx = sc->vr_cdata.vr_tx_free;
		sc->vr_cdata.vr_tx_free = cur_tx->vr_nextdesc;

		/* Pack the data into the descriptor. */
		if (vr_encap(sc, cur_tx, m_head)) {
			ifp->if_flags |= IFF_OACTIVE;
			cur_tx = NULL;
			break;
		}

		if (cur_tx != start_tx)
			VR_TXOWN(cur_tx) = VR_TXSTAT_OWN;

		/* now we are committed to transmit the packet */
		IFQ_DEQUEUE(&ifp->if_snd, m_head);

#if NBPFILTER > 0
		/*
		 * If there's a BPF listener, bounce a copy of this frame
		 * to him.
		 */
		if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, cur_tx->vr_mbuf);
#endif
		VR_TXOWN(cur_tx) = VR_TXSTAT_OWN;
		VR_SETBIT16(sc, VR_COMMAND, /*VR_CMD_TX_ON|*/VR_CMD_TX_GO);
	}

	/*
	 * If there are no frames queued, bail.
	 */
	if (cur_tx == NULL)
		return;

	sc->vr_cdata.vr_tx_tail = cur_tx;

	if (sc->vr_cdata.vr_tx_head == NULL)
		sc->vr_cdata.vr_tx_head = start_tx;

	/*
	 * Set a timeout in case the chip goes out to lunch.
	 */
	ifp->if_timer = 5;

	return;
}

void
vr_init(xsc)
	void			*xsc;
{
	struct vr_softc		*sc = xsc;
	struct ifnet		*ifp = &sc->arpcom.ac_if;
	struct mii_data		*mii = &sc->sc_mii;
	int			s, i;

	s = splimp();

	/*
	 * Cancel pending I/O and free all RX/TX buffers.
	 */
	vr_stop(sc);
	vr_reset(sc);

	/*
	 * Set our station address.
	 */
	for (i = 0; i < ETHER_ADDR_LEN; i++)
		CSR_WRITE_1(sc, VR_PAR0 + i, sc->arpcom.ac_enaddr[i]);

	VR_CLRBIT(sc, VR_RXCFG, VR_RXCFG_RX_THRESH);
	VR_SETBIT(sc, VR_RXCFG, VR_RXTHRESH_STORENFWD);

	VR_CLRBIT(sc, VR_TXCFG, VR_TXCFG_TX_THRESH);
	VR_SETBIT(sc, VR_TXCFG, VR_TXTHRESH_STORENFWD);

	/* Init circular RX list. */
	if (vr_list_rx_init(sc) == ENOBUFS) {
		printf("%s: initialization failed: no memory for rx buffers\n",
							sc->sc_dev.dv_xname);
		vr_stop(sc);
		splx(s);
		return;
	}

	/*
	 * Init tx descriptors.
	 */
	vr_list_tx_init(sc);

	/* If we want promiscuous mode, set the allframes bit. */
	if (ifp->if_flags & IFF_PROMISC)
		VR_SETBIT(sc, VR_RXCFG, VR_RXCFG_RX_PROMISC);
	else
		VR_CLRBIT(sc, VR_RXCFG, VR_RXCFG_RX_PROMISC);

	/* Set capture broadcast bit to capture broadcast frames. */
	if (ifp->if_flags & IFF_BROADCAST)
		VR_SETBIT(sc, VR_RXCFG, VR_RXCFG_RX_BROAD);
	else
		VR_CLRBIT(sc, VR_RXCFG, VR_RXCFG_RX_BROAD);

	/*
	 * Program the multicast filter, if necessary.
	 */
	vr_setmulti(sc);

	/*
	 * Load the address of the RX list.
	 */
	CSR_WRITE_4(sc, VR_RXADDR, vtophys(sc->vr_cdata.vr_rx_head->vr_ptr));

	/* Enable receiver and transmitter. */
	CSR_WRITE_2(sc, VR_COMMAND, VR_CMD_TX_NOPOLL|VR_CMD_START|
				    VR_CMD_TX_ON|VR_CMD_RX_ON|
				    VR_CMD_RX_GO);

	CSR_WRITE_4(sc, VR_TXADDR, vtophys(&sc->vr_ldata->vr_tx_list[0]));

	/*
	 * Enable interrupts.
	 */
	CSR_WRITE_2(sc, VR_ISR, 0xFFFF);
	CSR_WRITE_2(sc, VR_IMR, VR_INTRS);

	/* Restore state of BMCR */
	mii_mediachg(mii);

	ifp->if_flags |= IFF_RUNNING;
	ifp->if_flags &= ~IFF_OACTIVE;

	if (!timeout_pending(&sc->sc_to))
		timeout_add(&sc->sc_to, hz);

	splx(s);
}

/*
 * Set media options.
 */
int
vr_ifmedia_upd(ifp)
	struct ifnet		*ifp;
{
	struct vr_softc		*sc = ifp->if_softc;

	if (ifp->if_flags & IFF_UP)
		vr_init(sc);

	return(0);
}

/*
 * Report current media status.
 */
void
vr_ifmedia_sts(ifp, ifmr)
	struct ifnet		*ifp;
	struct ifmediareq	*ifmr;
{
	struct vr_softc		*sc = ifp->if_softc;
	struct mii_data		*mii = &sc->sc_mii;

	mii_pollstat(mii);
	ifmr->ifm_active = mii->mii_media_active;
	ifmr->ifm_status = mii->mii_media_status;
}

int
vr_ioctl(ifp, command, data)
	struct ifnet		*ifp;
	u_long			command;
	caddr_t			data;
{
	struct vr_softc		*sc = ifp->if_softc;
	struct ifreq		*ifr = (struct ifreq *) data;
	int			s, error = 0;
	struct ifaddr *ifa = (struct ifaddr *)data;

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
			vr_init(sc);
			arp_ifinit(&sc->arpcom, ifa);
			break;
#endif	/* INET */
		default:
			vr_init(sc);
			break;
		}
		break;
	case SIOCSIFFLAGS:
		if (ifp->if_flags & IFF_UP) {
			vr_init(sc);
		} else {
			if (ifp->if_flags & IFF_RUNNING)
				vr_stop(sc);
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
			vr_setmulti(sc);
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

	splx(s);

	return(error);
}

void
vr_watchdog(ifp)
	struct ifnet		*ifp;
{
	struct vr_softc		*sc;

	sc = ifp->if_softc;

	ifp->if_oerrors++;
	printf("%s: watchdog timeout\n", sc->sc_dev.dv_xname);

	vr_stop(sc);
	vr_reset(sc);
	vr_init(sc);

	if (!IFQ_IS_EMPTY(&ifp->if_snd))
		vr_start(ifp);

	return;
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
void
vr_stop(sc)
	struct vr_softc		*sc;
{
	register int		i;
	struct ifnet		*ifp;

	ifp = &sc->arpcom.ac_if;
	ifp->if_timer = 0;

	if (timeout_pending(&sc->sc_to))
		timeout_del(&sc->sc_to);

	VR_SETBIT16(sc, VR_COMMAND, VR_CMD_STOP);
	VR_CLRBIT16(sc, VR_COMMAND, (VR_CMD_RX_ON|VR_CMD_TX_ON));
	CSR_WRITE_2(sc, VR_IMR, 0x0000);
	CSR_WRITE_4(sc, VR_TXADDR, 0x00000000);
	CSR_WRITE_4(sc, VR_RXADDR, 0x00000000);

	/*
	 * Free data in the RX lists.
	 */
	for (i = 0; i < VR_RX_LIST_CNT; i++) {
		if (sc->vr_cdata.vr_rx_chain[i].vr_mbuf != NULL) {
			m_freem(sc->vr_cdata.vr_rx_chain[i].vr_mbuf);
			sc->vr_cdata.vr_rx_chain[i].vr_mbuf = NULL;
		}
	}
	bzero((char *)&sc->vr_ldata->vr_rx_list,
		sizeof(sc->vr_ldata->vr_rx_list));

	/*
	 * Free the TX list buffers.
	 */
	for (i = 0; i < VR_TX_LIST_CNT; i++) {
		if (sc->vr_cdata.vr_tx_chain[i].vr_mbuf != NULL) {
			m_freem(sc->vr_cdata.vr_tx_chain[i].vr_mbuf);
			sc->vr_cdata.vr_tx_chain[i].vr_mbuf = NULL;
		}
	}

	bzero((char *)&sc->vr_ldata->vr_tx_list,
		sizeof(sc->vr_ldata->vr_tx_list));

	ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);

	return;
}

/*
 * Stop all chip I/O so that the kernel's probe routines don't
 * get confused by errant DMAs when rebooting.
 */
void
vr_shutdown(arg)
	void			*arg;
{
	struct vr_softc		*sc = (struct vr_softc *)arg;

	vr_stop(sc);
}
