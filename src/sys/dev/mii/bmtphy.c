/*	$OpenBSD: bmtphy.c,v 1.2 2001/06/01 21:40:48 deraadt Exp $	*/
/*	$NetBSD: nsphy.c,v 1.25 2000/02/02 23:34:57 thorpej Exp $	*/

/*
 * driver for Broadcom BCM5201/5202 Mini-Theta ethernet 10/100 PHY
 * Data Sheet available from Broadcom
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

#include <dev/mii/bmtphyreg.h>

int	bmtphymatch __P((struct device *, void *, void *));
void	bmtphyattach __P((struct device *, struct device *, void *));

struct cfattach bmtphy_ca = {
	sizeof(struct mii_softc), bmtphymatch, bmtphyattach, mii_phy_detach,
	    mii_phy_activate
};

struct cfdriver bmtphy_cd = {
	NULL, "bmtphy", DV_DULL
};

int	bmtphy_service __P((struct mii_softc *, struct mii_data *, int));
void	bmtphy_status __P((struct mii_softc *));
void	bmtphy_reset __P((struct mii_softc *));

int
bmtphymatch(parent, match, aux)
	struct device *parent;
	void *match;
	void *aux;
{
	struct mii_attach_args *ma = aux;

	if (MII_OUI(ma->mii_id1, ma->mii_id2) == MII_OUI_BROADCOM &&
	    MII_MODEL(ma->mii_id2) == MII_MODEL_BROADCOM_BCM5201)
		return (10);
	if (MII_OUI(ma->mii_id1, ma->mii_id2) == MII_OUI_BROADCOM &&
	    MII_MODEL(ma->mii_id2) == MII_MODEL_BROADCOM_BCM5221)
		return (10);

	return (0);
}

void
bmtphyattach(parent, self, aux)
	struct device *parent;
	struct device *self;
	void *aux;
{
	struct mii_softc *sc = (struct mii_softc *)self;
	struct mii_attach_args *ma = aux;
	struct mii_data *mii = ma->mii_data;
	char *model;

	if (MII_MODEL(ma->mii_id2) == MII_MODEL_BROADCOM_BCM5201)
		model = MII_STR_BROADCOM_BCM5201;
	else if (MII_MODEL(ma->mii_id2) == MII_MODEL_BROADCOM_BCM5221)
		model = MII_STR_BROADCOM_BCM5221;

	printf(": %s, rev. %d\n", model, MII_REV(ma->mii_id2));

	sc->mii_inst = mii->mii_instance;
	sc->mii_phy = ma->mii_phyno;
	sc->mii_service = bmtphy_service;
	sc->mii_status = bmtphy_status;
	sc->mii_pdata = mii;
	sc->mii_flags = mii->mii_flags;

	bmtphy_reset(sc);

	sc->mii_capabilities =
	    PHY_READ(sc, MII_BMSR) & ma->mii_capmask;
	if (sc->mii_capabilities & BMSR_MEDIAMASK)
		mii_phy_add_media(sc);
}

int
bmtphy_service(sc, mii, cmd)
	struct mii_softc *sc;
	struct mii_data *mii;
	int cmd;
{
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	int reg;

	if ((sc->mii_dev.dv_flags & DVF_ACTIVE) == 0)
		return (ENXIO);

	switch (cmd) {
	case MII_POLLSTAT:
		/*
		 * If we're not polling our PHY instance, just return.
		 */
		if (IFM_INST(ife->ifm_media) != sc->mii_inst)
			return (0);
		break;

	case MII_MEDIACHG:
		/*
		 * If the media indicates a different PHY instance,
		 * isolate ourselves.
		 */
		if (IFM_INST(ife->ifm_media) != sc->mii_inst) {
			reg = PHY_READ(sc, MII_BMCR);
			PHY_WRITE(sc, MII_BMCR, reg | BMCR_ISO);
			return (0);
		}

		/*
		 * If the interface is not up, don't do anything.
		 */
		if ((mii->mii_ifp->if_flags & IFF_UP) == 0)
			break;

		mii_phy_setmedia(sc);
		break;

	case MII_TICK:
		/*
		 * If we're not currently selected, just return.
		 */
		if (IFM_INST(ife->ifm_media) != sc->mii_inst)
			return (0);

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
		 * Check to see if we have link.  If we do, we don't
		 * need to restart the autonegotiation process.  Read
		 * the BMSR twice in case it's latched.
		 */
		reg = PHY_READ(sc, MII_BMSR) |
		    PHY_READ(sc, MII_BMSR);
		if (reg & BMSR_LINK)
			return (0);

		/*
		 * Only retry autonegotiation every 5 seconds.
		 */
		if (++sc->mii_ticks != 5)
			return (0);

		sc->mii_ticks = 0;
		bmtphy_reset(sc);
		if (mii_phy_auto(sc, 0) == EJUSTRETURN)
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
bmtphy_status(sc)
	struct mii_softc *sc;
{
	struct mii_data *mii = sc->mii_pdata;
	struct ifmedia_entry *ife = mii->mii_media.ifm_cur;
	int bmsr, bmcr, auxc;

	mii->mii_media_status = IFM_AVALID;
	mii->mii_media_active = IFM_ETHER;

	bmsr = PHY_READ(sc, MII_BMSR) |
	    PHY_READ(sc, MII_BMSR);
	if (bmsr & BMSR_LINK)
		mii->mii_media_status |= IFM_ACTIVE;

	bmcr = PHY_READ(sc, MII_BMCR);
	if (bmcr & BMCR_ISO) {
		mii->mii_media_active |= IFM_NONE;
		mii->mii_media_status = 0;
		return;
	}

	if (bmcr & BMCR_LOOP)
		mii->mii_media_active |= IFM_LOOP;

	if (bmcr & BMCR_AUTOEN) {
		/*
		 * The later are only valid if autonegotiation
		 * has completed (or it's disabled).
		 */
		if ((bmsr & BMSR_ACOMP) == 0) {
			/* Erg, still trying, I guess... */
			mii->mii_media_active |= IFM_NONE;
			return;
		}

		auxc = PHY_READ(sc, MII_BMTPHY_AUXC);
		if (auxc & AUXC_SP100)
			mii->mii_media_active |= IFM_100_TX;
		else
			mii->mii_media_active |= IFM_10_T;
		if (auxc & AUXC_FDX)
			mii->mii_media_active |= IFM_FDX;

	} else
		mii->mii_media_active = ife->ifm_media;
}

void
bmtphy_reset(sc)
	struct mii_softc *sc;
{
	int anar;

	mii_phy_reset(sc);

	anar = PHY_READ(sc, MII_ANAR);
	anar |= BMSR_MEDIA_TO_ANAR(PHY_READ(sc, MII_BMSR));
	PHY_WRITE(sc, MII_ANAR, anar);

        /* Chip resets with FDX bit not set */
        PHY_WRITE(sc, MII_BMCR, PHY_READ(sc, MII_BMCR) |
	    BMCR_S100|BMCR_AUTOEN|BMCR_STARTNEG|BMCR_FDX);
}
