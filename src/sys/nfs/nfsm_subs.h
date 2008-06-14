/*	$OpenBSD: nfsm_subs.h,v 1.30 2008/06/14 22:44:07 blambert Exp $	*/
/*	$NetBSD: nfsm_subs.h,v 1.10 1996/03/20 21:59:56 fvdl Exp $	*/

/*
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Rick Macklem at The University of Guelph.
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
 *	@(#)nfsm_subs.h	8.2 (Berkeley) 3/30/95
 */


#ifndef _NFS_NFSM_SUBS_H_
#define _NFS_NFSM_SUBS_H_


/*
 * These macros do strange and peculiar things to mbuf chains for
 * the assistance of the nfs code. To attempt to use them for any
 * other purpose will be dangerous. (they make weird assumptions)
 */

/*
 * First define what the actual subs. return
 */

#define	M_HASCL(m)	((m)->m_flags & M_EXT)
#define	NFSMADV(m, s)	(m)->m_data += (s)
#define	NFSMSIZ(m)	((M_HASCL(m)) ? (m)->m_ext.ext_size : \
				(((m)->m_flags & M_PKTHDR) ? MHLEN : MLEN))

/*
 * Now for the macros that do the simple stuff and call the functions
 * for the hard stuff.
 * These macros use several vars. declared in nfsm_reqhead and these
 * vars. must not be used elsewhere unless you are careful not to corrupt
 * them. The vars. starting with pN and tN (N=1,2,3,..) are temporaries
 * that may be used so long as the value is not expected to retained
 * after a macro.
 * I know, this is kind of dorkey, but it makes the actual op functions
 * fairly clean and deals with the mess caused by the xdr discriminating
 * unions.
 */

#define	nfsm_dissect(a, c, s) \
		{ t1 = mtod(md, caddr_t)+md->m_len-dpos; \
		if (t1 >= (s)) { \
			(a) = (c)(dpos); \
			dpos += (s); \
		} else if ((t1 = nfsm_disct(&md, &dpos, (s), t1, &cp2)) != 0){ \
			error = t1; \
			m_freem(mrep); \
			goto nfsmout; \
		} else { \
			(a) = (c)cp2; \
		} }

#define nfsm_fhtom(v, v3) \
	      { if (v3) { \
			nfsm_strtombuf(&mb, VTONFS(v)->n_fhp, \
			    VTONFS(v)->n_fhsize); \
		} else { \
			nfsm_buftombuf(&mb, VTONFS(v)->n_fhp, NFSX_V2FH); \
		} }

#define nfsm_srvfhtom(f, v3) \
		{ if (v3) { \
			nfsm_strtombuf(&mb, (f), NFSX_V3FH); \
		} else { \
			nfsm_buftombuf(&mb, (f), NFSX_V2FH); \
		} }

#define nfsm_srvpostop_fh(f) \
		{ tl = nfsm_build(&mb, 2 * NFSX_UNSIGNED + NFSX_V3FH); \
		*tl++ = nfs_true; \
		*tl++ = txdr_unsigned(NFSX_V3FH); \
		bcopy((caddr_t)(f), (caddr_t)tl, NFSX_V3FH); \
		}

#define nfsm_mtofh(d, v, v3, f) \
		{ struct nfsnode *ttnp; nfsfh_t *ttfhp; int ttfhsize; \
		if (v3) { \
			nfsm_dissect(tl, u_int32_t *, NFSX_UNSIGNED); \
			(f) = fxdr_unsigned(int, *tl); \
		} else \
			(f) = 1; \
		if (f) { \
			nfsm_getfh(ttfhp, ttfhsize, (v3)); \
			if ((t1 = nfs_nget((d)->v_mount, ttfhp, ttfhsize, \
				&ttnp)) != 0) { \
				error = t1; \
				m_freem(mrep); \
				goto nfsmout; \
			} \
			(v) = NFSTOV(ttnp); \
		} \
		if (v3) { \
			nfsm_dissect(tl, u_int32_t *, NFSX_UNSIGNED); \
			if (f) \
				(f) = fxdr_unsigned(int, *tl); \
			else if (fxdr_unsigned(int, *tl)) \
				nfsm_adv(NFSX_V3FATTR); \
		} \
		if (f) \
			nfsm_loadattr((v), (struct vattr *)0); \
		}

#define nfsm_getfh(f, s, v3) \
		{ if (v3) { \
			nfsm_dissect(tl, u_int32_t *, NFSX_UNSIGNED); \
			if (((s) = fxdr_unsigned(int, *tl)) <= 0 || \
				(s) > NFSX_V3FHMAX) { \
				m_freem(mrep); \
				error = EBADRPC; \
				goto nfsmout; \
			} \
		} else \
			(s) = NFSX_V2FH; \
		nfsm_dissect((f), nfsfh_t *, nfsm_rndup(s)); }

#define	nfsm_loadattr(v, a) \
		{ struct vnode *ttvp = (v); \
		if ((t1 = nfs_loadattrcache(&ttvp, &md, &dpos, (a))) != 0) { \
			error = t1; \
			m_freem(mrep); \
			goto nfsmout; \
		} \
		(v) = ttvp; }

#define	nfsm_postop_attr(v, f) \
		{ struct vnode *ttvp = (v); \
		nfsm_dissect(tl, u_int32_t *, NFSX_UNSIGNED); \
		if (((f) = fxdr_unsigned(int, *tl)) != 0) { \
			if ((t1 = nfs_loadattrcache(&ttvp, &md, &dpos, \
				(struct vattr *)0)) != 0) { \
				error = t1; \
				(f) = 0; \
				m_freem(mrep); \
				goto nfsmout; \
			} \
			(v) = ttvp; \
		} }

/* Used as (f) for nfsm_wcc_data() */
#define NFSV3_WCCRATTR	0
#define NFSV3_WCCCHK	1

#define	nfsm_wcc_data(v, f) \
		{ int ttattrf, ttretf = 0; \
		nfsm_dissect(tl, u_int32_t *, NFSX_UNSIGNED); \
		if (*tl == nfs_true) { \
			nfsm_dissect(tl, u_int32_t *, 6 * NFSX_UNSIGNED); \
			if (f) \
				ttretf = (VTONFS(v)->n_mtime == \
					fxdr_unsigned(u_int32_t, *(tl + 2))); \
		} \
		nfsm_postop_attr((v), ttattrf); \
		if (f) { \
			(f) = ttretf; \
		} else { \
			(f) = ttattrf; \
		} }

#define	nfsm_strsiz(s,m) \
		{ nfsm_dissect(tl,u_int32_t *,NFSX_UNSIGNED); \
		if (((s) = fxdr_unsigned(int32_t,*tl)) > (m)) { \
			m_freem(mrep); \
			error = EBADRPC; \
			goto nfsmout; \
		} }

#define	nfsm_srvstrsiz(s,m) \
		{ nfsm_dissect(tl,u_int32_t *,NFSX_UNSIGNED); \
		if (((s) = fxdr_unsigned(int32_t,*tl)) > (m) || (s) <= 0) { \
			error = EBADRPC; \
			nfsm_reply(0); \
		} }

#define	nfsm_srvnamesiz(s) \
		{ nfsm_dissect(tl,u_int32_t *,NFSX_UNSIGNED); \
		if (((s) = fxdr_unsigned(int32_t,*tl)) > NFS_MAXNAMLEN) \
			error = NFSERR_NAMETOL; \
		if ((s) <= 0) \
			error = EBADRPC; \
		if (error) \
			nfsm_reply(0); \
		}

#define nfsm_mtouio(p,s) \
		if ((s) > 0 && \
		   (t1 = nfsm_mbuftouio(&md,(p),(s),&dpos)) != 0) { \
			error = t1; \
			m_freem(mrep); \
			goto nfsmout; \
		}

#define nfsm_rndup(a)	(((a)+3)&(~0x3))

#define	nfsm_request(v, t, p, c)	\
		if ((error = nfs_request((v), mreq, (t), (p), \
		   (c), &mrep, &md, &dpos)) != 0) { \
			if (error & NFSERR_RETERR) \
				error &= ~NFSERR_RETERR; \
			else \
				goto nfsmout; \
		}

#define	nfsm_strtom(a,s,m) \
		if ((s) > (m)) { \
			m_freem(mreq); \
			error = ENAMETOOLONG; \
			goto nfsmout; \
		} \
		nfsm_strtombuf(&mb, (a), (s))

#define	nfsm_reply(s) \
		{ \
		nfsd->nd_repstat = error; \
		if (error && !(nfsd->nd_flag & ND_NFSV3)) \
		   (void) nfs_rephead(0, nfsd, slp, error, \
			mrq, &mb); \
		else \
		   (void) nfs_rephead((s), nfsd, slp, error, \
			mrq, &mb); \
		if (mrep != NULL) { \
			m_freem(mrep); \
			mrep = NULL; \
		} \
		mreq = *mrq; \
		if (error && (!(nfsd->nd_flag & ND_NFSV3) || \
			error == EBADRPC)) \
			return(0); \
		}

#define	nfsm_writereply(s, v3) \
		{ \
		nfsd->nd_repstat = error; \
		if (error && !(v3)) \
		   (void) nfs_rephead(0, nfsd, slp, error, \
			&mreq, &mb); \
		else \
		   (void) nfs_rephead((s), nfsd, slp, error, \
			&mreq, &mb); \
		}

#define	nfsm_adv(s) \
		{ t1 = mtod(md, caddr_t)+md->m_len-dpos; \
		if (t1 >= (s)) { \
			dpos += (s); \
		} else if ((t1 = nfs_adv(&md, &dpos, (s), t1)) != 0) { \
			error = t1; \
			m_freem(mrep); \
			goto nfsmout; \
		} }

#define nfsm_srvmtofh(f) \
		{ if (nfsd->nd_flag & ND_NFSV3) { \
			nfsm_dissect(tl, u_int32_t *, NFSX_UNSIGNED); \
			if (fxdr_unsigned(int, *tl) != NFSX_V3FH) { \
				error = EBADRPC; \
				nfsm_reply(0); \
			} \
		} \
		nfsm_dissect(tl, u_int32_t *, NFSX_V3FH); \
		bcopy((caddr_t)tl, (caddr_t)(f), NFSX_V3FH); \
		if ((nfsd->nd_flag & ND_NFSV3) == 0) \
			nfsm_adv(NFSX_V2FH - NFSX_V3FH); \
		}

#define nfsm_srvsattr(a) \
		{ nfsm_dissect(tl, u_int32_t *, NFSX_UNSIGNED); \
		if (*tl == nfs_true) { \
			nfsm_dissect(tl, u_int32_t *, NFSX_UNSIGNED); \
			(a)->va_mode = nfstov_mode(*tl); \
		} \
		nfsm_dissect(tl, u_int32_t *, NFSX_UNSIGNED); \
		if (*tl == nfs_true) { \
			nfsm_dissect(tl, u_int32_t *, NFSX_UNSIGNED); \
			(a)->va_uid = fxdr_unsigned(uid_t, *tl); \
		} \
		nfsm_dissect(tl, u_int32_t *, NFSX_UNSIGNED); \
		if (*tl == nfs_true) { \
			nfsm_dissect(tl, u_int32_t *, NFSX_UNSIGNED); \
			(a)->va_gid = fxdr_unsigned(gid_t, *tl); \
		} \
		nfsm_dissect(tl, u_int32_t *, NFSX_UNSIGNED); \
		if (*tl == nfs_true) { \
			nfsm_dissect(tl, u_int32_t *, 2 * NFSX_UNSIGNED); \
			(a)->va_size = fxdr_hyper(tl); \
		} \
		nfsm_dissect(tl, u_int32_t *, NFSX_UNSIGNED); \
		switch (fxdr_unsigned(int, *tl)) { \
		case NFSV3SATTRTIME_TOCLIENT: \
			(a)->va_vaflags &= ~VA_UTIMES_NULL; \
			nfsm_dissect(tl, u_int32_t *, 2 * NFSX_UNSIGNED); \
			fxdr_nfsv3time(tl, &(a)->va_atime); \
			break; \
		case NFSV3SATTRTIME_TOSERVER: \
			getnanotime(&(a)->va_atime); \
			break; \
		}; \
		nfsm_dissect(tl, u_int32_t *, NFSX_UNSIGNED); \
		switch (fxdr_unsigned(int, *tl)) { \
		case NFSV3SATTRTIME_TOCLIENT: \
			(a)->va_vaflags &= ~VA_UTIMES_NULL; \
			nfsm_dissect(tl, u_int32_t *, 2 * NFSX_UNSIGNED); \
			fxdr_nfsv3time(tl, &(a)->va_mtime); \
			break; \
		case NFSV3SATTRTIME_TOSERVER: \
			getnanotime(&(a)->va_mtime); \
			break; \
		}; }

#endif
