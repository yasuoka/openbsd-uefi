/* $OpenBSD: softraidvar.h,v 1.86 2009/12/07 14:33:38 jsing Exp $ */
/*
 * Copyright (c) 2006 Marco Peereboom <marco@peereboom.us>
 * Copyright (c) 2008 Chris Kuethe <ckuethe@openbsd.org>
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

#ifndef SOFTRAIDVAR_H
#define SOFTRAIDVAR_H

#include <crypto/md5.h>
#include <sys/vnode.h>

#define SR_UUID_MAX		16
struct sr_uuid {
	u_int8_t		sui_id[SR_UUID_MAX];
} __packed;

#define SR_HOTSPARE_LEVEL	0xffffffff
#define SR_HOTSPARE_VOLID	0xffffffff

#define SR_META_SIZE		64	/* save space at chunk beginning */
#define SR_META_OFFSET		16	/* skip 8192 bytes at chunk beginning */
#define SR_META_VERSION		3	/* bump when sr_metadata changes */
struct sr_metadata {
	struct sr_meta_invariant {
		/* do not change order of ssd_magic, ssd_version */
		u_int64_t	ssd_magic;	/* magic id */
#define	SR_MAGIC		0x4d4152436372616dLLU
		u_int32_t	ssd_version; 	/* meta data version */
		u_int32_t	ssd_flags;
		struct sr_uuid	ssd_uuid;	/* unique identifier */

		/* chunks */
		u_int32_t	ssd_chunk_no;	/* number of chunks */
		u_int32_t	ssd_chunk_id;	/* chunk identifier */

		/* optional */
		u_int32_t	ssd_opt_no;	/* nr of optional md elements */
		u_int32_t	ssd_pad;

		/* volume metadata */
		u_int32_t	ssd_volid;	/* volume id */
		u_int32_t	ssd_level;	/* raid level */
		int64_t		ssd_size;	/* virt disk size in blocks */
		char		ssd_vendor[8];	/* scsi vendor */
		char		ssd_product[16];/* scsi product */
		char		ssd_revision[4];/* scsi revision */
		/* optional volume members */
		u_int32_t	ssd_strip_size;	/* strip size */
	} _sdd_invariant;
#define ssdi			_sdd_invariant
	/* MD5 of invariant metadata */
	u_int8_t		ssd_checksum[MD5_DIGEST_LENGTH];
	char			ssd_devname[32];/* /dev/XXXXX */
	u_int32_t		ssd_meta_flags;
#define	SR_META_DIRTY		0x1
	u_int32_t		ssd_pad;
	u_int64_t		ssd_ondisk;	/* on disk version counter */
	int64_t			ssd_rebuild;	/* last block of rebuild */
} __packed;

struct sr_meta_chunk {
	struct sr_meta_chunk_invariant {
		u_int32_t	scm_volid;	/* vd we belong to */
		u_int32_t	scm_chunk_id;	/* chunk id */
		char		scm_devname[32];/* /dev/XXXXX */
		int64_t		scm_size;	/* size of partition in blocks*/
		int64_t		scm_coerced_size; /* coerced sz of part in blk*/
		struct sr_uuid	scm_uuid;	/* unique identifier */
	} _scm_invariant;
#define scmi			_scm_invariant
	/* MD5 of invariant chunk metadata */
	u_int8_t		scm_checksum[MD5_DIGEST_LENGTH];
	u_int32_t		scm_status;	/* use bio bioc_disk status */
} __packed;

#define SR_CRYPTO_MAXKEYBYTES	32	/* max bytes in a key (AES-XTS-256) */
#define SR_CRYPTO_MAXKEYS	32	/* max keys per volume */
#define SR_CRYPTO_KEYBITS	512	/* AES-XTS with 2 * 256 bit keys */
#define SR_CRYPTO_KEYBYTES	(SR_CRYPTO_KEYBITS >> 3)
#define SR_CRYPTO_KDFHINTBYTES	256	/* size of opaque KDF hint */
#define SR_CRYPTO_CHECKBYTES	64	/* size of generic key chksum struct */
#define SR_CRYPTO_KEY_BLKSHIFT	30	/* 0.5TB per key */

/*
 * Check that HMAC-SHA1_k(decrypted scm_key) == sch_mac, where
 * k = SHA1(masking key)
 */
struct sr_crypto_chk_hmac_sha1 {
	u_int8_t	sch_mac[20];
} __packed;

struct sr_meta_crypto {
	u_int32_t		scm_alg;	/* vol crypto algorithm */
#define SR_CRYPTOA_AES_XTS_128	1
#define SR_CRYPTOA_AES_XTS_256	2
	u_int32_t		scm_flags;	/* key & kdfhint valid */
#define SR_CRYPTOF_INVALID	(0)
#define SR_CRYPTOF_KEY		(1<<0)
#define SR_CRYPTOF_KDFHINT	(1<<1)
	u_int32_t		scm_mask_alg;	/* disk key masking crypt alg */
#define SR_CRYPTOM_AES_ECB_256	1
	u_int32_t		scm_pad1;
	u_int8_t		scm_reserved[64];

	/* symmetric keys used for disk encryption */
	u_int8_t		scm_key[SR_CRYPTO_MAXKEYS][SR_CRYPTO_KEYBYTES];
	/* hint to kdf algorithm (opaque to kernel) */
	u_int8_t		scm_kdfhint[SR_CRYPTO_KDFHINTBYTES];

	u_int32_t		scm_check_alg;	/* key chksum algorithm */
#define SR_CRYPTOC_HMAC_SHA1		1
	u_int32_t		scm_pad2;
	union {
		struct sr_crypto_chk_hmac_sha1	chk_hmac_sha1;
		u_int8_t			chk_reserved2[64];
	}			_scm_chk;
#define	chk_hmac_sha1	_scm_chk.chk_hmac_sha1
} __packed;

struct sr_meta_opt {
	struct sr_meta_opt_invariant {
		u_int32_t	som_type;	/* optional type */
#define SR_OPT_INVALID		0x00
#define SR_OPT_CRYPTO		0x01
		u_int32_t	som_pad;
		union {
			struct sr_meta_crypto smm_crypto;
		}		som_meta;
	} _som_invariant;
#define somi			_som_invariant
#define somi_crypto		_som_invariant.smm_crypto
	/* MD5 of invariant optional metadata */
	u_int8_t		som_checksum[MD5_DIGEST_LENGTH];
} __packed;

/* this is a generic hint for KDF done in userland, not interpreted by the kernel. */
struct sr_crypto_genkdf {
	u_int32_t	len;
	u_int32_t	type;
#define SR_CRYPTOKDFT_INVALID	(0)
#define SR_CRYPTOKDFT_PBKDF2	(1<<0)
};

/* this is a hint for KDF using PKCS#5.  Not interpreted by the kernel */
struct sr_crypto_kdf_pbkdf2 {
	u_int32_t	len;
	u_int32_t	type;
	u_int32_t	rounds;
	u_int8_t	salt[128];
};

/*
 * this structure is used to copy masking keys and KDF hints from/to userland.
 * the embedded hint structures are not interpreted by the kernel.
 */
struct sr_crypto_kdfinfo {
	u_int32_t	len;
	u_int32_t	flags;
#define SR_CRYPTOKDF_INVALID	(0)
#define SR_CRYPTOKDF_KEY	(1<<0)
#define SR_CRYPTOKDF_HINT	(1<<1)
	u_int8_t	maskkey[SR_CRYPTO_MAXKEYBYTES];
	union {
		struct sr_crypto_genkdf		generic;
		struct sr_crypto_kdf_pbkdf2	pbkdf2;
	}		_kdfhint;
#define genkdf		_kdfhint.generic
#define pbkdf2		_kdfhint.pbkdf2
};

#define SR_IOCTL_GET_KDFHINT		0x01	/* Get KDF hint. */
#define SR_IOCTL_CHANGE_PASSPHRASE	0x02	/* Change passphase. */

struct sr_crypto_kdfpair {
	void		*kdfinfo1;
	u_int32_t	kdfsize1;
	void		*kdfinfo2;
	u_int32_t	kdfsize2;
};

#ifdef _KERNEL
#include <dev/biovar.h>

#include <sys/buf.h>
#include <sys/pool.h>
#include <sys/queue.h>
#include <sys/rwlock.h>

#include <scsi/scsi_all.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>

#define DEVNAME(_s)     ((_s)->sc_dev.dv_xname)

/* #define SR_DEBUG */
#ifdef SR_DEBUG
extern u_int32_t		sr_debug;
#define DPRINTF(x...)		do { if (sr_debug) printf(x); } while(0)
#define DNPRINTF(n,x...)	do { if (sr_debug & n) printf(x); } while(0)
#define	SR_D_CMD		0x0001
#define	SR_D_INTR		0x0002
#define	SR_D_MISC		0x0004
#define	SR_D_IOCTL		0x0008
#define	SR_D_CCB		0x0010
#define	SR_D_WU			0x0020
#define	SR_D_META		0x0040
#define	SR_D_DIS		0x0080
#define	SR_D_STATE		0x0100
#else
#define DPRINTF(x...)
#define DNPRINTF(n,x...)
#endif

#define	SR_MAXFER		MAXPHYS
#define	SR_MAX_LD		1
#define	SR_MAX_CMDS		16
#define	SR_MAX_STATES		7
#define SR_VM_IGNORE_DIRTY	1
#define SR_REBUILD_IO_SIZE	128 /* blocks */

/* forward define to prevent dependency goo */
struct sr_softc;

struct sr_ccb {
	struct buf		ccb_buf;	/* MUST BE FIRST!! */

	struct sr_workunit	*ccb_wu;
	struct sr_discipline	*ccb_dis;

	int			ccb_target;
	int			ccb_state;
#define SR_CCB_FREE		0
#define SR_CCB_INPROGRESS	1
#define SR_CCB_OK		2
#define SR_CCB_FAILED		3

	int			ccb_flag;
#define SR_CCBF_FREEBUF		(1<<0)		/* free ccb_buf.b_data */

	void			*ccb_opaque; /* discipline usable pointer */

	TAILQ_ENTRY(sr_ccb)	ccb_link;
};

TAILQ_HEAD(sr_ccb_list, sr_ccb);

struct sr_workunit {
	struct scsi_xfer	*swu_xs;
	struct sr_discipline	*swu_dis;

	int			swu_state;
#define SR_WU_FREE		0
#define SR_WU_INPROGRESS	1
#define SR_WU_OK		2
#define SR_WU_FAILED		3
#define SR_WU_PARTIALLYFAILED	4
#define SR_WU_DEFERRED		5
#define SR_WU_PENDING		6
#define SR_WU_RESTART		7
#define SR_WU_REQUEUE		8

	int			swu_flags;	/* additional hints */
#define SR_WUF_REBUILD		(1<<0)		/* rebuild io */
#define SR_WUF_REBUILDIOCOMP	(1<<1)		/* rbuild io complete */
#define SR_WUF_FAIL		(1<<2)		/* RAID6: failure */
#define SR_WUF_FAILIOCOMP	(1<<3)

	int			swu_fake;	/* faked wu */
	/* workunit io range */
	daddr64_t		swu_blk_start;
	daddr64_t		swu_blk_end;

	/* in flight totals */
	u_int32_t		swu_ios_complete;
	u_int32_t		swu_ios_failed;
	u_int32_t		swu_ios_succeeded;

	/* number of ios that makes up the whole work unit */
	u_int32_t		swu_io_count;

	/* colliding wu */
	struct sr_workunit	*swu_collider;

	/* all ios that make up this workunit */
	struct sr_ccb_list	swu_ccb;

	TAILQ_ENTRY(sr_workunit) swu_link;
};

TAILQ_HEAD(sr_wu_list, sr_workunit);

/* RAID 0 */
#define SR_RAID0_NOWU		16
struct sr_raid0 {
	int32_t			sr0_strip_bits;
};

/* RAID 1 */
#define SR_RAID1_NOWU		16
struct sr_raid1 {
	u_int32_t		sr1_counter;
};

/* RAID 4 */
#define SR_RAIDP_NOWU		16
struct sr_raidp {
	int32_t			srp_strip_bits;
};

/* RAID 6 */
#define SR_RAID6_NOWU		16
struct sr_raid6 {
	int32_t			sr6_strip_bits;
};

/* CRYPTO */
#define SR_CRYPTO_NOWU		16
struct sr_crypto {
	struct sr_meta_crypto	scr_meta;

	struct pool		sr_uiopl;
	struct pool		sr_iovpl;

	/* XXX only keep scr_sid over time */
	u_int8_t		scr_key[SR_CRYPTO_MAXKEYS][SR_CRYPTO_KEYBYTES];
	u_int8_t		scr_maskkey[SR_CRYPTO_MAXKEYBYTES];
	u_int64_t		scr_sid[SR_CRYPTO_MAXKEYS];
};

/* ata over ethernet */
#define SR_RAIDAOE_NOWU		2
struct sr_aoe {
	struct aoe_handler	*sra_ah;
	int			sra_tag;
	struct ifnet		*sra_ifp;
	char			sra_eaddr[6];
};

struct sr_metadata_list {
	u_int8_t		sml_metadata[SR_META_SIZE * 512];
	dev_t			sml_mm;
	struct vnode		*sml_vn;
	u_int32_t		sml_chunk_id;
	int			sml_used;

	SLIST_ENTRY(sr_metadata_list) sml_link;
};

SLIST_HEAD(sr_metadata_list_head, sr_metadata_list);

struct sr_boot_volume {
	struct sr_uuid		sbv_uuid;	/* Volume UUID. */
	u_int32_t		sbv_level;	/* Level. */
	u_int32_t		sbv_volid;	/* Volume ID. */
	u_int32_t		sbv_chunk_no;	/* Number of chunks. */
	u_int32_t		sbv_dev_no;	/* Number of devs discovered. */

	struct sr_metadata_list_head	sml;	/* List of metadata. */

	SLIST_ENTRY(sr_boot_volume)	sbv_link;	
};

SLIST_HEAD(sr_boot_volume_head, sr_boot_volume);

struct sr_chunk {
	struct sr_meta_chunk	src_meta;	/* chunk meta data */
	struct sr_meta_opt	src_opt;	/* optional metadata */

	/* runtime data */
	dev_t			src_dev_mm;	/* major/minor */
	struct vnode		*src_vn;	/* vnode */

	/* helper members before metadata makes it onto the chunk  */
	int			src_meta_ondisk;/* set when meta is on disk */
	char			src_devname[32];
	int64_t			src_size;	/* in blocks */

	SLIST_ENTRY(sr_chunk)	src_link;
};

SLIST_HEAD(sr_chunk_head, sr_chunk);

struct sr_volume {
	/* runtime data */
	struct sr_chunk_head	sv_chunk_list;	/* linked list of all chunks */
	struct sr_chunk		**sv_chunks;	/* array to same chunks */

	/* sensors */
	struct ksensor		sv_sensor;
	struct ksensordev	sv_sensordev;
	int			sv_sensor_valid;
};

struct sr_discipline {
	struct sr_softc		*sd_sc;		/* link back to sr softc */
	u_int8_t		sd_type;	/* type of discipline */
#define	SR_MD_RAID0		0
#define	SR_MD_RAID1		1
#define	SR_MD_RAID5		2
#define	SR_MD_CACHE		3
#define	SR_MD_CRYPTO		4
#define	SR_MD_AOE_INIT		5
#define	SR_MD_AOE_TARG		6
#define	SR_MD_RAID4		7
#define	SR_MD_RAID6		8
	char			sd_name[10];	/* human readable dis name */
	u_int8_t		sd_scsibus;	/* scsibus discipline uses */
	struct scsi_link	sd_link;	/* link to midlayer */

	u_int32_t		sd_capabilities;
#define SR_CAP_SYSTEM_DISK	0x00000001
#define SR_CAP_AUTO_ASSEMBLE	0x00000002
#define SR_CAP_REBUILD		0x00000004

	union {
	    struct sr_raid0	mdd_raid0;
	    struct sr_raid1	mdd_raid1;
	    struct sr_raidp	mdd_raidp;
	    struct sr_raid6	mdd_raid6;
	    struct sr_crypto	mdd_crypto;
#ifdef AOE
	    struct sr_aoe	mdd_aoe;
#endif /* AOE */
	}			sd_dis_specific;/* dis specific members */
#define mds			sd_dis_specific

	/* discipline metadata */
	struct sr_metadata	*sd_meta;	/* in memory copy of metadata */
	void			*sd_meta_foreign; /* non native metadata */
	u_int32_t		sd_meta_flags;
	int			sd_meta_type;	/* metadata functions */

	int			sd_sync;
	int			sd_must_flush;

	int			sd_deleted;

	struct device		*sd_scsibus_dev;
	void			(*sd_shutdownhook)(void *);

	/* discipline volume */
	struct sr_volume	sd_vol;		/* volume associated */
	int			sd_vol_status;	/* runtime vol status */
	/* discipline resources */
	struct sr_ccb		*sd_ccb;
	struct sr_ccb_list	sd_ccb_freeq;
	u_int32_t		sd_max_ccb_per_wu;

	struct sr_workunit	*sd_wu;		/* all workunits */
	u_int32_t		sd_max_wu;
	int			sd_reb_active;	/* rebuild in progress */
	int			sd_reb_abort;	/* abort rebuild */
	int			sd_ready;	/* fully operational */

	struct sr_wu_list	sd_wu_freeq;	/* free wu queue */
	struct sr_wu_list	sd_wu_pendq;	/* pending wu queue */
	struct sr_wu_list	sd_wu_defq;	/* deferred wu queue */
	int			sd_wu_sleep;	/* wu sleepers counter */

	/* discipline stats */
	int			sd_wu_pending;
	u_int64_t		sd_wu_collisions;

	/* discipline functions */
	int			(*sd_alloc_resources)(struct sr_discipline *);
	int			(*sd_free_resources)(struct sr_discipline *);
	int			(*sd_ioctl_handler)(struct sr_discipline *,
				    struct bioc_discipline *);
	int			(*sd_start_discipline)(struct sr_discipline *);
	void			(*sd_set_chunk_state)(struct sr_discipline *,
				    int, int);
	void			(*sd_set_vol_state)(struct sr_discipline *);
	int			(*sd_openings)(struct sr_discipline *);

	/* SCSI emulation */
	struct scsi_sense_data	sd_scsi_sense;
	int			(*sd_scsi_rw)(struct sr_workunit *);
	int			(*sd_scsi_sync)(struct sr_workunit *);
	int			(*sd_scsi_tur)(struct sr_workunit *);
	int			(*sd_scsi_start_stop)(struct sr_workunit *);
	int			(*sd_scsi_inquiry)(struct sr_workunit *);
	int			(*sd_scsi_read_cap)(struct sr_workunit *);
	int			(*sd_scsi_req_sense)(struct sr_workunit *);

	/* background operation */
	struct proc		*sd_background_proc;
};

struct sr_softc {
	struct device		sc_dev;

	int			(*sc_ioctl)(struct device *, u_long, caddr_t);

	struct rwlock		sc_lock;

	struct sr_chunk_head	sc_hotspare_list;	/* List of hotspares. */
	struct sr_chunk		**sc_hotspares;	/* Array to hotspare chunks. */
	struct rwlock		sc_hs_lock;	/* Lock for hotspares list. */
	int			sc_hotspare_no; /* Number of hotspares. */

	int			sc_sensors_running;
	/*
	 * during scsibus attach this is the discipline that is in use
	 * this variable is protected by sc_lock and splhigh
	 */
	struct sr_discipline	*sc_attach_dis;

	/*
	 * XXX expensive, alternative would be nice but has to be cheap
	 * since the scsibus lookup happens on each IO
	 */
#define SR_MAXSCSIBUS		256
	struct sr_discipline	*sc_dis[SR_MAXSCSIBUS]; /* scsibus is u_int8_t */
};

/* hotplug */
void			sr_hotplug_register(struct sr_discipline *, void *);
void			sr_hotplug_unregister(struct sr_discipline *, void *);

/* Hotspare and rebuild. */
void			sr_hotspare_rebuild_callback(void *, void *);

/* work units & ccbs */
int			sr_ccb_alloc(struct sr_discipline *);
void			sr_ccb_free(struct sr_discipline *);
struct sr_ccb		*sr_ccb_get(struct sr_discipline *);
void			sr_ccb_put(struct sr_ccb *);
int			sr_wu_alloc(struct sr_discipline *);
void			sr_wu_free(struct sr_discipline *);
struct sr_workunit	*sr_wu_get(struct sr_discipline *, int);
void			sr_wu_put(struct sr_workunit *);

/* misc functions */
int32_t			sr_validate_stripsize(u_int32_t);
void			sr_meta_save_callback(void *, void *);
int			sr_meta_save(struct sr_discipline *, u_int32_t);
void			sr_checksum(struct sr_softc *, void *, void *,
			    u_int32_t);
int			sr_validate_io(struct sr_workunit *, daddr64_t *,
			    char *);
int			sr_check_io_collision(struct sr_workunit *);
void			sr_scsi_done(struct sr_discipline *,
			    struct scsi_xfer *);

/* discipline functions */
int			sr_raid_inquiry(struct sr_workunit *);
int			sr_raid_read_cap(struct sr_workunit *);
int			sr_raid_tur(struct sr_workunit *);
int			sr_raid_request_sense( struct sr_workunit *);
int			sr_raid_start_stop(struct sr_workunit *);
int			sr_raid_sync(struct sr_workunit *);
void			sr_raid_startwu(struct sr_workunit *);

/* Discipline specific initialisation. */
void			sr_raid0_discipline_init(struct sr_discipline *);
void			sr_raid1_discipline_init(struct sr_discipline *);
void			sr_raidp_discipline_init(struct sr_discipline *,
			    u_int8_t);
void			sr_raid6_discipline_init(struct sr_discipline *);
void			sr_crypto_discipline_init(struct sr_discipline *);
void			sr_aoe_discipline_init(struct sr_discipline *);
void			sr_aoe_server_discipline_init(struct sr_discipline *);

/* raid 1 */
/* XXX - currently (ab)used by AOE and CRYPTO. */
void			sr_raid1_set_chunk_state(struct sr_discipline *,
			    int, int);
void			sr_raid1_set_vol_state(struct sr_discipline *);

/* Crypto discipline hooks. */
int			sr_crypto_get_kdf(struct bioc_createraid *,
			    struct sr_discipline *);
int			sr_crypto_create_keys(struct sr_discipline *);

#ifdef SR_DEBUG
void			sr_dump_mem(u_int8_t *, int);
#endif

#endif /* _KERNEL */

#endif /* SOFTRAIDVAR_H */
