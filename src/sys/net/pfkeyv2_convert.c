/*	$OpenBSD: pfkeyv2_convert.c,v 1.7 2001/12/12 04:46:42 angelos Exp $	*/
/*
 * The author of this code is Angelos D. Keromytis (angelos@keromytis.org)
 *
 * Part of this code is based on code written by Craig Metz (cmetz@inner.net)
 * for NRL. Those licenses follow this one.
 *
 * Copyright (c) 2001 Angelos D. Keromytis.
 *
 * Permission to use, copy, and modify this software with or without fee
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
 *	@(#)COPYRIGHT	1.1 (NRL) 17 January 1995
 * 
 * NRL grants permission for redistribution and use in source and binary
 * forms, with or without modification, of the software and documentation
 * created at NRL provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgements:
 * 	This product includes software developed by the University of
 * 	California, Berkeley and its contributors.
 * 	This product includes software developed at the Information
 * 	Technology Division, US Naval Research Laboratory.
 * 4. Neither the name of the NRL nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * 
 * THE SOFTWARE PROVIDED BY NRL IS PROVIDED BY NRL AND CONTRIBUTORS ``AS
 * IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL NRL OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * The views and conclusions contained in the software and documentation
 * are those of the authors and should not be interpreted as representing
 * official policies, either expressed or implied, of the US Naval
 * Research Laboratory (NRL).
 */

/*
 * Copyright (c) 1995, 1996, 1997, 1998, 1999 Craig Metz. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any contributors
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
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <netinet/ip_ipsp.h>
#include <net/pfkeyv2.h>
#include <crypto/cryptodev.h>
#include <crypto/xform.h>

/*
 * (Partly) Initialize a TDB based on an SADB_SA payload. Other parts
 * of the TDB will be initialized by other import routines, and tdb_init().
 */
void
import_sa(struct tdb *tdb, struct sadb_sa *sadb_sa, struct ipsecinit *ii)
{
	if (!sadb_sa)
		return;

	if (ii) {
		ii->ii_encalg = sadb_sa->sadb_sa_encrypt;
		ii->ii_authalg = sadb_sa->sadb_sa_auth;
		ii->ii_compalg = sadb_sa->sadb_sa_encrypt; /* Yeurk! */

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

	if (tdb->tdb_sproto == IPPROTO_IPCOMP) {
		switch (tdb->tdb_compalgxform->type)
		{
		case CRYPTO_DEFLATE_COMP:
			sadb_sa->sadb_sa_encrypt = SADB_X_CALG_DEFLATE;
			break;
		}
	}

	if (tdb->tdb_authalgxform) {
		switch (tdb->tdb_authalgxform->type) {
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

	if (tdb->tdb_encalgxform) {
		switch (tdb->tdb_encalgxform->type) {
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

	/* Only relevant for the "old" IPsec transforms. */
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
	struct timeval tv;
	int s;

	if (!sadb_lifetime)
		return;

	s = splhigh();
	tv = time;
	splx(s);

	switch (type) {
	case PFKEYV2_LIFETIME_HARD:
		if ((tdb->tdb_exp_allocations =
		    sadb_lifetime->sadb_lifetime_allocations) != 0)
			tdb->tdb_flags |= TDBF_ALLOCATIONS;
		else
			tdb->tdb_flags &= ~TDBF_ALLOCATIONS;

		if ((tdb->tdb_exp_bytes =
		    sadb_lifetime->sadb_lifetime_bytes) != 0)
			tdb->tdb_flags |= TDBF_BYTES;
		else
			tdb->tdb_flags &= ~TDBF_BYTES;

		if ((tdb->tdb_exp_timeout =
		    sadb_lifetime->sadb_lifetime_addtime) != 0) {
			tdb->tdb_flags |= TDBF_TIMER;
			tv.tv_sec += tdb->tdb_exp_timeout;
			timeout_add(&tdb->tdb_timer_tmo,
			    hzto(&tv));
		} else
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
		    sadb_lifetime->sadb_lifetime_addtime) != 0) {
			tdb->tdb_flags |= TDBF_SOFT_TIMER;
			tv.tv_sec += tdb->tdb_soft_timeout;
			timeout_add(&tdb->tdb_stimer_tmo,
			    hzto(&tv));
		} else
			tdb->tdb_flags &= ~TDBF_SOFT_TIMER;

		if ((tdb->tdb_soft_first_use =
		    sadb_lifetime->sadb_lifetime_usetime) != 0)
			tdb->tdb_flags |= TDBF_SOFT_FIRSTUSE;
		else
			tdb->tdb_flags &= ~TDBF_SOFT_FIRSTUSE;
		break;

	case PFKEYV2_LIFETIME_CURRENT:  /* Nothing fancy here. */
		tdb->tdb_cur_allocations =
		    sadb_lifetime->sadb_lifetime_allocations;
		tdb->tdb_cur_bytes = sadb_lifetime->sadb_lifetime_bytes;
		tdb->tdb_established = sadb_lifetime->sadb_lifetime_addtime;
		tdb->tdb_first_use = sadb_lifetime->sadb_lifetime_usetime;
	}
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

	switch (type) {
	case PFKEYV2_LIFETIME_HARD:
		if (tdb->tdb_flags & TDBF_ALLOCATIONS)
			sadb_lifetime->sadb_lifetime_allocations =
			    tdb->tdb_exp_allocations;

		if (tdb->tdb_flags & TDBF_BYTES)
			sadb_lifetime->sadb_lifetime_bytes =
			    tdb->tdb_exp_bytes;

		if (tdb->tdb_flags & TDBF_TIMER)
			sadb_lifetime->sadb_lifetime_addtime =
			    tdb->tdb_exp_timeout;

		if (tdb->tdb_flags & TDBF_FIRSTUSE)
			sadb_lifetime->sadb_lifetime_usetime =
			    tdb->tdb_exp_first_use;
		break;

	case PFKEYV2_LIFETIME_SOFT:
		if (tdb->tdb_flags & TDBF_SOFT_ALLOCATIONS)
			sadb_lifetime->sadb_lifetime_allocations =
			    tdb->tdb_soft_allocations;

		if (tdb->tdb_flags & TDBF_SOFT_BYTES)
			sadb_lifetime->sadb_lifetime_bytes =
			    tdb->tdb_soft_bytes;

		if (tdb->tdb_flags & TDBF_SOFT_TIMER)
			sadb_lifetime->sadb_lifetime_addtime =
			    tdb->tdb_soft_timeout;

		if (tdb->tdb_flags & TDBF_SOFT_FIRSTUSE)
			sadb_lifetime->sadb_lifetime_usetime =
			    tdb->tdb_soft_first_use;
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
		switch(ssa->sa_family) {
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
 * Import authentication information into the TDB.
 */
void
import_auth(struct tdb *tdb, struct sadb_x_cred *sadb_auth, int dstauth)
{
	struct ipsec_ref **ipr;

	if (!sadb_auth)
		return;

	if (dstauth == PFKEYV2_AUTH_REMOTE)
		ipr = &tdb->tdb_remote_auth;
	else
		ipr = &tdb->tdb_local_auth;

	MALLOC(*ipr, struct ipsec_ref *, EXTLEN(sadb_auth) -
	    sizeof(struct sadb_x_cred) + sizeof(struct ipsec_ref),
	    M_CREDENTIALS, M_WAITOK);
	(*ipr)->ref_len = EXTLEN(sadb_auth) - sizeof(struct sadb_x_cred);

	switch (sadb_auth->sadb_x_cred_type) {
	case SADB_X_AUTHTYPE_PASSPHRASE:
		(*ipr)->ref_type = IPSP_AUTH_PASSPHRASE;
		break;
	case SADB_X_AUTHTYPE_RSA:
		(*ipr)->ref_type = IPSP_AUTH_RSA;
		break;
	default:
		FREE(*ipr, M_CREDENTIALS);
		*ipr = NULL;
		return;
	}
	(*ipr)->ref_count = 1;
	(*ipr)->ref_malloctype = M_CREDENTIALS;
	bcopy((void *) sadb_auth + sizeof(struct sadb_x_cred),
	    (*ipr) + 1, (*ipr)->ref_len);
}

/*
 * Import a set of credentials into the TDB.
 */
void
import_credentials(struct tdb *tdb, struct sadb_x_cred *sadb_cred, int dstcred)
{
	struct ipsec_ref **ipr;

	if (!sadb_cred)
		return;

	if (dstcred == PFKEYV2_CRED_REMOTE)
		ipr = &tdb->tdb_remote_cred;
	else
		ipr = &tdb->tdb_local_cred;

	MALLOC(*ipr, struct ipsec_ref *, EXTLEN(sadb_cred) -
	    sizeof(struct sadb_x_cred) + sizeof(struct ipsec_ref),
	    M_CREDENTIALS, M_WAITOK);
	(*ipr)->ref_len = EXTLEN(sadb_cred) - sizeof(struct sadb_x_cred);

	switch (sadb_cred->sadb_x_cred_type) {
	case SADB_X_CREDTYPE_X509:
		(*ipr)->ref_type = IPSP_CRED_X509;
		break;
	case SADB_X_CREDTYPE_KEYNOTE:
		(*ipr)->ref_type = IPSP_CRED_KEYNOTE;
		break;
	default:
		FREE(*ipr, M_CREDENTIALS);
		*ipr = NULL;
		return;
	}
	(*ipr)->ref_count = 1;
	(*ipr)->ref_malloctype = M_CREDENTIALS;
	bcopy((void *) sadb_cred + sizeof(struct sadb_x_cred),
	    (*ipr) + 1, (*ipr)->ref_len);
}

/*
 * Import an identity payload into the TDB.
 */
void
import_identity(struct tdb *tdb, struct sadb_ident *sadb_ident, int type)
{
	struct ipsec_ref **ipr;

	if (!sadb_ident)
		return;

	if (type == PFKEYV2_IDENTITY_SRC)
		ipr = &tdb->tdb_srcid;
	else
		ipr = &tdb->tdb_dstid;

	MALLOC(*ipr, struct ipsec_ref *, EXTLEN(sadb_ident) -
	    sizeof(struct sadb_ident) + sizeof(struct ipsec_ref),
	    M_CREDENTIALS, M_WAITOK);
	(*ipr)->ref_len = EXTLEN(sadb_ident) - sizeof(struct sadb_ident);

	switch (sadb_ident->sadb_ident_type) {
	case SADB_IDENTTYPE_PREFIX:
		(*ipr)->ref_type = IPSP_IDENTITY_PREFIX;
		break;
	case SADB_IDENTTYPE_FQDN:
		(*ipr)->ref_type = IPSP_IDENTITY_FQDN;
		break;
	case SADB_IDENTTYPE_USERFQDN:
		(*ipr)->ref_type = IPSP_IDENTITY_USERFQDN;
		break;
	case SADB_X_IDENTTYPE_CONNECTION:
		(*ipr)->ref_type = IPSP_IDENTITY_CONNECTION;
		break;
	default:
		FREE(*ipr, M_CREDENTIALS);
		*ipr = NULL;
		return;
	}
	(*ipr)->ref_count = 1;
	(*ipr)->ref_malloctype = M_CREDENTIALS;
	bcopy((void *) sadb_ident + sizeof(struct sadb_ident), (*ipr) + 1,
	    (*ipr)->ref_len);
}

void
export_credentials(void **p, struct tdb *tdb, int dstcred)
{
	struct ipsec_ref **ipr;
	struct sadb_x_cred *sadb_cred = (struct sadb_x_cred *) *p;

	if (dstcred == PFKEYV2_CRED_REMOTE)
		ipr = &tdb->tdb_remote_cred;
	else
		ipr = &tdb->tdb_local_cred;

	sadb_cred->sadb_x_cred_len = (sizeof(struct sadb_x_cred) +
	    PADUP((*ipr)->ref_len)) / sizeof(uint64_t);

	switch ((*ipr)->ref_type) {
	case IPSP_CRED_KEYNOTE:
		sadb_cred->sadb_x_cred_type = SADB_X_CREDTYPE_KEYNOTE;
		break;
	case IPSP_CRED_X509:
		sadb_cred->sadb_x_cred_type = SADB_X_CREDTYPE_X509;
		break;
	}
	*p += sizeof(struct sadb_x_cred);
	bcopy((*ipr) + 1, *p, (*ipr)->ref_len);
	*p += PADUP((*ipr)->ref_len);
}

void
export_auth(void **p, struct tdb *tdb, int dstauth)
{
	struct ipsec_ref **ipr;
	struct sadb_x_cred *sadb_auth = (struct sadb_x_cred *) *p;

	if (dstauth == PFKEYV2_AUTH_REMOTE)
		ipr = &tdb->tdb_remote_auth;
	else
		ipr = &tdb->tdb_local_auth;

	sadb_auth->sadb_x_cred_len = (sizeof(struct sadb_x_cred) +
	    PADUP((*ipr)->ref_len)) / sizeof(uint64_t);

	switch ((*ipr)->ref_type) {
	case IPSP_CRED_KEYNOTE:
		sadb_auth->sadb_x_cred_type = SADB_X_CREDTYPE_KEYNOTE;
		break;
	case IPSP_CRED_X509:
		sadb_auth->sadb_x_cred_type = SADB_X_CREDTYPE_X509;
		break;
	}
	*p += sizeof(struct sadb_x_cred);
	bcopy((*ipr) + 1, *p, (*ipr)->ref_len);
	*p += PADUP((*ipr)->ref_len);
}

void
export_identity(void **p, struct tdb *tdb, int type)
{
	struct ipsec_ref **ipr;
	struct sadb_ident *sadb_ident = (struct sadb_ident *) *p;

	if (type == PFKEYV2_IDENTITY_SRC)
		ipr = &tdb->tdb_srcid;
	else
		ipr = &tdb->tdb_dstid;

	sadb_ident->sadb_ident_len = (sizeof(struct sadb_ident) +
	    PADUP((*ipr)->ref_len)) / sizeof(uint64_t);

	switch ((*ipr)->ref_type) {
	case IPSP_IDENTITY_PREFIX:
		sadb_ident->sadb_ident_type = SADB_IDENTTYPE_PREFIX;
		break;
	case IPSP_IDENTITY_FQDN:
		sadb_ident->sadb_ident_type = SADB_IDENTTYPE_FQDN;
		break;
	case IPSP_IDENTITY_USERFQDN:
		sadb_ident->sadb_ident_type = SADB_IDENTTYPE_USERFQDN;
		break;
	case IPSP_IDENTITY_CONNECTION:
		sadb_ident->sadb_ident_type = SADB_X_IDENTTYPE_CONNECTION;
		break;
	}
	*p += sizeof(struct sadb_ident);
	bcopy((*ipr) + 1, *p, (*ipr)->ref_len);
	*p += PADUP((*ipr)->ref_len);
}

/* ... */
void
import_key(struct ipsecinit *ii, struct sadb_key *sadb_key, int type)
{
	if (!sadb_key)
		return;

	if (type == PFKEYV2_ENCRYPTION_KEY) { /* Encryption key */
		ii->ii_enckeylen = sadb_key->sadb_key_bits / 8;
		ii->ii_enckey = (void *)sadb_key + sizeof(struct sadb_key);
	} else {
		ii->ii_authkeylen = sadb_key->sadb_key_bits / 8;
		ii->ii_authkey = (void *)sadb_key + sizeof(struct sadb_key);
	}
}

void
export_key(void **p, struct tdb *tdb, int type)
{
	struct sadb_key *sadb_key = (struct sadb_key *) *p;

	if (type == PFKEYV2_ENCRYPTION_KEY) {
		sadb_key->sadb_key_len = (sizeof(struct sadb_key) +
		    PADUP(tdb->tdb_emxkeylen)) /
		    sizeof(uint64_t);
		sadb_key->sadb_key_bits = tdb->tdb_emxkeylen * 8;
		*p += sizeof(struct sadb_key);
		bcopy(tdb->tdb_emxkey, *p, tdb->tdb_emxkeylen);
		*p += PADUP(tdb->tdb_emxkeylen);
	} else {
		sadb_key->sadb_key_len = (sizeof(struct sadb_key) +
		    PADUP(tdb->tdb_amxkeylen)) /
		    sizeof(uint64_t);
		sadb_key->sadb_key_bits = tdb->tdb_amxkeylen * 8;
		*p += sizeof(struct sadb_key);
		bcopy(tdb->tdb_amxkey, *p, tdb->tdb_amxkeylen);
		*p += PADUP(tdb->tdb_amxkeylen);
	}
}
