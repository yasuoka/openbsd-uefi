/*	$OpenBSD: conf.c,v 1.5 1998/09/29 07:14:53 mickey Exp $	*/

/*
 * Copyright (c) 1998 Michael Shalayeff
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
 *	This product includes software developed by Michael Shalayeff.
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

#include <sys/types.h>
#include <libsa.h>
#include <machine/lifvar.h>
#include <lib/libsa/ufs.h>
#include <lib/libsa/cd9660.h>
#ifdef notdef
#include <lib/libsa/nfs.h>
#include <lib/libsa/netif.h>
#endif
#include <lib/libsa/exec.h>
#include <dev/cons.h>

const char version[] = "0.03";
int	debug = 0;

const struct x_sw execsw[] = {
	{ "elf", elf_probe,	elf_load },
/*	{ "som", som_probe,	som_load }, */
	{ ""   , NULL,		NULL },
};

struct fs_ops file_system[] = {
	{ lif_open,    lif_close,    lif_read,    lif_write,    lif_seek,
	  lif_stat,    lif_readdir    },
	{ ufs_open,    ufs_close,    ufs_read,    ufs_write,    ufs_seek,
	  ufs_stat,    ufs_readdir    },
#ifdef notdef
	{ nfs_open,    nfs_close,    nfs_read,    nfs_write,    nfs_seek,
	  nfs_stat,    nfs_readdir    },
#endif
	{ cd9660_open, cd9660_close, cd9660_read, cd9660_write, cd9660_seek,
	  cd9660_stat, cd9660_readdir },
};
int nfsys = NENTS(file_system);

#ifdef notdef
struct netif_driver	*netif_drivers[] = {
	NULL
};
int n_netif_drivers = NENTS(netif_drivers);
#endif

struct devsw devsw[] = {
	{ "ct",	iodcstrategy, ctopen, ctclose, noioctl },
	{ "sd",	iodcstrategy, dkopen, dkclose, noioctl },
	{ "lf", iodcstrategy, lfopen, lfclose, noioctl }
};
int	ndevs = NENTS(devsw);

struct consdev	constab[] = {
	{ ite_probe, ite_init, ite_getc, ite_putc },
	{ NULL }
};
struct consdev *cn_tab;

