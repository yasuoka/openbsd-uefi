/*	$OpenBSD: drm_linux.h,v 1.5 2015/02/12 02:12:02 kettenis Exp $	*/
/*
 * Copyright (c) 2013, 2014 Mark Kettenis
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define __force
#define KERN_INFO
#define KERN_WARNING
#define KERN_NOTICE
#define KERN_DEBUG
#define KERN_CRIT
#define KERN_ERR

#define KBUILD_MODNAME "drm"

#ifndef pr_fmt
#define pr_fmt(fmt) fmt
#endif

#define printk(fmt, arg...)	printf(fmt, ## arg)
#define pr_warn(fmt, arg...)	printf(pr_fmt(fmt), ## arg)
#define pr_notice(fmt, arg...)	printf(pr_fmt(fmt), ## arg)
#define pr_crit(fmt, arg...)	printf(pr_fmt(fmt), ## arg)
#define pr_err(fmt, arg...)	printf(pr_fmt(fmt), ## arg)

#ifdef DRMDEBUG
#define pr_info(fmt, arg...)	printf(pr_fmt(fmt), ## arg)
#define pr_debug(fmt, arg...)	printf(pr_fmt(fmt), ## arg)
#else
#define pr_info(fmt, arg...)	do { } while(0)
#define pr_debug(fmt, arg...)	do { } while(0)
#endif

#define dev_warn(dev, fmt, arg...)				\
	printf("drm:pid%d:%s *WARNING* " fmt, curproc->p_pid,	\
	    __func__ , ## arg)
#define dev_notice(dev, fmt, arg...)				\
	printf("drm:pid%d:%s *NOTICE* " fmt, curproc->p_pid,	\
	    __func__ , ## arg)
#define dev_crit(dev, fmt, arg...)				\
	printf("drm:pid%d:%s *ERROR* " fmt, curproc->p_pid,	\
	    __func__ , ## arg)
#define dev_err(dev, fmt, arg...)				\
	printf("drm:pid%d:%s *ERROR* " fmt, curproc->p_pid,	\
	    __func__ , ## arg)

#ifdef DRMDEBUG
#define dev_info(dev, fmt, arg...)				\
	printf("drm: " fmt, ## arg)
#define dev_debug(dev, fmt, arg...)				\
	printf("drm:pid%d:%s *DEBUG* " fmt, curproc->p_pid,	\
	    __func__ , ## arg)
#else
#define dev_info(dev, fmt, arg...) 				\
	    do { } while(0)
#define dev_debug(dev, fmt, arg...) 				\
	    do { } while(0)
#endif

static inline void
spin_lock_irqsave(struct mutex *mtxp, __unused unsigned long flags)
{
	mtx_enter(mtxp);
}
static inline void
spin_unlock_irqrestore(struct mutex *mtxp, __unused unsigned long flags)
{
	mtx_leave(mtxp);
}
#define spin_lock(mtxp)			mtx_enter(mtxp)
#define spin_unlock(mtxp)		mtx_leave(mtxp)
#define spin_lock_irq(mtxp)		mtx_enter(mtxp)
#define spin_unlock_irq(mtxp)		mtx_leave(mtxp)
#define mutex_lock_interruptible(rwl)	-rw_enter(rwl, RW_WRITE | RW_INTR)
#define mutex_lock(rwl)			rw_enter_write(rwl)
#define mutex_unlock(rwl)		rw_exit_write(rwl)
#define mutex_is_locked(rwl)		(rw_status(rwl) == RW_WRITE)
#define down_read(rwl)			rw_enter_read(rwl)
#define up_read(rwl)			rw_exit_read(rwl)
#define down_write(rwl)			rw_enter_write(rwl)
#define up_write(rwl)			rw_exit_write(rwl)
#define read_lock(rwl)			rw_enter_read(rwl)
#define read_unlock(rwl)		rw_exit_read(rwl)
#define write_lock(rwl)			rw_enter_write(rwl)
#define write_unlock(rwl)		rw_exit_write(rwl)

#if defined(__i386__) || defined(__amd64__)

static inline void
pagefault_disable(void)
{
	KASSERT(curcpu()->ci_inatomic == 0);
	curcpu()->ci_inatomic = 1;
}

static inline void
pagefault_enable(void)
{
	KASSERT(curcpu()->ci_inatomic == 1);
	curcpu()->ci_inatomic = 0;
}

static inline int
in_atomic(void)
{
	return curcpu()->ci_inatomic;
}

static inline void *
kmap_atomic(struct vm_page *pg)
{
	vaddr_t va;

#if defined (__HAVE_PMAP_DIRECT)
	va = pmap_map_direct(pg);
#else
	extern vaddr_t pmap_tmpmap_pa(paddr_t);
	va = pmap_tmpmap_pa(VM_PAGE_TO_PHYS(pg));
#endif
	return (void *)va;
}

static inline void
kunmap_atomic(void *addr)
{
#if defined (__HAVE_PMAP_DIRECT)
	pmap_unmap_direct((vaddr_t)addr);
#else
	extern void pmap_tmpunmap_pa(void);
	pmap_tmpunmap_pa();
#endif
}

#endif
