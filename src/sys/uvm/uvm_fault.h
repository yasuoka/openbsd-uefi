/*	$OpenBSD: uvm_fault.h,v 1.2 1999/02/26 05:32:06 art Exp $	*/
/*	$NetBSD: uvm_fault.h,v 1.7 1998/10/11 23:07:42 chuck Exp $	*/

/*
 * XXXCDC: "ROUGH DRAFT" QUALITY UVM PRE-RELEASE FILE!   
 *	   >>>USE AT YOUR OWN RISK, WORK IS NOT FINISHED<<<
 */
/*
 *
 * Copyright (c) 1997 Charles D. Cranor and Washington University.
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
 *      This product includes software developed by Charles D. Cranor and
 *      Washington University.
 * 4. The name of the author may not be used to endorse or promote products
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
 * from: Id: uvm_fault.h,v 1.1.2.2 1997/12/08 16:07:12 chuck Exp
 */

#ifndef _UVM_UVM_FAULT_H_
#define _UVM_UVM_FAULT_H_

/*
 * fault types
 */

#define VM_FAULT_INVALID ((vm_fault_t) 0x0)	/* invalid mapping */
#define VM_FAULT_PROTECT ((vm_fault_t) 0x1)	/* protection */
#define VM_FAULT_WIRE	 ((vm_fault_t) 0x2)	/* wire mapping */

/*
 * fault data structures
 */

/*
 * uvm_faultinfo: to load one of these fill in all orig_* fields and
 * then call uvmfault_lookup on it.
 */


struct uvm_faultinfo {
	vm_map_t orig_map;		/* IN: original map */
	vaddr_t orig_rvaddr;		/* IN: original rounded VA */
	vsize_t orig_size;		/* IN: original size of interest */
	vm_map_t map;			/* map (could be a submap) */
	unsigned int mapv;		/* map's version number */
	vm_map_entry_t entry;		/* map entry (from 'map') */
	vsize_t size;			/* size of interest */
};

/*
 * fault prototypes
 */


int uvmfault_anonget __P((struct uvm_faultinfo *, struct vm_amap *,
													struct vm_anon *));
static boolean_t uvmfault_lookup __P((struct uvm_faultinfo *, boolean_t));
static boolean_t uvmfault_relock __P((struct uvm_faultinfo *));
static void uvmfault_unlockall __P((struct uvm_faultinfo *, struct vm_amap *,
			            struct uvm_object *, struct vm_anon *));
static void uvmfault_unlockmaps __P((struct uvm_faultinfo *, boolean_t));

int uvm_fault_wire __P((vm_map_t, vaddr_t, vaddr_t));
void uvm_fault_unwire __P((struct pmap *, vaddr_t, vaddr_t));

#endif /* _UVM_UVM_FAULT_H_ */
