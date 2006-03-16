/*	$OpenBSD: powernow-k8.c,v 1.4 2006/03/16 02:39:57 dlg Exp $ */
/*
 * Copyright (c) 2004 Martin V�giard.
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
 * 3. The name of the author may not be used to endorse or promote products
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
 *
 * Copyright (c) 2004-2005 Bruno Ducrot
 * Copyright (c) 2004 FUKUDA Nobuhiko <nfukuda@spa.is.uec.ac.jp>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
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
/* AMD POWERNOW K8 driver */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>

#include <dev/isa/isareg.h>
#include <amd64/include/isa_machdep.h>

#include <machine/cpu.h>
#include <machine/cpufunc.h>
#include <machine/bus.h>

#define BIOS_START		0xe0000
#define	BIOS_LEN		0x20000

extern int cpuspeed;

/*
 * MSRs and bits used by Powernow technology
 */
#define MSR_AMDK7_FIDVID_CTL		0xc0010041
#define MSR_AMDK7_FIDVID_STATUS		0xc0010042

/* Bitfields used by K8 */

#define PN8_CTR_FID(x)			((x) & 0x3f)
#define PN8_CTR_VID(x)			(((x) & 0x1f) << 8)
#define PN8_CTR_PENDING(x)		(((x) & 1) << 32)

#define PN8_STA_CFID(x)			((x) & 0x3f)
#define PN8_STA_SFID(x)			(((x) >> 8) & 0x3f)
#define PN8_STA_MFID(x)			(((x) >> 16) & 0x3f)
#define PN8_STA_PENDING(x)		(((x) >> 31) & 0x01)
#define PN8_STA_CVID(x)			(((x) >> 32) & 0x1f)
#define PN8_STA_SVID(x)			(((x) >> 40) & 0x1f)
#define PN8_STA_MVID(x)			(((x) >> 48) & 0x1f)

/* Reserved1 to powernow k8 configuration */
#define PN8_PSB_TO_RVO(x)		((x) & 0x03)
#define PN8_PSB_TO_IRT(x)		(((x) >> 2) & 0x03)
#define PN8_PSB_TO_MVS(x)		(((x) >> 4) & 0x03)
#define PN8_PSB_TO_BATT(x)		(((x) >> 6) & 0x03)

/* ACPI ctr_val status register to powernow k8 configuration */
#define ACPI_PN8_CTRL_TO_FID(x)		((x) & 0x3f)
#define ACPI_PN8_CTRL_TO_VID(x)		(((x) >> 6) & 0x1f)
#define ACPI_PN8_CTRL_TO_VST(x)		(((x) >> 11) & 0x1f)
#define ACPI_PN8_CTRL_TO_MVS(x)		(((x) >> 18) & 0x03)
#define ACPI_PN8_CTRL_TO_PLL(x)		(((x) >> 20) & 0x7f)
#define ACPI_PN8_CTRL_TO_RVO(x)		(((x) >> 28) & 0x03)
#define ACPI_PN8_CTRL_TO_IRT(x)		(((x) >> 30) & 0x03)

#define WRITE_FIDVID(fid, vid, ctrl)	\
	wrmsr(MSR_AMDK7_FIDVID_CTL,	\
	    (((ctrl) << 32) | (1ULL << 16) | ((vid) << 8) | (fid)))


#define COUNT_OFF_IRT(irt)	DELAY(10 * (1 << (irt)))
#define COUNT_OFF_VST(vst)	DELAY(20 * (vst))

#define FID_TO_VCO_FID(fid)	\
	(((fid) < 8) ? (8 + ((fid) << 1)) : (fid))

#define POWERNOW_MAX_STATES		16

struct k8pnow_state {
	int freq;
	uint8_t fid;
	uint8_t vid;
};

struct k8pnow_cpu_state {
	struct k8pnow_state state_table[POWERNOW_MAX_STATES];
	unsigned int n_states;
	unsigned int sgtc;
	unsigned int vst;
	unsigned int mvs;
	unsigned int pll;
	unsigned int rvo;
	unsigned int irt;
	int low;
};

struct psb_s {
	char signature[10];     /* AMDK7PNOW! */
	uint8_t version;
	uint8_t flags;
	uint16_t ttime;         /* Min Settling time */
	uint8_t reserved;
	uint8_t n_pst;
};

struct pst_s {
	uint32_t cpuid;
	uint8_t pll;
	uint8_t fid;
	uint8_t vid;
	uint8_t n_states;
};

struct k8pnow_cpu_state *k8pnow_current_state;

/*
 * Prototypes
 */
int k8pnow_read_pending_wait(uint64_t *);
int k8pnow_decode_pst(struct k8pnow_cpu_state *, uint8_t *);
int k8pnow_states(struct k8pnow_cpu_state *, uint32_t, unsigned int,
    unsigned int);

int
k8pnow_read_pending_wait(uint64_t *status)
{
	unsigned int i = 1000;

	while (i--) {
		*status = rdmsr(MSR_AMDK7_FIDVID_STATUS);
		if (!PN8_STA_PENDING(*status))
			return 0;

	}
	printf("k8pnow_read_pending_wait: change pending stuck.\n");
	return 1;
}

int
k8_powernow_setperf(int level)
{
	unsigned int i, low, high, freq;
	uint64_t status;
	int cfid, cvid, fid = 0, vid = 0;
	int rvo;
	u_int val;
	struct k8pnow_cpu_state *cstate;

	/*
	 * We dont do a k8pnow_read_pending_wait here, need to ensure that the
	 * change pending bit isn't stuck,
	 */
	status = rdmsr(MSR_AMDK7_FIDVID_STATUS);
	if (PN8_STA_PENDING(status))
		return 1;
	cfid = PN8_STA_CFID(status);
	cvid = PN8_STA_CVID(status);

	cstate = k8pnow_current_state;
	low = cstate->state_table[0].freq;
	high = cstate->state_table[cstate->n_states-1].freq;

	freq = low + (high - low) * level / 100;

	for (i = 0; i < cstate->n_states; i++) {
		if (cstate->state_table[i].freq >= freq) {
			fid = cstate->state_table[i].fid;
			vid = cstate->state_table[i].vid;
			break;
		}
	}

	if (fid == cfid && vid == cvid)
		return (0);

	/*
	 * Phase 1: Raise core voltage to requested VID if frequency is
	 * going up.
	 */
	while (cvid > vid) {
		val = cvid - (1 << cstate->mvs);
		WRITE_FIDVID(cfid, (val > 0) ? val : 0, 1ULL);
		if (k8pnow_read_pending_wait(&status))
			return 1;
		cvid = PN8_STA_CVID(status);
		COUNT_OFF_VST(cstate->vst);
	}

	/* ... then raise to voltage + RVO (if required) */
	for (rvo = cstate->rvo; rvo > 0 && cvid > 0; --rvo) {
		/* XXX It's not clear from spec if we have to do that
		 * in 0.25 step or in MVS.  Therefore do it as it's done
		 * under Linux */
		WRITE_FIDVID(cfid, cvid - 1, 1ULL);
		if (k8pnow_read_pending_wait(&status))
			return 1;
		cvid = PN8_STA_CVID(status);
		COUNT_OFF_VST(cstate->vst);
	}

	/* Phase 2: change to requested core frequency */
	if (cfid != fid) {
		u_int vco_fid, vco_cfid;

		vco_fid = FID_TO_VCO_FID(fid);
		vco_cfid = FID_TO_VCO_FID(cfid);

		while (abs(vco_fid - vco_cfid) > 2) {
			if (fid > cfid) {
				if (cfid > 6)
					val = cfid + 2;
				else
					val = FID_TO_VCO_FID(cfid) + 2;
			} else
				val = cfid - 2;
			WRITE_FIDVID(val, cvid, (uint64_t)cstate->pll * 1000 / 5);

			if (k8pnow_read_pending_wait(&status))
				return 1;
			cfid = PN8_STA_CFID(status);
			COUNT_OFF_IRT(cstate->irt);

			vco_cfid = FID_TO_VCO_FID(cfid);
		}

		WRITE_FIDVID(fid, cvid, (uint64_t) cstate->pll * 1000 / 5);
		if (k8pnow_read_pending_wait(&status))
			return 1;
		cfid = PN8_STA_CFID(status);
		COUNT_OFF_IRT(cstate->irt);
	}

	/* Phase 3: change to requested voltage */
	if (cvid != vid) {
		WRITE_FIDVID(cfid, vid, 1ULL);
		if (k8pnow_read_pending_wait(&status))
			return 1;
		cvid = PN8_STA_CVID(status);
		COUNT_OFF_VST(cstate->vst);
	}

	/* Check if transition failed. */
	if (cfid != fid || cvid != vid)
		return (1);

	cpuspeed = cstate->state_table[i].freq;
	return (0);
}

/*
 * Given a set of pair of fid/vid, and number of performance states,
 * compute state_table via an insertion sort.
 */
int
k8pnow_decode_pst(struct k8pnow_cpu_state *cstate, uint8_t *p)
{
	int i, j, n;
	struct k8pnow_state state;
	for (n = 0, i = 0; i < cstate->n_states; i++) {
		state.fid = *p++;
		state.vid = *p++;
	
		/*
		 * The minimum supported frequency per the data sheet is 800MHz
		 * The maximum supported frequency is 5000MHz.
		 */
		state.freq = 800 + state.fid * 100;
		j = n;
		while (j > 0 && cstate->state_table[j - 1].freq > state.freq) {
			memcpy(&cstate->state_table[j],
			    &cstate->state_table[j - 1],
			    sizeof(struct k8pnow_state));
			--j;
		}
		memcpy(&cstate->state_table[j], &state,
		    sizeof(struct k8pnow_state));
		n++;
	}
	return 1;
}

int
k8pnow_states(struct k8pnow_cpu_state *cstate, uint32_t cpusig,
    unsigned int fid, unsigned int vid)
{
	struct psb_s *psb;
	struct pst_s *pst;
	uint8_t *p;
	int i;

	for (p = (u_int8_t *)ISA_HOLE_VADDR(BIOS_START);
	    p < (u_int8_t *)ISA_HOLE_VADDR(BIOS_START + BIOS_LEN); p += 16) {
		if (memcmp(p, "AMDK7PNOW!", 10) == 0) {
			psb = (struct psb_s *)p;
			if (psb->version != 0x14)
				return 0;

			cstate->vst = psb->ttime;
			cstate->rvo = PN8_PSB_TO_RVO(psb->reserved);
			cstate->irt = PN8_PSB_TO_IRT(psb->reserved);
			cstate->mvs = PN8_PSB_TO_MVS(psb->reserved);
			cstate->low = PN8_PSB_TO_BATT(psb->reserved);
			p+= sizeof(struct psb_s);

			for(i = 0; i < psb->n_pst; ++i) {
				pst = (struct pst_s *) p;

				cstate->pll = pst->pll;
				cstate->n_states = pst->n_states;
				if (cpusig == pst->cpuid &&
				    pst->fid == fid && pst->vid == vid) {
					return (k8pnow_decode_pst(cstate,
					    p+= sizeof (struct pst_s)));
				}
				p += sizeof(struct pst_s) + 2 * cstate->n_states;
			}
		}
	}

	return 0;

}

void
k8_powernow_init(void)
{
	uint64_t status;
	u_int maxfid, maxvid, i;
	struct k8pnow_cpu_state *cstate;
	struct k8pnow_state *state;
	struct cpu_info * ci;
	char * techname = NULL;
	ci = curcpu();

	cstate = malloc(sizeof(struct k8pnow_cpu_state), M_DEVBUF, M_NOWAIT);
	if (!cstate)
		return;

	status = rdmsr(MSR_AMDK7_FIDVID_STATUS);
	maxfid = PN8_STA_MFID(status);
	maxvid = PN8_STA_MVID(status);

	/*
	* If start FID is different to max FID, then it is a
	* mobile processor.  If not, it is a low powered desktop
	* processor.
	*/
	if (PN8_STA_SFID(status) != PN8_STA_MFID(status))
		techname = "PowerNow! K8";
	else
		techname = "Cool`n'Quiet K8";

	if (k8pnow_states(cstate, ci->ci_signature, maxfid, maxvid)) {
		if (cstate->n_states) {
			printf("%s: %s %d Mhz: speeds:",
			    ci->ci_dev->dv_xname, techname, cpuspeed);
			for(i = cstate->n_states; i > 0; i--) {
				state = &cstate->state_table[i-1];
				printf(" %d", state->freq);
			}
			printf(" Mhz\n");
			k8pnow_current_state = cstate;
			cpu_setperf = k8_powernow_setperf;
			return;
		}
	}
	free(cstate, M_DEVBUF);
}
