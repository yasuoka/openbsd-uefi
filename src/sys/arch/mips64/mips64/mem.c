/*	$OpenBSD: mem.c,v 1.5 2004/09/09 22:11:38 pefo Exp $	*/
/*	$NetBSD: mem.c,v 1.6 1995/04/10 11:55:03 mycroft Exp $	*/

/*
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1982, 1986, 1990, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * the Systems Programming Group of the University of Utah Computer
 * Science Department and Ralph Campbell.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)mem.c	8.3 (Berkeley) 1/12/94
 */

/*
 * Memory special file
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/buf.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/msgbuf.h>
#include <sys/systm.h>
#include <sys/uio.h>
#include <sys/malloc.h>

#include <machine/autoconf.h>
#include <machine/pte.h>
#include <machine/cpu.h>

#include <uvm/uvm_extern.h>

#ifdef APERTURE
static int ap_open_count = 0;
extern int allowaperture;
#endif
caddr_t zeropage;

#define mmread  mmrw
#define mmwrite mmrw
cdev_decl(mm);

/*ARGSUSED*/
int
mmopen(dev_t dev, int flag, int mode, struct proc *p)
{

	switch (minor(dev)) {
	case 0:
	case 1:
	case 2:
	case 12:
		return (0);
#ifdef APERTURE
	case 4:
		if (suser(p->p_ucred, &p->p_acflag) != 0 || !allowaperture)
			return (EPERM);

		/* authorize only one simultaneous open() */
		if (ap_open_count > 0)
			return(EPERM);
		ap_open_count++;
		return (0);
#endif
	default:
		return (ENXIO);
	}
}

/*ARGSUSED*/
int
mmclose(dev_t dev, int flag, int mode, struct proc *p)
{
#ifdef APERTURE
	if (minor(dev) == 4)
		ap_open_count--;
#endif
	return (0);
}

/*ARGSUSED*/
int
mmrw(dev_t dev, struct uio *uio, int flags)
{
	struct iovec *iov;
	int error = 0, c;
	vaddr_t v;

	while (uio->uio_resid > 0 && error == 0) {
		iov = uio->uio_iov;
		if (iov->iov_len == 0) {
			uio->uio_iov++;
			uio->uio_iovcnt--;
			if (uio->uio_iovcnt < 0)
				panic("mmrw");
			continue;
		}
		switch (minor(dev)) {

/* minor device 0 is physical memory */
		case 0:
			v = uio->uio_offset;
			c = iov->iov_len;
			if (v + c > ctob(physmem))
				return (EFAULT);
			v = (vaddr_t)PHYS_TO_KSEG0(v);
			error = uiomove((caddr_t)v, c, uio);
			continue;

/* minor device 1 is kernel memory */
		case 1:
			v = uio->uio_offset;
			c = min(iov->iov_len, MAXPHYS);
			if ((v > KSEG0_BASE && v + c <= KSEG0_BASE + ctob(physmem)) ||
			    uvm_kernacc((caddr_t)v, c,
			    uio->uio_rw == UIO_READ ? B_READ : B_WRITE)) {
				error = uiomove((caddr_t)v, c, uio);
				continue;
			} else {
				return (EFAULT);
			}

/* minor device 2 is EOF/RATHOLE */
		case 2:
			if (uio->uio_rw == UIO_WRITE)
				uio->uio_resid = 0;
			return (0);

/* minor device 12 (/dev/zero) is source of nulls on read, rathole on write */
		case 12:
			if (uio->uio_rw == UIO_WRITE) {
				c = iov->iov_len;
				break;
			}
			if (zeropage == NULL) {
				zeropage = (caddr_t)
				    malloc(PAGE_SIZE, M_TEMP, M_WAITOK);
				bzero(zeropage, PAGE_SIZE);
			}
			c = min(iov->iov_len, PAGE_SIZE);
			error = uiomove(zeropage, c, uio);
			continue;

		default:
			return (ENODEV);
		}
		if (error)
			break;
		iov->iov_base += c;
		iov->iov_len -= c;
		uio->uio_offset += c;
		uio->uio_resid -= c;
	}
	return error;
}

/*ARGSUSED*/
paddr_t
mmmmap(dev_t dev, off_t off, int prot)
{
#ifdef APERTURE
	if (minor(dev) == 4) {
		if (off >= 0x0000 && off < 0x10000) {
			off += sys_config.pci_io[0].bus_base;
			return mips_btop(off);
		} else if (off >= 0xa0000 && off < 0x10000000) {
			off += sys_config.pci_mem[0].bus_base;
			return mips_btop(off);
		} else {
			return -1;
		}
	}
#endif
	return -1;
}

int
mmioctl(dev_t dev, u_long cmd, caddr_t data, int flags, struct proc *p)
{
	return (EOPNOTSUPP);
}
