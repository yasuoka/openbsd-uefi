/*	$OpenBSD: rlphy.c,v 1.5 1999/12/07 22:01:32 jason Exp $	*/

/*
 * Copyright (c) 1998, 1999 Jason L. Wright (jason@thought.net)
 * All rights reserved.
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
 *	This product includes software developed by Jason L. Wright
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Driver for the internal PHY found on RTL8139 based nics, based
 * on drivers for the 'exphy' (Internal 3Com phys) and 'nsphy'
 * (National Semiconductor DP83840).
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/socket.h>
#include <sys/errno.h>

#include <net/if.h>
#include <net/if_media.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mii/miidevs.h>

int	rlphymatch __P((struct device *, void *, void *));
void	rlphyattach __P((struct device *, struct device *, void *));

struct cfattach rlphy_ca = {
	sizeof(struct mii_softc), rlphymatch, rlphyattach
};

struct cfdriver rlphy_cd = {
	NULL, "rlphy", DV_DULL
};

int	rlphy_service __P((struct mii_softc *, struct mii_data *, int));
void	rlphy_reset __P((struct mii_softc *));

int
rlphymatch(parent, match, aux)
	struct device *parent;
	void *match;
	void *aux;
{
	struct mii_attach_args *ma = aux;

	if (MII_OUI(ma->mii_id1, ma->mii_id2) != 0 ||
	    MII_MODEL(ma->mii_id2) != 0)
		return (0);

	if (strcmp(parent->dv_cfdata->cf_driver->cd_name, "rl") != 0)
		return (0);

	/*
	 * A "real" phy should get preference, but on the 8139 there
	 * is no phyid register.
	 */
	return (5);
}

void
rlphyattach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	struct mii_softc *sc = (struct mii_softc *)self;
	struct mii_attach_args *ma = aux;
	struct mii_data *mii = ma->mii_data;

	printf(": RTL internal phy\n");

	sc->mii_inst = mii->mii_instance;
	sc->mii_phy = ma->mii_phyno;
	sc->mii_service = rlphy_service;
	sc->mii_pdata = mii;

	rlphy_reset(sc);

	sc->mii_capabilities =
	    PHY_READ(sc, MII_BMSR) & ma->mii_capmask;
	if (sc->mii_capabilities & BMSR_MEDIAMASK)
		mii_add_media(sc);
}

int
rlphy_service(sc, mii, cmd)
	struct mii_softc *sc;
	struct mii_data *mii;
	int cmd;
{
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;

	/*
	 * Can't isolate the RTL8139 phy, so it has to be the only one.
	 */
	if (IFM_INST(ife->ifm_media) != sc->mii_inst)
		panic("rlphy_service: attempt to isolate phy");

	switch (cmd) {
	case MII_POLLSTAT:
		break;

	case MII_MEDIACHG:
		/*
		 * If the interface is not up, don't do anything.
		 */
		if ((mii->mii_ifp->if_flags & IFF_UP) == 0)
			break;

		switch (IFM_SUBTYPE(ife->ifm_media)) {
		case IFM_AUTO:
			/*
			 * If we're already in auto mode, just return.
			 */
			if (PHY_READ(sc, MII_BMCR) & BMCR_AUTOEN)
				return (0);
			(void) mii_phy_auto(sc, 1);
			break;
		default:
			mii_phy_setmedia(sc);
		}
		break;

	case MII_TICK:
		/*
		 * Only used for autonegotiation.
		 */
		if (IFM_SUBTYPE(ife->ifm_media) != IFM_AUTO)
			return (0);

		/*
		 * Is the interface even up?
		 */
		if ((mii->mii_ifp->if_flags & IFF_UP) == 0)
			return (0);

		/*
		 * The RTL8139 autonegotiation doesn't need to be
		 * kicked; it continues in the background.
		 */
		break;

	case MII_DOWN:
		mii_phy_down(sc);
		return (0);
	}

	/* Update the media status. */
	ukphy_status(sc);

	/* Callback if something changed. */
	if (sc->mii_active != mii->mii_media_active || cmd == MII_MEDIACHG) {
		(*mii->mii_statchg)(sc->mii_dev.dv_parent);
		sc->mii_active = mii->mii_media_active;
	}
	return (0);
}

void
rlphy_reset(sc)
	struct mii_softc *sc;
{
	int bmcr;

	/*
	 * XXX The RTL8139 doesn't set the BMCR properly
	 * XXX after reset, which breaks autoneg.
	 */

	bmcr = PHY_READ(sc, MII_BMCR);
	mii_phy_reset(sc);
	PHY_WRITE(sc, MII_BMCR, bmcr);
}
