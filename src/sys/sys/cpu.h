/*	$OpenBSD: cpu.h,v 1.5 1996/04/21 22:31:35 deraadt Exp $	*/
/*	$NetBSD: cpu.h,v 1.5 1996/03/16 23:12:11 christos Exp $	*/

/*
 * Copyright (c) 1996 Christos Zoulas.  All rights reserved.
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
 *	This product includes software developed by Christos Zoulas.
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
 */

#ifdef _KERNEL
struct proc;
struct vnode;
struct code;
struct ucred;
struct core;
struct buf;
struct disklabel;
struct device;
struct disk;
__BEGIN_DECLS

void	consinit __P((void));
void	boot __P((int))
    __attribute__((__noreturn__));
void	pagemove __P((caddr_t, caddr_t, size_t));
/* delay() is declared in <machine/param.h> */
int	bounds_check_with_label __P((struct buf *, struct disklabel *, int));
int	dk_establish __P((struct disk *, struct device *));


void	cpu_exit __P((struct proc *));
void	cpu_startup __P((void));
void	cpu_initclocks __P((void));
void	cpu_switch __P((struct proc *));
int	cpu_coredump __P((struct proc *, struct vnode *, struct ucred *,
			  struct core *));
__END_DECLS

#endif /* _KERNEL */
