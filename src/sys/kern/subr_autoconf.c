/*	$OpenBSD: subr_autoconf.c,v 1.48 2006/05/28 07:12:11 deraadt Exp $	*/
/*	$NetBSD: subr_autoconf.c,v 1.21 1996/04/04 06:06:18 cgd Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratories.
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
 * from: Header: subr_autoconf.c,v 1.12 93/02/01 19:31:48 torek Exp  (LBL)
 *
 *	@(#)subr_autoconf.c	8.1 (Berkeley) 6/10/93
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/hotplug.h>
#include <sys/limits.h>
#include <sys/malloc.h>
#include <sys/systm.h>
/* Extra stuff from Matthias Drochner <drochner@zelux6.zel.kfa-juelich.de> */
#include <sys/queue.h>
#include <sys/proc.h>

#include "hotplug.h"

/*
 * Autoconfiguration subroutines.
 */

typedef int (*cond_predicate_t)(struct device *, void *);

/*
 * ioconf.c exports exactly two names: cfdata and cfroots.  All system
 * devices and drivers are found via these tables.
 */
extern short cfroots[];

#define	ROOT ((struct device *)NULL)

struct matchinfo {
	cfmatch_t fn;
	struct	device *parent;
	void	*match, *aux;
	int	indirect, pri;
};

struct cftable_head allcftables;

static struct cftable staticcftable = {
	cfdata
};

#ifndef AUTOCONF_VERBOSE
#define AUTOCONF_VERBOSE 0
#endif /* AUTOCONF_VERBOSE */
int autoconf_verbose = AUTOCONF_VERBOSE;	/* trace probe calls */

static void mapply(struct matchinfo *, struct cfdata *);

struct deferred_config {
	TAILQ_ENTRY(deferred_config) dc_queue;
	struct device *dc_dev;
	void (*dc_func)(struct device *);
};

TAILQ_HEAD(, deferred_config) deferred_config_queue;

void config_process_deferred_children(struct device *);

struct devicelist alldevs;		/* list of all devices */
struct evcntlist allevents;		/* list of all event counters */

__volatile int config_pending;		/* semaphore for mountroot */

/*
 * Initialize autoconfiguration data structures.  This occurs before console
 * initialization as that might require use of this subsystem.  Furthermore
 * this means that malloc et al. isn't yet available.
 */
void
config_init(void)
{
	TAILQ_INIT(&deferred_config_queue);
	TAILQ_INIT(&alldevs);
	TAILQ_INIT(&allevents);
	TAILQ_INIT(&allcftables);
	TAILQ_INSERT_TAIL(&allcftables, &staticcftable, list);
}

/*
 * Apply the matching function and choose the best.  This is used
 * a few times and we want to keep the code small.
 */
void
mapply(struct matchinfo *m, struct cfdata *cf)
{
	int pri;
	void *match;

	if (m->indirect)
		match = config_make_softc(m->parent, cf);
	else
		match = cf;

	if (autoconf_verbose) {
		printf(">>> probing for %s", cf->cf_driver->cd_name);
		if (cf->cf_fstate == FSTATE_STAR)
			printf("*\n");
		else
			printf("%d\n", cf->cf_unit);
	}
	if (m->fn != NULL)
		pri = (*m->fn)(m->parent, match, m->aux);
	else {
	        if (cf->cf_attach->ca_match == NULL) {
			panic("mapply: no match function for '%s' device",
			    cf->cf_driver->cd_name);
		}
		pri = (*cf->cf_attach->ca_match)(m->parent, match, m->aux);
	}
	if (autoconf_verbose)
		printf(">>> %s probe returned %d\n", cf->cf_driver->cd_name,
		    pri);

	if (pri > m->pri) {
		if (m->indirect && m->match)
			free(m->match, M_DEVBUF);
		m->match = match;
		m->pri = pri;
	} else {
		if (m->indirect)
			free(match, M_DEVBUF);
	}
}

/*
 * Iterate over all potential children of some device, calling the given
 * function (default being the child's match function) for each one.
 * Nonzero returns are matches; the highest value returned is considered
 * the best match.  Return the `found child' if we got a match, or NULL
 * otherwise.  The `aux' pointer is simply passed on through.
 *
 * Note that this function is designed so that it can be used to apply
 * an arbitrary function to all potential children (its return value
 * can be ignored).
 */
void *
config_search(cfmatch_t fn, struct device *parent, void *aux)
{
	struct cfdata *cf;
	short *p;
	struct matchinfo m;
	struct cftable *t;

	m.fn = fn;
	m.parent = parent;
	m.match = NULL;
	m.aux = aux;
	m.indirect = parent && parent->dv_cfdata->cf_driver->cd_indirect;
	m.pri = 0;
	TAILQ_FOREACH(t, &allcftables, list) {
		for (cf = t->tab; cf->cf_driver; cf++) {
			/*
			 * Skip cf if no longer eligible, otherwise scan
			 * through parents for one matching `parent',
			 * and try match function.
			 */
			if (cf->cf_fstate == FSTATE_FOUND)
				continue;
			if (cf->cf_fstate == FSTATE_DNOTFOUND ||
			    cf->cf_fstate == FSTATE_DSTAR)
				continue;
			for (p = cf->cf_parents; *p >= 0; p++)
				if (parent->dv_cfdata == &(t->tab)[*p])
					mapply(&m, cf);
		}
	}
	if (autoconf_verbose) {
		if (m.match) {
			if (m.indirect)
				cf = ((struct device *)m.match)->dv_cfdata;
			else
				cf = (struct cfdata *)m.match;
			printf(">>> %s probe won\n",
			    cf->cf_driver->cd_name);
		} else
			printf(">>> no winning probe\n");
	}
	return (m.match);
}

/*
 * Iterate over all potential children of some device, calling the given
 * function for each one.
 *
 * Note that this function is designed so that it can be used to apply
 * an arbitrary function to all potential children (its return value
 * can be ignored).
 */
void
config_scan(cfscan_t fn, struct device *parent)
{
	struct cfdata *cf;
	short *p;
	void *match;
	int indirect;
	struct cftable *t;

	indirect = parent && parent->dv_cfdata->cf_driver->cd_indirect;
	TAILQ_FOREACH(t, &allcftables, list) {
		for (cf = t->tab; cf->cf_driver; cf++) {
			/*
			 * Skip cf if no longer eligible, otherwise scan
			 * through parents for one matching `parent',
			 * and try match function.
			 */
			if (cf->cf_fstate == FSTATE_FOUND)
				continue;
			if (cf->cf_fstate == FSTATE_DNOTFOUND ||
			    cf->cf_fstate == FSTATE_DSTAR)
				continue;
			for (p = cf->cf_parents; *p >= 0; p++)
				if (parent->dv_cfdata == &(t->tab)[*p]) {
					match = indirect?
					    config_make_softc(parent, cf) :
					    (void *)cf;
					(*fn)(parent, match);
				}
		}
	}
}

/*
 * Find the given root device.
 * This is much like config_search, but there is no parent.
 */
void *
config_rootsearch(cfmatch_t fn, char *rootname, void *aux)
{
	struct cfdata *cf;
	short *p;
	struct matchinfo m;

	m.fn = fn;
	m.parent = ROOT;
	m.match = NULL;
	m.aux = aux;
	m.indirect = 0;
	m.pri = 0;
	/*
	 * Look at root entries for matching name.  We do not bother
	 * with found-state here since only one root should ever be
	 * searched (and it must be done first).
	 */
	for (p = cfroots; *p >= 0; p++) {
		cf = &cfdata[*p];
		if (strcmp(cf->cf_driver->cd_name, rootname) == 0)
			mapply(&m, cf);
	}
	return (m.match);
}

char *msgs[3] = { "", " not configured\n", " unsupported\n" };

/*
 * The given `aux' argument describes a device that has been found
 * on the given parent, but not necessarily configured.  Locate the
 * configuration data for that device (using the submatch function
 * provided, or using candidates' cd_match configuration driver
 * functions) and attach it, and return true.  If the device was
 * not configured, call the given `print' function and return 0.
 */
struct device *
config_found_sm(struct device *parent, void *aux, cfprint_t print,
    cfmatch_t submatch)
{
	void *match;

	if ((match = config_search(submatch, parent, aux)) != NULL)
		return (config_attach(parent, match, aux, print));
	if (print)
		printf(msgs[(*print)(aux, parent->dv_xname)]);
	return (NULL);
}

/*
 * As above, but for root devices.
 */
struct device *
config_rootfound(char *rootname, void *aux)
{
	void *match;

	if ((match = config_rootsearch((cfmatch_t)NULL, rootname, aux)) != NULL)
		return (config_attach(ROOT, match, aux, (cfprint_t)NULL));
	printf("root device %s not configured\n", rootname);
	return (NULL);
}

/*
 * Attach a found device.  Allocates memory for device variables.
 */
struct device *
config_attach(struct device *parent, void *match, void *aux, cfprint_t print)
{
	struct cfdata *cf;
	struct device *dev;
	struct cfdriver *cd;
	struct cfattach *ca;
	struct cftable *t;

	if (parent && parent->dv_cfdata->cf_driver->cd_indirect) {
		dev = match;
		cf = dev->dv_cfdata;
	} else {
		cf = match;
		dev = config_make_softc(parent, cf);
	}

	cd = cf->cf_driver;
	ca = cf->cf_attach;

	cd->cd_devs[dev->dv_unit] = dev;

	/*
	 * If this is a "STAR" device and we used the last unit, prepare for
	 * another one.
	 */
	if (cf->cf_fstate == FSTATE_STAR) {
		if (dev->dv_unit == cf->cf_unit)
			cf->cf_unit++;
	} else
		cf->cf_fstate = FSTATE_FOUND;

	TAILQ_INSERT_TAIL(&alldevs, dev, dv_list);
	device_ref(dev);

	if (parent == ROOT)
		printf("%s (root)", dev->dv_xname);
	else {
		printf("%s at %s", dev->dv_xname, parent->dv_xname);
		if (print)
			(void) (*print)(aux, (char *)0);
	}

	/*
	 * Before attaching, clobber any unfound devices that are
	 * otherwise identical, or bump the unit number on all starred
	 * cfdata for this device.
	 */
	TAILQ_FOREACH(t, &allcftables, list) {
		for (cf = t->tab; cf->cf_driver; cf++)
			if (cf->cf_driver == cd &&
			    cf->cf_unit == dev->dv_unit) {
				if (cf->cf_fstate == FSTATE_NOTFOUND)
					cf->cf_fstate = FSTATE_FOUND;
				if (cf->cf_fstate == FSTATE_STAR)
					cf->cf_unit++;
			}
	}
#ifdef __HAVE_DEVICE_REGISTER
	device_register(dev, aux);
#endif
	(*ca->ca_attach)(parent, dev, aux);
	config_process_deferred_children(dev);
#if NHOTPLUG > 0
	if (!cold)
		hotplug_device_attach(cd->cd_class, dev->dv_xname, dev->dv_unit);
#endif
	return (dev);
}

struct device *
config_make_softc(struct device *parent, struct cfdata *cf)
{
	struct device *dev;
	struct cfdriver *cd;
	struct cfattach *ca;

	cd = cf->cf_driver;
	ca = cf->cf_attach;
	if (ca->ca_devsize < sizeof(struct device))
		panic("config_make_softc");

	/* get memory for all device vars */
	dev = (struct device *)malloc(ca->ca_devsize, M_DEVBUF, M_NOWAIT);
	if (!dev)
		panic("config_make_softc: allocation for device softc failed");
	bzero(dev, ca->ca_devsize);
	dev->dv_class = cd->cd_class;
	dev->dv_cfdata = cf;
	dev->dv_flags = DVF_ACTIVE;	/* always initially active */

	/* If this is a STAR device, search for a free unit number */
	if (cf->cf_fstate == FSTATE_STAR) {
		for (dev->dv_unit = cf->cf_starunit1;
		    dev->dv_unit < cf->cf_unit; dev->dv_unit++)
			if (cd->cd_ndevs == 0 ||
			    dev->dv_unit >= cd->cd_ndevs ||
			    cd->cd_devs[dev->dv_unit] == NULL)
				break;
	} else
		dev->dv_unit = cf->cf_unit;

	/* Build the device name into dv_xname. */
	if (snprintf(dev->dv_xname, sizeof(dev->dv_xname), "%s%d",
	    cd->cd_name, dev->dv_unit) >= sizeof(dev->dv_xname))
		panic("config_make_softc: device name too long");
	dev->dv_parent = parent;

	/* put this device in the devices array */
	if (dev->dv_unit >= cd->cd_ndevs) {
		/*
		 * Need to expand the array.
		 */
		int old = cd->cd_ndevs, new;
		void **nsp;

		if (old == 0)
			new = MINALLOCSIZE / sizeof(void *);
		else
			new = old * 2;
		while (new <= dev->dv_unit)
			new *= 2;
		cd->cd_ndevs = new;
		nsp = malloc(new * sizeof(void *), M_DEVBUF, M_NOWAIT);	
		if (nsp == 0)
			panic("config_make_softc: %sing dev array",
			    old != 0 ? "expand" : "creat");
		bzero(nsp + old, (new - old) * sizeof(void *));
		if (old != 0) {
			bcopy(cd->cd_devs, nsp, old * sizeof(void *));
			free(cd->cd_devs, M_DEVBUF);
		}
		cd->cd_devs = nsp;
	}
	if (cd->cd_devs[dev->dv_unit])
		panic("config_make_softc: duplicate %s", dev->dv_xname);

	dev->dv_ref = 1;

	return (dev);
}

/*
 * Detach a device.  Optionally forced (e.g. because of hardware
 * removal) and quiet.  Returns zero if successful, non-zero
 * (an error code) otherwise.
 *
 * Note that this code wants to be run from a process context, so
 * that the detach can sleep to allow processes which have a device
 * open to run and unwind their stacks.
 */
int
config_detach(struct device *dev, int flags)
{
	struct cfdata *cf;
	struct cfattach *ca;
	struct cfdriver *cd;
	int rv = 0, i, devnum = dev->dv_unit;
#ifdef DIAGNOSTIC
	struct device *d;
#endif
#if NHOTPLUG > 0
	char devname[16];
#endif

#if NHOTPLUG > 0
	strlcpy(devname, dev->dv_xname, sizeof(devname));
#endif

	cf = dev->dv_cfdata;
#ifdef DIAGNOSTIC
	if (cf->cf_fstate != FSTATE_FOUND && cf->cf_fstate != FSTATE_STAR)
		panic("config_detach: bad device fstate");
#endif
	ca = cf->cf_attach;
	cd = cf->cf_driver;

	/*
	 * Ensure the device is deactivated.  If the device doesn't
	 * have an activation entry point, we allow DVF_ACTIVE to
	 * remain set.  Otherwise, if DVF_ACTIVE is still set, the
	 * device is busy, and the detach fails.
	 */
	if (ca->ca_activate != NULL)
		rv = config_deactivate(dev);

	/*
	 * Try to detach the device.  If that's not possible, then
	 * we either panic() (for the forced but failed case), or
	 * return an error.
	 */
	if (rv == 0) {
		if (ca->ca_detach != NULL)
			rv = (*ca->ca_detach)(dev, flags);
		else
			rv = EOPNOTSUPP;
	}
	if (rv != 0) {
		if ((flags & DETACH_FORCE) == 0)
			return (rv);
		else
			panic("config_detach: forced detach of %s failed (%d)",
			    dev->dv_xname, rv);
	}

	/*
	 * The device has now been successfully detached.
	 */

#ifdef DIAGNOSTIC
	/*
	 * Sanity: If you're successfully detached, you should have no
	 * children.  (Note that because children must be attached
	 * after parents, we only need to search the latter part of
	 * the list.)
	 */
	for (d = TAILQ_NEXT(dev, dv_list); d != NULL;
	     d = TAILQ_NEXT(d, dv_list)) {
		if (d->dv_parent == dev)
			panic("config_detach: detached device has children");
	}
#endif

	/*
	 * Mark cfdata to show that the unit can be reused, if possible.
	 * Note that we can only re-use a starred unit number if the unit
	 * being detached had the last assigned unit number.
	 */
	for (cf = cfdata; cf->cf_driver; cf++) {
		if (cf->cf_driver == cd) {
			if (cf->cf_fstate == FSTATE_FOUND &&
			    cf->cf_unit == devnum)
				cf->cf_fstate = FSTATE_NOTFOUND;
			if (cf->cf_fstate == FSTATE_STAR &&
			    cf->cf_unit == devnum + 1)
				cf->cf_unit--;
		}
	}

	/*
	 * Unlink from device list.
	 */
	TAILQ_REMOVE(&alldevs, dev, dv_list);
	device_unref(dev);

	/*
	 * Remove from cfdriver's array, tell the world, and free softc.
	 */
	cd->cd_devs[devnum] = NULL;
	if ((flags & DETACH_QUIET) == 0)
		printf("%s detached\n", dev->dv_xname);

	device_unref(dev);
	/*
	 * If the device now has no units in use, deallocate its softc array.
	 */
	for (i = 0; i < cd->cd_ndevs; i++)
		if (cd->cd_devs[i] != NULL)
			break;
	if (i == cd->cd_ndevs) {		/* nothing found; deallocate */
		free(cd->cd_devs, M_DEVBUF);
		cd->cd_devs = NULL;
		cd->cd_ndevs = 0;
		cf->cf_unit = 0;
	}

#if NHOTPLUG > 0
	if (!cold)
		hotplug_device_detach(cd->cd_class, devname, devnum);
#endif

	/*
	 * Return success.
	 */
	return (0);
}

int
config_activate(struct device *dev)
{
	struct cfattach *ca = dev->dv_cfdata->cf_attach;
	int rv = 0, oflags = dev->dv_flags;

	if (ca->ca_activate == NULL)
		return (EOPNOTSUPP);

	if ((dev->dv_flags & DVF_ACTIVE) == 0) {
		dev->dv_flags |= DVF_ACTIVE;
		rv = (*ca->ca_activate)(dev, DVACT_ACTIVATE);
		if (rv)
			dev->dv_flags = oflags;
	}
	return (rv);
}

int
config_deactivate(struct device *dev)
{
	struct cfattach *ca = dev->dv_cfdata->cf_attach;
	int rv = 0, oflags = dev->dv_flags;

	if (ca->ca_activate == NULL)
		return (EOPNOTSUPP);

	if (dev->dv_flags & DVF_ACTIVE) {
		dev->dv_flags &= ~DVF_ACTIVE;
		rv = (*ca->ca_activate)(dev, DVACT_DEACTIVATE);
		if (rv)
			dev->dv_flags = oflags;
	}
	return (rv);
}

/*
 * Defer the configuration of the specified device until all
 * of its parent's devices have been attached.
 */
void
config_defer(struct device *dev, void (*func)(struct device *))
{
	struct deferred_config *dc;

	if (dev->dv_parent == NULL)
		panic("config_defer: can't defer config of a root device");

#ifdef DIAGNOSTIC
	for (dc = TAILQ_FIRST(&deferred_config_queue); dc != NULL;
	     dc = TAILQ_NEXT(dc, dc_queue)) {
		if (dc->dc_dev == dev)
			panic("config_defer: deferred twice");
	}
#endif

	if ((dc = malloc(sizeof(*dc), M_DEVBUF, M_NOWAIT)) == NULL)
		panic("config_defer: can't allocate defer structure");

	dc->dc_dev = dev;
	dc->dc_func = func;
	TAILQ_INSERT_TAIL(&deferred_config_queue, dc, dc_queue);
	config_pending_incr();
}

/*
 * Process the deferred configuration queue for a device.
 */
void
config_process_deferred_children(struct device *parent)
{
	struct deferred_config *dc, *ndc;

	for (dc = TAILQ_FIRST(&deferred_config_queue);
	     dc != NULL; dc = ndc) {
		ndc = TAILQ_NEXT(dc, dc_queue);
		if (dc->dc_dev->dv_parent == parent) {
			TAILQ_REMOVE(&deferred_config_queue, dc, dc_queue);
			(*dc->dc_func)(dc->dc_dev);
			free(dc, M_DEVBUF);
			config_pending_decr();
		}
	}
}

/*
 * Manipulate the config_pending semaphore.
 */
void
config_pending_incr(void)
{

	config_pending++;
}

void
config_pending_decr(void)
{

#ifdef DIAGNOSTIC
	if (config_pending == 0)
		panic("config_pending_decr: config_pending == 0");
#endif
	config_pending--;
	if (config_pending == 0)
		wakeup((void *)&config_pending);
}

int
config_detach_children(struct device *parent, int flags)
{
	struct device *dev, *next_dev, *prev_dev;
	int rv = 0;

	/*
	 * The config_detach routine may sleep, meaning devices
	 * may be added to the queue. However, all devices will
	 * be added to the tail of the queue, the queue won't
	 * be re-organized, and the subtree of parent here should be locked
	 * for purposes of adding/removing children.
	 *
	 * Note that we can not afford trying to walk the device list
	 * once - our ``next'' device might be a child of the device
	 * we are about to detach, so it would disappear.
	 * Just play it safe and restart from the parent.
	 */
	for (prev_dev = NULL, dev = TAILQ_LAST(&alldevs, devicelist);
	    dev != NULL; dev = next_dev) {
		if (dev->dv_parent == parent) {
			if ((rv = config_detach(dev, flags)) != 0)
				return (rv);
			next_dev = prev_dev ? prev_dev : TAILQ_LAST(&alldevs,
			    devicelist);
		} else {
			prev_dev = dev;
			next_dev = TAILQ_PREV(dev, devicelist, dv_list);
		}
	}

	return (0);
}

int
config_activate_children(struct device *parent, enum devact act)
{
	struct device *dev, *next_dev;
	int  rv = 0;

	/* The config_deactivate routine may sleep, meaning devices
	   may be added to the queue. However, all devices will
	   be added to the tail of the queue, the queue won't
	   be re-organized, and the subtree of parent here should be locked
	   for purposes of adding/removing children.
	*/
	for (dev = TAILQ_FIRST(&alldevs);
	     dev != NULL; dev = next_dev) {
		next_dev = TAILQ_NEXT(dev, dv_list);
		if (dev->dv_parent == parent) {
			switch (act) {
			case DVACT_ACTIVATE:
				rv = config_activate(dev);
				break;
			case DVACT_DEACTIVATE:
				rv = config_deactivate(dev);
				break;
			default:
#ifdef DIAGNOSTIC
				printf ("config_activate_children: shouldn't get here");
#endif
				rv = EOPNOTSUPP;
				break;

			}
						
			if (rv)
				break;
		}
	}

	return  (rv);
}

/* 
 * Lookup a device in the cfdriver device array.  Does not return a
 * device if it is not active.
 *
 * Increments ref count on the device by one, reflecting the
 * new reference created on the stack.
 *
 * Context: process only 
 */
struct device *
device_lookup(struct cfdriver *cd, int unit)
{
	struct device *dv = NULL;

	if (unit >= 0 && unit < cd->cd_ndevs)
		dv = (struct device *)(cd->cd_devs[unit]);
	
	if (!dv)
		return (NULL);

	if (!(dv->dv_flags & DVF_ACTIVE))
		dv = NULL;

	if (dv != NULL)
		device_ref(dv);

	return (dv);
}


/*
 * Increments the ref count on the device structure. The device
 * structure is freed when the ref count hits 0.
 *
 * Context: process or interrupt
 */
void
device_ref(struct device *dv)
{
	dv->dv_ref++;
}

/*
 * Decrement the ref count on the device structure.
 *
 * free's the structure when the ref count hits zero.
 *
 * Context: process or interrupt
 */
void
device_unref(struct device *dv)
{
	dv->dv_ref--;
	if (dv->dv_ref == 0) {
		free(dv, M_DEVBUF);
	}
}

/*
 * Attach an event.  These must come from initially-zero space (see
 * commented-out assignments below), but that occurs naturally for
 * device instance variables.
 */
void
evcnt_attach(struct device *dev, const char *name, struct evcnt *ev)
{

#ifdef DIAGNOSTIC
	if (strlen(name) >= sizeof(ev->ev_name))
		panic("evcnt_attach");
#endif
	/* ev->ev_next = NULL; */
	ev->ev_dev = dev;
	/* ev->ev_count = 0; */
	strlcpy(ev->ev_name, name, sizeof ev->ev_name);
	TAILQ_INSERT_TAIL(&allevents, ev, ev_list);
}
