/*	$OpenBSD: mpcpcibus.c,v 1.1 1997/10/11 11:29:30 pefo Exp $ */

/*
 * Copyright (c) 1997 Per Fogelstrom
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
 *	This product includes software developed under OpenBSD for RTMX Inc
 *      by Per Fogelstrom, Opsycon AB.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * MPC106  PCI BUS Bridge driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/proc.h>
#include <vm/vm.h>

#include <machine/autoconf.h>

#include <dev/isa/isareg.h>
#include <dev/isa/isavar.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <powerpc/pci/pcibrvar.h>
#include <powerpc/pci/mpc106reg.h>

extern vm_map_t phys_map;

int	 mpcpcibrmatch __P((struct device *, void *, void *));
void	 mpcpcibrattach __P((struct device *, struct device *, void *));

void	 mpc_attach_hook __P((struct device *, struct device *,
				struct pcibus_attach_args *));
int	 mpc_bus_maxdevs __P((void *, int));
pcitag_t mpc_make_tag __P((void *, int, int, int));
void	 mpc_decompose_tag __P((void *, pcitag_t, int *, int *, int *));
pcireg_t mpc_conf_read __P((void *, pcitag_t, int));
void	 mpc_conf_write __P((void *, pcitag_t, int, pcireg_t));

int      mpc_intr_map __P((void *, pcitag_t, int, int, pci_intr_handle_t *));
const char *mpc_intr_string __P((void *, pci_intr_handle_t));
void     *mpc_intr_establish __P((void *, pci_intr_handle_t,
            int, int (*func)(void *), void *, char *));
void     mpc_intr_disestablish __P((void *, void *));
int      mpc_ether_hw_addr __P((u_int8_t *));

struct cfattach mpcpcibr_ca = {
        sizeof(struct pcibr_softc), mpcpcibrmatch, mpcpcibrattach,
};

struct cfdriver mpcpcibr_cd = {
	NULL, "mpcpcibr", DV_DULL,
};

static int      mpcpcibrprint __P((void *, const char *pnp));

struct pcibr_config mpc_config;

int
mpcpcibrmatch(parent, match, aux)
	struct device *parent;
	void *match, *aux;
{
	struct confargs *ca = aux;

	if (strcmp(ca->ca_name, mpcpcibr_cd.cd_name) != 0)
		return (0);

	return (1);
}

void
mpcpcibrattach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	struct pcibr_softc *sc = (struct pcibr_softc *)self;
	struct pcibr_config *lcp;
	struct pcibus_attach_args pba;

	switch(system_type) {
	case POWER4e:
		lcp = sc->sc_pcibr = &mpc_config;

		sc->sc_bus_space.bus_base = MPC106_V_PCI_MEM_SPACE;
		sc->sc_bus_space.bus_reverse = 1;

		lcp->lc_pc.pc_conf_v = lcp;
		lcp->lc_pc.pc_attach_hook = mpc_attach_hook;
		lcp->lc_pc.pc_bus_maxdevs = mpc_bus_maxdevs;
		lcp->lc_pc.pc_make_tag = mpc_make_tag;
		lcp->lc_pc.pc_decompose_tag = mpc_decompose_tag;
		lcp->lc_pc.pc_conf_read = mpc_conf_read;
		lcp->lc_pc.pc_conf_write = mpc_conf_write;
		lcp->lc_pc.pc_ether_hw_addr = mpc_ether_hw_addr;

	        lcp->lc_pc.pc_intr_v = lcp;
		lcp->lc_pc.pc_intr_map = mpc_intr_map;
		lcp->lc_pc.pc_intr_string = mpc_intr_string;
		lcp->lc_pc.pc_intr_establish = mpc_intr_establish;
		lcp->lc_pc.pc_intr_disestablish = mpc_intr_disestablish;

		printf(": MPC106, Revision %x.\n",
				mpc_cfg_read_1(MPC106_PCI_REVID));
		mpc_cfg_write_2(MPC106_PCI_STAT, 0xff80); /* Reset status */
		break;
	}

	pba.pba_busname = "pci";
	pba.pba_iot = &sc->sc_bus_space;
	pba.pba_memt = &sc->sc_bus_space;
	pba.pba_pc = &lcp->lc_pc;
	pba.pba_bus = 0;
	config_found(self, &pba, mpcpcibrprint);

}

static int
mpcpcibrprint(aux, pnp)
	void *aux;
	const char *pnp;
{
	struct pcibus_attach_args *pba = aux;

	if(pnp)
		printf("%s at %s", pba->pba_busname, pnp);
	printf(" bus %d", pba->pba_bus);
	return(UNCONF);
}

/*
 *  Get PCI physical address from given viritual address.
 *  XXX Note that cross page boundarys are *not* garantueed to work!
 */

vm_offset_t
vtophys(p)
	void *p;
{
	vm_offset_t pa;
	vm_offset_t va;

	va = (vm_offset_t)p;
	if((vm_offset_t)va < VM_MIN_KERNEL_ADDRESS) {
		pa = va;
	}
	else {
		pa = pmap_extract(vm_map_pmap(phys_map), va);
	}
	return(pa | MPC106_PCI_CPUMEM);
}

void
mpc_attach_hook(parent, self, pba)
	struct device *parent, *self;
	struct pcibus_attach_args *pba;
{
}

int
mpc_ether_hw_addr(p)
	u_int8_t *p;
{
	return(0);
}

int
mpc_bus_maxdevs(cpv, busno)
	void *cpv;
	int busno;
{
	return(16);
}

pcitag_t
mpc_make_tag(cpv, bus, dev, fnc)
	void *cpv;
	int bus, dev, fnc;
{
	return (bus << 16) | (dev << 11) | (fnc << 8);
}

void
mpc_decompose_tag(cpv, tag, busp, devp, fncp)
	void *cpv;
	pcitag_t tag;
	int *busp, *devp, *fncp;
{
	if (busp != NULL)
		*busp = (tag >> 16) & 0xff;
	if (devp != NULL)
		*devp = (tag >> 11) & 0x1f;
	if (fncp != NULL)
		*fncp = (tag >> 8) & 0x7;
}

pcireg_t
mpc_conf_read(cpv, tag, offset)
	void *cpv;
	pcitag_t tag;
	int offset;
{
	pcireg_t data;
	u_int32_t addr;
	int device;
	int s;

	if((tag >> 16) != 0)
		return(~0);
	if(offset & 3 || offset < 0 || offset >= 0x100) {
		printf ("pci_conf_read: bad reg %x\n", offset);
		return(~0);
	}

	device = (tag >> 11) & 0x1f;
	if(device > 11)
		return(~0);	/* Outside config space */

	addr = (0x800 << device) | (tag & 0x380) | offset;

	s = splhigh();

	/* low 20 bits of address are in the actual address */
	data = in32rb(MPC106_PCI_CONF_SPACE | addr);

	splx(s);
	return(data);
}

void
mpc_conf_write(cpv, tag, offset, data)
	void *cpv;
	pcitag_t tag;
	int offset;
	pcireg_t data;
{
	u_int32_t addr;
	int device;
	int s;

	device = (tag >> 11) & 0x1f;
	addr = (0x800 << device) | (tag & 0x380) | offset;

	s = splhigh();

	/* low 20 bits of address are in the actual address */
	out32rb(MPC106_PCI_CONF_SPACE | addr, data);

	splx(s);
}

int
mpc_intr_map(lcv, bustag, buspin, line, ihp)
	void *lcv;
	pcitag_t bustag;
	int buspin, line;
	pci_intr_handle_t *ihp;
{
	int error = 0;
	int route;
	int lvl;

	*ihp = -1;
        if (buspin == 0) {
                /* No IRQ used. */
                error = 1;
        }
        else if (buspin > 4) {
                printf("mpc_intr_map: bad interrupt pin %d\n", buspin);
                error = 1;
        }

	if(system_type == POWER4e) {
		route = in32rb(MPC106_PCI_CONF_SPACE + 0x860);
		switch(line) {
		case 14:
			line = 9;
			route &= ~0x000000ff;
			route |= line;
			break;
		case 11:
			line = 6;
			route &= ~0x0000ff00;
			route |= line << 8;
			break;
		case 12:
			line = 14;
			route &= ~0x00ff0000;
			route |= line << 16;
			break;
		case 13:
			line = 15;
			route &= ~0xff000000;
			route |= line << 24;
			break;
		}
		lvl = isa_inb(0x04d0);
		lvl |= isa_inb(0x04d1);
		lvl |= 1L << line;
		isa_outb(0x04d0, lvl);
		isa_outb(0x04d1, lvl >> 8);
		out32rb(MPC106_PCI_CONF_SPACE + 0x860, route);
	}

	if(!error)
		*ihp = line;
	return error;
}

const char *
mpc_intr_string(lcv, ih)
	void *lcv;
	pci_intr_handle_t ih;
{
	static char str[16];

	sprintf(str, "irq %d", ih);
	return(str);
}

void *
mpc_intr_establish(lcv, ih, level, func, arg, name)
	void *lcv;
	pci_intr_handle_t ih;
	int level;
	int (*func) __P((void *));
	void *arg;
	char *name;
{
	if (ih == 0 || ih >= ICU_LEN || ih == 2)
		panic("pci_intr_establish: bogus handle 0x%x\n", ih);

	return isabr_intr_establish(NULL, ih, IST_LEVEL, level, func, arg, name);
}

void
mpc_intr_disestablish(lcv, cookie)
	void *lcv, *cookie;
{
	/* XXX We should probably do something clever here.... later */
}

void
mpc_print_pci_stat()
{
	u_int32_t stat;

	stat = mpc_cfg_read_4(MPC106_PCI_CMD);
	printf("pci: status 0x%08x.\n", stat);
	stat = mpc_cfg_read_2(MPC106_PCI_STAT);
	printf("pci: status 0x%04x.\n", stat);
}
