/*	$OpenBSD: upa.c,v 1.2 2002/06/11 11:03:07 jason Exp $	*/

/*
 * Copyright (c) 2002 Jason L. Wright (jason@thought.net)
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
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

/*
 *
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/conf.h>
#include <sys/timeout.h>
#include <sys/malloc.h>

#define _SPARC_BUS_DMA_PRIVATE
#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/openfirm.h>

#include <sparc64/dev/ebusreg.h>
#include <sparc64/dev/ebusvar.h>

#include "pckbd.h"
#if NPCKBD > 0
#include <dev/ic/pckbcvar.h>
#include <dev/pckbc/pckbdvar.h>
#endif

struct upa_range {
	u_int64_t	ur_space;
	u_int64_t	ur_addr;
	u_int64_t	ur_len;
};

struct upa_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_bt;
	bus_space_handle_t	sc_reg[3];
	struct upa_range	*sc_range;
	int			sc_node;
	int			sc_nrange;
	bus_space_tag_t		sc_cbt;
};

int	upa_match(struct device *, void *, void *);
void	upa_attach(struct device *, struct device *, void *);

struct cfattach upa_ca = {
	sizeof(struct upa_softc), upa_match, upa_attach
};

struct cfdriver upa_cd = {
	NULL, "upa", DV_DULL
};

int upa_print(void *, const char *);
bus_space_tag_t upa_alloc_bus_tag(struct upa_softc *);
int __upa_bus_map(bus_space_tag_t, bus_type_t, bus_addr_t,
    bus_size_t, int, vaddr_t, bus_space_handle_t *);

int
upa_match(parent, match, aux)
	struct device *parent;
	void *match;
	void *aux;
{
	struct mainbus_attach_args *ma = aux;

	if (strcmp(ma->ma_name, "upa") == 0)
		return (1);
	return (0);
}

void
upa_attach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	struct upa_softc *sc = (void *)self;
	struct mainbus_attach_args *ma = aux;
	int i, node;

	sc->sc_bt = ma->ma_bustag;
	sc->sc_node = ma->ma_node;

	for (i = 0; i < 3; i++) {
		if (i >= ma->ma_nreg) {
			printf(": no register %d\n", i);
			return;
		}
		if (bus_space_map(sc->sc_bt, ma->ma_reg[i].ur_paddr,
		    ma->ma_reg[i].ur_len, 0, &sc->sc_reg[i])) {
			printf(": failed to map reg%d\n", i);
			return;
		}
	}

	if (getprop(sc->sc_node, "ranges", sizeof(struct upa_range),
	    &sc->sc_nrange, (void **)&sc->sc_range))
		panic("upa: can't get ranges");

	printf("\n");

	sc->sc_cbt = upa_alloc_bus_tag(sc);

	for (node = OF_child(sc->sc_node); node; node = OF_peer(node)) {
		char buf[32];
		struct mainbus_attach_args map;

		bzero(&map, sizeof(map));
		if (OF_getprop(node, "device_type", buf, sizeof(buf)) <= 0)
			continue;
		if (getprop(node, "reg", sizeof(*map.ma_reg),
		    &map.ma_nreg, (void **)&map.ma_reg) != 0)
			continue;
		if (OF_getprop(node, "name", buf, sizeof(buf)) <= 0)
			continue;
		map.ma_node = node;
		map.ma_name = buf;
		map.ma_bustag = sc->sc_cbt;
		map.ma_dmatag = ma->ma_dmatag;
		config_found(&sc->sc_dev, &map, upa_print);
	}
}

int
upa_print(args, name)
	void *args;
	const char *name;
{
	struct mainbus_attach_args *ma = args;

	if (name)
		printf("%s at %s", ma->ma_name, name);
	return (UNCONF);
}

bus_space_tag_t
upa_alloc_bus_tag(sc)
	struct upa_softc *sc;
{
	bus_space_tag_t bt;

	bt = (bus_space_tag_t)malloc(sizeof(struct sparc_bus_space_tag),
	    M_DEVBUF, M_NOWAIT);
	if (bt == NULL)
		panic("upa: couldn't alloc bus tag");

	bzero(bt, sizeof *bt);
	bt->cookie = sc;
	bt->parent = sc->sc_bt;
	bt->type = sc->sc_bt->type;
	bt->sparc_bus_map = __upa_bus_map;
	/* XXX bt->sparc_bus_mmap = upa_bus_mmap; */
	/* XXX bt->sparc_intr_establish = upa_intr_establish; */
	return (bt);
}

int
__upa_bus_map(t, btype, offset, size, flags, vaddr, hp)
	bus_space_tag_t t;
	bus_type_t btype;
	bus_addr_t offset;
	bus_size_t size;
	int flags;
	vaddr_t vaddr;
	bus_space_handle_t *hp;
{
	struct upa_softc *sc = t->cookie;
	int i;

	for (i = 0; i < sc->sc_nrange; i++) {
		if (offset < sc->sc_range[i].ur_space)
			continue;
		if (offset >= (sc->sc_range[i].ur_space +
		    sc->sc_range[i].ur_space))
			continue;
		break;
	}
	if (i == sc->sc_nrange)
		return (EINVAL);

	offset -= sc->sc_range[i].ur_space;
	offset += sc->sc_range[i].ur_addr;

	return (bus_space_map2(sc->sc_bt, btype, offset, size,
	    flags, vaddr, hp));
}
