/* $OpenBSD: pfkeyv2.c,v 1.55 2001/03/04 20:50:40 angelos Exp $ */
/*
%%% copyright-nrl-97
This software is Copyright 1997-1998 by Randall Atkinson, Ronald Lee,
Daniel McDonald, Bao Phan, and Chris Winters. All Rights Reserved. All
rights under this copyright have been assigned to the US Naval Research
Laboratory (NRL). The NRL Copyright Notice and License Agreement Version
1.1 (January 17, 1995) applies to this software.
You should have received a copy of the license with this software. If you
didn't get a copy, you may request one from <license@ipv6.nrl.navy.mil>.

%%% copyright-cmetz-97
This software is Copyright 1997-1998 by Craig Metz, All Rights Reserved.
The Inner Net License Version 2 applies to this software.
You should have received a copy of the license with this software. If
you didn't get a copy, you may request one from <license@inner.net>.

*/

#include <sys/types.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/socketvar.h>
#include <sys/proc.h>
#include <net/route.h>
#include <netinet/in.h>
#include <netinet/ip_ipsp.h>
#include <net/pfkeyv2.h>
#include <netinet/ip_ah.h>
#include <netinet/ip_esp.h>
#include <crypto/blf.h>
#include <crypto/crypto.h>
#include <crypto/xform.h>

#define PFKEYV2_PROTOCOL 2
#define GETSPI_TRIES 10

/* Static globals */
static struct pfkeyv2_socket *pfkeyv2_sockets = NULL;
static struct pfkey_version pfkeyv2_version;
static uint32_t pfkeyv2_seq = 1;
static int nregistered = 0;
static int npromisc = 0;

static struct sadb_alg ealgs[] =
{
    { SADB_EALG_DESCBC, 64, 64, 64 },
    { SADB_EALG_3DESCBC, 64, 192, 192 },
    { SADB_X_EALG_BLF, 64, 40, BLF_MAXKEYLEN * 8},
    { SADB_X_EALG_CAST, 64, 40, 128},
    { SADB_X_EALG_SKIPJACK, 64, 80, 80},
    { SADB_X_EALG_AES, 128, 64, 256},
};

static struct sadb_alg aalgs[] =
{
    { SADB_AALG_SHA1HMAC, 0, 160, 160 },
    { SADB_AALG_MD5HMAC, 0, 128, 128 },
    { SADB_AALG_RIPEMD160HMAC, 0, 160, 160 }
};

void export_address(void **, struct sockaddr *);
void export_identity(void **, struct tdb *, int);
void export_lifetime(void **, struct tdb *, int);
void export_credentials(void **, struct tdb *);
void export_sa(void **, struct tdb *);
void export_key(void **, struct tdb *, int);

void import_address(struct sockaddr *, struct sadb_address *);
void import_identity(struct tdb *, struct sadb_ident *, int);
void import_key(struct ipsecinit *, struct sadb_key *, int);
void import_lifetime(struct tdb *, struct sadb_lifetime *, int);
void import_credentials(struct tdb *, struct sadb_cred *);
void import_sa(struct tdb *, struct sadb_sa *, struct ipsecinit *);

int pfkeyv2_create(struct socket *);
int pfkeyv2_get(struct tdb *, void **, void **);
int pfkeyv2_policy(struct ipsec_acquire *, void **, void **);
int pfkeyv2_release(struct socket *);
int pfkeyv2_send(struct socket *, void *, int);
int pfkeyv2_sendmessage(void **, int, struct socket *, u_int8_t, int);
int pfkeyv2_dump_walker(struct tdb *, void *, int);
int pfkeyv2_flush_walker(struct tdb *, void *, int);
int pfkeyv2_get_proto_alg(u_int8_t, u_int8_t *, int *);

int pfdatatopacket(void *, int, struct mbuf **);

extern uint32_t sadb_exts_allowed_out[SADB_MAX+1];
extern uint32_t sadb_exts_required_out[SADB_MAX+1];

#define EXTLEN(x) (((struct sadb_ext *)(x))->sadb_ext_len * sizeof(uint64_t))
#define PADUP(x) (((x) + sizeof(uint64_t) - 1) & ~(sizeof(uint64_t) - 1))

/*
 * Wrapper around m_devget(); copy data from contiguous buffer to mbuf
 * chain.
 */
int
pfdatatopacket(void *data, int len, struct mbuf **packet)
{
    if (!(*packet = m_devget(data, len, 0, NULL, NULL)))
      return ENOMEM;

    return 0;
}

/*
 * Create a new PF_KEYv2 socket.
 */
int
pfkeyv2_create(struct socket *socket)
{
    struct pfkeyv2_socket *pfkeyv2_socket;

    if (!(pfkeyv2_socket = malloc(sizeof(struct pfkeyv2_socket), M_PFKEY,
				  M_DONTWAIT)))
      return ENOMEM;

    bzero(pfkeyv2_socket, sizeof(struct pfkeyv2_socket));
    pfkeyv2_socket->next = pfkeyv2_sockets;
    pfkeyv2_socket->socket = socket;
    pfkeyv2_socket->pid = curproc->p_pid;

    pfkeyv2_sockets = pfkeyv2_socket;

    return 0;
}

/*
 * Close a PF_KEYv2 socket.
 */
int
pfkeyv2_release(struct socket *socket)
{
    struct pfkeyv2_socket **pp;

    for (pp = &pfkeyv2_sockets;
	 *pp && ((*pp)->socket != socket);
	 pp = &((*pp)->next))
      ;

    if (*pp)
    {
	struct pfkeyv2_socket *pfkeyv2_socket;

	pfkeyv2_socket = *pp;
	*pp = (*pp)->next;

	if (pfkeyv2_socket->flags & PFKEYV2_SOCKETFLAGS_REGISTERED)
	  nregistered--;

	if (pfkeyv2_socket->flags & PFKEYV2_SOCKETFLAGS_PROMISC)
	  npromisc--;

	free(pfkeyv2_socket, M_PFKEY);
    }

    return 0;
}

/*
 * (Partly) Initialize a TDB based on an SADB_SA payload. Other parts
 * of the TDB will be initialized by other import routines, and tdb_init().
 */
void
import_sa(struct tdb *tdb, struct sadb_sa *sadb_sa, struct ipsecinit *ii)
{
    if (!sadb_sa)
      return;

    if (ii)
    {
	ii->ii_encalg = sadb_sa->sadb_sa_encrypt;
	ii->ii_authalg = sadb_sa->sadb_sa_auth;

	tdb->tdb_spi = sadb_sa->sadb_sa_spi;
	tdb->tdb_wnd = sadb_sa->sadb_sa_replay;

	if (sadb_sa->sadb_sa_flags & SADB_SAFLAGS_PFS)
	  tdb->tdb_flags |= TDBF_PFS;

	if (sadb_sa->sadb_sa_flags & SADB_X_SAFLAGS_HALFIV)
	  tdb->tdb_flags |= TDBF_HALFIV;

	if (sadb_sa->sadb_sa_flags & SADB_X_SAFLAGS_TUNNEL)
	  tdb->tdb_flags |= TDBF_TUNNELING;

	if (sadb_sa->sadb_sa_flags & SADB_X_SAFLAGS_RANDOMPADDING)
	  tdb->tdb_flags |= TDBF_RANDOMPADDING;

	if (sadb_sa->sadb_sa_flags & SADB_X_SAFLAGS_NOREPLAY)
	  tdb->tdb_flags |= TDBF_NOREPLAY;
    }

    if (sadb_sa->sadb_sa_state != SADB_SASTATE_MATURE)
      tdb->tdb_flags |= TDBF_INVALID;
}

/*
 * Export some of the information on a TDB.
 */
void
export_sa(void **p, struct tdb *tdb)
{
    struct sadb_sa *sadb_sa = (struct sadb_sa *) *p;

    sadb_sa->sadb_sa_len = sizeof(struct sadb_sa) / sizeof(uint64_t);

    sadb_sa->sadb_sa_spi = tdb->tdb_spi;
    sadb_sa->sadb_sa_replay = tdb->tdb_wnd;
  
    if (tdb->tdb_flags & TDBF_INVALID)
      sadb_sa->sadb_sa_state = SADB_SASTATE_LARVAL;

    if (tdb->tdb_authalgxform)
    {
	switch (tdb->tdb_authalgxform->type)
	{
	    case CRYPTO_MD5_HMAC:
		sadb_sa->sadb_sa_auth = SADB_AALG_MD5HMAC;
		break;

	    case CRYPTO_SHA1_HMAC:
		sadb_sa->sadb_sa_auth = SADB_AALG_SHA1HMAC;
		break;

	    case CRYPTO_RIPEMD160_HMAC:
		sadb_sa->sadb_sa_auth = SADB_AALG_RIPEMD160HMAC;
		break;

	    case CRYPTO_MD5_KPDK:
		sadb_sa->sadb_sa_auth = SADB_X_AALG_MD5;
		break;

	    case CRYPTO_SHA1_KPDK:
		sadb_sa->sadb_sa_auth = SADB_X_AALG_SHA1;
		break;
	}
    }

    if (tdb->tdb_encalgxform)
    {
	switch (tdb->tdb_encalgxform->type)
	{
	    case CRYPTO_DES_CBC:
		sadb_sa->sadb_sa_encrypt = SADB_EALG_DESCBC;
		break;

	    case CRYPTO_3DES_CBC:
		sadb_sa->sadb_sa_encrypt = SADB_EALG_3DESCBC;
		break;

	    case CRYPTO_AES_CBC:
		sadb_sa->sadb_sa_encrypt = SADB_X_EALG_AES;
		break;

	    case CRYPTO_CAST_CBC:
		sadb_sa->sadb_sa_encrypt = SADB_X_EALG_CAST;
		break;

	    case CRYPTO_BLF_CBC:
		sadb_sa->sadb_sa_encrypt = SADB_X_EALG_BLF;
		break;

	    case CRYPTO_SKIPJACK_CBC:
		sadb_sa->sadb_sa_encrypt = SADB_X_EALG_SKIPJACK;
		break;
	}
    }

    if (tdb->tdb_flags & TDBF_PFS)
      sadb_sa->sadb_sa_flags |= SADB_SAFLAGS_PFS;

    /* Only relevant for the "old" IPsec transforms */
    if (tdb->tdb_flags & TDBF_HALFIV)
      sadb_sa->sadb_sa_flags |= SADB_X_SAFLAGS_HALFIV;

    if (tdb->tdb_flags & TDBF_TUNNELING)
      sadb_sa->sadb_sa_flags |= SADB_X_SAFLAGS_TUNNEL;

    if (tdb->tdb_flags & TDBF_RANDOMPADDING)
      sadb_sa->sadb_sa_flags |= SADB_X_SAFLAGS_RANDOMPADDING;

    if (tdb->tdb_flags & TDBF_NOREPLAY)
      sadb_sa->sadb_sa_flags |= SADB_X_SAFLAGS_NOREPLAY;

    *p += sizeof(struct sadb_sa);
}

/*
 * Initialize expirations and counters based on lifetime payload.
 */
void
import_lifetime(struct tdb *tdb, struct sadb_lifetime *sadb_lifetime, int type)
{
    if (!sadb_lifetime)
      return;

    switch (type)
    {
	case PFKEYV2_LIFETIME_HARD:
	    if ((tdb->tdb_exp_allocations =
		 sadb_lifetime->sadb_lifetime_allocations) != 0)
	      tdb->tdb_flags |= TDBF_ALLOCATIONS;
	    else
	      tdb->tdb_flags &= ~TDBF_ALLOCATIONS;
      
	    if ((tdb->tdb_exp_bytes = sadb_lifetime->sadb_lifetime_bytes) != 0)
	      tdb->tdb_flags |= TDBF_BYTES;
	    else
	      tdb->tdb_flags &= ~TDBF_BYTES;
      
	    if ((tdb->tdb_exp_timeout =
		 sadb_lifetime->sadb_lifetime_addtime) != 0)
	    {
		tdb->tdb_flags |= TDBF_TIMER;
		tdb->tdb_exp_timeout += time.tv_sec;
	    }
	    else
	      tdb->tdb_flags &= ~TDBF_TIMER;
      
	    if ((tdb->tdb_exp_first_use =
		 sadb_lifetime->sadb_lifetime_usetime) != 0)
	      tdb->tdb_flags |= TDBF_FIRSTUSE;
	    else
	      tdb->tdb_flags &= ~TDBF_FIRSTUSE;
	    break;
      
	case PFKEYV2_LIFETIME_SOFT:
	    if ((tdb->tdb_soft_allocations =
		 sadb_lifetime->sadb_lifetime_allocations) != 0)
	      tdb->tdb_flags |= TDBF_SOFT_ALLOCATIONS;
	    else
	      tdb->tdb_flags &= ~TDBF_SOFT_ALLOCATIONS;
      
	    if ((tdb->tdb_soft_bytes =
		 sadb_lifetime->sadb_lifetime_bytes) != 0)
	      tdb->tdb_flags |= TDBF_SOFT_BYTES;
	    else
	      tdb->tdb_flags &= ~TDBF_SOFT_BYTES;
      
	    if ((tdb->tdb_soft_timeout =
		 sadb_lifetime->sadb_lifetime_addtime) != 0)
	    {
		tdb->tdb_flags |= TDBF_SOFT_TIMER;
		tdb->tdb_soft_timeout += time.tv_sec;
	    }
	    else
	      tdb->tdb_flags &= ~TDBF_SOFT_TIMER;
      
	    if ((tdb->tdb_soft_first_use =
		 sadb_lifetime->sadb_lifetime_usetime) != 0)
	      tdb->tdb_flags |= TDBF_SOFT_FIRSTUSE;
	    else
	      tdb->tdb_flags &= ~TDBF_SOFT_FIRSTUSE;
	    break;
      
	case PFKEYV2_LIFETIME_CURRENT:  /* Nothing fancy here */
	    tdb->tdb_cur_allocations =
				      sadb_lifetime->sadb_lifetime_allocations;
	    tdb->tdb_cur_bytes = sadb_lifetime->sadb_lifetime_bytes;
	    tdb->tdb_established = sadb_lifetime->sadb_lifetime_addtime;
	    tdb->tdb_first_use = sadb_lifetime->sadb_lifetime_usetime;
    }

    /* Setup/update our position in the expiration queue.  */
    tdb_expiration(tdb, TDBEXP_TIMEOUT);
}

/*
 * Export TDB expiration information.
 */
void
export_lifetime(void **p, struct tdb *tdb, int type)
{
    struct sadb_lifetime *sadb_lifetime = (struct sadb_lifetime *) *p;

    sadb_lifetime->sadb_lifetime_len = sizeof(struct sadb_lifetime) /
				       sizeof(uint64_t);

    switch (type)
    {
	case PFKEYV2_LIFETIME_HARD:
	    if (tdb->tdb_flags & TDBF_ALLOCATIONS)
	      sadb_lifetime->sadb_lifetime_allocations =
						     tdb->tdb_exp_allocations;

	    if (tdb->tdb_flags & TDBF_BYTES)
	      sadb_lifetime->sadb_lifetime_bytes = tdb->tdb_exp_bytes;

	    if (tdb->tdb_flags & TDBF_TIMER)
	      sadb_lifetime->sadb_lifetime_addtime = tdb->tdb_exp_timeout -
						     tdb->tdb_established;

	    if (tdb->tdb_flags & TDBF_FIRSTUSE)
	      sadb_lifetime->sadb_lifetime_usetime = tdb->tdb_exp_first_use -
						     tdb->tdb_first_use;
	    break;

	case PFKEYV2_LIFETIME_SOFT:
	    if (tdb->tdb_flags & TDBF_SOFT_ALLOCATIONS)
	      sadb_lifetime->sadb_lifetime_allocations =
						    tdb->tdb_soft_allocations;

	    if (tdb->tdb_flags & TDBF_SOFT_BYTES)
	      sadb_lifetime->sadb_lifetime_bytes = tdb->tdb_soft_bytes;

	    if (tdb->tdb_flags & TDBF_SOFT_TIMER)
	      sadb_lifetime->sadb_lifetime_addtime = tdb->tdb_soft_timeout -
						     tdb->tdb_established;

	    if (tdb->tdb_flags & TDBF_SOFT_FIRSTUSE)
	      sadb_lifetime->sadb_lifetime_usetime = tdb->tdb_soft_first_use -
						     tdb->tdb_first_use;
	    break;
      
	case PFKEYV2_LIFETIME_CURRENT:
	    sadb_lifetime->sadb_lifetime_allocations =
						      tdb->tdb_cur_allocations;
	    sadb_lifetime->sadb_lifetime_bytes = tdb->tdb_cur_bytes;
	    sadb_lifetime->sadb_lifetime_addtime = tdb->tdb_established;
	    sadb_lifetime->sadb_lifetime_usetime = tdb->tdb_first_use;
	    break;
    }

    *p += sizeof(struct sadb_lifetime);
}

/*
 * Copy an SADB_ADDRESS payload to a struct sockaddr.
 */
void
import_address(struct sockaddr *sa, struct sadb_address *sadb_address)
{
    int salen;
    struct sockaddr *ssa = (struct sockaddr *)((void *) sadb_address +
					       sizeof(struct sadb_address));

    if (!sadb_address)
      return;

    if (ssa->sa_len)
      salen = ssa->sa_len;
    else
      switch(ssa->sa_family)
      {
#ifdef INET
	  case AF_INET:
	      salen = sizeof(struct sockaddr_in);
	      break;
#endif /* INET */

#if INET6
	  case AF_INET6:
	      salen = sizeof(struct sockaddr_in6);
	      break;
#endif /* INET6 */

	  default:
	      return;
    }

    bcopy(ssa, sa, salen);
    sa->sa_len = salen;
}

/*
 * Export a struct sockaddr as an SADB_ADDRESS payload.
 */
void
export_address(void **p, struct sockaddr *sa)
{
    struct sadb_address *sadb_address = (struct sadb_address *) *p;

    sadb_address->sadb_address_len = (sizeof(struct sadb_address) +
				      PADUP(SA_LEN(sa))) / sizeof(uint64_t);

    *p += sizeof(struct sadb_address);
    bcopy(sa, *p, SA_LEN(sa));
    ((struct sockaddr *) *p)->sa_family = sa->sa_family;
    *p += PADUP(SA_LEN(sa));
}

/*
 * Import a set of credentials into the TDB.
 */
void
import_credentials(struct tdb *tdb, struct sadb_cred *sadb_cred)
{
    if (!sadb_cred)
      return;

    tdb->tdb_cred_len = EXTLEN(sadb_cred) - sizeof(struct sadb_cred);
    tdb->tdb_cred_type = sadb_cred->sadb_cred_type;
    MALLOC(tdb->tdb_credentials, caddr_t, tdb->tdb_cred_len, M_XDATA,
	   M_WAITOK);
    bcopy((void *) sadb_cred + sizeof(struct sadb_cred),
	  tdb->tdb_credentials, tdb->tdb_cred_len);
}

/*
 * Import an identity payload into the TDB.
 */
void
import_identity(struct tdb *tdb, struct sadb_ident *sadb_ident, int type)
{
    if (!sadb_ident)
      return;

    if (type == PFKEYV2_IDENTITY_SRC)
    {
	tdb->tdb_srcid_len = EXTLEN(sadb_ident) -
			     sizeof(struct sadb_ident);
	tdb->tdb_srcid_type = sadb_ident->sadb_ident_type;
	MALLOC(tdb->tdb_srcid, u_int8_t *, tdb->tdb_srcid_len, M_XDATA,
	       M_WAITOK);
	bcopy((void *) sadb_ident + sizeof(struct sadb_ident),
	      tdb->tdb_srcid, tdb->tdb_srcid_len);
    }
    else
    {
	tdb->tdb_dstid_len = EXTLEN(sadb_ident) -
			     sizeof(struct sadb_ident);
	tdb->tdb_dstid_type = sadb_ident->sadb_ident_type;
	MALLOC(tdb->tdb_dstid, u_int8_t *, tdb->tdb_dstid_len, M_XDATA,
	       M_WAITOK);
	bcopy((void *) sadb_ident + sizeof(struct sadb_ident),
	      tdb->tdb_dstid, tdb->tdb_dstid_len);
    }
}

void
export_credentials(void **p, struct tdb *tdb)
{
    struct sadb_cred *sadb_cred = (struct sadb_cred *) *p;

    sadb_cred->sadb_cred_len = (sizeof(struct sadb_cred) +
				PADUP(tdb->tdb_cred_len)) /
			       sizeof(uint64_t);
    sadb_cred->sadb_cred_type = tdb->tdb_cred_type;
    *p += sizeof(struct sadb_cred);
    bcopy(tdb->tdb_credentials, *p, tdb->tdb_cred_len);
    *p += PADUP(tdb->tdb_cred_len);
}

void
export_identity(void **p, struct tdb *tdb, int type)
{
    struct sadb_ident *sadb_ident = (struct sadb_ident *) *p;

    if (type == PFKEYV2_IDENTITY_SRC)
    {
	sadb_ident->sadb_ident_len = (sizeof(struct sadb_ident) +
				      PADUP(tdb->tdb_srcid_len)) /
				     sizeof(uint64_t);
	sadb_ident->sadb_ident_type = tdb->tdb_srcid_type;
	*p += sizeof(struct sadb_ident);
	bcopy(tdb->tdb_srcid, *p, tdb->tdb_srcid_len);
	*p += PADUP(tdb->tdb_srcid_len);
    }
    else
    {
	sadb_ident->sadb_ident_len = (sizeof(struct sadb_ident) +
				      PADUP(tdb->tdb_dstid_len)) /
				     sizeof(uint64_t);
	sadb_ident->sadb_ident_type = tdb->tdb_dstid_type;
	*p += sizeof(struct sadb_ident);
	bcopy(tdb->tdb_dstid, *p, tdb->tdb_dstid_len);
	*p += PADUP(tdb->tdb_dstid_len);
    }
}

/* ... */
void
import_key(struct ipsecinit *ii, struct sadb_key *sadb_key, int type)
{
    if (!sadb_key)
      return;
        
    if (type == PFKEYV2_ENCRYPTION_KEY)
    { /* Encryption key */
	ii->ii_enckeylen = sadb_key->sadb_key_bits / 8;
	ii->ii_enckey = (void *)sadb_key + sizeof(struct sadb_key);
    }
    else
    {
	ii->ii_authkeylen = sadb_key->sadb_key_bits / 8;
	ii->ii_authkey = (void *)sadb_key + sizeof(struct sadb_key);
    }
}

void
export_key(void **p, struct tdb *tdb, int type)
{
    struct sadb_key *sadb_key = (struct sadb_key *) *p;

    if (type == PFKEYV2_ENCRYPTION_KEY)
    {
	sadb_key->sadb_key_len = (sizeof(struct sadb_key) +
				  PADUP(tdb->tdb_emxkeylen)) /
				 sizeof(uint64_t);
	sadb_key->sadb_key_bits = tdb->tdb_emxkeylen * 8;
	*p += sizeof(struct sadb_key);
	bcopy(tdb->tdb_emxkey, *p, tdb->tdb_emxkeylen);
	*p += PADUP(tdb->tdb_emxkeylen);
    }
    else
    {
	sadb_key->sadb_key_len = (sizeof(struct sadb_key) +
				  PADUP(tdb->tdb_amxkeylen)) /
				 sizeof(uint64_t);
	sadb_key->sadb_key_bits = tdb->tdb_amxkeylen * 8;
	*p += sizeof(struct sadb_key);
	bcopy(tdb->tdb_amxkey, *p, tdb->tdb_amxkeylen);
	*p += PADUP(tdb->tdb_amxkeylen);
    }
}

/*
 * Send a PFKEYv2 message, possibly to many receivers, based on the
 * satype of the socket (which is set by the REGISTER message), and the
 * third argument.
 */
int
pfkeyv2_sendmessage(void **headers, int mode, struct socket *socket,
		    u_int8_t satype, int count)
{
    int i, j, rval;
    void *p, *buffer = NULL;
    struct mbuf *packet;
    struct pfkeyv2_socket *s;
    struct sadb_msg *smsg;

    /* Find out how much space we'll need... */
    j = sizeof(struct sadb_msg);

    for (i = 1; i <= SADB_EXT_MAX; i++)
      if (headers[i])
	j += ((struct sadb_ext *)headers[i])->sadb_ext_len * sizeof(uint64_t);

    /* ...and allocate it */
    if (!(buffer = malloc(j + sizeof(struct sadb_msg), M_PFKEY, M_DONTWAIT)))
    {
	rval = ENOMEM;
	goto ret;
    }

    p = buffer + sizeof(struct sadb_msg);
    bcopy(headers[0], p, sizeof(struct sadb_msg));
    ((struct sadb_msg *) p)->sadb_msg_len = j / sizeof(uint64_t);
    p += sizeof(struct sadb_msg);

    /* Copy payloads in the packet */
    for (i = 1; i <= SADB_EXT_MAX; i++)
      if (headers[i])
      {
	  ((struct sadb_ext *) headers[i])->sadb_ext_type = i;
	  bcopy(headers[i], p, EXTLEN(headers[i]));
	  p += EXTLEN(headers[i]);
      }

    if ((rval = pfdatatopacket(buffer + sizeof(struct sadb_msg),
			       j, &packet)) != 0)
      goto ret;

    switch(mode)
    {
	case PFKEYV2_SENDMESSAGE_UNICAST:
	    /*
	     * Send message to the specified socket, plus all
	     * promiscuous listeners.
	     */
	    pfkey_sendup(socket, packet, 0);

	    /*
	     * Promiscuous messages contain the original message
	     * encapsulated in another sadb_msg header.
	     */
	    bzero(buffer, sizeof(struct sadb_msg));
	    smsg = (struct sadb_msg *) buffer;
	    smsg->sadb_msg_version = PF_KEY_V2;
	    smsg->sadb_msg_type = SADB_X_PROMISC;
	    smsg->sadb_msg_len = (sizeof(struct sadb_msg) + j) /
				 sizeof(uint64_t);
	    smsg->sadb_msg_seq = 0;

	    /* Copy to mbuf chain */
	    if ((rval = pfdatatopacket(buffer, sizeof(struct sadb_msg) + j,
				       &packet)) != 0)
	      goto ret;

	    /* 
	     * Search for promiscuous listeners, skipping the
	     * original destination.
	     */
	    for (s = pfkeyv2_sockets; s; s = s->next)
	      if ((s->flags & PFKEYV2_SOCKETFLAGS_PROMISC) &&
		  (s->socket != socket))
		pfkey_sendup(s->socket, packet, 1);

	    /* Done, let's be a bit paranoid */
	    m_zero(packet);
	    m_freem(packet);
	    break;

	case PFKEYV2_SENDMESSAGE_REGISTERED:
	    /*
	     * Send the message to all registered sockets that match
	     * the specified satype (e.g., all IPSEC-ESP negotiators)
	     */
	    for (s = pfkeyv2_sockets; s; s = s->next)
	      if (s->flags & PFKEYV2_SOCKETFLAGS_REGISTERED)
	      {
		  if (!satype)    /* Just send to everyone registered */
		    pfkey_sendup(s->socket, packet, 1);
		  else
		  {
		       /* Check for specified satype */
		      if ((1 << satype) & s->registration)
		          pfkey_sendup(s->socket, packet, 1);
		  }
	      }

	    /* Free last/original copy of the packet */
	    m_freem(packet);

	    /* Encapsulate the original message "inside" an sadb_msg header */
	    bzero(buffer, sizeof(struct sadb_msg));
	    smsg = (struct sadb_msg *) buffer;
	    smsg->sadb_msg_version = PF_KEY_V2;
	    smsg->sadb_msg_type = SADB_X_PROMISC;
	    smsg->sadb_msg_len = (sizeof(struct sadb_msg) + j) /
				 sizeof(uint64_t);
	    smsg->sadb_msg_seq = 0;

	    /* Convert to mbuf chain */
	    if ((rval = pfdatatopacket(buffer, sizeof(struct sadb_msg) + j,
				       &packet)) != 0)
	      goto ret;

	    /* Send to all registered promiscuous listeners */
	    for (s = pfkeyv2_sockets; s; s = s->next)
	      if ((s->flags & PFKEYV2_SOCKETFLAGS_PROMISC) &&
		  !(s->flags & PFKEYV2_SOCKETFLAGS_REGISTERED))
		pfkey_sendup(s->socket, packet, 1);

	    m_freem(packet);
	    break;

	case PFKEYV2_SENDMESSAGE_BROADCAST:
	    /* Send message to all sockets */
	    for (s = pfkeyv2_sockets; s; s = s->next)
	      pfkey_sendup(s->socket, packet, 1);

	    m_freem(packet);
	    break;
    }

 ret:
    if (buffer != NULL)
    {
	bzero(buffer, j + sizeof(struct sadb_msg));
	free(buffer, M_PFKEY);
    }

    return rval;
}

/*
 * Get SPD information for an ACQUIRE. We setup the message such that
 * the SRC/DST payloads are relative to us (regardless of whether the
 * SPD rule was for incoming or outgoing packets).
 */
int
pfkeyv2_policy(struct ipsec_acquire *ipa, void **headers, void **buffer)
{
    union sockaddr_union sunion;
    struct sadb_protocol *sp;
    int rval, i, dir;
    void *p;

    /* Find out how big a buffer we need */
    i = 4 * sizeof(struct sadb_address) + sizeof(struct sadb_protocol);
    bzero(&sunion, sizeof(union sockaddr_union));

    switch (ipa->ipa_info.sen_type)
    {
#ifdef INET
	case SENT_IP4:
	    i += 4 * PADUP(sizeof(struct sockaddr_in));
	    sunion.sa.sa_family = AF_INET;
	    sunion.sa.sa_len = sizeof(struct sockaddr_in);
	    dir = ipa->ipa_info.sen_direction;
	    break;
#endif /* INET */

#ifdef INET6
	case SENT_IP6:
	    i += 4 * PADUP(sizeof(struct sockaddr_in6));
	    sunion.sa.sa_family = AF_INET6;
	    sunion.sa.sa_len = sizeof(struct sockaddr_in6);
	    dir = ipa->ipa_info.sen_ip6_direction;
	    break;
#endif /* INET6 */

	default:
	    return EINVAL;
    }

    if (!(p = malloc(i, M_PFKEY, M_DONTWAIT)))
    {
	rval = ENOMEM;
	goto ret;
    }
    else
    {
	*buffer = p;
	bzero(p, i);
    }

    if (dir == IPSP_DIRECTION_OUT)
      headers[SADB_X_EXT_SRC_FLOW] = p;
    else
      headers[SADB_X_EXT_DST_FLOW] = p;
    switch (sunion.sa.sa_family)
    {
#ifdef INET
	case AF_INET:
	    sunion.sin.sin_addr = ipa->ipa_info.sen_ip_src;
	    sunion.sin.sin_port = ipa->ipa_info.sen_sport;
	    break;
#endif /* INET */

#ifdef INET6
	case AF_INET6:
	    sunion.sin6.sin6_addr = ipa->ipa_info.sen_ip6_src;
	    sunion.sin6.sin6_port = ipa->ipa_info.sen_ip6_sport;
	    break;
#endif /* INET6 */
    }
    export_address(&p, (struct sockaddr *) &sunion);

    if (dir == IPSP_DIRECTION_OUT)
      headers[SADB_X_EXT_SRC_MASK] = p;
    else
      headers[SADB_X_EXT_DST_MASK] = p;
    switch (sunion.sa.sa_family)
    {
#ifdef INET
	case AF_INET:
	    sunion.sin.sin_addr = ipa->ipa_mask.sen_ip_src;
	    sunion.sin.sin_port = ipa->ipa_mask.sen_sport;
	    break;
#endif /* INET */

#ifdef INET6
	case AF_INET6:
	    sunion.sin6.sin6_addr = ipa->ipa_mask.sen_ip6_src;
	    sunion.sin6.sin6_port = ipa->ipa_mask.sen_ip6_sport;
	    break;
#endif /* INET6 */
    }
    export_address(&p, (struct sockaddr *) &sunion);

    if (dir == IPSP_DIRECTION_OUT)
      headers[SADB_X_EXT_DST_FLOW] = p;
    else
      headers[SADB_X_EXT_SRC_FLOW] = p;
    switch (sunion.sa.sa_family)
    {
#ifdef INET
	case AF_INET:
	    sunion.sin.sin_addr = ipa->ipa_info.sen_ip_dst;
	    sunion.sin.sin_port = ipa->ipa_info.sen_dport;
	    break;
#endif /* INET */

#ifdef INET6
	case AF_INET6:
	    sunion.sin6.sin6_addr = ipa->ipa_info.sen_ip6_dst;
	    sunion.sin6.sin6_port = ipa->ipa_info.sen_ip6_dport;
	    break;
#endif /* INET6 */
    }
    export_address(&p, (struct sockaddr *) &sunion);

    if (dir == IPSP_DIRECTION_OUT)
      headers[SADB_X_EXT_DST_MASK] = p;
    else
      headers[SADB_X_EXT_SRC_MASK] = p;
    switch (sunion.sa.sa_family)
    {
#ifdef INET
	case AF_INET:
	    sunion.sin.sin_addr = ipa->ipa_mask.sen_ip_dst;
	    sunion.sin.sin_port = ipa->ipa_mask.sen_dport;
	    break;
#endif /* INET */

#ifdef INET6
	case AF_INET6:
	    sunion.sin6.sin6_addr = ipa->ipa_mask.sen_ip6_dst;
	    sunion.sin6.sin6_port = ipa->ipa_mask.sen_ip6_dport;
	    break;
#endif /* INET6 */
    }
    export_address(&p, (struct sockaddr *) &sunion);

    headers[SADB_X_EXT_FLOW_TYPE] = p;
    sp = p;
    sp->sadb_protocol_len = sizeof(struct sadb_protocol) / sizeof(u_int64_t);
    switch (sunion.sa.sa_family)
    {
#ifdef INET
	case AF_INET:
	    if (ipa->ipa_mask.sen_proto)
	      sp->sadb_protocol_proto = ipa->ipa_info.sen_proto;
	    sp->sadb_protocol_direction = ipa->ipa_info.sen_direction;
	    break;
#endif /* INET */

#ifdef INET6
	case AF_INET6:
	    if (ipa->ipa_mask.sen_ip6_proto)
	      sp->sadb_protocol_proto = ipa->ipa_info.sen_ip6_proto;
	    sp->sadb_protocol_direction = ipa->ipa_info.sen_ip6_direction;
	    break;
#endif /* INET6 */
    }

    rval = 0;

 ret:
    return rval;
}

/*
 * Get all the information contained in an SA to a PFKEYV2 message.
 */
int
pfkeyv2_get(struct tdb *sa, void **headers, void **buffer)
{
    int rval, i;
    void *p;

    /* Find how much space we need */
    i = sizeof(struct sadb_sa) + sizeof(struct sadb_lifetime);

    if (sa->tdb_soft_allocations || sa->tdb_soft_bytes ||
	sa->tdb_soft_timeout || sa->tdb_soft_first_use)
      i += sizeof(struct sadb_lifetime);

    if (sa->tdb_exp_allocations || sa->tdb_exp_bytes ||
	sa->tdb_exp_timeout || sa->tdb_exp_first_use)
      i += sizeof(struct sadb_lifetime);

    if (sa->tdb_src.sa.sa_family)
      i += sizeof(struct sadb_address) + PADUP(SA_LEN(&sa->tdb_src.sa));

    if (sa->tdb_dst.sa.sa_family)
      i += sizeof(struct sadb_address) + PADUP(SA_LEN(&sa->tdb_dst.sa));

    if (sa->tdb_proxy.sa.sa_family)
      i += sizeof(struct sadb_address) + PADUP(SA_LEN(&sa->tdb_proxy.sa));

    if (sa->tdb_srcid_len)
      i += PADUP(sa->tdb_srcid_len) + sizeof(struct sadb_ident);

    if (sa->tdb_dstid_len)
      i += PADUP(sa->tdb_dstid_len) + sizeof(struct sadb_ident);

    if (!(p = malloc(i, M_PFKEY, M_DONTWAIT)))
    {
	rval = ENOMEM;
	goto ret;
    }
    else
    {
	*buffer = p;
	bzero(p, i);
    }

    headers[SADB_EXT_SA] = p;

    export_sa(&p, sa);  /* Export SA information (mostly flags) */

    /* Export lifetimes where applicable */
    headers[SADB_EXT_LIFETIME_CURRENT] = p;
    export_lifetime(&p, sa, PFKEYV2_LIFETIME_CURRENT);

    if (sa->tdb_soft_allocations || sa->tdb_soft_bytes ||
	sa->tdb_soft_first_use || sa->tdb_soft_timeout)
    {
	headers[SADB_EXT_LIFETIME_SOFT] = p;
	export_lifetime(&p, sa, PFKEYV2_LIFETIME_SOFT);
    }

    if (sa->tdb_exp_allocations || sa->tdb_exp_bytes ||
	sa->tdb_exp_first_use || sa->tdb_exp_timeout)
    {
	headers[SADB_EXT_LIFETIME_HARD] = p;
	export_lifetime(&p, sa, PFKEYV2_LIFETIME_HARD);
    }

    /* Export TDB source address */
    headers[SADB_EXT_ADDRESS_SRC] = p;
    export_address(&p, (struct sockaddr *) &sa->tdb_src);

    /* Export TDB destination address */
    headers[SADB_EXT_ADDRESS_DST] = p;
    export_address(&p, (struct sockaddr *) &sa->tdb_dst);

    /* Export TDB proxy address, if present */
    if (SA_LEN(&sa->tdb_proxy.sa))
    {
	headers[SADB_EXT_ADDRESS_PROXY] = p;
	export_address(&p, (struct sockaddr *) &sa->tdb_proxy);
    }

    /* Export source identity, if present */
    if (sa->tdb_srcid_len)
    {
	headers[SADB_EXT_IDENTITY_SRC] = p;
	export_identity(&p, sa, PFKEYV2_IDENTITY_SRC);
    }

    /* Export destination identity, if present */
    if (sa->tdb_dstid_len)
    {
	headers[SADB_EXT_IDENTITY_DST] = p;
	export_identity(&p, sa, PFKEYV2_IDENTITY_DST);
    }

    /* Export credentials, if present */
    if (sa->tdb_credentials)
    {
	headers[SADB_X_EXT_CREDENTIALS] = p;
	export_credentials(&p, sa);
    }

    /* Export authentication key, if present */
    if (sa->tdb_amxkey)
    {
	headers[SADB_EXT_KEY_AUTH] = p;
	export_key(&p, sa, PFKEYV2_AUTHENTICATION_KEY);
    }

    /* Export encryption key, if present */
    if (sa->tdb_emxkey)
    {
	headers[SADB_EXT_KEY_ENCRYPT] = p;
	export_key(&p, sa, PFKEYV2_ENCRYPTION_KEY);
    }

    rval = 0;

 ret:
    return rval;
}

/*
 * Dump a TDB.
 */
int
pfkeyv2_dump_walker(struct tdb *sa, void *state, int last)
{
    struct dump_state *dump_state = (struct dump_state *) state;
    void *headers[SADB_EXT_MAX+1], *buffer;
    int rval;

    /* If not satype was specified, dump all TDBs */
    if (!dump_state->sadb_msg->sadb_msg_satype ||
	(sa->tdb_satype == dump_state->sadb_msg->sadb_msg_satype))
    {
	bzero(headers, sizeof(headers));
	headers[0] = (void *) dump_state->sadb_msg;

	/* Get the information from the TDB to a PFKEYv2 message */
	if ((rval = pfkeyv2_get(sa, headers, &buffer)) != 0)
	  return rval;

	if (last)
	  ((struct sadb_msg *)headers[0])->sadb_msg_seq = 0;

	/* Send the message to the specified socket */
	rval = pfkeyv2_sendmessage(headers, PFKEYV2_SENDMESSAGE_UNICAST,
				   dump_state->socket, 0, 0);

	free(buffer, M_PFKEY);
	if (rval)
	  return rval;
    }

    return 0;
}

/*
 * Delete an SA.
 */
int 
pfkeyv2_flush_walker(struct tdb *sa, void *satype_vp, int last)
{
    if (!(*((u_int8_t *) satype_vp)) ||
	sa->tdb_satype == *((u_int8_t *) satype_vp))
      tdb_delete(sa, 0);

    return 0;
}

/*
 * Convert between SATYPEs and IPsec protocols, taking into consideration
 * sysctl variables enabling/disabling ESP/AH and the presence of the old
 * IPsec transforms.
 */
int
pfkeyv2_get_proto_alg(u_int8_t satype, u_int8_t *sproto, int *alg)
{
    switch (satype)
    {
	case SADB_SATYPE_AH:
	    if (!ah_enable)
	      return EOPNOTSUPP;

	    *sproto = IPPROTO_AH;

	    if(alg != NULL)
	      *alg = satype = XF_AH;

	    break;

	case SADB_SATYPE_ESP:
	    if (!esp_enable)
	      return EOPNOTSUPP;

	    *sproto = IPPROTO_ESP;

	    if(alg != NULL) 
	      *alg = satype = XF_ESP;

	    break;

	case SADB_X_SATYPE_IPIP:
	    *sproto = IPPROTO_IPIP;

	    if (alg != NULL)
	      *alg = XF_IP4;

	    break;

#ifdef TCP_SIGNATURE
	case SADB_X_SATYPE_TCPSIGNATURE:
	    *sproto = IPPROTO_TCP;

	    if (alg != NULL)
	      *alg = XF_TCPSIGNATURE;

	    break;
#endif /* TCP_SIGNATURE */

	default: /* Nothing else supported */
	    return EOPNOTSUPP;
    }

    return 0;
}

/*
 * Handle all messages from userland to kernel.
 */
int
pfkeyv2_send(struct socket *socket, void *message, int len)
{
    int i, j, rval = 0, mode = PFKEYV2_SENDMESSAGE_BROADCAST, delflag = 0, s;
    struct sockaddr_encap encapdst, encapnetmask, encapgw;
    struct ipsec_policy *ipo;
    struct ipsec_acquire *ipa;

    struct pfkeyv2_socket *pfkeyv2_socket, *so = NULL;

    void *freeme = NULL, *bckptr = NULL;
    void *headers[SADB_EXT_MAX + 1];

    union sockaddr_union *sunionp;

    struct tdb sa, *sa2 = NULL;

    struct sadb_msg *smsg;
    struct sadb_spirange *sprng;
    struct sadb_sa *ssa;
    struct sadb_supported *ssup;
    struct sadb_ident *sid;

    /* Verify that we received this over a legitimate pfkeyv2 socket */
    bzero(headers, sizeof(headers));

    for (pfkeyv2_socket = pfkeyv2_sockets;
	 pfkeyv2_socket;
	 pfkeyv2_socket = pfkeyv2_socket->next)
      if (pfkeyv2_socket->socket == socket)
	break;

    if (!pfkeyv2_socket)
    {
	rval = EINVAL;
	goto ret;
    }

    /* If we have any promiscuous listeners, send them a copy of the message */
    if (npromisc)
    {
	struct mbuf *packet;

	if (!(freeme = malloc(sizeof(struct sadb_msg) + len, M_PFKEY,
			      M_DONTWAIT)))
	{
	    rval = ENOMEM;
	    goto ret;
	}

	/* Initialize encapsulating header */
	bzero(freeme, sizeof(struct sadb_msg));
	smsg = (struct sadb_msg *) freeme;
	smsg->sadb_msg_version = PF_KEY_V2;
	smsg->sadb_msg_type = SADB_X_PROMISC;
	smsg->sadb_msg_len = (sizeof(struct sadb_msg) + len) /
			     sizeof(uint64_t);
	smsg->sadb_msg_seq = curproc->p_pid;

	bcopy(message, freeme + sizeof(struct sadb_msg), len);

	/* Convert to mbuf chain */
	if ((rval = pfdatatopacket(freeme, sizeof(struct sadb_msg) + len,
				   &packet)) != 0)
	  goto ret;

	/* Send to all promiscuous listeners */
	for (so = pfkeyv2_sockets; so; so = so->next)
	  if (so->flags & PFKEYV2_SOCKETFLAGS_PROMISC)
	    pfkey_sendup(so->socket, packet, 1);

	/* Paranoid */
	m_zero(packet);
	m_freem(packet);

	/* Even more paranoid */
	bzero(freeme, sizeof(struct sadb_msg) + len);
	free(freeme, M_PFKEY);
	freeme = NULL;
    }

    /* Validate message format */
    if ((rval = pfkeyv2_parsemessage(message, len, headers)) != 0)
      goto ret;
 
    smsg = (struct sadb_msg *) headers[0];
    switch(smsg->sadb_msg_type)
    {
	case SADB_GETSPI:  /* Reserve an SPI */
	    bzero(&sa, sizeof(struct tdb));

	    sa.tdb_satype = smsg->sadb_msg_satype;
	    if ((rval = pfkeyv2_get_proto_alg(sa.tdb_satype,
					      &sa.tdb_sproto, 0)))
	      goto ret;

	    import_address((struct sockaddr *) &sa.tdb_src,
			   headers[SADB_EXT_ADDRESS_SRC]);
	    import_address((struct sockaddr *) &sa.tdb_dst,
			   headers[SADB_EXT_ADDRESS_DST]);

	    /* Find an unused SA identifier */
	    sprng = (struct sadb_spirange *) headers[SADB_EXT_SPIRANGE];
	    sa.tdb_spi = reserve_spi(sprng->sadb_spirange_min,
				     sprng->sadb_spirange_max,
				     &sa.tdb_src, &sa.tdb_dst,
				     sa.tdb_sproto, &rval);
	    if (sa.tdb_spi == 0)
	      goto ret;

	    /* Send a message back telling what the SA (the SPI really) is */
	    if (!(freeme = malloc(sizeof(struct sadb_sa), M_PFKEY,
				  M_DONTWAIT)))
	    {
		rval = ENOMEM;
		goto ret;
	    }

	    bzero(freeme, sizeof(struct sadb_sa));
	    headers[SADB_EXT_SPIRANGE] = NULL;
	    headers[SADB_EXT_SA] = freeme;
	    bckptr = freeme;

	    /* We really only care about the SPI, but we'll export the SA */
	    export_sa((void **) &bckptr, &sa);
	    break;

	case SADB_UPDATE:
	    ssa = (struct sadb_sa *) headers[SADB_EXT_SA];
	    sunionp = (union sockaddr_union *) (headers[SADB_EXT_ADDRESS_DST] +
						sizeof(struct sadb_address));
	    s = spltdb();

	    /* Find TDB */
	    sa2 = gettdb(ssa->sadb_sa_spi, sunionp,
			 SADB_GETSPROTO(smsg->sadb_msg_satype));

	    /* If there's no such SA, we're done */
	    if (sa2 == NULL)
	    {
		rval = ESRCH;
		goto splxret;
	    }

	    /* If this is a reserved SA */
	    if (sa2->tdb_flags & TDBF_INVALID)
	    {
		struct tdb *newsa;
		struct ipsecinit ii;
		int alg;

		/* Create new TDB */
		MALLOC(freeme, struct tdb *, sizeof(struct tdb),
		       M_TDB, M_WAITOK);
		bzero(freeme, sizeof(struct tdb));
		bzero(&ii, sizeof(struct ipsecinit));

		newsa = (struct tdb *) freeme;
		newsa->tdb_satype = smsg->sadb_msg_satype;

		if ((rval = pfkeyv2_get_proto_alg(newsa->tdb_satype, 
						  &newsa->tdb_sproto, &alg)))
		  goto splxret;

		/* Initialize SA */
		import_sa(newsa, headers[SADB_EXT_SA], &ii);
		import_address((struct sockaddr *) &newsa->tdb_src,
			       headers[SADB_EXT_ADDRESS_SRC]);
		import_address((struct sockaddr *) &newsa->tdb_dst,
			       headers[SADB_EXT_ADDRESS_DST]);
		import_address((struct sockaddr *) &newsa->tdb_proxy,
			       headers[SADB_EXT_ADDRESS_PROXY]);
		import_lifetime(newsa, headers[SADB_EXT_LIFETIME_CURRENT],
				PFKEYV2_LIFETIME_CURRENT);
		import_lifetime(newsa, headers[SADB_EXT_LIFETIME_SOFT],
				PFKEYV2_LIFETIME_SOFT);
		import_lifetime(newsa, headers[SADB_EXT_LIFETIME_HARD],
				PFKEYV2_LIFETIME_HARD);
		import_key(&ii, headers[SADB_EXT_KEY_AUTH],
			   PFKEYV2_AUTHENTICATION_KEY);
		import_key(&ii, headers[SADB_EXT_KEY_ENCRYPT],
			   PFKEYV2_ENCRYPTION_KEY);
		import_identity(newsa, headers[SADB_EXT_IDENTITY_SRC],
				PFKEYV2_IDENTITY_SRC);
		import_identity(newsa, headers[SADB_EXT_IDENTITY_DST],
				PFKEYV2_IDENTITY_DST);
		import_credentials(newsa, headers[SADB_X_EXT_CREDENTIALS]);

		headers[SADB_EXT_KEY_AUTH] = NULL;
		headers[SADB_EXT_KEY_ENCRYPT] = NULL;

		rval = tdb_init(newsa, alg, &ii);
		if (rval)
		{
		    rval = EINVAL;
		    tdb_delete(freeme, TDBEXP_TIMEOUT);
		    freeme = NULL;
		    goto splxret;
		}

		newsa->tdb_cur_allocations = sa2->tdb_cur_allocations;

		/* Delete old version of the SA, insert new one */
		tdb_delete(sa2, TDBEXP_TIMEOUT);
		puttdb((struct tdb *) freeme);
		sa2 = freeme = NULL;
	    }
	    else
	    {
		/*
		 * The SA is already initialized, so we're only allowed to
		 * change lifetimes and some other information; we're
		 * not allowed to change keys, addresses or identities.
		 */
		if (headers[SADB_EXT_ADDRESS_PROXY] ||
		    headers[SADB_EXT_KEY_AUTH] ||
		    headers[SADB_EXT_KEY_ENCRYPT] ||
		    headers[SADB_EXT_IDENTITY_SRC] ||
		    headers[SADB_EXT_IDENTITY_DST] ||
		    headers[SADB_EXT_SENSITIVITY])
		{
		    rval = EINVAL;
		    goto splxret;
		}

		import_sa(sa2, headers[SADB_EXT_SA], NULL);
		import_lifetime(sa2, headers[SADB_EXT_LIFETIME_CURRENT],
				PFKEYV2_LIFETIME_CURRENT);
		import_lifetime(sa2, headers[SADB_EXT_LIFETIME_SOFT],
				PFKEYV2_LIFETIME_SOFT);
		import_lifetime(sa2, headers[SADB_EXT_LIFETIME_HARD],
				PFKEYV2_LIFETIME_HARD);
	    }

	    splx(s);
	    break;

	case SADB_ADD:
	    ssa = (struct sadb_sa *) headers[SADB_EXT_SA];
	    sunionp = (union sockaddr_union *) (headers[SADB_EXT_ADDRESS_DST] +
						sizeof(struct sadb_address));

	    s = spltdb();

	    sa2 = gettdb(ssa->sadb_sa_spi, sunionp,
			 SADB_GETSPROTO(smsg->sadb_msg_satype));

	    /* We can't add an existing SA! */
	    if (sa2 != NULL)
	    {
		rval = EEXIST;
		goto splxret;
	    }

	    /* We can only add "mature" SAs */
	    if (ssa->sadb_sa_state != SADB_SASTATE_MATURE)
	    {
		rval = EINVAL;
		goto splxret;
	    }

	    /* Allocate and initialize new TDB */
	    MALLOC(freeme, struct tdb *, sizeof(struct tdb), M_TDB, M_WAITOK);
	    bzero(freeme, sizeof(struct tdb));

	    {
		struct tdb *newsa = (struct tdb *) freeme;
		struct ipsecinit ii;
		int alg;

		bzero(&ii, sizeof(struct ipsecinit));

		newsa->tdb_satype = smsg->sadb_msg_satype;
		if ((rval = pfkeyv2_get_proto_alg(newsa->tdb_satype, 
						  &newsa->tdb_sproto, &alg)))
		  goto splxret;

		import_sa(newsa, headers[SADB_EXT_SA], &ii);
		import_address((struct sockaddr *) &newsa->tdb_src,
			       headers[SADB_EXT_ADDRESS_SRC]);
		import_address((struct sockaddr *) &newsa->tdb_dst,
			       headers[SADB_EXT_ADDRESS_DST]);
		import_address((struct sockaddr *) &newsa->tdb_proxy,
			       headers[SADB_EXT_ADDRESS_PROXY]);

		import_lifetime(newsa, headers[SADB_EXT_LIFETIME_CURRENT],
				PFKEYV2_LIFETIME_CURRENT);
		import_lifetime(newsa, headers[SADB_EXT_LIFETIME_SOFT],
				PFKEYV2_LIFETIME_SOFT);
		import_lifetime(newsa, headers[SADB_EXT_LIFETIME_HARD],
				PFKEYV2_LIFETIME_HARD);

		import_key(&ii, headers[SADB_EXT_KEY_AUTH],
			   PFKEYV2_AUTHENTICATION_KEY);
		import_key(&ii, headers[SADB_EXT_KEY_ENCRYPT],
			   PFKEYV2_ENCRYPTION_KEY);

		import_identity(newsa, headers[SADB_EXT_IDENTITY_SRC],
				PFKEYV2_IDENTITY_SRC);
		import_identity(newsa, headers[SADB_EXT_IDENTITY_DST],
				PFKEYV2_IDENTITY_DST);

		import_credentials(newsa, headers[SADB_X_EXT_CREDENTIALS]);

		headers[SADB_EXT_KEY_AUTH] = NULL;
		headers[SADB_EXT_KEY_ENCRYPT] = NULL;

		rval = tdb_init(newsa, alg, &ii);
		if (rval)
		{
		    rval = EINVAL;
		    tdb_delete(freeme, TDBEXP_TIMEOUT);
		    freeme = NULL;
		    goto splxret;
		}
	    }

	     /* Add TDB in table */
	     puttdb((struct tdb *) freeme);

	     splx(s);

	     freeme = NULL;
	     break;

	case SADB_DELETE:
	    ssa = (struct sadb_sa *) headers[SADB_EXT_SA];
	    sunionp = (union sockaddr_union *) (headers[SADB_EXT_ADDRESS_DST] +
						sizeof(struct sadb_address));
	    s = spltdb();

	    sa2 = gettdb(ssa->sadb_sa_spi, sunionp,
			 SADB_GETSPROTO(smsg->sadb_msg_satype));
	    if (sa2 == NULL)
	    {
		rval = ESRCH;
		goto splxret;
	    }
      
	    tdb_delete(sa2, TDBEXP_TIMEOUT);

	    splx(s);

	    sa2 = NULL;
	    break;

	case SADB_X_ASKPOLICY:
	    /* Get the relevant policy */
	    ipa = ipsec_get_acquire(((struct sadb_policy *) headers[SADB_X_EXT_POLICY])->sadb_policy_seq);
	    if (ipa == NULL)
	    {
		rval = ESRCH;
		goto ret;
	    }

	    rval = pfkeyv2_policy(ipa, headers, &freeme);
	    if (rval)
	      mode = PFKEYV2_SENDMESSAGE_UNICAST;

	    break;

	case SADB_GET:
	    ssa = (struct sadb_sa *) headers[SADB_EXT_SA];
	    sunionp = (union sockaddr_union *) (headers[SADB_EXT_ADDRESS_DST] +
						sizeof(struct sadb_address));
	    s = spltdb();

	    sa2 = gettdb(ssa->sadb_sa_spi, sunionp,
			 SADB_GETSPROTO(smsg->sadb_msg_satype));
	    if (sa2 == NULL)
	    {
		rval = ESRCH;
		goto splxret;
	    }
      
	    rval = pfkeyv2_get(sa2, headers, &freeme);
	    if (rval)
	      mode = PFKEYV2_SENDMESSAGE_UNICAST;

	    splx(s);

	    break;

	case SADB_REGISTER:
	    pfkeyv2_socket->flags |= PFKEYV2_SOCKETFLAGS_REGISTERED;
	    nregistered++;

	    i = sizeof(struct sadb_supported) + sizeof(ealgs);

	    if (!(freeme = malloc(i, M_PFKEY, M_DONTWAIT)))
	    {
		rval = ENOMEM;
		goto ret;
	    }
      
	    bzero(freeme, i);

	    ssup = (struct sadb_supported *) freeme;
	    ssup->sadb_supported_len = i / sizeof(uint64_t);

	    {
		void *p = freeme + sizeof(struct sadb_supported);

		bcopy(&ealgs[0], p, sizeof(ealgs));
	    }

	    headers[SADB_EXT_SUPPORTED_ENCRYPT] = freeme;

	    i = sizeof(struct sadb_supported) + sizeof(aalgs);

	    if (!(freeme = malloc(i, M_PFKEY, M_DONTWAIT)))
	    {
		rval = ENOMEM;
		goto ret;
	    }

	    /* Keep track what this socket has registered for */
	    pfkeyv2_socket->registration |= (1 << ((struct sadb_msg *)message)->sadb_msg_satype);
      
	    bzero(freeme, i);

	    ssup = (struct sadb_supported *) freeme;
	    ssup->sadb_supported_len = i / sizeof(uint64_t);

	    {
		void *p = freeme + sizeof(struct sadb_supported);

		bcopy(&aalgs[0], p, sizeof(aalgs));
	    }

	    headers[SADB_EXT_SUPPORTED_AUTH] = freeme;

	    break;

	case SADB_ACQUIRE:
	case SADB_EXPIRE:
	    /* Nothing to handle */
	    rval = 0;
	    break;

	case SADB_FLUSH:
	    rval = 0;

            s = spltdb();
            while ((ipo = TAILQ_FIRST(&ipsec_policy_head)) != NULL)
              ipsec_delete_policy(ipo);
            splx(s);

	    switch(smsg->sadb_msg_satype)
	    {
		case SADB_SATYPE_UNSPEC:  
		case SADB_SATYPE_AH:
		case SADB_SATYPE_ESP:
		case SADB_X_SATYPE_IPIP:
#ifdef TCP_SIGNATURE
		case SADB_X_SATYPE_TCPSIGNATURE:
#endif /* TCP_SIGNATURE */
		    s = spltdb();

		    tdb_walk(pfkeyv2_flush_walker, 
			     (u_int8_t *) &(smsg->sadb_msg_satype));

		    splx(s);
		    break;

		default:
		    rval = EINVAL; /* Unknown/unsupported type */
	    }

	    break;

	case SADB_DUMP:
	{
	    struct dump_state dump_state;
	    dump_state.sadb_msg = (struct sadb_msg *) headers[0];
	    dump_state.socket = socket;

	    if (!(rval = tdb_walk(pfkeyv2_dump_walker, &dump_state)))
	      goto realret;

	    if ((rval == ENOMEM) || (rval == ENOBUFS))
	      rval = 0;
	}

	 break;

	case SADB_X_GRPSPIS:
	{
	    struct tdb *tdb1, *tdb2, *tdb3;
	    struct sadb_protocol *sa_proto;

	    ssa = (struct sadb_sa *) headers[SADB_EXT_SA];
	    sunionp = (union sockaddr_union *) (headers[SADB_EXT_ADDRESS_DST] +
						sizeof(struct sadb_address));

	    s = spltdb();

	    tdb1 = gettdb(ssa->sadb_sa_spi, sunionp,
			  SADB_GETSPROTO(smsg->sadb_msg_satype));
	    if (tdb1 == NULL)
	    {
		rval = ESRCH;
		goto splxret;
	    }

	    ssa = (struct sadb_sa *) headers[SADB_X_EXT_SA2];
	    sunionp = (union sockaddr_union *) (headers[SADB_X_EXT_DST2] +
						sizeof(struct sadb_address));
	    sa_proto = ((struct sadb_protocol *) headers[SADB_X_EXT_PROTOCOL]);

	    tdb2 = gettdb(ssa->sadb_sa_spi, sunionp,
			  SADB_GETSPROTO(sa_proto->sadb_protocol_proto));
	    if (tdb2 == NULL)
	    {
		rval = ESRCH;
		goto splxret;
	    }

	    /* Detect cycles */
	    for (tdb3 = tdb2; tdb3; tdb3 = tdb3->tdb_onext)
	      if (tdb3 == tdb1)
	      {
		  rval = ESRCH;
		  goto splxret;
	      }

	    /* Maintenance */
	    if ((tdb1->tdb_onext) &&
		(tdb1->tdb_onext->tdb_inext == tdb1))
	      tdb1->tdb_onext->tdb_inext = NULL;

	    if ((tdb2->tdb_inext) &&
		(tdb2->tdb_inext->tdb_onext == tdb2))
	      tdb2->tdb_inext->tdb_onext = NULL;

	    /* Link them */
	    tdb1->tdb_onext = tdb2;
	    tdb2->tdb_inext = tdb1;

	    splx(s);
	}
	 break;
	 
	case SADB_X_DELFLOW:
	    delflag = 1;   /* fall through */
	
	case SADB_X_ADDFLOW:
	{
	    union sockaddr_union *src, *dst, *srcmask, *dstmask, *ssrc;
	    struct route_enc re;
	    u_int8_t transproto = 0;
	    u_int8_t direction;
	    int exists = 0;

	    direction = (((struct sadb_protocol *) headers[SADB_X_EXT_FLOW_TYPE])->sadb_protocol_direction);
            if ((direction != IPSP_DIRECTION_IN) &&
                (direction != IPSP_DIRECTION_OUT))
            {
		rval = EINVAL;
		goto ret;
            }

            /* If the security protocol wasn't specified, pretend it was ESP */
            if (smsg->sadb_msg_satype == 0)
              smsg->sadb_msg_satype = SADB_SATYPE_ESP;

	    if (headers[SADB_EXT_ADDRESS_DST])
	      sunionp = (union sockaddr_union *)
			(headers[SADB_EXT_ADDRESS_DST] +
			 sizeof(struct sadb_address));
	    else
	      sunionp = NULL;

	    if (headers[SADB_EXT_ADDRESS_SRC])
	      ssrc = (union sockaddr_union *)
		     (headers[SADB_EXT_ADDRESS_SRC] +
		      sizeof(struct sadb_address));
	    else
	      ssrc = NULL;

	    src = (union sockaddr_union *) (headers[SADB_X_EXT_SRC_FLOW] +
					    sizeof(struct sadb_address));
	    dst = (union sockaddr_union *) (headers[SADB_X_EXT_DST_FLOW] +
					    sizeof(struct sadb_address));
	    srcmask = (union sockaddr_union *) (headers[SADB_X_EXT_SRC_MASK] +
						sizeof(struct sadb_address));
	    dstmask = (union sockaddr_union *) (headers[SADB_X_EXT_DST_MASK] +
						sizeof(struct sadb_address));

	    /*
	     * Check that all the address families match. We know they are
	     * valid and supported because pfkeyv2_parsemessage() checked that.
	     */
	    if ((src->sa.sa_family != dst->sa.sa_family) ||
		(src->sa.sa_family != srcmask->sa.sa_family) ||
		(src->sa.sa_family != dstmask->sa.sa_family))
	    {
		rval = EINVAL;
		goto ret;
	    }

	    bzero(&encapdst, sizeof(struct sockaddr_encap));
	    bzero(&encapnetmask, sizeof(struct sockaddr_encap));
	    bzero(&encapgw, sizeof(struct sockaddr_encap));

	    /* Transport protocol specified ? */
	    if (headers[SADB_X_EXT_PROTOCOL])
	      transproto = ((struct sadb_protocol *) headers[SADB_X_EXT_PROTOCOL])->sadb_protocol_proto;

	    /* Generic netmask handling, works for IPv4 and IPv6 */
	    rt_maskedcopy(&src->sa, &src->sa, &srcmask->sa);
	    rt_maskedcopy(&dst->sa, &dst->sa, &dstmask->sa);

	    /* Setup the encap fields */
	    encapdst.sen_family = encapnetmask.sen_family = PF_KEY;
	    encapdst.sen_len = encapnetmask.sen_len = SENT_LEN;

	    switch (src->sa.sa_family)
	    {
#ifdef INET
		case AF_INET:
		    encapdst.sen_type = SENT_IP4;
		    encapdst.sen_direction = direction;
		    encapdst.sen_ip_src = src->sin.sin_addr;
		    encapdst.sen_ip_dst = dst->sin.sin_addr;
		    encapdst.sen_proto = transproto;
		    encapdst.sen_sport = src->sin.sin_port;
		    encapdst.sen_dport = dst->sin.sin_port;

		    encapnetmask.sen_type = SENT_IP4;
		    encapnetmask.sen_direction = 0xff;
		    encapnetmask.sen_ip_src = srcmask->sin.sin_addr;
		    encapnetmask.sen_ip_dst = dstmask->sin.sin_addr;
                    encapnetmask.sen_sport = srcmask->sin.sin_port;
                    encapnetmask.sen_dport = dstmask->sin.sin_port;
                    if (transproto)
                      encapnetmask.sen_proto = 0xff;
		    break;
#endif /* INET */

#ifdef INET6
		case AF_INET6:
		    encapdst.sen_type = SENT_IP6;
		    encapdst.sen_ip6_direction = direction;
		    encapdst.sen_ip6_src = src->sin6.sin6_addr;
		    encapdst.sen_ip6_dst = dst->sin6.sin6_addr;
		    encapdst.sen_ip6_proto = transproto;
		    encapdst.sen_ip6_sport = src->sin6.sin6_port;
		    encapdst.sen_ip6_dport = dst->sin6.sin6_port;

		    encapnetmask.sen_type = SENT_IP6;
		    encapnetmask.sen_ip6_direction = 0xff;
		    encapnetmask.sen_ip6_src = srcmask->sin6.sin6_addr;
		    encapnetmask.sen_ip6_dst = dstmask->sin6.sin6_addr;
                    encapnetmask.sen_ip6_sport = srcmask->sin6.sin6_port;
                    encapnetmask.sen_ip6_dport = dstmask->sin6.sin6_port;
                    if (transproto)
                      encapnetmask.sen_ip6_proto = 0xff;
		    break;
#endif /* INET6 */
	    }

	    /* Determine whether the exact same SPD entry already exists. */
            bzero(&re, sizeof(struct route_enc));
	    bcopy(&encapdst, &re.re_dst, sizeof(struct sockaddr_encap));
	    rtalloc((struct route *) &re);

	    if (re.re_rt != NULL)
	    {
		ipo = ((struct sockaddr_encap *) re.re_rt->rt_gateway)->sen_ipsp;
		RTFREE(re.re_rt);

		/* Verify that the entry is identical */
		if (bcmp(&ipo->ipo_addr, &encapdst,
			 sizeof(struct sockaddr_encap)) ||
		    bcmp(&ipo->ipo_mask, &encapnetmask,
			 sizeof(struct sockaddr_encap)))
		  ipo = NULL; /* Fall through */
		else
		  exists = 1;
	    }
            else
              ipo = NULL;

	    /* Delete ? */
	    if (delflag)
	    {
		if (exists)
		{
		    s = spltdb();
		    rval = ipsec_delete_policy(ipo);
		    splx(s);
		    goto ret;
		}

		/* If we were asked to delete something non-existant, error */
		rval = ESRCH;
		break;
	    }

	    if (!exists)
	    {
		/* Allocate policy entry */
		MALLOC(ipo, struct ipsec_policy *, sizeof(struct ipsec_policy),
		       M_TDB, M_NOWAIT);
		if (ipo == NULL)
		{
		    rval = ENOMEM;
		    goto ret;
		}

		bzero(ipo, sizeof(struct ipsec_policy));

		/* Finish initialization of SPD entry */
		encapgw.sen_len = SENT_LEN;
		encapgw.sen_family = PF_KEY;
		encapgw.sen_type = SENT_IPSP;
		encapgw.sen_ipsp = ipo;

		/* Initialize policy entry */
		bcopy(&encapdst, &ipo->ipo_addr,
		      sizeof(struct sockaddr_encap));
		bcopy(&encapnetmask, &ipo->ipo_mask,
		      sizeof(struct sockaddr_encap));
	    }

	    switch (((struct sadb_protocol *) headers[SADB_X_EXT_FLOW_TYPE])->sadb_protocol_proto)
	    {
		case FLOW_X_TYPE_USE:
		    ipo->ipo_type = IPSP_IPSEC_USE;
		    break;

		case FLOW_X_TYPE_ACQUIRE:
		    ipo->ipo_type = IPSP_IPSEC_ACQUIRE;
		    break;

		case FLOW_X_TYPE_REQUIRE:
		    ipo->ipo_type = IPSP_IPSEC_REQUIRE;
		    break;

		case FLOW_X_TYPE_DENY:
		    ipo->ipo_type = IPSP_DENY;
		    break;

		case FLOW_X_TYPE_BYPASS:
		    ipo->ipo_type = IPSP_PERMIT;
		    break;

		case FLOW_X_TYPE_DONTACQ:
		    ipo->ipo_type = IPSP_IPSEC_DONTACQ;
		    break;

		default:
                    if (!exists)
		      FREE(ipo, M_TDB);
                    else
		    {
			s = spltdb();
			ipsec_delete_policy(ipo);
			splx(s);
		    }

		    rval = EINVAL;
		    goto ret;
	    }

            if (sunionp)
	      bcopy(sunionp, &ipo->ipo_dst, sizeof(union sockaddr_union));
            else
            {
		bzero(&ipo->ipo_dst, sizeof(union sockaddr_union));
                ipo->ipo_dst.sa.sa_family = src->sa.sa_family;
                ipo->ipo_dst.sa.sa_len = src->sa.sa_len;
            }

	    if (ssrc)
	      bcopy(ssrc, &ipo->ipo_src, sizeof(union sockaddr_union));
	    else
	    {
		bzero(&ipo->ipo_src, sizeof(union sockaddr_union));
		ipo->ipo_src.sa.sa_family = src->sa.sa_family;
		ipo->ipo_src.sa.sa_len = src->sa.sa_len;
	    }

	    ipo->ipo_sproto = SADB_GETSPROTO(smsg->sadb_msg_satype);
	    if (ipo->ipo_srcid)
	    {
		FREE(ipo->ipo_srcid, M_TEMP);
		ipo->ipo_srcid = NULL;
	    }

	    if (ipo->ipo_dstid)
	    {
		FREE(ipo->ipo_dstid, M_TEMP);
		ipo->ipo_dstid = NULL;
	    }

	    if ((sid = headers[SADB_EXT_IDENTITY_SRC]) != NULL)
	    {
		ipo->ipo_srcid_type = sid->sadb_ident_type;
		ipo->ipo_srcid_len = (sid->sadb_ident_len * sizeof(u_int64_t)) - sizeof(struct sadb_ident);

		MALLOC(ipo->ipo_srcid, u_int8_t *, ipo->ipo_srcid_len,
		       M_TEMP, M_DONTWAIT);
		if (ipo->ipo_srcid == NULL)
		{
		    if (exists)
		    {
			s = spltdb();
			ipsec_delete_policy(ipo);
			splx(s);
		    }
		    else
		      FREE(ipo, M_TDB);
		    rval = ENOBUFS;
		    goto ret;
		}

		bcopy(sid + 1, ipo->ipo_srcid, ipo->ipo_srcid_len);
	    }

	    if ((sid = headers[SADB_EXT_IDENTITY_DST]) != NULL)
	    {
		ipo->ipo_dstid_type = sid->sadb_ident_type;
		ipo->ipo_dstid_len = (sid->sadb_ident_len * sizeof(u_int64_t)) -
				     sizeof(struct sadb_ident);

		MALLOC(ipo->ipo_dstid, u_int8_t *, ipo->ipo_dstid_len,
		       M_TEMP, M_DONTWAIT);
		if (ipo->ipo_dstid == NULL)
		{
		    if (exists)
		    {
			s = spltdb();
			ipsec_delete_policy(ipo);
			splx(s);
		    }
		    else
		    {
			if (ipo->ipo_dstid)
			  FREE(ipo->ipo_dstid, M_TEMP);
			FREE(ipo, M_TDB);
		    }

		    rval = ENOBUFS;
		    goto ret;
		}

		bcopy(sid + 1, ipo->ipo_dstid, ipo->ipo_dstid_len);
	    }

	    /* Flow type */
	    if (!exists)
	    {
		/* Add SPD entry */
		if ((rval = rtrequest(RTM_ADD, (struct sockaddr *) &encapdst,
				      (struct sockaddr *) &encapgw,
				      (struct sockaddr *) &encapnetmask,
				      RTF_UP | RTF_GATEWAY | RTF_STATIC,
				      (struct rtentry **) 0)) != 0)
		{
		    /* Remove from linked list of policies on TDB */
		    if (ipo->ipo_tdb)
		    {
			s = spltdb();
			TAILQ_REMOVE(&ipo->ipo_tdb->tdb_policy_head, ipo,
				     ipo_tdb_next);
			splx(s);
		    }

		    if (ipo->ipo_srcid)
		      FREE(ipo->ipo_srcid, M_TEMP);
		    if (ipo->ipo_dstid)
		      FREE(ipo->ipo_dstid, M_TEMP);
		    FREE(ipo, M_TDB); /* Free policy entry */
		    goto ret;
		}

		s = spltdb();
		TAILQ_INSERT_HEAD(&ipsec_policy_head, ipo, ipo_list);
		splx(s);

		ipsec_in_use++;
	    }
	    else
	    {
		ipo->ipo_last_searched = ipo->ipo_flags = 0;
	    }
         }
	 break;
	
	case SADB_X_PROMISC:
	    if (len >= 2 * sizeof(struct sadb_msg))
	    {
		struct mbuf *packet;

		if ((rval = pfdatatopacket(message, len, &packet)) != 0)
		  goto ret;

		for (so = pfkeyv2_sockets; so; so = so->next)
		  if ((so != pfkeyv2_socket) &&
		      (!smsg->sadb_msg_seq ||
		       (smsg->sadb_msg_seq == pfkeyv2_socket->pid)))
		    pfkey_sendup(so->socket, packet, 1);

		m_freem(packet);
	    }
	    else
	    {
		if (len != sizeof(struct sadb_msg))
		{
		    rval = EINVAL;
		    goto ret;
		}

		i = (pfkeyv2_socket->flags &
		     PFKEYV2_SOCKETFLAGS_PROMISC) ? 1 : 0;
		j = smsg->sadb_msg_satype ? 1 : 0;

		if (i ^ j)
		{
		    if (j)
		    {
			pfkeyv2_socket->flags |= PFKEYV2_SOCKETFLAGS_PROMISC;
			npromisc++;
		    }
		    else
		    {
			pfkeyv2_socket->flags &= ~PFKEYV2_SOCKETFLAGS_PROMISC;
			npromisc--;
		    }
		}
	    }

	    break;

	default:
	    rval = EINVAL;
	    goto ret;
    }

ret:
    if (rval)
    {
	if ((rval == EINVAL) || (rval == ENOMEM) || (rval == ENOBUFS))
	  goto realret;

	for (i = 1; i <= SADB_EXT_MAX; i++)
	  headers[i] = NULL;

	smsg->sadb_msg_errno = abs(rval);
    }
    else
    {
	uint32_t seen = 0;

	for (i = 1; i <= SADB_EXT_MAX; i++)
	  if (headers[i])
	    seen |= (1 << i);

	if ((seen & sadb_exts_allowed_out[smsg->sadb_msg_type]) != seen)
	  goto realret;

	if ((seen & sadb_exts_required_out[smsg->sadb_msg_type]) !=
	    sadb_exts_required_out[smsg->sadb_msg_type])
	  goto realret;
    }

    rval = pfkeyv2_sendmessage(headers, mode, socket, 0, 0);

realret:
    if (freeme)
      free(freeme, M_PFKEY);

    free(message, M_PFKEY);

    return rval;

splxret:
    splx(s);
    goto ret;
}

/*
 * Send an ACQUIRE message to key management, to get a new SA.
 */
int
pfkeyv2_acquire(struct ipsec_policy *ipo, union sockaddr_union *gw,
		union sockaddr_union *laddr, u_int32_t *seq,
		struct sockaddr_encap *ddst)
{
    void *p, *headers[SADB_EXT_MAX + 1], *buffer = NULL;
    struct sadb_ident *srcid, *dstid;
    struct sadb_comb *sadb_comb;
    struct sadb_address *sadd;
    struct sadb_prop *sa_prop;
    struct sadb_msg *smsg;
    int rval = 0;
    int i, j;

    *seq = pfkeyv2_seq++;

    if (!nregistered)
    {
	rval = ESRCH;
	goto ret;
    }

    /* How large a buffer do we need... XXX we only do one proposal for now */
    i = sizeof(struct sadb_msg) +
        (laddr == NULL ? 0 : sizeof(struct sadb_address) +
                             PADUP(SA_LEN(&ipo->ipo_src.sa))) +
        sizeof(struct sadb_address) + PADUP(SA_LEN(&gw->sa)) +
        sizeof(struct sadb_prop) + 1 * sizeof(struct sadb_comb);

    if (ipo->ipo_srcid)
      i += sizeof(struct sadb_ident) + PADUP(ipo->ipo_srcid_len);

    if (ipo->ipo_dstid)
      i += sizeof(struct sadb_ident) + PADUP(ipo->ipo_dstid_len);

    /* Allocate */
    if (!(p = malloc(i, M_PFKEY, M_DONTWAIT)))
    {
	rval = ENOMEM;
	goto ret;
    }

    bzero(headers, sizeof(headers));

    buffer = p;
    bzero(p, i);

    headers[0] = p;
    p += sizeof(struct sadb_msg);

    smsg = (struct sadb_msg *) headers[0];
    smsg->sadb_msg_version = PF_KEY_V2;
    smsg->sadb_msg_type = SADB_ACQUIRE;
    smsg->sadb_msg_len = i / sizeof(uint64_t);
    smsg->sadb_msg_seq = *seq;

    if (ipo->ipo_sproto == IPPROTO_ESP)
      smsg->sadb_msg_satype = SADB_SATYPE_ESP;
    else
      smsg->sadb_msg_satype = SADB_SATYPE_AH;

    if (laddr)
    {
        headers[SADB_EXT_ADDRESS_SRC] = p;
        p += sizeof(struct sadb_address) + PADUP(SA_LEN(&laddr->sa));
        sadd = (struct sadb_address *) headers[SADB_EXT_ADDRESS_SRC];
        sadd->sadb_address_len = (sizeof(struct sadb_address) +
			         SA_LEN(&laddr->sa) +
			         sizeof(uint64_t) - 1) / sizeof(uint64_t);
        bcopy(laddr,
	      headers[SADB_EXT_ADDRESS_SRC] + sizeof(struct sadb_address),
	      SA_LEN(&laddr->sa));
    }

    headers[SADB_EXT_ADDRESS_DST] = p;
    p += sizeof(struct sadb_address) + PADUP(SA_LEN(&gw->sa));
    sadd = (struct sadb_address *) headers[SADB_EXT_ADDRESS_DST];
    sadd->sadb_address_len = (sizeof(struct sadb_address) +
			      SA_LEN(&gw->sa) +
			      sizeof(uint64_t) - 1) / sizeof(uint64_t);
    bcopy(gw, headers[SADB_EXT_ADDRESS_DST] + sizeof(struct sadb_address),
	  SA_LEN(&gw->sa));

    if (ipo->ipo_srcid)
    {
	headers[SADB_EXT_IDENTITY_SRC] = p;
	p += sizeof(struct sadb_ident) + PADUP(ipo->ipo_srcid_len);
	srcid = (struct sadb_ident *) headers[SADB_EXT_IDENTITY_SRC];
	srcid->sadb_ident_len = (sizeof(struct sadb_ident) +
				 PADUP(ipo->ipo_srcid_len)) /
				sizeof(u_int64_t);
	srcid->sadb_ident_type = ipo->ipo_srcid_type;
	bcopy(ipo->ipo_srcid, headers[SADB_EXT_IDENTITY_SRC] +
	      sizeof(struct sadb_ident), ipo->ipo_srcid_len);
    }

    if (ipo->ipo_dstid)
    {
	headers[SADB_EXT_IDENTITY_DST] = p;
	p += sizeof(struct sadb_ident) + PADUP(ipo->ipo_dstid_len);
	dstid = (struct sadb_ident *) headers[SADB_EXT_IDENTITY_DST];
	dstid->sadb_ident_len = (sizeof(struct sadb_ident) +
				 PADUP(ipo->ipo_dstid_len)) /
				sizeof(u_int64_t);
	dstid->sadb_ident_type = ipo->ipo_dstid_type;
	bcopy(ipo->ipo_dstid, headers[SADB_EXT_IDENTITY_DST] +
	      sizeof(struct sadb_ident), ipo->ipo_dstid_len);
    }

    headers[SADB_EXT_PROPOSAL] = p;
    p += sizeof(struct sadb_prop);
    sa_prop = (struct sadb_prop *) headers[SADB_EXT_PROPOSAL];
    sa_prop->sadb_prop_num = 1; /* XXX One proposal only */
    sa_prop->sadb_prop_len = (sizeof(struct sadb_prop) +
			      (sizeof(struct sadb_comb) *
			       sa_prop->sadb_prop_num)) / sizeof(uint64_t);

    sadb_comb = p;

    /* XXX Should actually ask the crypto layer what's supported */
    for (j = 0; j < sa_prop->sadb_prop_num; j++)
    {
	sadb_comb->sadb_comb_flags = 0;

	if (ipsec_require_pfs)
	  sadb_comb->sadb_comb_flags |= SADB_SAFLAGS_PFS;

	/* Set the encryption algorithm */
	if (ipo->ipo_sproto == IPPROTO_ESP)
	{
	    if (!strncasecmp(ipsec_def_enc, "aes", sizeof("aes")))
	    {
		sadb_comb->sadb_comb_encrypt = SADB_X_EALG_AES;
		sadb_comb->sadb_comb_encrypt_minbits = 64;
		sadb_comb->sadb_comb_encrypt_maxbits = 256;
	    }
	    else
	      if (!strncasecmp(ipsec_def_enc, "3des", sizeof("3des")))
	      {
		  sadb_comb->sadb_comb_encrypt = SADB_EALG_3DESCBC;
		  sadb_comb->sadb_comb_encrypt_minbits = 192;
		  sadb_comb->sadb_comb_encrypt_maxbits = 192;
	      }
	      else
		if (!strncasecmp(ipsec_def_enc, "des", sizeof("des")))
		{
		    sadb_comb->sadb_comb_encrypt = SADB_EALG_DESCBC;
		    sadb_comb->sadb_comb_encrypt_minbits = 64;
		    sadb_comb->sadb_comb_encrypt_maxbits = 64;
		}
		else
		  if (!strncasecmp(ipsec_def_enc, "blowfish",
				   sizeof("blowfish")))
		  {
		      sadb_comb->sadb_comb_encrypt = SADB_X_EALG_BLF;
		      sadb_comb->sadb_comb_encrypt_minbits = 40;
		      sadb_comb->sadb_comb_encrypt_maxbits = BLF_MAXKEYLEN * 8;
		  }
		  else
		    if (!strncasecmp(ipsec_def_enc, "skipjack",
				     sizeof("skipjack")))
		    {
			sadb_comb->sadb_comb_encrypt = SADB_X_EALG_SKIPJACK;
			sadb_comb->sadb_comb_encrypt_minbits = 80;
			sadb_comb->sadb_comb_encrypt_maxbits = 80;
		    }
		    else
		      if (!strncasecmp(ipsec_def_enc, "cast128",
				       sizeof("cast128")))
		      {
			  sadb_comb->sadb_comb_encrypt = SADB_X_EALG_CAST;
			  sadb_comb->sadb_comb_encrypt_minbits = 40;
			  sadb_comb->sadb_comb_encrypt_maxbits = 128;
		      }
	}

	/* Set the authentication algorithm */
	if (!strncasecmp(ipsec_def_auth, "hmac-sha1", sizeof("hmac-sha1")))
	{
	    sadb_comb->sadb_comb_auth = SADB_AALG_SHA1HMAC;
	    sadb_comb->sadb_comb_auth_minbits = 160;
	    sadb_comb->sadb_comb_auth_maxbits = 160;
	}
	else
	  if (!strncasecmp(ipsec_def_auth, "hmac-ripemd160",
			   sizeof("hmac_ripemd160")))
	  {
	      sadb_comb->sadb_comb_auth = SADB_AALG_RIPEMD160HMAC;
	      sadb_comb->sadb_comb_auth_minbits = 160;
	      sadb_comb->sadb_comb_auth_maxbits = 160;
	  }
	  else
	    if (!strncasecmp(ipsec_def_auth, "hmac-md5", sizeof("hmac-md5")))
	    {
		sadb_comb->sadb_comb_auth = SADB_AALG_MD5HMAC;
		sadb_comb->sadb_comb_auth_minbits = 128;
		sadb_comb->sadb_comb_auth_maxbits = 128;
	    }
	
	sadb_comb->sadb_comb_soft_allocations = ipsec_soft_allocations;
	sadb_comb->sadb_comb_hard_allocations = ipsec_exp_allocations;

	sadb_comb->sadb_comb_soft_bytes = ipsec_soft_bytes;
	sadb_comb->sadb_comb_hard_bytes = ipsec_exp_bytes;

	sadb_comb->sadb_comb_soft_addtime = ipsec_soft_timeout;
	sadb_comb->sadb_comb_hard_addtime = ipsec_exp_timeout;

	sadb_comb->sadb_comb_soft_usetime = ipsec_soft_first_use;
	sadb_comb->sadb_comb_hard_usetime = ipsec_exp_first_use;
	sadb_comb++;
    }

    /* Send the ACQUIRE message to all compliant registered listeners. */
    if ((rval = pfkeyv2_sendmessage(headers, PFKEYV2_SENDMESSAGE_REGISTERED,
				    NULL, smsg->sadb_msg_satype, 0)) != 0)
      goto ret;

    rval = 0;
ret:
     if (buffer != NULL)
     {
	 bzero(buffer, i);
	 free(buffer, M_PFKEY);
     }

     return rval;
}

/*
 * Notify key management that an expiration went off. The second argument
 * specifies the type of expiration (soft or hard).
 */
int
pfkeyv2_expire(struct tdb *sa, u_int16_t type)
{
    void *p, *headers[SADB_EXT_MAX+1], *buffer = NULL;
    struct sadb_msg *smsg;
    int rval = 0;
    int i;

    switch (sa->tdb_sproto)
    {
	case IPPROTO_AH:
	case IPPROTO_ESP:
	case IPPROTO_IPIP:
#ifdef TCP_SIGNATURE
	case IPPROTO_TCP:
#endif /* TCP_SIGNATURE */
	    break;

	default:
	    rval = EOPNOTSUPP;
	    goto ret;
    }

    i = sizeof(struct sadb_msg) + sizeof(struct sadb_sa) +
	2 * sizeof(struct sadb_lifetime) +
	sizeof(struct sadb_address) + PADUP(SA_LEN(&sa->tdb_src.sa)) +
	sizeof(struct sadb_address) + PADUP(SA_LEN(&sa->tdb_dst.sa));

    if (!(p = malloc(i, M_PFKEY, M_DONTWAIT)))
    {
	rval = ENOMEM;
	goto ret;
    }

    bzero(headers, sizeof(headers));

    buffer = p;
    bzero(p, i);

    headers[0] = p;
    p += sizeof(struct sadb_msg);

    smsg = (struct sadb_msg *) headers[0];
    smsg->sadb_msg_version = PF_KEY_V2;
    smsg->sadb_msg_type = SADB_EXPIRE;
    smsg->sadb_msg_satype = sa->tdb_satype;
    smsg->sadb_msg_len = i / sizeof(uint64_t);
    smsg->sadb_msg_seq = pfkeyv2_seq++;

    headers[SADB_EXT_SA] = p;
    export_sa(&p, sa);

    headers[SADB_EXT_LIFETIME_CURRENT] = p;
    export_lifetime(&p, sa, 2);

    headers[type] = p;
    type = (SADB_EXT_LIFETIME_SOFT ? PFKEYV2_LIFETIME_SOFT :
	                             PFKEYV2_LIFETIME_HARD);
    export_lifetime(&p, sa, type);

    headers[SADB_EXT_ADDRESS_SRC] = p;
    export_address(&p, (struct sockaddr *) &sa->tdb_src);

    headers[SADB_EXT_ADDRESS_DST] = p;
    export_address(&p, (struct sockaddr *) &sa->tdb_dst);

    if ((rval = pfkeyv2_sendmessage(headers, PFKEYV2_SENDMESSAGE_BROADCAST,
				    NULL, 0, 0)) != 0)
      goto ret;

    rval = 0;

ret:
    if (buffer != NULL)
    {
	bzero(buffer, i);
	free(buffer, M_PFKEY);
    }

    return rval;
}

int
pfkeyv2_init(void)
{
    int rval;

    bzero(&pfkeyv2_version, sizeof(struct pfkey_version));
    pfkeyv2_version.protocol = PFKEYV2_PROTOCOL;
    pfkeyv2_version.create = &pfkeyv2_create;
    pfkeyv2_version.release = &pfkeyv2_release;
    pfkeyv2_version.send = &pfkeyv2_send;

    rval = pfkey_register(&pfkeyv2_version);
    return rval;
}

int
pfkeyv2_cleanup(void)
{
    pfkey_unregister(&pfkeyv2_version);
    return 0;
}
