/*	$NetBSD: sbi.c,v 1.9 1996/04/08 18:32:55 ragge Exp $ */
/*
 * Copyright (c) 1994 Ludd, University of Lule}, Sweden.
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
 *     This product includes software developed at Ludd, University of Lule}.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/device.h>
#include <sys/systm.h>
#include <vm/vm.h>
#include <vm/vm_kern.h>
#include <vm/vm_page.h>
#include <machine/ka750.h>
#include <machine/pmap.h>
#include <machine/sid.h>
#include <machine/cpu.h>

struct nexus *nexus;

static	int sbi_print __P((void *, char *));
	int sbi_match __P((struct device *, void *, void *));
	void sbi_attach __P((struct device *, struct device *, void*));


struct bp_conf {
	char *type;
	int num;
	int partyp;
};

int
sbi_print(aux, name)
	void *aux;
	char *name;
{
	struct sbi_attach_args *sa = (struct sbi_attach_args *)aux;
	int unsupp = 0;
	extern int nmba;

	if (name) {
		switch (sa->type) {
		case NEX_MBA:
			printf("mba%d at %s",nmba++, name);
			break;
		default:
			printf("unknown device 0x%x at %s", sa->type, name);
			unsupp++;
		}		
	}
	printf(" tr%d", sa->nexnum);
	return (unsupp ? UNSUPP : UNCONF);
}

int
sbi_match(parent, cf, aux)
	struct  device  *parent;
	void    *cf, *aux;
{
	struct bp_conf *bp = aux;

	if (strcmp(bp->type, "sbi"))
		return 0;
	return 1;
}

void
sbi_attach(parent, self, aux)
	struct  device  *parent, *self;
	void    *aux;
{
	u_int 	nexnum, maxnex, minnex;
	struct	sbi_attach_args sa;

	switch (cpunumber) {
#ifdef VAX730
	case VAX_730:
		maxnex = NNEX730;
		printf(": BL[730\n");
		break;
#endif
#ifdef VAX750
	case VAX_750:
		maxnex = NNEX750;
		printf(": CMI750\n");
		break;
#endif
#ifdef VAX630
	case VAX_78032:
		switch (cpu_type) {
		case VAX_630:
			maxnex = NNEX630;
			printf(": Q22\n");
			break;
		default:
			panic("Microvax not supported");
		};
		break;
#endif
#ifdef VAX650
	case VAX_650:
		maxnex = NNEX630; /* XXX */
		printf(": Q22\n");
		break;
#endif
#if VAX780 || VAX8600
	case VAX_780:
	case VAX_8600:
		maxnex = NNEXSBI;
		printf(": SBI780\n");
		break;
#endif
	default:
		maxnex = 0; /* Leave it */
		break;
	}

	/*
	 * Now a problem: on different machines with SBI units identifies
	 * in different ways (if they identifies themselves at all).
	 * We have to fake identifying depending on different CPUs.
	 */
	minnex = self->dv_unit * maxnex;
	for (nexnum = minnex; nexnum < minnex + maxnex; nexnum++) {
		volatile int tmp;

		if (badaddr((caddr_t)&nexus[nexnum], 4))
			continue;

		switch (cpunumber) {
#ifdef VAX750
		case VAX_750:
		{	extern	int nexty750[];
			sa.type = nexty750[nexnum];
			break;
		}
#endif
#ifdef VAX730
		case VAX_730:
		{	extern	int nexty730[];
			sa.type = nexty730[nexnum];
			break;
		}
#endif
#if VAX630 || VAX650
		case VAX_78032:
		case VAX_650:
			sa.type = NEX_UBA0;
			break;
#endif
		default:
			tmp = nexus[nexnum].nexcsr.nex_csr; /* no byte reads */
			sa.type = tmp & 255;
		}
		sa.nexnum = nexnum;
		sa.nexaddr = nexus + nexnum;
		config_found(self, (void*)&sa, sbi_print);
	}
}

struct  cfdriver sbi_cd = {
	NULL, "sbi", DV_DULL
};

struct	cfattach sbi_ca = {
	sizeof(struct device), sbi_match, sbi_attach
};
	
