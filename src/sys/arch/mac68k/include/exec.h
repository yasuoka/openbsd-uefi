/*	$OpenBSD: exec.h,v 1.5 1996/12/23 02:42:30 deraadt Exp $	*/
/*	$NetBSD: exec.h,v 1.8 1996/05/05 06:17:40 briggs Exp $	*/

/*-
 * Copyright (C) 1993	Allen K. Briggs, Chris P. Caputo,
 *			Michael L. Finch, Bradley A. Grantham, and
 *			Lawrence A. Kesteloot
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
 *	This product includes software developed by the Alice Group.
 * 4. The names of the Alice Group or any of its members may not be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE ALICE GROUP ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE ALICE GROUP BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#ifndef _EXEC_MACHINE_
#define _EXEC_MACHINE_

#define __LDPGSZ	8192

/* #define cpu_exec_makecmds(p,epp)	ENOEXEC */

/* Relocation format. */
struct relocation_info_mac68k {
	int r_address;			/* offset in text or data segment */
	unsigned int r_symbolnum : 24,	/* ordinal number of add symbol */
			 r_pcrel :  1,	/* 1 if value should be pc-relative */
			r_length :  2,	/* log base 2 of value's width */
			r_extern :  1,	/* 1 if need to add symbol to value */
		       r_baserel :  1,	/* linkage table relative */
		      r_jmptable :  1,	/* relocate to jump table */
		      r_relative :  1,	/* load address relative */
			  r_copy :  1;	/* run time copy */
};
#define relocation_info	relocation_info_mac68k

#define ELF_TARG_CLASS		ELFCLASS32
#define ELF_TARG_DATA		ELFDATA2MSB
#define ELF_TARG_MACH		EM_68K

#define _NLIST_DO_AOUT
#define _NLIST_DO_ELF

#define _KERN_DO_AOUT
#if defined(COMPAT_LINUX) || defined(COMPAT_SVR4)
#define _KERN_DO_ELF
#endif

#endif /* _EXEC_MACHINE_ */
