/*	$OpenBSD: ieee80211_crypto.h,v 1.11 2008/04/18 09:16:14 djm Exp $	*/
/*	$NetBSD: ieee80211_crypto.h,v 1.2 2003/09/14 01:14:55 dyoung Exp $	*/

/*-
 * Copyright (c) 2001 Atsushi Onoe
 * Copyright (c) 2002, 2003 Sam Leffler, Errno Consulting
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/net80211/ieee80211_crypto.h,v 1.2 2003/06/27 05:13:52 sam Exp $
 */
#ifndef _NET80211_IEEE80211_CRYPTO_H_
#define _NET80211_IEEE80211_CRYPTO_H_

/*
 * 802.11 protocol crypto-related definitions.
 */

/*
 * 802.11i ciphers.
 */
enum ieee80211_cipher {
	IEEE80211_CIPHER_NONE     = 0x00000000,
	IEEE80211_CIPHER_USEGROUP = 0x00000001,
	IEEE80211_CIPHER_WEP40    = 0x00000002,
	IEEE80211_CIPHER_TKIP     = 0x00000004,
	IEEE80211_CIPHER_CCMP     = 0x00000008,
	IEEE80211_CIPHER_WEP104   = 0x00000010
};

/*
 * 802.11i Authentication and Key Management Protocols.
 */
enum ieee80211_akm {
	IEEE80211_AKM_NONE	= 0x00000000,
	IEEE80211_AKM_IEEE8021X	= 0x00000001,
	IEEE80211_AKM_PSK	= 0x00000002
};

#define	IEEE80211_KEYBUF_SIZE	16

#define IEEE80211_TKIP_HDRLEN	8
#define IEEE80211_TKIP_MICLEN	8
#define IEEE80211_TKIP_ICVLEN	4
#define IEEE80211_CCMP_HDRLEN	8
#define IEEE80211_CCMP_MICLEN	8

#define IEEE80211_PMK_LEN	32

struct ieee80211_key {
	u_int8_t		k_id;		/* identifier (0-3) */
	enum ieee80211_cipher	k_cipher;
	u_int			k_flags;
#define IEEE80211_KEY_GROUP	0x00000001	/* group key */
#define IEEE80211_KEY_TX	0x00000002	/* Tx+Rx */

	u_int			k_len;
	u_int64_t		k_rsc[IEEE80211_NUM_TID];
	u_int64_t		k_tsc;
	u_int8_t		k_key[32];
	void			*k_priv;
};

/* forward references */
struct ieee80211com;
struct ieee80211_node;

extern	void ieee80211_crypto_attach(struct ifnet *);
extern	void ieee80211_crypto_detach(struct ifnet *);

extern	const u_int8_t *ieee80211_get_pmk(struct ieee80211com *,
	    struct ieee80211_node *, const u_int8_t *);


extern	struct ieee80211_key *ieee80211_get_txkey(struct ieee80211com *,
	    const struct ieee80211_frame *, struct ieee80211_node *);
extern	struct mbuf *ieee80211_encrypt(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_key *);
extern	struct mbuf *ieee80211_decrypt(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_node *);

int ieee80211_set_key(struct ieee80211com *, struct ieee80211_node *,
    struct ieee80211_key *);
void ieee80211_delete_key(struct ieee80211com *, struct ieee80211_node *,
    struct ieee80211_key *);

int ieee80211_wep_set_key(struct ieee80211com *, struct ieee80211_key *);
void ieee80211_wep_delete_key(struct ieee80211com *, struct ieee80211_key *);
struct mbuf *
ieee80211_wep_encrypt(struct ieee80211com *, struct mbuf *,
    struct ieee80211_key *);
struct mbuf *
ieee80211_wep_decrypt(struct ieee80211com *, struct mbuf *,
    struct ieee80211_key *);

int ieee80211_tkip_set_key(struct ieee80211com *, struct ieee80211_key *);
void ieee80211_tkip_delete_key(struct ieee80211com *, struct ieee80211_key *);
struct mbuf *ieee80211_tkip_encrypt(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_key *);
struct mbuf *ieee80211_tkip_decrypt(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_key *);

int ieee80211_ccmp_set_key(struct ieee80211com *, struct ieee80211_key *);
void ieee80211_ccmp_delete_key(struct ieee80211com *, struct ieee80211_key *);
struct mbuf *ieee80211_ccmp_encrypt(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_key *);
struct mbuf *ieee80211_ccmp_decrypt(struct ieee80211com *, struct mbuf *,
	    struct ieee80211_key *);

extern	void ieee80211_tkip_mic(struct mbuf *, int, const u_int8_t *,
	    u_int8_t[IEEE80211_TKIP_MICLEN]);
extern	void ieee80211_michael_mic_failure(struct ieee80211com *, u_int64_t);

extern	void ieee80211_derive_ptk(const u_int8_t *, size_t, const u_int8_t *,
	    const u_int8_t *, const u_int8_t *, const u_int8_t *, u_int8_t *,
	    size_t);
extern	int ieee80211_cipher_keylen(enum ieee80211_cipher);
extern	void ieee80211_map_ptk(const struct ieee80211_ptk *,
	    enum ieee80211_cipher, u_int64_t, struct ieee80211_key *);
extern	void ieee80211_map_gtk(const u_int8_t *, enum ieee80211_cipher, int,
	    int, u_int64_t, struct ieee80211_key *);


#endif /* _NET80211_IEEE80211_CRYPTO_H_ */
