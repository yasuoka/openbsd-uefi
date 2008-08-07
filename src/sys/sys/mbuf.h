/*	$OpenBSD: mbuf.h,v 1.101 2008/08/07 18:33:49 henning Exp $	*/
/*	$NetBSD: mbuf.h,v 1.19 1996/02/09 18:25:14 christos Exp $	*/

/*
 * Copyright (c) 1982, 1986, 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)mbuf.h	8.5 (Berkeley) 2/19/95
 */

#include <sys/malloc.h>
#include <sys/pool.h>
#include <sys/queue.h>

/*
 * Mbufs are of a single size, MSIZE (machine/param.h), which
 * includes overhead.  An mbuf may add a single "mbuf cluster" of size
 * MCLBYTES (also in machine/param.h), which has no additional overhead
 * and is used instead of the internal data area; this is done when
 * at least MINCLSIZE of data must be stored.
 */

#define	MLEN		(MSIZE - sizeof(struct m_hdr))	/* normal data len */
#define	MHLEN		(MLEN - sizeof(struct pkthdr))	/* data len w/pkthdr */

/* smallest amount to put in cluster */
#define	MINCLSIZE	(MHLEN + MLEN + 1)
#define	M_MAXCOMPRESS	(MHLEN / 2)	/* max amount to copy for compression */

/* Packet tags structure */
struct m_tag {
	SLIST_ENTRY(m_tag)	m_tag_link;	/* List of packet tags */
	u_int16_t		m_tag_id;	/* Tag ID */
	u_int16_t		m_tag_len;	/* Length of data */
};

/*
 * Macros for type conversion
 * mtod(m,t) -	convert mbuf pointer to data pointer of correct type
 */
#define	mtod(m,t)	((t)((m)->m_data))

/* header at beginning of each mbuf: */
struct m_hdr {
	struct	mbuf *mh_next;		/* next buffer in chain */
	struct	mbuf *mh_nextpkt;	/* next chain in queue/record */
	caddr_t	mh_data;		/* location of data */
	u_int	mh_len;			/* amount of data in this mbuf */
	short	mh_type;		/* type of data in this mbuf */
	u_short	mh_flags;		/* flags; see below */
};

/* pf stuff */
struct pkthdr_pf {
	void		*hdr;		/* saved hdr pos in mbuf, for ECN */
	void		*statekey;	/* pf stackside statekey */
	u_int		 rtableid;	/* alternate routing table id */
	u_int32_t	 qid;		/* queue id */
	u_int16_t	 tag;		/* tag id */
	u_int8_t	 flags;
	u_int8_t	 routed;
};

/* pkthdr_pf.flags */
#define	PF_TAG_GENERATED		0x01
#define	PF_TAG_FRAGCACHE		0x02
#define	PF_TAG_TRANSLATE_LOCALHOST	0x04
#define	PF_TAG_DIVERTED			0x08

/* record/packet header in first mbuf of chain; valid if M_PKTHDR set */
struct	pkthdr {
	struct ifnet		*rcvif;		/* rcv interface */
	SLIST_HEAD(packet_tags, m_tag) tags; /* list of packet tags */
	int			 len;		/* total packet length */
	int			 csum_flags;	/* checksum flags */
	struct pkthdr_pf	 pf;
};

/* description of external storage mapped into mbuf, valid if M_EXT set */
struct m_ext {
	caddr_t	ext_buf;		/* start of buffer */
					/* free routine if not the usual */
	void	(*ext_free)(caddr_t, u_int, void *);
	void	*ext_arg;		/* argument for ext_free */
	u_int	ext_size;		/* size of buffer, for ext_free */
	int	ext_type;
	struct mbuf *ext_nextref;
	struct mbuf *ext_prevref;
#ifdef DEBUG
	const char *ext_ofile;
	const char *ext_nfile;
	int ext_oline;
	int ext_nline;
#endif
};

struct mbuf {
	struct	m_hdr m_hdr;
	union {
		struct {
			struct	pkthdr MH_pkthdr;	/* M_PKTHDR set */
			union {
				struct	m_ext MH_ext;	/* M_EXT set */
				char	MH_databuf[MHLEN];
			} MH_dat;
		} MH;
		char	M_databuf[MLEN];		/* !M_PKTHDR, !M_EXT */
	} M_dat;
};
#define	m_next		m_hdr.mh_next
#define	m_len		m_hdr.mh_len
#define	m_data		m_hdr.mh_data
#define	m_type		m_hdr.mh_type
#define	m_flags		m_hdr.mh_flags
#define	m_nextpkt	m_hdr.mh_nextpkt
#define	m_act		m_nextpkt
#define	m_pkthdr	M_dat.MH.MH_pkthdr
#define	m_ext		M_dat.MH.MH_dat.MH_ext
#define	m_pktdat	M_dat.MH.MH_dat.MH_databuf
#define	m_dat		M_dat.M_databuf

/* mbuf flags */
#define	M_EXT		0x0001	/* has associated external storage */
#define	M_PKTHDR	0x0002	/* start of record */
#define	M_EOR		0x0004	/* end of record */
#define M_CLUSTER	0x0008	/* external storage is a cluster */
#define	M_PROTO1	0x0010	/* protocol-specific */

/* mbuf pkthdr flags, also in m_flags */
#define	M_BCAST		0x0100	/* send/received as link-level broadcast */
#define	M_MCAST		0x0200	/* send/received as link-level multicast */
#define M_CONF		0x0400  /* payload was encrypted (ESP-transport) */
#define M_AUTH		0x0800  /* payload was authenticated (AH or ESP auth) */
#define M_AUTH_AH	0x2000  /* header was authenticated (AH) */
#define M_TUNNEL	0x1000  /* IP-in-IP added by tunnel mode IPsec */
#define M_ANYCAST6	0x4000	/* received as IPv6 anycast */
#define M_LINK0		0x8000	/* link layer specific flag */
#define M_LOOP		0x0040	/* for Mbuf statistics */
#define M_FILDROP	0x0080	/* dropped by bpf filter */

/* flags copied when copying m_pkthdr */
#define	M_COPYFLAGS	(M_PKTHDR|M_EOR|M_PROTO1|M_BCAST|M_MCAST|M_CONF|\
			 M_AUTH|M_ANYCAST6|M_LOOP|M_TUNNEL|M_LINK0|M_FILDROP)

/* Checksumming flags */
#define	M_IPV4_CSUM_OUT		0x0001	/* IPv4 checksum needed */
#define M_TCPV4_CSUM_OUT	0x0002	/* TCP checksum needed */
#define	M_UDPV4_CSUM_OUT	0x0004	/* UDP checksum needed */
#define	M_IPV4_CSUM_IN_OK	0x0008	/* IPv4 checksum verified */
#define	M_IPV4_CSUM_IN_BAD	0x0010	/* IPv4 checksum bad */
#define	M_TCP_CSUM_IN_OK	0x0020	/* TCP/IPv4 checksum verified */
#define	M_TCP_CSUM_IN_BAD	0x0040	/* TCP/IPv4 checksum bad */
#define	M_UDP_CSUM_IN_OK	0x0080	/* UDP/IPv4 checksum verified */
#define	M_UDP_CSUM_IN_BAD	0x0100	/* UDP/IPv4 checksum bad */

/* mbuf types */
#define	MT_FREE		0	/* should be on free list */
#define	MT_DATA		1	/* dynamic (data) allocation */
#define	MT_HEADER	2	/* packet header */
#define	MT_SONAME	3	/* socket name */
#define	MT_SOOPTS	4	/* socket options */
#define	MT_FTABLE	5	/* fragment reassembly header */
#define	MT_CONTROL	6	/* extra-data protocol message */
#define	MT_OOBDATA	7	/* expedited data  */

/* flags to m_get/MGET */
#define	M_DONTWAIT	M_NOWAIT
#define	M_WAIT		M_WAITOK

/*
 * mbuf utility macros:
 *
 *	MBUFLOCK(code)
 * prevents a section of code from from being interrupted by network
 * drivers.
 */
#define	MBUFLOCK(code) do {						\
	int ms = splvm();						\
	{ code }							\
	splx(ms);							\
} while(/* CONSTCOND */ 0)

/*
 * mbuf allocation/deallocation macros:
 *
 *	MGET(struct mbuf *m, int how, int type)
 * allocates an mbuf and initializes it to contain internal data.
 *
 *	MGETHDR(struct mbuf *m, int how, int type)
 * allocates an mbuf and initializes it to contain a packet header
 * and internal data.
 */
#define MGET(m, how, type) m = m_get((how), (type))

#define MGETHDR(m, how, type) m = m_gethdr((how), (type))

/*
 * Macros for tracking external storage associated with an mbuf.
 *
 * Note: add and delete reference must be called at splvm().
 */
#ifdef DEBUG
#define MCLREFDEBUGN(m, file, line) do {				\
		(m)->m_ext.ext_nfile = (file);				\
		(m)->m_ext.ext_nline = (line);				\
	} while (/* CONSTCOND */ 0)
#define MCLREFDEBUGO(m, file, line) do {				\
		(m)->m_ext.ext_ofile = (file);				\
		(m)->m_ext.ext_oline = (line);				\
	} while (/* CONSTCOND */ 0)
#else
#define MCLREFDEBUGN(m, file, line)
#define MCLREFDEBUGO(m, file, line)
#endif

#define	MCLISREFERENCED(m)	((m)->m_ext.ext_nextref != (m))

#define	_MCLDEREFERENCE(m)	do {					\
		(m)->m_ext.ext_nextref->m_ext.ext_prevref =		\
			(m)->m_ext.ext_prevref;				\
		(m)->m_ext.ext_prevref->m_ext.ext_nextref =		\
			(m)->m_ext.ext_nextref;				\
	} while (/* CONSTCOND */ 0)

#define	_MCLADDREFERENCE(o, n)	do {					\
		(n)->m_flags |= ((o)->m_flags & (M_EXT|M_CLUSTER));	\
		(n)->m_ext.ext_nextref = (o)->m_ext.ext_nextref;	\
		(n)->m_ext.ext_prevref = (o);				\
		(o)->m_ext.ext_nextref = (n);				\
		(n)->m_ext.ext_nextref->m_ext.ext_prevref = (n);	\
		MCLREFDEBUGN((n), __FILE__, __LINE__);			\
	} while (/* CONSTCOND */ 0)

#define	MCLINITREFERENCE(m)	do {					\
		(m)->m_ext.ext_prevref = (m);				\
		(m)->m_ext.ext_nextref = (m);				\
		MCLREFDEBUGO((m), __FILE__, __LINE__);			\
		MCLREFDEBUGN((m), NULL, 0);				\
	} while (/* CONSTCOND */ 0)

#define	MCLADDREFERENCE(o, n)	MBUFLOCK(_MCLADDREFERENCE((o), (n));)

/*
 * Macros for mbuf external storage.
 *
 * MEXTMALLOC allocates external storage and adds it to
 * a normal mbuf; the flag M_EXT is set upon success.
 *
 * MEXTADD adds pre-allocated external storage to
 * a normal mbuf; the flag M_EXT is set.
 *
 * MCLGET allocates and adds an mbuf cluster to a normal mbuf;
 * the flag M_EXT is set upon success.
 */
#define	MEXTMALLOC(m, size, how) do {					\
	(m)->m_ext.ext_buf =						\
	    (caddr_t)malloc((size), mbtypes[(m)->m_type], (how));	\
	if ((m)->m_ext.ext_buf != NULL) {				\
		(m)->m_data = (m)->m_ext.ext_buf;			\
		(m)->m_flags |= M_EXT;					\
		(m)->m_flags &= ~M_CLUSTER;				\
		(m)->m_ext.ext_size = (size);				\
		(m)->m_ext.ext_free = NULL;				\
		(m)->m_ext.ext_arg = NULL;				\
		(m)->m_ext.ext_type = mbtypes[(m)->m_type];		\
		MCLINITREFERENCE(m);					\
	}								\
} while (/* CONSTCOND */ 0)

#define	MEXTADD(m, buf, size, type, free, arg) do {			\
	(m)->m_data = (m)->m_ext.ext_buf = (caddr_t)(buf);		\
	(m)->m_flags |= M_EXT;						\
	(m)->m_flags &= ~M_CLUSTER;					\
	(m)->m_ext.ext_size = (size);					\
	(m)->m_ext.ext_free = (free);					\
	(m)->m_ext.ext_arg = (arg);					\
	(m)->m_ext.ext_type = (type);					\
	MCLINITREFERENCE(m);						\
} while (/* CONSTCOND */ 0)

#define MCLGET(m, how) m_clget((m), (how))

/*
 * Reset the data pointer on an mbuf.
 */
#define	MRESETDATA(m)							\
do {									\
	if ((m)->m_flags & M_EXT)					\
		(m)->m_data = (m)->m_ext.ext_buf;			\
	else if ((m)->m_flags & M_PKTHDR)				\
		(m)->m_data = (m)->m_pktdat;				\
	else								\
		(m)->m_data = (m)->m_dat;				\
} while (/* CONSTCOND */ 0)

/*
 * MFREE(struct mbuf *m, struct mbuf *n)
 * Free a single mbuf and associated external storage.
 * Place the successor, if any, in n.
 */
#define	MFREE(m, n) n = m_free((m))

/*
 * Move just m_pkthdr from from to to,
 * remove M_PKTHDR and clean the tag for from.
 */
#define M_MOVE_HDR(to, from) {						\
	(to)->m_pkthdr = (from)->m_pkthdr;				\
	(from)->m_flags &= ~M_PKTHDR;					\
	SLIST_INIT(&(from)->m_pkthdr.tags);				\
}

/*
 * Duplicate just m_pkthdr from from to to.
 */
#define M_DUP_HDR(to, from) {						\
	(to)->m_pkthdr = (from)->m_pkthdr;				\
	SLIST_INIT(&(to)->m_pkthdr.tags);				\
	m_tag_copy_chain((to), (from));					\
}

/*
 * Duplicate mbuf pkthdr from from to to.
 * from must have M_PKTHDR set, and to must be empty.
 */
#define M_DUP_PKTHDR(to, from) {					\
	(to)->m_flags = (from)->m_flags & M_COPYFLAGS;			\
	M_DUP_HDR((to), (from));					\
	(to)->m_data = (to)->m_pktdat;					\
}

/*
 * MOVE mbuf pkthdr from from to to.
 * from must have M_PKTHDR set, and to must be empty.
 */
#define	M_MOVE_PKTHDR(to, from) {					\
	(to)->m_flags = (from)->m_flags & M_COPYFLAGS;			\
	M_MOVE_HDR((to), (from));					\
	(to)->m_data = (to)->m_pktdat;					\
}

/*
 * Set the m_data pointer of a newly-allocated mbuf (m_get/MGET) to place
 * an object of the specified size at the end of the mbuf, longword aligned.
 */
#define	M_ALIGN(m, len) \
	{ (m)->m_data += (MLEN - (len)) &~ (sizeof(long) - 1); }
/*
 * As above, for mbufs allocated with m_gethdr/MGETHDR
 * or initialized by M_MOVE_PKTHDR.
 */
#define	MH_ALIGN(m, len) \
	{ (m)->m_data += (MHLEN - (len)) &~ (sizeof(long) - 1); }

/*
 * Determine if an mbuf's data area is read-only. This is true for
 * non-cluster external storage and for clusters that are being
 * referenced by more than one mbuf.
 */
#define	M_READONLY(m)							\
	(((m)->m_flags & M_EXT) != 0 &&					\
	  (((m)->m_flags & M_CLUSTER) == 0 || MCLISREFERENCED(m)))

/*
 * Compute the amount of space available
 * before the current start of data in an mbuf.
 */
#define	M_LEADINGSPACE(m) m_leadingspace(m)

/*
 * Compute the amount of space available
 * after the end of data in an mbuf.
 */
#define	M_TRAILINGSPACE(m) m_trailingspace(m)

/*
 * Arrange to prepend space of size plen to mbuf m.
 * If a new mbuf must be allocated, how specifies whether to wait.
 * If how is M_DONTWAIT and allocation fails, the original mbuf chain
 * is freed and m is set to NULL.
 */
#define	M_PREPEND(m, plen, how) {					\
	if (M_LEADINGSPACE(m) >= (plen)) {				\
		(m)->m_data -= (plen);					\
		(m)->m_len += (plen);					\
	} else								\
		(m) = m_prepend((m), (plen), (how));			\
	if ((m) && (m)->m_flags & M_PKTHDR)				\
		(m)->m_pkthdr.len += (plen);				\
}

/* change mbuf to new type */
#define MCHTYPE(m, t) {							\
	MBUFLOCK(							\
		mbstat.m_mtypes[(m)->m_type]--;				\
		mbstat.m_mtypes[t]++;					\
	);								\
	(m)->m_type = t;						\
}

/* length to m_copy to copy all */
#define	M_COPYALL	1000000000

/* compatibility with 4.3 */
#define  m_copy(m, o, l)	m_copym((m), (o), (l), M_DONTWAIT)

/*
 * Mbuf statistics.
 * For statistics related to mbuf and cluster allocations, see also the
 * pool headers (mbpool and mclpool).
 */
struct mbstat {
	u_long	_m_spare;	/* formerly m_mbufs */
	u_long	_m_spare1;	/* formerly m_clusters */
	u_long	_m_spare2;	/* spare field */
	u_long	_m_spare3;	/* formely m_clfree - free clusters */
	u_long	m_drops;	/* times failed to find space */
	u_long	m_wait;		/* times waited for space */
	u_long	m_drain;	/* times drained protocols for space */
	u_short	m_mtypes[256];	/* type specific mbuf allocations */
};

#ifdef	_KERNEL
extern	struct mbstat mbstat;
extern	int nmbclust;			/* limit on the # of clusters */
extern	int mblowat;			/* mbuf low water mark */
extern	int mcllowat;			/* mbuf cluster low water mark */
extern	int max_linkhdr;		/* largest link-level header */
extern	int max_protohdr;		/* largest protocol header */
extern	int max_hdr;			/* largest link+protocol header */
extern	int max_datalen;		/* MHLEN - max_hdr */
extern	int mbtypes[];			/* XXX */

void	mbinit(void);
struct	mbuf *m_copym2(struct mbuf *, int, int, int);
struct	mbuf *m_copym(struct mbuf *, int, int, int);
struct	mbuf *m_free(struct mbuf *);
struct	mbuf *m_get(int, int);
struct	mbuf *m_getclr(int, int);
struct	mbuf *m_gethdr(int, int);
struct	mbuf *m_inithdr(struct mbuf *);
struct	mbuf *m_prepend(struct mbuf *, int, int);
struct	mbuf *m_pulldown(struct mbuf *, int, int, int *);
struct	mbuf *m_pullup(struct mbuf *, int);
struct	mbuf *m_pullup2(struct mbuf *, int);
struct	mbuf *m_split(struct mbuf *, int, int);
struct  mbuf *m_inject(struct mbuf *, int, int, int);
struct  mbuf *m_getptr(struct mbuf *, int, int *);
int	m_leadingspace(struct mbuf *);
int	m_trailingspace(struct mbuf *);
void	m_clget(struct mbuf *, int);
void	m_adj(struct mbuf *, int);
void	m_copyback(struct mbuf *, int, int, const void *);
void	m_freem(struct mbuf *);
void	m_reclaim(void *, int);
void	m_copydata(struct mbuf *, int, int, caddr_t);
void	m_cat(struct mbuf *, struct mbuf *);
struct mbuf *m_devget(char *, int, int, struct ifnet *,
	    void (*)(const void *, void *, size_t));
void	m_zero(struct mbuf *);
int	m_apply(struct mbuf *, int, int,
	    int (*)(caddr_t, caddr_t, unsigned int), caddr_t);

/* Packet tag routines */
struct m_tag *m_tag_get(int, int, int);
void	m_tag_prepend(struct mbuf *, struct m_tag *);
void	m_tag_delete(struct mbuf *, struct m_tag *);
void	m_tag_delete_chain(struct mbuf *);
struct m_tag *m_tag_find(struct mbuf *, int, struct m_tag *);
struct m_tag *m_tag_copy(struct m_tag *);
int	m_tag_copy_chain(struct mbuf *, struct mbuf *);
void	m_tag_init(struct mbuf *);
struct m_tag *m_tag_first(struct mbuf *);
struct m_tag *m_tag_next(struct mbuf *, struct m_tag *);

/* Packet tag types */
#define PACKET_TAG_NONE				0  /* Nadda */
#define PACKET_TAG_IPSEC_IN_DONE		1  /* IPsec applied, in */
#define PACKET_TAG_IPSEC_OUT_DONE		2  /* IPsec applied, out */
#define PACKET_TAG_IPSEC_IN_CRYPTO_DONE		3  /* NIC IPsec crypto done */
#define PACKET_TAG_IPSEC_OUT_CRYPTO_NEEDED	4  /* NIC IPsec crypto req'ed */
#define PACKET_TAG_IPSEC_IN_COULD_DO_CRYPTO	5  /* NIC notifies IPsec */
#define PACKET_TAG_IPSEC_PENDING_TDB		6  /* Reminder to do IPsec */
#define PACKET_TAG_BRIDGE			7  /* Bridge processing done */
#define PACKET_TAG_GIF				8  /* GIF processing done */
#define PACKET_TAG_GRE				9  /* GRE processing done */
#define PACKET_TAG_IN_PACKET_CHECKSUM		10 /* NIC checksumming done */
#define PACKET_TAG_DLT				17 /* data link layer type */
#define PACKET_TAG_PF_DIVERT			18 /* pf(4) diverted packet */

#ifdef MBTYPES
int mbtypes[] = {				/* XXX */
	M_FREE,		/* MT_FREE	0	   should be on free list */
	M_MBUF,		/* MT_DATA	1	   dynamic (data) allocation */
	M_MBUF,		/* MT_HEADER	2	   packet header */
	M_MBUF,		/* MT_SONAME	8	   socket name */
	M_SOOPTS,	/* MT_SOOPTS	10	   socket options */
	M_FTABLE,	/* MT_FTABLE	11	   fragment reassembly header */
	M_MBUF,		/* MT_CONTROL	14	   extra-data protocol message */
	M_MBUF,		/* MT_OOBDATA	15	   expedited data  */
};
#endif /* MBTYPES */
#endif
