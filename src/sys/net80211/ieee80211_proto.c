/*	$OpenBSD: ieee80211_proto.c,v 1.27 2008/07/26 12:56:06 damien Exp $	*/
/*	$NetBSD: ieee80211_proto.c,v 1.8 2004/04/30 23:58:20 dyoung Exp $	*/

/*-
 * Copyright (c) 2001 Atsushi Onoe
 * Copyright (c) 2002, 2003 Sam Leffler, Errno Consulting
 * Copyright (c) 2008 Damien Bergamini
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
 */

/*
 * IEEE 802.11 protocol support.
 */

#include "bpfilter.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <sys/endian.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/sysctl.h>

#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_arp.h>
#include <net/if_llc.h>

#if NBPFILTER > 0
#include <net/bpf.h>
#endif

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_priv.h>

#include <dev/rndvar.h>

const char * const ieee80211_mgt_subtype_name[] = {
	"assoc_req",	"assoc_resp",	"reassoc_req",	"reassoc_resp",
	"probe_req",	"probe_resp",	"reserved#6",	"reserved#7",
	"beacon",	"atim",		"disassoc",	"auth",
	"deauth",	"action",	"action_noack",	"reserved#15"
};
const char * const ieee80211_state_name[IEEE80211_S_MAX] = {
	"INIT",		/* IEEE80211_S_INIT */
	"SCAN",		/* IEEE80211_S_SCAN */
	"AUTH",		/* IEEE80211_S_AUTH */
	"ASSOC",	/* IEEE80211_S_ASSOC */
	"RUN"		/* IEEE80211_S_RUN */
};
const char * const ieee80211_phymode_name[] = {
	"auto",		/* IEEE80211_MODE_AUTO */
	"11a",		/* IEEE80211_MODE_11A */
	"11b",		/* IEEE80211_MODE_11B */
	"11g",		/* IEEE80211_MODE_11G */
	"fh",		/* IEEE80211_MODE_FH */
	"turbo",	/* IEEE80211_MODE_TURBO */
};

int ieee80211_newstate(struct ieee80211com *, enum ieee80211_state, int);
void ieee80211_set_link_state(struct ieee80211com *, int);

void
ieee80211_proto_attach(struct ifnet *ifp)
{
	struct ieee80211com *ic = (void *)ifp;

	ifp->if_hdrlen = sizeof(struct ieee80211_frame);

#ifdef notdef
	ic->ic_rtsthreshold = IEEE80211_RTS_DEFAULT;
#else
	ic->ic_rtsthreshold = IEEE80211_RTS_MAX;
#endif
	ic->ic_fragthreshold = 2346;		/* XXX not used yet */
	ic->ic_fixed_rate = -1;			/* no fixed rate */
	ic->ic_protmode = IEEE80211_PROT_CTSONLY;

	/* protocol state change handler */
	ic->ic_newstate = ieee80211_newstate;

	/* initialize management frame handlers */
	ic->ic_recv_mgmt = ieee80211_recv_mgmt;
	ic->ic_send_mgmt = ieee80211_send_mgmt;

	/* initialize EAPOL frame handler */
	ic->ic_recv_eapol = ieee80211_recv_eapol;
}

void
ieee80211_proto_detach(struct ifnet *ifp)
{
	struct ieee80211com *ic = (void *)ifp;

	IF_PURGE(&ic->ic_mgtq);
	IF_PURGE(&ic->ic_pwrsaveq);
}

void
ieee80211_print_essid(const u_int8_t *essid, int len)
{
	int i;
	const u_int8_t *p;

	if (len > IEEE80211_NWID_LEN)
		len = IEEE80211_NWID_LEN;
	/* determine printable or not */
	for (i = 0, p = essid; i < len; i++, p++) {
		if (*p < ' ' || *p > 0x7e)
			break;
	}
	if (i == len) {
		printf("\"");
		for (i = 0, p = essid; i < len; i++, p++)
			printf("%c", *p);
		printf("\"");
	} else {
		printf("0x");
		for (i = 0, p = essid; i < len; i++, p++)
			printf("%02x", *p);
	}
}

void
ieee80211_dump_pkt(const u_int8_t *buf, int len, int rate, int rssi)
{
	struct ieee80211_frame *wh;
	int i;

	wh = (struct ieee80211_frame *)buf;
	switch (wh->i_fc[1] & IEEE80211_FC1_DIR_MASK) {
	case IEEE80211_FC1_DIR_NODS:
		printf("NODS %s", ether_sprintf(wh->i_addr2));
		printf("->%s", ether_sprintf(wh->i_addr1));
		printf("(%s)", ether_sprintf(wh->i_addr3));
		break;
	case IEEE80211_FC1_DIR_TODS:
		printf("TODS %s", ether_sprintf(wh->i_addr2));
		printf("->%s", ether_sprintf(wh->i_addr3));
		printf("(%s)", ether_sprintf(wh->i_addr1));
		break;
	case IEEE80211_FC1_DIR_FROMDS:
		printf("FRDS %s", ether_sprintf(wh->i_addr3));
		printf("->%s", ether_sprintf(wh->i_addr1));
		printf("(%s)", ether_sprintf(wh->i_addr2));
		break;
	case IEEE80211_FC1_DIR_DSTODS:
		printf("DSDS %s", ether_sprintf((u_int8_t *)&wh[1]));
		printf("->%s", ether_sprintf(wh->i_addr3));
		printf("(%s", ether_sprintf(wh->i_addr2));
		printf("->%s)", ether_sprintf(wh->i_addr1));
		break;
	}
	switch (wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK) {
	case IEEE80211_FC0_TYPE_DATA:
		printf(" data");
		break;
	case IEEE80211_FC0_TYPE_MGT:
		printf(" %s", ieee80211_mgt_subtype_name[
		    (wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK)
		    >> IEEE80211_FC0_SUBTYPE_SHIFT]);
		break;
	default:
		printf(" type#%d", wh->i_fc[0] & IEEE80211_FC0_TYPE_MASK);
		break;
	}
	if (wh->i_fc[1] & IEEE80211_FC1_WEP)
		printf(" WEP");
	if (rate >= 0)
		printf(" %d%sM", rate / 2, (rate & 1) ? ".5" : "");
	if (rssi >= 0)
		printf(" +%d", rssi);
	printf("\n");
	if (len > 0) {
		for (i = 0; i < len; i++) {
			if ((i & 1) == 0)
				printf(" ");
			printf("%02x", buf[i]);
		}
		printf("\n");
	}
}

int
ieee80211_fix_rate(struct ieee80211com *ic, struct ieee80211_node *ni,
    int flags)
{
#define	RV(v)	((v) & IEEE80211_RATE_VAL)
	int i, j, ignore, error;
	int okrate, badrate, fixedrate;
	const struct ieee80211_rateset *srs;
	struct ieee80211_rateset *nrs;
	u_int8_t r;

	/*
	 * If the fixed rate check was requested but no fixed rate has been
	 * defined then just remove the check.
	 */
	if ((flags & IEEE80211_F_DOFRATE) && ic->ic_fixed_rate == -1)
		flags &= ~IEEE80211_F_DOFRATE;

	error = 0;
	okrate = badrate = fixedrate = 0;
	srs = &ic->ic_sup_rates[ieee80211_chan2mode(ic, ni->ni_chan)];
	nrs = &ni->ni_rates;
	for (i = 0; i < nrs->rs_nrates; ) {
		ignore = 0;
		if (flags & IEEE80211_F_DOSORT) {
			/*
			 * Sort rates.
			 */
			for (j = i + 1; j < nrs->rs_nrates; j++) {
				if (RV(nrs->rs_rates[i]) >
				    RV(nrs->rs_rates[j])) {
					r = nrs->rs_rates[i];
					nrs->rs_rates[i] = nrs->rs_rates[j];
					nrs->rs_rates[j] = r;
				}
			}
		}
		r = nrs->rs_rates[i] & IEEE80211_RATE_VAL;
		badrate = r;
		if (flags & IEEE80211_F_DOFRATE) {
			/*
			 * Check fixed rate is included.
			 */
			if (r == RV(srs->rs_rates[ic->ic_fixed_rate]))
				fixedrate = r;
		}
		if (flags & IEEE80211_F_DONEGO) {
			/*
			 * Check against supported rates.
			 */
			for (j = 0; j < srs->rs_nrates; j++) {
				if (r == RV(srs->rs_rates[j])) {
					/*
					 * Overwrite with the supported rate
					 * value so any basic rate bit is set.
					 * This insures that response we send
					 * to stations have the necessary basic
					 * rate bit set.
					 */
					nrs->rs_rates[i] = srs->rs_rates[j];
					break;
				}
			}
			if (j == srs->rs_nrates) {
				/*
				 * A rate in the node's rate set is not
				 * supported.  If this is a basic rate and we
				 * are operating as an AP then this is an error.
				 * Otherwise we just discard/ignore the rate.
				 * Note that this is important for 11b stations
				 * when they want to associate with an 11g AP.
				 */
				if (ic->ic_opmode == IEEE80211_M_HOSTAP &&
				    (nrs->rs_rates[i] & IEEE80211_RATE_BASIC))
					error++;
				ignore++;
			}
		}
		if (flags & IEEE80211_F_DODEL) {
			/*
			 * Delete unacceptable rates.
			 */
			if (ignore) {
				nrs->rs_nrates--;
				for (j = i; j < nrs->rs_nrates; j++)
					nrs->rs_rates[j] = nrs->rs_rates[j + 1];
				nrs->rs_rates[j] = 0;
				continue;
			}
		}
		if (!ignore)
			okrate = nrs->rs_rates[i];
		i++;
	}
	if (okrate == 0 || error != 0 ||
	    ((flags & IEEE80211_F_DOFRATE) && fixedrate == 0))
		return badrate | IEEE80211_RATE_BASIC;
	else
		return RV(okrate);
#undef RV
}

/*
 * Reset 11g-related state.
 */
void
ieee80211_reset_erp(struct ieee80211com *ic)
{
	ic->ic_flags &= ~IEEE80211_F_USEPROT;
	ic->ic_nonerpsta = 0;
	ic->ic_longslotsta = 0;

	/*
	 * Enable short slot time iff:
	 * - we're operating in 802.11a or
	 * - we're operating in 802.11g and we're not in IBSS mode and
	 *   the device supports short slot time
	 */
	ieee80211_set_shortslottime(ic,
	    ic->ic_curmode == IEEE80211_MODE_11A ||
	    (ic->ic_curmode == IEEE80211_MODE_11G &&
	     ic->ic_opmode == IEEE80211_M_HOSTAP &&
	     (ic->ic_caps & IEEE80211_C_SHSLOT)));

	if (ic->ic_curmode == IEEE80211_MODE_11A ||
	    (ic->ic_caps & IEEE80211_C_SHPREAMBLE))
		ic->ic_flags |= IEEE80211_F_SHPREAMBLE;
	else
		ic->ic_flags &= ~IEEE80211_F_SHPREAMBLE;
}

/*
 * Set the short slot time state and notify the driver.
 */
void
ieee80211_set_shortslottime(struct ieee80211com *ic, int on)
{
	if (on)
		ic->ic_flags |= IEEE80211_F_SHSLOT;
	else
		ic->ic_flags &= ~IEEE80211_F_SHSLOT;

	/* notify the driver */
	if (ic->ic_updateslot != NULL)
		ic->ic_updateslot(ic);
}

/*
 * Initiate a group key handshake with a node.
 */
static void
ieee80211_node_gtk_rekey(void *arg, struct ieee80211_node *ni)
{
	struct ieee80211com *ic = arg;

	if (ni->ni_state != IEEE80211_STA_ASSOC ||
	    ni->ni_rsn_gstate != RSNA_IDLE)
		return;

	/* initiate a group key handshake with STA */
	if (ieee80211_send_group_msg1(ic, ni) == 0) {
		ni->ni_flags |= IEEE80211_NODE_REKEY;
		ic->ic_rsn_keydonesta++;
	}
}

/*
 * This function is called in HostAP mode when the group key needs to be
 * changed.
 */
void
ieee80211_setkeys(struct ieee80211com *ic)
{
	u_int8_t gtk[IEEE80211_PMK_LEN];
	u_int8_t kid;

	/* Swap(GM, GN) */
	kid = (ic->ic_def_txkey == 1) ? 2 : 1;

	arc4random_buf(gtk, sizeof(gtk));
	ieee80211_map_gtk(gtk, ic->ic_bss->ni_rsngroupcipher, kid, 1, 0,
	    &ic->ic_nw_keys[kid]);

	ic->ic_rsn_keydonesta = 0;
	ieee80211_iterate_nodes(ic, ieee80211_node_gtk_rekey, ic);
}

/*
 * The group key handshake has been completed with all associated stations.
 */
void
ieee80211_setkeysdone(struct ieee80211com *ic)
{
	u_int8_t kid;

	/* install GTK */
	kid = (ic->ic_def_txkey == 1) ? 2 : 1;
	if ((*ic->ic_set_key)(ic, ic->ic_bss, &ic->ic_nw_keys[kid]) == 0)
		ic->ic_def_txkey = kid;
}

/*
 * Group key lifetime has expired, update it.
 */
void
ieee80211_gtk_rekey_timeout(void *arg)
{
	struct ieee80211com *ic = arg;
	int s;

	s = splnet();
	ieee80211_setkeys(ic);
	splx(s);

	/* re-schedule a GTK rekeying after 3600s */
	timeout_add(&ic->ic_rsn_timeout, 3600 * hz);
}

void
ieee80211_auth_open(struct ieee80211com *ic, const struct ieee80211_frame *wh,
    struct ieee80211_node *ni, struct ieee80211_rxinfo *rxi, u_int16_t seq,
    u_int16_t status)
{
	struct ifnet *ifp = &ic->ic_if;
	switch (ic->ic_opmode) {
	case IEEE80211_M_IBSS:
		if (ic->ic_state != IEEE80211_S_RUN ||
		    seq != IEEE80211_AUTH_OPEN_REQUEST) {
			IEEE80211_DPRINTF(("%s: discard auth from %s; "
			    "state %u, seq %u\n", __func__,
			    ether_sprintf((u_int8_t *)wh->i_addr2),
			    ic->ic_state, seq));
			ic->ic_stats.is_rx_bad_auth++;
			return;
		}
		ieee80211_new_state(ic, IEEE80211_S_AUTH,
		    wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK);
		break;

	case IEEE80211_M_AHDEMO:
		/* should not come here */
		break;

	case IEEE80211_M_HOSTAP:
		if (ic->ic_state != IEEE80211_S_RUN ||
		    seq != IEEE80211_AUTH_OPEN_REQUEST) {
			IEEE80211_DPRINTF(("%s: discard auth from %s; "
			    "state %u, seq %u\n", __func__,
			    ether_sprintf((u_int8_t *)wh->i_addr2),
			    ic->ic_state, seq));
			ic->ic_stats.is_rx_bad_auth++;
			return;
		}
		if (ni == ic->ic_bss) {
			ni = ieee80211_alloc_node(ic, wh->i_addr2);
			if (ni == NULL) {
				ic->ic_stats.is_rx_nodealloc++;
				return;
			}
			IEEE80211_ADDR_COPY(ni->ni_bssid, ic->ic_bss->ni_bssid);
			ni->ni_rssi = rxi->rxi_rssi;
			ni->ni_rstamp = rxi->rxi_tstamp;
			ni->ni_chan = ic->ic_bss->ni_chan;
		}
		IEEE80211_SEND_MGMT(ic, ni,
			IEEE80211_FC0_SUBTYPE_AUTH, seq + 1);
		if (ifp->if_flags & IFF_DEBUG)
			printf("%s: station %s %s authenticated (open)\n",
			    ifp->if_xname,
			    ether_sprintf((u_int8_t *)ni->ni_macaddr),
			    ni->ni_state != IEEE80211_STA_CACHE ?
			    "newly" : "already");
		ieee80211_node_newstate(ni, IEEE80211_STA_AUTH);
		break;

	case IEEE80211_M_STA:
		if (ic->ic_state != IEEE80211_S_AUTH ||
		    seq != IEEE80211_AUTH_OPEN_RESPONSE) {
			ic->ic_stats.is_rx_bad_auth++;
			IEEE80211_DPRINTF(("%s: discard auth from %s; "
			    "state %u, seq %u\n", __func__,
			    ether_sprintf((u_int8_t *)wh->i_addr2),
			    ic->ic_state, seq));
			return;
		}
		if (ic->ic_flags & IEEE80211_F_RSNON) {
			/* XXX not here! */
			ic->ic_bss->ni_port_valid = 0;
			ic->ic_bss->ni_replaycnt_ok = 0;
			(*ic->ic_delete_key)(ic, ic->ic_bss,
			    &ic->ic_bss->ni_pairwise_key);
		}
		if (status != 0) {
			if (ifp->if_flags & IFF_DEBUG)
				printf("%s: open authentication failed "
				    "(reason %d) for %s\n", ifp->if_xname,
				    status,
				    ether_sprintf((u_int8_t *)wh->i_addr3));
			if (ni != ic->ic_bss)
				ni->ni_fails++;
			ic->ic_stats.is_rx_auth_fail++;
			return;
		}
		ieee80211_new_state(ic, IEEE80211_S_ASSOC,
		    wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK);
		break;
	case IEEE80211_M_MONITOR:
		break;
	}
}

int
ieee80211_newstate(struct ieee80211com *ic, enum ieee80211_state nstate,
    int mgt)
{
	struct ifnet *ifp = &ic->ic_if;
	struct ieee80211_node *ni;
	enum ieee80211_state ostate;
	u_int rate;
	int s;

	ostate = ic->ic_state;
	IEEE80211_DPRINTF(("%s: %s -> %s\n", __func__,
	    ieee80211_state_name[ostate], ieee80211_state_name[nstate]));
	ic->ic_state = nstate;			/* state transition */
	ni = ic->ic_bss;			/* NB: no reference held */
	if (ostate == IEEE80211_S_RUN)
		ieee80211_set_link_state(ic, LINK_STATE_DOWN);
	switch (nstate) {
	case IEEE80211_S_INIT:
		switch (ostate) {
		case IEEE80211_S_INIT:
			break;
		case IEEE80211_S_RUN:
			switch (ic->ic_opmode) {
			case IEEE80211_M_STA:
				IEEE80211_SEND_MGMT(ic, ni,
				    IEEE80211_FC0_SUBTYPE_DISASSOC,
				    IEEE80211_REASON_ASSOC_LEAVE);
				break;
			case IEEE80211_M_HOSTAP:
				s = splnet();
				RB_FOREACH(ni, ieee80211_tree, &ic->ic_tree) {
					if (ni->ni_associd == 0)
						continue;
					IEEE80211_SEND_MGMT(ic, ni,
					    IEEE80211_FC0_SUBTYPE_DISASSOC,
					    IEEE80211_REASON_ASSOC_LEAVE);
				}
				splx(s);
				break;
			default:
				break;
			}
			/* FALLTHROUGH */
		case IEEE80211_S_ASSOC:
			switch (ic->ic_opmode) {
			case IEEE80211_M_STA:
				IEEE80211_SEND_MGMT(ic, ni,
				    IEEE80211_FC0_SUBTYPE_DEAUTH,
				    IEEE80211_REASON_AUTH_LEAVE);
				break;
			case IEEE80211_M_HOSTAP:
				s = splnet();
				RB_FOREACH(ni, ieee80211_tree, &ic->ic_tree) {
					IEEE80211_SEND_MGMT(ic, ni,
					    IEEE80211_FC0_SUBTYPE_DEAUTH,
					    IEEE80211_REASON_AUTH_LEAVE);
				}
				splx(s);
				break;
			default:
				break;
			}
			/* FALLTHROUGH */
		case IEEE80211_S_AUTH:
		case IEEE80211_S_SCAN:
			if (ic->ic_opmode == IEEE80211_M_HOSTAP)
				timeout_del(&ic->ic_rsn_timeout);
			ic->ic_mgt_timer = 0;
			IF_PURGE(&ic->ic_mgtq);
			IF_PURGE(&ic->ic_pwrsaveq);
			ieee80211_free_allnodes(ic);
			break;
		}
		break;
	case IEEE80211_S_SCAN:
		ic->ic_flags &= ~IEEE80211_F_SIBSS;
		/* initialize bss for probe request */
		IEEE80211_ADDR_COPY(ni->ni_macaddr, etherbroadcastaddr);
		IEEE80211_ADDR_COPY(ni->ni_bssid, etherbroadcastaddr);
		ni->ni_rates = ic->ic_sup_rates[
			ieee80211_chan2mode(ic, ni->ni_chan)];
		ni->ni_associd = 0;
		ni->ni_rstamp = 0;
		switch (ostate) {
		case IEEE80211_S_INIT:
			if (ic->ic_opmode == IEEE80211_M_HOSTAP &&
			    ic->ic_des_chan != IEEE80211_CHAN_ANYC) {
				/*
				 * AP operation and we already have a channel;
				 * bypass the scan and startup immediately.
				 */
				ieee80211_create_ibss(ic, ic->ic_des_chan);
			} else {
				ieee80211_begin_scan(ifp);
			}
			break;
		case IEEE80211_S_SCAN:
			/* scan next */
			if (ic->ic_flags & IEEE80211_F_ASCAN) {
				IEEE80211_SEND_MGMT(ic, ni,
				    IEEE80211_FC0_SUBTYPE_PROBE_REQ, 0);
			}
			break;
		case IEEE80211_S_RUN:
			/* beacon miss */
			if (ifp->if_flags & IFF_DEBUG) {
				/* XXX bssid clobbered above */
				printf("%s: no recent beacons from %s;"
				    " rescanning\n", ifp->if_xname,
				    ether_sprintf(ic->ic_bss->ni_bssid));
			}
			ieee80211_free_allnodes(ic);
			/* FALLTHROUGH */
		case IEEE80211_S_AUTH:
		case IEEE80211_S_ASSOC:
			/* timeout restart scan */
			ni = ieee80211_find_node(ic, ic->ic_bss->ni_macaddr);
			if (ni != NULL)
				ni->ni_fails++;
			ieee80211_begin_scan(ifp);
			break;
		}
		break;
	case IEEE80211_S_AUTH:
		switch (ostate) {
		case IEEE80211_S_INIT:
			IEEE80211_DPRINTF(("%s: invalid transition\n",
				__func__));
			break;
		case IEEE80211_S_SCAN:
			IEEE80211_SEND_MGMT(ic, ni,
			    IEEE80211_FC0_SUBTYPE_AUTH, 1);
			break;
		case IEEE80211_S_AUTH:
		case IEEE80211_S_ASSOC:
			switch (mgt) {
			case IEEE80211_FC0_SUBTYPE_AUTH:
				/* ??? */
				IEEE80211_SEND_MGMT(ic, ni,
				    IEEE80211_FC0_SUBTYPE_AUTH, 2);
				break;
			case IEEE80211_FC0_SUBTYPE_DEAUTH:
				/* ignore and retry scan on timeout */
				break;
			}
			break;
		case IEEE80211_S_RUN:
			switch (mgt) {
			case IEEE80211_FC0_SUBTYPE_AUTH:
				IEEE80211_SEND_MGMT(ic, ni,
				    IEEE80211_FC0_SUBTYPE_AUTH, 2);
				ic->ic_state = ostate;	/* stay RUN */
				break;
			case IEEE80211_FC0_SUBTYPE_DEAUTH:
				/* try to reauth */
				IEEE80211_SEND_MGMT(ic, ni,
				    IEEE80211_FC0_SUBTYPE_AUTH, 1);
				break;
			}
			break;
		}
		break;
	case IEEE80211_S_ASSOC:
		switch (ostate) {
		case IEEE80211_S_INIT:
		case IEEE80211_S_SCAN:
		case IEEE80211_S_ASSOC:
			IEEE80211_DPRINTF(("%s: invalid transition\n",
				__func__));
			break;
		case IEEE80211_S_AUTH:
			IEEE80211_SEND_MGMT(ic, ni,
			    IEEE80211_FC0_SUBTYPE_ASSOC_REQ, 0);
			break;
		case IEEE80211_S_RUN:
			IEEE80211_SEND_MGMT(ic, ni,
			    IEEE80211_FC0_SUBTYPE_ASSOC_REQ, 1);
			break;
		}
		break;
	case IEEE80211_S_RUN:
		ieee80211_set_link_state(ic, LINK_STATE_UP);
		switch (ostate) {
		case IEEE80211_S_INIT:
		case IEEE80211_S_AUTH:
		case IEEE80211_S_RUN:
			IEEE80211_DPRINTF(("%s: invalid transition\n",
				__func__));
			break;
		case IEEE80211_S_SCAN:		/* adhoc/hostap mode */
		case IEEE80211_S_ASSOC:		/* infra mode */
			if (ni->ni_txrate >= ni->ni_rates.rs_nrates)
				panic("%s: bogus xmit rate %u setup\n",
				    __func__, ni->ni_txrate);
			if (ifp->if_flags & IFF_DEBUG) {
				printf("%s: %s with %s ssid ",
				    ifp->if_xname,
				    ic->ic_opmode == IEEE80211_M_STA ?
				    "associated" : "synchronized",
				    ether_sprintf(ni->ni_bssid));
				ieee80211_print_essid(ic->ic_bss->ni_essid,
				    ni->ni_esslen);
				rate = ni->ni_rates.rs_rates[ni->ni_txrate] &
				    IEEE80211_RATE_VAL;
				printf(" channel %d start %u%sMb",
				    ieee80211_chan2ieee(ic, ni->ni_chan),
				    rate / 2, (rate & 1) ? ".5" : "");
				printf(" %s preamble %s slot time%s\n",
				    (ic->ic_flags & IEEE80211_F_SHPREAMBLE) ?
					"short" : "long",
				    (ic->ic_flags & IEEE80211_F_SHSLOT) ?
					"short" : "long",
				    (ic->ic_flags & IEEE80211_F_USEPROT) ?
					" protection enabled" : "");
			}
			ic->ic_mgt_timer = 0;
			(*ifp->if_start)(ifp);
			break;
		}
		break;
	}
	return 0;
}

void
ieee80211_set_link_state(struct ieee80211com *ic, int nstate)
{
	struct ifnet *ifp = &ic->ic_if;

	switch (ic->ic_opmode) {
	case IEEE80211_M_IBSS:
	case IEEE80211_M_HOSTAP:
		nstate = LINK_STATE_UNKNOWN;
		break;
	case IEEE80211_M_MONITOR:
		nstate = LINK_STATE_DOWN;
		break;
	default:
		break;
	}
	if (nstate != ifp->if_link_state) {
		ifp->if_link_state = nstate;
		if_link_state_change(ifp);
	}
}
