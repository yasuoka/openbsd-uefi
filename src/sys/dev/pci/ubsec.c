/*	$OpenBSD: ubsec.c,v 1.44 2001/04/29 00:37:11 jason Exp $	*/

/*
 * Copyright (c) 2000 Jason L. Wright (jason@thought.net)
 * Copyright (c) 2000 Theo de Raadt (deraadt@openbsd.org)
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Jason L. Wright
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#undef UBSEC_DEBUG


/*
 * uBsec 5[56]01, 580x hardware crypto accelerator
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/mbuf.h>
#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/pmap.h>
#include <machine/pmap.h>
#include <sys/device.h>
#include <sys/queue.h>

#include <crypto/crypto.h>
#include <crypto/cryptosoft.h>
#include <dev/rndvar.h>
#include <sys/md5k.h>
#include <crypto/sha1.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

#include <dev/pci/ubsecreg.h>
#include <dev/pci/ubsecvar.h>

/*
 * Prototypes and count for the pci_device structure
 */
int ubsec_probe		__P((struct device *, void *, void *));
void ubsec_attach	__P((struct device *, struct device *, void *));

struct cfattach ubsec_ca = {
	sizeof(struct ubsec_softc), ubsec_probe, ubsec_attach,
};

struct cfdriver ubsec_cd = {
	0, "ubsec", DV_DULL
};

int	ubsec_intr __P((void *));
int	ubsec_newsession __P((u_int32_t *, struct cryptoini *));
int	ubsec_freesession __P((u_int64_t));
int	ubsec_process __P((struct cryptop *));
void	ubsec_callback __P((struct ubsec_q *));
int	ubsec_feed __P((struct ubsec_softc *));
void	ubsec_mcopy __P((struct mbuf *, struct mbuf *, int, int));
void	ubsec_callback2 __P((struct ubsec_q2 *));
int	ubsec_feed2 __P((struct ubsec_softc *));
void	ubsec_rng __P((void *));

#define	READ_REG(sc,r) \
	bus_space_read_4((sc)->sc_st, (sc)->sc_sh, (r))

#define WRITE_REG(sc,reg,val) \
	bus_space_write_4((sc)->sc_st, (sc)->sc_sh, reg, val)

#define	SWAP32(x) (x) = swap32((x))

int
ubsec_probe(parent, match, aux)
	struct device *parent;
	void *match;
	void *aux;
{
	struct pci_attach_args *pa = (struct pci_attach_args *) aux;

	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_BLUESTEEL &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_BLUESTEEL_5501)
		return (1);
	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_BLUESTEEL &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_BLUESTEEL_5601)
		return (1);
	if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_BROADCOM &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_BROADCOM_5805)
		return (1);
	return (0);
}

void
ubsec_attach(parent, self, aux)
	struct device *parent, *self;
	void *aux;
{
	struct ubsec_softc *sc = (struct ubsec_softc *)self;
	struct pci_attach_args *pa = aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pci_intr_handle_t ih;
	const char *intrstr = NULL;
	bus_size_t iosize;
	u_int32_t cmd;

	SIMPLEQ_INIT(&sc->sc_queue);
	SIMPLEQ_INIT(&sc->sc_qchip);
	SIMPLEQ_INIT(&sc->sc_queue2);
	SIMPLEQ_INIT(&sc->sc_qchip2);

	sc->sc_statmask = BS_STAT_MCR1_DONE | BS_STAT_DMAERR;
	if ((PCI_VENDOR(pa->pa_id) == PCI_VENDOR_BLUESTEEL &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_BLUESTEEL_5601) ||
	    (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_BROADCOM &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_BROADCOM_5805)) {
		sc->sc_statmask |= BS_STAT_MCR2_DONE;
		sc->sc_5601 = 1;
	}
	
	cmd = pci_conf_read(pc, pa->pa_tag, PCI_COMMAND_STATUS_REG);
	cmd |= PCI_COMMAND_MEM_ENABLE | PCI_COMMAND_MASTER_ENABLE;
	pci_conf_write(pc, pa->pa_tag, PCI_COMMAND_STATUS_REG, cmd);
	cmd = pci_conf_read(pc, pa->pa_tag, PCI_COMMAND_STATUS_REG);

	if (!(cmd & PCI_COMMAND_MEM_ENABLE)) {
		printf(": failed to enable memory mapping\n");
		return;
	}

	if (pci_mapreg_map(pa, BS_BAR, PCI_MAPREG_TYPE_MEM, 0,
	    &sc->sc_st, &sc->sc_sh, NULL, &iosize)) {
		printf(": can't find mem space\n");
		return;
	}
	sc->sc_dmat = pa->pa_dmat;

	if (pci_intr_map(pc, pa->pa_intrtag, pa->pa_intrpin,
	    pa->pa_intrline, &ih)) {
		printf(": couldn't map interrupt\n");
		bus_space_unmap(sc->sc_st, sc->sc_sh, iosize);
		return;
	}
	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_NET, ubsec_intr, sc,
	    self->dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		bus_space_unmap(sc->sc_st, sc->sc_sh, iosize);
		return;
	}

	sc->sc_cid = crypto_get_driverid();
	if (sc->sc_cid < 0) {
		pci_intr_disestablish(pc, sc->sc_ih);
		bus_space_unmap(sc->sc_st, sc->sc_sh, iosize);
		return;
	}

	crypto_register(sc->sc_cid, CRYPTO_3DES_CBC,
	    ubsec_newsession, ubsec_freesession, ubsec_process);
	crypto_register(sc->sc_cid, CRYPTO_DES_CBC, NULL, NULL, NULL);
	crypto_register(sc->sc_cid, CRYPTO_MD5_HMAC, NULL, NULL, NULL);
	crypto_register(sc->sc_cid, CRYPTO_SHA1_HMAC, NULL, NULL, NULL);

	WRITE_REG(sc, BS_CTRL,
	    READ_REG(sc, BS_CTRL) | BS_CTRL_MCR1INT | BS_CTRL_DMAERR |
	    (sc->sc_5601 ? BS_CTRL_MCR2INT : 0));

	if (sc->sc_5601) {
		timeout_set(&sc->sc_rngto, ubsec_rng, sc);
		timeout_add(&sc->sc_rngto, 1);
	}

	printf(": %s\n", intrstr);
}

int
ubsec_intr(arg)
	void *arg;
{
	struct ubsec_softc *sc = arg;
	volatile u_int32_t stat, a;
	struct ubsec_q *q;
	struct ubsec_q2 *q2;
	struct ubsec_mcr *mcr;
	int npkts = 0, i;

	stat = READ_REG(sc, BS_STAT);

	stat &= sc->sc_statmask;
	if (stat == 0)
		return (0);

	WRITE_REG(sc, BS_STAT, stat);		/* IACK */

#ifdef UBSEC_DEBUG
	printf("ubsec intr %x\n", stat);
#endif
	if ((stat & BS_STAT_MCR1_DONE)) {
		while (!SIMPLEQ_EMPTY(&sc->sc_qchip)) {
			q = SIMPLEQ_FIRST(&sc->sc_qchip);
#ifdef UBSEC_DEBUG
			printf("mcr_flags %x %x %x\n", q->q_mcr,
			    q->q_mcr->mcr_flags, READ_REG(sc, BS_ERR));
#endif
			if ((q->q_mcr->mcr_flags & UBS_MCR_DONE) == 0)
				break;
			npkts++;
			SIMPLEQ_REMOVE_HEAD(&sc->sc_qchip, q, q_next);
#ifdef UBSEC_DEBUG
			printf("intr: callback q %08x flags %04x\n", q,
			    q->q_mcr->mcr_flags);
#endif
			mcr = q->q_mcr;
			ubsec_callback(q);

			/*
			 * search for further sc_qchip ubsec_q's that share
			 * the same MCR, and complete them too, they must be
			 * at the top.
			 */
			for (i = 1; i < mcr->mcr_pkts; i++) {
				q = SIMPLEQ_FIRST(&sc->sc_qchip);
				if (q && q->q_mcr == mcr) {
#ifdef UBSEC_DEBUG
					printf("found shared mcr %d out of %d\n",
					    i, mcr->mcr_pkts);
#endif
					SIMPLEQ_REMOVE_HEAD(&sc->sc_qchip,
					    q, q_next);
					ubsec_callback(q);
				} else {
					printf("HUH!\n");
					break;
				}
			}

			free(mcr, M_DEVBUF);
		}
#ifdef UBSEC_DEBUG
		if (npkts > 1)
			printf("intr: %d pkts\n", npkts);
#endif
		ubsec_feed(sc);
	}

	if (sc->sc_5601 && (stat & BS_STAT_MCR2_DONE)) {
		while (!SIMPLEQ_EMPTY(&sc->sc_qchip2)) {
			q2 = SIMPLEQ_FIRST(&sc->sc_qchip2);

			if ((q2->q_mcr->mcr_flags & UBS_MCR_DONE) == 0)
				break;
			SIMPLEQ_REMOVE_HEAD(&sc->sc_qchip2, q2, q_next);
			ubsec_callback2(q2);
			ubsec_feed2(sc);
		}
	}

	if (stat & BS_STAT_DMAERR) {
		a = READ_REG(sc, BS_ERR);
		printf("%s: dmaerr %s@%08x\n", sc->sc_dv.dv_xname,
		    (a & BS_ERR_READ) ? "read" : "write",
		    a & BS_ERR_ADDR);
	}

	return (1);
}

int
ubsec_feed(sc)
	struct ubsec_softc *sc;
{
	static int max;
	struct ubsec_q *q;
	struct ubsec_mcr *mcr;
	int npkts, i, l;
	void *v, *mcr2;

	npkts = sc->sc_nqueue;
	if (npkts > 5)
		npkts = 5;
	if (npkts < 2)
		goto feed1;

	if (READ_REG(sc, BS_STAT) & BS_STAT_MCR1_FULL)
		return (0);

	mcr = (struct ubsec_mcr *)malloc(sizeof(struct ubsec_mcr) +
	    (npkts-1) * sizeof(struct ubsec_mcr_add), M_DEVBUF, M_NOWAIT);
	if (mcr == NULL)
		goto feed1;

#ifdef UBSEC_DEBUG
	printf("merging %d records\n", npkts);
#endif

	/* XXX temporary aggregation statistics reporting code */
	if (max < npkts) {
		max = npkts;
		printf("%s: new max aggregate %d\n", sc->sc_dv.dv_xname, max);
	}

	for (mcr2 = mcr, i = 0; i < npkts; i++) {
		q = SIMPLEQ_FIRST(&sc->sc_queue);
		SIMPLEQ_REMOVE_HEAD(&sc->sc_queue, q, q_next);
		--sc->sc_nqueue;
		SIMPLEQ_INSERT_TAIL(&sc->sc_qchip, q, q_next);

		/*
		 * first packet contains a full mcr, others contain
		 * a shortened one
		 */
		if (i == 0) {
			v = q->q_mcr;
			l = sizeof(struct ubsec_mcr);
		} else {
			v = ((void *)q->q_mcr) + sizeof(struct ubsec_mcr) -
			    sizeof(struct ubsec_mcr_add);
			l = sizeof(struct ubsec_mcr_add);
		}
#ifdef UBSEC_DEBUG
		printf("copying %d from %x (mcr %x)\n", l, v, q->q_mcr);
#endif
		bcopy(v, mcr2, l);
		mcr2 += l;

		free(q->q_mcr, M_DEVBUF);
		q->q_mcr = mcr;
	}
	mcr->mcr_pkts = npkts;
	WRITE_REG(sc, BS_MCR1, (u_int32_t)vtophys(mcr));
	return (0);

feed1:
	while (!SIMPLEQ_EMPTY(&sc->sc_queue)) {
		if (READ_REG(sc, BS_STAT) & BS_STAT_MCR1_FULL)
			break;
		q = SIMPLEQ_FIRST(&sc->sc_queue);
		WRITE_REG(sc, BS_MCR1, (u_int32_t)vtophys(q->q_mcr));
#ifdef UBSEC_DEBUG
		printf("feed: q->chip %08x %08x\n", q, (u_int32_t)vtophys(q->q_mcr));
#endif
		SIMPLEQ_REMOVE_HEAD(&sc->sc_queue, q, q_next);
		--sc->sc_nqueue;
		SIMPLEQ_INSERT_TAIL(&sc->sc_qchip, q, q_next);
	}
	return (0);
}

/*
 * Allocate a new 'session' and return an encoded session id.  'sidp'
 * contains our registration id, and should contain an encoded session
 * id on successful allocation.
 */
int
ubsec_newsession(sidp, cri)
	u_int32_t *sidp;
	struct cryptoini *cri;
{
	struct cryptoini *c, *encini = NULL, *macini = NULL;
	struct ubsec_softc *sc = NULL;
	struct ubsec_session *ses = NULL;
	MD5_CTX md5ctx;
	SHA1_CTX sha1ctx;
	int i, sesn;

	if (sidp == NULL || cri == NULL)
		return (EINVAL);

	for (i = 0; i < ubsec_cd.cd_ndevs; i++) {
		sc = ubsec_cd.cd_devs[i];
		if (sc == NULL || sc->sc_cid == (*sidp))
			break;
	}
	if (sc == NULL)
		return (EINVAL);

	for (c = cri; c != NULL; c = c->cri_next) {
		if (c->cri_alg == CRYPTO_MD5_HMAC ||
		    c->cri_alg == CRYPTO_SHA1_HMAC) {
			if (macini)
				return (EINVAL);
			macini = c;
		} else if (c->cri_alg == CRYPTO_DES_CBC ||
		    c->cri_alg == CRYPTO_3DES_CBC) {
			if (encini)
				return (EINVAL);
			encini = c;
		} else
			return (EINVAL);
	}
	if (encini == NULL && macini == NULL)
		return (EINVAL);

	if (sc->sc_sessions == NULL) {
		ses = sc->sc_sessions = (struct ubsec_session *)malloc(
		    sizeof(struct ubsec_session), M_DEVBUF, M_NOWAIT);
		if (ses == NULL)
			return (ENOMEM);
		sesn = 0;
		sc->sc_nsessions = 1;
	} else {
		for (sesn = 0; sesn < sc->sc_nsessions; sesn++) {
			if (sc->sc_sessions[sesn].ses_used == 0) {
				ses = &sc->sc_sessions[sesn];
				break;
			}
		}

		if (ses == NULL) {
			sesn = sc->sc_nsessions;
			ses = (struct ubsec_session *)malloc((sesn + 1) *
			    sizeof(struct ubsec_session), M_DEVBUF, M_NOWAIT);
			if (ses == NULL)
				return (ENOMEM);
			bcopy(sc->sc_sessions, ses, sesn *
			    sizeof(struct ubsec_session));
			bzero(sc->sc_sessions, sesn *
			    sizeof(struct ubsec_session));
			free(sc->sc_sessions, M_DEVBUF);
			sc->sc_sessions = ses;
			ses = &sc->sc_sessions[sesn];
			sc->sc_nsessions++;
		}
	}

	bzero(ses, sizeof(struct ubsec_session));
	ses->ses_used = 1;
	if (encini) {
		/* get an IV, network byte order */
		get_random_bytes(ses->ses_iv, sizeof(ses->ses_iv));

		/* Go ahead and compute key in ubsec's byte order */
		if (encini->cri_alg == CRYPTO_DES_CBC) {
			bcopy(encini->cri_key, &ses->ses_deskey[0], 8);
			bcopy(encini->cri_key, &ses->ses_deskey[2], 8);
			bcopy(encini->cri_key, &ses->ses_deskey[4], 8);
		} else
			bcopy(encini->cri_key, ses->ses_deskey, 24);

		SWAP32(ses->ses_deskey[0]);
		SWAP32(ses->ses_deskey[1]);
		SWAP32(ses->ses_deskey[2]);
		SWAP32(ses->ses_deskey[3]);
		SWAP32(ses->ses_deskey[4]);
		SWAP32(ses->ses_deskey[5]);
	}

	if (macini) {
		for (i = 0; i < macini->cri_klen / 8; i++)
			macini->cri_key[i] ^= HMAC_IPAD_VAL;

		if (macini->cri_alg == CRYPTO_MD5_HMAC) {
			MD5Init(&md5ctx);
			MD5Update(&md5ctx, macini->cri_key,
			    macini->cri_klen / 8);
			MD5Update(&md5ctx, hmac_ipad_buffer,
			    HMAC_BLOCK_LEN - (macini->cri_klen / 8));
			bcopy(md5ctx.state, ses->ses_hminner,
			    sizeof(md5ctx.state));
		} else {
			SHA1Init(&sha1ctx);
			SHA1Update(&sha1ctx, macini->cri_key,
			    macini->cri_klen / 8);
			SHA1Update(&sha1ctx, hmac_ipad_buffer,
			    HMAC_BLOCK_LEN - (macini->cri_klen / 8));
			bcopy(sha1ctx.state, ses->ses_hminner,
			    sizeof(sha1ctx.state));
		}

		for (i = 0; i < macini->cri_klen / 8; i++)
			macini->cri_key[i] ^= (HMAC_IPAD_VAL ^ HMAC_OPAD_VAL);

		if (macini->cri_alg == CRYPTO_MD5_HMAC) {
			MD5Init(&md5ctx);
			MD5Update(&md5ctx, macini->cri_key,
			    macini->cri_klen / 8);
			MD5Update(&md5ctx, hmac_opad_buffer,
			    HMAC_BLOCK_LEN - (macini->cri_klen / 8));
			bcopy(md5ctx.state, ses->ses_hmouter,
			    sizeof(md5ctx.state));
		} else {
			SHA1Init(&sha1ctx);
			SHA1Update(&sha1ctx, macini->cri_key,
			    macini->cri_klen / 8);
			SHA1Update(&sha1ctx, hmac_opad_buffer,
			    HMAC_BLOCK_LEN - (macini->cri_klen / 8));
			bcopy(sha1ctx.state, ses->ses_hmouter,
			    sizeof(sha1ctx.state));
		}

		for (i = 0; i < macini->cri_klen / 8; i++)
			macini->cri_key[i] ^= HMAC_OPAD_VAL;
	}

	*sidp = UBSEC_SID(sc->sc_dv.dv_unit, sesn);
	return (0);
}

/*
 * Deallocate a session.
 */
int
ubsec_freesession(tid)
	u_int64_t tid;
{
	struct ubsec_softc *sc;
	int card, session;
	u_int32_t sid = ((u_int32_t) tid) & 0xffffffff;

	card = UBSEC_CARD(sid);
	if (card >= ubsec_cd.cd_ndevs || ubsec_cd.cd_devs[card] == NULL)
		return (EINVAL);
	sc = ubsec_cd.cd_devs[card];
	session = UBSEC_SESSION(sid);
	bzero(&sc->sc_sessions[session], sizeof(sc->sc_sessions[session]));
	return (0);
}

int
ubsec_process(crp)
	struct cryptop *crp;
{
	struct ubsec_q *q = NULL;
	int card, err, i, j, s, nicealign;
	struct ubsec_softc *sc;
	struct cryptodesc *crd1, *crd2, *maccrd, *enccrd;
	int encoffset = 0, macoffset = 0, cpskip, cpoffset;
	int sskip, dskip, stheend, dtheend;
	int16_t coffset;
	struct ubsec_session *ses;

	if (crp == NULL || crp->crp_callback == NULL)
		return (EINVAL);

	card = UBSEC_CARD(crp->crp_sid);
	if (card >= ubsec_cd.cd_ndevs || ubsec_cd.cd_devs[card] == NULL) {
		err = EINVAL;
		goto errout;
	}

	sc = ubsec_cd.cd_devs[card];

	s = splnet();
	if (sc->sc_nqueue >= UBS_MAX_NQUEUE) {
		splx(s);
		err = ENOMEM;
		goto errout;
	}
	splx(s);

	q = (struct ubsec_q *)malloc(sizeof(struct ubsec_q),
	    M_DEVBUF, M_NOWAIT);
	if (q == NULL) {
		err = ENOMEM;
		goto errout;
	}
	bzero(q, sizeof(struct ubsec_q));

	q->q_sesn = UBSEC_SESSION(crp->crp_sid);
	ses = &sc->sc_sessions[q->q_sesn];

	if (crp->crp_flags & CRYPTO_F_IMBUF) {
		q->q_src_m = (struct mbuf *)crp->crp_buf;
		q->q_dst_m = (struct mbuf *)crp->crp_buf;
	} else {
		err = EINVAL;
		goto errout;	/* XXX only handle mbufs right now */
	}

	q->q_mcr = (struct ubsec_mcr *)malloc(sizeof(struct ubsec_mcr),
	    M_DEVBUF, M_NOWAIT);
	if (q->q_mcr == NULL) {
		err = ENOMEM;
		goto errout;
	}
	bzero(q->q_mcr, sizeof(struct ubsec_mcr));

	q->q_mcr->mcr_pkts = 1;
	q->q_mcr->mcr_flags = 0;
	q->q_mcr->mcr_cmdctxp = vtophys(&q->q_ctx);
	q->q_sc = sc;
	q->q_crp = crp;

	crd1 = crp->crp_desc;
	if (crd1 == NULL) {
		err = EINVAL;
		goto errout;
	}
	crd2 = crd1->crd_next;

	if (crd2 == NULL) {
		if (crd1->crd_alg == CRYPTO_MD5_HMAC ||
		    crd1->crd_alg == CRYPTO_SHA1_HMAC) {
			maccrd = crd1;
			enccrd = NULL;
		} else if (crd1->crd_alg == CRYPTO_DES_CBC ||
		    crd1->crd_alg == CRYPTO_3DES_CBC) {
			maccrd = NULL;
			enccrd = crd1;
		} else {
			err = EINVAL;
			goto errout;
		}
	} else {
		if ((crd1->crd_alg == CRYPTO_MD5_HMAC ||
		    crd1->crd_alg == CRYPTO_SHA1_HMAC) &&
		    (crd2->crd_alg == CRYPTO_DES_CBC ||
			crd2->crd_alg == CRYPTO_3DES_CBC) &&
		    ((crd2->crd_flags & CRD_F_ENCRYPT) == 0)) {
			maccrd = crd1;
			enccrd = crd2;
		} else if ((crd1->crd_alg == CRYPTO_DES_CBC ||
		    crd1->crd_alg == CRYPTO_3DES_CBC) &&
		    (crd2->crd_alg == CRYPTO_MD5_HMAC ||
			crd2->crd_alg == CRYPTO_SHA1_HMAC) &&
		    (crd1->crd_flags & CRD_F_ENCRYPT)) {
			enccrd = crd1;
			maccrd = crd2;
		} else {
			/*
			 * We cannot order the ubsec as requested
			 */
			err = EINVAL;
			goto errout;
		}
	}

	if (enccrd) {
		encoffset = enccrd->crd_skip;
		q->q_ctx.pc_flags |= UBS_PKTCTX_ENC_3DES;

		if (enccrd->crd_flags & CRD_F_ENCRYPT) {
			if (enccrd->crd_flags & CRD_F_IV_EXPLICIT)
				bcopy(enccrd->crd_iv, q->q_ctx.pc_iv, 8);
			else {
				q->q_ctx.pc_iv[0] = ses->ses_iv[0];
				q->q_ctx.pc_iv[1] = ses->ses_iv[1];
			}

			if ((enccrd->crd_flags & CRD_F_IV_PRESENT) == 0)
				m_copyback(q->q_src_m, enccrd->crd_inject,
				    8, (caddr_t)q->q_ctx.pc_iv);
		} else {
			q->q_ctx.pc_flags |= UBS_PKTCTX_INBOUND;

			if (enccrd->crd_flags & CRD_F_IV_EXPLICIT)
				bcopy(enccrd->crd_iv, q->q_ctx.pc_iv, 8);
			else
				m_copydata(q->q_src_m, enccrd->crd_inject,
				    8, (caddr_t)q->q_ctx.pc_iv);
		}

		q->q_ctx.pc_deskey[0] = ses->ses_deskey[0];
		q->q_ctx.pc_deskey[1] = ses->ses_deskey[1];
		q->q_ctx.pc_deskey[2] = ses->ses_deskey[2];
		q->q_ctx.pc_deskey[3] = ses->ses_deskey[3];
		q->q_ctx.pc_deskey[4] = ses->ses_deskey[4];
		q->q_ctx.pc_deskey[5] = ses->ses_deskey[5];
		SWAP32(q->q_ctx.pc_iv[0]);
		SWAP32(q->q_ctx.pc_iv[1]);
	}

	if (maccrd) {
		macoffset = maccrd->crd_skip;

		if (maccrd->crd_alg == CRYPTO_MD5_HMAC)
			q->q_ctx.pc_flags |= UBS_PKTCTX_AUTH_MD5;
		else
			q->q_ctx.pc_flags |= UBS_PKTCTX_AUTH_SHA1;

		for (i = 0; i < 5; i++) {
			q->q_ctx.pc_hminner[i] = ses->ses_hminner[i];
			q->q_ctx.pc_hmouter[i] = ses->ses_hmouter[i];
		}
	}

	if (enccrd && maccrd) {
		/*
		 * ubsec cannot handle packets where the end of encryption
		 * and authentication are not the same, or where the
		 * encrypted part begins before the authenticated part.
		 */
		if (((encoffset + enccrd->crd_len) !=
		    (macoffset + maccrd->crd_len)) ||
		    (enccrd->crd_skip < maccrd->crd_skip)) {
			err = EINVAL;
			goto errout;
		}
		sskip = maccrd->crd_skip;
		cpskip = dskip = enccrd->crd_skip;
		stheend = maccrd->crd_len;
		dtheend = enccrd->crd_len;
		coffset = enccrd->crd_skip - maccrd->crd_skip;
		cpoffset = cpskip + dtheend;
#ifdef UBSEC_DEBUG
		printf("mac: skip %d, len %d, inject %d\n",
 		    maccrd->crd_skip, maccrd->crd_len, maccrd->crd_inject);
		printf("enc: skip %d, len %d, inject %d\n",
		    enccrd->crd_skip, enccrd->crd_len, enccrd->crd_inject);
		printf("src: skip %d, len %d\n", sskip, stheend);
		printf("dst: skip %d, len %d\n", dskip, dtheend);
		printf("ubs: coffset %d, pktlen %d, cpskip %d, cpoffset %d\n",
		    coffset, stheend, cpskip, cpoffset);
#endif
	} else {
		cpskip = dskip = sskip = macoffset + encoffset;
		dtheend = stheend = (enccrd)?enccrd->crd_len:maccrd->crd_len;
		cpoffset = cpskip + dtheend;
		coffset = 0;
	}
	q->q_ctx.pc_offset = coffset >> 2;

	q->q_src_l = mbuf2pages(q->q_src_m, &q->q_src_npa, q->q_src_packp,
	    q->q_src_packl, MAX_SCATTER, &nicealign);
	if (q->q_src_l == 0) {
		err = ENOMEM;
		goto errout;
	}
	q->q_mcr->mcr_pktlen = stheend;

#ifdef UBSEC_DEBUG
	printf("src skip: %d\n", sskip);
#endif
	for (i = j = 0; i < q->q_src_npa; i++) {
		struct ubsec_pktbuf *pb;

#ifdef UBSEC_DEBUG
		printf("  src[%d->%d]: %d@%x\n", i, j,
		    q->q_src_packl[i], q->q_src_packp[i]);
#endif
		if (sskip) {
			if (sskip >= q->q_src_packl[i]) {
				sskip -= q->q_src_packl[i];
				continue;
			}
			q->q_src_packp[i] += sskip;
			q->q_src_packl[i] -= sskip;
			sskip = 0;
		}

		if (j == 0)
			pb = &q->q_mcr->mcr_ipktbuf;
		else
			pb = &q->q_srcpkt[j - 1];

#ifdef UBSEC_DEBUG
		printf("  pb v %08x p %08x\n", pb, vtophys(pb));
#endif
		pb->pb_addr = q->q_src_packp[i];
		if (stheend) {
			if (q->q_src_packl[i] > stheend) {
				pb->pb_len = stheend;
				stheend = 0;
			} else {
				pb->pb_len = q->q_src_packl[i];
				stheend -= pb->pb_len;
			}
		} else
			pb->pb_len = q->q_src_packl[i];

		if ((i + 1) == q->q_src_npa)
			pb->pb_next = 0;
		else
			pb->pb_next = vtophys(&q->q_srcpkt[j]);
		j++;
	}
#ifdef UBSEC_DEBUG
	printf("  buf[%x]: %d@%x -> %x\n", vtophys(q->q_mcr),
	    q->q_mcr->mcr_ipktbuf.pb_len,
	    q->q_mcr->mcr_ipktbuf.pb_addr,
	    q->q_mcr->mcr_ipktbuf.pb_next);
	for (i = 0; i < j - 1; i++) {
		printf("  buf[%x]: %d@%x -> %x\n", vtophys(&q->q_srcpkt[i]),
		    q->q_srcpkt[i].pb_len,
		    q->q_srcpkt[i].pb_addr,
		    q->q_srcpkt[i].pb_next);
	}
#endif

	if (enccrd == NULL && maccrd != NULL) {
		q->q_mcr->mcr_opktbuf.pb_addr = 0;
		q->q_mcr->mcr_opktbuf.pb_len = 0;
		q->q_mcr->mcr_opktbuf.pb_next =
		    (u_int32_t)vtophys(q->q_macbuf);
#ifdef UBSEC_DEBUG
		printf("opkt: %x %x %x\n",
		    q->q_mcr->mcr_opktbuf.pb_addr,
		    q->q_mcr->mcr_opktbuf.pb_len,
		    q->q_mcr->mcr_opktbuf.pb_next);
#endif
	} else {
		if (!nicealign) {
			int totlen, len;
			struct mbuf *m, *top, **mp;

			totlen = q->q_dst_l = q->q_src_l;
			if (q->q_src_m->m_flags & M_PKTHDR) {
				len = MHLEN;
				MGETHDR(m, M_DONTWAIT, MT_DATA);
			} else {
				len = MLEN;
				MGET(m, M_DONTWAIT, MT_DATA);
			}
			if (m == NULL) {
				err = ENOMEM;
				goto errout;
			}
			if (len == MHLEN)
				M_DUP_PKTHDR(m, q->q_src_m);
			if (totlen >= MINCLSIZE) {
				MCLGET(m, M_DONTWAIT);
				if (m->m_flags & M_EXT)
					len = MCLBYTES;
			}
			m->m_len = len;
			top = NULL;
			mp = &top;

			while (totlen > 0) {
				if (top) {
					MGET(m, M_DONTWAIT, MT_DATA);
					if (m == NULL) {
						m_freem(top);
						err = ENOMEM;
						goto errout;
					}
					len = MLEN;
				}
				if (top && totlen >= MINCLSIZE) {
					MCLGET(m, M_DONTWAIT);
					if (m->m_flags & M_EXT)
						len = MCLBYTES;
				}
				m->m_len = len = min(totlen, len);
				totlen -= len;
				*mp = m;
				mp = &m->m_next;
			}
			q->q_dst_m = top;
			ubsec_mcopy(q->q_src_m, q->q_dst_m, cpskip, cpoffset);
		} else
			q->q_dst_m = q->q_src_m;

		q->q_dst_l = mbuf2pages(q->q_dst_m, &q->q_dst_npa,
		    q->q_dst_packp, q->q_dst_packl, MAX_SCATTER, NULL);

#ifdef UBSEC_DEBUG
		printf("dst skip: %d\n", dskip);
#endif
		for (i = j = 0; i < q->q_dst_npa; i++) {
			struct ubsec_pktbuf *pb;

#ifdef UBSEC_DEBUG
			printf("  dst[%d->%d]: %d@%x\n", i, j,
			    q->q_dst_packl[i], q->q_dst_packp[i]);
#endif
			if (dskip) {
				if (dskip >= q->q_dst_packl[i]) {
					dskip -= q->q_dst_packl[i];
					continue;
				}
				q->q_dst_packp[i] += dskip;
				q->q_dst_packl[i] -= dskip;
				dskip = 0;
			}

			if (j == 0)
				pb = &q->q_mcr->mcr_opktbuf;
			else
				pb = &q->q_dstpkt[j - 1];

#ifdef UBSEC_DEBUG
			printf("  pb v %08x p %08x\n", pb, vtophys(pb));
#endif
			pb->pb_addr = q->q_dst_packp[i];

			if (dtheend) {
				if (q->q_dst_packl[i] > dtheend) {
					pb->pb_len = dtheend;
					dtheend = 0;
				} else {
					pb->pb_len = q->q_dst_packl[i];
					dtheend -= pb->pb_len;
				}
			} else
				pb->pb_len = q->q_dst_packl[i];

			if ((i + 1) == q->q_dst_npa) {
				if (maccrd)
					pb->pb_next = vtophys(q->q_macbuf);
				else
					pb->pb_next = 0;
			} else
				pb->pb_next = vtophys(&q->q_dstpkt[j]);
			j++;
		}
#ifdef UBSEC_DEBUG
		printf("  buf[%d, %x]: %d@%x -> %x\n", 0,
		    vtophys(q->q_mcr),
		    q->q_mcr->mcr_opktbuf.pb_len,
		    q->q_mcr->mcr_opktbuf.pb_addr,
		    q->q_mcr->mcr_opktbuf.pb_next);
		for (i = 0; i < j - 1; i++) {
			printf("  buf[%d, %x]: %d@%x -> %x\n", i+1,
			    vtophys(&q->q_dstpkt[i]),
			    q->q_dstpkt[i].pb_len,
			    q->q_dstpkt[i].pb_addr,
			    q->q_dstpkt[i].pb_next);
		}
#endif
	}

	s = splnet();
	SIMPLEQ_INSERT_TAIL(&sc->sc_queue, q, q_next);
	sc->sc_nqueue++;
	ubsec_feed(sc);
	splx(s);
	return (0);

errout:
	if (q != NULL) {
		if (q->q_mcr)
			free(q->q_mcr, M_DEVBUF);
		if (q->q_src_m != q->q_dst_m)
			m_freem(q->q_dst_m);
		free(q, M_DEVBUF);
	}
	crp->crp_etype = err;
	crp->crp_callback(crp);
	return (0);
}

void
ubsec_callback(q)
	struct ubsec_q *q;
{
	struct cryptop *crp = (struct cryptop *)q->q_crp;
	struct cryptodesc *crd;

	if ((crp->crp_flags & CRYPTO_F_IMBUF) && (q->q_src_m != q->q_dst_m)) {
		m_freem(q->q_src_m);
		crp->crp_buf = (caddr_t)q->q_dst_m;
	}

	/* copy out IV for future use */
	if ((q->q_ctx.pc_flags & (UBS_PKTCTX_ENC_3DES | UBS_PKTCTX_INBOUND)) ==
	    UBS_PKTCTX_ENC_3DES) {
		for (crd = crp->crp_desc; crd; crd = crd->crd_next) {
			if (crd->crd_alg != CRYPTO_DES_CBC &&
			    crd->crd_alg != CRYPTO_3DES_CBC)
				continue;
			m_copydata((struct mbuf *)crp->crp_buf,
			    crd->crd_skip + crd->crd_len - 8, 8,
			    (caddr_t)q->q_sc->sc_sessions[q->q_sesn].ses_iv);
			break;
		}
	}

	for (crd = crp->crp_desc; crd; crd = crd->crd_next) {
		if (crd->crd_alg != CRYPTO_MD5_HMAC &&
		    crd->crd_alg != CRYPTO_SHA1_HMAC)
			continue;
		m_copyback((struct mbuf *)crp->crp_buf,
		    crd->crd_inject, 12, (caddr_t)q->q_macbuf);
		break;
	}

	/*
	 * note that q->q_mcr is not freed, because ubsec_intr() has to
	 * deal with possible sharing
	 */
	free(q, M_DEVBUF);
	crypto_done(crp);
}

void
ubsec_mcopy(srcm, dstm, hoffset, toffset)
	struct mbuf *srcm, *dstm;
	int hoffset, toffset;
{
	int i, j, dlen, slen;
	caddr_t dptr, sptr;

	j = 0;
	sptr = srcm->m_data;
	slen = srcm->m_len;
	dptr = dstm->m_data;
	dlen = dstm->m_len;

	while (1) {
		for (i = 0; i < min(slen, dlen); i++) {
			if (j < hoffset || j >= toffset)
				*dptr++ = *sptr++;
			slen--;
			dlen--;
			j++;
		}
		if (slen == 0) {
			srcm = srcm->m_next;
			if (srcm == NULL)
				return;
			sptr = srcm->m_data;
			slen = srcm->m_len;
		}
		if (dlen == 0) {
			dstm = dstm->m_next;
			if (dstm == NULL)
				return;
			dptr = dstm->m_data;
			dlen = dstm->m_len;
		}
	}
}

/*
 * feed the key generator, must be called at splnet() or higher.
 */
int
ubsec_feed2(sc)
	struct ubsec_softc *sc;
{
	struct ubsec_q2 *q;

	while (!SIMPLEQ_EMPTY(&sc->sc_queue2)) {
		if (READ_REG(sc, BS_STAT) & BS_STAT_MCR2_FULL)
			break;
		q = SIMPLEQ_FIRST(&sc->sc_queue2);
		WRITE_REG(sc, BS_MCR2, (u_int32_t)vtophys(q->q_mcr));
		SIMPLEQ_REMOVE_HEAD(&sc->sc_queue2, q, q_next);
		--sc->sc_nqueue2;
		SIMPLEQ_INSERT_TAIL(&sc->sc_qchip2, q, q_next);
	}
	return (0);
}

void
ubsec_callback2(q)
	struct ubsec_q2 *q;
{
	struct ubsec_softc *sc = q->q_sc;
	struct ubsec_keyctx *ctx = q->q_ctx;

	switch (ctx->ctx_op) {
	case UBS_CTXOP_RNGBYPASS: {
		struct ubsec_rng *rng = q->q_private;
		volatile u_int32_t *dat;
		int i;

		dat = rng->rng_buf;
		for (i = 0; i < UBS_RNGBUFSZ; i++, dat++)
			add_true_randomness(*dat);
		free(rng, M_DEVBUF);
		free(q, M_DEVBUF);
		timeout_add(&sc->sc_rngto, 1);
		break;
	}
	default:
		printf("%s: unknown ctx op: %x\n", sc->sc_dv.dv_xname,
		    ctx->ctx_op);
		break;
	}
}

void
ubsec_rng(vsc)
	void *vsc;
{
	struct ubsec_softc *sc = vsc;
	struct ubsec_q2 *q;
	struct ubsec_rng *rng;
	int s;

	s = splnet();
	if (sc->sc_nqueue2 >= UBS_MAX_NQUEUE) {
		splx(s);
		goto out;
	}
	splx(s);
	
	q = (struct ubsec_q2 *)malloc(sizeof(struct ubsec_q2), M_DEVBUF, M_NOWAIT);
	if (q == NULL)
		goto out;

	rng = (struct ubsec_rng *)malloc(sizeof(struct ubsec_rng),
	    M_DEVBUF, M_NOWAIT);
	if (rng == NULL) {
		free(q, M_DEVBUF);
		goto out;
	}

	q->q_mcr = &rng->rng_mcr;
	q->q_ctx = &rng->rng_ctx;
	q->q_private = rng;
	q->q_sc = sc;

	bzero(rng, sizeof(*rng));

	rng->rng_mcr.mcr_pkts = 1;
	rng->rng_mcr.mcr_cmdctxp = (u_int32_t)vtophys(&rng->rng_ctx);
	rng->rng_ctx.rbp_len = sizeof(struct ubsec_rngbypass_ctx);
	rng->rng_ctx.rbp_op = UBS_CTXOP_RNGBYPASS;
	rng->rng_mcr.mcr_opktbuf.pb_addr = (u_int32_t)vtophys(&rng->rng_buf[0]);
	rng->rng_mcr.mcr_opktbuf.pb_len = ((sizeof(u_int32_t) * UBS_RNGBUFSZ))
	    & UBS_PKTBUF_LEN;

	s = splnet();
	SIMPLEQ_INSERT_TAIL(&sc->sc_queue2, q, q_next);
	sc->sc_nqueue2++;
	ubsec_feed2(sc);
	splx(s);

	return;

out:
	/*
	 * Something weird happened, generate our own call back.
	 */
	timeout_add(&sc->sc_rngto, 1);
}
