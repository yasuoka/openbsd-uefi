/*	$OpenBSD: cpu.c,v 1.2 2004/01/15 05:19:46 drahn Exp $ */

/*
 * Copyright (c) 1997 Per Fogelstrom
 * Copyright (c) 1997 RTMX Inc
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
 *	North Carolina, USA, by Per Fogelstrom, Opsycon AB, Sweden.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/device.h>

#include <dev/ofw/openfirm.h>

#include <machine/autoconf.h>

#define MPC601          1
#define MPC603          3
#define MPC604          4
#define MPC603e         6
#define MPC603ev        7
#define MPC750          8
#define MPC604ev        9
#define MPC7400         12
#define	IBM750FX	0x7000
#define MPC7410         0x800c
#define MPC7450         0x8000
#define MPC7455         0x8001

/* only valid on 603(e,ev) and G3, G4 */
#define HID0_DOZE	(1 << (31-8))
#define HID0_NAP	(1 << (31-9))
#define HID0_SLEEP	(1 << (31-10))
#define HID0_DPM	(1 << (31-11))
#define HID0_SGE	(1 << (31-24))
#define HID0_BTIC	(1 << (31-26))
#define HID0_LRSTK	(1 << (31-27))
#define HID0_FOLD	(1 << (31-28))
#define HID0_BHT	(1 << (31-29))

char cpu_model[80];
char machine[] = MACHINE;	/* cpu architecture */

/* Definition of the driver for autoconfig. */
int	cpumatch(struct device *, void *, void *);
void	cpuattach(struct device *, struct device *, void *);

struct cfattach cpu_ca = {
	sizeof(struct device), cpumatch, cpuattach
};

struct cfdriver cpu_cd = {
	NULL, "cpu", DV_DULL, NULL, 0
};

void config_l2cr(int cpu);

int
cpumatch(parent, cfdata, aux)
	struct device *parent;
	void *cfdata;
	void *aux;
{
	struct confargs *ca = aux;

	/* make sure that we're looking for a CPU. */
	if (strcmp(ca->ca_name, cpu_cd.cd_name) != 0)
		return (0);

	return (1);
}

extern int (*cpu_cpuspeed)(void *, size_t *, void *, size_t);
static u_int32_t ppc_curfreq;


int
ppc_cpuspeed(void *oldp, size_t *oldlenp, void *newp, size_t newlen)
{
	return (sysctl_rdint(oldp, oldlenp, newp, ppc_curfreq));
}


void
cpuattach(struct device *parent, struct device *dev, void *aux)
{
	unsigned int cpu, pvr, hid0;
	char name[32];
	int qhandle, phandle;
	unsigned int clock_freq = 0;

	pvr = ppc_mfpvr();
	cpu = pvr >> 16;
	switch (cpu) {
	case MPC601:
		snprintf(cpu_model, sizeof(cpu_model), "601");
		break;
	case MPC603:
		snprintf(cpu_model, sizeof(cpu_model), "603");
		break;
	case MPC604:
		snprintf(cpu_model, sizeof(cpu_model), "604");
		break;
	case MPC603e:
		snprintf(cpu_model, sizeof(cpu_model), "603e");
		break;
	case MPC603ev:
		snprintf(cpu_model, sizeof(cpu_model), "603ev");
		break;
	case MPC750:
		snprintf(cpu_model, sizeof(cpu_model), "750");
		break;
	case MPC604ev:
		snprintf(cpu_model, sizeof(cpu_model), "604ev");
		break;
	case MPC7400:
		snprintf(cpu_model, sizeof(cpu_model), "7400");
		break;
	case IBM750FX:
		snprintf(cpu_model, sizeof(cpu_model), "750FX");
		break;
	case MPC7410:
		snprintf(cpu_model, sizeof(cpu_model), "7410");
		break;
	case MPC7450:
		if ((pvr & 0xf) < 3)
			snprintf(cpu_model, sizeof(cpu_model), "7450");
		 else
			snprintf(cpu_model, sizeof(cpu_model), "7451");
		break;
	case MPC7455:
		snprintf(cpu_model, sizeof(cpu_model), "7455");
		break;
	default:
		snprintf(cpu_model, sizeof(cpu_model), "Version %x", cpu);
		break;
	}
	snprintf(cpu_model + strlen(cpu_model),
	    sizeof(cpu_model) - strlen(cpu_model),
	    " (Revision %x)", pvr & 0xffff);
	printf(": %s", cpu_model);

	/* This should only be executed on openfirmware systems... */

	for (qhandle = OF_peer(0); qhandle; qhandle = phandle) {
                if (OF_getprop(qhandle, "device_type", name, sizeof name) >= 0
                    && !strcmp(name, "cpu")
                    && OF_getprop(qhandle, "clock-frequency",
                        &clock_freq , sizeof clock_freq ) >= 0)
		{
			break;
		}
                if ((phandle = OF_child(qhandle)))
                        continue;
                while (qhandle) {
                        if ((phandle = OF_peer(qhandle)))
                                break;
                        qhandle = OF_parent(qhandle);
                }
	}

	if (clock_freq != 0) {
		/* Openfirmware stores clock in Hz, not MHz */
		clock_freq /= 1000000;
		printf(": %d MHz", clock_freq);
		ppc_curfreq = clock_freq;
		cpu_cpuspeed = ppc_cpuspeed;
	}
	/* power savings mode */
	hid0 = ppc_mfhid0();
	switch (cpu) {
	case MPC603:
	case MPC603e:
	case MPC750:
	case MPC7400:
	case IBM750FX:
	case MPC7410:
		/* select DOZE mode */
		hid0 &= ~(HID0_NAP | HID0_SLEEP);
		hid0 |= HID0_DOZE | HID0_DPM;
		break;
	case MPC7450:
	case MPC7455:
		/* select NAP mode */
		hid0 &= ~(HID0_DOZE | HID0_SLEEP);
		hid0 |= HID0_NAP | HID0_DPM;
		/* try some other flags */
		hid0 |= HID0_SGE | HID0_BTIC;
		hid0 |= HID0_LRSTK | HID0_FOLD | HID0_BHT;
		/* Disable BTIC on 7450 Rev 2.0 or earlier */
		if (cpu == MPC7450 && (pvr & 0xffff) < 0x0200)
			hid0 &= ~HID0_BTIC;
		break;
	}
	ppc_mthid0(hid0);

	/* if processor is G3 or G4, configure l2 cache */
	if ( (cpu == MPC750) || (cpu == MPC7400) || (cpu == IBM750FX)
	    || (cpu == MPC7410) || (cpu == MPC7450) || (cpu == MPC7455)) {
		config_l2cr(cpu);
	}
	printf("\n");


}

/* L2CR bit definitions */
#define L2CR_L2E        0x80000000 /* 0: L2 enable */
#define L2CR_L2PE       0x40000000 /* 1: L2 data parity enable */
#define L2CR_L2SIZ      0x30000000 /* 2-3: L2 size */
#define  L2SIZ_RESERVED         0x00000000
#define  L2SIZ_256K             0x10000000
#define  L2SIZ_512K             0x20000000
#define  L2SIZ_1M       0x30000000
#define L2CR_L2CLK      0x0e000000 /* 4-6: L2 clock ratio */
#define  L2CLK_DIS              0x00000000 /* disable L2 clock */
#define  L2CLK_10               0x02000000 /* core clock / 1   */
#define  L2CLK_15               0x04000000 /*            / 1.5 */
#define  L2CLK_20               0x08000000 /*            / 2   */
#define  L2CLK_25               0x0a000000 /*            / 2.5 */
#define  L2CLK_30               0x0c000000 /*            / 3   */
#define L2CR_L2RAM      0x01800000 /* 7-8: L2 RAM type */
#define  L2RAM_FLOWTHRU_BURST   0x00000000
#define  L2RAM_PIPELINE_BURST   0x01000000
#define  L2RAM_PIPELINE_LATE    0x01800000
#define L2CR_L2DO       0x00400000 /* 9: L2 data-only.
                                      Setting this bit disables instruction
                                      caching. */
#define L2CR_L2I        0x00200000 /* 10: L2 global invalidate. */
#define L2CR_L2CTL      0x00100000 /* 11: L2 RAM control (ZZ enable).
                                      Enables automatic operation of the
                                      L2ZZ (low-power mode) signal. */
#define L2CR_L2WT       0x00080000 /* 12: L2 write-through. */
#define L2CR_L2TS       0x00040000 /* 13: L2 test support. */
#define L2CR_L2OH       0x00030000 /* 14-15: L2 output hold. */
#define L2CR_L2SL       0x00008000 /* 16: L2 DLL slow. */
#define L2CR_L2DF       0x00004000 /* 17: L2 differential clock. */
#define L2CR_L2BYP      0x00002000 /* 18: L2 DLL bypass. */
#define L2CR_L2IP       0x00000001 /* 31: L2 global invalidate in progress
				       (read only). */
#ifndef L2CR_CONFIG
#define L2CR_CONFIG L2CR_L2E|L2SIZ_512K
#endif

#ifdef L2CR_CONFIG
u_int l2cr_config = L2CR_CONFIG;
#else
u_int l2cr_config = 0;
#endif

/* L3CR bit definitions */
#define   L3CR_L3E                0x80000000 /*  0: L3 enable */
#define   L3CR_L3SIZ              0x10000000 /*  3: L3 size (0=1MB, 1=2MB) */

void
config_l2cr(int cpu)
{
	u_int l2cr, x;

	l2cr = ppc_mfl2cr();

	/*
	 * Configure L2 cache if not enabled.
	 */
	if ((l2cr & L2CR_L2E) == 0 && l2cr_config != 0) {
		l2cr = l2cr_config & ~L2CR_L2E;
		__asm __volatile ("sync");
		ppc_mtl2cr(l2cr);
		__asm __volatile ("sync");

		/* Wait for L2 clock to be stable (640 L2 clocks). */
		delay(100);

		/* Invalidate all L2 contents. */
		l2cr |= L2CR_L2I;
		ppc_mtl2cr(l2cr);
		do {
			x = ppc_mfl2cr();
		} while (x & L2CR_L2IP);

		/* Enable L2 cache. */
		l2cr &= ~L2CR_L2I;
		l2cr |= L2CR_L2E;
		ppc_mtl2cr(l2cr);
	}

	if (l2cr & L2CR_L2E) {
		if (cpu == MPC7450 || cpu == MPC7455) {
			u_int l3cr;

			printf(": 256KB L2 cache");

			l3cr = ppc_mfl3cr();
			if (l3cr & L3CR_L3E)
				printf(", %cMB L3 cache",
				    l3cr & L3CR_L3SIZ ? '2' : '1');
		} else if (cpu == IBM750FX)
			printf(": 512KB L2 cache");
		else {
			switch (l2cr & L2CR_L2SIZ) {
			case L2SIZ_256K:
				printf(": 256KB");
				break;
			case L2SIZ_512K:
				printf(": 512KB");
				break;
			case L2SIZ_1M:
				printf(": 1MB");
				break;
			default:
				printf(": unknown size");
			}
			printf(" backside cache");
		}
#if 0
		switch (l2cr & L2CR_L2RAM) {
		case L2RAM_FLOWTHRU_BURST:
			printf(" Flow-through synchronous burst SRAM");
			break;
		case L2RAM_PIPELINE_BURST:
			printf(" Pipelined synchronous burst SRAM");
			break;
		case L2RAM_PIPELINE_LATE:
			printf(" Pipelined synchronous late-write SRAM");
			break;
		default:
			printf(" unknown type");
		}

		if (l2cr & L2CR_L2PE)
			printf(" with parity");
#endif
	} else
		printf(": L2 cache not enabled");
}
