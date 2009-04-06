/* drmP.h -- Private header for Direct Rendering Manager -*- linux-c -*-
 * Created: Mon Jan  4 10:05:05 1999 by faith@precisioninsight.com
 */
/*-
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 *
 */

#ifndef _DRM_P_H_
#define _DRM_P_H_

#if defined(_KERNEL) || defined(__KERNEL__)


#include <sys/param.h>
#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/stat.h>
#include <sys/proc.h>
#include <sys/resource.h>
#include <sys/resourcevar.h>
#include <sys/mutex.h>
#include <sys/fcntl.h>
#include <sys/filio.h>
#include <sys/signalvar.h>
#include <sys/poll.h>
#include <sys/tree.h>
#include <sys/endian.h>
#include <sys/mman.h>
#include <sys/stdint.h>
#include <sys/agpio.h>
#include <sys/memrange.h>
#include <sys/extent.h>
#include <sys/vnode.h>
#include <uvm/uvm.h>
#include <dev/pci/pcidevs.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/agpvar.h>
#include <dev/pci/vga_pcivar.h>
#include <machine/param.h>
#include <machine/bus.h>

#include "drm.h"
#include "drm_linux_list.h"
#include "drm_atomic.h"

#define DRM_KERNEL_CONTEXT    0	 /* Change drm_resctx if changed	  */
#define DRM_RESERVED_CONTEXTS 1	 /* Change drm_resctx if changed	  */

#define DRM_MAX_CTXBITMAP (PAGE_SIZE * 8)

				/* Internal types and structures */
#define DRM_IF_VERSION(maj, min) (maj << 16 | min)

#define __OS_HAS_AGP	1

#define DRM_CURRENTPID		curproc->p_pid
#define DRM_LOCK()		rw_enter_write(&dev->dev_lock)
#define DRM_UNLOCK()		rw_exit_write(&dev->dev_lock)
#define DRM_MAXUNITS		8

/* D_CLONE only supports one device, this will be fixed eventually */
#define drm_get_device_from_kdev(_kdev)	\
	(drm_cd.cd_ndevs > 0 ? drm_cd.cd_devs[0] : NULL)
#if 0
#define drm_get_device_from_kdev(_kdev)			\
	(minor(_kdev) < drm_cd.cd_ndevs) ? drm_cd.cd_devs[minor(_kdev)] : NULL
#endif

/* DRM_SUSER returns true if the user is superuser */
#define DRM_SUSER(p)		(suser(p, p->p_acflag) == 0)
#define DRM_MTRR_WC		MDF_WRITECOMBINE

#define PAGE_ALIGN(addr)	(((addr) + PAGE_SIZE - 1) & PAGE_MASK)

extern struct cfdriver drm_cd;

typedef u_int64_t u64;
typedef u_int32_t u32;
typedef u_int16_t u16;
typedef u_int8_t u8;

/* DRM_READMEMORYBARRIER() prevents reordering of reads.
 * DRM_WRITEMEMORYBARRIER() prevents reordering of writes.
 * DRM_MEMORYBARRIER() prevents reordering of reads and writes.
 */
#if defined(__i386__)
#define DRM_READMEMORYBARRIER()		__asm __volatile( \
					"lock; addl $0,0(%%esp)" : : : "memory");
#define DRM_WRITEMEMORYBARRIER()	__asm __volatile("" : : : "memory");
#define DRM_MEMORYBARRIER()		__asm __volatile( \
					"lock; addl $0,0(%%esp)" : : : "memory");
#elif defined(__alpha__)
#define DRM_READMEMORYBARRIER()		alpha_mb();
#define DRM_WRITEMEMORYBARRIER()	alpha_wmb();
#define DRM_MEMORYBARRIER()		alpha_mb();
#elif defined(__amd64__)
#define DRM_READMEMORYBARRIER()		__asm __volatile( \
					"lock; addl $0,0(%%rsp)" : : : "memory");
#define DRM_WRITEMEMORYBARRIER()	__asm __volatile("" : : : "memory");
#define DRM_MEMORYBARRIER()		__asm __volatile( \
					"lock; addl $0,0(%%rsp)" : : : "memory");
#endif

#define	DRM_COPY_TO_USER(user, kern, size)	copyout(kern, user, size)
#define	DRM_COPY_FROM_USER(kern, user, size)	copyin(user, kern, size)
#define le32_to_cpu(x) letoh32(x)
#define cpu_to_le32(x) htole32(x)

#define DRM_UDELAY(udelay)	DELAY(udelay)

#define LOCK_TEST_WITH_RETURN(dev, file_priv)				\
do {									\
	if (!_DRM_LOCK_IS_HELD(dev->lock.hw_lock->lock) ||		\
	     dev->lock.file_priv != file_priv) {			\
		DRM_ERROR("%s called without lock held\n",		\
			   __FUNCTION__);				\
		return EINVAL;						\
	}								\
} while (0)

#define DRM_WAIT_ON(ret, queue, lock,  timeout, msg, condition ) do {	\
	mtx_enter(lock);						\
	while ((ret) == 0) {						\
		if (condition)						\
			break;						\
		ret = msleep((queue), (lock), PZERO | PCATCH,		\
		    (msg), (timeout));					\
	}								\
	mtx_leave(lock);						\
} while (/* CONSTCOND */ 0)

#define DRM_ERROR(fmt, arg...) \
	printf("error: [" DRM_NAME ":pid%d:%s] *ERROR* " fmt,		\
	    curproc->p_pid, __func__ , ## arg)


#define DRM_INFO(fmt, arg...)  printf("%s: " fmt, dev_priv->dev.dv_xname, ## arg)

#ifdef DRMDEBUG
#undef DRM_DEBUG
#define DRM_DEBUG(fmt, arg...) do {					\
	if (drm_debug_flag)						\
		printf("[" DRM_NAME ":pid%d:%s] " fmt, curproc->p_pid,	\
			__func__ , ## arg);				\
} while (0)
#else
#define DRM_DEBUG(fmt, arg...) do { } while(/* CONSTCOND */ 0)
#endif

struct drm_pcidev {
	int vendor;
	int device;
	long driver_private;
};

struct drm_file;
struct drm_device;

struct drm_buf {
	int		  idx;	       /* Index into master buflist	     */
	int		  total;       /* Buffer size			     */
	int		  used;	       /* Amount of buffer in use (for DMA)  */
	unsigned long	  offset;      /* Byte offset (used internally)	     */
	void 		  *address;    /* KVA of buffer			     */
	unsigned long	  bus_address; /* Bus address of buffer		     */
	__volatile__ int  pending;     /* On hardware DMA queue		     */
	struct drm_file   *file_priv;  /* Unique identifier of holding process */
	void		  *dev_private;  /* Per-buffer private storage       */
};

struct drm_dmamem {
	bus_dmamap_t		map;
	caddr_t			kva;
	bus_size_t		size;
	int			nsegs;
	bus_dma_segment_t	segs[1];
};

struct drm_buf_entry {
	struct drm_dmamem	**seglist;
	struct drm_buf		*buflist;
	int			 buf_count;
	int			 buf_size;
	int			 page_order;
	int			 seg_count;
};

struct drm_file {
	SPLAY_ENTRY(drm_file)	 link;
	int			 authenticated;
	unsigned long		 ioctl_count;
	dev_t			 kdev;
	drm_magic_t		 magic;
	int			 flags;
	int			 master;
	int			 minor;
};

struct drm_lock_data {
	struct mutex		 spinlock;
	struct drm_hw_lock	*hw_lock;	/* Hardware lock */
	/* Unique identifier of holding process (NULL is kernel) */
	struct drm_file		*file_priv;
};

/* This structure, in the struct drm_device, is always initialized while
 * the device is open.  dev->dma_lock protects the incrementing of
 * dev->buf_use, which when set marks that no further bufs may be allocated
 * until device teardown occurs (when the last open of the device has closed).
 * The high/low watermarks of bufs are only touched by the X Server, and thus
 * not concurrently accessed, so no locking is needed.
 */
struct drm_device_dma {
	struct rwlock	 	 dma_lock;
	struct drm_buf_entry	 bufs[DRM_MAX_ORDER+1];
	struct drm_buf		**buflist;	/* Vector of pointers info bufs*/
	unsigned long		*pagelist;
	unsigned long		 byte_count;
	int			 buf_use;	/* Buffers used no more alloc */
	int			 buf_count;
	int			 page_count;
	int			 seg_count;
	enum {
		_DRM_DMA_USE_AGP = 0x01,
		_DRM_DMA_USE_SG  = 0x02
	} flags;
};

struct drm_agp_mem {
	void               *handle;
	unsigned long      bound; /* address */
	int                pages;
	TAILQ_ENTRY(drm_agp_mem) link;
};

struct drm_agp_head {
	struct device				*agpdev;
	const char				*chipset;
	TAILQ_HEAD(agp_memlist, drm_agp_mem)	 memory;
	struct agp_info				 info;
	unsigned long				 base;
	unsigned long				 mode;
	unsigned long				 page_mask;
	int					 acquired;
	int					 cant_use_aperture;
	int					 enabled;
   	int					 mtrr;
};

struct drm_sg_mem {
	struct drm_dmamem	*mem;
	unsigned long		 handle;
};

struct drm_local_map {
	TAILQ_ENTRY(drm_local_map)	 link;	/* Link for map list */
	struct drm_dmamem		*dmamem;/* Handle to DMA mem */
	void				*handle;/* KVA, if mapped */
	bus_space_tag_t			 bst;	/* Tag for mapped pci mem */
	bus_space_handle_t		 bsh;	/* Handle to mapped pci mem */
	u_long				 ext;	/* extent for mmap */
	u_long				 offset;/* Physical address */
	u_long				 size;	/* Physical size (bytes) */
	int				 mtrr;	/* Boolean: MTRR used */
	enum drm_map_flags		 flags;	/* Flags */
	enum drm_map_type		 type;	/* Type of memory mapped */
};

struct drm_vblank_info {
	struct mutex		 vb_lock;		/* VBLANK data lock */
	struct timeout		 vb_disable_timer;	/* timer for disable */
	int			 vb_disable_allowed;
	int			 vb_num;		/* number of crtcs */
	u_int32_t		 vb_max;		/* counter reg size */
	struct drm_vblank {
		u_int32_t	 vbl_last;		/* Last recieved */
		u_int32_t	 vbl_count;		/* interrupt no. */
		int		 vbl_refs;		/* Number of users */
		int		 vbl_enabled;		/* Enabled? */
		int		 vbl_inmodeset;		/* in a modeset? */
	}			 vb_crtcs[1];
};

/* Heap implementation for radeon and i915 legacy */
TAILQ_HEAD(drm_heap, drm_mem);

struct drm_mem {
	TAILQ_ENTRY(drm_mem)	 link;
	struct drm_file		*file_priv; /* NULL: free, other: real files */
	int			 start;
	int			 size;
};

/* location of GART table */
#define DRM_ATI_GART_MAIN 1
#define DRM_ATI_GART_FB   2

#define DRM_ATI_GART_PCI  1
#define DRM_ATI_GART_PCIE 2
#define DRM_ATI_GART_IGP  3

#define DMA_BIT_MASK(n) (((n) == 64) ? ~0ULL : (1ULL<<(n)) -1)
#define upper_32_bits(_val) ((u_int32_t)(((_val) >> 16) >> 16))

struct drm_ati_pcigart_info {
	struct drm_local_map	 mapping;
	struct drm_dmamem	*mem;
	void			*addr;
	bus_addr_t		 bus_addr;
	bus_addr_t		 table_mask;
	int			 gart_table_location;
	int			 gart_reg_if;
	int			 table_size;
};

struct drm_driver_info {
	int	(*firstopen)(struct drm_device *);
	int	(*open)(struct drm_device *, struct drm_file *);
	int	(*ioctl)(struct drm_device*, u_long, caddr_t,
		    struct drm_file *);
	void	(*close)(struct drm_device *, struct drm_file *);
	void	(*lastclose)(struct drm_device *);
	void	(*reclaim_buffers_locked)(struct drm_device *,
		    struct drm_file *);
	int	(*dma_ioctl)(struct drm_device *, struct drm_dma *,
		    struct drm_file *);
	int	(*dma_quiescent)(struct drm_device *);
	int	(*context_ctor)(struct drm_device *, int);
	int	(*context_dtor)(struct drm_device *, int);
	int	(*irq_install)(struct drm_device *);
	void	(*irq_uninstall)(struct drm_device *);
	int	vblank_pipes;
	u_int32_t (*get_vblank_counter)(struct drm_device *, int);
	int	(*enable_vblank)(struct drm_device *, int);
	void	(*disable_vblank)(struct drm_device *, int);

	size_t	buf_priv_size;
	size_t	file_priv_size;

	int	major;
	int	minor;
	int	patchlevel;
	const char *name;		/* Simple driver name		   */
	const char *desc;		/* Longer driver name		   */
	const char *date;		/* Date of last major changes.	   */

#define DRIVER_AGP		0x1
#define DRIVER_AGP_REQUIRE	0x2
#define DRIVER_MTRR		0x4
#define DRIVER_DMA		0x8
#define DRIVER_PCI_DMA		0x10
#define DRIVER_SG		0x20
#define DRIVER_IRQ		0x40

	u_int	flags;
};

/** 
 * DRM device functions structure
 */
struct drm_device {
	struct device	  device; /* softc is an extension of struct device */

	const struct drm_driver_info *driver;

	bus_dma_tag_t			dmat;
	bus_space_tag_t			bst;

	char		  *unique;	/* Unique identifier: e.g., busid  */
	int		  unique_len;	/* Length of unique field	   */
	
	int		  if_version;	/* Highest interface version set */
				/* Locks */
	struct rwlock	  dev_lock;	/* protects everything else */

				/* Usage Counters */
	int		  open_count;	/* Outstanding files open	   */

				/* Authentication */
	SPLAY_HEAD(drm_file_tree, drm_file)	files;
	drm_magic_t	  magicid;

	/* Linked list of mappable regions. Protected by dev_lock */
	struct extent				*handle_ext;
	TAILQ_HEAD(drm_map_list, drm_local_map)	 maplist;


	struct drm_lock_data  lock;	/* Information on hardware lock	*/

				/* DMA queues (contexts) */
	struct drm_device_dma  *dma;		/* Optional pointer for DMA support */

				/* Context support */
	int		  irq;		/* Interrupt used by board	   */
	int		  irq_enabled;	/* True if the irq handler is enabled */

	/* VBLANK support */
	struct drm_vblank_info	*vblank;		/* One per ctrc */

	pid_t			 buf_pgid;

	struct drm_agp_head	*agp;
	struct drm_sg_mem	*sg;  /* Scatter gather memory */
	atomic_t		*ctx_bitmap;
	void			*dev_private;
	struct drm_local_map	*agp_buffer_map;
};

struct drm_attach_args {
	const struct drm_driver_info	*driver;
	char				*busid;
	bus_dma_tag_t			 dmat;
	bus_space_tag_t			 bst;
	size_t				 busid_len;
	int				 is_agp;
	u_int8_t			 irq;
};

extern int	drm_debug_flag;

/* Device setup support (drm_drv.c) */
int	drm_pciprobe(struct pci_attach_args *, const struct drm_pcidev * );
struct device	*drm_attach_pci(const struct drm_driver_info *, 
		     struct pci_attach_args *, int, struct device *);
dev_type_ioctl(drmioctl);
dev_type_open(drmopen);
dev_type_close(drmclose);
dev_type_mmap(drmmmap);
struct drm_local_map	*drm_getsarea(struct drm_device *);
struct drm_dmamem	*drm_dmamem_alloc(bus_dma_tag_t, bus_size_t, bus_size_t,
			     int, bus_size_t, int, int);
void			 drm_dmamem_free(bus_dma_tag_t, struct drm_dmamem *);

const struct drm_pcidev	*drm_find_description(int , int ,
			     const struct drm_pcidev *);

/* File operations helpers (drm_fops.c) */
struct drm_file	*drm_find_file_by_minor(struct drm_device *, int);

/* Memory management support (drm_memory.c) */
void	*drm_alloc(size_t);
void	*drm_calloc(size_t, size_t);
void	*drm_realloc(void *, size_t, size_t);
void	 drm_free(void *);

/* XXX until we get PAT support */
#define drm_core_ioremap_wc drm_core_ioremap
void	drm_core_ioremap(struct drm_local_map *, struct drm_device *);
void	drm_core_ioremapfree(struct drm_local_map *);

int	drm_mtrr_add(unsigned long, size_t, int);
int	drm_mtrr_del(int, unsigned long, size_t, int);

/* Heap interface (DEPRECATED) */
int		 drm_init_heap(struct drm_heap *, int, int);
struct drm_mem	*drm_alloc_block(struct drm_heap *, int, int,
		     struct drm_file *);
struct drm_mem	*drm_find_block(struct drm_heap *, int);
void		 drm_free_block(struct drm_heap *, struct drm_mem *);
void		 drm_mem_release(struct drm_heap *, struct drm_file *);
void		 drm_mem_takedown(struct drm_heap *);



int	drm_ctxbitmap_init(struct drm_device *);
void	drm_ctxbitmap_cleanup(struct drm_device *);
void	drm_ctxbitmap_free(struct drm_device *, int);
int	drm_ctxbitmap_next(struct drm_device *);

/* Locking IOCTL support (drm_lock.c) */
int	drm_lock_take(struct drm_lock_data *, unsigned int);
int	drm_lock_free(struct drm_lock_data *, unsigned int);

/* Buffer management and DMA support (drm_bufs.c) */
int	drm_order(unsigned long);
struct drm_local_map *drm_core_findmap(struct drm_device *, unsigned long);
int	drm_rmmap_ioctl(struct drm_device *, void *, struct drm_file *);
void	drm_rmmap(struct drm_device *, struct drm_local_map *);
void	drm_rmmap_locked(struct drm_device *, struct drm_local_map *);
int	drm_addmap_ioctl(struct drm_device *, void *, struct drm_file *);
int	drm_addmap(struct drm_device *, unsigned long, unsigned long,
	    enum drm_map_type, enum drm_map_flags, struct drm_local_map **);
int	drm_addbufs(struct drm_device *, struct drm_buf_desc *);
int	drm_freebufs(struct drm_device *, void *, struct drm_file *);
int	drm_mapbufs(struct drm_device *, void *, struct drm_file *);
int	drm_dma(struct drm_device *, void *, struct drm_file *);
int	drm_dma_setup(struct drm_device *);
void	drm_dma_takedown(struct drm_device *);
void	drm_cleanup_buf(struct drm_device *, struct drm_buf_entry *);
void	drm_free_buffer(struct drm_device *, struct drm_buf *);
void	drm_reclaim_buffers(struct drm_device *, struct drm_file *);

/* IRQ support (drm_irq.c) */
int	drm_irq_install(struct drm_device *);
int	drm_irq_uninstall(struct drm_device *);
void	drm_vblank_cleanup(struct drm_device *);
int	drm_vblank_init(struct drm_device *, int);
u_int32_t drm_vblank_count(struct drm_device *, int);
int	drm_vblank_get(struct drm_device *, int);
void	drm_vblank_put(struct drm_device *, int);
int	drm_modeset_ctl(struct drm_device *, void *, struct drm_file *);
void	drm_handle_vblank(struct drm_device *, int);

/* AGP/PCI Express/GART support (drm_agpsupport.c) */
struct drm_agp_head *drm_agp_init(void);
void	drm_agp_takedown(struct drm_device *);
int	drm_agp_acquire(struct drm_device *);
int	drm_agp_release(struct drm_device *);
int	drm_agp_info(struct drm_device *, struct drm_agp_info *);
int	drm_agp_enable(struct drm_device *, struct drm_agp_mode);
void	*drm_agp_allocate_memory(size_t, u32);
int	drm_agp_free_memory(void *);
int	drm_agp_bind_memory(void *, off_t);
int	drm_agp_unbind_memory(void *);
int	drm_agp_alloc(struct drm_device *, struct drm_agp_buffer *);
int	drm_agp_free(struct drm_device *, struct drm_agp_buffer *);
int	drm_agp_bind(struct drm_device *, struct drm_agp_binding *);
int	drm_agp_unbind(struct drm_device *, struct drm_agp_binding *);

/* Scatter Gather Support (drm_scatter.c) */
void	drm_sg_cleanup(struct drm_device *, struct drm_sg_mem *);
int	drm_sg_alloc(struct drm_device *, struct drm_scatter_gather *);

/* ATI PCIGART support (ati_pcigart.c) */
int	drm_ati_pcigart_init(struct drm_device *,
	    struct drm_ati_pcigart_info *);
int	drm_ati_pcigart_cleanup(struct drm_device *,
	    struct drm_ati_pcigart_info *);

/* Locking IOCTL support (drm_drv.c) */
int	drm_lock(struct drm_device *, void *, struct drm_file *);
int	drm_unlock(struct drm_device *, void *, struct drm_file *);

/* Context IOCTL support (drm_context.c) */
int	drm_resctx(struct drm_device *, void *, struct drm_file *);
int	drm_addctx(struct drm_device *, void *, struct drm_file *);
int	drm_getctx(struct drm_device *, void *, struct drm_file *);
int	drm_rmctx(struct drm_device *, void *, struct drm_file *);

/* IRQ support (drm_irq.c) */
int	drm_control(struct drm_device *, void *, struct drm_file *);
int	drm_wait_vblank(struct drm_device *, void *, struct drm_file *);
int	drm_irq_by_busid(struct drm_device *, void *, struct drm_file *);

/* AGP/GART support (drm_agpsupport.c) */
int	drm_agp_acquire_ioctl(struct drm_device *, void *, struct drm_file *);
int	drm_agp_release_ioctl(struct drm_device *, void *, struct drm_file *);
int	drm_agp_enable_ioctl(struct drm_device *, void *, struct drm_file *);
int	drm_agp_info_ioctl(struct drm_device *, void *, struct drm_file *);
int	drm_agp_alloc_ioctl(struct drm_device *, void *, struct drm_file *);
int	drm_agp_free_ioctl(struct drm_device *, void *, struct drm_file *);
int	drm_agp_unbind_ioctl(struct drm_device *, void *, struct drm_file *);
int	drm_agp_bind_ioctl(struct drm_device *, void *, struct drm_file *);

/* Scatter Gather Support (drm_scatter.c) */
int	drm_sg_alloc_ioctl(struct drm_device *, void *, struct drm_file *);
int	drm_sg_free(struct drm_device *, void *, struct drm_file *);

#endif /* __KERNEL__ */
#endif /* _DRM_P_H_ */
