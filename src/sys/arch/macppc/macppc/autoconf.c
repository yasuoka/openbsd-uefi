/*	$OpenBSD: autoconf.c,v 1.22 2005/12/27 18:31:09 miod Exp $	*/
/*
 * Copyright (c) 1996, 1997 Per Fogelstrom
 * Copyright (c) 1995 Theo de Raadt
 * Copyright (c) 1988 University of Utah.
 * Copyright (c) 1992, 1993
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
 * 3. Neither the name of the University nor the names of its contributors
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
 * from: Utah Hdr: autoconf.c 1.31 91/01/21
 *
 *	from: @(#)autoconf.c	8.1 (Berkeley) 6/10/93
 *      $Id: autoconf.c,v 1.22 2005/12/27 18:31:09 miod Exp $
 */

/*
 * Setup the system to run on the current machine.
 *
 * cpu_configure() is called at boot time.  Available
 * devices are determined (from possibilities mentioned in ioconf.c),
 * and the drivers are initialized.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/buf.h>
#include <sys/disklabel.h>
#include <sys/conf.h>
#include <sys/reboot.h>
#include <sys/device.h>
#include <dev/cons.h>
#include <uvm/uvm_extern.h>
#include <machine/autoconf.h>
#include <machine/powerpc.h>

struct  device *parsedisk(char *, int, int, dev_t *);
void    setroot(void);
void	dumpconf(void);
int	findblkmajor(struct device *);
char	*findblkname(int);
static	struct device * getdisk(char *, int, int, dev_t *);
struct	device * getdevunit(char *, int);
static	struct devmap * findtype(char **);
void	makebootdev(char *cp);
int	getpno(char **);
void	diskconf(void);

/*
 * The following several variables are related to
 * the configuration process, and are used in initializing
 * the machine.
 */
int	cold = 1;	/* if 1, still working on cold-start */
char	bootdev[16];	/* to hold boot dev name */
struct device *bootdv = NULL;

struct dumpmem dumpmem[VM_PHYSSEG_MAX];
u_int ndumpmem;

/*
 *  Configure all devices found that we know about.
 *  This is done at boot time.
 */
void
cpu_configure()
{
	(void)splhigh();	/* To be really sure.. */
	calc_delayconst();

	if(config_rootfound("mainbus", "mainbus") == 0)
		panic("no mainbus found");
	(void)spl0();

	/*
	 * We can not know which is our root disk, defer
	 * until we can checksum blocks to figure it out.
	 */
	md_diskconf = diskconf;
	cold = 0;
}
/*
 * Now that we are fully operational, we can checksum the
 * disks, and using some heuristics, hopefully are able to
 * always determine the correct root disk.
 */
void
diskconf()
{
	/*
	 * Configure root, swap, and dump area.  This is
	 * currently done by running the same checksum
	 * algorithm over all known disks, as was done in
	 * /boot.  Then we basically fixup the *dev vars
	 * from the info we gleaned from this.
	dkcsumattach();
	 * - XXX
	 */

#if 0
	rootconf();
#endif
	setroot();
	dumpconf();
}

/*
 * Crash dump handling.
 */

static	struct nam2blk {
	char *name;
	int  maj;
} nam2blk[] = {
	{ "wd",		0 },	/* 0 = wd */
	{ "sd",		2 },	/* 2 = sd */
	{ "ofdisk",	4 },	/* 4 = ofdisk */
	{ "raid",	19 },	/* 19 = raid */
};

int
findblkmajor(struct device *dv)
{
	char *name = dv->dv_xname;
	int i;

	for (i = 0; i < sizeof(nam2blk)/sizeof(nam2blk[0]); ++i)
		if (strncmp(name, nam2blk[i].name, strlen(nam2blk[i].name)) ==
		    0)
			return (nam2blk[i].maj);
	 return (-1);
}

char *
findblkname(int maj)
{
	int i;

	for (i = 0; i < sizeof(nam2blk)/sizeof(nam2blk[0]); i++)
		if (nam2blk[i].maj == maj)
			return (nam2blk[i].name);
	 return (NULL);
}

static struct device *
getdisk(char *str, int len, int defpart, dev_t *devp)
{
	struct device *dv;

	if ((dv = parsedisk(str, len, defpart, devp)) == NULL) {
		printf("use one of:");
		TAILQ_FOREACH(dv, &alldevs, dv_list) {
			if (dv->dv_class == DV_DISK)
				printf(" %s[a-p]", dv->dv_xname);
#ifdef NFSCLIENT
			if (dv->dv_class == DV_IFNET)
				printf(" %s", dv->dv_xname);
#endif
		}
		printf("\n");
	}
	return (dv);
}

struct device *
parsedisk(char *str, int len, int defpart, dev_t *devp)
{
	struct device *dv;
	char *cp, c;
	int majdev, part;

	if (len == 0)
		return (NULL);
	cp = str + len - 1;
	c = *cp;
	if (c >= 'a' && (c - 'a') < MAXPARTITIONS) {
		part = c - 'a';
		*cp = '\0';
	} else
		part = defpart;

	TAILQ_FOREACH(dv, &alldevs, dv_list) {
		if (dv->dv_class == DV_DISK &&
		    strcmp(str, dv->dv_xname) == 0) {
			majdev = findblkmajor(dv);
			if (majdev < 0)
				panic("parsedisk");
			*devp = MAKEDISKDEV(majdev, dv->dv_unit, part);
			break;
		}
#ifdef NFSCLIENT
		if (dv->dv_class == DV_IFNET &&
		    strcmp(str, dv->dv_xname) == 0) {
			*devp = NODEV;
			break;
		}
#endif
	}

	*cp = c;
	return (dv);
}

/*
 * Attempt to find the device from which we were booted.
 * If we can do so, and not instructed not to do so,
 * change rootdev to correspond to the load device.
 */
void
setroot()
{
	int  majdev, mindev, unit, part, len;
	dev_t temp;
	struct swdevt *swp;
	struct device *dv;
	dev_t nrootdev, nswapdev = NODEV;
	char buf[128];
	int s;

#if defined(NFSCLIENT)
	extern char *nfsbootdevname;
#endif

	printf("bootpath: '%s'\n", bootpath);

	makebootdev(bootpath);
	if(boothowto & RB_DFLTROOT)
		return;		/* Boot compiled in */

	/*
	 * (raid) device auto-configuration could have returned
	 * the root device's id in rootdev.  Check this case.
	 */
	if (rootdev != NODEV) {
		majdev = major(rootdev);
		unit = DISKUNIT(rootdev);
		part = DISKPART(rootdev);

		len = snprintf(buf, sizeof buf, "%s%d", findblkname(majdev),
			unit);
		if (len == -1 || len >= sizeof(buf))
			panic("setroot: device name too long");

		bootdv = getdisk(buf, len, part, &rootdev);
	}

	/* Lookup boot device from boot if not set by configuration */
	if(bootdv == NULL) {
		bootdv = parsedisk(bootdev, strlen(bootdev), 0, &temp);
	}
	if(bootdv == NULL) {
		printf("boot device: lookup '%s' failed.\n", bootdev);
		boothowto |= RB_ASKNAME; /* Don't Panic :-) */
		/* boothowto |= RB_SINGLE; */
	} else
		printf("boot device: %s.\n", bootdv->dv_xname);

	if (boothowto & RB_ASKNAME) {
		for (;;) {
			printf("root device ");
			if (bootdv != NULL)
				 printf("(default %s%c)",
					bootdv->dv_xname,
					bootdv->dv_class == DV_DISK
						? 'a' : ' ');
			printf(": ");
			s = splhigh();
			cnpollc(TRUE);
			len = getsn(buf, sizeof(buf));

			cnpollc(FALSE);
			splx(s);
			if (len == 0 && bootdv != NULL) {
				strlcpy(buf, bootdv->dv_xname, sizeof buf);
				len = strlen(buf);
			}
			if (len > 0 && buf[len - 1] == '*') {
				buf[--len] = '\0';
				dv = getdisk(buf, len, 1, &nrootdev);
				if (dv != NULL) {
					bootdv = dv;
					nswapdev = nrootdev;
					goto gotswap;
				}
			}
			dv = getdisk(buf, len, 0, &nrootdev);
			if (dv != NULL) {
				bootdv = dv;
				break;
			}
		}
		/*
		 * because swap must be on same device as root, for
		 * network devices this is easy.
		 */
		if (bootdv->dv_class == DV_IFNET)
			goto gotswap;

		for (;;) {
			printf("swap device ");
			if (bootdv != NULL)
				printf("(default %s%c)",
					bootdv->dv_xname,
					bootdv->dv_class == DV_DISK?'b':' ');
			printf(": ");
			s = splhigh();
			cnpollc(TRUE);
			len = getsn(buf, sizeof(buf));
			cnpollc(FALSE);
			splx(s);
			if (len == 0 && bootdv != NULL) {
				switch (bootdv->dv_class) {
				case DV_IFNET:
					nswapdev = NODEV;
					break;
				case DV_DISK:
					nswapdev = MAKEDISKDEV(major(nrootdev),
					    DISKUNIT(nrootdev), 1);
					break;
				case DV_TAPE:
				case DV_TTY:
				case DV_DULL:
				case DV_CPU:
					break;
				}
				break;
			}
			dv = getdisk(buf, len, 1, &nswapdev);
			if (dv) {
				if (dv->dv_class == DV_IFNET)
					nswapdev = NODEV;
				break;
			}
		}

gotswap:
		rootdev = nrootdev;
		dumpdev = nswapdev;
		swdevt[0].sw_dev = nswapdev;
		swdevt[1].sw_dev = NODEV;
	}
	else if(mountroot == NULL) {
		/*
		 * `swap generic': Use the device the ROM told us to use.
		 */
		if (bootdv == NULL)
			panic("boot device not known");

		majdev = findblkmajor(bootdv);

		if (majdev >= 0) {
			/*
			 * Root and Swap are on disk.
			 * Boot is always from partition 0.
			 */
			rootdev = MAKEDISKDEV(majdev, bootdv->dv_unit, 0);
			nswapdev = MAKEDISKDEV(majdev, bootdv->dv_unit, 1);
			dumpdev = nswapdev;
		} else {
			/*
			 *  Root and Swap are on net.
			 */
			nswapdev = dumpdev = NODEV;
		}
		swdevt[0].sw_dev = nswapdev;
		swdevt[1].sw_dev = NODEV;

	} else {

		/*
		 * `root DEV swap DEV': honour rootdev/swdevt.
		 * rootdev/swdevt/mountroot already properly set.
		 */
		return;
	}

	switch (bootdv->dv_class) {
#if defined(NFSCLIENT)
	case DV_IFNET:
		mountroot = nfs_mountroot;
		nfsbootdevname = bootdv->dv_xname;
		return;
#endif
	case DV_DISK:
		mountroot = dk_mountroot;
		majdev = major(rootdev);
		mindev = minor(rootdev);
		unit = DISKUNIT(rootdev);
		part = DISKPART(rootdev);
		printf("root on %s%c\n", bootdv->dv_xname, part + 'a');
		break;
	default:
		printf("can't figure root, hope your kernel is right\n");
		return;
	}

	/*
	 * XXX: What is this doing?
	 */
	temp = NODEV;
	for (swp = swdevt; swp->sw_dev != NODEV; swp++) {
		if (majdev == major(swp->sw_dev) &&
		    unit == DISKUNIT(swp->sw_dev)) {
			temp = swdevt[0].sw_dev;
			swdevt[0].sw_dev = swp->sw_dev;
			swp->sw_dev = temp;
			break;
		}
	}
	if (swp->sw_dev == NODEV)
		return;

	/*
	 * If dumpdev was the same as the old primary swap device, move
	 * it to the new primary swap device.
	 */
	if (temp == dumpdev)
		dumpdev = swdevt[0].sw_dev;
}

/*
 * find a device matching "name" and unit number
 */
struct device *
getdevunit(char *name, int unit)
{
	struct device *dev = TAILQ_FIRST(&alldevs);
	char num[10], fullname[16];
	int lunit;

	/* compute length of name and decimal expansion of unit number */
	snprintf(num, sizeof num, "%d", unit);
	lunit = strlen(num);
	if (strlen(name) + lunit >= sizeof(fullname) - 1)
		panic("config_attach: device name too long");

	strlcpy(fullname, name, sizeof fullname);
	strlcat(fullname, num, sizeof fullname);

	while (strcmp(dev->dv_xname, fullname) != 0)
		if ((dev = TAILQ_NEXT(dev, dv_list)) == NULL)
			return NULL;

	return dev;
}

struct devmap {
	char *att;
	char *dev;
	int   type;
};
#define	T_IFACE	0x10

#define	T_BUS	0x00
#define	T_SCSI	0x11
#define	T_IDE	0x12
#define	T_DISK	0x21

static struct devmap *
findtype(char **s)
{
	static struct devmap devmap[] = {
		{ "/ht",		NULL, T_BUS },
		{ "/ht@",		NULL, T_BUS },
		{ "/pci@",		NULL, T_BUS },
		{ "/pci",		NULL, T_BUS },
		{ "/AppleKiwi@",	NULL, T_BUS },
		{ "/AppleKiwi",		NULL, T_BUS },
		{ "/mac-io@",		NULL, T_BUS },
		{ "/mac-io",		NULL, T_BUS },
		{ "/@",			NULL, T_BUS },
		{ "/scsi@",		"sd", T_SCSI },
		{ "/ide",		"wd", T_IDE },
		{ "/ata",		"wd", T_IDE },
		{ "/k2-sata-root",	NULL, T_BUS },
		{ "/k2-sata",		"wd", T_IDE },
		{ "/disk@",		"sd", T_DISK },
		{ "/disk",		"wd", T_DISK },
		{ "/bcom5704@4",	"bge0", T_IFACE },
		{ "/bcom5704@4,1",	"bge1", T_IFACE },
		{ "/ethernet",		"gem0", T_IFACE },
		{ NULL, NULL }
	};
	struct devmap *dp = &devmap[0];

	while (dp->att) {
		if (strncmp(*s, dp->att, strlen(dp->att)) == 0) {
			*s += strlen(dp->att);
			break;
		}
		dp++;
	}
	if (dp->att == NULL)
		printf("string [%s] not found\n", *s);

	return(dp);
}

/*
 * Look at the string 'bp' and decode the boot device.
 * Boot names look like: '/pci/scsi@c/disk@0,0/bsd'
 *                       '/pci/mac-io/ide@20000/disk@0,0/bsd
 *                       '/pci/mac-io/ide/disk/bsd
 *			 '/ht@0,f2000000/pci@2/bcom5704@4/bsd'
 */
void
makebootdev(char *bp)
{
	int	unit;
	char   *dev, *cp;
	struct devmap *dp;

	cp = bp;
	do {
		while(*cp && *cp != '/')
			cp++;

		dp = findtype(&cp);
		if (!dp->att) {
			printf("Warning: bootpath unrecognized: %s\n", bp);
			return;
		}
	} while((dp->type & T_IFACE) == 0);

	if (dp->att && dp->type == T_IFACE) {
		snprintf(bootdev, sizeof bootdev, "%s", dp->dev);
		return;
	}
	dev = dp->dev;
	while(*cp && *cp != '/')
		cp++;
	dp = findtype(&cp);
	if (dp->att && dp->type == T_DISK) {
		unit = getpno(&cp);
		snprintf(bootdev, sizeof bootdev, "%s%d%c", dev, unit, 'a');
		return;
	}
	printf("Warning: boot device unrecognized: %s\n", bp);
	return;
}

int
getpno(char **cp)
{
	int val = 0;
	char *cx = *cp;

	while(*cx && *cx >= '0' && *cx <= '9') {
		val = val * 10 + *cx - '0';
		cx++;
	}
	*cp = cx;
	return val;
}
