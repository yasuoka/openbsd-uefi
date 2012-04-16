/*	$OpenBSD: gio.c,v 1.3 2012/04/16 22:28:12 miod Exp $	*/
/*	$NetBSD: gio.c,v 1.32 2011/07/01 18:53:46 dyoung Exp $	*/

/*
 * Copyright (c) 2012 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
/*
 * Copyright (c) 2000 Soren S. Jorvang
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
 *          This product includes software developed for the
 *          NetBSD Project.  See http://www.NetBSD.org/ for
 *          information about NetBSD.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <mips64/archtype.h>

#include <machine/autoconf.h>
#include <machine/bus.h>

#include <sgi/gio/gioreg.h>
#include <sgi/gio/giovar.h>
#include <sgi/gio/giodevs_data.h>

#include <sgi/localbus/imcvar.h>
#include <sgi/localbus/intreg.h>
#include <sgi/localbus/intvar.h>
#include <sgi/sgi/ip22.h>

#include "grtwo.h"
#include "light.h"
#include "newport.h"

#if NGRTWO > 0
#include <sgi/gio/grtwovar.h>
#endif
#if NLIGHT > 0
#include <sgi/gio/lightvar.h>
#endif
#if NNEWPORT > 0
#include <sgi/gio/newportvar.h>
#endif

int	 gio_match(struct device *, void *, void *);
void	 gio_attach(struct device *, struct device *, void *);
int	 gio_print(void *, const char *);
int	 gio_print_fb(void *, const char *);
int	 gio_search(struct device *, void *, void *);
int	 gio_submatch(struct device *, void *, void *);
uint32_t gio_id(vaddr_t, paddr_t, int);

struct gio_softc {
	struct device	sc_dev;

	bus_space_tag_t	sc_iot;
	bus_dma_tag_t	sc_dmat;
};

const struct cfattach gio_ca = {
	sizeof(struct gio_softc), gio_match, gio_attach
};

struct cfdriver gio_cd = {
	NULL, "gio", DV_DULL
};

/* Address of the console frame buffer registers, if applicable */
paddr_t		giofb_consaddr;
/* Names of the frame buffers, as obtained by ARCBios */
const char	*giofb_names[GIO_MAX_FB];

struct gio_probe {
	uint32_t slot;
	uint64_t base;
	uint32_t mach_type;
	uint32_t mach_subtype;
};

/* an invalid GIO ID value used to report the device might be a frame buffer */
#define	GIO_FAKE_FB_ID	(0xffffff7f)

/*
 * Expansion Slot Base Addresses
 *
 * IP20 and IP24 have two GIO connectors: GIO_SLOT_EXP0 and
 * GIO_SLOT_EXP1.
 *
 * On IP24 these slots exist on the graphics board or the IOPLUS
 * "mezzanine" on Indy and Challenge S, respectively. The IOPLUS or
 * graphics board connects to the mainboard via a single GIO64 connector.
 *
 * IP22 has either three or four physical connectors, but only two
 * electrically distinct slots: GIO_SLOT_GFX and GIO_SLOT_EXP0.
 *
 * It should also be noted that DMA is (mostly) not supported in Challenge S's
 * GIO_SLOT_EXP1. See gio(4) for the story.
 */
static const struct gio_probe slot_bases[] = {
	/* GFX is only a slot on Indigo 2 */
	{ GIO_SLOT_GFX, GIO_ADDR_GFX, SGI_IP22, IP22_INDIGO2 },

	/* EXP0 is available on all systems */
	{ GIO_SLOT_EXP0, GIO_ADDR_EXP0, SGI_IP20, -1 },
	{ GIO_SLOT_EXP0, GIO_ADDR_EXP0, SGI_IP22, -1 },

	/* EXP1 does not exist on Indigo 2 */
	{ GIO_SLOT_EXP1, GIO_ADDR_EXP1, SGI_IP20, -1 },
	{ GIO_SLOT_EXP1, GIO_ADDR_EXP1, SGI_IP22, IP22_INDY },
	{ GIO_SLOT_EXP1, GIO_ADDR_EXP1, SGI_IP22, IP22_CHALLS },

	{ 0, 0, 0, 0 }
};

/*
 * Graphic Board Base Addresses
 *
 * Graphics boards are not treated like expansion slot cards. Their base
 * addresses do not necessarily correspond to GIO slot addresses and they
 * do not contain product identification words.
 *
 * This list needs to be sorted in address order, to match the descriptions
 * obtained from ARCBios.
 */
static const struct gio_probe gfx_bases[] = {
	{ -1, GIO_ADDR_GFX, SGI_IP20, -1 },
	{ -1, GIO_ADDR_GFX, SGI_IP22, -1 },

	/* IP20 LG1/LG2 */
	{ -1, GIO_ADDR_GFX + 0x003f0000, SGI_IP20, -1 },
	{ -1, GIO_ADDR_GFX + 0x003f8000, SGI_IP20, -1 }, /* second head */

	{ -1, GIO_ADDR_EXP0, SGI_IP22, -1 },
	{ -1, GIO_ADDR_EXP1, SGI_IP22, -1 },

	{ 0, 0, 0, 0 }
};

int
gio_match(struct device *parent, void *match, void *aux)
{
	struct imc_attach_args *iaa = aux;

	if (strcmp(iaa->iaa_name, gio_cd.cd_name) != 0)
		return 0;

	return 1;
}

void
gio_attach(struct device *parent, struct device *self, void *aux)
{
	struct gio_softc *sc = (struct gio_softc *)self;
	struct imc_attach_args *iaa = (struct imc_attach_args *)aux;
	struct gio_attach_args ga;
	uint32_t gfx[GIO_MAX_FB];
	uint i, j, ngfx;

	printf("\n");

	sc->sc_iot = iaa->iaa_st;
	sc->sc_dmat = iaa->iaa_dmat;

	ngfx = 0;
	memset(gfx, 0, sizeof(gfx));

	/*
	 * Try and attach graphics devices first.
	 * Unfortunately, they - not being GIO devices after all - do not
	 * contain a Product Identification Word, nor have a slot number.
	 *
	 * Record addresses to which graphics devices attach so that
	 * we do not confuse them with expansion slots, should the
	 * addresses coincide.
	 *
	 * Unfortunately graphics devices for which we have no configured
	 * driver, which address matches a regular slot number, will show
	 * up as rogue devices attached to real slots.
	 *
	 * If only the ARCBios component tree would be so kind as to give
	 * us the address of the frame buffer components...
	 */
	if (sys_config.system_type != SGI_IP22 ||
	    sys_config.system_subtype != IP22_CHALLS) {
		for (i = 0; gfx_bases[i].base != 0; i++) {
			/* skip slots that don't apply to us */
			if (gfx_bases[i].mach_type != sys_config.system_type)
				continue;

			if (gfx_bases[i].mach_subtype != -1 &&
			    gfx_bases[i].mach_subtype !=
			      sys_config.system_subtype)
				continue;

			ga.ga_addr = gfx_bases[i].base;
			ga.ga_ioh = PHYS_TO_XKPHYS(ga.ga_addr, CCA_NC);

			if (gio_id(ga.ga_ioh, ga.ga_addr, 1) != GIO_FAKE_FB_ID)
				continue;

			ga.ga_iot = sc->sc_iot;
			ga.ga_dmat = sc->sc_dmat;
			ga.ga_slot = -1;
			ga.ga_product = -1;
			if (ngfx < GIO_MAX_FB)
				ga.ga_descr = giofb_names[ngfx];
			else
				ga.ga_descr = NULL;	/* shouldn't happen */

			if (config_found_sm(self, &ga, gio_print_fb,
			    gio_submatch))
				gfx[ngfx] = gfx_bases[i].base;

			ngfx++;
		}
	}

	/*
	 * Now attach any GIO expansion cards.
	 *
	 * Be sure to skip any addresses to which a graphics device has
	 * already been attached.
	 */
	for (i = 0; slot_bases[i].base != 0; i++) {
		int skip = 0;

		/* skip slots that don't apply to us */
		if (slot_bases[i].mach_type != sys_config.system_type)
			continue;

		if (slot_bases[i].mach_subtype != -1 &&
		    slot_bases[i].mach_subtype != sys_config.system_subtype)
			continue;

		for (j = 0; j < ngfx; j++) {
			if (slot_bases[i].base == gfx[j]) {
				skip = 1;
				break;
			}
		}
		if (skip)
			continue;

		ga.ga_addr = slot_bases[i].base;
		ga.ga_iot = sc->sc_iot;
		ga.ga_ioh = PHYS_TO_XKPHYS(ga.ga_addr, CCA_NC);

		if (gio_id(ga.ga_ioh, ga.ga_addr, 0) == 0)
			continue;

		ga.ga_dmat = sc->sc_dmat;
		ga.ga_slot = slot_bases[i].slot;
		ga.ga_product = bus_space_read_4(ga.ga_iot, ga.ga_ioh, 0);
		ga.ga_descr = NULL;

		config_found_sm(self, &ga, gio_print, gio_submatch);
	}

	config_search(gio_search, self, aux);
}

/*
 * Try and figure out whether there is a device at the given slot address.
 */
uint32_t
gio_id(vaddr_t va, paddr_t pa, int maybe_gfx)
{
	uint32_t id32;
	uint16_t id16 = 0;
	uint8_t id8 = 0;

	/*
	 * First, attempt to read the address with various sizes.
	 * If the slot is pipelined, and the address does not hit a
	 * device register, we will not fault but read the transfer
	 * width back.
	 */

	if (guarded_read_4(va, &id32) != 0)
		return 0;
	if (guarded_read_2(va | 2, &id16) != 0)
		return 0;
	if (guarded_read_1(va | 3, &id8) != 0)
		return 0;

	/*
	 * If the address doesn't match a base slot address, then we are
	 * only probing for a frame buffer.
	 */

	if (pa != GIO_ADDR_GFX && pa != GIO_ADDR_EXP0 && pa != GIO_ADDR_EXP1 &&
	    maybe_gfx == 0)
		return 0;

	/*
	 * If there is a real GIO device at this address (as opposed to
	 * a graphics card), then the low-order 8 bits of each read need
	 * to be consistent...
	 */

	if (id8 == (id16 & 0xff) && id8 == (id32 & 0xff)) {
		if (GIO_PRODUCT_32BIT_ID(id8)) {
			if (id16 == (id32 & 0xffff))
				return id32;
		} else {
			if (id8 != 0)
				return id32;
		}
	}

	/*
	 * If there is a frame buffer device, then either we have hit a
	 * device register (light, grtwo), or we did not fault because
	 * the slot is pipelined (impact, newport).
	 * In the latter case, we attempt to probe a known register
	 * offset.
	 */

	if (maybe_gfx) {
		if (id32 != 4 || id16 != 2 || id8 != 1)
			return GIO_FAKE_FB_ID;

		/* could be impact(4) */
		va += 0x70000;
		if (guarded_read_4(va, &id32) == 0 &&
		    guarded_read_2(va | 2, &id16) == 0 &&
		    guarded_read_1(va | 3, &id8) == 0) {
			if (id32 != 4 || id16 != 2 || id8 != 1)
				return GIO_FAKE_FB_ID;
		}

		/* could be newport(4) */
		va += 0x80000;
		if (guarded_read_4(va, &id32) == 0 &&
		    guarded_read_2(va | 2, &id16) == 0 &&
		    guarded_read_1(va | 3, &id8) == 0) {
			if (id32 != 4 || id16 != 2 || id8 != 1)
				return GIO_FAKE_FB_ID;
		}

		return 0;
	}

	return 0;
}

int
gio_print(void *aux, const char *pnp)
{
	struct gio_attach_args *ga = aux;
	const char *descr;
	int product, revision;
	uint i;

	product = GIO_PRODUCT_PRODUCTID(ga->ga_product);
	if (GIO_PRODUCT_32BIT_ID(ga->ga_product))
		revision = GIO_PRODUCT_REVISION(ga->ga_product);
	else
		revision = 0;

	descr = "unknown GIO card";
	for (i = 0; gio_knowndevs[i].productid != 0; i++) {
		if (gio_knowndevs[i].productid == product) {
			descr = gio_knowndevs[i].product;
			break;
		}
	}

	if (pnp != NULL) {
		printf("%s", descr);
		if (ga->ga_product != -1)
			printf(" (product 0x%02x revision 0x%02x)",
			    product, revision);
		printf(" at %s", pnp);
	}

	if (ga->ga_slot != -1)
		printf(" slot %d", ga->ga_slot);
	printf(" addr 0x%lx", ga->ga_addr);

	return UNCONF;
}

int
gio_print_fb(void *aux, const char *pnp)
{
	struct gio_attach_args *ga = aux;

	if (pnp != NULL)
		printf("framebuffer at %s", pnp);

	if (ga->ga_addr != (uint64_t)-1)
		printf(" addr 0x%lx", ga->ga_addr);

	return UNCONF;
}

int
gio_search(struct device *parent, void *vcf, void *aux)
{
	struct gio_softc *sc = (struct gio_softc *)parent;
	struct cfdata *cf = (struct cfdata *)vcf;
	struct gio_attach_args ga;

	/* Handled by direct configuration, so skip here */
	if (cf->cf_loc[1 /*GIOCF_ADDR*/] == -1)
		return 0;

	ga.ga_addr = (uint64_t)cf->cf_loc[1 /*GIOCF_ADDR*/];
	ga.ga_iot = sc->sc_iot;
	ga.ga_ioh = PHYS_TO_XKPHYS(ga.ga_addr, CCA_NC);
	ga.ga_dmat = sc->sc_dmat;
	ga.ga_slot = cf->cf_loc[0 /*GIOCF_SLOT*/];
	ga.ga_product = -1;
	ga.ga_descr = NULL;

	if ((*cf->cf_attach->ca_match)(parent, cf, &ga) == 0)
		return 0;

	config_attach(parent, cf, &ga, gio_print);

	return 1;
}

int
gio_submatch(struct device *parent, void *vcf, void *aux)
{
	struct cfdata *cf = (struct cfdata *)vcf;
	struct gio_attach_args *ga = (struct gio_attach_args *)aux;

	if (cf->cf_loc[0 /*GIOCF_SLOT*/] != -1 &&
	    cf->cf_loc[0 /*GIOCF_SLOT*/] != ga->ga_slot)
		return 0;

	if (cf->cf_loc[1 /*GIOCF_ADDR*/] != -1 &&
	    (uint64_t)cf->cf_loc[1 /*GIOCF_ADDR*/] != ga->ga_addr)
		return 0;

	return (*cf->cf_attach->ca_match)(parent, cf, aux);
}

int
giofb_cnprobe()
{
	struct gio_attach_args ga;
	int i;

	for (i = 0; gfx_bases[i].base != 0; i++) {
		if (gfx_bases[i].base != giofb_consaddr)
			continue;

		/* skip bases that don't apply to us */
		if (gfx_bases[i].mach_type != sys_config.system_type)
			continue;

		if (gfx_bases[i].mach_subtype != -1 &&
		    gfx_bases[i].mach_subtype != sys_config.system_subtype)
			continue;

		ga.ga_addr = gfx_bases[i].base;
		ga.ga_iot = &imcbus_tag;
		ga.ga_ioh = PHYS_TO_XKPHYS(ga.ga_addr, CCA_NC);
		ga.ga_dmat = &imc_bus_dma_tag;
		ga.ga_slot = -1;
		ga.ga_product = -1;
		ga.ga_descr = NULL;

		if (gio_id(ga.ga_ioh, ga.ga_addr, 1) != GIO_FAKE_FB_ID)
			continue;

#if NGRTWO > 0
		if (grtwo_cnprobe(&ga) != 0)
			return 0;
#endif
#if NLIGHT > 0
		if (light_cnprobe(&ga) != 0)
			return 0;
#endif
#if NNEWPORT > 0
		if (newport_cnprobe(&ga) != 0)
			return 0;
#endif
	}

	return ENXIO;
}

int
giofb_cnattach()
{
	struct gio_attach_args ga;
	int i;

	for (i = 0; gfx_bases[i].base != 0; i++) {
		if (gfx_bases[i].base != giofb_consaddr)
			continue;

		/* skip bases that don't apply to us */
		if (gfx_bases[i].mach_type != sys_config.system_type)
			continue;

		if (gfx_bases[i].mach_subtype != -1 &&
		    gfx_bases[i].mach_subtype != sys_config.system_subtype)
			continue;

		ga.ga_addr = gfx_bases[i].base;
		ga.ga_iot = &imcbus_tag;
		ga.ga_ioh = PHYS_TO_XKPHYS(ga.ga_addr, CCA_NC);
		ga.ga_dmat = &imc_bus_dma_tag;
		ga.ga_slot = -1;
		ga.ga_product = -1;
		ga.ga_descr = NULL;

		if (gio_id(ga.ga_ioh, ga.ga_addr, 1) != GIO_FAKE_FB_ID)
			continue;

		/*
		 * We still need to probe before attach since we don't
		 * know the frame buffer type here.
		 */
#if NGRTWO > 0
		if (grtwo_cnprobe(&ga) != 0 &&
		    grtwo_cnattach(&ga) == 0)
			return 0;
#endif
#if NLIGHT > 0
		if (light_cnprobe(&ga) != 0 &&
		    light_cnattach(&ga) == 0)
			return 0;
#endif
#if NNEWPORT > 0
		if (newport_cnprobe(&ga) != 0 &&
		    newport_cnattach(&ga) == 0)
			return 0;
#endif
	}

	return ENXIO;
}

/*
 * Devices living in the expansion slots must enable or disable some
 * GIO arbiter settings. This is accomplished via imc(4) registers.
 */
int
gio_arb_config(int slot, uint32_t flags)
{
	if (flags == 0)
		return (EINVAL);

	if (flags & ~(GIO_ARB_RT | GIO_ARB_LB | GIO_ARB_MST | GIO_ARB_SLV |
	    GIO_ARB_PIPE | GIO_ARB_NOPIPE | GIO_ARB_32BIT | GIO_ARB_64BIT |
	    GIO_ARB_HPC2_32BIT | GIO_ARB_HPC2_64BIT))
		return (EINVAL);

	if (((flags & GIO_ARB_RT)   && (flags & GIO_ARB_LB))  ||
	    ((flags & GIO_ARB_MST)  && (flags & GIO_ARB_SLV)) ||
	    ((flags & GIO_ARB_PIPE) && (flags & GIO_ARB_NOPIPE)) ||
	    ((flags & GIO_ARB_32BIT) && (flags & GIO_ARB_64BIT)) ||
	    ((flags & GIO_ARB_HPC2_32BIT) && (flags & GIO_ARB_HPC2_64BIT)))
		return (EINVAL);

	return (imc_gio64_arb_config(slot, flags));
}

/*
 * Establish an interrupt handler for expansion boards (not frame buffers!)
 * in the specified slot.
 *
 * Indy and Challenge S have a single GIO interrupt per GIO slot, but
 * distinct slot interrups. Indigo and Indigo2 have three GIO interrupts per
 * slot, but at a given GIO interrupt level, all slots share the same
 * interrupt on the interrupt controller.
 *
 * Expansion boards appear to always use the intermediate level.
 */
void *
gio_intr_establish(int slot, int level, int (*func)(void *), void *arg,
    const char *what)
{
	int intr;

	switch (sys_config.system_type) {
	case SGI_IP20:
		if (slot == GIO_SLOT_GFX)
			return NULL;
		intr = INT2_L0_INTR(INT2_L0_GIO_LVL1);
		break;
	case SGI_IP22:
	case SGI_IP26:
	case SGI_IP28:
		if (sys_config.system_subtype == IP22_INDIGO2) {
			if (slot == GIO_SLOT_EXP1)
				return NULL;
			intr = INT2_L0_INTR(INT2_L0_GIO_LVL1);
		} else {
			if (slot == GIO_SLOT_GFX)
				return NULL;
			intr = INT2_MAP1_INTR(slot == GIO_SLOT_EXP0 ?
			    INT2_MAP_GIO_SLOT0 : INT2_MAP_GIO_SLOT1);
		}
		break;
	default:
		return NULL;
	}

	return int2_intr_establish(intr, level, func, arg, what);
}

const char *
gio_product_string(int prid)
{
	int i;

	for (i = 0; gio_knowndevs[i].product != NULL; i++)
		if (gio_knowndevs[i].productid == prid)
			return (gio_knowndevs[i].product);

	return (NULL);
}
