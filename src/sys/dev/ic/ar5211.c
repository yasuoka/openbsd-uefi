/*	$OpenBSD: ar5211.c,v 1.9 2005/04/08 22:02:49 reyk Exp $	*/

/*
 * Copyright (c) 2004, 2005 Reyk Floeter <reyk@vantronix.net>
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

/*
 * HAL interface for the Atheros AR5001 Wireless LAN chipset
 * (AR5211 + AR5111).
 */

#include <dev/ic/ar5xxx.h>
#include <dev/ic/ar5211reg.h>
#include <dev/ic/ar5211var.h>

HAL_BOOL	 ar5k_ar5211_nic_reset(struct ath_hal *, u_int32_t);
HAL_BOOL	 ar5k_ar5211_nic_wakeup(struct ath_hal *, u_int16_t);
u_int16_t	 ar5k_ar5211_radio_revision(struct ath_hal *, HAL_CHIP);
const void	 ar5k_ar5211_fill(struct ath_hal *);

/*
 * Initial register setting for the AR5211
 */
static const struct ar5k_ini ar5211_ini[] =
    AR5K_AR5211_INI;
static const struct ar5k_ar5211_ini_mode ar5211_mode[] =
    AR5K_AR5211_INI_MODE;

AR5K_HAL_FUNCTIONS(extern, ar5k_ar5211,);

const void
ar5k_ar5211_fill(hal)
	struct ath_hal *hal;
{
	hal->ah_magic = AR5K_AR5211_MAGIC;

	/*
	 * Init/Exit functions
	 */
	AR5K_HAL_FUNCTION(hal, ar5211, getRateTable);
	AR5K_HAL_FUNCTION(hal, ar5211, detach);

	/*
	 * Reset functions
	 */
	AR5K_HAL_FUNCTION(hal, ar5211, reset);
	AR5K_HAL_FUNCTION(hal, ar5211, setPCUConfig);
	AR5K_HAL_FUNCTION(hal, ar5211, perCalibration);

	/*
	 * TX functions
	 */
	AR5K_HAL_FUNCTION(hal, ar5211, updateTxTrigLevel);
	AR5K_HAL_FUNCTION(hal, ar5211, setupTxQueue);
	AR5K_HAL_FUNCTION(hal, ar5211, setTxQueueProps);
	AR5K_HAL_FUNCTION(hal, ar5211, releaseTxQueue);
	AR5K_HAL_FUNCTION(hal, ar5211, resetTxQueue);
	AR5K_HAL_FUNCTION(hal, ar5211, getTxDP);
	AR5K_HAL_FUNCTION(hal, ar5211, setTxDP);
	AR5K_HAL_FUNCTION(hal, ar5211, startTxDma);
	AR5K_HAL_FUNCTION(hal, ar5211, stopTxDma);
	AR5K_HAL_FUNCTION(hal, ar5211, setupTxDesc);
	AR5K_HAL_FUNCTION(hal, ar5211, setupXTxDesc);
	AR5K_HAL_FUNCTION(hal, ar5211, fillTxDesc);
	AR5K_HAL_FUNCTION(hal, ar5211, procTxDesc);
	AR5K_HAL_FUNCTION(hal, ar5211, hasVEOL);

	/*
	 * RX functions
	 */
	AR5K_HAL_FUNCTION(hal, ar5211, getRxDP);
	AR5K_HAL_FUNCTION(hal, ar5211, setRxDP);
	AR5K_HAL_FUNCTION(hal, ar5211, enableReceive);
	AR5K_HAL_FUNCTION(hal, ar5211, stopDmaReceive);
	AR5K_HAL_FUNCTION(hal, ar5211, startPcuReceive);
	AR5K_HAL_FUNCTION(hal, ar5211, stopPcuReceive);
	AR5K_HAL_FUNCTION(hal, ar5211, setMulticastFilter);
	AR5K_HAL_FUNCTION(hal, ar5211, setMulticastFilterIndex);
	AR5K_HAL_FUNCTION(hal, ar5211, clrMulticastFilterIndex);
	AR5K_HAL_FUNCTION(hal, ar5211, getRxFilter);
	AR5K_HAL_FUNCTION(hal, ar5211, setRxFilter);
	AR5K_HAL_FUNCTION(hal, ar5211, setupRxDesc);
	AR5K_HAL_FUNCTION(hal, ar5211, procRxDesc);
	AR5K_HAL_FUNCTION(hal, ar5211, rxMonitor);

	/*
	 * Misc functions
	 */
	AR5K_HAL_FUNCTION(hal, ar5211, dumpState);
	AR5K_HAL_FUNCTION(hal, ar5211, getDiagState);
	AR5K_HAL_FUNCTION(hal, ar5211, getMacAddress);
	AR5K_HAL_FUNCTION(hal, ar5211, setMacAddress);
	AR5K_HAL_FUNCTION(hal, ar5211, setRegulatoryDomain);
	AR5K_HAL_FUNCTION(hal, ar5211, setLedState);
	AR5K_HAL_FUNCTION(hal, ar5211, writeAssocid);
	AR5K_HAL_FUNCTION(hal, ar5211, gpioCfgInput);
	AR5K_HAL_FUNCTION(hal, ar5211, gpioCfgOutput);
	AR5K_HAL_FUNCTION(hal, ar5211, gpioGet);
	AR5K_HAL_FUNCTION(hal, ar5211, gpioSet);
	AR5K_HAL_FUNCTION(hal, ar5211, gpioSetIntr);
	AR5K_HAL_FUNCTION(hal, ar5211, getTsf32);
	AR5K_HAL_FUNCTION(hal, ar5211, getTsf64);
	AR5K_HAL_FUNCTION(hal, ar5211, resetTsf);
	AR5K_HAL_FUNCTION(hal, ar5211, getRegDomain);
	AR5K_HAL_FUNCTION(hal, ar5211, detectCardPresent);
	AR5K_HAL_FUNCTION(hal, ar5211, updateMibCounters);
	AR5K_HAL_FUNCTION(hal, ar5211, getRfGain);
	AR5K_HAL_FUNCTION(hal, ar5211, setSlotTime);
	AR5K_HAL_FUNCTION(hal, ar5211, getSlotTime);
	AR5K_HAL_FUNCTION(hal, ar5211, setAckTimeout);
	AR5K_HAL_FUNCTION(hal, ar5211, getAckTimeout);
	AR5K_HAL_FUNCTION(hal, ar5211, setCTSTimeout);
	AR5K_HAL_FUNCTION(hal, ar5211, getCTSTimeout);

	/*
	 * Key table (WEP) functions
	 */
	AR5K_HAL_FUNCTION(hal, ar5211, isHwCipherSupported);
	AR5K_HAL_FUNCTION(hal, ar5211, getKeyCacheSize);
	AR5K_HAL_FUNCTION(hal, ar5211, resetKeyCacheEntry);
	AR5K_HAL_FUNCTION(hal, ar5211, isKeyCacheEntryValid);
	AR5K_HAL_FUNCTION(hal, ar5211, setKeyCacheEntry);
	AR5K_HAL_FUNCTION(hal, ar5211, setKeyCacheEntryMac);

	/*
	 * Power management functions
	 */
	AR5K_HAL_FUNCTION(hal, ar5211, setPowerMode);
	AR5K_HAL_FUNCTION(hal, ar5211, getPowerMode);
	AR5K_HAL_FUNCTION(hal, ar5211, queryPSPollSupport);
	AR5K_HAL_FUNCTION(hal, ar5211, initPSPoll);
	AR5K_HAL_FUNCTION(hal, ar5211, enablePSPoll);
	AR5K_HAL_FUNCTION(hal, ar5211, disablePSPoll);

	/*
	 * Beacon functions
	 */
	AR5K_HAL_FUNCTION(hal, ar5211, beaconInit);
	AR5K_HAL_FUNCTION(hal, ar5211, setStationBeaconTimers);
	AR5K_HAL_FUNCTION(hal, ar5211, resetStationBeaconTimers);
	AR5K_HAL_FUNCTION(hal, ar5211, waitForBeaconDone);

	/*
	 * Interrupt functions
	 */
	AR5K_HAL_FUNCTION(hal, ar5211, isInterruptPending);
	AR5K_HAL_FUNCTION(hal, ar5211, getPendingInterrupts);
	AR5K_HAL_FUNCTION(hal, ar5211, getInterrupts);
	AR5K_HAL_FUNCTION(hal, ar5211, setInterrupts);

	/*
	 * Chipset functions (ar5k-specific, non-HAL)
	 */
	AR5K_HAL_FUNCTION(hal, ar5211, get_capabilities);
	AR5K_HAL_FUNCTION(hal, ar5211, radar_alert);

	/*
	 * EEPROM access
	 */
	AR5K_HAL_FUNCTION(hal, ar5211, eeprom_is_busy);
	AR5K_HAL_FUNCTION(hal, ar5211, eeprom_read);
	AR5K_HAL_FUNCTION(hal, ar5211, eeprom_write);
}

struct ath_hal *
ar5k_ar5211_attach(device, sc, st, sh, status)
	u_int16_t device;
	void *sc;
	bus_space_tag_t st;
	bus_space_handle_t sh;
	int *status;
{
	struct ath_hal *hal = (struct ath_hal*) sc;
	u_int8_t mac[IEEE80211_ADDR_LEN];
	u_int32_t srev;

	ar5k_ar5211_fill(hal);

	/* Bring device out of sleep and reset it's units */
	if (ar5k_ar5211_nic_wakeup(hal, AR5K_INIT_MODE) != AH_TRUE)
		return (NULL);

	/* Get MAC, PHY and RADIO revisions */
	srev = AR5K_REG_READ(AR5K_AR5211_SREV) & AR5K_AR5211_SREV_M;
	hal->ah_mac_version = srev & AR5K_AR5211_SREV_VERSION;
	hal->ah_mac_revision = srev & AR5K_AR5211_SREV_REVISION;
	hal->ah_phy_revision = AR5K_REG_READ(AR5K_AR5211_PHY_CHIP_ID) &
	    0x00ffffffff;

	hal->ah_radio_5ghz_revision =
	    ar5k_ar5211_radio_revision(hal, HAL_CHIP_5GHZ);

	/* Get the 2GHz radio revision if it's supported */
	if (hal->ah_mac_version >= AR5K_SREV_VER_AR5211)
		hal->ah_radio_2ghz_revision =
		    ar5k_ar5211_radio_revision(hal, HAL_CHIP_2GHZ);

	/* Identify the chipset (this has to be done in an early step) */
	hal->ah_version = AR5K_AR5211;
	hal->ah_radio = AR5K_AR5111;
	hal->ah_phy = AR5K_AR5211_PHY(0);

	bcopy(etherbroadcastaddr, mac, IEEE80211_ADDR_LEN);
	ar5k_ar5211_writeAssocid(hal, mac, 0, 0);
	ar5k_ar5211_getMacAddress(hal, mac);
	ar5k_ar5211_setPCUConfig(hal);

	return (hal);
}

HAL_BOOL
ar5k_ar5211_nic_reset(hal, val)
	struct ath_hal *hal;
	u_int32_t val;
{
	HAL_BOOL ret = AH_FALSE;
	u_int32_t mask = val ? val : ~0;

	/* Read-and-clear */
	AR5K_REG_READ(AR5K_AR5211_RXDP);

	/*
	 * Reset the device and wait until success
	 */
	AR5K_REG_WRITE(AR5K_AR5211_RC, val);

	/* Wait at least 128 PCI clocks */
	AR5K_DELAY(15);

	val &=
	    AR5K_AR5211_RC_PCU | AR5K_AR5211_RC_BB;

	mask &=
	    AR5K_AR5211_RC_PCU | AR5K_AR5211_RC_BB;

	ret = ar5k_register_timeout(hal, AR5K_AR5211_RC, mask, val, AH_FALSE);

	/*
	 * Reset configuration register
	 */
	if ((val & AR5K_AR5211_RC_PCU) == 0)
		AR5K_REG_WRITE(AR5K_AR5211_CFG, AR5K_AR5211_INIT_CFG);

	return (ret);
}

HAL_BOOL
ar5k_ar5211_nic_wakeup(hal, flags)
	struct ath_hal *hal;
	u_int16_t flags;
{
	u_int32_t turbo, mode, clock;

	turbo = 0;
	mode = 0;
	clock = 0;

	/*
	 * Get channel mode flags
	 */

	if (flags & IEEE80211_CHAN_2GHZ) {
		mode |= AR5K_AR5211_PHY_MODE_FREQ_2GHZ;
		clock |= AR5K_AR5211_PHY_PLL_44MHZ;
	} else if (flags & IEEE80211_CHAN_5GHZ) {
		mode |= AR5K_AR5211_PHY_MODE_FREQ_5GHZ;
		clock |= AR5K_AR5211_PHY_PLL_40MHZ;
	} else {
		AR5K_PRINT("invalid radio frequency mode\n");
		return (AH_FALSE);
	}

	if ((flags & IEEE80211_CHAN_CCK) ||
	    (flags & IEEE80211_CHAN_DYN)) {
		/* Dynamic OFDM/CCK is not supported by the AR5211 */
		mode |= AR5K_AR5211_PHY_MODE_MOD_CCK;
	} else if (flags & IEEE80211_CHAN_OFDM) {
		mode |= AR5K_AR5211_PHY_MODE_MOD_OFDM;
	} else {
		AR5K_PRINT("invalid radio frequency mode\n");
		return (AH_FALSE);
	}

	if (flags & IEEE80211_CHAN_TURBO) {
		turbo = AR5K_AR5211_PHY_TURBO_MODE |
		    AR5K_AR5211_PHY_TURBO_SHORT;
	}

	/*
	 * Reset and wakeup the device
	 */

	/* ...reset chipset and PCI device */
	if (ar5k_ar5211_nic_reset(hal,
		AR5K_AR5211_RC_CHIP | AR5K_AR5211_RC_PCI) == AH_FALSE) {
		AR5K_PRINT("failed to reset the AR5211 + PCI chipset\n");
		return (AH_FALSE);
	}

	/* ...wakeup */
	if (ar5k_ar5211_setPowerMode(hal,
		HAL_PM_AWAKE, AH_TRUE, 0) == AH_FALSE) {
		AR5K_PRINT("failed to resume the AR5211 (again)\n");
		return (AH_FALSE);
	}

	/* ...final warm reset */
	if (ar5k_ar5211_nic_reset(hal, 0) == AH_FALSE) {
		AR5K_PRINT("failed to warm reset the AR5211\n");
		return (AH_FALSE);
	}

	/* ...set the PHY operating mode */
	AR5K_REG_WRITE(AR5K_AR5211_PHY_PLL, clock);
	AR5K_DELAY(300);

	AR5K_REG_WRITE(AR5K_AR5211_PHY_MODE, mode);
	AR5K_REG_WRITE(AR5K_AR5211_PHY_TURBO, turbo);

	return (AH_TRUE);
}

u_int16_t
ar5k_ar5211_radio_revision(hal, chip)
	struct ath_hal *hal;
	HAL_CHIP chip;
{
	int i;
	u_int32_t srev;
	u_int16_t ret;

	/*
	 * Set the radio chip access register
	 */
	switch (chip) {
	case HAL_CHIP_2GHZ:
		AR5K_REG_WRITE(AR5K_AR5211_PHY(0), AR5K_AR5211_PHY_SHIFT_2GHZ);
		break;
	case HAL_CHIP_5GHZ:
		AR5K_REG_WRITE(AR5K_AR5211_PHY(0), AR5K_AR5211_PHY_SHIFT_5GHZ);
		break;
	default:
		return (0);
	}

	AR5K_DELAY(2000);

	/* ...wait until PHY is ready and read the selected radio revision */
	AR5K_REG_WRITE(AR5K_AR5211_PHY(0x34), 0x00001c16);

	for (i = 0; i < 8; i++)
		AR5K_REG_WRITE(AR5K_AR5211_PHY(0x20), 0x00010000);
	srev = (AR5K_REG_READ(AR5K_AR5211_PHY(0x100)) >> 24) & 0xff;

	ret = ar5k_bitswap(((srev & 0xf0) >> 4) | ((srev & 0x0f) << 4), 8);

	/* Reset to the 5GHz mode */
	AR5K_REG_WRITE(AR5K_AR5211_PHY(0), AR5K_AR5211_PHY_SHIFT_5GHZ);

	return (ret);
}

const HAL_RATE_TABLE *
ar5k_ar5211_getRateTable(hal, mode)
	struct ath_hal *hal;
	u_int mode;
{
	switch (mode) {
	case HAL_MODE_11A:
		return (&hal->ah_rt_11a);
	case HAL_MODE_TURBO:
		return (&hal->ah_rt_turbo);
	case HAL_MODE_11B:
		return (&hal->ah_rt_11b);
	case HAL_MODE_11G:
	case HAL_MODE_PUREG:
		return (&hal->ah_rt_11g);
	default:
		return (NULL);
	}

	return (NULL);
}

void
ar5k_ar5211_detach(hal)
	struct ath_hal *hal;
{
	if (hal->ah_rf_banks != NULL)
		free(hal->ah_rf_banks, M_DEVBUF);

	/*
	 * Free HAL structure, assume interrupts are down
	 */
	free(hal, M_DEVBUF);
}

HAL_BOOL
ar5k_ar5211_reset(hal, op_mode, channel, change_channel, status)
	struct ath_hal *hal;
	HAL_OPMODE op_mode;
	HAL_CHANNEL *channel;
	HAL_BOOL change_channel;
	HAL_STATUS *status;
{
	struct ar5k_eeprom_info *ee = &hal->ah_capabilities.cap_eeprom;
	u_int8_t mac[IEEE80211_ADDR_LEN];
	u_int32_t data, s_seq, s_ant, s_led[3];
	u_int i, mode, freq, ee_mode, ant[2];

	/*
	 * Save some registers before a reset
	 */
	if (change_channel == AH_TRUE) {
		s_seq = AR5K_REG_READ(AR5K_AR5211_DCU_SEQNUM(0));
		s_ant = AR5K_REG_READ(AR5K_AR5211_DEFAULT_ANTENNA);
	} else {
		s_seq = 0;
		s_ant = 1;
	}

	s_led[0] = AR5K_REG_READ(AR5K_AR5211_PCICFG) &
	    AR5K_AR5211_PCICFG_LEDSTATE;
	s_led[1] = AR5K_REG_READ(AR5K_AR5211_GPIOCR);
	s_led[2] = AR5K_REG_READ(AR5K_AR5211_GPIODO);

	if (ar5k_ar5211_nic_wakeup(hal, channel->c_channel_flags) == AH_FALSE)
		return (AH_FALSE);

	/*
	 * Initialize operating mode
	 */
	hal->ah_op_mode = op_mode;

	if (channel->c_channel_flags & IEEE80211_CHAN_A) {
		mode = AR5K_INI_VAL_11A;
		freq = AR5K_INI_RFGAIN_5GHZ;
		ee_mode = AR5K_EEPROM_MODE_11A;
	} else if (channel->c_channel_flags & IEEE80211_CHAN_T) {
		mode = AR5K_INI_VAL_11A_TURBO;
		freq = AR5K_INI_RFGAIN_5GHZ;
		ee_mode = AR5K_EEPROM_MODE_11A;
	} else if (channel->c_channel_flags & IEEE80211_CHAN_B) {
		mode = AR5K_INI_VAL_11B;
		freq = AR5K_INI_RFGAIN_2GHZ;
		ee_mode = AR5K_EEPROM_MODE_11B;
	} else if (channel->c_channel_flags & IEEE80211_CHAN_G) {
		mode = AR5K_INI_VAL_11G;
		freq = AR5K_INI_RFGAIN_2GHZ;
		ee_mode = AR5K_EEPROM_MODE_11G;
	} else {
		AR5K_PRINTF("invalid channel: %d\n", channel->c_channel);
		return (AH_FALSE);
	}

	/* PHY access enable */
	AR5K_REG_WRITE(AR5K_AR5211_PHY(0), AR5K_AR5211_PHY_SHIFT_5GHZ);

	/*
	 * Write initial mode settings
	 */
	for (i = 0; i < AR5K_ELEMENTS(ar5211_mode); i++) {
		AR5K_REG_WAIT(i);
		AR5K_REG_WRITE((u_int32_t)ar5211_mode[i].mode_register,
		    ar5211_mode[i].mode_value[mode]);
	}

	/*
	 * Write initial register settings
	 */
	for (i = 0; i < AR5K_ELEMENTS(ar5211_ini); i++) {
		if (change_channel == AH_TRUE &&
		    ar5211_ini[i].ini_register >= AR5K_AR5211_PCU_MIN &&
		    ar5211_ini[i].ini_register <= AR5K_AR5211_PCU_MAX)
			continue;

		AR5K_REG_WAIT(i);
		AR5K_REG_WRITE((u_int32_t)ar5211_ini[i].ini_register,
		    ar5211_ini[i].ini_value);
	}

	/*
	 * Write initial RF gain settings
	 */
	if (ar5k_rfgain(hal, AR5K_INI_PHY_5111, freq) == AH_FALSE)
		return (AH_FALSE);

	AR5K_DELAY(1000);

	/*
	 * Configure additional registers
	 */

	/* Set antenna mode */
	AR5K_REG_MASKED_BITS(AR5K_AR5211_PHY(0x44),
	    hal->ah_antenna[ee_mode][0], 0xfffffc06);

	ant[0] = HAL_ANT_FIXED_A;
	ant[1] = HAL_ANT_FIXED_B;

	if (hal->ah_ant_diversity == AH_FALSE) {
		if (freq == AR5K_INI_RFGAIN_2GHZ)
			ant[0] = HAL_ANT_FIXED_B;
		else if (freq == AR5K_INI_RFGAIN_5GHZ)
			ant[1] = HAL_ANT_FIXED_A;
	}

	AR5K_REG_WRITE(AR5K_AR5211_PHY_ANT_SWITCH_TABLE_0,
	    hal->ah_antenna[ee_mode][ant[0]]);
	AR5K_REG_WRITE(AR5K_AR5211_PHY_ANT_SWITCH_TABLE_1,
	    hal->ah_antenna[ee_mode][ant[1]]);

	/* Commit values from EEPROM */
	AR5K_REG_WRITE_BITS(AR5K_AR5211_PHY_FC,
	    AR5K_AR5211_PHY_FC_TX_CLIP, ee->ee_tx_clip);

	AR5K_REG_WRITE(AR5K_AR5211_PHY(0x5a),
	    AR5K_AR5211_PHY_NF_SVAL(ee->ee_noise_floor_thr[ee_mode]));

	AR5K_REG_MASKED_BITS(AR5K_AR5211_PHY(0x11),
	    (ee->ee_switch_settling[ee_mode] << 7) & 0x3f80, 0xffffc07f);
	AR5K_REG_MASKED_BITS(AR5K_AR5211_PHY(0x12),
	    (ee->ee_ant_tx_rx[ee_mode] << 12) & 0x3f000, 0xfffc0fff);
	AR5K_REG_MASKED_BITS(AR5K_AR5211_PHY(0x14),
	    (ee->ee_adc_desired_size[ee_mode] & 0x00ff) |
	    ((ee->ee_pga_desired_size[ee_mode] << 8) & 0xff00), 0xffff0000);

	AR5K_REG_WRITE(AR5K_AR5211_PHY(0x0d),
	    (ee->ee_tx_end2xpa_disable[ee_mode] << 24) |
	    (ee->ee_tx_end2xpa_disable[ee_mode] << 16) |
	    (ee->ee_tx_frm2xpa_enable[ee_mode] << 8) |
	    (ee->ee_tx_frm2xpa_enable[ee_mode]));

	AR5K_REG_MASKED_BITS(AR5K_AR5211_PHY(0x0a),
	    ee->ee_tx_end2xlna_enable[ee_mode] << 8, 0xffff00ff);
	AR5K_REG_MASKED_BITS(AR5K_AR5211_PHY(0x19),
	    (ee->ee_thr_62[ee_mode] << 12) & 0x7f000, 0xfff80fff);
	AR5K_REG_MASKED_BITS(AR5K_AR5211_PHY(0x49), 4, 0xffffff01);

	AR5K_REG_ENABLE_BITS(AR5K_AR5211_PHY_IQ,
	    AR5K_AR5211_PHY_IQ_CORR_ENABLE |
	    (ee->ee_i_cal[ee_mode] << AR5K_AR5211_PHY_IQ_CORR_Q_I_COFF_S) |
	    ee->ee_q_cal[ee_mode]);

	/*
	 * Restore saved values
	 */
	AR5K_REG_WRITE(AR5K_AR5211_DCU_SEQNUM(0), s_seq);
	AR5K_REG_WRITE(AR5K_AR5211_DEFAULT_ANTENNA, s_ant);
	AR5K_REG_ENABLE_BITS(AR5K_AR5211_PCICFG, s_led[0]);
	AR5K_REG_WRITE(AR5K_AR5211_GPIOCR, s_led[1]);
	AR5K_REG_WRITE(AR5K_AR5211_GPIODO, s_led[2]);

	/*
	 * Misc
	 */
	bcopy(etherbroadcastaddr, mac, IEEE80211_ADDR_LEN);
	ar5k_ar5211_writeAssocid(hal, mac, 0, 0);
	ar5k_ar5211_setPCUConfig(hal);
	AR5K_REG_WRITE(AR5K_AR5211_PISR, 0xffffffff);
	AR5K_REG_WRITE(AR5K_AR5211_RSSI_THR, AR5K_TUNE_RSSI_THRES);

	/*
	 * Set channel and calibrate the PHY
	 */
	if (ar5k_channel(hal, channel) == AH_FALSE)
		return (AH_FALSE);

	/*
	 * Enable the PHY and wait until completion
	 */
	AR5K_REG_WRITE(AR5K_AR5211_PHY_ACTIVE, AR5K_AR5211_PHY_ENABLE);

	data = AR5K_REG_READ(AR5K_AR5211_PHY_RX_DELAY) &
	    AR5K_AR5211_PHY_RX_DELAY_M;
	data = (channel->c_channel_flags & IEEE80211_CHAN_CCK) ?
	    ((data << 2) / 22) : (data / 10);

	AR5K_DELAY(100 + data);

	/*
	 * Start calibration
	 */
	AR5K_REG_ENABLE_BITS(AR5K_AR5211_PHY_AGCCTL,
	    AR5K_AR5211_PHY_AGCCTL_NF |
	    AR5K_AR5211_PHY_AGCCTL_CAL);

	if (channel->c_channel_flags & IEEE80211_CHAN_B) {
		hal->ah_calibration = AH_FALSE;
	} else {
		hal->ah_calibration = AH_TRUE;
		AR5K_REG_WRITE_BITS(AR5K_AR5211_PHY_IQ,
		    AR5K_AR5211_PHY_IQ_CAL_NUM_LOG_MAX, 15);
		AR5K_REG_ENABLE_BITS(AR5K_AR5211_PHY_IQ,
		    AR5K_AR5211_PHY_IQ_RUN);
	}

	/*
	 * Reset queues and start beacon timers at the end of the reset routine
	 */
	for (i = 0; i < hal->ah_capabilities.cap_queues.q_tx_num; i++) {
		AR5K_REG_WRITE_Q(AR5K_AR5211_DCU_QCUMASK(i), i);
		if (ar5k_ar5211_resetTxQueue(hal, i) == AH_FALSE) {
			AR5K_PRINTF("failed to reset TX queue #%d\n", i);
			return (AH_FALSE);
		}
	}

	/* Pre-enable interrupts */
	ar5k_ar5211_setInterrupts(hal, HAL_INT_RX | HAL_INT_TX | HAL_INT_FATAL);

	/*
	 * Set RF kill flags if supported by the device (read from the EEPROM)
	 */
	if (AR5K_EEPROM_HDR_RFKILL(hal->ah_capabilities.cap_eeprom.ee_header)) {
		ar5k_ar5211_gpioCfgInput(hal, 0);
		if ((hal->ah_gpio[0] = ar5k_ar5211_gpioGet(hal, 0)) == 0)
			ar5k_ar5211_gpioSetIntr(hal, 0, 1);
		else
			ar5k_ar5211_gpioSetIntr(hal, 0, 0);
	}

	/* 
	 * Disable beacons and reset the register
	 */
	AR5K_REG_DISABLE_BITS(AR5K_AR5211_BEACON,
	    AR5K_AR5211_BEACON_ENABLE | AR5K_AR5211_BEACON_RESET_TSF);

	return (AH_TRUE);
}

void
ar5k_ar5211_setPCUConfig(hal)
	struct ath_hal *hal;
{
	u_int32_t pcu_reg, low_id, high_id;

	pcu_reg = 0;

	switch (hal->ah_op_mode) {
	case IEEE80211_M_IBSS:
		pcu_reg |= AR5K_AR5211_STA_ID1_ADHOC |
		    AR5K_AR5211_STA_ID1_DESC_ANTENNA;
		break;

	case IEEE80211_M_HOSTAP:
		pcu_reg |= AR5K_AR5211_STA_ID1_AP |
		    AR5K_AR5211_STA_ID1_RTS_DEFAULT_ANTENNA;
		break;

	case IEEE80211_M_STA:
	case IEEE80211_M_MONITOR:
		pcu_reg |= AR5K_AR5211_STA_ID1_DEFAULT_ANTENNA;
		break;

	default:
		return;
	}

	/*
	 * Set PCU registers
	 */
	bcopy(&(hal->ah_sta_id[0]), &low_id, 4);
	bcopy(&(hal->ah_sta_id[4]), &high_id, 2);
	AR5K_REG_WRITE(AR5K_AR5211_STA_ID0, low_id);
	AR5K_REG_WRITE(AR5K_AR5211_STA_ID1, pcu_reg | high_id);

	return;
}

HAL_BOOL
ar5k_ar5211_perCalibration(hal, channel)
	struct ath_hal *hal;
	HAL_CHANNEL *channel;
{
	u_int32_t i_pwr, q_pwr;
	int32_t iq_corr, i_coff, i_coffd, q_coff, q_coffd;

	if (hal->ah_calibration == AH_FALSE ||
	    AR5K_REG_READ(AR5K_AR5211_PHY_IQ) & AR5K_AR5211_PHY_IQ_RUN)
		goto done;

	hal->ah_calibration = AH_FALSE;

	iq_corr = AR5K_REG_READ(AR5K_AR5211_PHY_IQRES_CAL_CORR);
	i_pwr = AR5K_REG_READ(AR5K_AR5211_PHY_IQRES_CAL_PWR_I);
	q_pwr = AR5K_REG_READ(AR5K_AR5211_PHY_IQRES_CAL_PWR_Q);
	i_coffd = ((i_pwr >> 1) + (q_pwr >> 1)) >> 7;
	q_coffd = q_pwr >> 6;

	if (i_coffd == 0 || q_coffd == 0)
		goto done;

	i_coff = ((-iq_corr) / i_coffd) & 0x3f;
	q_coff = (((int32_t)i_pwr / q_coffd) - 64) & 0x1f;

	/* Commit new IQ value */
	AR5K_REG_ENABLE_BITS(AR5K_AR5211_PHY_IQ,
	    AR5K_AR5211_PHY_IQ_CORR_ENABLE |
	    ((u_int32_t)q_coff) |
	    ((u_int32_t)i_coff << AR5K_AR5211_PHY_IQ_CORR_Q_I_COFF_S));

 done:
	/* Start noise floor calibration */
	AR5K_REG_ENABLE_BITS(AR5K_AR5211_PHY_AGCCTL,
	    AR5K_AR5211_PHY_AGCCTL_NF);

	return (AH_TRUE);
}

/*
 * Transmit functions
 */

HAL_BOOL
ar5k_ar5211_updateTxTrigLevel(hal, increase)
	struct ath_hal *hal;
	HAL_BOOL increase;
{
	u_int32_t trigger_level, imr;
	HAL_BOOL status = AH_FALSE;

	/*
	 * Disable interrupts by setting the mask
	 */
	imr = ar5k_ar5211_setInterrupts(hal, hal->ah_imr & ~HAL_INT_GLOBAL);

	trigger_level = AR5K_REG_MS(AR5K_REG_READ(AR5K_AR5211_TXCFG),
	    AR5K_AR5211_TXCFG_TXFULL);

	if (increase == AH_FALSE) {
		if (--trigger_level < AR5K_TUNE_MIN_TX_FIFO_THRES)
			goto done;
	} else
		trigger_level +=
		    ((AR5K_TUNE_MAX_TX_FIFO_THRES - trigger_level) / 2);

	/*
	 * Update trigger level on success
	 */
	AR5K_REG_WRITE_BITS(AR5K_AR5211_TXCFG,
	    AR5K_AR5211_TXCFG_TXFULL, trigger_level);
	status = AH_TRUE;

 done:
	/*
	 * Restore interrupt mask
	 */
	ar5k_ar5211_setInterrupts(hal, imr);

	return (status);
}

int
ar5k_ar5211_setupTxQueue(hal, queue_type, queue_info)
	struct ath_hal *hal;
	HAL_TX_QUEUE queue_type;
	const HAL_TXQ_INFO *queue_info;
{
	u_int queue;

	/*
	 * Get queue by type
	 */
	if (queue_type == HAL_TX_QUEUE_DATA) {
		for (queue = HAL_TX_QUEUE_ID_DATA_MIN;
		     hal->ah_txq[queue].tqi_type != HAL_TX_QUEUE_INACTIVE;
		     queue++)
			if (queue > HAL_TX_QUEUE_ID_DATA_MAX)
				return (-1);
	} else if (queue_type == HAL_TX_QUEUE_PSPOLL) {
		queue = HAL_TX_QUEUE_ID_PSPOLL;
	} else if (queue_type == HAL_TX_QUEUE_BEACON) {
		queue = HAL_TX_QUEUE_ID_BEACON;
	} else if (queue_type == HAL_TX_QUEUE_CAB) {
		queue = HAL_TX_QUEUE_ID_CAB;
	} else
		return (-1);

	/*
	 * Setup internal queue structure
	 */
	bzero(&hal->ah_txq[queue], sizeof(HAL_TXQ_INFO));
	hal->ah_txq[queue].tqi_type = queue_type;

	if (queue_info != NULL) {
		if (ar5k_ar5211_setTxQueueProps(hal, queue, queue_info)
		    != AH_TRUE)
			return (-1);
	}

	AR5K_Q_ENABLE_BITS(hal->ah_txq_interrupts, queue);

	return (queue);
}

HAL_BOOL
ar5k_ar5211_setTxQueueProps(hal, queue, queue_info)
	struct ath_hal *hal;
	int queue;
	const HAL_TXQ_INFO *queue_info;
{
	AR5K_ASSERT_ENTRY(queue, hal->ah_capabilities.cap_queues.q_tx_num);

	if (hal->ah_txq[queue].tqi_type == HAL_TX_QUEUE_INACTIVE)
		return (AH_FALSE);

	bcopy(queue_info, &hal->ah_txq[queue], sizeof(HAL_TXQ_INFO));

	if (queue_info->tqi_type == HAL_TX_QUEUE_DATA &&
	    (queue_info->tqi_subtype >= HAL_WME_AC_VI) &&
	    (queue_info->tqi_subtype <= HAL_WME_UPSD))
		hal->ah_txq[queue].tqi_flags |=
		    AR5K_TXQ_FLAG_POST_FR_BKOFF_DIS;

	return (AH_TRUE);
}

HAL_BOOL
ar5k_ar5211_releaseTxQueue(hal, queue)
	struct ath_hal *hal;
	u_int queue;
{
	AR5K_ASSERT_ENTRY(queue, hal->ah_capabilities.cap_queues.q_tx_num);

	/* This queue will be skipped in further operations */
	hal->ah_txq[queue].tqi_type = HAL_TX_QUEUE_INACTIVE;
	AR5K_Q_DISABLE_BITS(hal->ah_txq_interrupts, queue);

	return (AH_FALSE);
}

HAL_BOOL
ar5k_ar5211_resetTxQueue(hal, queue)
	struct ath_hal *hal;
	u_int queue;
{
	u_int32_t cw_min, cw_max, retry_lg, retry_sh;
	struct ieee80211_channel *channel = (struct ieee80211_channel*)
	    &hal->ah_current_channel;
	HAL_TXQ_INFO *tq;

	AR5K_ASSERT_ENTRY(queue, hal->ah_capabilities.cap_queues.q_tx_num);

	tq = &hal->ah_txq[queue];

	if (tq->tqi_type == HAL_TX_QUEUE_INACTIVE)
		return (AH_TRUE);

	/*
	 * Set registers by channel mode
	 */
	if (IEEE80211_IS_CHAN_B(channel)) {
		hal->ah_cw_min = AR5K_TUNE_CWMIN_11B;
		cw_max = hal->ah_cw_max = AR5K_TUNE_CWMAX_11B;
		hal->ah_aifs = AR5K_TUNE_AIFS_11B;
	} else {
		hal->ah_cw_min = AR5K_TUNE_CWMIN;
		cw_max = hal->ah_cw_max = AR5K_TUNE_CWMAX;
		hal->ah_aifs = AR5K_TUNE_AIFS;
	}

	/*
	 * Set retry limits
	 */
	if (hal->ah_software_retry == AH_TRUE) {
		/* XXX Need to test this */
		retry_lg = hal->ah_limit_tx_retries;
		retry_sh = retry_lg =
		    retry_lg > AR5K_AR5211_DCU_RETRY_LMT_SH_RETRY ?
		    AR5K_AR5211_DCU_RETRY_LMT_SH_RETRY : retry_lg;
	} else {
		retry_lg = AR5K_INIT_LG_RETRY;
		retry_sh = AR5K_INIT_SH_RETRY;
	}

	AR5K_REG_WRITE(AR5K_AR5211_DCU_RETRY_LMT(queue),
	    AR5K_REG_SM(AR5K_INIT_SLG_RETRY,
	    AR5K_AR5211_DCU_RETRY_LMT_SLG_RETRY) |
	    AR5K_REG_SM(AR5K_INIT_SSH_RETRY,
	    AR5K_AR5211_DCU_RETRY_LMT_SSH_RETRY) |
	    AR5K_REG_SM(retry_lg, AR5K_AR5211_DCU_RETRY_LMT_LG_RETRY) |
	    AR5K_REG_SM(retry_sh, AR5K_AR5211_DCU_RETRY_LMT_SH_RETRY));

	/*
	 * Set initial content window (cw_min/cw_max)
	 */
	cw_min = 1;
	while (cw_min < hal->ah_cw_min)
		cw_min = (cw_min << 1) | 1;

	cw_min = tq->tqi_cw_min < 0 ?
	    (cw_min >> (-tq->tqi_cw_min)) :
	    ((cw_min << tq->tqi_cw_min) + (1 << tq->tqi_cw_min) - 1);
	cw_max = tq->tqi_cw_max < 0 ?
	    (cw_max >> (-tq->tqi_cw_max)) :
	    ((cw_max << tq->tqi_cw_max) + (1 << tq->tqi_cw_max) - 1);

	AR5K_REG_WRITE(AR5K_AR5211_DCU_LCL_IFS(queue),
	    AR5K_REG_SM(cw_min, AR5K_AR5211_DCU_LCL_IFS_CW_MIN) |
	    AR5K_REG_SM(cw_max, AR5K_AR5211_DCU_LCL_IFS_CW_MAX) |
	    AR5K_REG_SM(hal->ah_aifs + tq->tqi_aifs,
	    AR5K_AR5211_DCU_LCL_IFS_AIFS));

	/*
	 * Set misc registers
	 */
	AR5K_REG_WRITE(AR5K_AR5211_QCU_MISC(queue),
	    AR5K_AR5211_QCU_MISC_DCU_EARLY);

	if (tq->tqi_cbr_period) {
		AR5K_REG_WRITE(AR5K_AR5211_QCU_CBRCFG(queue),
		    AR5K_REG_SM(tq->tqi_cbr_period,
		    AR5K_AR5211_QCU_CBRCFG_INTVAL) |
		    AR5K_REG_SM(tq->tqi_cbr_overflow_limit,
		    AR5K_AR5211_QCU_CBRCFG_ORN_THRES));
		AR5K_REG_ENABLE_BITS(AR5K_AR5211_QCU_MISC(queue),
		    AR5K_AR5211_QCU_MISC_FRSHED_CBR);
		if (tq->tqi_cbr_overflow_limit)
			AR5K_REG_ENABLE_BITS(AR5K_AR5211_QCU_MISC(queue),
			    AR5K_AR5211_QCU_MISC_CBR_THRES_ENABLE);
	}

	if (tq->tqi_ready_time) {
		AR5K_REG_WRITE(AR5K_AR5211_QCU_RDYTIMECFG(queue),
		    AR5K_REG_SM(tq->tqi_ready_time,
		    AR5K_AR5211_QCU_RDYTIMECFG_INTVAL) |
		    AR5K_AR5211_QCU_RDYTIMECFG_ENABLE);
	}

	if (tq->tqi_burst_time) {
		AR5K_REG_WRITE(AR5K_AR5211_DCU_CHAN_TIME(queue),
		    AR5K_REG_SM(tq->tqi_burst_time,
		    AR5K_AR5211_DCU_CHAN_TIME_DUR) |
		    AR5K_AR5211_DCU_CHAN_TIME_ENABLE);

		if (tq->tqi_flags & AR5K_TXQ_FLAG_RDYTIME_EXP_POLICY_ENABLE) {
			AR5K_REG_ENABLE_BITS(AR5K_AR5211_QCU_MISC(queue),
			    AR5K_AR5211_QCU_MISC_TXE);
		}
	}

	if (tq->tqi_flags & AR5K_TXQ_FLAG_BACKOFF_DISABLE) {
		AR5K_REG_WRITE(AR5K_AR5211_DCU_MISC(queue),
		    AR5K_AR5211_DCU_MISC_POST_FR_BKOFF_DIS);
	}

	if (tq->tqi_flags & AR5K_TXQ_FLAG_FRAG_BURST_BACKOFF_ENABLE) {
		AR5K_REG_WRITE(AR5K_AR5211_DCU_MISC(queue),
		    AR5K_AR5211_DCU_MISC_BACKOFF_FRAG);
	}

	/*
	 * Set registers by queue type
	 */
	switch (tq->tqi_type) {
	case HAL_TX_QUEUE_BEACON:
		AR5K_REG_ENABLE_BITS(AR5K_AR5211_QCU_MISC(queue),
		    AR5K_AR5211_QCU_MISC_FRSHED_DBA_GT |
		    AR5K_AR5211_QCU_MISC_CBREXP_BCN |
		    AR5K_AR5211_QCU_MISC_BCN_ENABLE);

		AR5K_REG_ENABLE_BITS(AR5K_AR5211_DCU_MISC(queue),
		    (AR5K_AR5211_DCU_MISC_ARBLOCK_CTL_GLOBAL <<
		    AR5K_AR5211_DCU_MISC_ARBLOCK_CTL_GLOBAL) |
		    AR5K_AR5211_DCU_MISC_POST_FR_BKOFF_DIS |
		    AR5K_AR5211_DCU_MISC_BCN_ENABLE);

		AR5K_REG_WRITE(AR5K_AR5211_QCU_RDYTIMECFG(queue),
		    ((AR5K_TUNE_BEACON_INTERVAL -
		    (AR5K_TUNE_SW_BEACON_RESP - AR5K_TUNE_DMA_BEACON_RESP) -
		    AR5K_TUNE_ADDITIONAL_SWBA_BACKOFF) * 1024) |
		    AR5K_AR5211_QCU_RDYTIMECFG_ENABLE);
		break;

	case HAL_TX_QUEUE_CAB:
		AR5K_REG_ENABLE_BITS(AR5K_AR5211_QCU_MISC(queue),
		    AR5K_AR5211_QCU_MISC_FRSHED_DBA_GT |
		    AR5K_AR5211_QCU_MISC_CBREXP |
		    AR5K_AR5211_QCU_MISC_CBREXP_BCN);

		AR5K_REG_ENABLE_BITS(AR5K_AR5211_DCU_MISC(queue),
		    (AR5K_AR5211_DCU_MISC_ARBLOCK_CTL_GLOBAL <<
		    AR5K_AR5211_DCU_MISC_ARBLOCK_CTL_GLOBAL));
		break;

	case HAL_TX_QUEUE_PSPOLL:
		AR5K_REG_ENABLE_BITS(AR5K_AR5211_QCU_MISC(queue),
		    AR5K_AR5211_QCU_MISC_CBREXP);
		break;

	case HAL_TX_QUEUE_DATA:
	default:
		break;
	}

	/*
	 * Enable tx queue in the secondary interrupt mask registers
	 */
	AR5K_REG_WRITE(AR5K_AR5211_SIMR0,
	    AR5K_REG_SM(hal->ah_txq_interrupts, AR5K_AR5211_SIMR0_QCU_TXOK) |
	    AR5K_REG_SM(hal->ah_txq_interrupts, AR5K_AR5211_SIMR0_QCU_TXDESC));
	AR5K_REG_WRITE(AR5K_AR5211_SIMR1,
	    AR5K_REG_SM(hal->ah_txq_interrupts, AR5K_AR5211_SIMR1_QCU_TXERR));
	AR5K_REG_WRITE(AR5K_AR5211_SIMR2,
	    AR5K_REG_SM(hal->ah_txq_interrupts, AR5K_AR5211_SIMR2_QCU_TXURN));

	return (AH_TRUE);
}

u_int32_t
ar5k_ar5211_getTxDP(hal, queue)
	struct ath_hal *hal;
	u_int queue;
{
	AR5K_ASSERT_ENTRY(queue, hal->ah_capabilities.cap_queues.q_tx_num);

	/*
	 * Get the transmit queue descriptor pointer from the selected queue
	 */
	return (AR5K_REG_READ(AR5K_AR5211_QCU_TXDP(queue)));
}

HAL_BOOL
ar5k_ar5211_setTxDP(hal, queue, phys_addr)
	struct ath_hal *hal;
	u_int queue;
	u_int32_t phys_addr;
{
	AR5K_ASSERT_ENTRY(queue, hal->ah_capabilities.cap_queues.q_tx_num);

	/*
	 * Set the transmit queue descriptor pointer for the selected queue
	 * (this won't work if the queue is still active)
	 */
	if (AR5K_REG_READ_Q(AR5K_AR5211_QCU_TXE, queue))
		return (AH_FALSE);

	AR5K_REG_WRITE(AR5K_AR5211_QCU_TXDP(queue), phys_addr);

	return (AH_TRUE);
}

HAL_BOOL
ar5k_ar5211_startTxDma(hal, queue)
	struct ath_hal *hal;
	u_int queue;
{
	AR5K_ASSERT_ENTRY(queue, hal->ah_capabilities.cap_queues.q_tx_num);

	/* Return if queue is disabled */
	if (AR5K_REG_READ_Q(AR5K_AR5211_QCU_TXD, queue))
		return (AH_FALSE);

	/* Start queue */
	AR5K_REG_WRITE_Q(AR5K_AR5211_QCU_TXE, queue);

	return (AH_TRUE);
}

HAL_BOOL
ar5k_ar5211_stopTxDma(hal, queue)
	struct ath_hal *hal;
	u_int queue;
{
	HAL_BOOL ret;

	AR5K_ASSERT_ENTRY(queue, hal->ah_capabilities.cap_queues.q_tx_num);

	/*
	 * Schedule TX disable and wait until queue is empty
	 */
	AR5K_REG_WRITE_Q(AR5K_AR5211_QCU_TXD, queue);

	ret = ar5k_register_timeout(hal, AR5K_AR5211_QCU_STS(queue),
	    AR5K_AR5211_QCU_STS_FRMPENDCNT, 0, AH_FALSE);

	if (AR5K_REG_READ_Q(AR5K_AR5211_QCU_TXE, queue))
		return (AH_FALSE);

	/* Clear register */
	AR5K_REG_WRITE(AR5K_AR5211_QCU_TXD, 0);

	return (AH_TRUE);
}

HAL_BOOL
ar5k_ar5211_setupTxDesc(hal, desc, packet_length, header_length, type, tx_power,
    tx_rate0, tx_tries0, key_index, antenna_mode, flags, rtscts_rate,
    rtscts_duration)
	struct ath_hal *hal;
	struct ath_desc *desc;
	u_int packet_length;
	u_int header_length;
	HAL_PKT_TYPE type;
	u_int tx_power;
	u_int tx_rate0;
	u_int tx_tries0;
	u_int key_index;
	u_int antenna_mode;
	u_int flags;
	u_int rtscts_rate;
	u_int rtscts_duration;
{
	struct ar5k_ar5211_tx_desc *tx_desc;

	tx_desc = (struct ar5k_ar5211_tx_desc*)&desc->ds_ctl0;

	/* Clear descriptor */
	bzero(tx_desc, sizeof(struct ar5k_ar5211_tx_desc));

	/*
	 * Validate input
	 */
	if (tx_tries0 == 0)
		return (AH_FALSE);

	if ((tx_desc->tx_control_0 = (packet_length &
	    AR5K_AR5211_DESC_TX_CTL0_FRAME_LEN)) != packet_length)
		return (AH_FALSE);

	tx_desc->tx_control_0 |=
	    AR5K_REG_SM(tx_rate0, AR5K_AR5211_DESC_TX_CTL0_XMIT_RATE) |
	    AR5K_REG_SM(antenna_mode, AR5K_AR5211_DESC_TX_CTL0_ANT_MODE_XMIT);
	tx_desc->tx_control_1 =
	    AR5K_REG_SM(type, AR5K_AR5211_DESC_TX_CTL1_FRAME_TYPE);

#define _TX_FLAGS(_c, _flag)	 					\
	if (flags & HAL_TXDESC_##_flag)					\
		tx_desc->tx_control_##_c |=    				\
			AR5K_AR5211_DESC_TX_CTL##_c##_##_flag

	_TX_FLAGS(0, CLRDMASK);
	_TX_FLAGS(0, VEOL);
	_TX_FLAGS(0, INTREQ);
	_TX_FLAGS(0, RTSENA);
	_TX_FLAGS(1, NOACK);

#undef _TX_FLAGS

	/*
	 * WEP crap
	 */
	if (key_index != HAL_TXKEYIX_INVALID) {
		tx_desc->tx_control_0 |=
		    AR5K_AR5211_DESC_TX_CTL0_ENCRYPT_KEY_VALID;
		tx_desc->tx_control_1 |=
		    AR5K_REG_SM(key_index,
		    AR5K_AR5211_DESC_TX_CTL1_ENCRYPT_KEY_INDEX);
	}

	return (AH_TRUE);
}

HAL_BOOL
ar5k_ar5211_fillTxDesc(hal, desc, segment_length, first_segment, last_segment)
	struct ath_hal *hal;
	struct ath_desc *desc;
	u_int segment_length;
	HAL_BOOL first_segment;
	HAL_BOOL last_segment;
{
	struct ar5k_ar5211_tx_desc *tx_desc;

	tx_desc = (struct ar5k_ar5211_tx_desc*)&desc->ds_ctl0;

	/* Clear status descriptor */
	bzero(desc->ds_hw, sizeof(desc->ds_hw));

	/* Validate segment length and initialize the descriptor */
	if ((tx_desc->tx_control_1 = (segment_length &
	    AR5K_AR5211_DESC_TX_CTL1_BUF_LEN)) != segment_length)
		return (AH_FALSE);

	if (first_segment != AH_TRUE)
		tx_desc->tx_control_0 &= ~AR5K_AR5211_DESC_TX_CTL0_FRAME_LEN;

	if (last_segment != AH_TRUE)
		tx_desc->tx_control_1 |= AR5K_AR5211_DESC_TX_CTL1_MORE;

	return (AH_TRUE);
}

HAL_BOOL
ar5k_ar5211_setupXTxDesc(hal, desc, tx_rate1, tx_tries1, tx_rate2, tx_tries2,
    tx_rate3, tx_tries3)
	struct ath_hal *hal;
	struct ath_desc *desc;
	u_int tx_rate1;
	u_int tx_tries1;
	u_int tx_rate2;
	u_int tx_tries2;
	u_int tx_rate3;
	u_int tx_tries3;
{
	return (AH_FALSE);
}

HAL_STATUS
ar5k_ar5211_procTxDesc(hal, desc)
	struct ath_hal *hal;
	struct ath_desc *desc;
{
	struct ar5k_ar5211_tx_status *tx_status;
	struct ar5k_ar5211_tx_desc *tx_desc;

	tx_desc = (struct ar5k_ar5211_tx_desc*)&desc->ds_ctl0;
	tx_status = (struct ar5k_ar5211_tx_status*)&desc->ds_hw[0];

	/* No frame has been send or error */
	if ((tx_status->tx_status_1 & AR5K_AR5211_DESC_TX_STATUS1_DONE) == 0)
		return (HAL_EINPROGRESS);

	/*
	 * Get descriptor status
	 */
	desc->ds_us.tx.ts_tstamp =
	    AR5K_REG_MS(tx_status->tx_status_0,
	    AR5K_AR5211_DESC_TX_STATUS0_SEND_TIMESTAMP);
	desc->ds_us.tx.ts_shortretry =
	    AR5K_REG_MS(tx_status->tx_status_0,
	    AR5K_AR5211_DESC_TX_STATUS0_RTS_FAIL_COUNT);
	desc->ds_us.tx.ts_longretry =
	    AR5K_REG_MS(tx_status->tx_status_0,
	    AR5K_AR5211_DESC_TX_STATUS0_DATA_FAIL_COUNT);
	desc->ds_us.tx.ts_seqnum =
	    AR5K_REG_MS(tx_status->tx_status_1,
	    AR5K_AR5211_DESC_TX_STATUS1_SEQ_NUM);
	desc->ds_us.tx.ts_rssi =
	    AR5K_REG_MS(tx_status->tx_status_1,
	    AR5K_AR5211_DESC_TX_STATUS1_ACK_SIG_STRENGTH);
	desc->ds_us.tx.ts_antenna = 1;
	desc->ds_us.tx.ts_status = 0;
	desc->ds_us.tx.ts_tstamp =
	    AR5K_REG_MS(tx_desc->tx_control_0,
	    AR5K_AR5211_DESC_TX_CTL0_XMIT_RATE);

	if ((tx_status->tx_status_0 &
	    AR5K_AR5211_DESC_TX_STATUS0_FRAME_XMIT_OK) == 0) {
		if (tx_status->tx_status_0 &
		    AR5K_AR5211_DESC_TX_STATUS0_EXCESSIVE_RETRIES)
			desc->ds_us.tx.ts_status |= HAL_TXERR_XRETRY;

		if (tx_status->tx_status_0 &
		    AR5K_AR5211_DESC_TX_STATUS0_FIFO_UNDERRUN)
			desc->ds_us.tx.ts_status |= HAL_TXERR_FIFO;

		if (tx_status->tx_status_0 &
		    AR5K_AR5211_DESC_TX_STATUS0_FILTERED)
			desc->ds_us.tx.ts_status |= HAL_TXERR_FILT;
	}

	return (HAL_OK);
}

HAL_BOOL
ar5k_ar5211_hasVEOL(hal)
	struct ath_hal *hal;
{
	return (AH_TRUE);
}

/*
 * Receive functions
 */

u_int32_t
ar5k_ar5211_getRxDP(hal)
	struct ath_hal *hal;
{
	return (AR5K_REG_READ(AR5K_AR5211_RXDP));
}

void
ar5k_ar5211_setRxDP(hal, phys_addr)
	struct ath_hal *hal;
	u_int32_t phys_addr;
{
	AR5K_REG_WRITE(AR5K_AR5211_RXDP, phys_addr);
}

void
ar5k_ar5211_enableReceive(hal)
	struct ath_hal *hal;
{
	AR5K_REG_WRITE(AR5K_AR5211_CR, AR5K_AR5211_CR_RXE);
}

HAL_BOOL
ar5k_ar5211_stopDmaReceive(hal)
	struct ath_hal *hal;
{
	int i;

	AR5K_REG_WRITE(AR5K_AR5211_CR, AR5K_AR5211_CR_RXD);

	/*
	 * It may take some time to disable the DMA receive unit
	 */
	for (i = 2000;
	     i > 0 && (AR5K_REG_READ(AR5K_AR5211_CR) & AR5K_AR5211_CR_RXE) != 0;
	     i--)
		AR5K_DELAY(10);

	return (i > 0 ? AH_TRUE : AH_FALSE);
}

void
ar5k_ar5211_startPcuReceive(hal)
	struct ath_hal *hal;
{
	AR5K_REG_DISABLE_BITS(AR5K_AR5211_DIAG_SW, AR5K_AR5211_DIAG_SW_DIS_RX);
}

void
ar5k_ar5211_stopPcuReceive(hal)
	struct ath_hal *hal;
{
	AR5K_REG_ENABLE_BITS(AR5K_AR5211_DIAG_SW, AR5K_AR5211_DIAG_SW_DIS_RX);
}

void
ar5k_ar5211_setMulticastFilter(hal, filter0, filter1)
	struct ath_hal *hal;
	u_int32_t filter0;
	u_int32_t filter1;
{
	/* Set the multicat filter */
	AR5K_REG_WRITE(AR5K_AR5211_MCAST_FIL0, filter0);
	AR5K_REG_WRITE(AR5K_AR5211_MCAST_FIL1, filter1);
}

HAL_BOOL
ar5k_ar5211_setMulticastFilterIndex(hal, index)
	struct ath_hal *hal;
	u_int32_t index;
{
	if (index >= 64) {
	    return (AH_FALSE);
	} else if (index >= 32) {
	    AR5K_REG_ENABLE_BITS(AR5K_AR5211_MCAST_FIL1,
		(1 << (index - 32)));
	} else {
	    AR5K_REG_ENABLE_BITS(AR5K_AR5211_MCAST_FIL0,
		(1 << index));
	}

	return (AH_TRUE);
}

HAL_BOOL
ar5k_ar5211_clrMulticastFilterIndex(hal, index)
	struct ath_hal *hal;
	u_int32_t index;
{

	if (index >= 64) {
	    return (AH_FALSE);
	} else if (index >= 32) {
	    AR5K_REG_DISABLE_BITS(AR5K_AR5211_MCAST_FIL1,
		(1 << (index - 32)));
	} else {
	    AR5K_REG_DISABLE_BITS(AR5K_AR5211_MCAST_FIL0,
		(1 << index));
	}

	return (AH_TRUE);
}

u_int32_t
ar5k_ar5211_getRxFilter(hal)
	struct ath_hal *hal;
{
	return (AR5K_REG_READ(AR5K_AR5211_RX_FILTER));
}

void
ar5k_ar5211_setRxFilter(hal, filter)
	struct ath_hal *hal;
	u_int32_t filter;
{
	AR5K_REG_WRITE(AR5K_AR5211_RX_FILTER, filter);
}

HAL_BOOL
ar5k_ar5211_setupRxDesc(hal, desc, size, flags)
	struct ath_hal *hal;
	struct ath_desc *desc;
	u_int32_t size;
	u_int flags;
{
	struct ar5k_ar5211_rx_desc *rx_desc;

	/* Reset descriptor */
	desc->ds_ctl0 = 0;
	desc->ds_ctl1 = 0;
	bzero(&desc->ds_hw[0], sizeof(struct ar5k_ar5211_rx_status));

	rx_desc = (struct ar5k_ar5211_rx_desc*)&desc->ds_ctl0;

	if ((rx_desc->rx_control_1 = (size &
	    AR5K_AR5211_DESC_RX_CTL1_BUF_LEN)) != size)
		return (AH_FALSE);

	if (flags & HAL_RXDESC_INTREQ)
		rx_desc->rx_control_1 |= AR5K_AR5211_DESC_RX_CTL1_INTREQ;

	return (AH_TRUE);
}

HAL_STATUS
ar5k_ar5211_procRxDesc(hal, desc, phys_addr, next)
	struct ath_hal *hal;
	struct ath_desc *desc;
	u_int32_t phys_addr;
	struct ath_desc *next;
{
	struct ar5k_ar5211_rx_status *rx_status;

	rx_status = (struct ar5k_ar5211_rx_status*)&desc->ds_hw[0];

	/* No frame received / not ready */
	if ((rx_status->rx_status_1 & AR5K_AR5211_DESC_RX_STATUS1_DONE) == 0)
		return (HAL_EINPROGRESS);

	/*
	 * Frame receive status
	 */
	desc->ds_us.rx.rs_datalen = rx_status->rx_status_0 &
	    AR5K_AR5211_DESC_RX_STATUS0_DATA_LEN;
	desc->ds_us.rx.rs_rssi =
	    AR5K_REG_MS(rx_status->rx_status_0,
	    AR5K_AR5211_DESC_RX_STATUS0_RECEIVE_SIGNAL);
	desc->ds_us.rx.rs_rate =
	    AR5K_REG_MS(rx_status->rx_status_0,
	    AR5K_AR5211_DESC_RX_STATUS0_RECEIVE_RATE);
	desc->ds_us.rx.rs_antenna = rx_status->rx_status_0 &
	    AR5K_AR5211_DESC_RX_STATUS0_RECEIVE_ANTENNA;
	desc->ds_us.rx.rs_more = rx_status->rx_status_0 &
	    AR5K_AR5211_DESC_RX_STATUS0_MORE;
	desc->ds_us.rx.rs_tstamp =
	    AR5K_REG_MS(rx_status->rx_status_1,
	    AR5K_AR5211_DESC_RX_STATUS1_RECEIVE_TIMESTAMP);
	desc->ds_us.rx.rs_status = 0;

	/*
	 * Key table status
	 */
	if (rx_status->rx_status_1 &
	    AR5K_AR5211_DESC_RX_STATUS1_KEY_INDEX_VALID) {
		desc->ds_us.rx.rs_keyix =
		    AR5K_REG_MS(rx_status->rx_status_1,
		    AR5K_AR5211_DESC_RX_STATUS1_KEY_INDEX);
	} else {
		desc->ds_us.rx.rs_keyix = HAL_RXKEYIX_INVALID;
	}

	/*
	 * Receive/descriptor errors
	 */
	if ((rx_status->rx_status_1 &
	    AR5K_AR5211_DESC_RX_STATUS1_FRAME_RECEIVE_OK) == 0) {
		if (rx_status->rx_status_1 &
		    AR5K_AR5211_DESC_RX_STATUS1_CRC_ERROR)
			desc->ds_us.rx.rs_status |= HAL_RXERR_CRC;

		if (rx_status->rx_status_1 &
		    AR5K_AR5211_DESC_RX_STATUS1_PHY_ERROR) {
			desc->ds_us.rx.rs_status |= HAL_RXERR_PHY;
			desc->ds_us.rx.rs_phyerr =
			    AR5K_REG_MS(rx_status->rx_status_1,
			    AR5K_AR5211_DESC_RX_STATUS1_PHY_ERROR);
		}

		if (rx_status->rx_status_1 &
		    AR5K_AR5211_DESC_RX_STATUS1_DECRYPT_CRC_ERROR)
			desc->ds_us.rx.rs_status |= HAL_RXERR_DECRYPT;
	}

	return (HAL_OK);
}

void
ar5k_ar5211_rxMonitor(hal)
	struct ath_hal *hal;
{
	AR5K_REG_ENABLE_BITS(AR5K_AR5211_RX_FILTER,
	    AR5K_AR5211_RX_FILTER_PROMISC);
}

/*
 * Misc functions
 */

void
ar5k_ar5211_dumpState(hal)
	struct ath_hal *hal;
{
#ifdef AR5K_DEBUG
#define AR5K_PRINT_REGISTER(_x)						\
	printf("(%s: %08x) ", #_x, AR5K_REG_READ(AR5K_AR5211_##_x));

	printf("MAC registers:\n");
	AR5K_PRINT_REGISTER(CR);
	AR5K_PRINT_REGISTER(CFG);
	AR5K_PRINT_REGISTER(IER);
	AR5K_PRINT_REGISTER(RTSD0);
	AR5K_PRINT_REGISTER(TXCFG);
	AR5K_PRINT_REGISTER(RXCFG);
	AR5K_PRINT_REGISTER(RXJLA);
	AR5K_PRINT_REGISTER(MIBC);
	AR5K_PRINT_REGISTER(TOPS);
	AR5K_PRINT_REGISTER(RXNOFRM);
	AR5K_PRINT_REGISTER(RPGTO);
	AR5K_PRINT_REGISTER(RFCNT);
	AR5K_PRINT_REGISTER(MISC);
	AR5K_PRINT_REGISTER(PISR);
	AR5K_PRINT_REGISTER(SISR0);
	AR5K_PRINT_REGISTER(SISR1);
	AR5K_PRINT_REGISTER(SISR3);
	AR5K_PRINT_REGISTER(SISR4);
	AR5K_PRINT_REGISTER(QCU_TXE);
	AR5K_PRINT_REGISTER(QCU_TXD);
	AR5K_PRINT_REGISTER(DCU_GBL_IFS_SIFS);
	AR5K_PRINT_REGISTER(DCU_GBL_IFS_SLOT);
	AR5K_PRINT_REGISTER(DCU_FP);
	AR5K_PRINT_REGISTER(DCU_TXP);
	AR5K_PRINT_REGISTER(DCU_TX_FILTER);
	AR5K_PRINT_REGISTER(RC);
	AR5K_PRINT_REGISTER(SCR);
	AR5K_PRINT_REGISTER(INTPEND);
	AR5K_PRINT_REGISTER(PCICFG);
	AR5K_PRINT_REGISTER(GPIOCR);
	AR5K_PRINT_REGISTER(GPIODO);
	AR5K_PRINT_REGISTER(SREV);
	AR5K_PRINT_REGISTER(EEPROM_BASE);
	AR5K_PRINT_REGISTER(EEPROM_DATA);
	AR5K_PRINT_REGISTER(EEPROM_CMD);
	AR5K_PRINT_REGISTER(EEPROM_CFG);
	AR5K_PRINT_REGISTER(PCU_MIN);
	AR5K_PRINT_REGISTER(STA_ID0);
	AR5K_PRINT_REGISTER(STA_ID1);
	AR5K_PRINT_REGISTER(BSS_ID0);
	AR5K_PRINT_REGISTER(SLOT_TIME);
	AR5K_PRINT_REGISTER(TIME_OUT);
	AR5K_PRINT_REGISTER(RSSI_THR);
	AR5K_PRINT_REGISTER(BEACON);
	AR5K_PRINT_REGISTER(CFP_PERIOD);
	AR5K_PRINT_REGISTER(TIMER0);
	AR5K_PRINT_REGISTER(TIMER2);
	AR5K_PRINT_REGISTER(TIMER3);
	AR5K_PRINT_REGISTER(CFP_DUR);
	AR5K_PRINT_REGISTER(MCAST_FIL0);
	AR5K_PRINT_REGISTER(MCAST_FIL1);
	AR5K_PRINT_REGISTER(DIAG_SW);
	AR5K_PRINT_REGISTER(TSF_U32);
	AR5K_PRINT_REGISTER(ADDAC_TEST);
	AR5K_PRINT_REGISTER(DEFAULT_ANTENNA);
	AR5K_PRINT_REGISTER(LAST_TSTP);
	AR5K_PRINT_REGISTER(NAV);
	AR5K_PRINT_REGISTER(RTS_OK);
	AR5K_PRINT_REGISTER(ACK_FAIL);
	AR5K_PRINT_REGISTER(FCS_FAIL);
	AR5K_PRINT_REGISTER(BEACON_CNT);
	AR5K_PRINT_REGISTER(KEYTABLE_0);
	printf("\n");

	printf("PHY registers:\n");
	AR5K_PRINT_REGISTER(PHY_TURBO);
	AR5K_PRINT_REGISTER(PHY_AGC);
	AR5K_PRINT_REGISTER(PHY_CHIP_ID);
	AR5K_PRINT_REGISTER(PHY_AGCCTL);
	AR5K_PRINT_REGISTER(PHY_NF);
	AR5K_PRINT_REGISTER(PHY_RX_DELAY);
	AR5K_PRINT_REGISTER(PHY_IQ);
	AR5K_PRINT_REGISTER(PHY_PAPD_PROBE);
	AR5K_PRINT_REGISTER(PHY_FC);
	AR5K_PRINT_REGISTER(PHY_RADAR);
	AR5K_PRINT_REGISTER(PHY_ANT_SWITCH_TABLE_0);
	AR5K_PRINT_REGISTER(PHY_ANT_SWITCH_TABLE_1);
	printf("\n");
#endif
}

HAL_BOOL
ar5k_ar5211_getDiagState(hal, id, device, size)
	struct ath_hal *hal;
	int id;
	void **device;
	u_int *size;

{
	/*
	 * We'll ignore this right now. This seems to be some kind of an obscure
	 * debugging interface for the binary-only HAL.
	 */
	return (AH_FALSE);
}

void
ar5k_ar5211_getMacAddress(hal, mac)
	struct ath_hal *hal;
	u_int8_t *mac;
{
	bcopy(hal->ah_sta_id, mac, IEEE80211_ADDR_LEN);
}

HAL_BOOL
ar5k_ar5211_setMacAddress(hal, mac)
	struct ath_hal *hal;
	const u_int8_t *mac;
{
	u_int32_t low_id, high_id;

	/* Set new station ID */
	bcopy(mac, hal->ah_sta_id, IEEE80211_ADDR_LEN);

	bcopy(mac, &low_id, 4);
	bcopy(mac + 4, &high_id, 2);
	high_id = 0x0000ffff & htole32(high_id);

	AR5K_REG_WRITE(AR5K_AR5211_STA_ID0, htole32(low_id));
	AR5K_REG_WRITE(AR5K_AR5211_STA_ID1, high_id);

	return (AH_TRUE);
}

HAL_BOOL
ar5k_ar5211_setRegulatoryDomain(hal, regdomain, status)
	struct ath_hal *hal;
	u_int16_t regdomain;
	HAL_STATUS *status;

{
	ieee80211_regdomain_t ieee_regdomain;

	ieee_regdomain = ar5k_regdomain_to_ieee(regdomain);

	if (ar5k_eeprom_regulation_domain(hal, AH_TRUE,
		&ieee_regdomain) == AH_TRUE) {
		*status = HAL_OK;
		return (AH_TRUE);
	}

	*status = EIO;

	return (AH_FALSE);
}

void
ar5k_ar5211_setLedState(hal, state)
	struct ath_hal *hal;
	HAL_LED_STATE state;
{
	u_int32_t led;

	AR5K_REG_DISABLE_BITS(AR5K_AR5211_PCICFG,
	    AR5K_AR5211_PCICFG_LEDMODE |  AR5K_AR5211_PCICFG_LED);

	/*
	 * Some blinking values, define at your wish
	 */
	switch (state) {
	case IEEE80211_S_SCAN:
	case IEEE80211_S_AUTH:
		led = AR5K_AR5211_PCICFG_LEDMODE_PROP |
		    AR5K_AR5211_PCICFG_LED_PEND;
		break;

	case IEEE80211_S_INIT:
		led = AR5K_AR5211_PCICFG_LEDMODE_PROP |
		    AR5K_AR5211_PCICFG_LED_NONE;
		break;

	case IEEE80211_S_ASSOC:
	case IEEE80211_S_RUN:
		led = AR5K_AR5211_PCICFG_LEDMODE_PROP |
		    AR5K_AR5211_PCICFG_LED_ASSOC;
		break;

	default:
		led = AR5K_AR5211_PCICFG_LEDMODE_PROM |
		    AR5K_AR5211_PCICFG_LED_NONE;
		break;
	}

	AR5K_REG_ENABLE_BITS(AR5K_AR5211_PCICFG, led);
}

void
ar5k_ar5211_writeAssocid(hal, bssid, assoc_id, tim_offset)
	struct ath_hal *hal;
	const u_int8_t *bssid;
	u_int16_t assoc_id;
	u_int16_t tim_offset;
{
	u_int32_t low_id, high_id;

	/*
	 * Set BSSID which triggers the "SME Join" operation
	 */
	bcopy(bssid, &low_id, 4);
	bcopy(bssid + 4, &high_id, 2);
	AR5K_REG_WRITE(AR5K_AR5211_BSS_ID0, htole32(low_id));
	AR5K_REG_WRITE(AR5K_AR5211_BSS_ID1, htole32(high_id) |
	    ((assoc_id & 0x3fff) << AR5K_AR5211_BSS_ID1_AID_S));
	bcopy(bssid, hal->ah_bssid, IEEE80211_ADDR_LEN);

	if (assoc_id == 0) {
		ar5k_ar5211_disablePSPoll(hal);
		return;
	}

	AR5K_REG_WRITE(AR5K_AR5211_BEACON,
	    (AR5K_REG_READ(AR5K_AR5211_BEACON) &
	    ~AR5K_AR5211_BEACON_TIM) |
	    (((tim_offset ? tim_offset + 4 : 0) <<
	    AR5K_AR5211_BEACON_TIM_S) &
	    AR5K_AR5211_BEACON_TIM));

	ar5k_ar5211_enablePSPoll(hal, NULL, 0);
}

HAL_BOOL
ar5k_ar5211_gpioCfgOutput(hal, gpio)
	struct ath_hal *hal;
	u_int32_t gpio;
{
	if (gpio > AR5K_AR5211_NUM_GPIO)
		return (AH_FALSE);

	AR5K_REG_WRITE(AR5K_AR5211_GPIOCR,
	    (AR5K_REG_READ(AR5K_AR5211_GPIOCR) &~ AR5K_AR5211_GPIOCR_ALL(gpio))
	    | AR5K_AR5211_GPIOCR_ALL(gpio));

	return (AH_TRUE);
}

HAL_BOOL
ar5k_ar5211_gpioCfgInput(hal, gpio)
	struct ath_hal *hal;
	u_int32_t gpio;
{
	if (gpio > AR5K_AR5211_NUM_GPIO)
		return (AH_FALSE);

	AR5K_REG_WRITE(AR5K_AR5211_GPIOCR,
	    (AR5K_REG_READ(AR5K_AR5211_GPIOCR) &~ AR5K_AR5211_GPIOCR_ALL(gpio))
	    | AR5K_AR5211_GPIOCR_NONE(gpio));

	return (AH_TRUE);
}

u_int32_t
ar5k_ar5211_gpioGet(hal, gpio)
	struct ath_hal *hal;
	u_int32_t gpio;
{
	if (gpio > AR5K_AR5211_NUM_GPIO)
		return (0xffffffff);

	/* GPIO input magic */
	return (((AR5K_REG_READ(AR5K_AR5211_GPIODI) &
	    AR5K_AR5211_GPIODI_M) >> gpio) & 0x1);
}

HAL_BOOL
ar5k_ar5211_gpioSet(hal, gpio, val)
	struct ath_hal *hal;
	u_int32_t gpio;
	u_int32_t val;
{
	u_int32_t data;

	if (gpio > AR5K_AR5211_NUM_GPIO)
		return (0xffffffff);

	/* GPIO output magic */
	data =  AR5K_REG_READ(AR5K_AR5211_GPIODO);

	data &= ~(1 << gpio);
	data |= (val&1) << gpio;

	AR5K_REG_WRITE(AR5K_AR5211_GPIODO, data);

	return (AH_TRUE);
}

void
ar5k_ar5211_gpioSetIntr(hal, gpio, interrupt_level)
	struct ath_hal *hal;
	u_int gpio;
	u_int32_t interrupt_level;
{
	u_int32_t data;

	if (gpio > AR5K_AR5211_NUM_GPIO)
		return;

	/*
	 * Set the GPIO interrupt
	 */
	data = (AR5K_REG_READ(AR5K_AR5211_GPIOCR) &
	    ~(AR5K_AR5211_GPIOCR_INT_SEL(gpio) | AR5K_AR5211_GPIOCR_INT_SELH |
	    AR5K_AR5211_GPIOCR_INT_ENA | AR5K_AR5211_GPIOCR_ALL(gpio))) |
	    (AR5K_AR5211_GPIOCR_INT_SEL(gpio) | AR5K_AR5211_GPIOCR_INT_ENA);

	AR5K_REG_WRITE(AR5K_AR5211_GPIOCR,
	    interrupt_level ? data : (data | AR5K_AR5211_GPIOCR_INT_SELH));

	hal->ah_imr |= AR5K_AR5211_PIMR_GPIO;

	/* Enable GPIO interrupts */
	AR5K_REG_ENABLE_BITS(AR5K_AR5211_PIMR, AR5K_AR5211_PIMR_GPIO);
}

u_int32_t
ar5k_ar5211_getTsf32(hal)
	struct ath_hal *hal;
{
	return (AR5K_REG_READ(AR5K_AR5211_TSF_L32));
}

u_int64_t
ar5k_ar5211_getTsf64(hal)
	struct ath_hal *hal;
{
	u_int64_t tsf = AR5K_REG_READ(AR5K_AR5211_TSF_U32);

	return (AR5K_REG_READ(AR5K_AR5211_TSF_L32) | (tsf << 32));
}

void
ar5k_ar5211_resetTsf(hal)
	struct ath_hal *hal;
{
	AR5K_REG_ENABLE_BITS(AR5K_AR5211_BEACON,
	    AR5K_AR5211_BEACON_RESET_TSF);
}

u_int16_t
ar5k_ar5211_getRegDomain(hal)
	struct ath_hal *hal;
{
	return (ar5k_get_regdomain(hal));
}

HAL_BOOL
ar5k_ar5211_detectCardPresent(hal)
	struct ath_hal *hal;
{
	u_int16_t magic;

	/*
	 * Checking the EEPROM's magic value could be an indication
	 * if the card is still present. I didn't find another suitable
	 * way to do this.
	 */
	if (ar5k_ar5211_eeprom_read(hal, AR5K_EEPROM_MAGIC, &magic) != 0)
		return (AH_FALSE);

	return (magic == AR5K_EEPROM_MAGIC_VALUE ? AH_TRUE : AH_FALSE);
}

void
ar5k_ar5211_updateMibCounters(hal, statistics)
	struct ath_hal *hal;
	HAL_MIB_STATS *statistics;
{
	statistics->ackrcv_bad += AR5K_REG_READ(AR5K_AR5211_ACK_FAIL);
	statistics->rts_bad += AR5K_REG_READ(AR5K_AR5211_RTS_FAIL);
	statistics->rts_good += AR5K_REG_READ(AR5K_AR5211_RTS_OK);
	statistics->fcs_bad += AR5K_REG_READ(AR5K_AR5211_FCS_FAIL);
	statistics->beacons += AR5K_REG_READ(AR5K_AR5211_BEACON_CNT);
}

HAL_RFGAIN
ar5k_ar5211_getRfGain(hal)
	struct ath_hal *hal;
{
	return (HAL_RFGAIN_INACTIVE);
}

HAL_BOOL
ar5k_ar5211_setSlotTime(hal, slot_time)
	struct ath_hal *hal;
	u_int slot_time;
{
	if (slot_time < HAL_SLOT_TIME_9 || slot_time > HAL_SLOT_TIME_MAX)
		return (AH_FALSE);

	AR5K_REG_WRITE(AR5K_AR5211_DCU_GBL_IFS_SLOT,
	    ar5k_htoclock(slot_time, hal->ah_turbo));

	return (AH_TRUE);
}

u_int
ar5k_ar5211_getSlotTime(hal)
	struct ath_hal *hal;
{
	return (ar5k_clocktoh(AR5K_REG_READ(AR5K_AR5211_DCU_GBL_IFS_SLOT) &
	    0xffff, hal->ah_turbo));
}

HAL_BOOL
ar5k_ar5211_setAckTimeout(hal, timeout)
	struct ath_hal *hal;
	u_int timeout;
{
	if (ar5k_clocktoh(AR5K_REG_MS(0xffffffff, AR5K_AR5211_TIME_OUT_ACK),
	    hal->ah_turbo) <= timeout)
		return (AH_FALSE);

	AR5K_REG_WRITE_BITS(AR5K_AR5211_TIME_OUT, AR5K_AR5211_TIME_OUT_ACK,
	    ar5k_htoclock(timeout, hal->ah_turbo));

	return (AH_TRUE);
}

u_int
ar5k_ar5211_getAckTimeout(hal)
	struct ath_hal *hal;
{
	return (ar5k_clocktoh(AR5K_REG_MS(AR5K_REG_READ(AR5K_AR5211_TIME_OUT),
	    AR5K_AR5211_TIME_OUT_ACK), hal->ah_turbo));
}

HAL_BOOL
ar5k_ar5211_setCTSTimeout(hal, timeout)
	struct ath_hal *hal;
	u_int timeout;
{
	if (ar5k_clocktoh(AR5K_REG_MS(0xffffffff, AR5K_AR5211_TIME_OUT_CTS),
	    hal->ah_turbo) <= timeout)
		return (AH_FALSE);

	AR5K_REG_WRITE_BITS(AR5K_AR5211_TIME_OUT, AR5K_AR5211_TIME_OUT_CTS,
	    ar5k_htoclock(timeout, hal->ah_turbo));

	return (AH_TRUE);
}

u_int
ar5k_ar5211_getCTSTimeout(hal)
	struct ath_hal *hal;
{
	return (ar5k_clocktoh(AR5K_REG_MS(AR5K_REG_READ(AR5K_AR5211_TIME_OUT),
	    AR5K_AR5211_TIME_OUT_CTS), hal->ah_turbo));
}

/*
 * Key table (WEP) functions
 */

HAL_BOOL
ar5k_ar5211_isHwCipherSupported(hal, cipher)
	struct ath_hal *hal;
	HAL_CIPHER cipher;
{
	/*
	 * The AR5211 only supports WEP
	 */
	if (cipher == HAL_CIPHER_WEP)
		return (AH_TRUE);

	return (AH_FALSE);
}

u_int32_t
ar5k_ar5211_getKeyCacheSize(hal)
	struct ath_hal *hal;
{
	return (AR5K_AR5211_KEYCACHE_SIZE);
}

HAL_BOOL
ar5k_ar5211_resetKeyCacheEntry(hal, entry)
	struct ath_hal *hal;
	u_int16_t entry;
{
	int i;

	AR5K_ASSERT_ENTRY(entry, AR5K_AR5211_KEYTABLE_SIZE);

	for (i = 0; i < AR5K_AR5211_KEYCACHE_SIZE; i++)
		AR5K_REG_WRITE(AR5K_AR5211_KEYTABLE_OFF(entry, i), 0);

	return (AH_FALSE);
}

HAL_BOOL
ar5k_ar5211_isKeyCacheEntryValid(hal, entry)
	struct ath_hal *hal;
	u_int16_t entry;
{
	AR5K_ASSERT_ENTRY(entry, AR5K_AR5211_KEYTABLE_SIZE);

	/*
	 * Check the validation flag at the end of the entry
	 */
	if (AR5K_REG_READ(AR5K_AR5211_KEYTABLE_MAC1(entry)) &
	    AR5K_AR5211_KEYTABLE_VALID)
		return (AH_TRUE);

	return (AH_FALSE);
}

HAL_BOOL
ar5k_ar5211_setKeyCacheEntry(hal, entry, keyval, mac, xor_notused)
	struct ath_hal *hal;
	u_int16_t entry;
	const HAL_KEYVAL *keyval;
	const u_int8_t *mac;
	int xor_notused;
{
	int elements = AR5K_AR5211_KEYCACHE_SIZE - 2, i;
	u_int32_t key_v[elements];

	AR5K_ASSERT_ENTRY(entry, AR5K_AR5211_KEYTABLE_SIZE);

	bzero(&key_v, sizeof(key_v));

	switch (keyval->wk_len) {
	case AR5K_KEYVAL_LENGTH_40:
		bcopy(keyval->wk_key, &key_v[0], 4);
		bcopy(keyval->wk_key + 4, &key_v[1], 1);
		key_v[5] = AR5K_AR5211_KEYTABLE_TYPE_40;
		break;

	case AR5K_KEYVAL_LENGTH_104:
		bcopy(keyval->wk_key, &key_v[0], 4);
		bcopy(keyval->wk_key + 4, &key_v[1], 2);
		bcopy(keyval->wk_key + 6, &key_v[2], 4);
		bcopy(keyval->wk_key + 10, &key_v[3], 2);
		bcopy(keyval->wk_key + 12, &key_v[4], 1);
		key_v[5] = AR5K_AR5211_KEYTABLE_TYPE_104;
		break;

	case AR5K_KEYVAL_LENGTH_128:
		bcopy(keyval->wk_key, &key_v[0], 4);
		bcopy(keyval->wk_key + 4, &key_v[1], 2);
		bcopy(keyval->wk_key + 6, &key_v[2], 4);
		bcopy(keyval->wk_key + 10, &key_v[3], 2);
		bcopy(keyval->wk_key + 12, &key_v[4], 4);
		key_v[5] = AR5K_AR5211_KEYTABLE_TYPE_128;
		break;

	default:
		/* Unsupported key length (not WEP40/104/128) */
		return (AH_FALSE);
	}

	for (i = 0; i < elements; i++)
		AR5K_REG_WRITE(AR5K_AR5211_KEYTABLE_OFF(entry, i), key_v[i]);

	return (ar5k_ar5211_setKeyCacheEntryMac(hal, entry, mac));
}

HAL_BOOL
ar5k_ar5211_setKeyCacheEntryMac(hal, entry, mac)
	struct ath_hal *hal;
	u_int16_t entry;
	const u_int8_t *mac;
{
	u_int32_t low_id, high_id;
	const u_int8_t *mac_v;

	/*
	 * Invalid entry (key table overflow)
	 */
	AR5K_ASSERT_ENTRY(entry, AR5K_AR5211_KEYTABLE_SIZE);

	/* MAC may be NULL if it's a broadcast key */
	mac_v = mac == NULL ? etherbroadcastaddr : mac;

	bcopy(mac_v, &low_id, 4);
	bcopy(mac_v + 4, &high_id, 2);
	high_id |= AR5K_AR5211_KEYTABLE_VALID;

	AR5K_REG_WRITE(AR5K_AR5211_KEYTABLE_MAC0(entry), htole32(low_id));
	AR5K_REG_WRITE(AR5K_AR5211_KEYTABLE_MAC1(entry), htole32(high_id));

	return (AH_TRUE);
}

/*
 * Power management functions
 */

HAL_BOOL
ar5k_ar5211_setPowerMode(hal, mode, set_chip, sleep_duration)
	struct ath_hal *hal;
	HAL_POWER_MODE mode;
	HAL_BOOL set_chip;
	u_int16_t sleep_duration;
{
	int i;

	switch (mode) {
	case HAL_PM_AUTO:
		if (set_chip == AH_TRUE) {
			AR5K_REG_WRITE(AR5K_AR5211_SCR,
			    AR5K_AR5211_SCR_SLE | sleep_duration);
		}
		break;

	case HAL_PM_FULL_SLEEP:
		if (set_chip == AH_TRUE) {
			AR5K_REG_WRITE(AR5K_AR5211_SCR,
			    AR5K_AR5211_SCR_SLE_SLP);
		}
		break;

	case HAL_PM_AWAKE:
		if (set_chip == AH_FALSE)
			goto commit;

		AR5K_REG_WRITE(AR5K_AR5211_SCR, AR5K_AR5211_SCR_SLE_WAKE);

		for (i = 5000; i > 0; i--) {
			/* Check if the AR5211 did wake up */
			if ((AR5K_REG_READ(AR5K_AR5211_PCICFG) &
			    AR5K_AR5211_PCICFG_SPWR_DN) == 0)
				break;

			/* Wait a bit and retry */
			AR5K_DELAY(200);
			AR5K_REG_WRITE(AR5K_AR5211_SCR,
			    AR5K_AR5211_SCR_SLE_WAKE);
		}

		/* Fail if the AR5211 didn't wake up */
		if (i <= 0)
			return (AH_FALSE);
		break;

	case HAL_PM_NETWORK_SLEEP:
	case HAL_PM_UNDEFINED:
	default:
		return (AH_FALSE);
	}

 commit:
	hal->ah_power_mode = mode;

	AR5K_REG_DISABLE_BITS(AR5K_AR5211_STA_ID1,
	    AR5K_AR5211_STA_ID1_DEFAULT_ANTENNA);
	AR5K_REG_ENABLE_BITS(AR5K_AR5211_STA_ID1,
	    AR5K_AR5211_STA_ID1_PWR_SV);

	return (AH_TRUE);
}

HAL_POWER_MODE
ar5k_ar5211_getPowerMode(hal)
	struct ath_hal *hal;
{
	return (hal->ah_power_mode);
}

HAL_BOOL
ar5k_ar5211_queryPSPollSupport(hal)
	struct ath_hal *hal;
{
	/* nope */
	return (AH_FALSE);
}

HAL_BOOL
ar5k_ar5211_initPSPoll(hal)
	struct ath_hal *hal;
{
	/*
	 * Not used on the AR5211
	 */
	return (AH_FALSE);
}

HAL_BOOL
ar5k_ar5211_enablePSPoll(hal, bssid, assoc_id)
	struct ath_hal *hal;
	u_int8_t *bssid;
	u_int16_t assoc_id;
{
	return (AH_FALSE);
}

HAL_BOOL
ar5k_ar5211_disablePSPoll(hal)
	struct ath_hal *hal;
{
	return (AH_FALSE);
}

/*
 * Beacon functions
 */

void
ar5k_ar5211_beaconInit(hal, next_beacon, interval)
	struct ath_hal *hal;
	u_int32_t next_beacon;
	u_int32_t interval;
{
	u_int32_t timer1, timer2, timer3;

	/*
	 * Set the additional timers by mode
	 */
	switch (hal->ah_op_mode) {
	case HAL_M_STA:
		timer1 = 0x0000ffff;
		timer2 = 0x0007ffff;
		break;

	default:
		timer1 = (next_beacon - AR5K_TUNE_DMA_BEACON_RESP) <<
		    0x00000003;
		timer2 = (next_beacon - AR5K_TUNE_SW_BEACON_RESP) <<
		    0x00000003;
	}

	timer3 = next_beacon +
	    (hal->ah_atim_window ? hal->ah_atim_window : 1);

	/*
	 * Enable all timers and set the beacon register
	 * (next beacon, DMA beacon, software beacon, ATIM window time)
	 */
	AR5K_REG_WRITE(AR5K_AR5211_TIMER0, next_beacon);
	AR5K_REG_WRITE(AR5K_AR5211_TIMER1, timer1);
	AR5K_REG_WRITE(AR5K_AR5211_TIMER2, timer2);
	AR5K_REG_WRITE(AR5K_AR5211_TIMER3, timer3);

	AR5K_REG_WRITE(AR5K_AR5211_BEACON, interval &
	    (AR5K_AR5211_BEACON_PERIOD | AR5K_AR5211_BEACON_RESET_TSF |
	    AR5K_AR5211_BEACON_ENABLE));
}

void
ar5k_ar5211_setStationBeaconTimers(hal, state, tsf, dtim_count, cfp_count)
	struct ath_hal *hal;
	const HAL_BEACON_STATE *state;
	u_int32_t tsf;
	u_int32_t dtim_count;
	u_int32_t cfp_count;

{
	u_int32_t cfp_period, next_cfp;

	/* Return on an invalid beacon state */
	if (state->bs_interval < 1)
		return;

	/*
	 * PCF support?
	 */
	if (state->bs_cfp_period > 0) {
		/* Enable CFP mode and set the CFP and timer registers */
		cfp_period = state->bs_cfp_period * state->bs_dtim_period *
		    state->bs_interval;
		next_cfp = (cfp_count * state->bs_dtim_period + dtim_count) *
		    state->bs_interval;

		AR5K_REG_DISABLE_BITS(AR5K_AR5211_STA_ID1,
		    AR5K_AR5211_STA_ID1_DEFAULT_ANTENNA |
		    AR5K_AR5211_STA_ID1_PCF);
		AR5K_REG_WRITE(AR5K_AR5211_CFP_PERIOD, cfp_period);
		AR5K_REG_WRITE(AR5K_AR5211_CFP_DUR, state->bs_cfp_max_duration);
		AR5K_REG_WRITE(AR5K_AR5211_TIMER2,
		    (tsf + (next_cfp == 0 ? cfp_period : next_cfp)) << 3);
	} else {
		/* Disable PCF mode */
		AR5K_REG_DISABLE_BITS(AR5K_AR5211_STA_ID1,
		    AR5K_AR5211_STA_ID1_DEFAULT_ANTENNA |
		    AR5K_AR5211_STA_ID1_PCF);
	}

	/*
	 * Enable the beacon timer register
	 */
	AR5K_REG_WRITE(AR5K_AR5211_TIMER0, state->bs_next_beacon);

	/*
	 * Start the beacon timers
	 */
	AR5K_REG_WRITE(AR5K_AR5211_BEACON,
	    (AR5K_REG_READ(AR5K_AR5211_BEACON) &~
	    (AR5K_AR5211_BEACON_PERIOD | AR5K_AR5211_BEACON_TIM)) |
	    AR5K_REG_SM(state->bs_tim_offset ? state->bs_tim_offset + 4 : 0,
	    AR5K_AR5211_BEACON_TIM) | AR5K_REG_SM(state->bs_interval,
	    AR5K_AR5211_BEACON_PERIOD));

	/*
	 * Write new beacon miss threshold, if it appears to be valid
	 */
	if ((AR5K_AR5211_RSSI_THR_BMISS >> AR5K_AR5211_RSSI_THR_BMISS_S) <
	    state->bs_bmiss_threshold)
		return;

	AR5K_REG_WRITE_BITS(AR5K_AR5211_RSSI_THR_M,
	    AR5K_AR5211_RSSI_THR_BMISS, state->bs_bmiss_threshold);
	AR5K_REG_WRITE_BITS(AR5K_AR5211_SCR, AR5K_AR5211_SCR_SLDUR,
	    (state->bs_sleepduration - 3) << 3);
}

void
ar5k_ar5211_resetStationBeaconTimers(hal)
	struct ath_hal *hal;
{
	/*
	 * Disable beacon timer
	 */
	AR5K_REG_WRITE(AR5K_AR5211_TIMER0, 0);

	/*
	 * Disable some beacon register values
	 */
	AR5K_REG_DISABLE_BITS(AR5K_AR5211_STA_ID1,
	    AR5K_AR5211_STA_ID1_DEFAULT_ANTENNA | AR5K_AR5211_STA_ID1_PCF);
	AR5K_REG_WRITE(AR5K_AR5211_BEACON, AR5K_AR5211_BEACON_PERIOD);
}

HAL_BOOL
ar5k_ar5211_waitForBeaconDone(hal, phys_addr)
	struct ath_hal *hal;
	bus_addr_t phys_addr;
{
	HAL_BOOL ret;

	/*
	 * Wait for beaconn queue to be done
	 */
	ret = ar5k_register_timeout(hal,
	    AR5K_AR5211_QCU_STS(HAL_TX_QUEUE_ID_BEACON),
	    AR5K_AR5211_QCU_STS_FRMPENDCNT, 0, AH_FALSE);

	if (AR5K_REG_READ_Q(AR5K_AR5211_QCU_TXE, HAL_TX_QUEUE_ID_BEACON))
		return (AH_FALSE);

	return (ret);
}

/*
 * Interrupt handling
 */

HAL_BOOL
ar5k_ar5211_isInterruptPending(hal)
	struct ath_hal *hal;
{
	return (AR5K_REG_READ(AR5K_AR5211_INTPEND) == 0 ? AH_FALSE : AH_TRUE);
}

HAL_BOOL
ar5k_ar5211_getPendingInterrupts(hal, interrupt_mask)
	struct ath_hal *hal;
	u_int32_t *interrupt_mask;
{
	u_int32_t data;

	/*
	 * Read interrupt status from the Read-And-Clear shadow register
	 */
	data = AR5K_REG_READ(AR5K_AR5211_RAC_PISR);

	/*
	 * Get abstract interrupt mask (HAL-compatible)
	 */
	*interrupt_mask = (data & HAL_INT_COMMON) & hal->ah_imr;

	if (data == HAL_INT_NOCARD)
		return (AH_FALSE);

	if (data & (AR5K_AR5211_PISR_RXOK | AR5K_AR5211_PISR_RXERR))
		*interrupt_mask |= HAL_INT_RX;

	if (data & (AR5K_AR5211_PISR_TXOK | AR5K_AR5211_PISR_TXERR))
		*interrupt_mask |= HAL_INT_TX;

	if (data & (AR5K_AR5211_PISR_HIUERR))
		*interrupt_mask |= HAL_INT_FATAL;

	/*
	 * Special interrupt handling (not catched by the driver)
	 */
	if (((*interrupt_mask) & AR5K_AR5211_PISR_RXPHY) &&
	    hal->ah_radar.r_enabled == AH_TRUE)
		ar5k_radar_alert(hal);

	return (AH_TRUE);
}

u_int32_t
ar5k_ar5211_getInterrupts(hal)
	struct ath_hal *hal;
{
	/* Return the interrupt mask stored previously */
	return (hal->ah_imr);
}

HAL_INT
ar5k_ar5211_setInterrupts(hal, new_mask)
	struct ath_hal *hal;
	HAL_INT new_mask;
{
	HAL_INT old_mask, int_mask;

	/*
	 * Disable card interrupts to prevent any race conditions
	 * (they will be re-enabled afterwards).
	 */
	AR5K_REG_WRITE(AR5K_AR5211_IER, AR5K_AR5211_IER_DISABLE);

	old_mask = hal->ah_imr;

	/*
	 * Add additional, chipset-dependent interrupt mask flags
	 * and write them to the IMR (interrupt mask register).
	 */
	int_mask = new_mask & HAL_INT_COMMON;

	if (new_mask & HAL_INT_RX)
		int_mask |=
		    AR5K_AR5211_PIMR_RXOK |
		    AR5K_AR5211_PIMR_RXERR |
		    AR5K_AR5211_PIMR_RXORN |
		    AR5K_AR5211_PIMR_RXDESC;

	if (new_mask & HAL_INT_TX)
		int_mask |=
		    AR5K_AR5211_PIMR_TXOK |
		    AR5K_AR5211_PIMR_TXERR |
		    AR5K_AR5211_PIMR_TXDESC |
		    AR5K_AR5211_PIMR_TXURN;

	if (new_mask & HAL_INT_FATAL) {
		int_mask |= AR5K_AR5211_PIMR_HIUERR;
		AR5K_REG_ENABLE_BITS(AR5K_AR5211_SIMR2,
		    AR5K_AR5211_SIMR2_MCABT |
		    AR5K_AR5211_SIMR2_SSERR |
		    AR5K_AR5211_SIMR2_DPERR);
	}

	if (hal->ah_op_mode & HAL_M_HOSTAP) {
		int_mask |= AR5K_AR5211_PIMR_MIB;
	} else {
		int_mask &= ~AR5K_AR5211_PIMR_MIB;
	}

	AR5K_REG_WRITE(AR5K_AR5211_PIMR, int_mask);

	/* Store new interrupt mask */
	hal->ah_imr = new_mask;

	/* ..re-enable interrupts */
	AR5K_REG_WRITE(AR5K_AR5211_IER, AR5K_AR5211_IER_ENABLE);

	return (old_mask);
}

/*
 * Misc internal functions
 */

HAL_BOOL
ar5k_ar5211_get_capabilities(hal)
	struct ath_hal *hal;
{
	u_int16_t ee_header;

	/* Capabilities stored in the EEPROM */
	ee_header = hal->ah_capabilities.cap_eeprom.ee_header;

	/*
	 * XXX The AR5211 tranceiver supports frequencies from 4920 to 6100GHz
	 * XXX and from 2312 to 2732GHz. There are problems with the current
	 * XXX ieee80211 implementation because the IEEE channel mapping
	 * XXX does not support negative channel numbers (2312MHz is channel
	 * XXX -19). Of course, this doesn't matter because these channels
	 * XXX are out of range but some regulation domains like MKK (Japan)
	 * XXX will support frequencies somewhere around 4.8GHz.
	 */

	/*
	 * Set radio capabilities
	 */

	if (AR5K_EEPROM_HDR_11A(ee_header)) {
		hal->ah_capabilities.cap_range.range_5ghz_min = 5005; /* 4920 */
		hal->ah_capabilities.cap_range.range_5ghz_max = 6100;

		/* Set supported modes */
		hal->ah_capabilities.cap_mode = HAL_MODE_11A | HAL_MODE_TURBO;
	}

	/* This chip will support 802.11b if the 2GHz radio is connected */
	if (AR5K_EEPROM_HDR_11B(ee_header) || AR5K_EEPROM_HDR_11G(ee_header)) {
		hal->ah_capabilities.cap_range.range_2ghz_min = 2412; /* 2312 */
		hal->ah_capabilities.cap_range.range_2ghz_max = 2732;
		hal->ah_capabilities.cap_mode |= HAL_MODE_11B;

		if (AR5K_EEPROM_HDR_11B(ee_header))
			hal->ah_capabilities.cap_mode |= HAL_MODE_11B;
		if (AR5K_EEPROM_HDR_11G(ee_header))
			hal->ah_capabilities.cap_mode |= HAL_MODE_11G;
	}

	/* GPIO */
	hal->ah_gpio_npins = AR5K_AR5211_NUM_GPIO;

	/* Set number of supported TX queues */
	hal->ah_capabilities.cap_queues.q_tx_num = AR5K_AR5211_TX_NUM_QUEUES;

	return (AH_TRUE);
}

void
ar5k_ar5211_radar_alert(hal, enable)
	struct ath_hal *hal;
	HAL_BOOL enable;
{
	/*
	 * Enable radar detection
	 */
	AR5K_REG_WRITE(AR5K_AR5211_IER, AR5K_AR5211_IER_DISABLE);

	if (enable == AH_TRUE) {
		AR5K_REG_WRITE(AR5K_AR5211_PHY_RADAR,
		    AR5K_AR5211_PHY_RADAR_ENABLE);
		AR5K_REG_ENABLE_BITS(AR5K_AR5211_PIMR,
		    AR5K_AR5211_PIMR_RXPHY);
	} else {
		AR5K_REG_WRITE(AR5K_AR5211_PHY_RADAR,
		    AR5K_AR5211_PHY_RADAR_DISABLE);
		AR5K_REG_DISABLE_BITS(AR5K_AR5211_PIMR,
		    AR5K_AR5211_PIMR_RXPHY);
	}

	AR5K_REG_WRITE(AR5K_AR5211_IER, AR5K_AR5211_IER_ENABLE);
}

/*
 * EEPROM access functions
 */

HAL_BOOL
ar5k_ar5211_eeprom_is_busy(hal)
	struct ath_hal *hal;
{
	return (AR5K_REG_READ(AR5K_AR5211_CFG) & AR5K_AR5211_CFG_EEBS ?
	    AH_TRUE : AH_FALSE);
}

int
ar5k_ar5211_eeprom_read(hal, offset, data)
	struct ath_hal *hal;
	u_int32_t offset;
	u_int16_t *data;
{
	u_int32_t status, i;

	/*
	 * Initialize EEPROM access
	 */
	AR5K_REG_WRITE(AR5K_AR5211_EEPROM_BASE, (u_int8_t)offset);
	AR5K_REG_ENABLE_BITS(AR5K_AR5211_EEPROM_CMD,
	    AR5K_AR5211_EEPROM_CMD_READ);

	for (i = AR5K_TUNE_REGISTER_TIMEOUT; i > 0; i--) {
		status = AR5K_REG_READ(AR5K_AR5211_EEPROM_STATUS);
		if (status & AR5K_AR5211_EEPROM_STAT_RDDONE) {
			if (status & AR5K_AR5211_EEPROM_STAT_RDERR)
				return (EIO);
			*data = (u_int16_t)
			    (AR5K_REG_READ(AR5K_AR5211_EEPROM_DATA) & 0xffff);
			return (0);
		}
		AR5K_DELAY(15);
	}

	return (ETIMEDOUT);
}

int
ar5k_ar5211_eeprom_write(hal, offset, data)
	struct ath_hal *hal;
	u_int32_t offset;
	u_int16_t data;
{
	u_int32_t status, timeout;

	/* Enable eeprom access */
	AR5K_REG_ENABLE_BITS(AR5K_AR5211_EEPROM_CMD,
	    AR5K_AR5211_EEPROM_CMD_RESET);
	AR5K_REG_ENABLE_BITS(AR5K_AR5211_EEPROM_CMD,
	    AR5K_AR5211_EEPROM_CMD_WRITE);

	/*
	 * Prime write pump
	 */
	AR5K_REG_WRITE(AR5K_AR5211_EEPROM_BASE, (u_int8_t)offset - 1);

	for (timeout = 10000; timeout > 0; timeout--) {
		AR5K_DELAY(1);
		status = AR5K_REG_READ(AR5K_AR5211_EEPROM_STATUS);
		if (status & AR5K_AR5211_EEPROM_STAT_WRDONE) {
			if (status & AR5K_AR5211_EEPROM_STAT_WRERR)
				return (EIO);
			return (0);
		}
	}

	return (ETIMEDOUT);
}
