/*	$OpenBSD: exphy.c,v 1.8 2000/08/26 20:04:17 nate Exp $	*/
/*	$NetBSD: exphy.c,v 1.23 2000/02/02 23:34:56 thorpej Exp $	*/

/*-
 * Copyright (c) 1998, 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe of the Numerical Aerospace Simulation Facility,
 * NASA Ames Research Center, and by Frank van der Linden.
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
 *	This product includes software developed by the NetBSD
 *	Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 1997 Manuel Bouyer.  All rights reserved.
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
 *	This product includes software developed by Manuel Bouyer.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * driver for 3Com internal PHYs
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/socket.h>

#include <net/if.h>
#include <net/if_media.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>
#include <dev/mii/miidevs.h>

int	exphymatch __P((struct device *, void *, void *));
void	exphyattach __P((struct device *, struct device *, void *));

struct cfattach exphy_ca = {
	sizeof(struct mii_softc), exphymatch, exphyattach, mii_phy_detach,
	    mii_phy_activate
};

struct cfdriver exphy_cd = {
	NULL, "exphy", DV_DULL
};

int	exphy_service __P((struct mii_softc *, struct mii_data *, int));
void	exphy_reset __P((struct mii_softc *));

int
exphymatch(parent, match, aux)
	struct device *parent;
	void *match;
	void *aux;
{
	struct mii_attach_args *ma = aux;

	/*
	 * Argh, 3Com PHY reports oui == 0 model == 0!
	 */
	if ((MII_OUI(ma->mii_id1, ma->mii_id2) != 0 ||
	     MII_MODEL(ma->mii_id2) != 0) &&
	    (MII_OUI(ma->mii_id1, ma->mii_id2) != MII_OUI_BROADCOM ||
	     MII_MODEL(ma->mii_id2) != MII_MODEL_BROADCOM_3C905C))
		return (0);

	/*
	 * Make sure the parent is an `xl'.
	 */
	if (strcmp(parent->dv_cfdata->cf_driver->cd_name, "xl") != 0)
		return (0);

	return (10);
}

void
exphyattach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	struct mii_softc *sc = (struct mii_softc *)self;
	struct mii_attach_args *ma = aux;
	struct mii_data *mii = ma->mii_data;

	if (MII_OUI(ma->mii_id1, ma->mii_id2) == 0 &&
	    MII_MODEL(ma->mii_id2) == 0)
		printf(": 3Com internal media interface\n");
	else if (MII_OUI(ma->mii_id1, ma->mii_id2) == MII_OUI_BROADCOM &&
	    MII_MODEL(ma->mii_id2) == MII_MODEL_BROADCOM_3C905C)
		printf(": %s, rev. %d\n", MII_STR_BROADCOM_3C905C,
		    MII_REV(ma->mii_id2));
	else
		printf(": unknown phy\n");

	sc->mii_inst = mii->mii_instance;
	sc->mii_phy = ma->mii_phyno;
	sc->mii_service = exphy_service;
	sc->mii_status = ukphy_status;
	sc->mii_pdata = mii;
	sc->mii_flags = mii->mii_flags;

	/*
	 * The 3Com PHY can never be isolated, so never allow non-zero
	 * instances!
	 */
	if (mii->mii_instance != 0) {
		printf("%s: ignoring this PHY, non-zero instance\n",
		    sc->mii_dev.dv_xname);
		return;
	}
	sc->mii_flags |= MIIF_NOISOLATE;

	exphy_reset(sc);

	sc->mii_capabilities =
	    PHY_READ(sc, MII_BMSR) & ma->mii_capmask;
	if (sc->mii_capabilities & BMSR_MEDIAMASK)
		mii_phy_add_media(sc);
}

int
exphy_service(sc, mii, cmd)
	struct mii_softc *sc;
	struct mii_data *mii;
	int cmd;
{
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;

	if ((sc->mii_dev.dv_flags & DVF_ACTIVE) == 0)
		return (ENXIO);

	/*
	 * We can't isolate the 3Com PHY, so it has to be the only one!
	 */
	if (IFM_INST(ife->ifm_media) != sc->mii_inst)
		panic("exphy_service: can't isolate 3Com PHY");

	switch (cmd) {
	case MII_POLLSTAT:
		break;

	case MII_MEDIACHG:
		/*
		 * If the interface is not up, don't do anything.
		 */
		if ((mii->mii_ifp->if_flags & IFF_UP) == 0)
			break;

		mii_phy_setmedia(sc);
		break;

	case MII_TICK:
		/*
		 * Only used for autonegotiation.
		 */
		if (IFM_SUBTYPE(ife->ifm_media) != IFM_AUTO)
			return (0);

		if (mii_phy_tick(sc) == EJUSTRETURN)
			return (0);

		break;

	case MII_DOWN:
		mii_phy_down(sc);
		return (0);
	}

	/* Update the media status. */
	mii_phy_status(sc);

	/* Callback if something changed. */
	mii_phy_update(sc, cmd);
	return (0);
}

void
exphy_reset(sc)
	struct mii_softc *sc;
{

	mii_phy_reset(sc);

	/*
	 * XXX 3Com PHY doesn't set the BMCR properly after
	 * XXX reset, which breaks autonegotiation.
	 */
	PHY_WRITE(sc, MII_BMCR, BMCR_S100|BMCR_AUTOEN|BMCR_FDX);
}
