/*	$OpenBSD: ip_espdesmd5.c,v 1.11 1997/07/01 22:12:49 provos Exp $	*/

/*
 * The author of this code is John Ioannidis, ji@tla.org,
 * 	(except when noted otherwise).
 *
 * This code was written for BSD/OS in Athens, Greece, in November 1995.
 *
 * Ported to OpenBSD and NetBSD, with additional transforms, in December 1996,
 * by Angelos D. Keromytis, kermit@forthnet.gr.
 *
 * Copyright (C) 1995, 1996, 1997 by John Ioannidis and Angelos D. Keromytis.
 *	
 * Permission to use, copy, and modify this software without fee
 * is hereby granted, provided that this entire notice is included in
 * all copies of any software which is or includes a copy or
 * modification of this software.
 *
 * THIS SOFTWARE IS BEING PROVIDED "AS IS", WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTY. IN PARTICULAR, NEITHER AUTHOR MAKES ANY
 * REPRESENTATION OR WARRANTY OF ANY KIND CONCERNING THE
 * MERCHANTABILITY OF THIS SOFTWARE OR ITS FITNESS FOR ANY PARTICULAR
 * PURPOSE.
 */

/*
 * Based on draft-ietf-ipsec-esp-des-md5-03.txt.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/domain.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/time.h>
#include <sys/kernel.h>
#include <machine/cpu.h>

#include <net/if.h>
#include <net/route.h>
#include <net/netisr.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/in_pcb.h>
#include <netinet/in_var.h>
#include <netinet/ip_var.h>

#include <sys/socketvar.h>
#include <net/raw_cb.h>
#include <net/encap.h>

#include <netinet/ip_icmp.h>
#include <netinet/ip_ipsp.h>
#include <netinet/ip_esp.h>

extern struct ifnet loif;

extern void des_ecb_encrypt(caddr_t, caddr_t, caddr_t, int);
extern void des_set_key(caddr_t, caddr_t);

int
espdesmd5_attach()
{
    return 0;
}

/*
 * espdesmd5_init() is called when an SPI is being set up. It interprets the
 * encap_msghdr present in m, and sets up the transformation data, in
 * this case, the encryption and decryption key schedules
 */

int
espdesmd5_init(struct tdb *tdbp, struct xformsw *xsp, struct mbuf *m)
{
    struct espdesmd5_xdata *xd;
    struct encap_msghdr *em;
    struct espdesmd5_xencap txd;
    u_char buf[ESPDESMD5_KEYSZ];
    int len;
    MD5_CTX ctx;

    tdbp->tdb_xform = xsp;

    m = m_pullup(m, ESPDESMD5_ULENGTH);
    if (m == NULL)
    {
#ifdef ENCDEBUG
	if (encdebug)
	  printf("espdesmd5_init: can't pull up %d bytes\n", ESP_ULENGTH);
#endif ENCDEBUG
	return ENOBUFS;
    }

    MALLOC(tdbp->tdb_xdata, caddr_t, sizeof (struct espdesmd5_xdata),
	   M_XDATA, M_WAITOK);
    if (tdbp->tdb_xdata == NULL)
      return ENOBUFS;
    bzero(tdbp->tdb_xdata, sizeof (struct espdesmd5_xdata));
    xd = (struct espdesmd5_xdata *)tdbp->tdb_xdata;

    em = mtod(m, struct encap_msghdr *);
    if (em->em_msglen - EMT_SETSPI_FLEN != ESPDESMD5_ULENGTH)
    {
	free((caddr_t)tdbp->tdb_xdata, M_XDATA);
	tdbp->tdb_xdata = NULL;
	return EINVAL;
    }

    m_copydata(m, EMT_SETSPI_FLEN, em->em_msglen - EMT_SETSPI_FLEN, 
	       (caddr_t)&txd);

    if ((txd.edx_ivlen != 0) && (txd.edx_ivlen != 8))
    {
	free((caddr_t)tdbp->tdb_xdata, M_XDATA);
	tdbp->tdb_xdata = NULL;
	return EINVAL;
    }

    bzero(ipseczeroes, IPSEC_ZEROES_SIZE);	/* paranoid */

    xd->edx_ivlen = txd.edx_ivlen;
    xd->edx_bitmap = 0;
    xd->edx_wnd = txd.edx_wnd;

    /* Fix the IV */

#ifdef ENCDEBUG
    if (encdebug)
    {
	if (txd.edx_initiator)
	  printf("INITIATOR\n");
	printf("IV length: %d\n", txd.edx_ivlen);
    }
#endif
    if (txd.edx_ivlen)
      bcopy(txd.edx_ivv, xd->edx_iv, ESPDESMD5_IVS);
    else
    {
	for (len = 0; len < ESPDESMD5_KEYSZ; len++)
	  buf[len] = txd.edx_initiator ? ESPDESMD5_IPADI :
		   ESPDESMD5_IPADR;

	MD5Init(&ctx);
	MD5Update(&ctx, buf, ESPDESMD5_KEYSZ);
	MD5Update(&ctx, txd.edx_key, txd.edx_keylen);
	MD5Final(buf, &ctx);
	bcopy(buf, xd->edx_iv, ESPDESMD5_IVS);
#ifdef ENCDEBUG
	printf("IV ");
	if (encdebug)
	  for (len = 0; len < ESPDESMD5_IVS; len++)
	    printf(" %02x", xd->edx_iv[len]);
	printf("\n");
#endif
    }

    /* DES key */

    MD5Init(&ctx);
    for (len = 0; len < ESPDESMD5_KEYSZ; len++)
      buf[len] = txd.edx_initiator ? ESPDESMD5_DPADI : ESPDESMD5_DPADR;
	 
    MD5Update(&ctx, buf, ESPDESMD5_KEYSZ);
    MD5Update(&ctx, txd.edx_key, txd.edx_keylen);
    MD5Final(buf, &ctx);
    des_set_key((caddr_t)buf, (caddr_t)(xd->edx_eks));

    /* HMAC contexts */

    MD5Init(&ctx);
    for (len = 0; len < ESPDESMD5_KEYSZ; len++)
      buf[len] = txd.edx_initiator ? ESPDESMD5_HPADI : ESPDESMD5_HPADR;

    MD5Update(&ctx, buf, ESPDESMD5_KEYSZ);
    MD5Update(&ctx, txd.edx_key, txd.edx_keylen);
    MD5Final(buf, &ctx);

    bzero(buf + ESPDESMD5_ALEN, ESPDESMD5_KEYSZ - ESPDESMD5_ALEN);

    for (len = 0; len < ESPDESMD5_KEYSZ; len++)
      buf[len] ^= ESPDESMD5_IPAD_VAL;

    MD5Init(&ctx);
    MD5Update(&ctx, buf, ESPDESMD5_KEYSZ);
    xd->edx_ictx = ctx;

    for (len = 0; len < ESPDESMD5_KEYSZ; len++)
      buf[len] ^= (ESPDESMD5_IPAD_VAL ^ ESPDESMD5_OPAD_VAL);

    MD5Init(&ctx);
    MD5Update(&ctx, buf, ESPDESMD5_KEYSZ);
    xd->edx_octx = ctx;
	
    /* Replay counter */

    for (len = 0; len < ESPDESMD5_KEYSZ; len++)
      buf[len] = txd.edx_initiator ? ESPDESMD5_RPADI : 
	ESPDESMD5_RPADR;

    MD5Init(&ctx);
    MD5Update(&ctx, buf, ESPDESMD5_KEYSZ);
    MD5Update(&ctx, txd.edx_key, txd.edx_keylen);
    MD5Final(buf, &ctx);
    bcopy(buf, (unsigned char *)&(xd->edx_rpl), ESPDESMD5_RPLENGTH);
    xd->edx_initial = xd->edx_rpl - 1;

#ifdef ENCDEBUG
    if (encdebug)
      printf("Initial replay counter: %x (%x)\n", xd->edx_rpl,
	     xd->edx_initial);
#endif
	
    bzero(&txd, sizeof(struct espdesmd5_xencap));
    bzero(buf, ESPDESMD5_KEYSZ);
    bzero(&ctx, sizeof(MD5_CTX));

    return 0;
}

int
espdesmd5_zeroize(struct tdb *tdbp)
{
    FREE(tdbp->tdb_xdata, M_XDATA);
    return 0;
}


struct mbuf *
espdesmd5_input(struct mbuf *m, struct tdb *tdb)
{
    struct espdesmd5_xdata *xd;
    struct ip *ip, ipo;
    u_char iv[8], niv[8], blk[8], auth[ESPDESMD5_ALEN];
    u_char iauth[ESPDESMD5_ALEN];
    u_char *idat, *odat;
    struct esp *esp;
    struct ifnet *rcvif;
    int plen, ilen, olen, i, authp, oplen, errc;
    u_int32_t rplc, tbitmap, trpl;
    u_char padsize, nextproto;
    struct mbuf *mi, *mo;
    MD5_CTX ctx;

    xd = (struct espdesmd5_xdata *)tdb->tdb_xdata;

    rcvif = m->m_pkthdr.rcvif;
    if (rcvif == NULL)
    {
#ifdef ENCDEBUG
	if (encdebug)
	  printf("espdesmd5_input: receive interface is NULL!!!\n");
#endif ENCDEBUG
	rcvif = &loif;
    }

    ip = mtod(m, struct ip *);
    ipo = *ip;
    esp = (struct esp *)(ip + 1);

    plen = m->m_pkthdr.len - sizeof (struct ip) - sizeof (u_int32_t) - 
	   xd->edx_ivlen;
    if (plen & 07)
    {
#ifdef ENCDEBUG
	if (encdebug)
	  printf("espdesmd5_input: payload not a multiple of 8 octets\n");
#endif ENCDEBUG
	espstat.esps_badilen++;
	m_freem(m);
	return NULL;
    }

    oplen = plen;
    ilen = m->m_len - sizeof (struct ip) - ESPDESMD5_IVS - sizeof(u_int32_t);
    idat = mtod(m, unsigned char *) + sizeof (struct ip) + sizeof(u_int32_t) +
	   ESPDESMD5_IVS;

    if (xd->edx_ivlen == 0)		/* KeyIV in use */
    {
	bcopy(xd->edx_iv, iv, ESPDESMD5_IVS);
	ilen += ESPDESMD5_IVS;
	idat -= ESPDESMD5_IVS;
    }
    else
      bcopy(idat - ESPDESMD5_IVS, iv, ESPDESMD5_IVS);

    olen = ilen;
    odat = idat;
    mi = mo = m;
    i = 0;
    authp = 0;

    ctx = xd->edx_ictx;

    MD5Update(&ctx, (unsigned char *)&(tdb->tdb_spi), sizeof(u_int32_t));
    MD5Update(&ctx, iv, ESPDESMD5_IVS);

#ifdef ENCDEBUG
    printf("IV ");
    for (i = 0; i < ESPDESMD5_IVS; i++)
      printf(" %02x", iv[i]);
    printf("\n");
    i = 0;
#endif

    /*
     * At this point:
     *   plen is # of encapsulated payload octets
     *   ilen is # of octets left in this mbuf
     *   idat is first encapsulated payload octed in this mbuf
     *   same for olen and odat
     *   iv contains the IV.
     *   mi and mo point to the first mbuf
     *
     * From now on until the end of the mbuf chain:
     *   . move the next eight octets of the chain into blk[]
     *     (ilen, idat, and mi are adjusted accordingly)
     *     and save it back into iv[]
     *   . decrypt blk[], xor with iv[], put back into chain
     *     (olen, odat, amd mo are adjusted accordingly)
     *   . repeat
     */

    while (plen > 0)		/* while not done */
    {
	while (ilen == 0)	/* we exhausted previous mbuf */
	{
	    mi = mi->m_next;
	    if (mi == NULL)
	      panic("espdesmd5_input: bad chain (i)\n");
	    ilen = mi->m_len;
	    idat = (u_char *)mi->m_data;
	}

	blk[i] = niv[i] = *idat++;
	i++;
	ilen--;

	if (i == 8)
	{
	    des_ecb_encrypt(blk, blk, (caddr_t)(xd->edx_eks), 0);
	    for (i=0; i<8; i++)
	    {
		while (olen == 0)
		{
		    mo = mo->m_next;
		    if (mo == NULL)
		      panic("espdesmd5_input: bad chain (o)\n");
		    olen = mo->m_len;
		    odat = (u_char *)mo->m_data;
		}
		*odat = blk[i] ^ iv[i];
		iv[i] = niv[i];
		blk[i] = *odat++; /* needed elsewhere */
		olen--;
	    }
	    i = 0;

	    if (plen < ESPDESMD5_ALEN)
	    {
		bcopy(blk, auth + authp, ESPDESMD5_DESBLK);
		authp += ESPDESMD5_DESBLK;
#ifdef ENCDEBUG
		if (encdebug)
		  printf("Copying authenticator from %d\n",
			 plen);
#endif
	    }
	    else
	    {
		if (plen == ESPDESMD5_ALEN + 1)
		{
		    nextproto = blk[7];
		    padsize = blk[6];
#ifdef ENCDEBUG
		    if (encdebug)
		      printf("Next protocol: %d\nPadsize: %d\n", nextproto,
			     padsize);
#endif
		}
		else
		  if (plen + 7 == oplen)
		  {
#ifdef ENCDEBUG
		      if (encdebug)
			printf("SEQ %02x %02x %02x %02x\n",
			       blk[0], blk[1], blk[2], 
			       blk[3]);
#endif
		      tbitmap = xd->edx_bitmap; /* Save it */
		      trpl = xd->edx_rpl;
		      rplc = ntohl(*((u_int32_t *)blk));
		      if ((errc = checkreplaywindow32(rplc, xd->edx_initial, &(xd->edx_rpl), xd->edx_wnd, &(xd->edx_bitmap))) != 0)
		      {
			  switch (errc)
			  {
			      case 1:
#ifdef ENCDEBUG
				  printf("espdesmd5_input: replay counter wrapped\n");
#endif
				  espstat.esps_wrap++;
				  break;
			      case 2:
#ifdef ENCDEBUG
				  printf("espdesmd5_input: received old packet, seq = %08x\n", rplc);
#endif
				  espstat.esps_replay++;
				  break;
			      case 3:
#ifdef ENCDEBUG
				  printf("espdesmd5_input: packet already received\n");
#endif
				  espstat.esps_replay++;
				  break;
			  }
			  m_freem(m);
			  return NULL;
		      }
		  }

		MD5Update(&ctx, blk, ESPDESMD5_DESBLK);
	    }
	}

	plen--;
    }

    /*
     * Now, the entire chain has been decrypted.
     */

    MD5Final(iauth, &ctx);
    ctx = xd->edx_octx;
    MD5Update(&ctx, iauth, ESPDESMD5_ALEN);
    MD5Final(iauth, &ctx);

#ifdef ENCDEBUG
    printf("RECEIVED ");
    for (rplc = 0; rplc < ESPDESMD5_ALEN; rplc++)
      printf(" %02x", auth[rplc]);
    printf("\nSHOULD HAVE ");
    for (rplc = 0; rplc < ESPDESMD5_ALEN; rplc++)
      printf(" %02x", iauth[rplc]);
    printf("\n");
#endif

    if (bcmp(auth, iauth, ESPDESMD5_ALEN))
    {
#ifdef ENCDEBUG
	if (encdebug)
	  printf("espdesmd5_input: bad auth\n");
#endif
	xd->edx_rpl = trpl;
	xd->edx_bitmap = tbitmap;  /* Restore */
	espstat.esps_badauth++;
	m_freem(m);
	return NULL;
    }

    m_adj(m, - padsize - 2 - ESPDESMD5_ALEN);
    m_adj(m, 4 + xd->edx_ivlen + ESPDESMD5_RPLENGTH);

    if (m->m_len < sizeof (struct ip))
    {
	m = m_pullup(m, sizeof (struct ip));
	if (m == NULL)
	{
	    xd->edx_rpl = trpl;
	    xd->edx_bitmap = tbitmap;
	    return NULL;
	}
    }

    ip = mtod(m, struct ip *);
    ipo.ip_p = nextproto;
    ipo.ip_id = htons(ipo.ip_id);
    ipo.ip_off = 0;
    ipo.ip_len += sizeof (struct ip) - ESPDESMD5_RPLENGTH - 4 - xd->edx_ivlen -
		  padsize - 2 - ESPDESMD5_ALEN;
#ifdef ENCDEBUG
    if (encdebug)
      printf("IP packet size %d\n", ipo.ip_len);
#endif
    ipo.ip_len = htons(ipo.ip_len);
    ipo.ip_sum = 0;
    *ip = ipo;
    ip->ip_sum = in_cksum(m, sizeof (struct ip));

    /* Update the counters */
    tdb->tdb_cur_packets++;
    tdb->tdb_cur_bytes += ntohs(ip->ip_len) - (ip->ip_hl << 2) + padsize +
		          2 + ESPDESMD5_ALEN;

    return m;
}

int
espdesmd5_output(struct mbuf *m, struct sockaddr_encap *gw, struct tdb *tdb,
		 struct mbuf **mp)
{
    struct espdesmd5_xdata *xd;
    struct ip *ip, ipo;
    int i, ilen, olen, ohlen, nh, rlen, plen, padding;
    u_int32_t rplc;
    u_int32_t spi;
    struct mbuf *mi, *mo, *ms;
    u_char *pad, *idat, *odat;
    u_char iv[ESPDESMD5_IVS], blk[8], auth[ESPDESMD5_ALEN], opts[40];
    MD5_CTX ctx;
    int iphlen;
    
    espstat.esps_output++;
    m = m_pullup(m, sizeof (struct ip));   /* Get IP header in one mbuf */
    if (m == NULL)
      return ENOBUFS;

    ip = mtod(m, struct ip *);
    spi = tdb->tdb_spi;
    iphlen = ip->ip_hl << 2;
    
    /*
     * If options are present, pullup the IP header, the options
     * and one DES block (8 bytes) of data.
     */
    if (iphlen != sizeof(struct ip))
    {
	m = m_pullup(m, iphlen + 8);
	if (m == NULL)
	  return ENOBUFS;

	ip = mtod(m, struct ip *);

	/* Keep the options */
	bcopy(mtod(m, u_char *) + sizeof(struct ip), opts,
	      iphlen - sizeof(struct ip));
    }

    xd = (struct espdesmd5_xdata *)tdb->tdb_xdata;
    ilen = ntohs(ip->ip_len);    /* Size of the packet */
    ohlen = sizeof (u_int32_t) + xd->edx_ivlen; /* size of plaintext ESP */

    if (xd->edx_rpl == xd->edx_initial)
    {
#ifdef ENCDEBUG
	if (encdebug)
	  printf("espdesmd5_output: replay counter wrapped\n");
#endif
	espstat.esps_wrap++;
	return EHOSTDOWN;   /* XXX */
    }
	
    ipo = *ip;
    nh = ipo.ip_p;

#ifdef ENCDEBUG
    if (encdebug)
      printf("espdesmd5_output: next protocol is %d\n", nh);
#endif

    /* Raw payload length */
    rlen = ESPDESMD5_RPLENGTH + ilen - iphlen; 

    padding = ((8 - ((rlen + 2) % 8)) % 8) + 2;

    pad = (u_char *)m_pad(m, padding);
    if (pad == NULL)
      return ENOBUFS;

    pad[padding-2] = padding - 2;
    pad[padding-1] = nh;

#ifdef ENCDEBUG
    if (encdebug)
      printf("espdesmd5_output: padding %d bytes\n", padding);
#endif

    plen = rlen + padding + ESPDESMD5_ALEN;

    ctx = xd->edx_ictx;  /* Get inner padding cached */

    bcopy(xd->edx_iv, iv, ESPDESMD5_IVS);

    MD5Update(&ctx, (u_char *)&spi, sizeof(u_int32_t));
    MD5Update(&ctx, iv, ESPDESMD5_IVS);
    rplc = htonl(xd->edx_rpl);
    MD5Update(&ctx, (unsigned char *)&rplc, ESPDESMD5_RPLENGTH);
    xd->edx_rpl++;

#ifdef ENCDEBUG
    if (encdebug)
      printf("espdesmd5_output: using replay counter %x\n",
	     xd->edx_rpl - 1);
#endif
    mi = m;

    /* MD5 the data */
    while (mi != NULL)
    {
#ifdef ENCDEBUG
	if (encdebug)
	  printf("espdesmd5_output: MD5'ing %d bytes\n", mi->m_len);
#endif
	if (mi == m)
	  MD5Update(&ctx, (u_char *)mi->m_data + iphlen,
		    mi->m_len - iphlen);
	else
	  MD5Update(&ctx, (u_char *)mi->m_data, mi->m_len);
	mi = mi->m_next;
    }

    MD5Final(auth, &ctx);
    ctx = xd->edx_octx;
    MD5Update(&ctx, auth, ESPDESMD5_ALEN);
    MD5Final(auth, &ctx);   /* That's the authenticator */

    /* 
     * This routine is different from espdes_output() in that
     * here we construct the whole packet before starting encrypting.
     */

    m = m_pullup(m, iphlen + ESPDESMD5_RPLENGTH + 
		 sizeof(u_int32_t) + xd->edx_ivlen);
    if (m == NULL)
      return ENOBUFS;

    /* Copy data if necessary */
    if (m->m_len - iphlen)
    {
#ifdef ENCDEBUG
	if (encdebug)
	  printf("espdesmd5_output: pushing data\n");
#endif
	ms = m_copym(m, iphlen, m->m_len - iphlen, M_DONTWAIT);
	if (ms == NULL)
	  return ENOBUFS;
	
	ms->m_next = m->m_next;
	m->m_next = ms;
	m->m_len = iphlen;
    }
	
    /* Copy SPI, IV (or not) and replay counter */
    bcopy((caddr_t)&spi, mtod(m, caddr_t) + iphlen, sizeof (u_int32_t));
    bcopy((caddr_t)iv,  mtod(m, caddr_t) + iphlen + sizeof (u_int32_t),
	  xd->edx_ivlen);
    bcopy((caddr_t)&rplc, mtod(m, caddr_t) + iphlen + sizeof(u_int32_t) +
	  xd->edx_ivlen, ESPDESMD5_RPLENGTH);

#ifdef ENCDEBUG
    if (encdebug)
      printf("espdesmd5_output: replay counter (wire value) %x\n", rplc);
#endif

    /* Adjust the length accordingly */
    m->m_len += sizeof(u_int32_t) + ESPDESMD5_RPLENGTH + xd->edx_ivlen;
    m->m_pkthdr.len += sizeof(u_int32_t) + ESPDESMD5_RPLENGTH + 
		       xd->edx_ivlen;

#ifdef ENCDEBUG
    if (encdebug)
      printf("espdesmd5_output: mbuf chain length %d\n", m->m_pkthdr.len);
#endif

    /* Let's append the authenticator too */
    MGET(ms, M_DONTWAIT, MT_DATA);
    if (ms == NULL)
      return ENOBUFS;

    bcopy(auth, mtod(ms, u_char *), ESPDESMD5_ALEN);
    ms->m_len = ESPDESMD5_ALEN;

    m_cat(m, ms);
    m->m_pkthdr.len += ESPDESMD5_ALEN;  /* Adjust length */
	
#ifdef ENCDEBUG
    if (encdebug)
      printf("espdesmd5_output: final mbuf chain length %d\n",
	     m->m_pkthdr.len);
#endif

    ilen = olen = m->m_len - iphlen - sizeof(u_int32_t) - xd->edx_ivlen;
    idat = odat = mtod(m, u_char *) + iphlen + sizeof(u_int32_t) 
	   + xd->edx_ivlen;
    i = 0;
    mi = mo = m;

#ifdef ENCDEBUG
    if (encdebug)
      printf("espdesmd5_output: starting encryption with ilen=%d, plen=%d\n",
	     ilen, plen);
#endif

    while (plen > 0)		/* while not done */
    {
	while (ilen == 0)	/* we exhausted previous mbuf */
	{
	    mi = mi->m_next;
	    if (mi == NULL)
	      panic("espdesmd5_output: bad chain (i)\n");
	    ilen = mi->m_len;
	    idat = (u_char *)mi->m_data;
	}

	blk[i] = *idat++ ^ iv[i];
		
	i++;
	ilen--;

	if (i == 8)   /* We have full block */
	{
	    des_ecb_encrypt(blk, blk, (caddr_t)(xd->edx_eks), 1);
	    for (i = 0; i < 8; i++)
	    {
		while (olen == 0)
		{
		    mo = mo->m_next;
		    if (mo == NULL)
		      panic("espdesmd5_output: bad chain (o)\n");
		    olen = mo->m_len;
		    odat = (u_char *)mo->m_data;
		}
		*odat++ = blk[i];
		iv[i] = blk[i];
		olen--;
	    }
	    i = 0;
	}

	plen--;
    }

#ifdef ENCDEBUG
    if (encdebug)
      printf("espdesmd5_output: almost done now\n");
#endif

    if (xd->edx_ivlen != 0)
      bcopy(iv, xd->edx_iv, ESPDESMD5_IVS); /* New IV */

    /* Fix the length and the next protocol, copy back and off we go */
    ipo.ip_len = htons(iphlen + ohlen + rlen + padding +
		       ESPDESMD5_ALEN);
    ipo.ip_p = IPPROTO_ESP;
    bcopy((caddr_t)&ipo, mtod(m, caddr_t), sizeof(struct ip));
	
    /* Copy back the options, if existing */
    if (iphlen != sizeof(struct ip))
      bcopy(opts, mtod(m, caddr_t) + sizeof(struct ip),
	    iphlen - sizeof(struct ip));
    
    /* Update the counters */
    tdb->tdb_cur_packets++;
    tdb->tdb_cur_bytes += rlen + padding;

    *mp = m;
    return 0;
}	


/*
 * both versions of the checkreplaywindow for 32 and 64 bit counters
 * return 0 on success
 * return 1 for counter == 0
 * return 2 for very old packet
 * return 3 for packet within current window but already received
 */

int
checkreplaywindow64(u_int64_t seq, u_int64_t *lastseq, u_int64_t window, u_int64_t *bitmap)
{
    u_int64_t diff;

    if (seq == 0)
      return 1;

    if (seq > *lastseq)
    {
	diff = seq - *lastseq;
	if (diff < window)
	  *bitmap = ((*bitmap) << diff) | 1;
	else
	  *bitmap = 1;
	*lastseq = seq;
	return 0;
    }
    diff = *lastseq - seq;
    if (diff >= window)
    {
	espstat.esps_wrap++;
	return 2;
    }
    if ((*bitmap) & (((u_int64_t) 1) << diff))
    {
	espstat.esps_replay++;
	return 3;
    }
    *bitmap |= (((u_int64_t) 1) << diff);
    return 0;
}

int
checkreplaywindow32(u_int32_t seq, u_int32_t initial, u_int32_t *lastseq, u_int32_t window, u_int32_t *bitmap)
{
    u_int32_t diff;

#ifdef ENCDEBUG
    if (encdebug)
      printf("checkreplaywindow32: seq=%x lastseq=%x\n", seq, *lastseq);
#endif

    seq -= initial;

    if (seq == 0)
      return 1;

    if (seq > *lastseq - initial)
    {
	diff = seq - (*lastseq - initial);
	if (diff < window)
	  *bitmap = ((*bitmap) << diff) | 1;
	else
	  *bitmap = 1;
	*lastseq = seq + initial;
	return 0;
    }
    diff = *lastseq - initial - seq;
    if (diff >= window)
    {
	espstat.esps_wrap++;
	return 2;
    }
    if ((*bitmap) & (((u_int32_t) 1) << diff))
    {
	espstat.esps_replay++;
	return 3;
    }
    *bitmap |= (((u_int32_t) 1) << diff);
    return 0;
}
