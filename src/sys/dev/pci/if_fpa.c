/*	$OpenBSD: if_fpa.c,v 1.13 1999/11/30 02:25:53 jason Exp $	*/
/*	$NetBSD: if_fpa.c,v 1.15 1996/10/21 22:56:40 thorpej Exp $	*/

/*-
 * Copyright (c) 1995, 1996 Matt Thomas <matt@3am-software.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software withough specific prior written permission
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
 *
 * Id: if_fpa.c,v 1.8 1996/05/17 01:15:18 thomas Exp
 *
 */

/*
 * DEC PDQ FDDI Controller; code for BSD derived operating systems
 *
 *   This module supports the DEC DEFPA PCI FDDI Controller
 */


#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/device.h>

#include <net/if.h>
#include <net/if_types.h>
#include <net/if_dl.h>
#include <net/route.h>

#include "bpfilter.h"
#if NBPFILTER > 0
#include <net/bpf.h>
#include <net/bpfdesc.h>
#endif

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif
#include <net/if_fddi.h>

#include <vm/vm.h>
#include <vm/vm_kern.h>
#include <vm/vm_param.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcivar.h>
#include <dev/ic/pdqvar.h>
#include <dev/ic/pdqreg.h>

#define	DEFPA_LATENCY	0x88
#define	DEFPA_CBMA	(PCI_MAPREG_START + 0)	/* Config Base Memory Address */
#define	DEFPA_CBIO	(PCI_MAPREG_START + 4)	/* Config Base I/O Address */

int  pdq_pci_ifintr	__P((void *));
int  pdq_pci_match	__P((struct device *, void *, void *));
void pdq_pci_attach	__P((struct device *, struct device *, void *aux));

int
pdq_pci_ifintr(arg)
	void *arg;
{
	pdq_softc_t *sc = (pdq_softc_t *)arg;
	(void) pdq_interrupt(sc->sc_pdq);
	return (1);
}

int
pdq_pci_match(parent, match, aux)
	struct device *parent;
	void *match;
	void *aux;
{
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;

	if (PCI_VENDOR(pa->pa_id) != PCI_VENDOR_DEC)
		return (0);
	if (PCI_PRODUCT(pa->pa_id) != PCI_PRODUCT_DEC_DEFPA)
		return (0);
	return (1);
}

void
pdq_pci_attach(parent, self, aux)
	struct device *parent;
	struct device *self;
	void *aux;
{
	pdq_softc_t *sc = (pdq_softc_t *)self;
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	u_int32_t data;
	pci_intr_handle_t intrhandle;
	const char *intrstr;
	bus_addr_t csrbase;
	bus_size_t csrsize;
	int cacheable = 0;

	data = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_BHLC_REG);
	if (PCI_LATTIMER(data) < DEFPA_LATENCY) {
		data &= ~(PCI_LATTIMER_MASK << PCI_LATTIMER_SHIFT);
		data |= (DEFPA_LATENCY & PCI_LATTIMER_MASK)
		    << PCI_LATTIMER_SHIFT;
		pci_conf_write(pa->pa_pc, pa->pa_tag, PCI_BHLC_REG, data);
	}

	bcopy(sc->sc_dev.dv_xname, sc->sc_if.if_xname, IFNAMSIZ);
	sc->sc_if.if_flags = 0;
	sc->sc_if.if_softc = sc;

	/*
	 * NOTE: sc_bc is an alias for sc_csrtag and sc_membase is an
	 * alias for sc_csrhandle.  sc_iobase is not used in this front-end.
	 */
#ifdef PDQ_IOMAPPED
	sc->sc_csrtag = pa->pa_iot;
	if (pci_io_find(pa->pa_pc, pa->pa_tag, DEFPA_CBIO, &csrbase, &csrsize)){
		printf(": can't find I/O space!\n");
		return;
	}
#else
	sc->sc_csrtag = pa->pa_memt;
	if (pci_mem_find(pa->pa_pc, pa->pa_tag, DEFPA_CBMA, &csrbase, &csrsize,
	    &cacheable)) {
		printf(": can't find memory space!\n");
		return;
	}
#endif

	if (bus_space_map(sc->sc_csrtag, csrbase, csrsize, cacheable,
	    &sc->sc_csrhandle)) {
		printf(": can't map CSRs!\n");
		return;
	}

	if (pci_intr_map(pa->pa_pc, pa->pa_intrtag, pa->pa_intrpin,
	    pa->pa_intrline, &intrhandle)) {
		printf(": couldn't map interrupt\n");
		return;
	}
	intrstr = pci_intr_string(pa->pa_pc, intrhandle);
	sc->sc_ih = pci_intr_establish(pa->pa_pc, intrhandle, IPL_NET,
	    pdq_pci_ifintr, sc, self->dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	if (intrstr != NULL)
		printf(": %s\n", intrstr);

	sc->sc_pdq = pdq_initialize(sc->sc_csrtag, sc->sc_csrhandle,
	    sc->sc_if.if_xname, 0, (void *) sc, PDQ_DEFPA);
	if (sc->sc_pdq == NULL) {
		printf(": initialization failed\n");
		return;
	}

	bcopy((caddr_t) sc->sc_pdq->pdq_hwaddr.lanaddr_bytes,
	    sc->sc_ac.ac_enaddr, 6);
	pdq_ifattach(sc, NULL);

	sc->sc_ats = shutdownhook_establish((void (*)(void *)) pdq_hwreset,
	    sc->sc_pdq);
	if (sc->sc_ats == NULL)
		printf("%s: warning: couldn't establish shutdown hook\n",
		    self->dv_xname);
}

struct cfattach fpa_ca = {
	sizeof(pdq_softc_t), pdq_pci_match, pdq_pci_attach
};

struct cfdriver fpa_cd = {
	0, "fpa", DV_IFNET
};
