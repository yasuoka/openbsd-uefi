/*	$OpenBSD: bios.c,v 1.31 2015/03/14 03:38:46 jsg Exp $	*/
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
#include <machine/mpbiosvar.h>
#include <machine/smbiosvar.h>

#include <dev/isa/isareg.h>
#include <amd64/include/isa_machdep.h>
#include <dev/pci/pcivar.h>

#include <dev/acpi/acpivar.h>

#include <dev/rndvar.h>

#include "acpi.h"
#include "mpbios.h"
#include "pci.h"

struct bios_softc {
	struct device sc_dev;
};

void smbios_info(char *);
int bios_match(struct device *, void *, void *);
void bios_attach(struct device *, struct device *, void *);
int bios_print(void *, const char *);
char *fixstring(char *);

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

const char *smbios_uninfo[] = {
	"System",
	"Not ",
	"To be",
	"SYS-"
};

int
bios_match(struct device *parent, void *match , void *aux)
{
	struct bios_attach_args *bia = aux;

	/* only one */
	if (bios_cd.cd_ndevs || strcmp(bia->ba_name, bios_cd.cd_name))
		return 0;
	return 1;
}

static struct smbhdr *
bios_find(u_int8_t *p)
{
	struct smbhdr * hdr = (struct smbhdr *)p;
	u_int8_t chksum;
	int i;

	if (hdr->sig != SMBIOS_SIGNATURE)
		return (NULL);
	i = hdr->len;
	for (chksum = 0; i--; chksum += p[i])
		;
	if (chksum != 0)
		return (NULL);
	p += 0x10;
	if (p[0] != '_' && p[1] != 'D' && p[2] != 'M' && p[3] != 'I' &&
	    p[4] != '_')
		return (NULL);
	for (chksum = 0, i = 0xf; i--; chksum += p[i])
		;
	if (chksum != 0)
		return (NULL);
	if (hdr->sig != SMBIOS_SIGNATURE)
		return (NULL);

	return (hdr);
}

void
bios_attach(struct device *parent, struct device *self, void *aux)
{
	struct bios_softc *sc = (struct bios_softc *)self;
	struct smbios_struct_bios *sb;
	struct smbtable bios;
	char scratch[64];
	vaddr_t va;
	paddr_t pa, end;
	u_int8_t *p;
	int smbiosrev = 0;
	struct smbhdr *hdr = NULL;
	extern bios_efiinfo_t	*bios_efiinfo;	/* XXX */

	if (bios_efiinfo != NULL && bios_efiinfo->config_smbios != 0)
		hdr = bios_find(PMAP_DIRECT_MAP(
		    (u_int8_t *)bios_efiinfo->config_smbios));

	if (hdr == NULL) {
		/* see if we have SMBIOS extentions */
		for (p = ISA_HOLE_VADDR(SMBIOS_START);
		    p < (u_int8_t *)ISA_HOLE_VADDR(SMBIOS_END); p+= 16) {
			hdr = bios_find(p);
			if (hdr == NULL)
				continue;
		}
	}

	if (hdr != NULL) {
		pa = trunc_page(hdr->addr);
		end = round_page(hdr->addr + hdr->size);
		va = uvm_km_valloc(kernel_map, end-pa);
		if (va == 0)
			goto out;

		smbios_entry.addr = (u_int8_t *)(va + (hdr->addr & PGOFSET));
		smbios_entry.len = hdr->size;
		smbios_entry.mjr = hdr->majrev;
		smbios_entry.min = hdr->minrev;
		smbios_entry.count = hdr->count;

		for (; pa < end; pa+= NBPG, va+= NBPG)
			pmap_kenter_pa(va, pa, PROT_READ);

		printf(": SMBIOS rev. %d.%d @ 0x%x (%d entries)",
		    hdr->majrev, hdr->minrev, hdr->addr, hdr->count);

		smbiosrev = hdr->majrev * 100 + hdr->minrev;
		if (hdr->minrev < 10)
			smbiosrev = hdr->majrev * 100 + hdr->minrev * 10;

		bios.cookie = 0;
		if (smbios_find_table(SMBIOS_TYPE_BIOS, &bios)) {
			sb = bios.tblhdr;
			printf("\n%s:", sc->sc_dev.dv_xname);
			if ((smbios_get_string(&bios, sb->vendor,
			    scratch, sizeof(scratch))) != NULL)
				printf(" vendor %s",
				    fixstring(scratch));
			if ((smbios_get_string(&bios, sb->version,
			    scratch, sizeof(scratch))) != NULL)
				printf(" version \"%s\"",
				    fixstring(scratch));
			if ((smbios_get_string(&bios, sb->release,
			    scratch, sizeof(scratch))) != NULL)
				printf(" date %s", fixstring(scratch));
		}

		smbios_info(sc->sc_dev.dv_xname);
	}
out:
	printf("\n");

	/* No SMBIOS extensions, go looking for Soekris comBIOS */
	if (smbiosrev == 0) {
		const char *signature = "Soekris Engineering";

		for (p = ISA_HOLE_VADDR(SMBIOS_START);
		    p <= (u_int8_t *)ISA_HOLE_VADDR(SMBIOS_END -
		    (strlen(signature) - 1)); p++)
			if (!memcmp(p, signature, strlen(signature))) {
				hw_vendor = malloc(strlen(signature) + 1,
				    M_DEVBUF, M_NOWAIT);
				if (hw_vendor)
					strlcpy(hw_vendor, signature,
					    strlen(signature) + 1);
				p += strlen(signature);
				break;
			}

		for (; hw_vendor &&
		    p <= (u_int8_t *)ISA_HOLE_VADDR(SMBIOS_END - 6); p++)
			/*
			 * Search only for "net6501" in the comBIOS as that's
			 * the only Soekris platform that can run amd64
			 */
			if (!memcmp(p, "net6501", 7)) {
				hw_prod = malloc(8, M_DEVBUF, M_NOWAIT);
				if (hw_prod) {
					memcpy(hw_prod, p, 7);
					hw_prod[7] = '\0';
				}
				break;
			}
	}

#if NACPI > 0
	{
		struct bios_attach_args ba;

		memset(&ba, 0, sizeof(ba));
		ba.ba_name = "acpi";
		ba.ba_iot = X86_BUS_SPACE_IO;
		ba.ba_memt = X86_BUS_SPACE_MEM;

		if (bios_efiinfo != NULL)
			ba.ba_acpipbase = bios_efiinfo->config_acpi;

		config_found(self, &ba, bios_print);
	}
#endif

#if NMPBIOS > 0
	if (mpbios_probe(self)) {
		struct bios_attach_args ba;

		memset(&ba, 0, sizeof(ba));
		ba.ba_name = "mpbios";
		ba.ba_iot = X86_BUS_SPACE_IO;
		ba.ba_memt = X86_BUS_SPACE_MEM;

		config_found(self, &ba, bios_print);
	}
#endif
}

/*
 * smbios_find_table() takes a caller supplied smbios struct type and
 * a pointer to a handle (struct smbtable) returning one if the structure
 * is successfully located and zero otherwise. Callers should take care
 * to initialize the cookie field of the smbtable structure to zero before
 * the first invocation of this function.
 * Multiple tables of the same type can be located by repeatedly calling
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
	 * successful location of a table, the type is encoded as bits 0:7 of
	 * the cookie value, the offset in terms of the number of structures
	 * preceding that referenced by the handle is encoded in bits 15:31.
	 */
	if ((st->cookie & 0xfff) == type && st->cookie >> 16) {
		if ((u_int8_t *)st->hdr >= va && (u_int8_t *)st->hdr < end) {
			hdr = st->hdr;
			if (hdr->type == type) {
				va = (u_int8_t *)hdr + hdr->size;
				for (; va + 1 < end; va++)
					if (*va == 0 && *(va + 1) == 0)
						break;
				va+= 2;
				tcount = st->cookie >> 16;
			}
		}
	}
	for (; va + sizeof(struct smbtblhdr) < end && tcount <=
	    smbios_entry.count; tcount++) {
		hdr = (struct smbtblhdr *)va;
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
			if (*va == 0 && *(va + 1) == 0)
				break;
		va+=2;
	}
	return ret;
}

char *
smbios_get_string(struct smbtable *st, u_int8_t indx, char *dest, size_t len)
{
	u_int8_t *va, *end;
	char *ret = NULL;
	int i;

	va = (u_int8_t *)st->hdr + st->hdr->size;
	end = smbios_entry.addr + smbios_entry.len;
	for (i = 1; va < end && i < indx && *va; i++)
		while (*va++)
			;
	if (i == indx) {
		if (va + len < end) {
			ret = dest;
			memcpy(ret, va, len);
			ret[len-1] = '\0';
		}
	}

	return ret;
}

char *
fixstring(char *s)
{
	char *p, *e;
	int i;

	for (i = 0; i < nitems(smbios_uninfo); i++)
		if ((strncasecmp(s, smbios_uninfo[i],
		    strlen(smbios_uninfo[i])))==0)
			return NULL;
	/*
	 * Remove leading and trailing whitespace
	 */
	for (p = s; *p == ' '; p++)
		;
	/*
	 * Special case entire string is whitespace
	 */
	if (p == s + strlen(s))
		return NULL;
	for (e = s + strlen(s) - 1; e > s && *e == ' '; e--)
		;
	if (p > s || e < s + strlen(s) - 1) {
		memmove(s, p, e - p + 1);
		s[e - p + 1] = '\0';
	}

	return s;
}

void
smbios_info(char * str)
{
	char *sminfop, sminfo[64];
	struct smbtable stbl, btbl;
	struct smbios_sys *sys;
	struct smbios_board *board;
	int i, infolen, uuidf, havebb;
	char *p;

	if (smbios_entry.mjr < 2)
		return;
	/*
	 * According to the spec the system table among others is required,
	 * if it is not we do not bother with this smbios implementation.
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
	 * perhaps naive belief that motherboard vendors will supply this
	 * information.
	 */
	sminfop = NULL;
	if ((p = smbios_get_string(&stbl, sys->vendor, sminfo,
	    sizeof(sminfo))) != NULL)
		sminfop = fixstring(p);
	if (sminfop == NULL) {
		if (havebb) {
			if ((p = smbios_get_string(&btbl, board->vendor,
			    sminfo, sizeof(sminfo))) != NULL)
				sminfop = fixstring(p);
		}
	}
	if (sminfop) {
		infolen = strlen(sminfop) + 1;
		hw_vendor = malloc(infolen, M_DEVBUF, M_NOWAIT);
		if (hw_vendor)
			strlcpy(hw_vendor, sminfop, infolen);
		sminfop = NULL;
	}
	if ((p = smbios_get_string(&stbl, sys->product, sminfo,
	    sizeof(sminfo))) != NULL)
		sminfop = fixstring(p);
	if (sminfop == NULL) {
		if (havebb) {
			if ((p = smbios_get_string(&btbl, board->product,
			    sminfo, sizeof(sminfo))) != NULL)
				sminfop = fixstring(p);
		}
	}
	if (sminfop) {
		infolen = strlen(sminfop) + 1;
		hw_prod = malloc(infolen, M_DEVBUF, M_NOWAIT);
		if (hw_prod)
			strlcpy(hw_prod, sminfop, infolen);
		sminfop = NULL;
	}
	if (hw_vendor != NULL && hw_prod != NULL)
		printf("\n%s: %s %s", str, hw_vendor, hw_prod);
	if ((p = smbios_get_string(&stbl, sys->version, sminfo,
	    sizeof(sminfo))) != NULL)
		sminfop = fixstring(p);
	if (sminfop) {
		infolen = strlen(sminfop) + 1;
		hw_ver = malloc(infolen, M_DEVBUF, M_NOWAIT);
		if (hw_ver)
			strlcpy(hw_ver, sminfop, infolen);
		sminfop = NULL;
	}
	if ((p = smbios_get_string(&stbl, sys->serial, sminfo,
	    sizeof(sminfo))) != NULL)
		sminfop = fixstring(p);
	if (sminfop) {
		infolen = strlen(sminfop) + 1;
		for (i = 0; i < infolen - 1; i++)
			add_timer_randomness(sminfop[i]);
		hw_serial = malloc(infolen, M_DEVBUF, M_NOWAIT);
		if (hw_serial)
			strlcpy(hw_serial, sminfop, infolen);
	}
	if (smbios_entry.mjr > 2 || (smbios_entry.mjr == 2 &&
	    smbios_entry.min >= 1)) {
		/*
		 * If the uuid value is all 0xff the uuid is present but not
		 * set, if its all 0 then the uuid isn't present at all.
		 */
		uuidf = SMBIOS_UUID_NPRESENT|SMBIOS_UUID_NSET;
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
			for (i = 0; i < sizeof(sys->uuid); i++)
				add_timer_randomness(sys->uuid[i]);
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

int
bios_print(void *aux, const char *pnp)
{
        struct bios_attach_args *ba = aux;

        if (pnp)
                printf("%s at %s",
                    ba->ba_name, pnp);
        return (UNCONF);
}
