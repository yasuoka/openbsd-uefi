/*	$OpenBSD: if_ax.c,v 1.2 1999/09/26 18:54:50 aaron Exp $ */

/*
 * Copyright (c) 1997, 1998, 1999
 *      Bill Paul <wpaul@ctr.columbia.edu>.  All rights reserved.
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
 *      This product includes software developed by Bill Paul.
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
 *      $Id: if_ax.c,v 1.2 1999/09/26 18:54:50 aaron Exp $
 */

/*
 * ASIX AX88140A and AX88141 fast ethernet PCI NIC driver.
 *
 * Written by Bill Paul <wpaul@ctr.columbia.edu>
 * Electrical Engineering Department
 * Columbia University, New York City
 */

/*
 * The ASIX Electronics AX88140A is still another DEC 21x4x clone. It's
 * a reasonably close copy of the tulip, except for the receiver filter
 * programming. Where the DEC chip has a special setup frame that
 * needs to be downloaded into the transmit DMA engine, the ASIX chip
 * has a less complicated setup frame which is written into one of
 * the registers.
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
#include <sys/socket.h>

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

#include <vm/vm.h>              /* for vtophys */
#include <vm/pmap.h>            /* for vtophys */

#include <sys/device.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#define AX_USEIOSPACE

/* #define AX_BACKGROUND_AUTONEG */

#include <dev/pci/if_axreg.h>

/*
 * Various supported PHY vendors/types and their names. Note that
 * this driver will work with pretty much any MII-compliant PHY,
 * so failure to positively identify the chip is not a fatal error.
 */

struct ax_type ax_phys[] = {
        { TI_PHY_VENDORID, TI_PHY_10BT, "<TI ThunderLAN 10BT (internal)>" },
        { TI_PHY_VENDORID, TI_PHY_100VGPMI, "<TI TNETE211 100VG Any-LAN>" },
        { NS_PHY_VENDORID, NS_PHY_83840A, "<National Semiconductor DP83840A>"},
        { LEVEL1_PHY_VENDORID, LEVEL1_PHY_LXT970, "<Level 1 LXT970>" },
        { INTEL_PHY_VENDORID, INTEL_PHY_82555, "<Intel 82555>" },
        { SEEQ_PHY_VENDORID, SEEQ_PHY_80220, "<SEEQ 80220>" },
        { 0, 0, "<MII-compliant physical interface>" }
};

int ax_probe	__P((struct device *, void *, void *));
void ax_attach	__P((struct device *, struct device *, void *));
int ax_intr		__P((void *));
void ax_shutdown         __P((void *));
int ax_newbuf            __P((struct ax_softc *,
                                                struct ax_chain_onefrag *));
int ax_encap             __P((struct ax_softc *, struct ax_chain *,
                                                struct mbuf *));
void ax_rxeof            __P((struct ax_softc *));
void ax_rxeoc            __P((struct ax_softc *));
void ax_txeof            __P((struct ax_softc *));
void ax_txeoc            __P((struct ax_softc *));
void ax_start            __P((struct ifnet *));
int ax_ioctl             __P((struct ifnet *, u_long, caddr_t));
void ax_init             __P((void *));
void ax_stop             __P((struct ax_softc *));
void ax_watchdog         __P((struct ifnet *));
u_int32_t ax_calchash	__P((caddr_t));
void ax_autoneg_mii	__P((struct ax_softc *, int, int));
void ax_setmode_mii	__P((struct ax_softc *, int));
void ax_ifmedia_sts	__P((struct ifnet *, struct ifmediareq *));
void ax_getmode_mii	__P((struct ax_softc *));
void ax_setcfg		__P((struct ax_softc *, int));
void ax_autoneg_xmit	__P((struct ax_softc *));
int ax_ifmedia_upd       __P((struct ifnet *));
void ax_setmode		__P((struct ax_softc *, int, int));

void ax_delay            __P((struct ax_softc *));
void ax_eeprom_idle      __P((struct ax_softc *));
void ax_eeprom_putbyte   __P((struct ax_softc *, int));
void ax_eeprom_getword   __P((struct ax_softc *, int, u_int16_t *));
void ax_read_eeprom      __P((struct ax_softc *, caddr_t, int,
                                                        int, int));

void ax_mii_writebit     __P((struct ax_softc *, int));
int ax_mii_readbit       __P((struct ax_softc *));
void ax_mii_sync         __P((struct ax_softc *));
void ax_mii_send         __P((struct ax_softc *, u_int32_t, int));
int ax_mii_readreg       __P((struct ax_softc *, struct ax_mii_frame *))
;
int ax_mii_writereg      __P((struct ax_softc *, struct ax_mii_frame *))
;
u_int16_t ax_phy_readreg __P((struct ax_softc *, int));
void ax_phy_writereg     __P((struct ax_softc *, int, int));

void ax_setmulti         __P((struct ax_softc *));
void ax_reset            __P((struct ax_softc *));
int ax_list_rx_init      __P((struct ax_softc *));
int ax_list_tx_init      __P((struct ax_softc *));

#define AX_SETBIT(sc, reg, x)                           \
        CSR_WRITE_4(sc, reg,                            \
                CSR_READ_4(sc, reg) | x)

#define AX_CLRBIT(sc, reg, x)                           \
        CSR_WRITE_4(sc, reg,                            \
                CSR_READ_4(sc, reg) & ~x)

#define SIO_SET(x)                                      \
        CSR_WRITE_4(sc, AX_SIO,                         \
                CSR_READ_4(sc, AX_SIO) | x)

#define SIO_CLR(x)                                      \
        CSR_WRITE_4(sc, AX_SIO,                         \
                CSR_READ_4(sc, AX_SIO) & ~x)

void ax_delay(sc)
        struct ax_softc         *sc;
{
        int                     idx;

        for (idx = (300 / 33) + 1; idx > 0; idx--)
                CSR_READ_4(sc, AX_BUSCTL);
}

void ax_eeprom_idle(sc)
        struct ax_softc         *sc;
{
        register int            i;

        CSR_WRITE_4(sc, AX_SIO, AX_SIO_EESEL);
        ax_delay(sc);
        AX_SETBIT(sc, AX_SIO,  AX_SIO_ROMCTL_READ);
        ax_delay(sc);
        AX_SETBIT(sc, AX_SIO, AX_SIO_EE_CS);
        ax_delay(sc);
        AX_SETBIT(sc, AX_SIO, AX_SIO_EE_CLK);
        ax_delay(sc);

        for (i = 0; i < 25; i++) {
                AX_CLRBIT(sc, AX_SIO, AX_SIO_EE_CLK);
                ax_delay(sc);
                AX_SETBIT(sc, AX_SIO, AX_SIO_EE_CLK);
                ax_delay(sc);
        }

        AX_CLRBIT(sc, AX_SIO, AX_SIO_EE_CLK);
        ax_delay(sc);
        AX_CLRBIT(sc, AX_SIO, AX_SIO_EE_CS);
        ax_delay(sc);
        CSR_WRITE_4(sc, AX_SIO, 0x00000000);

        return;
}

/*
 * Send a read command and address to the EEPROM, check for ACK.
 */
void ax_eeprom_putbyte(sc, addr)
        struct ax_softc         *sc;
        int                     addr;
{
        register int            d, i;

        d = addr | AX_EECMD_READ;

        /*
         * Feed in each bit and stobe the clock.
         */
        for (i = 0x400; i; i >>= 1) {
                if (d & i) {
                        SIO_SET(AX_SIO_EE_DATAIN);
                } else {
                        SIO_CLR(AX_SIO_EE_DATAIN);
                }
                ax_delay(sc);
                SIO_SET(AX_SIO_EE_CLK);
                ax_delay(sc);
                SIO_CLR(AX_SIO_EE_CLK);
                ax_delay(sc);
        }

        return;
}

/*
 * Read a word of data stored in the EEPROM at address 'addr.'
 */
void ax_eeprom_getword(sc, addr, dest)
        struct ax_softc         *sc;
        int                     addr;
        u_int16_t               *dest;
{
        register int            i;
        u_int16_t               word = 0;

        /* Force EEPROM to idle state. */
        ax_eeprom_idle(sc);

        /* Enter EEPROM access mode. */
        CSR_WRITE_4(sc, AX_SIO, AX_SIO_EESEL);
        ax_delay(sc);
        AX_SETBIT(sc, AX_SIO,  AX_SIO_ROMCTL_READ);
        ax_delay(sc);
        AX_SETBIT(sc, AX_SIO, AX_SIO_EE_CS);
        ax_delay(sc);
        AX_SETBIT(sc, AX_SIO, AX_SIO_EE_CLK);
        ax_delay(sc);

        /*
         * Send address of word we want to read.
         */
        ax_eeprom_putbyte(sc, addr);

        /*
         * Start reading bits from EEPROM.
         */
        for (i = 0x8000; i; i >>= 1) {
                SIO_SET(AX_SIO_EE_CLK);
                ax_delay(sc);
                if (CSR_READ_4(sc, AX_SIO) & AX_SIO_EE_DATAOUT)
                        word |= i;
                ax_delay(sc);
                SIO_CLR(AX_SIO_EE_CLK);
                ax_delay(sc);
        }

        /* Turn off EEPROM access mode. */
        ax_eeprom_idle(sc);

        *dest = word;

        return;
}

/*
 * Read a sequence of words from the EEPROM.
 */
void ax_read_eeprom(sc, dest, off, cnt, swap)
        struct ax_softc         *sc;
        caddr_t                 dest;
        int                     off;
        int                     cnt;
        int                     swap;
{
        int                     i;
        u_int16_t               word = 0, *ptr;

        for (i = 0; i < cnt; i++) {
                ax_eeprom_getword(sc, off + i, &word);
                ptr = (u_int16_t *)(dest + (i * 2));
                if (swap)
                        *ptr = ntohs(word);
                else
                        *ptr = word;
        }

        return;
}

/*
 * Write a bit to the MII bus.
 */
void ax_mii_writebit(sc, bit)
        struct ax_softc         *sc;
        int                     bit;
{
        if (bit)
                CSR_WRITE_4(sc, AX_SIO, AX_SIO_ROMCTL_WRITE|AX_SIO_MII_DATAOUT)
;
        else
                CSR_WRITE_4(sc, AX_SIO, AX_SIO_ROMCTL_WRITE);

        AX_SETBIT(sc, AX_SIO, AX_SIO_MII_CLK);
        AX_CLRBIT(sc, AX_SIO, AX_SIO_MII_CLK);

        return;
}

/*
 * Read a bit from the MII bus.
 */
int ax_mii_readbit(sc)
        struct ax_softc         *sc;
{
        CSR_WRITE_4(sc, AX_SIO, AX_SIO_ROMCTL_READ|AX_SIO_MII_DIR);
        CSR_READ_4(sc, AX_SIO);
        AX_SETBIT(sc, AX_SIO, AX_SIO_MII_CLK);
        AX_CLRBIT(sc, AX_SIO, AX_SIO_MII_CLK);
        if (CSR_READ_4(sc, AX_SIO) & AX_SIO_MII_DATAIN)
                return(1);

        return(0);
}

/*
 * Sync the PHYs by setting data bit and strobing the clock 32 times.
 */
void ax_mii_sync(sc)
        struct ax_softc         *sc;
{
        register int            i;

        CSR_WRITE_4(sc, AX_SIO, AX_SIO_ROMCTL_WRITE);

        for (i = 0; i < 32; i++)
                ax_mii_writebit(sc, 1);

        return;
}

/*
 * Clock a series of bits through the MII.
 */
void ax_mii_send(sc, bits, cnt)
        struct ax_softc         *sc;
        u_int32_t               bits;
        int                     cnt;
{
        int                     i;

        for (i = (0x1 << (cnt - 1)); i; i >>= 1)
                ax_mii_writebit(sc, bits & i);
}

/*
 * Read an PHY register through the MII.
 */
int ax_mii_readreg(sc, frame)
        struct ax_softc         *sc;
        struct ax_mii_frame     *frame;
        
{
        int                     i, ack, s;

        s = splimp();

        /*
         * Set up frame for RX.
         */
        frame->mii_stdelim = AX_MII_STARTDELIM;
        frame->mii_opcode = AX_MII_READOP;
        frame->mii_turnaround = 0;
        frame->mii_data = 0;
        
        /*
         * Sync the PHYs.
         */
        ax_mii_sync(sc);

        /*
         * Send command/address info.
         */
        ax_mii_send(sc, frame->mii_stdelim, 2);
        ax_mii_send(sc, frame->mii_opcode, 2);
        ax_mii_send(sc, frame->mii_phyaddr, 5);
        ax_mii_send(sc, frame->mii_regaddr, 5);

#ifdef notdef
        /* Idle bit */
        ax_mii_writebit(sc, 1);
        ax_mii_writebit(sc, 0);
#endif

        /* Check for ack */
        ack = ax_mii_readbit(sc);

        /*
         * Now try reading data bits. If the ack failed, we still
         * need to clock through 16 cycles to keep the PHY(s) in sync.
         */
        if (ack) {
                for(i = 0; i < 16; i++) {
                        ax_mii_readbit(sc);
                }
                goto fail;
        }

        for (i = 0x8000; i; i >>= 1) {
                if (!ack) {
                        if (ax_mii_readbit(sc))
                                frame->mii_data |= i;
                }
        }

fail:

        ax_mii_writebit(sc, 0);
        ax_mii_writebit(sc, 0);

        splx(s);

        if (ack)
                return(1);
        return(0);
}

/*
 * Write to a PHY register through the MII.
 */
int ax_mii_writereg(sc, frame)
        struct ax_softc         *sc;
        struct ax_mii_frame     *frame;
        
{
        int                     s;

        s = splimp();
        /*
         * Set up frame for TX.
         */

        frame->mii_stdelim = AX_MII_STARTDELIM;
        frame->mii_opcode = AX_MII_WRITEOP;
        frame->mii_turnaround = AX_MII_TURNAROUND;

        /*
         * Sync the PHYs.
         */
        ax_mii_sync(sc);

        ax_mii_send(sc, frame->mii_stdelim, 2);
        ax_mii_send(sc, frame->mii_opcode, 2);
        ax_mii_send(sc, frame->mii_phyaddr, 5);
        ax_mii_send(sc, frame->mii_regaddr, 5);
        ax_mii_send(sc, frame->mii_turnaround, 2);
        ax_mii_send(sc, frame->mii_data, 16);

        /* Idle bit. */
        ax_mii_writebit(sc, 0);
        ax_mii_writebit(sc, 0);

        splx(s);

        return(0);
}

u_int16_t ax_phy_readreg(sc, reg)
        struct ax_softc         *sc;
        int                     reg;
{
        struct ax_mii_frame     frame;

        bzero((char *)&frame, sizeof(frame));

        frame.mii_phyaddr = sc->ax_phy_addr;
        frame.mii_regaddr = reg;
        ax_mii_readreg(sc, &frame);

        return(frame.mii_data);
}

void ax_phy_writereg(sc, reg, data)
        struct ax_softc         *sc;
        int                     reg;
        int                     data;
{
        struct ax_mii_frame     frame;

        bzero((char *)&frame, sizeof(frame));

        frame.mii_phyaddr = sc->ax_phy_addr;
        frame.mii_regaddr = reg;
        frame.mii_data = data;

        ax_mii_writereg(sc, &frame);

        return;
}

/*
 * Calculate CRC of a multicast group address, return the lower 6 bits.
 */
u_int32_t ax_calchash(addr)
        caddr_t                 addr;
{
        u_int32_t               crc, carry;
        int                     i, j;
        u_int8_t                c;

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

void ax_setmulti(sc)
        struct ax_softc         *sc;
{
        struct ifnet            *ifp;
        int                     h = 0;
        u_int32_t               hashes[2] = { 0, 0 };
	struct arpcom *ac = &sc->arpcom;
	struct ether_multi *enm;
	struct ether_multistep step;
        u_int32_t               rxfilt;

        ifp = &sc->arpcom.ac_if;

        rxfilt = CSR_READ_4(sc, AX_NETCFG);

        if (ifp->if_flags & IFF_ALLMULTI || ifp->if_flags & IFF_PROMISC) {
                rxfilt |= AX_NETCFG_RX_ALLMULTI;
                CSR_WRITE_4(sc, AX_NETCFG, rxfilt);
                return;
        } else
                rxfilt &= ~AX_NETCFG_RX_ALLMULTI;

        /* first, zot all the existing hash bits */
        CSR_WRITE_4(sc, AX_FILTIDX, AX_FILTIDX_MAR0);
        CSR_WRITE_4(sc, AX_FILTDATA, 0);
        CSR_WRITE_4(sc, AX_FILTIDX, AX_FILTIDX_MAR1);
        CSR_WRITE_4(sc, AX_FILTDATA, 0);

        /* now program new ones */
	ETHER_FIRST_MULTI(step, ac, enm);
	while (enm != NULL) {
		h = ax_calchash(enm->enm_addrlo);
		if (h < 32)
			hashes[0] |= (1 << h);
		else
			hashes[1] |= (1 << (h - 32));
		ETHER_NEXT_MULTI(step, enm);
	}

        CSR_WRITE_4(sc, AX_FILTIDX, AX_FILTIDX_MAR0);
        CSR_WRITE_4(sc, AX_FILTDATA, hashes[0]);
        CSR_WRITE_4(sc, AX_FILTIDX, AX_FILTIDX_MAR1);
        CSR_WRITE_4(sc, AX_FILTDATA, hashes[1]);
        CSR_WRITE_4(sc, AX_NETCFG, rxfilt);

        return;
}

/*
 * Initiate an autonegotiation session.
 */
void ax_autoneg_xmit(sc)
        struct ax_softc         *sc;
{
        u_int16_t               phy_sts;

        ax_phy_writereg(sc, PHY_BMCR, PHY_BMCR_RESET);
        DELAY(500);
        while(ax_phy_readreg(sc, PHY_BMCR)
                        & PHY_BMCR_RESET);

        phy_sts = ax_phy_readreg(sc, PHY_BMCR);
        phy_sts |= PHY_BMCR_AUTONEGENBL|PHY_BMCR_AUTONEGRSTR;
        ax_phy_writereg(sc, PHY_BMCR, phy_sts);

        return;
}

/*
 * Invoke autonegotiation on a PHY.
 */
void ax_autoneg_mii(sc, flag, verbose)
        struct ax_softc         *sc;
        int                     flag;
        int                     verbose;
{
        u_int16_t               phy_sts = 0, media, advert, ability;
        struct ifnet            *ifp;
        struct ifmedia          *ifm;

        ifm = &sc->ifmedia;
        ifp = &sc->arpcom.ac_if;

        ifm->ifm_media = IFM_ETHER | IFM_AUTO;

        /*
         * The 100baseT4 PHY on the 3c905-T4 has the 'autoneg supported'
         * bit cleared in the status register, but has the 'autoneg enabled'
         * bit set in the control register. This is a contradiction, and
         * I'm not sure how to handle it. If you want to force an attempt
         * to autoneg for 100baseT4 PHYs, #define FORCE_AUTONEG_TFOUR
         * and see what happens.
         */
#ifndef FORCE_AUTONEG_TFOUR
        /*
         * First, see if autoneg is supported. If not, there's
         * no point in continuing.
         */
        phy_sts = ax_phy_readreg(sc, PHY_BMSR);
        if (!(phy_sts & PHY_BMSR_CANAUTONEG)) {
                if (verbose)
                        printf("ax%d: autonegotiation not supported\n",
                                                        sc->ax_unit);
                ifm->ifm_media = IFM_ETHER|IFM_10_T|IFM_HDX;
                return;
        }
#endif

        switch (flag) {
        case AX_FLAG_FORCEDELAY:
                /*
                 * XXX Never use this option anywhere but in the probe
                 * routine: making the kernel stop dead in its tracks
                 * for three whole seconds after we've gone multi-user
                 * is really bad manners.
                 */
                ax_autoneg_xmit(sc);
                DELAY(5000000);
                break;
        case AX_FLAG_SCHEDDELAY:
                /*
                 * Wait for the transmitter to go idle before starting
                 * an autoneg session, otherwise ax_start() may clobber
                 * our timeout, and we don't want to allow transmission
                 * during an autoneg session since that can screw it up.
                 */
                if (sc->ax_cdata.ax_tx_head != NULL) {
                        sc->ax_want_auto = 1;
                        return;
                }
                ax_autoneg_xmit(sc);
                ifp->if_timer = 5;
                sc->ax_autoneg = 1;
                sc->ax_want_auto = 0;
                return;
                break;
        case AX_FLAG_DELAYTIMEO:
                ifp->if_timer = 0;
                sc->ax_autoneg = 0;
                break;
        default:
                printf("ax%d: invalid autoneg flag: %d\n", sc->ax_unit, flag);
                return;
        }

        if (ax_phy_readreg(sc, PHY_BMSR) & PHY_BMSR_AUTONEGCOMP) {
                if (verbose)
                        printf("ax%d: autoneg complete, ", sc->ax_unit);
                phy_sts = ax_phy_readreg(sc, PHY_BMSR);
        } else {
                if (verbose)
                        printf("ax%d: autoneg not complete, ", sc->ax_unit);
        }

        media = ax_phy_readreg(sc, PHY_BMCR);

        /* Link is good. Report modes and set duplex mode. */
        if (ax_phy_readreg(sc, PHY_BMSR) & PHY_BMSR_LINKSTAT) {
                if (verbose)
                        printf("link status good ");
                advert = ax_phy_readreg(sc, PHY_ANAR);
                ability = ax_phy_readreg(sc, PHY_LPAR);

                if (advert & PHY_ANAR_100BT4 && ability & PHY_ANAR_100BT4) {
                        ifm->ifm_media = IFM_ETHER|IFM_100_T4;
                        media |= PHY_BMCR_SPEEDSEL;
                        media &= ~PHY_BMCR_DUPLEX;
                        printf("(100baseT4)\n");
                } else if (advert & PHY_ANAR_100BTXFULL &&
                        ability & PHY_ANAR_100BTXFULL) {
                        ifm->ifm_media = IFM_ETHER|IFM_100_TX|IFM_FDX;
                        media |= PHY_BMCR_SPEEDSEL;
                        media |= PHY_BMCR_DUPLEX;
                        printf("(full-duplex, 100Mbps)\n");
                } else if (advert & PHY_ANAR_100BTXHALF &&
                        ability & PHY_ANAR_100BTXHALF) {
                        ifm->ifm_media = IFM_ETHER|IFM_100_TX|IFM_HDX;
                        media |= PHY_BMCR_SPEEDSEL;
                        media &= ~PHY_BMCR_DUPLEX;
                        printf("(half-duplex, 100Mbps)\n");
                } else if (advert & PHY_ANAR_10BTFULL &&
                        ability & PHY_ANAR_10BTFULL) {
                        ifm->ifm_media = IFM_ETHER|IFM_10_T|IFM_FDX;
                        media &= ~PHY_BMCR_SPEEDSEL;
                        media |= PHY_BMCR_DUPLEX;
                        printf("(full-duplex, 10Mbps)\n");
                } else if (advert & PHY_ANAR_10BTHALF &&
                        ability & PHY_ANAR_10BTHALF) {
                        ifm->ifm_media = IFM_ETHER|IFM_10_T|IFM_HDX;
                        media &= ~PHY_BMCR_SPEEDSEL;
                        media &= ~PHY_BMCR_DUPLEX;
                        printf("(half-duplex, 10Mbps)\n");
                }

                media &= ~PHY_BMCR_AUTONEGENBL;

                /* Set ASIC's duplex mode to match the PHY. */
                ax_setcfg(sc, media);
                ax_phy_writereg(sc, PHY_BMCR, media);
        } else {
                if (verbose)
                        printf("no carrier\n");
        }

        ax_init(sc);

        if (sc->ax_tx_pend) {
                sc->ax_autoneg = 0;
                sc->ax_tx_pend = 0;
                ax_start(ifp);
        }

        return;
}

void ax_getmode_mii(sc)
        struct ax_softc         *sc;
{
        u_int16_t               bmsr;
        struct ifnet            *ifp;

        ifp = &sc->arpcom.ac_if;

        bmsr = ax_phy_readreg(sc, PHY_BMSR);

        /* fallback */
        sc->ifmedia.ifm_media = IFM_ETHER|IFM_10_T|IFM_HDX;

        if (bmsr & PHY_BMSR_10BTHALF) {
                ifmedia_add(&sc->ifmedia,
                        IFM_ETHER|IFM_10_T|IFM_HDX, 0, NULL);
                ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_10_T, 0, NULL);
        }

        if (bmsr & PHY_BMSR_10BTFULL) {
                ifmedia_add(&sc->ifmedia,
                        IFM_ETHER|IFM_10_T|IFM_FDX, 0, NULL);
                sc->ifmedia.ifm_media = IFM_ETHER|IFM_10_T|IFM_FDX;
        }

        if (bmsr & PHY_BMSR_100BTXHALF) {
                ifp->if_baudrate = 100000000;
                ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_100_TX, 0, NULL);
                ifmedia_add(&sc->ifmedia,
                        IFM_ETHER|IFM_100_TX|IFM_HDX, 0, NULL);
                sc->ifmedia.ifm_media = IFM_ETHER|IFM_100_TX|IFM_HDX;
        }

        if (bmsr & PHY_BMSR_100BTXFULL) {
                ifp->if_baudrate = 100000000;
                ifmedia_add(&sc->ifmedia,
                        IFM_ETHER|IFM_100_TX|IFM_FDX, 0, NULL);
                sc->ifmedia.ifm_media = IFM_ETHER|IFM_100_TX|IFM_FDX;
        }

        /* Some also support 100BaseT4. */
        if (bmsr & PHY_BMSR_100BT4) {
                ifp->if_baudrate = 100000000;
                ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_100_T4, 0, NULL);
                sc->ifmedia.ifm_media = IFM_ETHER|IFM_100_T4;
#ifdef FORCE_AUTONEG_TFOUR
                ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_AUTO, 0 NULL):
                sc->ifmedia.ifm_media = IFM_ETHER|IFM_AUTO;
#endif
        }

        if (bmsr & PHY_BMSR_CANAUTONEG) {
                ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_AUTO, 0, NULL);
                sc->ifmedia.ifm_media = IFM_ETHER|IFM_AUTO;
        }

        return;
}

/*
 * Set speed and duplex mode.
 */
void ax_setmode_mii(sc, media)
        struct ax_softc         *sc;
        int                     media;
{
        u_int16_t               bmcr;
        struct ifnet            *ifp;

        ifp = &sc->arpcom.ac_if;

        /*
         * If an autoneg session is in progress, stop it.
         */
        if (sc->ax_autoneg) {
                printf("ax%d: canceling autoneg session\n", sc->ax_unit);
                ifp->if_timer = sc->ax_autoneg = sc->ax_want_auto = 0;
                bmcr = ax_phy_readreg(sc, PHY_BMCR);
                bmcr &= ~PHY_BMCR_AUTONEGENBL;
                ax_phy_writereg(sc, PHY_BMCR, bmcr);
        }

        printf("ax%d: selecting MII, ", sc->ax_unit);

        bmcr = ax_phy_readreg(sc, PHY_BMCR);

        bmcr &= ~(PHY_BMCR_AUTONEGENBL|PHY_BMCR_SPEEDSEL|
                        PHY_BMCR_DUPLEX|PHY_BMCR_LOOPBK);

        if (IFM_SUBTYPE(media) == IFM_100_T4) {
                printf("100Mbps/T4, half-duplex\n");
                bmcr |= PHY_BMCR_SPEEDSEL;
                bmcr &= ~PHY_BMCR_DUPLEX;
        }

        if (IFM_SUBTYPE(media) == IFM_100_TX) {
                printf("100Mbps, ");
                bmcr |= PHY_BMCR_SPEEDSEL;
        }

        if (IFM_SUBTYPE(media) == IFM_10_T) {
                printf("10Mbps, ");
                bmcr &= ~PHY_BMCR_SPEEDSEL;
        }

        if ((media & IFM_GMASK) == IFM_FDX) {
                printf("full duplex\n");
                bmcr |= PHY_BMCR_DUPLEX;
        } else {
                printf("half duplex\n");
                bmcr &= ~PHY_BMCR_DUPLEX;
        }

        ax_setcfg(sc, bmcr);
        ax_phy_writereg(sc, PHY_BMCR, bmcr);

        return;
}

/*
 * Set speed and duplex mode on internal transceiver.
 */
void ax_setmode(sc, media, verbose)
        struct ax_softc         *sc;
        int                     media;
        int                     verbose;
{
        struct ifnet            *ifp;
        u_int32_t               mode;

        ifp = &sc->arpcom.ac_if;

        if (verbose)
                printf("ax%d: selecting internal xcvr, ", sc->ax_unit);

        mode = CSR_READ_4(sc, AX_NETCFG);

        mode &= ~(AX_NETCFG_FULLDUPLEX|AX_NETCFG_PORTSEL|
                AX_NETCFG_PCS|AX_NETCFG_SCRAMBLER|AX_NETCFG_SPEEDSEL);

        if (IFM_SUBTYPE(media) == IFM_100_T4) {
                if (verbose)
                        printf("100Mbps/T4, half-duplex\n");
                mode |= AX_NETCFG_PORTSEL|AX_NETCFG_PCS|AX_NETCFG_SCRAMBLER;
        }

        if (IFM_SUBTYPE(media) == IFM_100_TX) {
                if (verbose)
                        printf("100Mbps, ");
                mode |= AX_NETCFG_PORTSEL|AX_NETCFG_PCS|AX_NETCFG_SCRAMBLER;
        }

        if (IFM_SUBTYPE(media) == IFM_10_T) {
                if (verbose)
                        printf("10Mbps, ");
                mode &= ~AX_NETCFG_PORTSEL;
                mode |= AX_NETCFG_SPEEDSEL;
        }

        if ((media & IFM_GMASK) == IFM_FDX) {
                if (verbose)
                        printf("full duplex\n");
                mode |= AX_NETCFG_FULLDUPLEX;
        } else {
                if (verbose)
                        printf("half duplex\n");
                mode &= ~AX_NETCFG_FULLDUPLEX;
        }

        CSR_WRITE_4(sc, AX_NETCFG, mode);

        return;
}

/*
 * In order to fiddle with the
 * 'full-duplex' and '100Mbps' bits in the netconfig register, we
 * first have to put the transmit and/or receive logic in the idle state.
 */
void ax_setcfg(sc, bmcr)
        struct ax_softc         *sc;
        int                     bmcr;
{
        int                     i, restart = 0;

        if (CSR_READ_4(sc, AX_NETCFG) & (AX_NETCFG_TX_ON|AX_NETCFG_RX_ON)) {
                restart = 1;
                AX_CLRBIT(sc, AX_NETCFG, (AX_NETCFG_TX_ON|AX_NETCFG_RX_ON));

                for (i = 0; i < AX_TIMEOUT; i++) {
                        DELAY(10);
                        if (CSR_READ_4(sc, AX_ISR) & AX_ISR_TX_IDLE)
                                break;
                }

                if (i == AX_TIMEOUT)
                        printf("ax%d: failed to force tx and "
                                "rx to idle state\n", sc->ax_unit);

        }

        if (bmcr & PHY_BMCR_SPEEDSEL)
                AX_CLRBIT(sc, AX_NETCFG, AX_NETCFG_SPEEDSEL);
        else
                AX_SETBIT(sc, AX_NETCFG, AX_NETCFG_SPEEDSEL);

        if (bmcr & PHY_BMCR_DUPLEX)
                AX_SETBIT(sc, AX_NETCFG, AX_NETCFG_FULLDUPLEX);
        else
                AX_CLRBIT(sc, AX_NETCFG, AX_NETCFG_FULLDUPLEX);

        if (restart)
                AX_SETBIT(sc, AX_NETCFG, AX_NETCFG_TX_ON|AX_NETCFG_RX_ON);

        return;
}

void ax_reset(sc)
        struct ax_softc         *sc;
{
        register int            i;

        AX_SETBIT(sc, AX_BUSCTL, AX_BUSCTL_RESET);

        for (i = 0; i < AX_TIMEOUT; i++) {
                DELAY(10);
                if (!(CSR_READ_4(sc, AX_BUSCTL) & AX_BUSCTL_RESET))
                        break;
        }
#ifdef notdef
        if (i == AX_TIMEOUT)
                printf("ax%d: reset never completed!\n", sc->ax_unit);
#endif
        CSR_WRITE_4(sc, AX_BUSCTL, AX_BUSCTL_CONFIG);

        /* Wait a little while for the chip to get its brains in order. */
        DELAY(1000);
        return;
}

/*
 * Attach the interface. Allocate softc structures, do ifmedia
 * setup and ethernet/BPF attach.
 */
void
ax_attach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
        int                     s, i;
#ifndef AX_USEIOSPACE
        vm_offset_t             pbase, vbase;
#endif
	const char *intrstr = NULL;
        u_int32_t               command;
        struct ax_softc         *sc = (struct ax_softc *)self;
	struct pci_attach_args	*pa = aux;
	pci_chipset_tag_t	pc = pa->pa_pc;
	pci_intr_handle_t	ih;
        struct ifnet            *ifp;
        int                     media = IFM_ETHER|IFM_100_TX|IFM_FDX;
	bus_addr_t iobase;
	bus_size_t iosize;
        unsigned int            round;
        caddr_t                 roundptr;
        struct ax_type          *p;
        u_int16_t               phy_vid, phy_did, phy_sts;

        s = splimp();
	sc->ax_unit = sc->sc_dev.dv_unit;

        /*
         * Handle power management nonsense.
         */

        command = pci_conf_read(pc, pa->pa_tag, AX_PCI_CAPID) & 0x000000FF;
        if (command == 0x01) {

                command = pci_conf_read(pc, pa->pa_tag, AX_PCI_PWRMGMTCTRL);
                if (command & AX_PSTATE_MASK) {
                        u_int32_t               iobase, membase, irq;

                        /* Save important PCI config data. */
                        iobase = pci_conf_read(pc, pa->pa_tag, AX_PCI_LOIO);
                        membase = pci_conf_read(pc, pa->pa_tag, AX_PCI_LOMEM);
                        irq = pci_conf_read(pc, pa->pa_tag, AX_PCI_INTLINE);

                        /* Reset the power state. */
                        printf("ax%d: chip is in D%d power mode "
                        "-- setting to D0\n", sc->ax_unit, command &
				AX_PSTATE_MASK);
                        command &= 0xFFFFFFFC;
                        pci_conf_write(pc, pa->pa_tag, AX_PCI_PWRMGMTCTRL, command);

                        /* Restore PCI config data. */
                        pci_conf_write(pc, pa->pa_tag, AX_PCI_LOIO, iobase);
                        pci_conf_write(pc, pa->pa_tag, AX_PCI_LOMEM, membase);
                        pci_conf_write(pc, pa->pa_tag, AX_PCI_INTLINE, irq);
                }
        }

        /*
         * Map control/status registers.
         */
        command = pci_conf_read(pc, pa->pa_tag, PCI_COMMAND_STATUS_REG);
	command |= PCI_COMMAND_IO_ENABLE |
		   PCI_COMMAND_MEM_ENABLE |
		   PCI_COMMAND_MASTER_ENABLE;
        pci_conf_write(pc, pa->pa_tag, PCI_COMMAND_STATUS_REG, command);
        command = pci_conf_read(pc, pa->pa_tag, PCI_COMMAND_STATUS_REG);

#ifdef AX_USEIOSPACE
	if (!(command & PCI_COMMAND_IO_ENABLE)) {
		printf("%s: failed to enable i/o ports\n",
		    sc->sc_dev.dv_xname);
		return;
	}
	/*
	 * Map control/status registers.
	 */
	if (pci_io_find(pc, pa->pa_tag, AX_PCI_LOIO, &iobase, &iosize)) {
		printf(": can't find i/o space\n");
		return;
	}
	if (bus_space_map(pa->pa_iot, iobase, iosize, 0, &sc->ax_bhandle)) {
		printf(": can't map i/o space\n");
		return;
	}
	sc->ax_btag = pa->pa_iot;
#else
	if (!(command & PCI_COMMAND_MEM_ENABLE)) {
		printf(": failed to enable memory mapping\n");
		return;
	}
	if (pci_mem_find(pc, pa->pa_tag, AX_PCI_LOMEM, &iobase, &iosize, NULL)){
		printf(": can't map mem space\n");
		return;
	}
	if (bus_space_map(pa->pa_memt, iobase, iosize, 0, &sc->ax_bhandle)) {
		printf(": can't map mem space\n");
		return;
	}
	sc->ax_btag = pa->pa_memt;
#endif

	/*
	 * Allocate our interrupt.
	 */
	if (pci_intr_map(pc, pa->pa_intrtag, pa->pa_intrpin,
	    pa->pa_intrline, &ih)) {
		printf(": couldn't map interrupt\n");
		return;
	}

	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_NET, ax_intr, sc,
	    self->dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt\n");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		return;
	}
	printf(": %s", intrstr);

	ax_reset(sc);

        /*
         * Get station address from the EEPROM.
         */
        ax_read_eeprom(sc, (caddr_t)&sc->arpcom.ac_enaddr, AX_EE_NODEADDR,3,0);
	printf(" address %s\n", ether_sprintf(sc->arpcom.ac_enaddr));

        sc->ax_ldata_ptr = malloc(sizeof(struct ax_list_data) + 8,
                                M_DEVBUF, M_NOWAIT);
        if (sc->ax_ldata_ptr == NULL) {
                free(sc, M_DEVBUF);
                printf("ax%d: no memory for list buffers!\n", sc->ax_unit);
                goto fail;
        }

        sc->ax_ldata = (struct ax_list_data *)sc->ax_ldata_ptr;
#if __alpha__
        round = (u_int64_t)sc->ax_ldata_ptr & 0xF;
#else
	round = (u_int32_t)sc->ax_ldata_ptr & 0xF;
#endif
        roundptr = sc->ax_ldata_ptr;
        for (i = 0; i < 8; i++) {
                if (round % 8) {
                        round++;
                        roundptr++;
                } else
                        break;
        }
        sc->ax_ldata = (struct ax_list_data *)roundptr;
        bzero(sc->ax_ldata, sizeof(struct ax_list_data));

        ifp = &sc->arpcom.ac_if;
        ifp->if_softc = sc;
        ifp->if_mtu = ETHERMTU;
        ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
        ifp->if_ioctl = ax_ioctl;
        ifp->if_output = ether_output;
        ifp->if_start = ax_start;
        ifp->if_watchdog = ax_watchdog;
        ifp->if_baudrate = 10000000;
        ifp->if_snd.ifq_maxlen = AX_TX_LIST_CNT - 1;
	bcopy(sc->sc_dev.dv_xname, ifp->if_xname, IFNAMSIZ);

        for (i = AX_PHYADDR_MIN; i < AX_PHYADDR_MAX + 1; i++) {
                sc->ax_phy_addr = i;
                ax_phy_writereg(sc, PHY_BMCR, PHY_BMCR_RESET);
                DELAY(500);
                while(ax_phy_readreg(sc, PHY_BMCR)
                                & PHY_BMCR_RESET);
                if ((phy_sts = ax_phy_readreg(sc, PHY_BMSR)))
                        break;
        }
        if (phy_sts) {
                phy_vid = ax_phy_readreg(sc, PHY_VENID);
                phy_did = ax_phy_readreg(sc, PHY_DEVID);
                p = ax_phys;
                while(p->ax_vid) {
                        if (phy_vid == p->ax_vid &&
                                (phy_did | 0x000F) == p->ax_did) {
                                sc->ax_pinfo = p;
                                break;
                        }
                        p++;
                }
                if (sc->ax_pinfo == NULL)
                        sc->ax_pinfo = &ax_phys[PHY_UNKNOWN];
        } else {
#ifdef DIAGNOSTIC
                printf("ax%d: MII without any phy!\n", sc->ax_unit);
#endif
        }

        /*
         * Do ifmedia setup.
         */
        ifmedia_init(&sc->ifmedia, 0, ax_ifmedia_upd, ax_ifmedia_sts);

        if (sc->ax_pinfo != NULL) {
                ax_getmode_mii(sc);
                ax_autoneg_mii(sc, AX_FLAG_FORCEDELAY, 1);
        } else {
                ifmedia_add(&sc->ifmedia,
                        IFM_ETHER|IFM_10_T|IFM_HDX, 0, NULL);
                ifmedia_add(&sc->ifmedia,
                        IFM_ETHER|IFM_10_T|IFM_FDX, 0, NULL);
                ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_10_T, 0, NULL);
                ifmedia_add(&sc->ifmedia,
                        IFM_ETHER|IFM_100_TX|IFM_HDX, 0, NULL);
                ifmedia_add(&sc->ifmedia,
                        IFM_ETHER|IFM_100_TX|IFM_FDX, 0, NULL);
                ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_100_TX, 0, NULL);
                ifmedia_add(&sc->ifmedia, IFM_ETHER|IFM_AUTO, 0, NULL);
        }

        media = sc->ifmedia.ifm_media;
        ax_stop(sc);

        ifmedia_set(&sc->ifmedia, media);

        /*
         * Call MI attach routines.
         */
        if_attach(ifp);
        ether_ifattach(ifp);

#if NBPFILTER > 0
        bpfattach(&sc->arpcom.ac_if.if_bpf, ifp, DLT_EN10MB,
	          sizeof(struct ether_header));
#endif
        shutdownhook_establish(ax_shutdown, sc);

fail:
        splx(s);
        return;
}

/*
 * Initialize the transmit descriptors.
 */
int ax_list_tx_init(sc)
        struct ax_softc         *sc;
{
        struct ax_chain_data    *cd;
        struct ax_list_data     *ld;
        int                     i;

        cd = &sc->ax_cdata;
        ld = sc->ax_ldata;
        for (i = 0; i < AX_TX_LIST_CNT; i++) {
                cd->ax_tx_chain[i].ax_ptr = &ld->ax_tx_list[i];
                if (i == (AX_TX_LIST_CNT - 1))
                        cd->ax_tx_chain[i].ax_nextdesc =
                                &cd->ax_tx_chain[0];
                else
                        cd->ax_tx_chain[i].ax_nextdesc =
                                &cd->ax_tx_chain[i + 1];
        }

        cd->ax_tx_free = &cd->ax_tx_chain[0];
        cd->ax_tx_tail = cd->ax_tx_head = NULL;

        return(0);
}


/*
 * Initialize the RX descriptors and allocate mbufs for them. Note that
 * we arrange the descriptors in a closed ring, so that the last descriptor
 * points back to the first.
 */
int ax_list_rx_init(sc)
        struct ax_softc         *sc;
{
        struct ax_chain_data    *cd;
        struct ax_list_data     *ld;
        int                     i;

        cd = &sc->ax_cdata;
        ld = sc->ax_ldata;

        for (i = 0; i < AX_RX_LIST_CNT; i++) {
                cd->ax_rx_chain[i].ax_ptr =
                        (volatile struct ax_desc *)&ld->ax_rx_list[i];
                if (ax_newbuf(sc, &cd->ax_rx_chain[i]) == ENOBUFS)
                        return(ENOBUFS);
                if (i == (AX_RX_LIST_CNT - 1)) {
                        cd->ax_rx_chain[i].ax_nextdesc =
                                                &cd->ax_rx_chain[0];
                        ld->ax_rx_list[i].ax_next =
                                        vtophys(&ld->ax_rx_list[0]);
                } else {
                        cd->ax_rx_chain[i].ax_nextdesc =
                                                &cd->ax_rx_chain[i + 1];
                        ld->ax_rx_list[i].ax_next =
                                        vtophys(&ld->ax_rx_list[i + 1]);
                }
        }

        cd->ax_rx_head = &cd->ax_rx_chain[0];

        return(0);
}

/*
 * Initialize an RX descriptor and attach an MBUF cluster.
 * Note: the length fields are only 11 bits wide, which means the
 * largest size we can specify is 2047. This is important because
 * MCLBYTES is 2048, so we have to subtract one otherwise we'll
 * overflow the field and make a mess.
 */
int ax_newbuf(sc, c)
        struct ax_softc         *sc;
        struct ax_chain_onefrag *c;
{
        struct mbuf             *m_new = NULL;

        MGETHDR(m_new, M_DONTWAIT, MT_DATA);
        if (m_new == NULL)
                return(ENOBUFS);

        MCLGET(m_new, M_DONTWAIT);
        if (!(m_new->m_flags & M_EXT)) {
                m_freem(m_new);
                return(ENOBUFS);
        }

        c->ax_mbuf = m_new;
        c->ax_ptr->ax_status = AX_RXSTAT;
        c->ax_ptr->ax_data = vtophys(mtod(m_new, caddr_t));
        c->ax_ptr->ax_ctl = MCLBYTES - 1;

        return(0);
}

/*
 * A frame has been uploaded: pass the resulting mbuf chain up to
 * the higher level protocols.
 */
void ax_rxeof(sc)
        struct ax_softc         *sc;
{
        struct ether_header     *eh;
        struct mbuf             *m;
        struct ifnet            *ifp;
        struct ax_chain_onefrag *cur_rx;
        int                     total_len = 0;
        u_int32_t               rxstat;

        ifp = &sc->arpcom.ac_if;

        while(!((rxstat = sc->ax_cdata.ax_rx_head->ax_ptr->ax_status) &
                                                        AX_RXSTAT_OWN)) {
#ifdef __alpha__
                struct mbuf             *m0 = NULL;
#endif
                cur_rx = sc->ax_cdata.ax_rx_head;
                sc->ax_cdata.ax_rx_head = cur_rx->ax_nextdesc;

                /*
                 * If an error occurs, update stats, clear the
                 * status word and leave the mbuf cluster in place:
                 * it should simply get re-used next time this descriptor
                 * comes up in the ring.
                 */
                if (rxstat & AX_RXSTAT_RXERR) {
                        ifp->if_ierrors++;
                        if (rxstat & AX_RXSTAT_COLLSEEN)
                                ifp->if_collisions++;
                        cur_rx->ax_ptr->ax_status = AX_RXSTAT;
                        cur_rx->ax_ptr->ax_ctl = (MCLBYTES - 1);
                        continue;
                }

                /* No errors; receive the packet. */
                m = cur_rx->ax_mbuf;
                total_len = AX_RXBYTES(cur_rx->ax_ptr->ax_status);

                total_len -= ETHER_CRC_LEN;

#ifdef __alpha__
                /*
                 * Try to conjure up a new mbuf cluster. If that
                 * fails, it means we have an out of memory condition and
                 * should leave the buffer in place and continue. This will
                 * result in a lost packet, but there's little else we
                 * can do in this situation.
                 */
                if (ax_newbuf(sc, cur_rx) == ENOBUFS) {
                        ifp->if_ierrors++;
                        cur_rx->ax_ptr->ax_status = AX_RXSTAT;
                        cur_rx->ax_ptr->ax_ctl = (MCLBYTES - 1);
                        continue;
                }

                /*
                 * Sadly, the ASIX chip doesn't decode the last few
                 * bits of the RX DMA buffer address, so we have to
                 * cheat in order to obtain proper payload alignment
                 * on the alpha.
                 */
                MGETHDR(m0, M_DONTWAIT, MT_DATA);
                if (m0 == NULL) {
                        ifp->if_ierrors++;
                        cur_rx->ax_ptr->ax_status = AX_RXSTAT;
                        cur_rx->ax_ptr->ax_ctl = (MCLBYTES - 1);
                        continue;
                }

                m0->m_data += 2;
                if (total_len <= (MHLEN - 2)) {
                        bcopy(mtod(m, caddr_t), mtod(m0, caddr_t), total_len);
                        m_freem(m);
                        m = m0;
                        m->m_pkthdr.len = m->m_len = total_len;
                } else {
                        bcopy(mtod(m, caddr_t), mtod(m0, caddr_t), (MHLEN - 2))
;
                        m->m_len = total_len - (MHLEN - 2);
                        m->m_data += (MHLEN - 2);
                        m0->m_next = m;
                        m0->m_len = (MHLEN - 2);
                        m = m0;
                        m->m_pkthdr.len = total_len;
                }
                m->m_pkthdr.rcvif = ifp;
#else
                if (total_len < MINCLSIZE) {
                        m = m_devget(mtod(cur_rx->ax_mbuf, char *),
                                total_len, 0, ifp, NULL);
                        cur_rx->ax_ptr->ax_status = AX_RXSTAT;
                        cur_rx->ax_ptr->ax_ctl = (MCLBYTES - 1);
                        if (m == NULL) {
                                ifp->if_ierrors++;
                                continue;
                        }
                } else {
                        m = cur_rx->ax_mbuf;
                /*
                 * Try to conjure up a new mbuf cluster. If that
                 * fails, it means we have an out of memory condition and
                 * should leave the buffer in place and continue. This will
                 * result in a lost packet, but there's little else we
                 * can do in this situation.
                 */
                        if (ax_newbuf(sc, cur_rx) == ENOBUFS) {
                                ifp->if_ierrors++;
                                cur_rx->ax_ptr->ax_status = AX_RXSTAT;
                                cur_rx->ax_ptr->ax_ctl = (MCLBYTES - 1);
                                continue;
                        }
                        m->m_pkthdr.rcvif = ifp;
                        m->m_pkthdr.len = m->m_len = total_len;
                }
#endif

                ifp->if_ipackets++;
                eh = mtod(m, struct ether_header *);
#if NBPFILTER > 0
                /*
                 * Handle BPF listeners. Let the BPF user see the packet, but
                 * don't pass it up to the ether_input() layer unless it's
                 * a broadcast packet, multicast packet, matches our ethernet
                 * address or the interface is in promiscuous mode.
                 */
                if (ifp->if_bpf) {
			m->m_pkthdr.len = m->m_len = total_len;
			bpf_mtap(ifp->if_bpf, m);
                }
#endif
                /* Remove header from mbuf and pass it on. */
		m->m_pkthdr.len = m->m_len =
				total_len - sizeof(struct ether_header);
		m->m_data += sizeof(struct ether_header);
                ether_input(ifp, eh, m);
        }

        return;
}

void ax_rxeoc(sc)
        struct ax_softc         *sc;
{

        ax_rxeof(sc);
        AX_CLRBIT(sc, AX_NETCFG, AX_NETCFG_RX_ON);
        CSR_WRITE_4(sc, AX_RXADDR, vtophys(sc->ax_cdata.ax_rx_head->ax_ptr));
        AX_SETBIT(sc, AX_NETCFG, AX_NETCFG_RX_ON);
        CSR_WRITE_4(sc, AX_RXSTART, 0xFFFFFFFF);

        return;
}

/*
 * A frame was downloaded to the chip. It's safe for us to clean up
 * the list buffers.
 */

void ax_txeof(sc)
        struct ax_softc         *sc;
{
        struct ax_chain         *cur_tx;
        struct ifnet            *ifp;

        ifp = &sc->arpcom.ac_if;

        /* Clear the timeout timer. */
        ifp->if_timer = 0;

        if (sc->ax_cdata.ax_tx_head == NULL)
                return;

        /*
         * Go through our tx list and free mbufs for those
         * frames that have been transmitted.
         */
        while(sc->ax_cdata.ax_tx_head->ax_mbuf != NULL) {
                u_int32_t               txstat;

                cur_tx = sc->ax_cdata.ax_tx_head;
                txstat = AX_TXSTATUS(cur_tx);

                if (txstat & AX_TXSTAT_OWN)
                        break;

                if (txstat & AX_TXSTAT_ERRSUM) {
                        ifp->if_oerrors++;
                        if (txstat & AX_TXSTAT_EXCESSCOLL)
                                ifp->if_collisions++;
                        if (txstat & AX_TXSTAT_LATECOLL)
                                ifp->if_collisions++;
                }

                ifp->if_collisions += (txstat & AX_TXSTAT_COLLCNT) >> 3;

                ifp->if_opackets++;
                m_freem(cur_tx->ax_mbuf);
                cur_tx->ax_mbuf = NULL;

                if (sc->ax_cdata.ax_tx_head == sc->ax_cdata.ax_tx_tail) {
                        sc->ax_cdata.ax_tx_head = NULL;
                        sc->ax_cdata.ax_tx_tail = NULL;
                        break;
                }

                sc->ax_cdata.ax_tx_head = cur_tx->ax_nextdesc;
        }

        return;
}

/*
 * TX 'end of channel' interrupt handler.
 */
void ax_txeoc(sc)
        struct ax_softc         *sc;
{
        struct ifnet            *ifp;

        ifp = &sc->arpcom.ac_if;

        ifp->if_timer = 0;

        if (sc->ax_cdata.ax_tx_head == NULL) {
                ifp->if_flags &= ~IFF_OACTIVE;
                sc->ax_cdata.ax_tx_tail = NULL;
                if (sc->ax_want_auto)
                        ax_autoneg_mii(sc, AX_FLAG_DELAYTIMEO, 1);
        }

        return;
}

int ax_intr(arg)
	void			*arg;
{
        struct ax_softc         *sc;
        struct ifnet            *ifp;
        u_int32_t               status;
	int claimed = 0;

        sc = arg;
        ifp = &sc->arpcom.ac_if;

        /* Supress unwanted interrupts */
        if (!(ifp->if_flags & IFF_UP)) {
                ax_stop(sc);
                return (claimed);
        }

        /* Disable interrupts. */
        CSR_WRITE_4(sc, AX_IMR, 0x00000000);

        for (;;) {
                status = CSR_READ_4(sc, AX_ISR);
                if (status)
                        CSR_WRITE_4(sc, AX_ISR, status);

                if ((status & AX_INTRS) == 0)
                        break;

		claimed = 1;

                if ((status & AX_ISR_TX_OK) || (status & AX_ISR_TX_EARLY))
                        ax_txeof(sc);

                if (status & AX_ISR_TX_NOBUF)
                        ax_txeoc(sc);

                if (status & AX_ISR_TX_IDLE) {
                        ax_txeof(sc);
                        if (sc->ax_cdata.ax_tx_head != NULL) {
                                AX_SETBIT(sc, AX_NETCFG, AX_NETCFG_TX_ON);
                                CSR_WRITE_4(sc, AX_TXSTART, 0xFFFFFFFF);
                        }
                }

                if (status & AX_ISR_TX_UNDERRUN) {
                        u_int32_t               cfg;
                        cfg = CSR_READ_4(sc, AX_NETCFG);
                        if ((cfg & AX_NETCFG_TX_THRESH) == AX_TXTHRESH_160BYTES
)
                                AX_SETBIT(sc, AX_NETCFG, AX_NETCFG_STORENFWD);
                        else
                                CSR_WRITE_4(sc, AX_NETCFG, cfg + 0x4000);
                }

                if (status & AX_ISR_RX_OK)
                        ax_rxeof(sc);

                if ((status & AX_ISR_RX_WATDOGTIMEO)
                                        || (status & AX_ISR_RX_NOBUF))
                        ax_rxeoc(sc);

                if (status & AX_ISR_BUS_ERR) {
                        ax_reset(sc);
                        ax_init(sc);
                }
        }

        /* Re-enable interrupts. */
        CSR_WRITE_4(sc, AX_IMR, AX_INTRS);

        if (ifp->if_snd.ifq_head != NULL) {
                ax_start(ifp);
        }

	return claimed;
}

/*
 * Encapsulate an mbuf chain in a descriptor by coupling the mbuf data
 * pointers to the fragment pointers.
 */
int ax_encap(sc, c, m_head)
        struct ax_softc         *sc;
        struct ax_chain         *c;
        struct mbuf             *m_head;
{
        int                     frag = 0;
        volatile struct ax_desc *f = NULL;
        int                     total_len;
        struct mbuf             *m;

        /*
         * Start packing the mbufs in this chain into
         * the fragment pointers. Stop when we run out
         * of fragments or hit the end of the mbuf chain.
         */
        m = m_head;
        total_len = 0;

        for (m = m_head, frag = 0; m != NULL; m = m->m_next) {
                if (m->m_len != 0) {
                        if (frag == AX_MAXFRAGS)
                                break;
                        total_len += m->m_len;
                        f = &c->ax_ptr->ax_frag[frag];
                        f->ax_ctl = m->m_len;
                        if (frag == 0) {
                                f->ax_status = 0;
                                f->ax_ctl |= AX_TXCTL_FIRSTFRAG;
                        } else
                                f->ax_status = AX_TXSTAT_OWN;
                        f->ax_next = vtophys(&c->ax_ptr->ax_frag[frag + 1]);
                        f->ax_data = vtophys(mtod(m, vm_offset_t));
                        frag++;
                }
        }

        /*
         * Handle special case: we ran out of fragments,
         * but we have more mbufs left in the chain. Copy the
         * data into an mbuf cluster. Note that we don't
         * bother clearing the values in the other fragment
         * pointers/counters; it wouldn't gain us anything,
         * and would waste cycles.
         */
        if (m != NULL) {
                struct mbuf             *m_new = NULL;

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
                f = &c->ax_ptr->ax_frag[0];
                f->ax_status = 0;
                f->ax_data = vtophys(mtod(m_new, caddr_t));
                f->ax_ctl = total_len = m_new->m_len;
                f->ax_ctl |= AX_TXCTL_FIRSTFRAG;
                frag = 1;
        }

        c->ax_mbuf = m_head;
        c->ax_lastdesc = frag - 1;
        AX_TXCTL(c) |= AX_TXCTL_LASTFRAG|AX_TXCTL_FINT;
        c->ax_ptr->ax_frag[0].ax_ctl |= AX_TXCTL_FINT;
        AX_TXNEXT(c) = vtophys(&c->ax_nextdesc->ax_ptr->ax_frag[0]);
        return(0);
}

/*
 * Main transmit routine. To avoid having to do mbuf copies, we put pointers
 * to the mbuf data regions directly in the transmit lists. We also save a
 * copy of the pointers since the transmit list fragment pointers are
 * physical addresses.
 */

void ax_start(ifp)
        struct ifnet            *ifp;
{
        struct ax_softc         *sc;
        struct mbuf             *m_head = NULL;
        struct ax_chain         *cur_tx = NULL, *start_tx;

        sc = ifp->if_softc;

        if (sc->ax_autoneg) {
                sc->ax_tx_pend = 1;
                return;
        }

        /*
         * Check for an available queue slot. If there are none,
         * punt.
         */
        if (sc->ax_cdata.ax_tx_free->ax_mbuf != NULL) {
                ifp->if_flags |= IFF_OACTIVE;
                return;
        }

        start_tx = sc->ax_cdata.ax_tx_free;

        while(sc->ax_cdata.ax_tx_free->ax_mbuf == NULL) {
                IF_DEQUEUE(&ifp->if_snd, m_head);
                if (m_head == NULL)
                        break;

                /* Pick a descriptor off the free list. */
                cur_tx = sc->ax_cdata.ax_tx_free;
                sc->ax_cdata.ax_tx_free = cur_tx->ax_nextdesc;

                /* Pack the data into the descriptor. */
                ax_encap(sc, cur_tx, m_head);
                if (cur_tx != start_tx)
                        AX_TXOWN(cur_tx) = AX_TXSTAT_OWN;

#if NBPFILTER > 0
                /*
                 * If there's a BPF listener, bounce a copy of this frame
                 * to him.
                 */
                if (ifp->if_bpf)
			bpf_mtap(ifp->if_bpf, cur_tx->ax_mbuf);
#endif
                AX_TXOWN(cur_tx) = AX_TXSTAT_OWN;
                CSR_WRITE_4(sc, AX_TXSTART, 0xFFFFFFFF);
        }

        sc->ax_cdata.ax_tx_tail = cur_tx;
        if (sc->ax_cdata.ax_tx_head == NULL)
                sc->ax_cdata.ax_tx_head = start_tx;

        /*
         * Set a timeout in case the chip goes out to lunch.
         */
        ifp->if_timer = 5;

        return;
}

void ax_init(xsc)
        void                    *xsc;
{
        struct ax_softc         *sc = xsc;
        struct ifnet            *ifp = &sc->arpcom.ac_if;
        u_int16_t               phy_bmcr = 0;
        int                     s;

        if (sc->ax_autoneg)
                return;

        s = splimp();

        if (sc->ax_pinfo != NULL)
                phy_bmcr = ax_phy_readreg(sc, PHY_BMCR);

        /*
         * Cancel pending I/O and free all RX/TX buffers.
         */
        ax_stop(sc);
        ax_reset(sc);

        /*
         * Set cache alignment and burst length.
         */
        CSR_WRITE_4(sc, AX_BUSCTL, AX_BUSCTL_CONFIG);

        AX_CLRBIT(sc, AX_NETCFG, AX_NETCFG_HEARTBEAT);
        AX_CLRBIT(sc, AX_NETCFG, AX_NETCFG_STORENFWD);

        if (sc->ax_pinfo != NULL) {
                AX_SETBIT(sc, AX_NETCFG, AX_NETCFG_PORTSEL);
                ax_setcfg(sc, ax_phy_readreg(sc, PHY_BMCR));
        } else
                ax_setmode(sc, sc->ifmedia.ifm_media, 0);

        AX_CLRBIT(sc, AX_NETCFG, AX_NETCFG_TX_THRESH);
        AX_CLRBIT(sc, AX_NETCFG, AX_NETCFG_SPEEDSEL);

        if (IFM_SUBTYPE(sc->ifmedia.ifm_media) == IFM_10_T)
                AX_SETBIT(sc, AX_NETCFG, AX_TXTHRESH_160BYTES);
        else
                AX_SETBIT(sc, AX_NETCFG, AX_TXTHRESH_72BYTES);

        /* Init our MAC address */
        CSR_WRITE_4(sc, AX_FILTIDX, AX_FILTIDX_PAR0);
        CSR_WRITE_4(sc, AX_FILTDATA, *(u_int32_t *)(&sc->arpcom.ac_enaddr[0]));
        CSR_WRITE_4(sc, AX_FILTIDX, AX_FILTIDX_PAR1);
        CSR_WRITE_4(sc, AX_FILTDATA, *(u_int32_t *)(&sc->arpcom.ac_enaddr[4]));

        /* Init circular RX list. */
        if (ax_list_rx_init(sc) == ENOBUFS) {
                printf("ax%d: initialization failed: no "
                        "memory for rx buffers\n", sc->ax_unit);
                ax_stop(sc);
                (void)splx(s);
                return;
        }

        /*
         * Init tx descriptors.
         */
        ax_list_tx_init(sc);

         /* If we want promiscuous mode, set the allframes bit. */
        if (ifp->if_flags & IFF_PROMISC) {
                AX_SETBIT(sc, AX_NETCFG, AX_NETCFG_RX_PROMISC);
        } else {
                AX_CLRBIT(sc, AX_NETCFG, AX_NETCFG_RX_PROMISC);
        }

        /*
         * Set the capture broadcast bit to capture broadcast frames.
         */
        if (ifp->if_flags & IFF_BROADCAST) {
                AX_SETBIT(sc, AX_NETCFG, AX_NETCFG_RX_BROAD);
        } else {
                AX_CLRBIT(sc, AX_NETCFG, AX_NETCFG_RX_BROAD);
        }

        /*
         * Load the multicast filter.
         */
        ax_setmulti(sc);

        /*
         * Load the address of the RX list.
         */
        CSR_WRITE_4(sc, AX_RXADDR, vtophys(sc->ax_cdata.ax_rx_head->ax_ptr));
        CSR_WRITE_4(sc, AX_TXADDR, vtophys(&sc->ax_ldata->ax_tx_list[0]));

        /*
         * Enable interrupts.
         */
        CSR_WRITE_4(sc, AX_IMR, AX_INTRS);
        CSR_WRITE_4(sc, AX_ISR, 0xFFFFFFFF);

        /* Enable receiver and transmitter. */
        AX_SETBIT(sc, AX_NETCFG, AX_NETCFG_TX_ON|AX_NETCFG_RX_ON);
        CSR_WRITE_4(sc, AX_RXSTART, 0xFFFFFFFF);

        /* Restore state of BMCR */
        if (sc->ax_pinfo != NULL)
                ax_phy_writereg(sc, PHY_BMCR, phy_bmcr);

        ifp->if_flags |= IFF_RUNNING;
        ifp->if_flags &= ~IFF_OACTIVE;

        (void)splx(s);

        return;
}

/*
 * Set media options.
 */
int ax_ifmedia_upd(ifp)
        struct ifnet            *ifp;
{
        struct ax_softc         *sc;
        struct ifmedia          *ifm;

        sc = ifp->if_softc;
        ifm = &sc->ifmedia;

        if (IFM_TYPE(ifm->ifm_media) != IFM_ETHER)
                return(EINVAL);

        if (IFM_SUBTYPE(ifm->ifm_media) == IFM_AUTO)
                ax_autoneg_mii(sc, AX_FLAG_SCHEDDELAY, 1);
        else {
                if (sc->ax_pinfo == NULL)
                        ax_setmode(sc, ifm->ifm_media, 1);
                else
                        ax_setmode_mii(sc, ifm->ifm_media);
        }

        return(0);
}

/*
 * Report current media status.
 */
void ax_ifmedia_sts(ifp, ifmr)
        struct ifnet            *ifp;
        struct ifmediareq       *ifmr;
{
        struct ax_softc         *sc;
        u_int16_t               advert = 0, ability = 0;
        u_int32_t               media = 0;

        sc = ifp->if_softc;

        ifmr->ifm_active = IFM_ETHER;

        if (sc->ax_pinfo == NULL) {
                media = CSR_READ_4(sc, AX_NETCFG);
                if (media & AX_NETCFG_PORTSEL)
                        ifmr->ifm_active = IFM_ETHER|IFM_100_TX;
                else
                        ifmr->ifm_active = IFM_ETHER|IFM_10_T;
                if (media & AX_NETCFG_FULLDUPLEX)
                        ifmr->ifm_active |= IFM_FDX;
                else
                        ifmr->ifm_active |= IFM_HDX;
                return;
        }

        if (!(ax_phy_readreg(sc, PHY_BMCR) & PHY_BMCR_AUTONEGENBL)) {
                if (ax_phy_readreg(sc, PHY_BMCR) & PHY_BMCR_SPEEDSEL)
                        ifmr->ifm_active = IFM_ETHER|IFM_100_TX;
                else
                        ifmr->ifm_active = IFM_ETHER|IFM_10_T;
                if (ax_phy_readreg(sc, PHY_BMCR) & PHY_BMCR_DUPLEX)
                        ifmr->ifm_active |= IFM_FDX;
                else
                        ifmr->ifm_active |= IFM_HDX;
                return;
        }

        ability = ax_phy_readreg(sc, PHY_LPAR);
        advert = ax_phy_readreg(sc, PHY_ANAR);
        if (advert & PHY_ANAR_100BT4 &&
                ability & PHY_ANAR_100BT4) {
                ifmr->ifm_active = IFM_ETHER|IFM_100_T4;
        } else if (advert & PHY_ANAR_100BTXFULL &&
                ability & PHY_ANAR_100BTXFULL) {
                ifmr->ifm_active = IFM_ETHER|IFM_100_TX|IFM_FDX;
        } else if (advert & PHY_ANAR_100BTXHALF &&
                ability & PHY_ANAR_100BTXHALF) {
                ifmr->ifm_active = IFM_ETHER|IFM_100_TX|IFM_HDX;
        } else if (advert & PHY_ANAR_10BTFULL &&
                ability & PHY_ANAR_10BTFULL) {
                ifmr->ifm_active = IFM_ETHER|IFM_10_T|IFM_FDX;
        } else if (advert & PHY_ANAR_10BTHALF &&
                ability & PHY_ANAR_10BTHALF) {
                ifmr->ifm_active = IFM_ETHER|IFM_10_T|IFM_HDX;
        }

        return;
}

int ax_ioctl(ifp, command, data)
        struct ifnet            *ifp;
        u_long                  command;
        caddr_t                 data;
{
        struct ax_softc         *sc = ifp->if_softc;
        struct ifreq            *ifr = (struct ifreq *) data;
	struct ifaddr *ifa = (struct ifaddr *)data;
        int                     s, error = 0;

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
			ax_init(sc);
			arp_ifinit(&sc->arpcom, ifa);
			break;
#endif /* INET */
		default:
			ax_init(sc);
			break;
		}
		break;
        case SIOCSIFFLAGS:
                if (ifp->if_flags & IFF_UP) {
                        ax_init(sc);
                } else {
                        if (ifp->if_flags & IFF_RUNNING)
                                ax_stop(sc);
                }
                error = 0;
                break;
        case SIOCADDMULTI:
        case SIOCDELMULTI:
                ax_setmulti(sc);
                error = 0;
                break;
        case SIOCGIFMEDIA:
        case SIOCSIFMEDIA:
                error = ifmedia_ioctl(ifp, ifr, &sc->ifmedia, command);
                break;
        default:
                error = EINVAL;
                break;
        }

        (void)splx(s);

        return(error);
}

void ax_watchdog(ifp)
        struct ifnet            *ifp;
{
        struct ax_softc         *sc;

        sc = ifp->if_softc;

        if (sc->ax_autoneg) {
                ax_autoneg_mii(sc, AX_FLAG_DELAYTIMEO, 1);
                return;
        }

        ifp->if_oerrors++;
        printf("ax%d: watchdog timeout\n", sc->ax_unit);

        if (sc->ax_pinfo != NULL) {
                if (!(ax_phy_readreg(sc, PHY_BMSR) & PHY_BMSR_LINKSTAT))
                        printf("ax%d: no carrier - transceiver "
                                "cable problem?\n", sc->ax_unit);
        }

        ax_stop(sc);
        ax_reset(sc);
        ax_init(sc);

        if (ifp->if_snd.ifq_head != NULL)
                ax_start(ifp);

        return;
}

/*
 * Stop the adapter and free any mbufs allocated to the
 * RX and TX lists.
 */
void ax_stop(sc)
        struct ax_softc         *sc;
{
        register int            i;
        struct ifnet            *ifp;

        ifp = &sc->arpcom.ac_if;
        ifp->if_timer = 0;

        AX_CLRBIT(sc, AX_NETCFG, (AX_NETCFG_RX_ON|AX_NETCFG_TX_ON));
        CSR_WRITE_4(sc, AX_IMR, 0x00000000);
        CSR_WRITE_4(sc, AX_TXADDR, 0x00000000);
        CSR_WRITE_4(sc, AX_RXADDR, 0x00000000);

        /*
         * Free data in the RX lists.
         */
        for (i = 0; i < AX_RX_LIST_CNT; i++) {
                if (sc->ax_cdata.ax_rx_chain[i].ax_mbuf != NULL) {
                        m_freem(sc->ax_cdata.ax_rx_chain[i].ax_mbuf);
                        sc->ax_cdata.ax_rx_chain[i].ax_mbuf = NULL;
                }
        }
        bzero((char *)&sc->ax_ldata->ax_rx_list,
                sizeof(sc->ax_ldata->ax_rx_list));

        /*
         * Free the TX list buffers.
         */
        for (i = 0; i < AX_TX_LIST_CNT; i++) {
                if (sc->ax_cdata.ax_tx_chain[i].ax_mbuf != NULL) {
                        m_freem(sc->ax_cdata.ax_tx_chain[i].ax_mbuf);
                        sc->ax_cdata.ax_tx_chain[i].ax_mbuf = NULL;
                }
        }

        bzero((char *)&sc->ax_ldata->ax_tx_list,
                sizeof(sc->ax_ldata->ax_tx_list));

        ifp->if_flags &= ~(IFF_RUNNING | IFF_OACTIVE);

        return;
}

int
ax_probe(parent, match, aux)
	struct device *parent;
	void *match;
	void *aux;
{
	struct pci_attach_args *pa = (struct pci_attach_args *) aux;

	if (PCI_VENDOR(pa->pa_id) != PCI_VENDOR_ASIX)
		return (0);

	switch (PCI_PRODUCT(pa->pa_id)) {
		case PCI_PRODUCT_ASIX_AX88140A:
			return (1);
	}

	return (0);
}

void
ax_shutdown(v)
	void *v;
{
	struct ax_softc *sc = (struct ax_softc *)v;

	ax_stop(sc);
}

struct cfattach ax_ca = {
	sizeof(struct ax_softc), ax_probe, ax_attach
};

struct cfdriver ax_cd = {
	0, "ax", DV_IFNET
};

