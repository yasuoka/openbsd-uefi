/*	$OpenBSD: exec.h,v 1.3 1996/12/28 06:25:06 rahnds Exp $	*/
/*
 * Copyright (c) 1993 Christopher G. Demetriou
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
 *
 *	$Id: exec.h,v 1.3 1996/12/28 06:25:06 rahnds Exp $
 */

#ifndef _PPC_EXEC_H_
#define _PPC_EXEC_H_

#define __LDPGSZ	4096	/* linker page size */

enum reloc_type {
  reloc_type_rubbish
};

/* Relocation format (from PMAX?) */
struct relocation_info_powerpc {
  int utter_rubbish;
};
#define relocation_info	relocation_info_ppc

/*
 *  Define what exec "formats" we should handle.
 */
#define NATIVE_EXEC_ELF

#define ELF_TARG_CLASS          ELFCLASS32
#define ELF_TARG_DATA           ELFDATA2MSB
#define ELF_TARG_MACH           EM_PPC

#define _NLIST_DO_AOUT
#define _NLIST_DO_ELF

#define _KERN_DO_ELF

#endif  /* _PPC_EXEC_H_ */
