/*	$OpenBSD: ip_ipsp.c,v 1.55 1999/11/04 11:20:05 ho Exp $	*/

/*
 * The authors of this code are John Ioannidis (ji@tla.org),
 * Angelos D. Keromytis (kermit@csd.uch.gr), 
 * Niels Provos (provos@physnet.uni-hamburg.de) and
 * Niklas Hallqvist (niklas@appli.se).
 *
 * This code was written by John Ioannidis for BSD/OS in Athens, Greece, 
 * in November 1995.
 *
 * Ported to OpenBSD and NetBSD, with additional transforms, in December 1996,
 * by Angelos D. Keromytis.
 *
 * Additional transforms and features in 1997 and 1998 by Angelos D. Keromytis
 * and Niels Provos.
 *
 * Additional features in 1999 by Angelos D. Keromytis and Niklas Hallqvist.
 *
 * Copyright (c) 1995, 1996, 1997, 1998, 1999 by John Ioannidis,
 * Angelos D. Keromytis and Niels Provos.
 * Copyright (c) 1999 Niklas Hallqvist.
 *
 * Permission to use, copy, and modify this software without fee
 * is hereby granted, provided that this entire notice is included in
 * all copies of any software which is or includes a copy or
 * modification of this software. 
 * You may use this code under the GNU public license if you so wish. Please
 * contribute changes back to the authors under this freer than GPL license
 * so that we may further the use of strong encryption without limitations to
 * all.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, NONE OF THE AUTHORS MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */

/*
 * IPSP Processing
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <sys/proc.h>

#include <net/if.h>
#include <net/route.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/in_var.h>
#include <netinet/ip_var.h>
#include <netinet/ip_icmp.h>

#include <net/raw_cb.h>
#include <net/pfkeyv2.h>

#include <netinet/ip_ipsp.h>
#include <netinet/ip_ah.h>
#include <netinet/ip_esp.h>

#include <dev/rndvar.h>

#ifdef DDB
#include <ddb/db_output.h>
void tdb_hashstats(void);
#endif

#ifdef ENCDEBUG
#define DPRINTF(x)	if (encdebug) printf x
#else
#define DPRINTF(x)
#endif

#ifdef __GNUC__
#define INLINE static __inline
#endif

int		ipsp_kern __P((int, char **, int));
u_int8_t       	get_sa_require  __P((struct inpcb *));
int		check_ipsec_policy  __P((struct inpcb *, u_int32_t));
void		tdb_rehash __P((void));

extern int	ipsec_auth_default_level;
extern int	ipsec_esp_trans_default_level;
extern int	ipsec_esp_network_default_level;

extern int encdebug;
int ipsec_in_use = 0;
u_int32_t kernfs_epoch = 0;

struct expclusterlist_head expclusterlist =
    TAILQ_HEAD_INITIALIZER(expclusterlist);
struct explist_head explist = TAILQ_HEAD_INITIALIZER(explist);

u_int8_t hmac_ipad_buffer[64] = {
    0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 
    0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 
    0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 
    0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 
    0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 
    0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 
    0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 
    0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36, 0x36 };

u_int8_t hmac_opad_buffer[64] = {
    0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C,
    0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C,
    0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C,
    0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C,
    0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C,
    0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C,
    0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C,
    0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C, 0x5C };

/*
 * This is the proper place to define the various encapsulation transforms.
 */

struct xformsw xformsw[] = {
    { XF_IP4,	         0,               "IPv4 Simple Encapsulation",
      ipe4_attach,       ipe4_init,       ipe4_zeroize,
      (struct mbuf * (*)(struct mbuf *, struct tdb *))ipe4_input, 
      ipe4_output, },
    { XF_OLD_AH,         XFT_AUTH,	 "Keyed Authentication, RFC 1828/1852",
      ah_old_attach,     ah_old_init,     ah_old_zeroize,
      ah_old_input,      ah_old_output, },
    { XF_OLD_ESP,        XFT_CONF,        "Simple Encryption, RFC 1829/1851",
      esp_old_attach,    esp_old_init,    esp_old_zeroize,
      esp_old_input,     esp_old_output, },
    { XF_NEW_AH,	 XFT_AUTH,	  "HMAC Authentication",
      ah_new_attach,	 ah_new_init,     ah_new_zeroize,
      ah_new_input,	 ah_new_output, },
    { XF_NEW_ESP,	 XFT_CONF|XFT_AUTH,
      "Encryption + Authentication + Replay Protection",
      esp_new_attach,	 esp_new_init,  esp_new_zeroize,
      esp_new_input,	 esp_new_output, },
#ifdef TCP_SIGNATURE
    { XF_TCPSIGNATURE,	 XFT_AUTH, "TCP MD5 Signature Option, RFC 2385",
      tcp_signature_tdb_attach, 	tcp_signature_tdb_init,
      tcp_signature_tdb_zeroize,	tcp_signature_tdb_input,
      tcp_signature_tdb_output, }
#endif /* TCP_SIGNATURE */
};

struct xformsw *xformswNXFORMSW = &xformsw[sizeof(xformsw)/sizeof(xformsw[0])];

unsigned char ipseczeroes[IPSEC_ZEROES_SIZE]; /* zeroes! */ 

#define TDB_HASHSIZE_INIT 32
static struct tdb **tdbh = NULL, *tdb_bypass = NULL;
static u_int tdb_hashmask = TDB_HASHSIZE_INIT - 1;
static int tdb_count;

/*
 * Check which transformationes are required
 */

u_int8_t
get_sa_require(struct inpcb *inp)
{
    u_int8_t sareq = 0;
       
    if (inp != NULL)
    {
	sareq |= inp->inp_seclevel[SL_AUTH] >= IPSEC_LEVEL_USE ? 
		 NOTIFY_SATYPE_AUTH : 0;
	sareq |= inp->inp_seclevel[SL_ESP_TRANS] >= IPSEC_LEVEL_USE ?
		 NOTIFY_SATYPE_CONF : 0;
	sareq |= inp->inp_seclevel[SL_ESP_NETWORK] >= IPSEC_LEVEL_USE ?
		 NOTIFY_SATYPE_TUNNEL : 0;
    }
    else
    {
	sareq |= ipsec_auth_default_level >= IPSEC_LEVEL_USE ? 
		 NOTIFY_SATYPE_AUTH : 0;
	sareq |= ipsec_esp_trans_default_level >= IPSEC_LEVEL_USE ? 
		 NOTIFY_SATYPE_CONF : 0;
	sareq |= ipsec_esp_network_default_level >= IPSEC_LEVEL_USE ? 
		 NOTIFY_SATYPE_TUNNEL : 0;
    }
    
    return (sareq);
}

/*
 * Check the socket policy and request a new SA with a key management
 * daemon. Sometime the inp does not contain the destination address
 * in that case use dst.
 */

int
check_ipsec_policy(struct inpcb *inp, u_int32_t daddr)
{
    union sockaddr_union sunion;
    struct socket *so;
    struct route_enc re0, *re = &re0;
    struct sockaddr_encap *dst; 
    u_int8_t sa_require, sa_have;
    int error, i, s;
    struct tdb *tdb = NULL;

    if (inp == NULL || ((so = inp->inp_socket) == 0))
      return (EINVAL);

    /* If IPSEC is not required just use what we got */
    if (!(sa_require = inp->inp_secrequire))
      return 0;

    s = spltdb();
    if (!inp->inp_tdb)
    {
	bzero((caddr_t) re, sizeof(*re));
	dst = (struct sockaddr_encap *) &re->re_dst;
	dst->sen_family = PF_KEY;
	dst->sen_len = SENT_IP4_LEN;
	dst->sen_type = SENT_IP4;
	dst->sen_ip_src = inp->inp_laddr;
	dst->sen_ip_dst.s_addr = inp->inp_faddr.s_addr ? 
	  inp->inp_faddr.s_addr : daddr;
	dst->sen_proto = so->so_proto->pr_protocol;
	switch (dst->sen_proto)
	{
	  case IPPROTO_UDP:
	  case IPPROTO_TCP:
	    dst->sen_sport = inp->inp_lport;
	    dst->sen_dport = inp->inp_fport;
	    break;
	  default:
	    dst->sen_sport = 0;
	    dst->sen_dport = 0;
	}
    
	/* Try to find a flow */
	rtalloc((struct route *) re);
    
	if (re->re_rt != NULL)
	{
	    struct sockaddr_encap *gw;
	    
	    gw = (struct sockaddr_encap *) (re->re_rt->rt_gateway);
	    
	    if (gw->sen_type == SENT_IPSP)
	    {
		bzero(&sunion, sizeof(sunion));
	        sunion.sin.sin_family = AF_INET;
		sunion.sin.sin_len = sizeof(struct sockaddr_in);
		sunion.sin.sin_addr = gw->sen_ipsp_dst;
	      
		tdb = (struct tdb *) gettdb(gw->sen_ipsp_spi, &sunion,
					    gw->sen_ipsp_sproto);
	    }
	    RTFREE(re->re_rt);
	}
    } else
      tdb = inp->inp_tdb;

    if (tdb)
      SPI_CHAIN_ATTRIB(sa_have, tdb_onext, tdb);
    else
      sa_have = 0;
    splx(s);

    /* Check if our requirements are met */
    if (!(sa_require & ~sa_have))
      return 0;

    error = i = 0;

    inp->inp_secresult = SR_WAIT;

    /* If necessary try to notify keymanagement three times */
    while (i < 3)
    {
	/* XXX address */
	DPRINTF(("ipsec: send SA request (%d), remote ip: %s, SA type: %d\n",
		 i + 1, inet_ntoa4(dst->sen_ip_dst), sa_require));

	/* Send notify */
	/* XXX PF_KEYv2 Notify */
	 
	/* 
	 * Wait for the keymanagement daemon to establich a new SA,
	 * even on error check again, perhaps some other process
	 * already established the necessary SA.
	 */
	error = tsleep((caddr_t)inp, PSOCK|PCATCH, "ipsecnotify", 30*hz);
	DPRINTF(("check_ipsec: sleep %d\n", error));

	if (error && error != EWOULDBLOCK)
	  break;
	/* 
	 * A Key Management daemon returned an apropriate SA back
	 * to the kernel, the kernel noted that state in the waiting
	 * socket.
	 */
	if (inp->inp_secresult == SR_SUCCESS)
	  return (0);
	/*
	 * Key Management returned a permanent failure, we do not
	 * need to retry again. XXX - when more than one key
	 * management daemon is available we can not do that.
	 */
	if (inp->inp_secresult == SR_FAILED)
	  break;
	i++;
    }

    return (error ? error : EWOULDBLOCK);
}

/*
 * Add an inpcb to the list of inpcb which reference this tdb directly.
 */

void
tdb_add_inp(struct tdb *tdb, struct inpcb *inp)
{
    int s = spltdb();

    if (inp->inp_tdb)
    {
	if (inp->inp_tdb == tdb)
	{
	    splx(s);
	    return;
	}
	TAILQ_REMOVE(&inp->inp_tdb->tdb_inp, inp, inp_tdb_next);
    }
    inp->inp_tdb = tdb;
    TAILQ_INSERT_TAIL(&tdb->tdb_inp, inp, inp_tdb_next);
    splx(s);

    DPRINTF(("tdb_add_inp: tdb: %p, inp: %p\n", tdb, inp));
}

/*
 * Reserve an SPI; the SA is not valid yet though.  We use SPI_LOCAL_USE as
 * an error return value.  It'll not be a problem that we also use that
 * for demand-keying as that is manually specified.
 */

u_int32_t
reserve_spi(u_int32_t sspi, u_int32_t tspi, union sockaddr_union *src,
	    union sockaddr_union *dst, u_int8_t sproto, int *errval)
{
    struct tdb *tdbp;
    u_int32_t spi;
    int nums, s;

    /* Don't accept ranges only encompassing reserved SPIs.  */
    if (tspi < sspi || tspi <= SPI_RESERVED_MAX)
    {
	(*errval) = EINVAL;
	return 0;
    }

    /* Limit the range to not include reserved areas.  */
    if (sspi <= SPI_RESERVED_MAX)
      sspi = SPI_RESERVED_MAX + 1;

    if (sspi == tspi)   /* Asking for a specific SPI */
      nums = 1;
    else
      nums = 50;  /* XXX figure out some good value */

    while (nums--)
    {
	if (sspi == tspi)  /* Specific SPI asked */
	  spi = tspi;
	else    /* Range specified */
	{
	    get_random_bytes((void *) &spi, sizeof(spi));
	    spi = sspi + (spi % (tspi - sspi));
	}
	  
	/* Don't allocate reserved SPIs.  */
	if (spi == SPI_LOCAL_USE ||
	    (spi >= SPI_RESERVED_MIN && spi <= SPI_RESERVED_MAX))
	  continue;
	else
	  spi = htonl(spi);

	/* Check whether we're using this SPI already */
	s = spltdb();
	tdbp = gettdb(spi, dst, sproto);
	splx(s);

	if (tdbp != (struct tdb *) NULL)
	  continue;
	
	MALLOC(tdbp, struct tdb *, sizeof(struct tdb), M_TDB, M_WAITOK);
	bzero((caddr_t) tdbp, sizeof(struct tdb));
	
	tdbp->tdb_spi = spi;
	bcopy(&dst->sa, &tdbp->tdb_dst.sa, SA_LEN(&dst->sa));
	bcopy(&src->sa, &tdbp->tdb_src.sa, SA_LEN(&src->sa));
	tdbp->tdb_sproto = sproto;
	tdbp->tdb_flags |= TDBF_INVALID;       /* Mark SA as invalid for now */
	tdbp->tdb_satype = SADB_SATYPE_UNSPEC;
	tdbp->tdb_established = time.tv_sec;
	tdbp->tdb_epoch = kernfs_epoch - 1;
	puttdb(tdbp);

	/* XXX Should set up a silent expiration for this */
	
	return spi;
    }

    (*errval) = EEXIST;
    return 0;
}

/*
 * Our hashing function needs to stir things with a non-zero random multiplier
 * so we cannot be DoS-attacked via choosing of the data to hash.
 */
INLINE int
tdb_hash(u_int32_t spi, union sockaddr_union *dst, u_int8_t proto)
{
    static u_int32_t mult1 = 0, mult2 = 0;
    u_int8_t *ptr = (u_int8_t *) dst;
    int i, shift;
    u_int64_t hash;
    int val32 = 0;

    while (mult1 == 0)
	mult1 = arc4random();
    while (mult2 == 0)
	mult2 = arc4random();

    hash = (spi ^ proto) * mult1;
    for (i = 0; i < SA_LEN(&dst->sa); i++)
    {
	val32 = (val32 << 8) | ptr[i];
	if (i % 4 == 3)
	{
	    hash ^= val32 * mult2;
	    val32 = 0;
	}
    }
    if (i % 4 != 0)
	hash ^= val32 * mult2;

    shift = ffs(tdb_hashmask + 1);
    while ((hash & ~tdb_hashmask) != 0)
      hash = (hash >> shift) ^ (hash & tdb_hashmask);

    return hash;
}

/*
 * An IPSP SAID is really the concatenation of the SPI found in the 
 * packet, the destination address of the packet and the IPsec protocol.
 * When we receive an IPSP packet, we need to look up its tunnel descriptor
 * block, based on the SPI in the packet and the destination address (which
 * is really one of our addresses if we received the packet!
 *
 * Caller is responsible for setting at least spltdb().
 */

struct tdb *
gettdb(u_int32_t spi, union sockaddr_union *dst, u_int8_t proto)
{
    u_int32_t hashval;
    struct tdb *tdbp;
    int s;

    if (spi == 0 && proto == 0)
    {
      /* tdb_bypass; a placeholder for bypass flows, allocate on first pass */
      if (tdb_bypass == NULL)
      {
	s = spltdb();
	MALLOC(tdb_bypass, struct tdb *, sizeof(struct tdb), M_TDB, M_WAITOK);
	tdb_count++;
	splx(s);

	bzero(tdb_bypass, sizeof(struct tdb));
	tdb_bypass->tdb_satype = SADB_X_SATYPE_BYPASS;
	tdb_bypass->tdb_established = time.tv_sec;
	tdb_bypass->tdb_epoch = kernfs_epoch - 1;
	tdb_bypass->tdb_flags = 0;
	TAILQ_INIT(&tdb_bypass->tdb_bind_in);
	TAILQ_INIT(&tdb_bypass->tdb_inp);
      }
      return tdb_bypass;
    }

    if (tdbh == NULL)
      return (struct tdb *) NULL;

    hashval = tdb_hash(spi, dst, proto);

    for (tdbp = tdbh[hashval]; tdbp != NULL; tdbp = tdbp->tdb_hnext)
      if ((tdbp->tdb_spi == spi) && 
	  !bcmp(&tdbp->tdb_dst, dst, SA_LEN(&dst->sa)) &&
	  (tdbp->tdb_sproto == proto))
	break;

    return tdbp;
}

#if DDB
void
tdb_hashstats()
{
    int i, cnt, buckets[16];
    struct tdb *tdbp;

    if (tdbh == NULL)
    {
	db_printf("no tdb hash table\n");
	return;
    }

    bzero (buckets, sizeof(buckets));
    for (i = 0; i <= tdb_hashmask; i++)
    {
	cnt = 0;
	for (tdbp = tdbh[i]; cnt < 16 && tdbp != NULL; tdbp = tdbp->tdb_hnext)
	  cnt++;
	buckets[cnt]++;
    }

    db_printf("tdb cnt\t\tbucket cnt\n");
    for (i = 0; i < 16; i++)
      if (buckets[i] > 0)
	db_printf("%d%c\t\t%d\n", i, i == 15 ? "+" : "", buckets[i]);
}
#endif	/* DDB */

/*
 * Caller is responsible for setting at least spltdb().
 */

int
tdb_walk(int (*walker)(struct tdb *, void *), void *arg)
{
    int i, rval = 0;
    struct tdb *tdbp, *next;

    if (tdbh == NULL)
        return ENOENT;

    for (i = 0; i <= tdb_hashmask; i++)
	for (tdbp = tdbh[i]; rval == 0 && tdbp != NULL; tdbp = next)
	{
	    next = tdbp->tdb_hnext;
	    rval = walker(tdbp, (void *)arg);
	}

    return rval;
}

struct flow *
get_flow(void)
{
    struct flow *flow;

    MALLOC(flow, struct flow *, sizeof(struct flow), M_TDB, M_WAITOK);
    bzero(flow, sizeof(struct flow));

    ipsec_in_use++;
    return flow;
}

/*
 * Called at splsoftclock().
 */

void
handle_expirations(void *arg)
{
    struct tdb *tdb;

    for (tdb = TAILQ_FIRST(&explist);
	 tdb && tdb->tdb_timeout <= time.tv_sec;
	 tdb = TAILQ_FIRST(&explist))
    {
	/* Hard expirations first */
	if ((tdb->tdb_flags & TDBF_TIMER) &&
	    (tdb->tdb_exp_timeout <= time.tv_sec))
	{
	    pfkeyv2_expire(tdb, SADB_EXT_LIFETIME_HARD);
	    tdb_delete(tdb, 0, 0);
	    continue;
	}
	else
	  if ((tdb->tdb_flags & TDBF_FIRSTUSE) &&
	      (tdb->tdb_first_use + tdb->tdb_exp_first_use <= time.tv_sec))
	  {
	      pfkeyv2_expire(tdb, SADB_EXT_LIFETIME_HARD);
	      tdb_delete(tdb, 0, 0);
	      continue;
	  }

	/* Soft expirations */
	if ((tdb->tdb_flags & TDBF_SOFT_TIMER) &&
	    (tdb->tdb_soft_timeout <= time.tv_sec))
	{
	    pfkeyv2_expire(tdb, SADB_EXT_LIFETIME_SOFT);
	    tdb->tdb_flags &= ~TDBF_SOFT_TIMER;
	    tdb_expiration(tdb, TDBEXP_EARLY);
	}
	else
	  if ((tdb->tdb_flags & TDBF_SOFT_FIRSTUSE) &&
	      (tdb->tdb_first_use + tdb->tdb_soft_first_use <=
	       time.tv_sec))
	  {
	      pfkeyv2_expire(tdb, SADB_EXT_LIFETIME_SOFT);
	      tdb->tdb_flags &= ~TDBF_SOFT_FIRSTUSE;
	      tdb_expiration(tdb, TDBEXP_EARLY);
	  }
    }

    /* If any tdb is left on the expiration queue, set the timer.  */
    if (tdb)
      timeout(handle_expirations, (void *) NULL, 
	      hz * (tdb->tdb_timeout - time.tv_sec));
}

/*
 * Ensure the tdb is in the right place in the expiration list.
 */

void
tdb_expiration(struct tdb *tdb, int flags)
{
    u_int64_t next_timeout = 0;
    struct tdb *t, *former_expirer, *next_expirer;
    int will_be_first, sole_reason, early;
    int s = spltdb();

    /*
     * If this is the local use SPI, this is an SPD entry, so don't setup any
     * timers.
     */
    if (ntohl(tdb->tdb_spi) == SPI_LOCAL_USE)
    {
	splx(s);
	return;
    }

    /* Find the earliest expiration.  */
    if ((tdb->tdb_flags & TDBF_FIRSTUSE) && tdb->tdb_first_use != 0 &&
	(next_timeout == 0 ||
	 next_timeout > tdb->tdb_first_use + tdb->tdb_exp_first_use))
	next_timeout = tdb->tdb_first_use + tdb->tdb_exp_first_use;
    if ((tdb->tdb_flags & TDBF_SOFT_FIRSTUSE) && tdb->tdb_first_use != 0 &&
	(next_timeout == 0 ||
	 next_timeout > tdb->tdb_first_use + tdb->tdb_soft_first_use))
	next_timeout = tdb->tdb_first_use + tdb->tdb_soft_first_use;
    if ((tdb->tdb_flags & TDBF_TIMER) &&
	(next_timeout == 0 || next_timeout > tdb->tdb_exp_timeout))
	next_timeout = tdb->tdb_exp_timeout;
    if ((tdb->tdb_flags & TDBF_SOFT_TIMER) &&
	(next_timeout == 0 || next_timeout > tdb->tdb_soft_timeout))
	next_timeout = tdb->tdb_soft_timeout;

    /* No change?  */
    if (next_timeout == tdb->tdb_timeout)
    {
      splx(s);
      return;
    }

    /*
     * Find out some useful facts: Will our tdb be first to expire?
     * Was our tdb the sole reason for the old timeout?
     */
    former_expirer = TAILQ_FIRST(&expclusterlist);
    next_expirer = TAILQ_NEXT(tdb, tdb_explink);
    will_be_first = (next_timeout != 0 &&
		     (former_expirer == NULL ||
		      next_timeout < former_expirer->tdb_timeout));
    sole_reason = (tdb == former_expirer &&
		   (next_expirer == NULL ||
		    tdb->tdb_timeout != next_expirer->tdb_timeout));

    /*
     * We need to untimeout if either:
     * - there is an expiration pending and the new timeout is earlier than
     *   what already exists or
     * - the existing first expiration is due to our old timeout value solely
     */
    if ((former_expirer != NULL && will_be_first) || sole_reason)
      untimeout(handle_expirations, (void *) NULL);

    /*
     * We need to timeout if we've been asked to and if either
     * - our tdb has a timeout and no former expiration exist or
     * - the new timeout is earlier than what already exists or
     * - the existing first expiration is due to our old timeout value solely
     *   and another expiration is in the pipe.
     */
    if ((flags & TDBEXP_TIMEOUT) &&
	(will_be_first || (sole_reason && next_expirer != NULL)))
      timeout(handle_expirations, (void *) NULL,
	      hz * ((will_be_first ? next_timeout :
		     next_expirer->tdb_timeout) - time.tv_sec));

    /* Our old position, if any, is not relevant anymore.  */
    if (tdb->tdb_timeout != 0)
    {
        if (tdb->tdb_expnext.tqe_prev != NULL)
	{
	    if (next_expirer && tdb->tdb_timeout == next_expirer->tdb_timeout)
	      TAILQ_INSERT_BEFORE(tdb, next_expirer, tdb_expnext);
	    TAILQ_REMOVE(&expclusterlist, tdb, tdb_expnext);
	    tdb->tdb_expnext.tqe_prev = NULL;
	}
	TAILQ_REMOVE(&explist, tdb, tdb_explink);
    }

    tdb->tdb_timeout = next_timeout;

    if (next_timeout == 0)
    {
      splx(s);
      return;
    }

    /*
     * Search front-to-back if we believe we will end up early, otherwise
     * back-to-front.
     */
    early = will_be_first || (flags & TDBEXP_EARLY);
    for (t = (early ? TAILQ_FIRST(&expclusterlist) :
	      TAILQ_LAST(&expclusterlist, expclusterlist_head));
	 t != NULL && (early ? (t->tdb_timeout <= next_timeout) : 
		       (t->tdb_timeout > next_timeout));
	 t = (early ? TAILQ_NEXT(t, tdb_expnext) :
	      TAILQ_PREV(t, expclusterlist_head, tdb_expnext)))
      ;
    if (t == (early ? TAILQ_FIRST(&expclusterlist) : NULL))
    {
	/* We are to become the first expiration.  */
	TAILQ_INSERT_HEAD(&expclusterlist, tdb, tdb_expnext);
	TAILQ_INSERT_HEAD(&explist, tdb, tdb_explink);
    }
    else
    {
	if (early)
	  t = (t ? TAILQ_PREV(t, expclusterlist_head, tdb_expnext) :
	       TAILQ_LAST(&expclusterlist, expclusterlist_head));
	if (TAILQ_NEXT(t, tdb_expnext))
	  TAILQ_INSERT_BEFORE(TAILQ_NEXT(t, tdb_expnext), tdb, tdb_explink);
	else
	  TAILQ_INSERT_TAIL(&explist, tdb, tdb_explink);
	if (t->tdb_timeout < next_timeout)
	  TAILQ_INSERT_AFTER(&expclusterlist, t, tdb, tdb_expnext);
    }

#ifdef DIAGNOSTIC
    /*
     * Check various invariants.
     */
    if (tdb->tdb_expnext.tqe_prev != NULL)
    {
	t = TAILQ_FIRST(&expclusterlist);
	if (t != tdb && t->tdb_timeout >= tdb->tdb_timeout)
	  panic("tdb_expiration: "
		"expclusterlist first link out of order (%p, %p)",
		tdb, t);
	t = TAILQ_PREV(tdb, expclusterlist_head, tdb_expnext);
	if (t != NULL && t->tdb_timeout >= tdb->tdb_timeout)
	  panic("tdb_expiration: "
		"expclusterlist prev link out of order (%p, %p)",
		tdb, t);
	else if (t == NULL && tdb != TAILQ_FIRST(&expclusterlist))
	  panic("tdb_expiration: "
		"expclusterlist first link out of order (%p, %p)",
		tdb, TAILQ_FIRST(&expclusterlist));
	t = TAILQ_NEXT(tdb, tdb_expnext);
	if (t != NULL && t->tdb_timeout <= tdb->tdb_timeout)
	  panic("tdb_expiration: "
		"expclusterlist next link out of order (%p, %p)",
		tdb, t);
	else if (t == NULL &&
		 tdb != TAILQ_LAST(&expclusterlist, expclusterlist_head))
	  panic("tdb_expiration: "
		"expclusterlist last link out of order (%p, %p)",
		tdb, TAILQ_LAST(&expclusterlist, expclusterlist_head));
	t = TAILQ_LAST(&expclusterlist, expclusterlist_head);
	if (t != tdb && t->tdb_timeout <= tdb->tdb_timeout)
	  panic("tdb_expiration: "
		"expclusterlist last link out of order (%p, %p)",
		tdb, t);
    }
    t = TAILQ_FIRST(&explist);
    if (t != NULL && t->tdb_timeout > tdb->tdb_timeout)
      panic("tdb_expiration: explist first link out of order (%p, %p)", tdb,
	    t);
    t = TAILQ_PREV(tdb, explist_head, tdb_explink);
    if (t != NULL && t->tdb_timeout > tdb->tdb_timeout)
      panic("tdb_expiration: explist prev link out of order (%p, %p)", tdb, t);
    else if (t == NULL && tdb != TAILQ_FIRST(&explist))
      panic("tdb_expiration: explist first link out of order (%p, %p)", tdb,
	    TAILQ_FIRST(&explist));
    t = TAILQ_NEXT(tdb, tdb_explink);
    if (t != NULL && t->tdb_timeout < tdb->tdb_timeout)
      panic("tdb_expiration: explist next link out of order (%p, %p)", tdb, t);
    else if (t == NULL && tdb != TAILQ_LAST(&explist, explist_head))
      panic("tdb_expiration: explist last link out of order (%p, %p)", tdb,
	    TAILQ_LAST(&explist, explist_head));
    t = TAILQ_LAST(&explist, explist_head);
    if (t != tdb && t->tdb_timeout < tdb->tdb_timeout)
      panic("tdb_expiration: explist last link out of order (%p, %p)", tdb, t);
#endif

    splx(s);
}

/*
 * Caller is responsible for setting at least spltdb().
 */

struct flow *
find_flow(union sockaddr_union *src, union sockaddr_union *srcmask,
	  union sockaddr_union *dst, union sockaddr_union *dstmask,
	  u_int8_t proto, struct tdb *tdb)
{
    struct flow *flow;

    for (flow = tdb->tdb_flow; flow; flow = flow->flow_next)
      if (!bcmp(&src->sa, &flow->flow_src.sa, SA_LEN(&src->sa)) &&
	  !bcmp(&dst->sa, &flow->flow_dst.sa, SA_LEN(&dst->sa)) &&
	  !bcmp(&srcmask->sa, &flow->flow_srcmask.sa, SA_LEN(&srcmask->sa)) &&
	  !bcmp(&dstmask->sa, &flow->flow_dstmask.sa, SA_LEN(&dstmask->sa)) &&
	  (proto == flow->flow_proto))
	return flow;

    return (struct flow *) NULL;
}

/*
 * Caller is responsible for setting at least spltdb().
 */

struct flow *
find_global_flow(union sockaddr_union *src, union sockaddr_union *srcmask,
		 union sockaddr_union *dst, union sockaddr_union *dstmask,
		 u_int8_t proto)
{
    struct flow *flow;
    struct tdb *tdb;
    int i;

    if (tdbh == NULL)
      return (struct flow *) NULL;

    if (tdb_bypass != NULL)
      if ((flow = find_flow(src, srcmask, dst, dstmask, proto, tdb_bypass))
	  != (struct flow *) NULL)
	return flow;

    for (i = 0; i <= tdb_hashmask; i++)
    {
	for (tdb = tdbh[i]; tdb != NULL; tdb = tdb->tdb_hnext)
	  if ((flow = find_flow(src, srcmask, dst, dstmask, proto, tdb)) !=
	      (struct flow *) NULL)
	    return flow;
    }
    return (struct flow *) NULL;
}

/*
 * Caller is responsible for spltdb().
 */

void
tdb_rehash(void)
{
    struct tdb **new_tdbh, *tdbp, *tdbnp;
    u_int i, old_hashmask = tdb_hashmask;
    u_int32_t hashval;

    tdb_hashmask = (tdb_hashmask << 1) | 1;
    MALLOC(new_tdbh, struct tdb **, sizeof(struct tdb *) * (tdb_hashmask + 1),
	   M_TDB, M_WAITOK);
    bzero(new_tdbh, sizeof(struct tdb *) * (tdb_hashmask + 1));
    for (i = 0; i <= old_hashmask; i++)
      for (tdbp = tdbh[i]; tdbp != NULL; tdbp = tdbnp)
      {
	  tdbnp = tdbp->tdb_hnext;
      	  hashval = tdb_hash(tdbp->tdb_spi, &tdbp->tdb_dst, tdbp->tdb_sproto);
	  tdbp->tdb_hnext = new_tdbh[hashval];
	  new_tdbh[hashval] = tdbp;
      }
    FREE(tdbh, M_TDB);
    tdbh = new_tdbh;
}

void
puttdb(struct tdb *tdbp)
{
    u_int32_t hashval;
    int s = spltdb();

    if (tdbh == NULL)
    {
	MALLOC(tdbh, struct tdb **, sizeof(struct tdb *) * (tdb_hashmask + 1),
	       M_TDB, M_WAITOK);
	bzero(tdbh, sizeof(struct tdb *) * (tdb_hashmask + 1));
    }

    hashval = tdb_hash(tdbp->tdb_spi, &tdbp->tdb_dst, tdbp->tdb_sproto);
    
    /*
     * Rehash if this tdb would cause a bucket to have more than two items
     * and if the number of tdbs exceed 10% of the bucket count.  This
     * number is arbitratily chosen and is just a measure to not keep rehashing
     * when adding and removing tdbs which happens to always end up in the
     * same bucket, which is not uncommon when doing manual keying.
     */
    if (tdbh[hashval] != NULL && tdbh[hashval]->tdb_hnext != NULL &&
	tdb_count * 10 > tdb_hashmask + 1)
    {
	tdb_rehash();
	hashval = tdb_hash(tdbp->tdb_spi, &tdbp->tdb_dst, tdbp->tdb_sproto);
    }
    tdbp->tdb_hnext = tdbh[hashval];
    tdbh[hashval] = tdbp;
    tdb_count++;
    splx(s);
}

/*
 * Caller is responsible for setting at least spltdb().
 */

void
put_flow(struct flow *flow, struct tdb *tdb)
{
    flow->flow_next = tdb->tdb_flow;
    flow->flow_prev = (struct flow *) NULL;

    tdb->tdb_flow = flow;

    flow->flow_sa = tdb;

    if (flow->flow_next)
      flow->flow_next->flow_prev = flow;
}

/*
 * Caller is responsible for setting at least spltdb().
 */

void
delete_flow(struct flow *flow, struct tdb *tdb)
{
    if (tdb)
    {
	if (tdb->tdb_flow == flow)
	{
	    tdb->tdb_flow = flow->flow_next;
	    if (tdb->tdb_flow)
	      tdb->tdb_flow->flow_prev = (struct flow *) NULL;
	}
	else
	{
	    flow->flow_prev->flow_next = flow->flow_next;
	    if (flow->flow_next)
	      flow->flow_next->flow_prev = flow->flow_prev;
	}
    }

    ipsec_in_use--;
    FREE(flow, M_TDB);
}

void
tdb_delete(struct tdb *tdbp, int delchain, int expflags)
{
    struct tdb *tdbpp;
    struct inpcb *inp;
    u_int32_t hashval = tdbp->tdb_sproto + tdbp->tdb_spi;
    int s;

    /* When deleting the bypass tdb, skip the hash table code. */
    if (tdbp == tdb_bypass && tdbp != NULL)
    {
	s = spltdb();
	delchain = 0;
	goto skip_hash;
    }

    if (tdbh == NULL)
      return;

    hashval = tdb_hash(tdbp->tdb_spi, &tdbp->tdb_dst, tdbp->tdb_sproto);

    s = spltdb();
    if (tdbh[hashval] == tdbp)
    {
	tdbpp = tdbp;
	tdbh[hashval] = tdbp->tdb_hnext;
    }
    else
      for (tdbpp = tdbh[hashval]; tdbpp != NULL; tdbpp = tdbpp->tdb_hnext)
	if (tdbpp->tdb_hnext == tdbp)
	{
	    tdbpp->tdb_hnext = tdbp->tdb_hnext;
	    tdbpp = tdbp;
	}

 skip_hash:
    /*
     * If there was something before us in the chain pointing to us,
     * make it point nowhere
     */
    if ((tdbp->tdb_inext) &&
	(tdbp->tdb_inext->tdb_onext == tdbp))
      tdbp->tdb_inext->tdb_onext = NULL;

    /* 
     * If there was something after us in the chain pointing to us,
     * make it point nowhere
     */
    if ((tdbp->tdb_onext) &&
	(tdbp->tdb_onext->tdb_inext == tdbp))
      tdbp->tdb_onext->tdb_inext = NULL;
    
    tdbpp = tdbp->tdb_onext;
    
    if (tdbp->tdb_xform)
      (*(tdbp->tdb_xform->xf_zeroize))(tdbp);

    while (tdbp->tdb_flow)
    {
        /* Delete the flow and the routing entry that goes with it. */ 
        struct sockaddr_encap encapdst, encapnetmask;

        bzero(&encapdst, sizeof(struct sockaddr_encap));
        bzero(&encapnetmask, sizeof(struct sockaddr_encap));

        encapdst.sen_len = SENT_IP4_LEN;
        encapdst.sen_family = PF_KEY;
        encapdst.sen_type = SENT_IP4;
        encapdst.sen_ip_src = tdbp->tdb_flow->flow_src.sin.sin_addr;
        encapdst.sen_ip_dst = tdbp->tdb_flow->flow_dst.sin.sin_addr;
        encapdst.sen_proto = tdbp->tdb_flow->flow_proto;
        encapdst.sen_sport = tdbp->tdb_flow->flow_src.sin.sin_port;
        encapdst.sen_dport = tdbp->tdb_flow->flow_dst.sin.sin_port;

        encapnetmask.sen_len = SENT_IP4_LEN;
        encapnetmask.sen_family = PF_KEY;
        encapnetmask.sen_type = SENT_IP4;
        encapnetmask.sen_ip_src = tdbp->tdb_flow->flow_srcmask.sin.sin_addr;
        encapnetmask.sen_ip_dst = tdbp->tdb_flow->flow_dstmask.sin.sin_addr;

        if (tdbp->tdb_flow->flow_proto)
        {
            encapnetmask.sen_proto = 0xff;
            if (tdbp->tdb_flow->flow_src.sin.sin_port)
              encapnetmask.sen_sport = 0xffff;
            if (tdbp->tdb_flow->flow_dst.sin.sin_port)
              encapnetmask.sen_dport = 0xffff;
        }

        rtrequest(RTM_DELETE, (struct sockaddr *) &encapdst,
                  (struct sockaddr *) 0,
                  (struct sockaddr *) &encapnetmask,
                  0, (struct rtentry **) 0);

        delete_flow(tdbp->tdb_flow, tdbp);
    }

    /* Cleanup SA-Bindings */
    for (tdbpp = TAILQ_FIRST(&tdbp->tdb_bind_in); tdbpp;
	 tdbpp = TAILQ_FIRST(&tdbp->tdb_bind_in))
    {
        TAILQ_REMOVE(&tdbpp->tdb_bind_in, tdbpp, tdb_bind_in_next);
	tdbpp->tdb_bind_out = NULL;
    }
    /* Cleanup inp references */
    for (inp = TAILQ_FIRST(&tdbp->tdb_inp); inp;
	 inp = TAILQ_FIRST(&tdbp->tdb_inp))
    {
        TAILQ_REMOVE(&tdbp->tdb_inp, inp, inp_tdb_next);
	inp->inp_tdb = NULL;
    }

    if (tdbp->tdb_bind_out)
      TAILQ_REMOVE(&tdbp->tdb_bind_out->tdb_bind_in, tdbp, tdb_bind_in_next);

    /* Remove us from the expiration lists.  */
    if (tdbp->tdb_timeout != 0)
    {
        tdbp->tdb_flags &= ~(TDBF_FIRSTUSE | TDBF_SOFT_FIRSTUSE | TDBF_TIMER |
			     TDBF_SOFT_TIMER);
	tdb_expiration(tdbp, expflags);
    }

    if (tdbp->tdb_srcid)
      FREE(tdbp->tdb_srcid, M_XDATA);

    if (tdbp->tdb_dstid)
      FREE(tdbp->tdb_dstid, M_XDATA);

    /* If we're deleting the bypass tdb, reset the variable. */
    if (tdbp == tdb_bypass)
      tdb_bypass = NULL;

    FREE(tdbp, M_TDB);
    tdb_count--;

    if (delchain && tdbpp)
      tdb_delete(tdbpp, delchain, expflags);
    splx(s);
}

int
tdb_init(struct tdb *tdbp, u_int16_t alg, struct ipsecinit *ii)
{
    struct xformsw *xsp;

    /* Record establishment time */
    tdbp->tdb_established = time.tv_sec;
    tdbp->tdb_epoch = kernfs_epoch - 1;

    /* Init Incoming SA-Binding Queues */
    TAILQ_INIT(&tdbp->tdb_bind_in);
    TAILQ_INIT(&tdbp->tdb_inp);

    for (xsp = xformsw; xsp < xformswNXFORMSW; xsp++)
      if (xsp->xf_type == alg)
	return (*(xsp->xf_init))(tdbp, xsp, ii);

    DPRINTF(("tdb_init(): no alg %d for spi %08x, addr %s, proto %d\n", 
	     alg, ntohl(tdbp->tdb_spi), ipsp_address(tdbp->tdb_dst),
	     tdbp->tdb_sproto));
    
    return EINVAL;
}

/*
 * Used by kernfs
 */
int
ipsp_kern(int off, char **bufp, int len)
{
    static char buffer[IPSEC_KERNFS_BUFSIZE];
    struct flow *flow;
    struct tdb *tdb, *tdbp;
    int l, i, s;

    if (off == 0)
      kernfs_epoch++;
    
    if (bufp == NULL || tdbh == NULL)
      return 0;

    bzero(buffer, IPSEC_KERNFS_BUFSIZE);

    *bufp = buffer;
    
    for (i = 0; i <= tdb_hashmask; i++)
    {
        s = spltdb();
	for (tdb = tdbh[i]; tdb; tdb = tdb->tdb_hnext)
	  if (tdb->tdb_epoch != kernfs_epoch)
	  {
	      tdb->tdb_epoch = kernfs_epoch;

	      l = sprintf(buffer,
			  "SPI = %08x, Destination = %s, Sproto = %u\n",
			  ntohl(tdb->tdb_spi),
			  ipsp_address(tdb->tdb_dst), tdb->tdb_sproto);

	      l += sprintf(buffer + l, "\tEstablished %d seconds ago\n",
			   time.tv_sec - tdb->tdb_established);

	      l += sprintf(buffer + l, "\tSource = %s",
			   ipsp_address(tdb->tdb_src));

	      if (tdb->tdb_proxy.sa.sa_family)
		l += sprintf(buffer + l, ", Proxy = %s\n",
			     ipsp_address(tdb->tdb_proxy));
	      else
		l += sprintf(buffer + l, "\n");

	      l += sprintf(buffer + l, "\tFlags (%08x) = <", tdb->tdb_flags);

	      if ((tdb->tdb_flags & ~(TDBF_TIMER | TDBF_BYTES |
				      TDBF_ALLOCATIONS | TDBF_FIRSTUSE |
				      TDBF_SOFT_TIMER | TDBF_SOFT_BYTES |
				      TDBF_SOFT_FIRSTUSE |
				      TDBF_SOFT_ALLOCATIONS)) == 0)
		l += sprintf(buffer + l, "none>\n");
	      else
	      {
		  /* We can reuse variable 'i' here, since we're not looping */
		  i = 0;

		  if (tdb->tdb_flags & TDBF_UNIQUE)
		  {
		      if (i)
			l += sprintf(buffer + l, ", ");
		      else
			i = 1;

		      l += sprintf(buffer + l, "unique");
		      i = 1;
		  }

		  if (tdb->tdb_flags & TDBF_INVALID)
		  {
		      if (i)
			l += sprintf(buffer + l, ", ");
		      else
			i = 1;

		      l += sprintf(buffer + l, "invalid");
		  }

		  if (tdb->tdb_flags & TDBF_HALFIV)
		  {
		      if (i)
			l += sprintf(buffer + l, ", ");
		      else
			i = 1;

		      l += sprintf(buffer + l, "halfiv");
		  }

		  if (tdb->tdb_flags & TDBF_PFS)
		  {
		      if (i)
			l += sprintf(buffer + l, ", ");
		      else
			i = 1;

		      l += sprintf(buffer + l, "pfs");
		  }

		  if (tdb->tdb_flags & TDBF_TUNNELING)
		  {
		      if (i)
			l += sprintf(buffer + l, ", ");
		      else
			i = 1;

		      l += sprintf(buffer + l, "tunneling");
		  }

		  l += sprintf(buffer + l, ">\n");
	      }

	      if (tdb->tdb_xform)
		l += sprintf(buffer + l, "\txform = <%s>\n", 
			     tdb->tdb_xform->xf_name);

	      if (tdb->tdb_encalgxform)
		l += sprintf(buffer + l, "\t\tEncryption = <%s>\n",
			     tdb->tdb_encalgxform->name);

	      if (tdb->tdb_authalgxform)
		l += sprintf(buffer + l, "\t\tAuthentication = <%s>\n",
			     tdb->tdb_authalgxform->name);

	      if (tdb->tdb_bind_out)
		l += sprintf(buffer + l,
			     "\tBound SA: SPI = %08x, "
			     "Destination = %s, Sproto = %u\n",
			     ntohl(tdb->tdb_bind_out->tdb_spi),
			     ipsp_address(tdb->tdb_bind_out->tdb_dst),
			     tdb->tdb_bind_out->tdb_sproto);
	      for (i = 0, tdbp = TAILQ_FIRST(&tdb->tdb_bind_in); tdbp;
		   tdbp = TAILQ_NEXT(tdbp, tdb_bind_in_next))
		i++;

	      if (i > 0)
		l += sprintf(buffer + l,
			     "\tReferenced by %d incoming SA%s\n",
			     i, i == 1 ? "" : "s");

	      if (tdb->tdb_onext)
		l += sprintf(buffer + l,
			     "\tNext SA: SPI = %08x, "
			     "Destination = %s, Sproto = %u\n",
			     ntohl(tdb->tdb_onext->tdb_spi),
			     ipsp_address(tdb->tdb_onext->tdb_dst),
			     tdb->tdb_onext->tdb_sproto);

	      if (tdb->tdb_inext)
		l += sprintf(buffer + l,
			     "\tPrevious SA: SPI = %08x, "
			     "Destination = %s, Sproto = %u\n",
			     ntohl(tdb->tdb_inext->tdb_spi),
			     ipsp_address(tdb->tdb_inext->tdb_dst),
			     tdb->tdb_inext->tdb_sproto);

	      for (i = 0, flow = tdb->tdb_flow; flow; flow = flow->flow_next)
		i++;

	      l+= sprintf(buffer + l, "\tCurrently used by %d flows\n", i);

	      l += sprintf(buffer + l, "\t%u flows have used this SA\n",
			   tdb->tdb_cur_allocations);
	    
	      l += sprintf(buffer + l, "\t%qu bytes processed by this SA\n",
			 tdb->tdb_cur_bytes);
	    
	      l += sprintf(buffer + l, "\tExpirations:\n");

	      if (tdb->tdb_flags & TDBF_TIMER)
		l += sprintf(buffer + l,
			     "\t\tHard expiration(1) in %qu seconds\n",
			     tdb->tdb_exp_timeout - time.tv_sec);
	    
	      if (tdb->tdb_flags & TDBF_SOFT_TIMER)
		l += sprintf(buffer + l,
			     "\t\tSoft expiration(1) in %qu seconds\n",
			     tdb->tdb_soft_timeout - time.tv_sec);
	    
	      if (tdb->tdb_flags & TDBF_BYTES)
		l += sprintf(buffer + l,
			     "\t\tHard expiration after %qu bytes\n",
			     tdb->tdb_exp_bytes);
	    
	      if (tdb->tdb_flags & TDBF_SOFT_BYTES)
		l += sprintf(buffer + l,
			     "\t\tSoft expiration after %qu bytes\n",
			     tdb->tdb_soft_bytes);

	      if (tdb->tdb_flags & TDBF_ALLOCATIONS)
		l += sprintf(buffer + l,
			     "\t\tHard expiration after %u flows\n",
			     tdb->tdb_exp_allocations);
	    
	      if (tdb->tdb_flags & TDBF_SOFT_ALLOCATIONS)
		l += sprintf(buffer + l,
			     "\t\tSoft expiration after %u flows\n",
			     tdb->tdb_soft_allocations);

	      if (tdb->tdb_flags & TDBF_FIRSTUSE)
	      {
		  if (tdb->tdb_first_use)
		    l += sprintf(buffer + l,
				 "\t\tHard expiration(2) in %qu seconds\n",
				 (tdb->tdb_first_use +
				  tdb->tdb_exp_first_use) - time.tv_sec);
		  else
		    l += sprintf(buffer + l,
				 "\t\tHard expiration in %qu seconds "
				 "after first use\n",
				 tdb->tdb_exp_first_use);
	      }

	      if (tdb->tdb_flags & TDBF_SOFT_FIRSTUSE)
	      {
		  if (tdb->tdb_first_use)
		    l += sprintf(buffer + l,
				 "\t\tSoft expiration(2) in %qu seconds\n",
				 (tdb->tdb_first_use +
				  tdb->tdb_soft_first_use) - time.tv_sec);
		  else
		    l += sprintf(buffer + l,
				 "\t\tSoft expiration in %qu seconds "
				 "after first use\n",
				 tdb->tdb_soft_first_use);
	      }

	      if (!(tdb->tdb_flags &
		    (TDBF_TIMER | TDBF_SOFT_TIMER | TDBF_BYTES |
		     TDBF_SOFT_ALLOCATIONS | TDBF_ALLOCATIONS |
		     TDBF_SOFT_BYTES | TDBF_FIRSTUSE | TDBF_SOFT_FIRSTUSE)))
		l += sprintf(buffer + l, "\t\t(none)\n");

	      l += sprintf(buffer + l, "\n");

	      splx(s);
	      return l;
	  }
	splx(s);
    }
    return 0;
}

char *
inet_ntoa4(struct in_addr ina)
{
    static char buf[4][4 * sizeof "123"];
    unsigned char *ucp = (unsigned char *) &ina;
    static int i = 3;
 
    i = (i + 1) % 4;
    sprintf(buf[i], "%d.%d.%d.%d", ucp[0] & 0xff, ucp[1] & 0xff,
            ucp[2] & 0xff, ucp[3] & 0xff);
    return (buf[i]);
}

char *
ipsp_address(union sockaddr_union sa)
{
    switch (sa.sa.sa_family)
    {
	case AF_INET:
	    return inet_ntoa4(sa.sin.sin_addr);

#if INET6
	    /* XXX Add AF_INET6 support here */
#endif /* INET6 */

	default:
	    return "(unknown address family)";
    }
}
