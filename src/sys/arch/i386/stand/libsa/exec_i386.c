/*	$OpenBSD: exec_i386.c,v 1.28 2003/06/03 20:22:11 mickey Exp $	*/

/*
 * Copyright (c) 1997-1998 Michael Shalayeff
 * Copyright (c) 1997 Tobias Weingartner
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR 
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/param.h>
#include <dev/cons.h>
#include <stand/boot/bootarg.h>
#include <machine/biosvar.h>
#include <sys/disklabel.h>
#include "disk.h"
#include "libsa.h"
#include <lib/libsa/loadfile.h>

typedef void (*startfuncp)(int, int, int, int, int, int, int, int)
	__attribute__ ((noreturn));

void
run_loadfile(u_long *marks, int howto)
{
	u_long entry;
#ifndef _TEST
#ifdef EXEC_DEBUG
	extern int debug;
#endif
	dev_t bootdev = bootdev_dip->bootdev;
	size_t ac = BOOTARG_LEN;
	caddr_t av = (caddr_t)BOOTARG_OFF;
	bios_consdev_t cd;
	extern int com_speed; /* from bioscons.c */

	cd.consdev = cn_tab->cn_dev;
	cd.conspeed = com_speed;
	addbootarg(BOOTARG_CONSDEV, sizeof(cd), &cd);

	/* Pass memory map to the kernel */
	mem_pass();

	makebootargs(av, &ac);

	entry = marks[MARK_ENTRY] & 0x0fffffff;

	printf("entry point at 0x%x\n", (int) entry);
	/* stack and the gung is ok at this point, so, no need for asm setup */
	(*(startfuncp)entry)(howto, bootdev, BOOTARG_APIVER,
		marks[MARK_END], extmem, cnvmem, ac, (int)av);
	/* not reached */
#endif
}
