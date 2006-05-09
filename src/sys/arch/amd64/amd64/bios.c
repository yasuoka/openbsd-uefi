/*
 * Copyright (c) 2006 Gordon Willem Klok <gklok@cogeco.ca>
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <sys/malloc.h>

#include <uvm/uvm_extern.h>
#include <sys/sysctl.h>

#include <machine/conf.h>
#include <machine/biosvar.h>
#include <machine/smbiosvar.h>

#include <dev/isa/isareg.h>
#include <amd64/include/isa_machdep.h>

struct bios_softc {
	struct device sc_dev;
};

void smbios_info(char *);
int bios_match(struct device *, void *, void *);
void bios_attach(struct device *, struct device *, void *);

struct cfattach bios_ca = {
	sizeof(struct bios_softc), bios_match, bios_attach
};

struct cfdriver bios_cd = {
	NULL, "bios", DV_DULL
};

struct smbios_entry smbios_entry;
/*
 * used by hw_sysctl
 */
extern char *hw_vendor, *hw_prod, *hw_uuid, *hw_serial, *hw_ver;
const char * smbios_uninfo[] = {
	"System",
	"Not Specified"
};

int
bios_match(struct device *parent, void *match , void *aux)
{
	struct bios_attach_args *bia = aux;
	/* only one */
	if (bios_cd.cd_ndevs || strcmp(bia->bios_dev, bios_cd.cd_name))
		return 0;

	return 1;
}

void
bios_attach(struct device *parent, struct device *self, void *aux)
{
	struct bios_softc *sc = (struct bios_softc *)self;
	vaddr_t va;
	paddr_t pa, end;
	u_int8_t *p;

	/* see if we have SMBIOS extentions */	
	for (p = ISA_HOLE_VADDR(SMBIOS_START);
	    p < (u_int8_t *)ISA_HOLE_VADDR(SMBIOS_END); p+= 16) {
		struct smbhdr * hdr = (struct smbhdr *)p;
		u_int8_t chksum;
		int i;

		if (hdr->sig != SMBIOS_SIGNATURE)
			continue;
		i = hdr->len;
		for (chksum = 0; i--; chksum += p[i])
		;
		if (chksum != 0)
			continue;
		p += 0x10;
		if (p[0] != '_' && p[1] != 'D' && p[2] != 'M' &&
		    p[3] != 'I' && p[4] != '_')
			continue;
		for (chksum = 0, i = 0xf; i--; chksum += p[i]);
			;
		if (chksum != 0)
			continue;

		pa = trunc_page(hdr->addr);
		end = round_page(hdr->addr + hdr->size);
		va = uvm_km_valloc(kernel_map, end-pa);
		if (va == 0)
			break;

		smbios_entry.addr = (u_int8_t *)(va + (hdr->addr & PGOFSET));
		smbios_entry.len = hdr->size;
		smbios_entry.mjr = hdr->majrev;
		smbios_entry.min = hdr->minrev;
		smbios_entry.count = hdr->count;

		for (; pa < end; pa+= NBPG, va+= NBPG)
			pmap_kenter_pa(va, pa, VM_PROT_READ);

		printf(": SMBIOS rev. %d.%d @ 0x%lx (%d entries)",
		    hdr->majrev, hdr->minrev, hdr->addr, hdr->count);
		smbios_info(sc->sc_dev.dv_xname);
		break;
	}

	printf("\n");
}

/*
 * smbios_find_table() takes a caller supplied smbios struct type and
 * a pointer to a handle (struct smbtable) returning one if the structure
 * is sucessfully located and zero otherwise. Callers should take care
 * to initilize the cookie field of the smbtable structure to zero before
 * the first invocation of this function.
 * Multiple tables of the same type can be located by repeadtly calling
 * smbios_find_table with the same arguments.
 */
int
smbios_find_table(u_int8_t type, struct smbtable *st)
{
	u_int8_t *va, *end;
	struct smbtblhdr *hdr;
	int ret = 0, tcount = 1;

	va = smbios_entry.addr;
	end = va + smbios_entry.len;

	/*
	 * The cookie field of the smtable structure is used to locate
	 * multiple instances of a table of an arbitrary type. Following the
	 * sucessful location of a table, the type is encoded as bits 0:7 of
	 * the cookie value, the offset in terms of the number of structures
	 * preceding that referenced by the handle is encoded in bits 15:31.
	 */
	if ((st->cookie & 0xfff) == type && st->cookie >> 16) {
		if ((u_int8_t *)st->hdr >= va && (u_int8_t *)st->hdr < end) {
			hdr = st->hdr;
			if (hdr->type == type) {
				va = (u_int8_t *) hdr + hdr->size;
				for (; va + 1 < end; va++)
					if (*va == NULL && *(va + 1) == NULL)
						break;
				va+= 2;
				tcount = st->cookie >> 16;
			}
		}
	}
	for (; va + sizeof(struct smbtblhdr) < end && tcount <=
	    smbios_entry.count; tcount++) {
		hdr = (struct smbtblhdr *) va;
		if (hdr->type == type) {
			ret = 1;
			st->hdr = hdr;
			st->tblhdr = va + sizeof(struct smbtblhdr);
			st->cookie = (tcount + 1) << 16 | type;
			break;
		}
		if (hdr->type == SMBIOS_TYPE_EOT)
			break;
		va+= hdr->size;
		for (; va + 1 < end; va++)
			if (*va == NULL && *(va + 1) == NULL)
				break;
		va+=2;
	}

	return ret;
}

char *
smbios_get_string(struct smbtable *st, u_int8_t indx)
{
	u_int8_t *va, *end;
	char *ret = NULL;
	int i;

	va = (u_int8_t *)st->hdr + st->hdr->size;
	end = smbios_entry.addr + smbios_entry.len;
	for (i = 1; va < end && i < indx && *va; i++)
		while (*va++)
			;
	if (i == indx)
		ret = (char *) va;

	return ret;
}

void
smbios_info(char * str)
{
	struct smbtable stbl, btbl;
	struct smbios_sys *sys;
	struct smbios_board *board;
	int i, uuidf, havebb;

	if (smbios_entry.mjr < 2)
		return;
	/*
	 * According to the spec the system table among others are required to
	 * be present, if it is not we dont bother with this smbios
	 * implementation.
	 */
	stbl.cookie = btbl.cookie = 0;
	if (!smbios_find_table(SMBIOS_TYPE_SYSTEM, &stbl))
		return;
	havebb = smbios_find_table(SMBIOS_TYPE_BASEBOARD, &btbl);

	sys = (struct smbios_sys *)stbl.tblhdr;
	if (havebb)
		board = (struct smbios_board *)btbl.tblhdr;
	/*
	 * Some smbios implementations have no system vendor or product strings,
	 * some have very uninformative data which is harder to work around
	 * and we must rely upon various heuristics to detect this. In both
	 * cases we attempt to fall back on the base board information in the
	 * perhaps naieve belief that motherboard vendors will supply this
	 * information.
	 */
	if ((hw_vendor = smbios_get_string(&stbl, sys->vendor)) != NULL) {
		for (i = 0; i < sizeof(smbios_uninfo) / sizeof(smbios_uninfo[0])
		    ; i++) {
			if ((strncmp(hw_vendor, smbios_uninfo[i],
			    strlen(smbios_uninfo[i]))) == 0) {
				if (havebb)
					hw_vendor = smbios_get_string(&btbl,
				   	    board->vendor);
				break;
			}
		}
	} else
		hw_vendor = smbios_get_string(&btbl, board->vendor);
	if ((hw_prod = smbios_get_string(&stbl, sys->product)) != NULL) {
		for (i = 0; i < sizeof(smbios_uninfo) / sizeof(smbios_uninfo[0])
		    ; i++) {
			if ((strncmp(hw_prod, smbios_uninfo[i],
			    strlen(smbios_uninfo[i]))) == 0) {
				if (havebb)
					hw_prod = smbios_get_string(&btbl,
				   		board->product);
				break;
			}
		}
	} else
		hw_prod = smbios_get_string(&btbl, board->product);
	if (hw_vendor != NULL && hw_prod != NULL)
		printf("\n%s: %s %s", str, hw_vendor, hw_prod);
	hw_ver = smbios_get_string(&stbl, sys->version);
	hw_serial = smbios_get_string(&stbl, sys->serial);
	if (smbios_entry.mjr > 2 || (smbios_entry.mjr == 2 &&
	    smbios_entry.min >= 1)) {
		/*
		 * If the uuid value is all 0xff the uuid is present but not
		 * set, if its all 0 then the uuid isnt present at all.
		 */
		uuidf |= SMBIOS_UUID_NPRESENT|SMBIOS_UUID_NSET;
		for (i = 0; i < sizeof(sys->uuid); i++) {
			if (sys->uuid[i] != 0xff)
				uuidf &= ~SMBIOS_UUID_NSET;
			if (sys->uuid[i] != 0)
				uuidf &= ~SMBIOS_UUID_NPRESENT;
		}

		if (uuidf & SMBIOS_UUID_NPRESENT)
			hw_uuid = NULL;
		else if (uuidf & SMBIOS_UUID_NSET)
			hw_uuid = "Not Set";
		else {
			hw_uuid = malloc(SMBIOS_UUID_REPLEN, M_DEVBUF,
			    M_NOWAIT);
			if (hw_uuid) {
				snprintf(hw_uuid, SMBIOS_UUID_REPLEN,
				    SMBIOS_UUID_REP,
				    sys->uuid[0], sys->uuid[1], sys->uuid[2],
				    sys->uuid[3], sys->uuid[4], sys->uuid[5],
				    sys->uuid[6], sys->uuid[7], sys->uuid[8],
				    sys->uuid[9], sys->uuid[10], sys->uuid[11],
				    sys->uuid[12], sys->uuid[13], sys->uuid[14],
				    sys->uuid[15]);
			}
		}
	}
}
