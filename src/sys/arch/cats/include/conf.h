/*	$OpenBSD: conf.h,v 1.3 2005/05/13 22:54:00 miod Exp $	*/
/*	$NetBSD: conf.h,v 1.8 2002/02/10 12:26:03 chris Exp $	*/

#ifndef _CATS_CONF_H
#define	_CATS_CONF_H

/*
 * CATS specific device includes go in here
 */
#define	CONF_HAVE_PCI
#define	CONF_HAVE_USB
#define	CONF_HAVE_SCSIPI
#define	CONF_HAVE_WSCONS
#define	CONF_HAVE_FCOM
#define	CONF_HAVE_SPKR

#include <arm/conf.h>

#endif	/* _CATS_CONF_H */
