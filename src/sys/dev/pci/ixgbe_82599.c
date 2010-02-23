/*	$OpenBSD: ixgbe_82599.c,v 1.1 2010/02/23 18:43:15 jsg Exp $	*/

/******************************************************************************

  Copyright (c) 2001-2009, Intel Corporation 
  All rights reserved.
  
  Redistribution and use in source and binary forms, with or without 
  modification, are permitted provided that the following conditions are met:
  
   1. Redistributions of source code must retain the above copyright notice, 
      this list of conditions and the following disclaimer.
  
   2. Redistributions in binary form must reproduce the above copyright 
      notice, this list of conditions and the following disclaimer in the 
      documentation and/or other materials provided with the distribution.
  
   3. Neither the name of the Intel Corporation nor the names of its 
      contributors may be used to endorse or promote products derived from 
      this software without specific prior written permission.
  
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD: src/sys/dev/ixgbe/ixgbe_82599.c,v 1.3 2009/12/07 21:30:54 jfv Exp $*/

#include <dev/pci/ixgbe.h>
#include <dev/pci/ixgbe_type.h>

int32_t ixgbe_init_ops_82599(struct ixgbe_hw *hw);
int32_t ixgbe_get_link_capabilities_82599(struct ixgbe_hw *hw,
                                      ixgbe_link_speed *speed,
                                      int *autoneg);
enum ixgbe_media_type ixgbe_get_media_type_82599(struct ixgbe_hw *hw);
int32_t ixgbe_setup_mac_link_multispeed_fiber(struct ixgbe_hw *hw,
                                     ixgbe_link_speed speed, int autoneg,
                                     int autoneg_wait_to_complete);
int32_t ixgbe_setup_mac_link_smartspeed(struct ixgbe_hw *hw,
				     ixgbe_link_speed speed, int autoneg,
				     int autoneg_wait_to_complete);
int32_t ixgbe_start_mac_link_82599(struct ixgbe_hw *hw,
				int autoneg_wait_to_complete);
int32_t ixgbe_setup_mac_link_82599(struct ixgbe_hw *hw,
                                     ixgbe_link_speed speed,
                                     int autoneg,
                                     int autoneg_wait_to_complete);
int32_t ixgbe_setup_copper_link_82599(struct ixgbe_hw *hw,
                                               ixgbe_link_speed speed,
                                               int autoneg,
                                               int autoneg_wait_to_complete);
int32_t ixgbe_setup_sfp_modules_82599(struct ixgbe_hw *hw);
void ixgbe_init_mac_link_ops_82599(struct ixgbe_hw *hw);
int32_t ixgbe_reset_hw_82599(struct ixgbe_hw *hw);
int32_t ixgbe_read_analog_reg8_82599(struct ixgbe_hw *hw, uint32_t reg, uint8_t *val);
int32_t ixgbe_write_analog_reg8_82599(struct ixgbe_hw *hw, uint32_t reg, uint8_t val);
int32_t ixgbe_start_hw_rev_1_82599(struct ixgbe_hw *hw);
int32_t ixgbe_identify_phy_82599(struct ixgbe_hw *hw);
int32_t ixgbe_init_phy_ops_82599(struct ixgbe_hw *hw);
uint32_t ixgbe_get_supported_physical_layer_82599(struct ixgbe_hw *hw);
int32_t ixgbe_enable_rx_dma_82599(struct ixgbe_hw *hw, uint32_t regval);
int32_t ixgbe_get_device_caps_82599(struct ixgbe_hw *hw, uint16_t *device_caps);
int32_t ixgbe_verify_fw_version_82599(struct ixgbe_hw *hw);

int32_t ixgbe_reinit_fdir_tables_82599(struct ixgbe_hw *hw);
int32_t ixgbe_init_fdir_signature_82599(struct ixgbe_hw *hw, uint32_t pballoc);
int32_t ixgbe_init_fdir_perfect_82599(struct ixgbe_hw *hw, uint32_t pballoc);
int32_t ixgbe_fdir_add_signature_filter_82599(struct ixgbe_hw *hw,
                                          struct ixgbe_atr_input *input,
                                          uint8_t queue);
int32_t ixgbe_fdir_add_perfect_filter_82599(struct ixgbe_hw *hw,
                                        struct ixgbe_atr_input *input,
                                        uint16_t soft_id,
                                        uint8_t queue);
uint16_t ixgbe_atr_compute_hash_82599(struct ixgbe_atr_input *input, uint32_t key);
int32_t ixgbe_atr_set_vlan_id_82599(struct ixgbe_atr_input *input, uint16_t vlan_id);
int32_t ixgbe_atr_set_src_ipv4_82599(struct ixgbe_atr_input *input, uint32_t src_addr);
int32_t ixgbe_atr_set_dst_ipv4_82599(struct ixgbe_atr_input *input, uint32_t dst_addr);
int32_t ixgbe_atr_set_src_ipv6_82599(struct ixgbe_atr_input *input, uint32_t src_addr_1,
                                 uint32_t src_addr_2, uint32_t src_addr_3,
                                 uint32_t src_addr_4);
int32_t ixgbe_atr_set_dst_ipv6_82599(struct ixgbe_atr_input *input, uint32_t dst_addr_1,
                                 uint32_t dst_addr_2, uint32_t dst_addr_3,
                                 uint32_t dst_addr_4);
int32_t ixgbe_atr_set_src_port_82599(struct ixgbe_atr_input *input, uint16_t src_port);
int32_t ixgbe_atr_set_dst_port_82599(struct ixgbe_atr_input *input, uint16_t dst_port);
int32_t ixgbe_atr_set_flex_byte_82599(struct ixgbe_atr_input *input, uint16_t flex_byte);
int32_t ixgbe_atr_set_vm_pool_82599(struct ixgbe_atr_input *input, uint8_t vm_pool);
int32_t ixgbe_atr_set_l4type_82599(struct ixgbe_atr_input *input, uint8_t l4type);
int32_t ixgbe_atr_get_vlan_id_82599(struct ixgbe_atr_input *input, uint16_t *vlan_id);
int32_t ixgbe_atr_get_src_ipv4_82599(struct ixgbe_atr_input *input, uint32_t *src_addr);
int32_t ixgbe_atr_get_dst_ipv4_82599(struct ixgbe_atr_input *input, uint32_t *dst_addr);
int32_t ixgbe_atr_get_src_ipv6_82599(struct ixgbe_atr_input *input, uint32_t *src_addr_1,
                                 uint32_t *src_addr_2, uint32_t *src_addr_3,
                                 uint32_t *src_addr_4);
int32_t ixgbe_atr_get_dst_ipv6_82599(struct ixgbe_atr_input *input, uint32_t *dst_addr_1,
                                 uint32_t *dst_addr_2, uint32_t *dst_addr_3,
                                 uint32_t *dst_addr_4);
int32_t ixgbe_atr_get_src_port_82599(struct ixgbe_atr_input *input, uint16_t *src_port);
int32_t ixgbe_atr_get_dst_port_82599(struct ixgbe_atr_input *input, uint16_t *dst_port);
int32_t ixgbe_atr_get_flex_byte_82599(struct ixgbe_atr_input *input,
                                  uint16_t *flex_byte);
int32_t ixgbe_atr_get_vm_pool_82599(struct ixgbe_atr_input *input, uint8_t *vm_pool);
int32_t ixgbe_atr_get_l4type_82599(struct ixgbe_atr_input *input, uint8_t *l4type);


void ixgbe_init_mac_link_ops_82599(struct ixgbe_hw *hw)
{
	struct ixgbe_mac_info *mac = &hw->mac;

	DEBUGFUNC("ixgbe_init_mac_link_ops_82599");

	if (hw->phy.multispeed_fiber) {
		/* Set up dual speed SFP+ support */
		mac->ops.setup_link = &ixgbe_setup_mac_link_multispeed_fiber;
	} else {
		if ((ixgbe_get_media_type_82599(hw) == ixgbe_media_type_backplane) &&
		     (hw->phy.smart_speed == ixgbe_smart_speed_auto ||
		      hw->phy.smart_speed == ixgbe_smart_speed_on))
			mac->ops.setup_link = &ixgbe_setup_mac_link_smartspeed;
		else
			mac->ops.setup_link = &ixgbe_setup_mac_link_82599;
	}
}

/**
 *  ixgbe_init_phy_ops_82599 - PHY/SFP specific init
 *  @hw: pointer to hardware structure
 *
 *  Initialize any function pointers that were not able to be
 *  set during init_shared_code because the PHY/SFP type was
 *  not known.  Perform the SFP init if necessary.
 *
 **/
int32_t ixgbe_init_phy_ops_82599(struct ixgbe_hw *hw)
{
	struct ixgbe_mac_info *mac = &hw->mac;
	struct ixgbe_phy_info *phy = &hw->phy;
	int32_t ret_val = IXGBE_SUCCESS;

	DEBUGFUNC("ixgbe_init_phy_ops_82599");

	/* Identify the PHY or SFP module */
	ret_val = phy->ops.identify(hw);
	if (ret_val == IXGBE_ERR_SFP_NOT_SUPPORTED)
		goto init_phy_ops_out;

	/* Setup function pointers based on detected SFP module and speeds */
	ixgbe_init_mac_link_ops_82599(hw);
	if (hw->phy.sfp_type != ixgbe_sfp_type_unknown)
		hw->phy.ops.reset = NULL;

	/* If copper media, overwrite with copper function pointers */
	if (mac->ops.get_media_type(hw) == ixgbe_media_type_copper) {
		mac->ops.setup_link = &ixgbe_setup_copper_link_82599;
		mac->ops.get_link_capabilities =
		                  &ixgbe_get_copper_link_capabilities_generic;
	}

	/* Set necessary function pointers based on phy type */
	switch (hw->phy.type) {
	case ixgbe_phy_tn:
		phy->ops.setup_link = &ixgbe_setup_phy_link_tnx;
		phy->ops.check_link = &ixgbe_check_phy_link_tnx;
		phy->ops.get_firmware_version =
		             &ixgbe_get_phy_firmware_version_tnx;
		break;
	case ixgbe_phy_aq:
		phy->ops.get_firmware_version =
		             &ixgbe_get_phy_firmware_version_generic;
		break;
	default:
		break;
	}
init_phy_ops_out:
	return ret_val;
}

int32_t ixgbe_setup_sfp_modules_82599(struct ixgbe_hw *hw)
{
	int32_t ret_val = IXGBE_SUCCESS;
	uint16_t list_offset, data_offset, data_value;

	DEBUGFUNC("ixgbe_setup_sfp_modules_82599");

	if (hw->phy.sfp_type != ixgbe_sfp_type_unknown) {
		ixgbe_init_mac_link_ops_82599(hw);

		hw->phy.ops.reset = NULL;

		ret_val = ixgbe_get_sfp_init_sequence_offsets(hw, &list_offset,
		                                              &data_offset);
		if (ret_val != IXGBE_SUCCESS)
			goto setup_sfp_out;

		/* PHY config will finish before releasing the semaphore */
		ret_val = ixgbe_acquire_swfw_sync(hw, IXGBE_GSSR_MAC_CSR_SM);
		if (ret_val != IXGBE_SUCCESS) {
			ret_val = IXGBE_ERR_SWFW_SYNC;
			goto setup_sfp_out;
		}

		hw->eeprom.ops.read(hw, ++data_offset, &data_value);
		while (data_value != 0xffff) {
			IXGBE_WRITE_REG(hw, IXGBE_CORECTL, data_value);
			IXGBE_WRITE_FLUSH(hw);
			hw->eeprom.ops.read(hw, ++data_offset, &data_value);
		}
		/* Now restart DSP by setting Restart_AN */
		IXGBE_WRITE_REG(hw, IXGBE_AUTOC,
		    (IXGBE_READ_REG(hw, IXGBE_AUTOC) | IXGBE_AUTOC_AN_RESTART));

		/* Release the semaphore */
		ixgbe_release_swfw_sync(hw, IXGBE_GSSR_MAC_CSR_SM);
		/* Delay obtaining semaphore again to allow FW access */
		msec_delay(hw->eeprom.semaphore_delay);
	}

setup_sfp_out:
	return ret_val;
}

/**
 *  ixgbe_init_ops_82599 - Inits func ptrs and MAC type
 *  @hw: pointer to hardware structure
 *
 *  Initialize the function pointers and assign the MAC type for 82599.
 *  Does not touch the hardware.
 **/

int32_t ixgbe_init_ops_82599(struct ixgbe_hw *hw)
{
	struct ixgbe_mac_info *mac = &hw->mac;
	struct ixgbe_phy_info *phy = &hw->phy;
	int32_t ret_val;

	DEBUGFUNC("ixgbe_init_ops_82599");

	ret_val = ixgbe_init_phy_ops_generic(hw);
	ret_val = ixgbe_init_ops_generic(hw);

	/* PHY */
	phy->ops.identify = &ixgbe_identify_phy_82599;
	phy->ops.init = &ixgbe_init_phy_ops_82599;

	/* MAC */
	mac->ops.reset_hw = &ixgbe_reset_hw_82599;
	mac->ops.get_media_type = &ixgbe_get_media_type_82599;
	mac->ops.get_supported_physical_layer =
	                            &ixgbe_get_supported_physical_layer_82599;
	mac->ops.enable_rx_dma = &ixgbe_enable_rx_dma_82599;
	mac->ops.read_analog_reg8 = &ixgbe_read_analog_reg8_82599;
	mac->ops.write_analog_reg8 = &ixgbe_write_analog_reg8_82599;
	mac->ops.start_hw = &ixgbe_start_hw_rev_1_82599;
	mac->ops.get_device_caps = &ixgbe_get_device_caps_82599;

	/* RAR, Multicast, VLAN */
	mac->ops.set_vmdq = &ixgbe_set_vmdq_generic;
	mac->ops.clear_vmdq = &ixgbe_clear_vmdq_generic;
	mac->ops.insert_mac_addr = &ixgbe_insert_mac_addr_generic;
	mac->rar_highwater = 1;
	mac->ops.set_vfta = &ixgbe_set_vfta_generic;
	mac->ops.clear_vfta = &ixgbe_clear_vfta_generic;
	mac->ops.init_uta_tables = &ixgbe_init_uta_tables_generic;
	mac->ops.setup_sfp = &ixgbe_setup_sfp_modules_82599;

	/* Link */
	mac->ops.get_link_capabilities = &ixgbe_get_link_capabilities_82599;
	mac->ops.check_link            = &ixgbe_check_mac_link_generic;
	ixgbe_init_mac_link_ops_82599(hw);

	mac->mcft_size        = 128;
	mac->vft_size         = 128;
	mac->num_rar_entries  = 128;
	mac->max_tx_queues    = 128;
	mac->max_rx_queues    = 128;
	mac->max_msix_vectors = ixgbe_get_pcie_msix_count_generic(hw);


	return ret_val;
}

/**
 *  ixgbe_get_link_capabilities_82599 - Determines link capabilities
 *  @hw: pointer to hardware structure
 *  @speed: pointer to link speed
 *  @negotiation: TRUE when autoneg or autotry is enabled
 *
 *  Determines the link capabilities by reading the AUTOC register.
 **/
int32_t ixgbe_get_link_capabilities_82599(struct ixgbe_hw *hw,
                                      ixgbe_link_speed *speed,
                                      int *negotiation)
{
	int32_t status = IXGBE_SUCCESS;
	uint32_t autoc = 0;

	DEBUGFUNC("ixgbe_get_link_capabilities_82599");

	/*
	 * Determine link capabilities based on the stored value of AUTOC,
	 * which represents EEPROM defaults.  If AUTOC value has not
	 * been stored, use the current register values.
	 */
	if (hw->mac.orig_link_settings_stored)
		autoc = hw->mac.orig_autoc;
	else
		autoc = IXGBE_READ_REG(hw, IXGBE_AUTOC);

	switch (autoc & IXGBE_AUTOC_LMS_MASK) {
	case IXGBE_AUTOC_LMS_1G_LINK_NO_AN:
		*speed = IXGBE_LINK_SPEED_1GB_FULL;
		*negotiation = FALSE;
		break;

	case IXGBE_AUTOC_LMS_10G_LINK_NO_AN:
		*speed = IXGBE_LINK_SPEED_10GB_FULL;
		*negotiation = FALSE;
		break;

	case IXGBE_AUTOC_LMS_1G_AN:
		*speed = IXGBE_LINK_SPEED_1GB_FULL;
		*negotiation = TRUE;
		break;

	case IXGBE_AUTOC_LMS_10G_SERIAL:
		*speed = IXGBE_LINK_SPEED_10GB_FULL;
		*negotiation = FALSE;
		break;

	case IXGBE_AUTOC_LMS_KX4_KX_KR:
	case IXGBE_AUTOC_LMS_KX4_KX_KR_1G_AN:
		*speed = IXGBE_LINK_SPEED_UNKNOWN;
		if (autoc & IXGBE_AUTOC_KR_SUPP)
			*speed |= IXGBE_LINK_SPEED_10GB_FULL;
		if (autoc & IXGBE_AUTOC_KX4_SUPP)
			*speed |= IXGBE_LINK_SPEED_10GB_FULL;
		if (autoc & IXGBE_AUTOC_KX_SUPP)
			*speed |= IXGBE_LINK_SPEED_1GB_FULL;
		*negotiation = TRUE;
		break;

	case IXGBE_AUTOC_LMS_KX4_KX_KR_SGMII:
		*speed = IXGBE_LINK_SPEED_100_FULL;
		if (autoc & IXGBE_AUTOC_KR_SUPP)
			*speed |= IXGBE_LINK_SPEED_10GB_FULL;
		if (autoc & IXGBE_AUTOC_KX4_SUPP)
			*speed |= IXGBE_LINK_SPEED_10GB_FULL;
		if (autoc & IXGBE_AUTOC_KX_SUPP)
			*speed |= IXGBE_LINK_SPEED_1GB_FULL;
		*negotiation = TRUE;
		break;

	case IXGBE_AUTOC_LMS_SGMII_1G_100M:
		*speed = IXGBE_LINK_SPEED_1GB_FULL | IXGBE_LINK_SPEED_100_FULL;
		*negotiation = FALSE;
		break;

	default:
		status = IXGBE_ERR_LINK_SETUP;
		goto out;
		break;
	}

	if (hw->phy.multispeed_fiber) {
		*speed |= IXGBE_LINK_SPEED_10GB_FULL |
		          IXGBE_LINK_SPEED_1GB_FULL;
		*negotiation = TRUE;
	}

out:
	return status;
}

/**
 *  ixgbe_get_media_type_82599 - Get media type
 *  @hw: pointer to hardware structure
 *
 *  Returns the media type (fiber, copper, backplane)
 **/
enum ixgbe_media_type ixgbe_get_media_type_82599(struct ixgbe_hw *hw)
{
	enum ixgbe_media_type media_type;

	DEBUGFUNC("ixgbe_get_media_type_82599");

	/* Detect if there is a copper PHY attached. */
	if (hw->phy.type == ixgbe_phy_cu_unknown ||
	    hw->phy.type == ixgbe_phy_tn ||
	    hw->phy.type == ixgbe_phy_aq) {
		media_type = ixgbe_media_type_copper;
		goto out;
	}

	switch (hw->device_id) {
	case IXGBE_DEV_ID_82599_KX4:
	case IXGBE_DEV_ID_82599_KX4_MEZZ:
	case IXGBE_DEV_ID_82599_COMBO_BACKPLANE:
	case IXGBE_DEV_ID_82599_XAUI_LOM:
		/* Default device ID is mezzanine card KX/KX4 */
		media_type = ixgbe_media_type_backplane;
		break;
	case IXGBE_DEV_ID_82599_SFP:
		media_type = ixgbe_media_type_fiber;
		break;
	case IXGBE_DEV_ID_82599_CX4:
		media_type = ixgbe_media_type_cx4;
		break;
	default:
		media_type = ixgbe_media_type_unknown;
		break;
	}
out:
	return media_type;
}

/**
 *  ixgbe_start_mac_link_82599 - Setup MAC link settings
 *  @hw: pointer to hardware structure
 *
 *  Configures link settings based on values in the ixgbe_hw struct.
 *  Restarts the link.  Performs autonegotiation if needed.
 **/
int32_t ixgbe_start_mac_link_82599(struct ixgbe_hw *hw,
                               int autoneg_wait_to_complete)
{
	uint32_t autoc_reg;
	uint32_t links_reg;
	uint32_t i;
	int32_t status = IXGBE_SUCCESS;

	DEBUGFUNC("ixgbe_start_mac_link_82599");


	/* Restart link */
	autoc_reg = IXGBE_READ_REG(hw, IXGBE_AUTOC);
	autoc_reg |= IXGBE_AUTOC_AN_RESTART;
	IXGBE_WRITE_REG(hw, IXGBE_AUTOC, autoc_reg);

	/* Only poll for autoneg to complete if specified to do so */
	if (autoneg_wait_to_complete) {
		if ((autoc_reg & IXGBE_AUTOC_LMS_MASK) ==
		     IXGBE_AUTOC_LMS_KX4_KX_KR ||
		    (autoc_reg & IXGBE_AUTOC_LMS_MASK) ==
		     IXGBE_AUTOC_LMS_KX4_KX_KR_1G_AN
		    || (autoc_reg & IXGBE_AUTOC_LMS_MASK) ==
		     IXGBE_AUTOC_LMS_KX4_KX_KR_SGMII) {
			links_reg = 0; /* Just in case Autoneg time = 0 */
			for (i = 0; i < IXGBE_AUTO_NEG_TIME; i++) {
				links_reg = IXGBE_READ_REG(hw, IXGBE_LINKS);
				if (links_reg & IXGBE_LINKS_KX_AN_COMP)
					break;
				msec_delay(100);
			}
			if (!(links_reg & IXGBE_LINKS_KX_AN_COMP)) {
				status = IXGBE_ERR_AUTONEG_NOT_COMPLETE;
				DEBUGOUT("Autoneg did not complete.\n");
			}
		}
	}

	/* Add delay to filter out noises during initial link setup */
	msec_delay(50);

	return status;
}

/**
 *  ixgbe_setup_mac_link_multispeed_fiber - Set MAC link speed
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg: TRUE if autonegotiation enabled
 *  @autoneg_wait_to_complete: TRUE when waiting for completion is needed
 *
 *  Set the link speed in the AUTOC register and restarts link.
 **/
int32_t ixgbe_setup_mac_link_multispeed_fiber(struct ixgbe_hw *hw,
                                     ixgbe_link_speed speed, int autoneg,
                                     int autoneg_wait_to_complete)
{
	int32_t status = IXGBE_SUCCESS;
	ixgbe_link_speed link_speed;
	ixgbe_link_speed highest_link_speed = IXGBE_LINK_SPEED_UNKNOWN;
	uint32_t speedcnt = 0;
	uint32_t esdp_reg = IXGBE_READ_REG(hw, IXGBE_ESDP);
	uint32_t i = 0;
	int link_up = FALSE;
	int negotiation;

	DEBUGFUNC("ixgbe_setup_mac_link_multispeed_fiber");

	/* Mask off requested but non-supported speeds */
	status = ixgbe_get_link_capabilities_82599(hw, &link_speed, &negotiation);
	if (status != IXGBE_SUCCESS)
		return status;

	speed &= link_speed;

	/*
	 * When the driver changes the link speeds that it can support,
	 * it sets autotry_restart to TRUE to indicate that we need to
	 * initiate a new autotry session with the link partner.  To do
	 * so, we set the speed then disable and re-enable the tx laser, to
	 * alert the link partner that it also needs to restart autotry on its
	 * end.  This is consistent with TRUE clause 37 autoneg, which also
	 * involves a loss of signal.
	 */

	/*
	 * Try each speed one by one, highest priority first.  We do this in
	 * software because 10gb fiber doesn't support speed autonegotiation.
	 */
	if (speed & IXGBE_LINK_SPEED_10GB_FULL) {
		speedcnt++;
		highest_link_speed = IXGBE_LINK_SPEED_10GB_FULL;

		/* If we already have link at this speed, just jump out */
		status = ixgbe_check_mac_link_generic(hw, &link_speed, &link_up, FALSE);
		if (status != IXGBE_SUCCESS)
			return status;

		if ((link_speed == IXGBE_LINK_SPEED_10GB_FULL) && link_up)
			goto out;

		/* Set the module link speed */
		esdp_reg |= (IXGBE_ESDP_SDP5_DIR | IXGBE_ESDP_SDP5);
		IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp_reg);

		/* Allow module to change analog characteristics (1G->10G) */
		msec_delay(40);

		status = ixgbe_setup_mac_link_82599(
			hw, IXGBE_LINK_SPEED_10GB_FULL, autoneg,
			autoneg_wait_to_complete);
		if (status != IXGBE_SUCCESS)
			return status;

		/* Flap the tx laser if it has not already been done */
		if (hw->mac.autotry_restart) {
			/* Disable tx laser; allow 100us to go dark per spec */
			esdp_reg |= IXGBE_ESDP_SDP3;
			IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp_reg);
			usec_delay(100);

			/* Enable tx laser; allow 2ms to light up per spec */
			esdp_reg &= ~IXGBE_ESDP_SDP3;
			IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp_reg);
			msec_delay(2);

			hw->mac.autotry_restart = FALSE;
		}

		/*
		 * Wait for the controller to acquire link.  Per IEEE 802.3ap,
		 * Section 73.10.2, we may have to wait up to 500ms if KR is
		 * attempted.  82599 uses the same timing for 10g SFI.
		 */
		for (i = 0; i < 5; i++) {
			/* Wait for the link partner to also set speed */
			msec_delay(100);

			/* If we have link, just jump out */
			status = ixgbe_check_mac_link_generic(hw, &link_speed,
			                          &link_up, FALSE);
			if (status != IXGBE_SUCCESS)
				return status;

			if (link_up)
				goto out;
		}
	}

	if (speed & IXGBE_LINK_SPEED_1GB_FULL) {
		speedcnt++;
		if (highest_link_speed == IXGBE_LINK_SPEED_UNKNOWN)
			highest_link_speed = IXGBE_LINK_SPEED_1GB_FULL;

		/* If we already have link at this speed, just jump out */
		status = ixgbe_check_mac_link_generic(hw, &link_speed, &link_up, FALSE);
		if (status != IXGBE_SUCCESS)
			return status;

		if ((link_speed == IXGBE_LINK_SPEED_1GB_FULL) && link_up)
			goto out;

		/* Set the module link speed */
		esdp_reg &= ~IXGBE_ESDP_SDP5;
		esdp_reg |= IXGBE_ESDP_SDP5_DIR;
		IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp_reg);

		/* Allow module to change analog characteristics (10G->1G) */
		msec_delay(40);

		status = ixgbe_setup_mac_link_82599(
			hw, IXGBE_LINK_SPEED_1GB_FULL, autoneg,
			autoneg_wait_to_complete);
		if (status != IXGBE_SUCCESS)
			return status;

		/* Flap the tx laser if it has not already been done */
		if (hw->mac.autotry_restart) {
			/* Disable tx laser; allow 100us to go dark per spec */
			esdp_reg |= IXGBE_ESDP_SDP3;
			IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp_reg);
			usec_delay(100);

			/* Enable tx laser; allow 2ms to light up per spec */
			esdp_reg &= ~IXGBE_ESDP_SDP3;
			IXGBE_WRITE_REG(hw, IXGBE_ESDP, esdp_reg);
			msec_delay(2);

			hw->mac.autotry_restart = FALSE;
		}

		/* Wait for the link partner to also set speed */
		msec_delay(100);

		/* If we have link, just jump out */
		status = ixgbe_check_mac_link_generic(hw, &link_speed, &link_up, FALSE);
		if (status != IXGBE_SUCCESS)
			return status;

		if (link_up)
			goto out;
	}

	/*
	 * We didn't get link.  Configure back to the highest speed we tried,
	 * (if there was more than one).  We call ourselves back with just the
	 * single highest speed that the user requested.
	 */
	if (speedcnt > 1)
		status = ixgbe_setup_mac_link_multispeed_fiber(hw,
		        highest_link_speed, autoneg, autoneg_wait_to_complete);

out:
	/* Set autoneg_advertised value based on input link speed */
	hw->phy.autoneg_advertised = 0;

	if (speed & IXGBE_LINK_SPEED_10GB_FULL)
		hw->phy.autoneg_advertised |= IXGBE_LINK_SPEED_10GB_FULL;

	if (speed & IXGBE_LINK_SPEED_1GB_FULL)
		hw->phy.autoneg_advertised |= IXGBE_LINK_SPEED_1GB_FULL;

	return status;
}

/**
 *  ixgbe_setup_mac_link_smartspeed - Set MAC link speed using SmartSpeed
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg: TRUE if autonegotiation enabled
 *  @autoneg_wait_to_complete: TRUE when waiting for completion is needed
 *
 *  Implements the Intel SmartSpeed algorithm.
 **/
int32_t ixgbe_setup_mac_link_smartspeed(struct ixgbe_hw *hw,
				     ixgbe_link_speed speed, int autoneg,
				     int autoneg_wait_to_complete)
{
	int32_t status = IXGBE_SUCCESS;
	ixgbe_link_speed link_speed;
	int32_t i, j;
	int link_up = FALSE;
	uint32_t autoc_reg = IXGBE_READ_REG(hw, IXGBE_AUTOC);

	DEBUGFUNC("ixgbe_setup_mac_link_smartspeed");

	 /* Set autoneg_advertised value based on input link speed */
	hw->phy.autoneg_advertised = 0;

	if (speed & IXGBE_LINK_SPEED_10GB_FULL)
		hw->phy.autoneg_advertised |= IXGBE_LINK_SPEED_10GB_FULL;

	if (speed & IXGBE_LINK_SPEED_1GB_FULL)
		hw->phy.autoneg_advertised |= IXGBE_LINK_SPEED_1GB_FULL;

	if (speed & IXGBE_LINK_SPEED_100_FULL)
		hw->phy.autoneg_advertised |= IXGBE_LINK_SPEED_100_FULL;

	/*
	 * Implement Intel SmartSpeed algorithm.  SmartSpeed will reduce the
	 * autoneg advertisement if link is unable to be established at the
	 * highest negotiated rate.  This can sometimes happen due to integrity
	 * issues with the physical media connection.
	 */

	/* First, try to get link with full advertisement */
	hw->phy.smart_speed_active = FALSE;
	for (j = 0; j < IXGBE_SMARTSPEED_MAX_RETRIES; j++) {
		status = ixgbe_setup_mac_link_82599(hw, speed, autoneg,
						    autoneg_wait_to_complete);
		if (status != IXGBE_SUCCESS)
			goto out;

		/*
		 * Wait for the controller to acquire link.  Per IEEE 802.3ap,
		 * Section 73.10.2, we may have to wait up to 500ms if KR is
		 * attempted, or 200ms if KX/KX4/BX/BX4 is attempted, per
		 * Table 9 in the AN MAS.
		 */
		for (i = 0; i < 5; i++) {
			msec_delay(100);

			/* If we have link, just jump out */
			status = ixgbe_check_mac_link_generic(hw, &link_speed, &link_up,
						  FALSE);
			if (status != IXGBE_SUCCESS)
				goto out;

			if (link_up)
				goto out;
		}
	}

	/*
	 * We didn't get link.  If we advertised KR plus one of KX4/KX
	 * (or BX4/BX), then disable KR and try again.
	 */
	if (((autoc_reg & IXGBE_AUTOC_KR_SUPP) == 0) ||
	    ((autoc_reg & IXGBE_AUTOC_KX4_KX_SUPP_MASK) == 0))
		goto out;

	/* Turn SmartSpeed on to disable KR support */
	hw->phy.smart_speed_active = TRUE;
	status = ixgbe_setup_mac_link_82599(hw, speed, autoneg,
					    autoneg_wait_to_complete);
	if (status != IXGBE_SUCCESS)
		goto out;

	/*
	 * Wait for the controller to acquire link.  600ms will allow for
	 * the AN link_fail_inhibit_timer as well for multiple cycles of
	 * parallel detect, both 10g and 1g. This allows for the maximum
	 * connect attempts as defined in the AN MAS table 73-7.
	 */
	for (i = 0; i < 6; i++) {
		msec_delay(100);

		/* If we have link, just jump out */
		status = ixgbe_check_mac_link_generic(hw, &link_speed, &link_up, FALSE);
		if (status != IXGBE_SUCCESS)
			goto out;

		if (link_up)
			goto out;
	}

	/* We didn't get link.  Turn SmartSpeed back off. */
	hw->phy.smart_speed_active = FALSE;
	status = ixgbe_setup_mac_link_82599(hw, speed, autoneg,
					    autoneg_wait_to_complete);

out:
	return status;
}

/**
 *  ixgbe_setup_mac_link_82599 - Set MAC link speed
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg: TRUE if autonegotiation enabled
 *  @autoneg_wait_to_complete: TRUE when waiting for completion is needed
 *
 *  Set the link speed in the AUTOC register and restarts link.
 **/
int32_t ixgbe_setup_mac_link_82599(struct ixgbe_hw *hw,
                                     ixgbe_link_speed speed, int autoneg,
                                     int autoneg_wait_to_complete)
{
	int32_t status = IXGBE_SUCCESS;
	uint32_t autoc = IXGBE_READ_REG(hw, IXGBE_AUTOC);
	uint32_t autoc2 = IXGBE_READ_REG(hw, IXGBE_AUTOC2);
	uint32_t start_autoc = autoc;
	uint32_t orig_autoc = 0;
	uint32_t link_mode = autoc & IXGBE_AUTOC_LMS_MASK;
	uint32_t pma_pmd_1g = autoc & IXGBE_AUTOC_1G_PMA_PMD_MASK;
	uint32_t pma_pmd_10g_serial = autoc2 & IXGBE_AUTOC2_10G_SERIAL_PMA_PMD_MASK;
	uint32_t links_reg;
	uint32_t i;
	ixgbe_link_speed link_capabilities = IXGBE_LINK_SPEED_UNKNOWN;

	DEBUGFUNC("ixgbe_setup_mac_link_82599");

	/* Check to see if speed passed in is supported. */
	status = ixgbe_get_link_capabilities_82599(hw, &link_capabilities, &autoneg);
	if (status != IXGBE_SUCCESS)
		goto out;

	speed &= link_capabilities;

	if (speed == IXGBE_LINK_SPEED_UNKNOWN) {
		status = IXGBE_ERR_LINK_SETUP;
		goto out;
	}

	/* Use stored value (EEPROM defaults) of AUTOC to find KR/KX4 support*/
	if (hw->mac.orig_link_settings_stored)
		orig_autoc = hw->mac.orig_autoc;
	else
		orig_autoc = autoc;

	if (link_mode == IXGBE_AUTOC_LMS_KX4_KX_KR ||
	         link_mode == IXGBE_AUTOC_LMS_KX4_KX_KR_1G_AN ||
	         link_mode == IXGBE_AUTOC_LMS_KX4_KX_KR_SGMII) {
		/* Set KX4/KX/KR support according to speed requested */
		autoc &= ~(IXGBE_AUTOC_KX4_KX_SUPP_MASK | IXGBE_AUTOC_KR_SUPP);
		if (speed & IXGBE_LINK_SPEED_10GB_FULL)
			if (orig_autoc & IXGBE_AUTOC_KX4_SUPP)
				autoc |= IXGBE_AUTOC_KX4_SUPP;
			if ((orig_autoc & IXGBE_AUTOC_KR_SUPP) &&
			    (hw->phy.smart_speed_active == FALSE))
				autoc |= IXGBE_AUTOC_KR_SUPP;
		if (speed & IXGBE_LINK_SPEED_1GB_FULL)
			autoc |= IXGBE_AUTOC_KX_SUPP;
	} else if ((pma_pmd_1g == IXGBE_AUTOC_1G_SFI) &&
	         (link_mode == IXGBE_AUTOC_LMS_1G_LINK_NO_AN ||
	          link_mode == IXGBE_AUTOC_LMS_1G_AN)) {
		/* Switch from 1G SFI to 10G SFI if requested */
		if ((speed == IXGBE_LINK_SPEED_10GB_FULL) &&
		    (pma_pmd_10g_serial == IXGBE_AUTOC2_10G_SFI)) {
			autoc &= ~IXGBE_AUTOC_LMS_MASK;
			autoc |= IXGBE_AUTOC_LMS_10G_SERIAL;
		}
	} else if ((pma_pmd_10g_serial == IXGBE_AUTOC2_10G_SFI) &&
	         (link_mode == IXGBE_AUTOC_LMS_10G_SERIAL)) {
		/* Switch from 10G SFI to 1G SFI if requested */
		if ((speed == IXGBE_LINK_SPEED_1GB_FULL) &&
		    (pma_pmd_1g == IXGBE_AUTOC_1G_SFI)) {
			autoc &= ~IXGBE_AUTOC_LMS_MASK;
			if (autoneg)
				autoc |= IXGBE_AUTOC_LMS_1G_AN;
			else
				autoc |= IXGBE_AUTOC_LMS_1G_LINK_NO_AN;
		}
	}

	if (autoc != start_autoc) {

		/* Restart link */
		autoc |= IXGBE_AUTOC_AN_RESTART;
		IXGBE_WRITE_REG(hw, IXGBE_AUTOC, autoc);

		/* Only poll for autoneg to complete if specified to do so */
		if (autoneg_wait_to_complete) {
			if (link_mode == IXGBE_AUTOC_LMS_KX4_KX_KR ||
			    link_mode == IXGBE_AUTOC_LMS_KX4_KX_KR_1G_AN ||
			    link_mode == IXGBE_AUTOC_LMS_KX4_KX_KR_SGMII) {
				links_reg = 0; /*Just in case Autoneg time=0*/
				for (i = 0; i < IXGBE_AUTO_NEG_TIME; i++) {
					links_reg =
					       IXGBE_READ_REG(hw, IXGBE_LINKS);
					if (links_reg & IXGBE_LINKS_KX_AN_COMP)
						break;
					msec_delay(100);
				}
				if (!(links_reg & IXGBE_LINKS_KX_AN_COMP)) {
					status =
						IXGBE_ERR_AUTONEG_NOT_COMPLETE;
					DEBUGOUT("Autoneg did not complete.\n");
				}
			}
		}

		/* Add delay to filter out noises during initial link setup */
		msec_delay(50);
	}

out:
	return status;
}

/**
 *  ixgbe_setup_copper_link_82599 - Set the PHY autoneg advertised field
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg: TRUE if autonegotiation enabled
 *  @autoneg_wait_to_complete: TRUE if waiting is needed to complete
 *
 *  Restarts link on PHY and MAC based on settings passed in.
 **/
int32_t ixgbe_setup_copper_link_82599(struct ixgbe_hw *hw,
                                               ixgbe_link_speed speed,
                                               int autoneg,
                                               int autoneg_wait_to_complete)
{
	int32_t status;

	DEBUGFUNC("ixgbe_setup_copper_link_82599");

	/* Setup the PHY according to input speed */
	status = hw->phy.ops.setup_link_speed(hw, speed, autoneg,
	                                      autoneg_wait_to_complete);
	/* Set up MAC */
	ixgbe_start_mac_link_82599(hw, autoneg_wait_to_complete);

	return status;
}
/**
 *  ixgbe_reset_hw_82599 - Perform hardware reset
 *  @hw: pointer to hardware structure
 *
 *  Resets the hardware by resetting the transmit and receive units, masks
 *  and clears all interrupts, perform a PHY reset, and perform a link (MAC)
 *  reset.
 **/
int32_t ixgbe_reset_hw_82599(struct ixgbe_hw *hw)
{
	int32_t status = IXGBE_SUCCESS;
	uint32_t ctrl, ctrl_ext;
	uint32_t i;
	uint32_t autoc;
	uint32_t autoc2;

	DEBUGFUNC("ixgbe_reset_hw_82599");

	/* Call adapter stop to disable tx/rx and clear interrupts */
	hw->mac.ops.stop_adapter(hw);

	/* PHY ops must be identified and initialized prior to reset */

	/* Identify PHY and related function pointers */
	status = hw->phy.ops.init(hw);

	if (status == IXGBE_ERR_SFP_NOT_SUPPORTED)
		goto reset_hw_out;

	/* Setup SFP module if there is one present. */
	if (hw->phy.sfp_setup_needed) {
		status = hw->mac.ops.setup_sfp(hw);
		hw->phy.sfp_setup_needed = FALSE;
	}

	if (status == IXGBE_ERR_SFP_NOT_SUPPORTED)
		goto reset_hw_out;

	/* Reset PHY */
	if (hw->phy.reset_disable == FALSE && hw->phy.ops.reset != NULL)
		hw->phy.ops.reset(hw);

	/*
	 * Prevent the PCI-E bus from from hanging by disabling PCI-E master
	 * access and verify no pending requests before reset
	 */
	status = ixgbe_disable_pcie_master(hw);
	if (status != IXGBE_SUCCESS) {
		status = IXGBE_ERR_MASTER_REQUESTS_PENDING;
		DEBUGOUT("PCI-E Master disable polling has failed.\n");
	}

	/*
	 * Issue global reset to the MAC.  This needs to be a SW reset.
	 * If link reset is used, it might reset the MAC when mng is using it
	 */
	ctrl = IXGBE_READ_REG(hw, IXGBE_CTRL);
	IXGBE_WRITE_REG(hw, IXGBE_CTRL, (ctrl | IXGBE_CTRL_RST));
	IXGBE_WRITE_FLUSH(hw);

	/* Poll for reset bit to self-clear indicating reset is complete */
	for (i = 0; i < 10; i++) {
		usec_delay(1);
		ctrl = IXGBE_READ_REG(hw, IXGBE_CTRL);
		if (!(ctrl & IXGBE_CTRL_RST))
			break;
	}
	if (ctrl & IXGBE_CTRL_RST) {
		status = IXGBE_ERR_RESET_FAILED;
		DEBUGOUT("Reset polling failed to complete.\n");
	}
	/* Clear PF Reset Done bit so PF/VF Mail Ops can work */
	ctrl_ext = IXGBE_READ_REG(hw, IXGBE_CTRL_EXT);
	ctrl_ext |= IXGBE_CTRL_EXT_PFRSTD;
	IXGBE_WRITE_REG(hw, IXGBE_CTRL_EXT, ctrl_ext);

	msec_delay(50);

	/*
	 * Store the original AUTOC/AUTOC2 values if they have not been
	 * stored off yet.  Otherwise restore the stored original
	 * values since the reset operation sets back to defaults.
	 */
	autoc = IXGBE_READ_REG(hw, IXGBE_AUTOC);
	autoc2 = IXGBE_READ_REG(hw, IXGBE_AUTOC2);
	if (hw->mac.orig_link_settings_stored == FALSE) {
		hw->mac.orig_autoc = autoc;
		hw->mac.orig_autoc2 = autoc2;
		hw->mac.orig_link_settings_stored = TRUE;
	} else {
		if (autoc != hw->mac.orig_autoc)
			IXGBE_WRITE_REG(hw, IXGBE_AUTOC, (hw->mac.orig_autoc |
					IXGBE_AUTOC_AN_RESTART));

		if ((autoc2 & IXGBE_AUTOC2_UPPER_MASK) !=
		    (hw->mac.orig_autoc2 & IXGBE_AUTOC2_UPPER_MASK)) {
			autoc2 &= ~IXGBE_AUTOC2_UPPER_MASK;
			autoc2 |= (hw->mac.orig_autoc2 &
			           IXGBE_AUTOC2_UPPER_MASK);
			IXGBE_WRITE_REG(hw, IXGBE_AUTOC2, autoc2);
		}
	}

       /* Store the permanent mac address */
	hw->mac.ops.get_mac_addr(hw, hw->mac.perm_addr);

	/*
	 * Store MAC address from RAR0, clear receive address registers, and
	 * clear the multicast table.  Also reset num_rar_entries to 128,
	 * since we modify this value when programming the SAN MAC address.
	 */
	hw->mac.num_rar_entries = 128;
	hw->mac.ops.init_rx_addrs(hw);

reset_hw_out:
	return status;
}

/**
 *  ixgbe_reinit_fdir_tables_82599 - Reinitialize Flow Director tables.
 *  @hw: pointer to hardware structure
 **/
int32_t ixgbe_reinit_fdir_tables_82599(struct ixgbe_hw *hw)
{
	int i;
	uint32_t fdirctrl = IXGBE_READ_REG(hw, IXGBE_FDIRCTRL);
	fdirctrl &= ~IXGBE_FDIRCTRL_INIT_DONE;

	DEBUGFUNC("ixgbe_reinit_fdir_tables_82599");

	/*
	 * Before starting reinitialization process,
	 * FDIRCMD.CMD must be zero.
	 */
	for (i = 0; i < IXGBE_FDIRCMD_CMD_POLL; i++) {
		if (!(IXGBE_READ_REG(hw, IXGBE_FDIRCMD) &
		      IXGBE_FDIRCMD_CMD_MASK))
			break;
		usec_delay(10);
	}
	if (i >= IXGBE_FDIRCMD_CMD_POLL) {
		DEBUGOUT("Flow Director previous command isn't complete, "
		         "aborting table re-initialization. \n");
		return IXGBE_ERR_FDIR_REINIT_FAILED;
	}

	IXGBE_WRITE_REG(hw, IXGBE_FDIRFREE, 0);
	IXGBE_WRITE_FLUSH(hw);
	/*
	 * 82599 adapters flow director init flow cannot be restarted,
	 * Workaround 82599 silicon errata by performing the following steps
	 * before re-writing the FDIRCTRL control register with the same value.
	 * - write 1 to bit 8 of FDIRCMD register &
	 * - write 0 to bit 8 of FDIRCMD register
	 */
	IXGBE_WRITE_REG(hw, IXGBE_FDIRCMD,
	                (IXGBE_READ_REG(hw, IXGBE_FDIRCMD) |
	                 IXGBE_FDIRCMD_CLEARHT));
	IXGBE_WRITE_FLUSH(hw);
	IXGBE_WRITE_REG(hw, IXGBE_FDIRCMD,
	                (IXGBE_READ_REG(hw, IXGBE_FDIRCMD) &
	                 ~IXGBE_FDIRCMD_CLEARHT));
	IXGBE_WRITE_FLUSH(hw);
	/*
	 * Clear FDIR Hash register to clear any leftover hashes
	 * waiting to be programmed.
	 */
	IXGBE_WRITE_REG(hw, IXGBE_FDIRHASH, 0x00);
	IXGBE_WRITE_FLUSH(hw);

	IXGBE_WRITE_REG(hw, IXGBE_FDIRCTRL, fdirctrl);
	IXGBE_WRITE_FLUSH(hw);

	/* Poll init-done after we write FDIRCTRL register */
	for (i = 0; i < IXGBE_FDIR_INIT_DONE_POLL; i++) {
		if (IXGBE_READ_REG(hw, IXGBE_FDIRCTRL) &
		                   IXGBE_FDIRCTRL_INIT_DONE)
			break;
		usec_delay(10);
	}
	if (i >= IXGBE_FDIR_INIT_DONE_POLL) {
		DEBUGOUT("Flow Director Signature poll time exceeded!\n");
		return IXGBE_ERR_FDIR_REINIT_FAILED;
	}

	/* Clear FDIR statistics registers (read to clear) */
	IXGBE_READ_REG(hw, IXGBE_FDIRUSTAT);
	IXGBE_READ_REG(hw, IXGBE_FDIRFSTAT);
	IXGBE_READ_REG(hw, IXGBE_FDIRMATCH);
	IXGBE_READ_REG(hw, IXGBE_FDIRMISS);
	IXGBE_READ_REG(hw, IXGBE_FDIRLEN);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_init_fdir_signature_82599 - Initialize Flow Director signature filters
 *  @hw: pointer to hardware structure
 *  @pballoc: which mode to allocate filters with
 **/
int32_t ixgbe_init_fdir_signature_82599(struct ixgbe_hw *hw, uint32_t pballoc)
{
	uint32_t fdirctrl = 0;
	uint32_t pbsize;
	int i;

	DEBUGFUNC("ixgbe_init_fdir_signature_82599");

	/*
	 * Before enabling Flow Director, the Rx Packet Buffer size
	 * must be reduced.  The new value is the current size minus
	 * flow director memory usage size.
	 */
	pbsize = (1 << (IXGBE_FDIR_PBALLOC_SIZE_SHIFT + pballoc));
	IXGBE_WRITE_REG(hw, IXGBE_RXPBSIZE(0),
	    (IXGBE_READ_REG(hw, IXGBE_RXPBSIZE(0)) - pbsize));

	/*
	 * The defaults in the HW for RX PB 1-7 are not zero and so should be
	 * intialized to zero for non DCB mode otherwise actual total RX PB
	 * would be bigger than programmed and filter space would run into
	 * the PB 0 region.
	 */
	for (i = 1; i < 8; i++)
		IXGBE_WRITE_REG(hw, IXGBE_RXPBSIZE(i), 0);

	/* Send interrupt when 64 filters are left */
	fdirctrl |= 4 << IXGBE_FDIRCTRL_FULL_THRESH_SHIFT;

	/* Set the maximum length per hash bucket to 0xA filters */
	fdirctrl |= 0xA << IXGBE_FDIRCTRL_MAX_LENGTH_SHIFT;

	switch (pballoc) {
	case IXGBE_FDIR_PBALLOC_64K:
		/* 8k - 1 signature filters */
		fdirctrl |= IXGBE_FDIRCTRL_PBALLOC_64K;
		break;
	case IXGBE_FDIR_PBALLOC_128K:
		/* 16k - 1 signature filters */
		fdirctrl |= IXGBE_FDIRCTRL_PBALLOC_128K;
		break;
	case IXGBE_FDIR_PBALLOC_256K:
		/* 32k - 1 signature filters */
		fdirctrl |= IXGBE_FDIRCTRL_PBALLOC_256K;
		break;
	default:
		/* bad value */
		return IXGBE_ERR_CONFIG;
	};

	/* Move the flexible bytes to use the ethertype - shift 6 words */
	fdirctrl |= (0x6 << IXGBE_FDIRCTRL_FLEX_SHIFT);


	/* Prime the keys for hashing */
	IXGBE_WRITE_REG(hw, IXGBE_FDIRHKEY,
	                htonl(IXGBE_ATR_BUCKET_HASH_KEY));
	IXGBE_WRITE_REG(hw, IXGBE_FDIRSKEY,
	                htonl(IXGBE_ATR_SIGNATURE_HASH_KEY));

	/*
	 * Poll init-done after we write the register.  Estimated times:
	 *      10G: PBALLOC = 11b, timing is 60us
	 *       1G: PBALLOC = 11b, timing is 600us
	 *     100M: PBALLOC = 11b, timing is 6ms
	 *
	 *     Multiple these timings by 4 if under full Rx load
	 *
	 * So we'll poll for IXGBE_FDIR_INIT_DONE_POLL times, sleeping for
	 * 1 msec per poll time.  If we're at line rate and drop to 100M, then
	 * this might not finish in our poll time, but we can live with that
	 * for now.
	 */
	IXGBE_WRITE_REG(hw, IXGBE_FDIRCTRL, fdirctrl);
	IXGBE_WRITE_FLUSH(hw);
	for (i = 0; i < IXGBE_FDIR_INIT_DONE_POLL; i++) {
		if (IXGBE_READ_REG(hw, IXGBE_FDIRCTRL) &
		                   IXGBE_FDIRCTRL_INIT_DONE)
			break;
		msec_delay(1);
	}
	if (i >= IXGBE_FDIR_INIT_DONE_POLL)
		DEBUGOUT("Flow Director Signature poll time exceeded!\n");

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_init_fdir_perfect_82599 - Initialize Flow Director perfect filters
 *  @hw: pointer to hardware structure
 *  @pballoc: which mode to allocate filters with
 **/
int32_t ixgbe_init_fdir_perfect_82599(struct ixgbe_hw *hw, uint32_t pballoc)
{
	uint32_t fdirctrl = 0;
	uint32_t pbsize;
	int i;

	DEBUGFUNC("ixgbe_init_fdir_perfect_82599");

	/*
	 * Before enabling Flow Director, the Rx Packet Buffer size
	 * must be reduced.  The new value is the current size minus
	 * flow director memory usage size.
	 */

	pbsize = (1 << (IXGBE_FDIR_PBALLOC_SIZE_SHIFT + pballoc));
	IXGBE_WRITE_REG(hw, IXGBE_RXPBSIZE(0),
	    (IXGBE_READ_REG(hw, IXGBE_RXPBSIZE(0)) - pbsize));

	/*
	 * The defaults in the HW for RX PB 1-7 are not zero and so should be
	 * intialized to zero for non DCB mode otherwise actual total RX PB
	 * would be bigger than programmed and filter space would run into
	 * the PB 0 region.
	 */
	for (i = 1; i < 8; i++)
		IXGBE_WRITE_REG(hw, IXGBE_RXPBSIZE(i), 0);

	/* Send interrupt when 64 filters are left */
	fdirctrl |= 4 << IXGBE_FDIRCTRL_FULL_THRESH_SHIFT;

	switch (pballoc) {
	case IXGBE_FDIR_PBALLOC_64K:
		/* 2k - 1 perfect filters */
		fdirctrl |= IXGBE_FDIRCTRL_PBALLOC_64K;
		break;
	case IXGBE_FDIR_PBALLOC_128K:
		/* 4k - 1 perfect filters */
		fdirctrl |= IXGBE_FDIRCTRL_PBALLOC_128K;
		break;
	case IXGBE_FDIR_PBALLOC_256K:
		/* 8k - 1 perfect filters */
		fdirctrl |= IXGBE_FDIRCTRL_PBALLOC_256K;
		break;
	default:
		/* bad value */
		return IXGBE_ERR_CONFIG;
	};

	/* Turn perfect match filtering on */
	fdirctrl |= IXGBE_FDIRCTRL_PERFECT_MATCH;
	fdirctrl |= IXGBE_FDIRCTRL_REPORT_STATUS;

	/* Move the flexible bytes to use the ethertype - shift 6 words */
	fdirctrl |= (0x6 << IXGBE_FDIRCTRL_FLEX_SHIFT);

	/* Prime the keys for hashing */
	IXGBE_WRITE_REG(hw, IXGBE_FDIRHKEY,
	                htonl(IXGBE_ATR_BUCKET_HASH_KEY));
	IXGBE_WRITE_REG(hw, IXGBE_FDIRSKEY,
	                htonl(IXGBE_ATR_SIGNATURE_HASH_KEY));

	/*
	 * Poll init-done after we write the register.  Estimated times:
	 *      10G: PBALLOC = 11b, timing is 60us
	 *       1G: PBALLOC = 11b, timing is 600us
	 *     100M: PBALLOC = 11b, timing is 6ms
	 *
	 *     Multiple these timings by 4 if under full Rx load
	 *
	 * So we'll poll for IXGBE_FDIR_INIT_DONE_POLL times, sleeping for
	 * 1 msec per poll time.  If we're at line rate and drop to 100M, then
	 * this might not finish in our poll time, but we can live with that
	 * for now.
	 */

	/* Set the maximum length per hash bucket to 0xA filters */
	fdirctrl |= (0xA << IXGBE_FDIRCTRL_MAX_LENGTH_SHIFT);

	IXGBE_WRITE_REG(hw, IXGBE_FDIRCTRL, fdirctrl);
	IXGBE_WRITE_FLUSH(hw);
	for (i = 0; i < IXGBE_FDIR_INIT_DONE_POLL; i++) {
		if (IXGBE_READ_REG(hw, IXGBE_FDIRCTRL) &
		                   IXGBE_FDIRCTRL_INIT_DONE)
			break;
		msec_delay(1);
	}
	if (i >= IXGBE_FDIR_INIT_DONE_POLL)
		DEBUGOUT("Flow Director Perfect poll time exceeded!\n");

	return IXGBE_SUCCESS;
}


/**
 *  ixgbe_atr_compute_hash_82599 - Compute the hashes for SW ATR
 *  @stream: input bitstream to compute the hash on
 *  @key: 32-bit hash key
 **/
uint16_t ixgbe_atr_compute_hash_82599(struct ixgbe_atr_input *atr_input, uint32_t key)
{
	/*
	 * The algorithm is as follows:
	 *    Hash[15:0] = Sum { S[n] x K[n+16] }, n = 0...350
	 *    where Sum {A[n]}, n = 0...n is bitwise XOR of A[0], A[1]...A[n]
	 *    and A[n] x B[n] is bitwise AND between same length strings
	 *
	 *    K[n] is 16 bits, defined as:
	 *       for n modulo 32 >= 15, K[n] = K[n % 32 : (n % 32) - 15]
	 *       for n modulo 32 < 15, K[n] =
	 *             K[(n % 32:0) | (31:31 - (14 - (n % 32)))]
	 *
	 *    S[n] is 16 bits, defined as:
	 *       for n >= 15, S[n] = S[n:n - 15]
	 *       for n < 15, S[n] = S[(n:0) | (350:350 - (14 - n))]
	 *
	 *    To simplify for programming, the algorithm is implemented
	 *    in software this way:
	 *
	 *    Key[31:0], Stream[335:0]
	 *
	 *    tmp_key[11 * 32 - 1:0] = 11{Key[31:0] = key concatenated 11 times
	 *    int_key[350:0] = tmp_key[351:1]
	 *    int_stream[365:0] = Stream[14:0] | Stream[335:0] | Stream[335:321]
	 *
	 *    hash[15:0] = 0;
	 *    for (i = 0; i < 351; i++) {
	 *        if (int_key[i])
	 *            hash ^= int_stream[(i + 15):i];
	 *    }
	 */

	union {
		uint64_t    fill[6];
		uint32_t    key[11];
		uint8_t     key_stream[44];
	} tmp_key;

	uint8_t   *stream = (uint8_t *)atr_input;
	uint8_t   int_key[44];      /* upper-most bit unused */
	uint8_t   hash_str[46];     /* upper-most 2 bits unused */
	uint16_t  hash_result = 0;
	int  i, j, k, h;

	DEBUGFUNC("ixgbe_atr_compute_hash_82599");

	/*
	 * Initialize the fill member to prevent warnings
	 * on some compilers
	 */
	 tmp_key.fill[0] = 0;

	/* First load the temporary key stream */
	for (i = 0; i < 6; i++) {
		uint64_t fillkey = ((uint64_t)key << 32) | key;
		tmp_key.fill[i] = fillkey;
	}

	/*
	 * Set the interim key for the hashing.  Bit 352 is unused, so we must
	 * shift and compensate when building the key.
	 */

	int_key[0] = tmp_key.key_stream[0] >> 1;
	for (i = 1, j = 0; i < 44; i++) {
		unsigned int this_key = tmp_key.key_stream[j] << 7;
		j++;
		int_key[i] = (uint8_t)(this_key | (tmp_key.key_stream[j] >> 1));
	}

	/*
	 * Set the interim bit string for the hashing.  Bits 368 and 367 are
	 * unused, so shift and compensate when building the string.
	 */
	hash_str[0] = (stream[40] & 0x7f) >> 1;
	for (i = 1, j = 40; i < 46; i++) {
		unsigned int this_str = stream[j] << 7;
		j++;
		if (j > 41)
			j = 0;
		hash_str[i] = (uint8_t)(this_str | (stream[j] >> 1));
	}

	/*
	 * Now compute the hash.  i is the index into hash_str, j is into our
	 * key stream, k is counting the number of bits, and h interates within
	 * each byte.
	 */
	for (i = 45, j = 43, k = 0; k < 351 && i >= 2 && j >= 0; i--, j--) {
		for (h = 0; h < 8 && k < 351; h++, k++) {
			if (int_key[j] & (1 << h)) {
				/*
				 * Key bit is set, XOR in the current 16-bit
				 * string.  Example of processing:
				 *    h = 0,
				 *      tmp = (hash_str[i - 2] & 0 << 16) |
				 *            (hash_str[i - 1] & 0xff << 8) |
				 *            (hash_str[i] & 0xff >> 0)
				 *      So tmp = hash_str[15 + k:k], since the
				 *      i + 2 clause rolls off the 16-bit value
				 *    h = 7,
				 *      tmp = (hash_str[i - 2] & 0x7f << 9) |
				 *            (hash_str[i - 1] & 0xff << 1) |
				 *            (hash_str[i] & 0x80 >> 7)
				 */
				int tmp = (hash_str[i] >> h);
				tmp |= (hash_str[i - 1] << (8 - h));
				tmp |= (int)(hash_str[i - 2] & ((1 << h) - 1))
				             << (16 - h);
				hash_result ^= (uint16_t)tmp;
			}
		}
	}

	return hash_result;
}

/**
 *  ixgbe_atr_set_vlan_id_82599 - Sets the VLAN id in the ATR input stream
 *  @input: input stream to modify
 *  @vlan: the VLAN id to load
 **/
int32_t ixgbe_atr_set_vlan_id_82599(struct ixgbe_atr_input *input, uint16_t vlan)
{
	DEBUGFUNC("ixgbe_atr_set_vlan_id_82599");

	input->byte_stream[IXGBE_ATR_VLAN_OFFSET + 1] = vlan >> 8;
	input->byte_stream[IXGBE_ATR_VLAN_OFFSET] = vlan & 0xff;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_atr_set_src_ipv4_82599 - Sets the source IPv4 address
 *  @input: input stream to modify
 *  @src_addr: the IP address to load
 **/
int32_t ixgbe_atr_set_src_ipv4_82599(struct ixgbe_atr_input *input, uint32_t src_addr)
{
	DEBUGFUNC("ixgbe_atr_set_src_ipv4_82599");

	input->byte_stream[IXGBE_ATR_SRC_IPV4_OFFSET + 3] = src_addr >> 24;
	input->byte_stream[IXGBE_ATR_SRC_IPV4_OFFSET + 2] =
	                                               (src_addr >> 16) & 0xff;
	input->byte_stream[IXGBE_ATR_SRC_IPV4_OFFSET + 1] =
	                                                (src_addr >> 8) & 0xff;
	input->byte_stream[IXGBE_ATR_SRC_IPV4_OFFSET] = src_addr & 0xff;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_atr_set_dst_ipv4_82599 - Sets the destination IPv4 address
 *  @input: input stream to modify
 *  @dst_addr: the IP address to load
 **/
int32_t ixgbe_atr_set_dst_ipv4_82599(struct ixgbe_atr_input *input, uint32_t dst_addr)
{
	DEBUGFUNC("ixgbe_atr_set_dst_ipv4_82599");

	input->byte_stream[IXGBE_ATR_DST_IPV4_OFFSET + 3] = dst_addr >> 24;
	input->byte_stream[IXGBE_ATR_DST_IPV4_OFFSET + 2] =
	                                               (dst_addr >> 16) & 0xff;
	input->byte_stream[IXGBE_ATR_DST_IPV4_OFFSET + 1] =
	                                                (dst_addr >> 8) & 0xff;
	input->byte_stream[IXGBE_ATR_DST_IPV4_OFFSET] = dst_addr & 0xff;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_atr_set_src_ipv6_82599 - Sets the source IPv6 address
 *  @input: input stream to modify
 *  @src_addr_1: the first 4 bytes of the IP address to load
 *  @src_addr_2: the second 4 bytes of the IP address to load
 *  @src_addr_3: the third 4 bytes of the IP address to load
 *  @src_addr_4: the fourth 4 bytes of the IP address to load
 **/
int32_t ixgbe_atr_set_src_ipv6_82599(struct ixgbe_atr_input *input,
                                 uint32_t src_addr_1, uint32_t src_addr_2,
                                 uint32_t src_addr_3, uint32_t src_addr_4)
{
	DEBUGFUNC("ixgbe_atr_set_src_ipv6_82599");

	input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET] = src_addr_4 & 0xff;
	input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 1] =
	                                               (src_addr_4 >> 8) & 0xff;
	input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 2] =
	                                              (src_addr_4 >> 16) & 0xff;
	input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 3] = src_addr_4 >> 24;

	input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 4] = src_addr_3 & 0xff;
	input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 5] =
	                                               (src_addr_3 >> 8) & 0xff;
	input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 6] =
	                                              (src_addr_3 >> 16) & 0xff;
	input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 7] = src_addr_3 >> 24;

	input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 8] = src_addr_2 & 0xff;
	input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 9] =
	                                               (src_addr_2 >> 8) & 0xff;
	input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 10] =
	                                              (src_addr_2 >> 16) & 0xff;
	input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 11] = src_addr_2 >> 24;

	input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 12] = src_addr_1 & 0xff;
	input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 13] =
	                                               (src_addr_1 >> 8) & 0xff;
	input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 14] =
	                                              (src_addr_1 >> 16) & 0xff;
	input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 15] = src_addr_1 >> 24;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_atr_set_dst_ipv6_82599 - Sets the destination IPv6 address
 *  @input: input stream to modify
 *  @dst_addr_1: the first 4 bytes of the IP address to load
 *  @dst_addr_2: the second 4 bytes of the IP address to load
 *  @dst_addr_3: the third 4 bytes of the IP address to load
 *  @dst_addr_4: the fourth 4 bytes of the IP address to load
 **/
int32_t ixgbe_atr_set_dst_ipv6_82599(struct ixgbe_atr_input *input,
                                 uint32_t dst_addr_1, uint32_t dst_addr_2,
                                 uint32_t dst_addr_3, uint32_t dst_addr_4)
{
	DEBUGFUNC("ixgbe_atr_set_dst_ipv6_82599");

	input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET] = dst_addr_4 & 0xff;
	input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 1] =
	                                               (dst_addr_4 >> 8) & 0xff;
	input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 2] =
	                                              (dst_addr_4 >> 16) & 0xff;
	input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 3] = dst_addr_4 >> 24;

	input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 4] = dst_addr_3 & 0xff;
	input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 5] =
	                                               (dst_addr_3 >> 8) & 0xff;
	input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 6] =
	                                              (dst_addr_3 >> 16) & 0xff;
	input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 7] = dst_addr_3 >> 24;

	input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 8] = dst_addr_2 & 0xff;
	input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 9] =
	                                               (dst_addr_2 >> 8) & 0xff;
	input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 10] =
	                                              (dst_addr_2 >> 16) & 0xff;
	input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 11] = dst_addr_2 >> 24;

	input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 12] = dst_addr_1 & 0xff;
	input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 13] =
	                                               (dst_addr_1 >> 8) & 0xff;
	input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 14] =
	                                              (dst_addr_1 >> 16) & 0xff;
	input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 15] = dst_addr_1 >> 24;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_atr_set_src_port_82599 - Sets the source port
 *  @input: input stream to modify
 *  @src_port: the source port to load
 **/
int32_t ixgbe_atr_set_src_port_82599(struct ixgbe_atr_input *input, uint16_t src_port)
{
	DEBUGFUNC("ixgbe_atr_set_src_port_82599");

	input->byte_stream[IXGBE_ATR_SRC_PORT_OFFSET + 1] = src_port >> 8;
	input->byte_stream[IXGBE_ATR_SRC_PORT_OFFSET] = src_port & 0xff;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_atr_set_dst_port_82599 - Sets the destination port
 *  @input: input stream to modify
 *  @dst_port: the destination port to load
 **/
int32_t ixgbe_atr_set_dst_port_82599(struct ixgbe_atr_input *input, uint16_t dst_port)
{
	DEBUGFUNC("ixgbe_atr_set_dst_port_82599");

	input->byte_stream[IXGBE_ATR_DST_PORT_OFFSET + 1] = dst_port >> 8;
	input->byte_stream[IXGBE_ATR_DST_PORT_OFFSET] = dst_port & 0xff;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_atr_set_flex_byte_82599 - Sets the flexible bytes
 *  @input: input stream to modify
 *  @flex_bytes: the flexible bytes to load
 **/
int32_t ixgbe_atr_set_flex_byte_82599(struct ixgbe_atr_input *input, uint16_t flex_byte)
{
	DEBUGFUNC("ixgbe_atr_set_flex_byte_82599");

	input->byte_stream[IXGBE_ATR_FLEX_BYTE_OFFSET + 1] = flex_byte >> 8;
	input->byte_stream[IXGBE_ATR_FLEX_BYTE_OFFSET] = flex_byte & 0xff;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_atr_set_vm_pool_82599 - Sets the Virtual Machine pool
 *  @input: input stream to modify
 *  @vm_pool: the Virtual Machine pool to load
 **/
int32_t ixgbe_atr_set_vm_pool_82599(struct ixgbe_atr_input *input, uint8_t vm_pool)
{
	DEBUGFUNC("ixgbe_atr_set_vm_pool_82599");

	input->byte_stream[IXGBE_ATR_VM_POOL_OFFSET] = vm_pool;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_atr_set_l4type_82599 - Sets the layer 4 packet type
 *  @input: input stream to modify
 *  @l4type: the layer 4 type value to load
 **/
int32_t ixgbe_atr_set_l4type_82599(struct ixgbe_atr_input *input, uint8_t l4type)
{
	DEBUGFUNC("ixgbe_atr_set_l4type_82599");

	input->byte_stream[IXGBE_ATR_L4TYPE_OFFSET] = l4type;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_atr_get_vlan_id_82599 - Gets the VLAN id from the ATR input stream
 *  @input: input stream to search
 *  @vlan: the VLAN id to load
 **/
int32_t ixgbe_atr_get_vlan_id_82599(struct ixgbe_atr_input *input, uint16_t *vlan)
{
	DEBUGFUNC("ixgbe_atr_get_vlan_id_82599");

	*vlan = input->byte_stream[IXGBE_ATR_VLAN_OFFSET];
	*vlan |= input->byte_stream[IXGBE_ATR_VLAN_OFFSET + 1] << 8;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_atr_get_src_ipv4_82599 - Gets the source IPv4 address
 *  @input: input stream to search
 *  @src_addr: the IP address to load
 **/
int32_t ixgbe_atr_get_src_ipv4_82599(struct ixgbe_atr_input *input, uint32_t *src_addr)
{
	DEBUGFUNC("ixgbe_atr_get_src_ipv4_82599");

	*src_addr = input->byte_stream[IXGBE_ATR_SRC_IPV4_OFFSET];
	*src_addr |= input->byte_stream[IXGBE_ATR_SRC_IPV4_OFFSET + 1] << 8;
	*src_addr |= input->byte_stream[IXGBE_ATR_SRC_IPV4_OFFSET + 2] << 16;
	*src_addr |= input->byte_stream[IXGBE_ATR_SRC_IPV4_OFFSET + 3] << 24;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_atr_get_dst_ipv4_82599 - Gets the destination IPv4 address
 *  @input: input stream to search
 *  @dst_addr: the IP address to load
 **/
int32_t ixgbe_atr_get_dst_ipv4_82599(struct ixgbe_atr_input *input, uint32_t *dst_addr)
{
	DEBUGFUNC("ixgbe_atr_get_dst_ipv4_82599");

	*dst_addr = input->byte_stream[IXGBE_ATR_DST_IPV4_OFFSET];
	*dst_addr |= input->byte_stream[IXGBE_ATR_DST_IPV4_OFFSET + 1] << 8;
	*dst_addr |= input->byte_stream[IXGBE_ATR_DST_IPV4_OFFSET + 2] << 16;
	*dst_addr |= input->byte_stream[IXGBE_ATR_DST_IPV4_OFFSET + 3] << 24;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_atr_get_src_ipv6_82599 - Gets the source IPv6 address
 *  @input: input stream to search
 *  @src_addr_1: the first 4 bytes of the IP address to load
 *  @src_addr_2: the second 4 bytes of the IP address to load
 *  @src_addr_3: the third 4 bytes of the IP address to load
 *  @src_addr_4: the fourth 4 bytes of the IP address to load
 **/
int32_t ixgbe_atr_get_src_ipv6_82599(struct ixgbe_atr_input *input,
                                 uint32_t *src_addr_1, uint32_t *src_addr_2,
                                 uint32_t *src_addr_3, uint32_t *src_addr_4)
{
	DEBUGFUNC("ixgbe_atr_get_src_ipv6_82599");

	*src_addr_1 = input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 12];
	*src_addr_1 = input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 13] << 8;
	*src_addr_1 = input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 14] << 16;
	*src_addr_1 = input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 15] << 24;

	*src_addr_2 = input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 8];
	*src_addr_2 = input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 9] << 8;
	*src_addr_2 = input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 10] << 16;
	*src_addr_2 = input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 11] << 24;

	*src_addr_3 = input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 4];
	*src_addr_3 = input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 5] << 8;
	*src_addr_3 = input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 6] << 16;
	*src_addr_3 = input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 7] << 24;

	*src_addr_4 = input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET];
	*src_addr_4 = input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 1] << 8;
	*src_addr_4 = input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 2] << 16;
	*src_addr_4 = input->byte_stream[IXGBE_ATR_SRC_IPV6_OFFSET + 3] << 24;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_atr_get_dst_ipv6_82599 - Gets the destination IPv6 address
 *  @input: input stream to search
 *  @dst_addr_1: the first 4 bytes of the IP address to load
 *  @dst_addr_2: the second 4 bytes of the IP address to load
 *  @dst_addr_3: the third 4 bytes of the IP address to load
 *  @dst_addr_4: the fourth 4 bytes of the IP address to load
 **/
int32_t ixgbe_atr_get_dst_ipv6_82599(struct ixgbe_atr_input *input,
                                 uint32_t *dst_addr_1, uint32_t *dst_addr_2,
                                 uint32_t *dst_addr_3, uint32_t *dst_addr_4)
{
	DEBUGFUNC("ixgbe_atr_get_dst_ipv6_82599");

	*dst_addr_1 = input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 12];
	*dst_addr_1 = input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 13] << 8;
	*dst_addr_1 = input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 14] << 16;
	*dst_addr_1 = input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 15] << 24;

	*dst_addr_2 = input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 8];
	*dst_addr_2 = input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 9] << 8;
	*dst_addr_2 = input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 10] << 16;
	*dst_addr_2 = input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 11] << 24;

	*dst_addr_3 = input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 4];
	*dst_addr_3 = input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 5] << 8;
	*dst_addr_3 = input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 6] << 16;
	*dst_addr_3 = input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 7] << 24;

	*dst_addr_4 = input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET];
	*dst_addr_4 = input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 1] << 8;
	*dst_addr_4 = input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 2] << 16;
	*dst_addr_4 = input->byte_stream[IXGBE_ATR_DST_IPV6_OFFSET + 3] << 24;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_atr_get_src_port_82599 - Gets the source port
 *  @input: input stream to modify
 *  @src_port: the source port to load
 *
 *  Even though the input is given in big-endian, the FDIRPORT registers
 *  expect the ports to be programmed in little-endian.  Hence the need to swap
 *  endianness when retrieving the data.  This can be confusing since the
 *  internal hash engine expects it to be big-endian.
 **/
int32_t ixgbe_atr_get_src_port_82599(struct ixgbe_atr_input *input, uint16_t *src_port)
{
	DEBUGFUNC("ixgbe_atr_get_src_port_82599");

	*src_port = input->byte_stream[IXGBE_ATR_SRC_PORT_OFFSET] << 8;
	*src_port |= input->byte_stream[IXGBE_ATR_SRC_PORT_OFFSET + 1];

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_atr_get_dst_port_82599 - Gets the destination port
 *  @input: input stream to modify
 *  @dst_port: the destination port to load
 *
 *  Even though the input is given in big-endian, the FDIRPORT registers
 *  expect the ports to be programmed in little-endian.  Hence the need to swap
 *  endianness when retrieving the data.  This can be confusing since the
 *  internal hash engine expects it to be big-endian.
 **/
int32_t ixgbe_atr_get_dst_port_82599(struct ixgbe_atr_input *input, uint16_t *dst_port)
{
	DEBUGFUNC("ixgbe_atr_get_dst_port_82599");

	*dst_port = input->byte_stream[IXGBE_ATR_DST_PORT_OFFSET] << 8;
	*dst_port |= input->byte_stream[IXGBE_ATR_DST_PORT_OFFSET + 1];

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_atr_get_flex_byte_82599 - Gets the flexible bytes
 *  @input: input stream to modify
 *  @flex_bytes: the flexible bytes to load
 **/
int32_t ixgbe_atr_get_flex_byte_82599(struct ixgbe_atr_input *input, uint16_t *flex_byte)
{
	DEBUGFUNC("ixgbe_atr_get_flex_byte_82599");

	*flex_byte = input->byte_stream[IXGBE_ATR_FLEX_BYTE_OFFSET];
	*flex_byte |= input->byte_stream[IXGBE_ATR_FLEX_BYTE_OFFSET + 1] << 8;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_atr_get_vm_pool_82599 - Gets the Virtual Machine pool
 *  @input: input stream to modify
 *  @vm_pool: the Virtual Machine pool to load
 **/
int32_t ixgbe_atr_get_vm_pool_82599(struct ixgbe_atr_input *input, uint8_t *vm_pool)
{
	DEBUGFUNC("ixgbe_atr_get_vm_pool_82599");

	*vm_pool = input->byte_stream[IXGBE_ATR_VM_POOL_OFFSET];

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_atr_get_l4type_82599 - Gets the layer 4 packet type
 *  @input: input stream to modify
 *  @l4type: the layer 4 type value to load
 **/
int32_t ixgbe_atr_get_l4type_82599(struct ixgbe_atr_input *input, uint8_t *l4type)
{
	DEBUGFUNC("ixgbe_atr_get_l4type__82599");

	*l4type = input->byte_stream[IXGBE_ATR_L4TYPE_OFFSET];

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_atr_add_signature_filter_82599 - Adds a signature hash filter
 *  @hw: pointer to hardware structure
 *  @stream: input bitstream
 *  @queue: queue index to direct traffic to
 **/
int32_t ixgbe_fdir_add_signature_filter_82599(struct ixgbe_hw *hw,
                                          struct ixgbe_atr_input *input,
                                          uint8_t queue)
{
	uint64_t  fdirhashcmd;
	uint64_t  fdircmd;
	uint32_t  fdirhash;
	uint16_t  bucket_hash, sig_hash;
	uint8_t   l4type;

	DEBUGFUNC("ixgbe_fdir_add_signature_filter_82599");

	bucket_hash = ixgbe_atr_compute_hash_82599(input,
	                                           IXGBE_ATR_BUCKET_HASH_KEY);

	/* bucket_hash is only 15 bits */
	bucket_hash &= IXGBE_ATR_HASH_MASK;

	sig_hash = ixgbe_atr_compute_hash_82599(input,
	                                        IXGBE_ATR_SIGNATURE_HASH_KEY);

	/* Get the l4type in order to program FDIRCMD properly */
	/* lowest 2 bits are FDIRCMD.L4TYPE, third lowest bit is FDIRCMD.IPV6 */
	ixgbe_atr_get_l4type_82599(input, &l4type);

	/*
	 * The lower 32-bits of fdirhashcmd is for FDIRHASH, the upper 32-bits
	 * is for FDIRCMD.  Then do a 64-bit register write from FDIRHASH.
	 */
	fdirhash = sig_hash << IXGBE_FDIRHASH_SIG_SW_INDEX_SHIFT | bucket_hash;

	fdircmd = (IXGBE_FDIRCMD_CMD_ADD_FLOW | IXGBE_FDIRCMD_FILTER_UPDATE |
	           IXGBE_FDIRCMD_LAST | IXGBE_FDIRCMD_QUEUE_EN);

	switch (l4type & IXGBE_ATR_L4TYPE_MASK) {
	case IXGBE_ATR_L4TYPE_TCP:
		fdircmd |= IXGBE_FDIRCMD_L4TYPE_TCP;
		break;
	case IXGBE_ATR_L4TYPE_UDP:
		fdircmd |= IXGBE_FDIRCMD_L4TYPE_UDP;
		break;
	case IXGBE_ATR_L4TYPE_SCTP:
		fdircmd |= IXGBE_FDIRCMD_L4TYPE_SCTP;
		break;
	default:
		DEBUGOUT(" Error on l4type input\n");
		return IXGBE_ERR_CONFIG;
	}

	if (l4type & IXGBE_ATR_L4TYPE_IPV6_MASK)
		fdircmd |= IXGBE_FDIRCMD_IPV6;

	fdircmd |= ((uint64_t)queue << IXGBE_FDIRCMD_RX_QUEUE_SHIFT);
	fdirhashcmd = ((fdircmd << 32) | fdirhash);

	DEBUGOUT2("Tx Queue=%x hash=%x\n", queue, fdirhash & 0x7FFF7FFF);
	IXGBE_WRITE_REG64(hw, IXGBE_FDIRHASH, fdirhashcmd);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_fdir_add_perfect_filter_82599 - Adds a perfect filter
 *  @hw: pointer to hardware structure
 *  @input: input bitstream
 *  @queue: queue index to direct traffic to
 *
 *  Note that the caller to this function must lock before calling, since the
 *  hardware writes must be protected from one another.
 **/
int32_t ixgbe_fdir_add_perfect_filter_82599(struct ixgbe_hw *hw,
                                        struct ixgbe_atr_input *input,
                                        uint16_t soft_id,
                                        uint8_t queue)
{
	uint32_t fdircmd = 0;
	uint32_t fdirhash;
	uint32_t src_ipv4, dst_ipv4;
	uint32_t src_ipv6_1, src_ipv6_2, src_ipv6_3, src_ipv6_4;
	uint16_t src_port, dst_port, vlan_id, flex_bytes;
	uint16_t bucket_hash;
	uint8_t  l4type;

	DEBUGFUNC("ixgbe_fdir_add_perfect_filter_82599");

	/* Get our input values */
	ixgbe_atr_get_l4type_82599(input, &l4type);

	/*
	 * Check l4type formatting, and bail out before we touch the hardware
	 * if there's a configuration issue
	 */
	switch (l4type & IXGBE_ATR_L4TYPE_MASK) {
	case IXGBE_ATR_L4TYPE_TCP:
		fdircmd |= IXGBE_FDIRCMD_L4TYPE_TCP;
		break;
	case IXGBE_ATR_L4TYPE_UDP:
		fdircmd |= IXGBE_FDIRCMD_L4TYPE_UDP;
		break;
	case IXGBE_ATR_L4TYPE_SCTP:
		fdircmd |= IXGBE_FDIRCMD_L4TYPE_SCTP;
		break;
	default:
		DEBUGOUT(" Error on l4type input\n");
		return IXGBE_ERR_CONFIG;
	}

	bucket_hash = ixgbe_atr_compute_hash_82599(input,
	                                           IXGBE_ATR_BUCKET_HASH_KEY);

	/* bucket_hash is only 15 bits */
	bucket_hash &= IXGBE_ATR_HASH_MASK;

	ixgbe_atr_get_vlan_id_82599(input, &vlan_id);
	ixgbe_atr_get_src_port_82599(input, &src_port);
	ixgbe_atr_get_dst_port_82599(input, &dst_port);
	ixgbe_atr_get_flex_byte_82599(input, &flex_bytes);

	fdirhash = soft_id << IXGBE_FDIRHASH_SIG_SW_INDEX_SHIFT | bucket_hash;

	/* Now figure out if we're IPv4 or IPv6 */
	if (l4type & IXGBE_ATR_L4TYPE_IPV6_MASK) {
		/* IPv6 */
		ixgbe_atr_get_src_ipv6_82599(input, &src_ipv6_1, &src_ipv6_2,
	                                     &src_ipv6_3, &src_ipv6_4);

		IXGBE_WRITE_REG(hw, IXGBE_FDIRSIPv6(0), src_ipv6_1);
		IXGBE_WRITE_REG(hw, IXGBE_FDIRSIPv6(1), src_ipv6_2);
		IXGBE_WRITE_REG(hw, IXGBE_FDIRSIPv6(2), src_ipv6_3);
		/* The last 4 bytes is the same register as IPv4 */
		IXGBE_WRITE_REG(hw, IXGBE_FDIRIPSA, src_ipv6_4);

		fdircmd |= IXGBE_FDIRCMD_IPV6;
		fdircmd |= IXGBE_FDIRCMD_IPv6DMATCH;
	} else {
		/* IPv4 */
		ixgbe_atr_get_src_ipv4_82599(input, &src_ipv4);
		IXGBE_WRITE_REG(hw, IXGBE_FDIRIPSA, src_ipv4);

	}

	ixgbe_atr_get_dst_ipv4_82599(input, &dst_ipv4);
	IXGBE_WRITE_REG(hw, IXGBE_FDIRIPDA, dst_ipv4);

	IXGBE_WRITE_REG(hw, IXGBE_FDIRVLAN, (vlan_id |
	                            (flex_bytes << IXGBE_FDIRVLAN_FLEX_SHIFT)));
	IXGBE_WRITE_REG(hw, IXGBE_FDIRPORT, (src_port |
	                       (dst_port << IXGBE_FDIRPORT_DESTINATION_SHIFT)));

	fdircmd |= IXGBE_FDIRCMD_CMD_ADD_FLOW;
	fdircmd |= IXGBE_FDIRCMD_FILTER_UPDATE;
	fdircmd |= IXGBE_FDIRCMD_LAST;
	fdircmd |= IXGBE_FDIRCMD_QUEUE_EN;
	fdircmd |= queue << IXGBE_FDIRCMD_RX_QUEUE_SHIFT;

	IXGBE_WRITE_REG(hw, IXGBE_FDIRHASH, fdirhash);
	IXGBE_WRITE_REG(hw, IXGBE_FDIRCMD, fdircmd);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_read_analog_reg8_82599 - Reads 8 bit Omer analog register
 *  @hw: pointer to hardware structure
 *  @reg: analog register to read
 *  @val: read value
 *
 *  Performs read operation to Omer analog register specified.
 **/
int32_t ixgbe_read_analog_reg8_82599(struct ixgbe_hw *hw, uint32_t reg, uint8_t *val)
{
	uint32_t  core_ctl;

	DEBUGFUNC("ixgbe_read_analog_reg8_82599");

	IXGBE_WRITE_REG(hw, IXGBE_CORECTL, IXGBE_CORECTL_WRITE_CMD |
	                (reg << 8));
	IXGBE_WRITE_FLUSH(hw);
	usec_delay(10);
	core_ctl = IXGBE_READ_REG(hw, IXGBE_CORECTL);
	*val = (uint8_t)core_ctl;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_write_analog_reg8_82599 - Writes 8 bit Omer analog register
 *  @hw: pointer to hardware structure
 *  @reg: atlas register to write
 *  @val: value to write
 *
 *  Performs write operation to Omer analog register specified.
 **/
int32_t ixgbe_write_analog_reg8_82599(struct ixgbe_hw *hw, uint32_t reg, uint8_t val)
{
	uint32_t  core_ctl;

	DEBUGFUNC("ixgbe_write_analog_reg8_82599");

	core_ctl = (reg << 8) | val;
	IXGBE_WRITE_REG(hw, IXGBE_CORECTL, core_ctl);
	IXGBE_WRITE_FLUSH(hw);
	usec_delay(10);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_start_hw_rev_1_82599 - Prepare hardware for Tx/Rx
 *  @hw: pointer to hardware structure
 *
 *  Starts the hardware using the generic start_hw function.
 *  Then performs revision-specific operations:
 *  Clears the rate limiter registers.
 **/
int32_t ixgbe_start_hw_rev_1_82599(struct ixgbe_hw *hw)
{
	uint32_t i;
	uint32_t regval;
	int32_t ret_val = IXGBE_SUCCESS;

	DEBUGFUNC("ixgbe_start_hw_rev_1__82599");

	ret_val = ixgbe_start_hw_generic(hw);

	/* Clear the rate limiters */
	for (i = 0; i < hw->mac.max_tx_queues; i++) {
		IXGBE_WRITE_REG(hw, IXGBE_RTTDQSEL, i);
		IXGBE_WRITE_REG(hw, IXGBE_RTTBCNRC, 0);
	}
	IXGBE_WRITE_FLUSH(hw);

	/* Disable relaxed ordering */
	for (i = 0; i < hw->mac.max_tx_queues; i++) {
		regval = IXGBE_READ_REG(hw, IXGBE_DCA_TXCTRL_82599(i));
		regval &= ~IXGBE_DCA_TXCTRL_TX_WB_RO_EN;
		IXGBE_WRITE_REG(hw, IXGBE_DCA_TXCTRL_82599(i), regval);
	}

	for (i = 0; i < hw->mac.max_rx_queues; i++) {
		regval = IXGBE_READ_REG(hw, IXGBE_DCA_RXCTRL(i));
		regval &= ~(IXGBE_DCA_RXCTRL_DESC_WRO_EN |
			    IXGBE_DCA_RXCTRL_DESC_HSRO_EN);
		IXGBE_WRITE_REG(hw, IXGBE_DCA_RXCTRL(i), regval);
	}

	/* We need to run link autotry after the driver loads */
	hw->mac.autotry_restart = TRUE;

	if (ret_val == IXGBE_SUCCESS)
		ret_val = ixgbe_verify_fw_version_82599(hw);
	return ret_val;
}

/**
 *  ixgbe_identify_phy_82599 - Get physical layer module
 *  @hw: pointer to hardware structure
 *
 *  Determines the physical layer module found on the current adapter.
 *  If PHY already detected, maintains current PHY type in hw struct,
 *  otherwise executes the PHY detection routine.
 **/
int32_t ixgbe_identify_phy_82599(struct ixgbe_hw *hw)
{
	int32_t status = IXGBE_ERR_PHY_ADDR_INVALID;

	DEBUGFUNC("ixgbe_identify_phy_82599");

	/* Detect PHY if not unknown - returns success if already detected. */
	status = ixgbe_identify_phy_generic(hw);
	if (status != IXGBE_SUCCESS)
		status = ixgbe_identify_sfp_module_generic(hw);
	/* Set PHY type none if no PHY detected */
	if (hw->phy.type == ixgbe_phy_unknown) {
		hw->phy.type = ixgbe_phy_none;
		status = IXGBE_SUCCESS;
	}

	/* Return error if SFP module has been detected but is not supported */
	if (hw->phy.type == ixgbe_phy_sfp_unsupported)
		status = IXGBE_ERR_SFP_NOT_SUPPORTED;

	return status;
}

/**
 *  ixgbe_get_supported_physical_layer_82599 - Returns physical layer type
 *  @hw: pointer to hardware structure
 *
 *  Determines physical layer capabilities of the current configuration.
 **/
uint32_t ixgbe_get_supported_physical_layer_82599(struct ixgbe_hw *hw)
{
	uint32_t physical_layer = IXGBE_PHYSICAL_LAYER_UNKNOWN;
	uint32_t autoc = IXGBE_READ_REG(hw, IXGBE_AUTOC);
	uint32_t autoc2 = IXGBE_READ_REG(hw, IXGBE_AUTOC2);
	uint32_t pma_pmd_10g_serial = autoc2 & IXGBE_AUTOC2_10G_SERIAL_PMA_PMD_MASK;
	uint32_t pma_pmd_10g_parallel = autoc & IXGBE_AUTOC_10G_PMA_PMD_MASK;
	uint32_t pma_pmd_1g = autoc & IXGBE_AUTOC_1G_PMA_PMD_MASK;
	uint16_t ext_ability = 0;
	uint8_t comp_codes_10g = 0;

	DEBUGFUNC("ixgbe_get_support_physical_layer_82599");

	hw->phy.ops.identify(hw);

	if (hw->phy.type == ixgbe_phy_tn ||
	    hw->phy.type == ixgbe_phy_aq ||
	    hw->phy.type == ixgbe_phy_cu_unknown) {
		hw->phy.ops.read_reg(hw, IXGBE_MDIO_PHY_EXT_ABILITY,
		IXGBE_MDIO_PMA_PMD_DEV_TYPE, &ext_ability);
		if (ext_ability & IXGBE_MDIO_PHY_10GBASET_ABILITY)
			physical_layer |= IXGBE_PHYSICAL_LAYER_10GBASE_T;
		if (ext_ability & IXGBE_MDIO_PHY_1000BASET_ABILITY)
			physical_layer |= IXGBE_PHYSICAL_LAYER_1000BASE_T;
		if (ext_ability & IXGBE_MDIO_PHY_100BASETX_ABILITY)
			physical_layer |= IXGBE_PHYSICAL_LAYER_100BASE_TX;
		goto out;
	}

	switch (autoc & IXGBE_AUTOC_LMS_MASK) {
	case IXGBE_AUTOC_LMS_1G_AN:
	case IXGBE_AUTOC_LMS_1G_LINK_NO_AN:
		if (pma_pmd_1g == IXGBE_AUTOC_1G_KX_BX) {
			physical_layer = IXGBE_PHYSICAL_LAYER_1000BASE_KX |
			    IXGBE_PHYSICAL_LAYER_1000BASE_BX;
			goto out;
		} else
			/* SFI mode so read SFP module */
			goto sfp_check;
		break;
	case IXGBE_AUTOC_LMS_10G_LINK_NO_AN:
		if (pma_pmd_10g_parallel == IXGBE_AUTOC_10G_CX4)
			physical_layer = IXGBE_PHYSICAL_LAYER_10GBASE_CX4;
		else if (pma_pmd_10g_parallel == IXGBE_AUTOC_10G_KX4)
			physical_layer = IXGBE_PHYSICAL_LAYER_10GBASE_KX4;
		else if (pma_pmd_10g_parallel == IXGBE_AUTOC_10G_XAUI)
			physical_layer = IXGBE_PHYSICAL_LAYER_10GBASE_XAUI;
		goto out;
		break;
	case IXGBE_AUTOC_LMS_10G_SERIAL:
		if (pma_pmd_10g_serial == IXGBE_AUTOC2_10G_KR) {
			physical_layer = IXGBE_PHYSICAL_LAYER_10GBASE_KR;
			goto out;
		} else if (pma_pmd_10g_serial == IXGBE_AUTOC2_10G_SFI)
			goto sfp_check;
		break;
	case IXGBE_AUTOC_LMS_KX4_KX_KR:
	case IXGBE_AUTOC_LMS_KX4_KX_KR_1G_AN:
		if (autoc & IXGBE_AUTOC_KX_SUPP)
			physical_layer |= IXGBE_PHYSICAL_LAYER_1000BASE_KX;
		if (autoc & IXGBE_AUTOC_KX4_SUPP)
			physical_layer |= IXGBE_PHYSICAL_LAYER_10GBASE_KX4;
		if (autoc & IXGBE_AUTOC_KR_SUPP)
			physical_layer |= IXGBE_PHYSICAL_LAYER_10GBASE_KR;
		goto out;
		break;
	default:
		goto out;
		break;
	}

sfp_check:
	/* SFP check must be done last since DA modules are sometimes used to
	 * test KR mode -  we need to id KR mode correctly before SFP module.
	 * Call identify_sfp because the pluggable module may have changed */
	hw->phy.ops.identify_sfp(hw);
	if (hw->phy.sfp_type == ixgbe_sfp_type_not_present)
		goto out;

	switch (hw->phy.type) {
	case ixgbe_phy_tw_tyco:
	case ixgbe_phy_tw_unknown:
		physical_layer = IXGBE_PHYSICAL_LAYER_SFP_PLUS_CU;
		break;
	case ixgbe_phy_sfp_avago:
	case ixgbe_phy_sfp_ftl:
	case ixgbe_phy_sfp_intel:
	case ixgbe_phy_sfp_unknown:
		hw->phy.ops.read_i2c_eeprom(hw,
		      IXGBE_SFF_10GBE_COMP_CODES, &comp_codes_10g);
		if (comp_codes_10g & IXGBE_SFF_10GBASESR_CAPABLE)
			physical_layer = IXGBE_PHYSICAL_LAYER_10GBASE_SR;
		else if (comp_codes_10g & IXGBE_SFF_10GBASELR_CAPABLE)
			physical_layer = IXGBE_PHYSICAL_LAYER_10GBASE_LR;
		break;
	default:
		break;
	}

out:
	return physical_layer;
}

/**
 *  ixgbe_enable_rx_dma_82599 - Enable the Rx DMA unit on 82599
 *  @hw: pointer to hardware structure
 *  @regval: register value to write to RXCTRL
 *
 *  Enables the Rx DMA unit for 82599
 **/
int32_t ixgbe_enable_rx_dma_82599(struct ixgbe_hw *hw, uint32_t regval)
{
#define IXGBE_MAX_SECRX_POLL 30
	int i;
	int secrxreg;

	DEBUGFUNC("ixgbe_enable_rx_dma_82599");

	/*
	 * Workaround for 82599 silicon errata when enabling the Rx datapath.
	 * If traffic is incoming before we enable the Rx unit, it could hang
	 * the Rx DMA unit.  Therefore, make sure the security engine is
	 * completely disabled prior to enabling the Rx unit.
	 */
	secrxreg = IXGBE_READ_REG(hw, IXGBE_SECRXCTRL);
	secrxreg |= IXGBE_SECRXCTRL_RX_DIS;
	IXGBE_WRITE_REG(hw, IXGBE_SECRXCTRL, secrxreg);
	for (i = 0; i < IXGBE_MAX_SECRX_POLL; i++) {
		secrxreg = IXGBE_READ_REG(hw, IXGBE_SECRXSTAT);
		if (secrxreg & IXGBE_SECRXSTAT_SECRX_RDY)
			break;
		else
			/* Use interrupt-safe sleep just in case */
			usec_delay(10);
	}

	/* For informational purposes only */
	if (i >= IXGBE_MAX_SECRX_POLL)
		DEBUGOUT("Rx unit being enabled before security "
		         "path fully disabled.  Continuing with init.\n");

	IXGBE_WRITE_REG(hw, IXGBE_RXCTRL, regval);
	secrxreg = IXGBE_READ_REG(hw, IXGBE_SECRXCTRL);
	secrxreg &= ~IXGBE_SECRXCTRL_RX_DIS;
	IXGBE_WRITE_REG(hw, IXGBE_SECRXCTRL, secrxreg);
	IXGBE_WRITE_FLUSH(hw);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_get_device_caps_82599 - Get additional device capabilities
 *  @hw: pointer to hardware structure
 *  @device_caps: the EEPROM word with the extra device capabilities
 *
 *  This function will read the EEPROM location for the device capabilities,
 *  and return the word through device_caps.
 **/
int32_t ixgbe_get_device_caps_82599(struct ixgbe_hw *hw, uint16_t *device_caps)
{
	DEBUGFUNC("ixgbe_get_device_caps_82599");

	hw->eeprom.ops.read(hw, IXGBE_DEVICE_CAPS, device_caps);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_verify_fw_version_82599 - verify fw version for 82599
 *  @hw: pointer to hardware structure
 *
 *  Verifies that installed the firmware version is 0.6 or higher
 *  for SFI devices. All 82599 SFI devices should have version 0.6 or higher.
 *
 *  Returns IXGBE_ERR_EEPROM_VERSION if the FW is not present or
 *  if the FW version is not supported.
 **/
int32_t ixgbe_verify_fw_version_82599(struct ixgbe_hw *hw)
{
	int32_t status = IXGBE_ERR_EEPROM_VERSION;
	uint16_t fw_offset, fw_ptp_cfg_offset;
	uint16_t fw_version = 0;

	DEBUGFUNC("ixgbe_verify_fw_version_82599");

	/* firmware check is only necessary for SFI devices */
	if (hw->phy.media_type != ixgbe_media_type_fiber) {
		status = IXGBE_SUCCESS;
		goto fw_version_out;
	}

	/* get the offset to the Firmware Module block */
	hw->eeprom.ops.read(hw, IXGBE_FW_PTR, &fw_offset);

	if ((fw_offset == 0) || (fw_offset == 0xFFFF))
		goto fw_version_out;

	/* get the offset to the Pass Through Patch Configuration block */
	hw->eeprom.ops.read(hw, (fw_offset +
	                         IXGBE_FW_PASSTHROUGH_PATCH_CONFIG_PTR),
	                         &fw_ptp_cfg_offset);

	if ((fw_ptp_cfg_offset == 0) || (fw_ptp_cfg_offset == 0xFFFF))
		goto fw_version_out;

	/* get the firmware version */
	hw->eeprom.ops.read(hw, (fw_ptp_cfg_offset +
	                         IXGBE_FW_PATCH_VERSION_4),
	                         &fw_version);

	if (fw_version > 0x5)
		status = IXGBE_SUCCESS;

fw_version_out:
	return status;
}
