/*	$OpenBSD: ixgbe_phy.c,v 1.2 2008/06/08 20:33:51 reyk Exp $	*/

/******************************************************************************

  Copyright (c) 2001-2008, Intel Corporation 
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
/*$FreeBSD: src/sys/dev/ixgbe/ixgbe_phy.c,v 1.4 2008/05/16 18:46:30 jfv Exp $*/

#include <dev/pci/ixgbe.h>

/**
 *  ixgbe_init_phy_ops_generic - Inits PHY function ptrs
 *  @hw: pointer to the hardware structure
 *
 *  Initialize the function pointers.
 **/
int32_t ixgbe_init_phy_ops_generic(struct ixgbe_hw *hw)
{
	struct ixgbe_phy_info *phy = &hw->phy;

	/* PHY */
	phy->ops.identify = &ixgbe_identify_phy_generic;
	phy->ops.reset = &ixgbe_reset_phy_generic;
	phy->ops.read_reg = &ixgbe_read_phy_reg_generic;
	phy->ops.write_reg = &ixgbe_write_phy_reg_generic;
	phy->ops.setup_link = &ixgbe_setup_phy_link_generic;
	phy->ops.setup_link_speed = &ixgbe_setup_phy_link_speed_generic;
	phy->ops.check_link = NULL;
	phy->ops.get_firmware_version = NULL;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_identify_phy_generic - Get physical layer module
 *  @hw: pointer to hardware structure
 *
 *  Determines the physical layer module found on the current adapter.
 **/
int32_t ixgbe_identify_phy_generic(struct ixgbe_hw *hw)
{
	int32_t status = IXGBE_ERR_PHY_ADDR_INVALID;
	uint32_t phy_addr;

	if (hw->phy.type == ixgbe_phy_unknown) {
		for (phy_addr = 0; phy_addr < IXGBE_MAX_PHY_ADDR; phy_addr++) {
			if (ixgbe_validate_phy_addr(hw, phy_addr)) {
				hw->phy.addr = phy_addr;
				ixgbe_get_phy_id(hw);
				hw->phy.type =
				        ixgbe_get_phy_type_from_id(hw->phy.id);
				status = IXGBE_SUCCESS;
				break;
			}
		}
	} else {
		status = IXGBE_SUCCESS;
	}

	return status;
}

/**
 *  ixgbe_validate_phy_addr - Determines phy address is valid
 *  @hw: pointer to hardware structure
 *
 **/
int ixgbe_validate_phy_addr(struct ixgbe_hw *hw, uint32_t phy_addr)
{
	uint16_t phy_id = 0;
	int valid = FALSE;

	hw->phy.addr = phy_addr;
	hw->phy.ops.read_reg(hw, IXGBE_MDIO_PHY_ID_HIGH,
	                     IXGBE_MDIO_PMA_PMD_DEV_TYPE, &phy_id);

	if (phy_id != 0xFFFF && phy_id != 0x0)
		valid = TRUE;

	return valid;
}

/**
 *  ixgbe_get_phy_id - Get the phy type
 *  @hw: pointer to hardware structure
 *
 **/
int32_t ixgbe_get_phy_id(struct ixgbe_hw *hw)
{
	uint32_t status;
	uint16_t phy_id_high = 0;
	uint16_t phy_id_low = 0;

	status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_PHY_ID_HIGH,
	                              IXGBE_MDIO_PMA_PMD_DEV_TYPE,
	                              &phy_id_high);

	if (status == IXGBE_SUCCESS) {
		hw->phy.id = (uint32_t)(phy_id_high << 16);
		status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_PHY_ID_LOW,
		                              IXGBE_MDIO_PMA_PMD_DEV_TYPE,
		                              &phy_id_low);
		hw->phy.id |= (uint32_t)(phy_id_low & IXGBE_PHY_REVISION_MASK);
		hw->phy.revision = (uint32_t)(phy_id_low & ~IXGBE_PHY_REVISION_MASK);
	}

	return status;
}

/**
 *  ixgbe_get_phy_type_from_id - Get the phy type
 *  @hw: pointer to hardware structure
 *
 **/
enum ixgbe_phy_type ixgbe_get_phy_type_from_id(uint32_t phy_id)
{
	enum ixgbe_phy_type phy_type;

	switch (phy_id) {
	case TN1010_PHY_ID:
		phy_type = ixgbe_phy_tn;
		break;
	case QT2022_PHY_ID:
		phy_type = ixgbe_phy_qt;
		break;
	case ATH_PHY_ID:
		phy_type = ixgbe_phy_nl;
		break;
	default:
		phy_type = ixgbe_phy_unknown;
		break;
	}

	DEBUGOUT1("phy type found is %d\n", phy_type);
	return phy_type;
}

/**
 *  ixgbe_reset_phy_generic - Performs a PHY reset
 *  @hw: pointer to hardware structure
 **/
int32_t ixgbe_reset_phy_generic(struct ixgbe_hw *hw)
{
	/*
	 * Perform soft PHY reset to the PHY_XS.
	 * This will cause a soft reset to the PHY
	 */
	return hw->phy.ops.write_reg(hw, IXGBE_MDIO_PHY_XS_CONTROL,
	                             IXGBE_MDIO_PHY_XS_DEV_TYPE,
	                             IXGBE_MDIO_PHY_XS_RESET);
}

/**
 *  ixgbe_read_phy_reg_generic - Reads a value from a specified PHY register
 *  @hw: pointer to hardware structure
 *  @reg_addr: 32 bit address of PHY register to read
 *  @phy_data: Pointer to read data from PHY register
 **/
int32_t ixgbe_read_phy_reg_generic(struct ixgbe_hw *hw, uint32_t reg_addr,
                               uint32_t device_type, uint16_t *phy_data)
{
	uint32_t command;
	uint32_t i;
	uint32_t data;
	int32_t status = IXGBE_SUCCESS;
	uint16_t gssr;

	if (IXGBE_READ_REG(hw, IXGBE_STATUS) & IXGBE_STATUS_LAN_ID_1)
		gssr = IXGBE_GSSR_PHY1_SM;
	else
		gssr = IXGBE_GSSR_PHY0_SM;

	if (ixgbe_acquire_swfw_sync(hw, gssr) != IXGBE_SUCCESS)
		status = IXGBE_ERR_SWFW_SYNC;

	if (status == IXGBE_SUCCESS) {
		/* Setup and write the address cycle command */
		command = ((reg_addr << IXGBE_MSCA_NP_ADDR_SHIFT)  |
		           (device_type << IXGBE_MSCA_DEV_TYPE_SHIFT) |
		           (hw->phy.addr << IXGBE_MSCA_PHY_ADDR_SHIFT) |
		           (IXGBE_MSCA_ADDR_CYCLE | IXGBE_MSCA_MDI_COMMAND));

		IXGBE_WRITE_REG(hw, IXGBE_MSCA, command);

		/*
		 * Check every 10 usec to see if the address cycle completed.
		 * The MDI Command bit will clear when the operation is
		 * complete
		 */
		for (i = 0; i < IXGBE_MDIO_COMMAND_TIMEOUT; i++) {
			usec_delay(10);

			command = IXGBE_READ_REG(hw, IXGBE_MSCA);

			if ((command & IXGBE_MSCA_MDI_COMMAND) == 0) {
				break;
			}
		}

		if ((command & IXGBE_MSCA_MDI_COMMAND) != 0) {
			DEBUGOUT("PHY address command did not complete.\n");
			status = IXGBE_ERR_PHY;
		}

		if (status == IXGBE_SUCCESS) {
			/*
			 * Address cycle complete, setup and write the read
			 * command
			 */
			command = ((reg_addr << IXGBE_MSCA_NP_ADDR_SHIFT)  |
			           (device_type << IXGBE_MSCA_DEV_TYPE_SHIFT) |
			           (hw->phy.addr << IXGBE_MSCA_PHY_ADDR_SHIFT) |
			           (IXGBE_MSCA_READ | IXGBE_MSCA_MDI_COMMAND));

			IXGBE_WRITE_REG(hw, IXGBE_MSCA, command);

			/*
			 * Check every 10 usec to see if the address cycle
			 * completed. The MDI Command bit will clear when the
			 * operation is complete
			 */
			for (i = 0; i < IXGBE_MDIO_COMMAND_TIMEOUT; i++) {
				usec_delay(10);

				command = IXGBE_READ_REG(hw, IXGBE_MSCA);

				if ((command & IXGBE_MSCA_MDI_COMMAND) == 0)
					break;
			}

			if ((command & IXGBE_MSCA_MDI_COMMAND) != 0) {
				DEBUGOUT("PHY read command didn't complete\n");
				status = IXGBE_ERR_PHY;
			} else {
				/*
				 * Read operation is complete.  Get the data
				 * from MSRWD
				 */
				data = IXGBE_READ_REG(hw, IXGBE_MSRWD);
				data >>= IXGBE_MSRWD_READ_DATA_SHIFT;
				*phy_data = (uint16_t)(data);
			}
		}

		ixgbe_release_swfw_sync(hw, gssr);
	}

	return status;
}

/**
 *  ixgbe_write_phy_reg_generic - Writes a value to specified PHY register
 *  @hw: pointer to hardware structure
 *  @reg_addr: 32 bit PHY register to write
 *  @device_type: 5 bit device type
 *  @phy_data: Data to write to the PHY register
 **/
int32_t ixgbe_write_phy_reg_generic(struct ixgbe_hw *hw, uint32_t reg_addr,
                                uint32_t device_type, uint16_t phy_data)
{
	uint32_t command;
	uint32_t i;
	int32_t status = IXGBE_SUCCESS;
	uint16_t gssr;

	if (IXGBE_READ_REG(hw, IXGBE_STATUS) & IXGBE_STATUS_LAN_ID_1)
		gssr = IXGBE_GSSR_PHY1_SM;
	else
		gssr = IXGBE_GSSR_PHY0_SM;

	if (ixgbe_acquire_swfw_sync(hw, gssr) != IXGBE_SUCCESS)
		status = IXGBE_ERR_SWFW_SYNC;

	if (status == IXGBE_SUCCESS) {
		/* Put the data in the MDI single read and write data register*/
		IXGBE_WRITE_REG(hw, IXGBE_MSRWD, (uint32_t)phy_data);

		/* Setup and write the address cycle command */
		command = ((reg_addr << IXGBE_MSCA_NP_ADDR_SHIFT)  |
		           (device_type << IXGBE_MSCA_DEV_TYPE_SHIFT) |
		           (hw->phy.addr << IXGBE_MSCA_PHY_ADDR_SHIFT) |
		           (IXGBE_MSCA_ADDR_CYCLE | IXGBE_MSCA_MDI_COMMAND));

		IXGBE_WRITE_REG(hw, IXGBE_MSCA, command);

		/*
		 * Check every 10 usec to see if the address cycle completed.
		 * The MDI Command bit will clear when the operation is
		 * complete
		 */
		for (i = 0; i < IXGBE_MDIO_COMMAND_TIMEOUT; i++) {
			usec_delay(10);

			command = IXGBE_READ_REG(hw, IXGBE_MSCA);

			if ((command & IXGBE_MSCA_MDI_COMMAND) == 0)
				break;
		}

		if ((command & IXGBE_MSCA_MDI_COMMAND) != 0) {
			DEBUGOUT("PHY address cmd didn't complete\n");
			status = IXGBE_ERR_PHY;
		}

		if (status == IXGBE_SUCCESS) {
			/*
			 * Address cycle complete, setup and write the write
			 * command
			 */
			command = ((reg_addr << IXGBE_MSCA_NP_ADDR_SHIFT)  |
			           (device_type << IXGBE_MSCA_DEV_TYPE_SHIFT) |
			           (hw->phy.addr << IXGBE_MSCA_PHY_ADDR_SHIFT) |
			           (IXGBE_MSCA_WRITE | IXGBE_MSCA_MDI_COMMAND));

			IXGBE_WRITE_REG(hw, IXGBE_MSCA, command);

			/*
			 * Check every 10 usec to see if the address cycle
			 * completed. The MDI Command bit will clear when the
			 * operation is complete
			 */
			for (i = 0; i < IXGBE_MDIO_COMMAND_TIMEOUT; i++) {
				usec_delay(10);

				command = IXGBE_READ_REG(hw, IXGBE_MSCA);

				if ((command & IXGBE_MSCA_MDI_COMMAND) == 0)
					break;
			}

			if ((command & IXGBE_MSCA_MDI_COMMAND) != 0) {
				DEBUGOUT("PHY address cmd didn't complete\n");
				status = IXGBE_ERR_PHY;
			}
		}

		ixgbe_release_swfw_sync(hw, gssr);
	}

	return status;
}

/**
 *  ixgbe_setup_phy_link_generic - Set and restart autoneg
 *  @hw: pointer to hardware structure
 *
 *  Restart autonegotiation and PHY and waits for completion.
 **/
int32_t ixgbe_setup_phy_link_generic(struct ixgbe_hw *hw)
{
	int32_t status = IXGBE_NOT_IMPLEMENTED;
	uint32_t time_out;
	uint32_t max_time_out = 10;
	uint16_t autoneg_reg = IXGBE_MII_AUTONEG_REG;

	/*
	 * Set advertisement settings in PHY based on autoneg_advertised
	 * settings. If autoneg_advertised = 0, then advertise default values
	 * tnx devices cannot be "forced" to a autoneg 10G and fail.  But can
	 * for a 1G.
	 */
	hw->phy.ops.read_reg(hw, IXGBE_MII_SPEED_SELECTION_REG,
	                     IXGBE_MDIO_AUTO_NEG_DEV_TYPE, &autoneg_reg);

	if (hw->phy.autoneg_advertised == IXGBE_LINK_SPEED_1GB_FULL)
		autoneg_reg &= 0xEFFF; /* 0 in bit 12 is 1G operation */
	else
		autoneg_reg |= 0x1000; /* 1 in bit 12 is 10G/1G operation */

	hw->phy.ops.write_reg(hw, IXGBE_MII_SPEED_SELECTION_REG,
	                      IXGBE_MDIO_AUTO_NEG_DEV_TYPE, autoneg_reg);

	/* Restart PHY autonegotiation and wait for completion */
	hw->phy.ops.read_reg(hw, IXGBE_MDIO_AUTO_NEG_CONTROL,
	                     IXGBE_MDIO_AUTO_NEG_DEV_TYPE, &autoneg_reg);

	autoneg_reg |= IXGBE_MII_RESTART;

	hw->phy.ops.write_reg(hw, IXGBE_MDIO_AUTO_NEG_CONTROL,
	                      IXGBE_MDIO_AUTO_NEG_DEV_TYPE, autoneg_reg);

	/* Wait for autonegotiation to finish */
	for (time_out = 0; time_out < max_time_out; time_out++) {
		usec_delay(10);
		/* Restart PHY autonegotiation and wait for completion */
		status = hw->phy.ops.read_reg(hw, IXGBE_MDIO_AUTO_NEG_STATUS,
		                              IXGBE_MDIO_AUTO_NEG_DEV_TYPE,
		                              &autoneg_reg);

		autoneg_reg &= IXGBE_MII_AUTONEG_COMPLETE;
		if (autoneg_reg == IXGBE_MII_AUTONEG_COMPLETE) {
			status = IXGBE_SUCCESS;
			break;
		}
	}

	if (time_out == max_time_out)
		status = IXGBE_ERR_LINK_SETUP;

	return status;
}

/**
 *  ixgbe_setup_phy_link_speed_generic - Sets the auto advertised capabilities
 *  @hw: pointer to hardware structure
 *  @speed: new link speed
 *  @autoneg: TRUE if autonegotiation enabled
 **/
int32_t ixgbe_setup_phy_link_speed_generic(struct ixgbe_hw *hw,
                                       ixgbe_link_speed speed,
                                       int autoneg,
                                       int autoneg_wait_to_complete)
{
	UNREFERENCED_PARAMETER(autoneg);
	UNREFERENCED_PARAMETER(autoneg_wait_to_complete);

	/*
	 * Clear autoneg_advertised and set new values based on input link
	 * speed.
	 */
	hw->phy.autoneg_advertised = 0;

	if (speed & IXGBE_LINK_SPEED_10GB_FULL) {
		hw->phy.autoneg_advertised |= IXGBE_LINK_SPEED_10GB_FULL;
	}
	if (speed & IXGBE_LINK_SPEED_1GB_FULL) {
		hw->phy.autoneg_advertised |= IXGBE_LINK_SPEED_1GB_FULL;
	}

	/* Setup link based on the new speed settings */
	hw->phy.ops.setup_link(hw);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_check_phy_link_tnx - Determine link and speed status
 *  @hw: pointer to hardware structure
 *
 *  Reads the VS1 register to determine if link is up and the current speed for
 *  the PHY.
 **/
int32_t ixgbe_check_phy_link_tnx(struct ixgbe_hw *hw, ixgbe_link_speed *speed,
                             int *link_up)
{
	int32_t status = IXGBE_SUCCESS;
	uint32_t time_out;
	uint32_t max_time_out = 10;
	uint16_t phy_link = 0;
	uint16_t phy_speed = 0;
	uint16_t phy_data = 0;

	/* Initialize speed and link to default case */
	*link_up = FALSE;
	*speed = IXGBE_LINK_SPEED_10GB_FULL;

	/*
	 * Check current speed and link status of the PHY register.
	 * This is a vendor specific register and may have to
	 * be changed for other copper PHYs.
	 */
	for (time_out = 0; time_out < max_time_out; time_out++) {
		usec_delay(10);
		status = hw->phy.ops.read_reg(hw,
		                        IXGBE_MDIO_VENDOR_SPECIFIC_1_STATUS,
		                        IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
		                        &phy_data);
		phy_link = phy_data &
		           IXGBE_MDIO_VENDOR_SPECIFIC_1_LINK_STATUS;
		phy_speed = phy_data &
		            IXGBE_MDIO_VENDOR_SPECIFIC_1_SPEED_STATUS;
		if (phy_link == IXGBE_MDIO_VENDOR_SPECIFIC_1_LINK_STATUS) {
			*link_up = TRUE;
			if (phy_speed ==
			    IXGBE_MDIO_VENDOR_SPECIFIC_1_SPEED_STATUS)
				*speed = IXGBE_LINK_SPEED_1GB_FULL;
			break;
		}
	}

	return status;
}

/**
 *  ixgbe_get_phy_firmware_version_tnx - Gets the PHY Firmware Version
 *  @hw: pointer to hardware structure
 *  @firmware_version: pointer to the PHY Firmware Version
 **/
int32_t ixgbe_get_phy_firmware_version_tnx(struct ixgbe_hw *hw,
                                       uint16_t *firmware_version)
{
	int32_t status = IXGBE_SUCCESS;

	status = hw->phy.ops.read_reg(hw, TNX_FW_REV,
	                              IXGBE_MDIO_VENDOR_SPECIFIC_1_DEV_TYPE,
	                              firmware_version);

	return status;
}

/**
 *  ixgbe_reset_phy_nl - Performs a PHY reset
 *  @hw: pointer to hardware structure
 **/
int32_t ixgbe_reset_phy_nl(struct ixgbe_hw *hw)
{
	uint16_t phy_offset, control, eword, edata, list_crc, block_crc, id, sfp_id;
	int end_data = FALSE;
	uint16_t list_offset, data_offset;
	uint16_t phy_data = 0;
	int32_t ret_val = IXGBE_SUCCESS;
	uint32_t i;

	hw->phy.ops.read_reg(hw, IXGBE_MDIO_PHY_XS_CONTROL,
	                     IXGBE_MDIO_PHY_XS_DEV_TYPE, &phy_data);

	/* reset the PHY and poll for completion */
	hw->phy.ops.write_reg(hw, IXGBE_MDIO_PHY_XS_CONTROL,
	                      IXGBE_MDIO_PHY_XS_DEV_TYPE,
	                      (phy_data | IXGBE_MDIO_PHY_XS_RESET));

	for (i = 0; i < 100; i++) {
		hw->phy.ops.read_reg(hw, IXGBE_MDIO_PHY_XS_CONTROL,
		                     IXGBE_MDIO_PHY_XS_DEV_TYPE, &phy_data);
		if ((phy_data & IXGBE_MDIO_PHY_XS_RESET) == 0 )
			break;
		msec_delay(10);
	}

	if ((phy_data & IXGBE_MDIO_PHY_XS_RESET) != 0) {
		DEBUGOUT("PHY reset did not complete.\n");
		ret_val = IXGBE_ERR_PHY;
		goto out;
	}

	/* read offset to PHY init contents */
	hw->eeprom.ops.read(hw, IXGBE_PHY_INIT_OFFSET_NL, &list_offset);

	if ((!list_offset) || (list_offset == 0xFFFF)) {
		ret_val = IXGBE_ERR_PHY;
		goto out;
	}

	/* Acquire the CRC */
	hw->eeprom.ops.read(hw, list_offset, &list_crc);

	/* Shift offset to first ID word */
	list_offset++;

	/* determine the sfp sequence based on device ID */
	switch (hw->device_id) {
	case IXGBE_DEV_ID_82598_DA_DUAL_PORT:
		sfp_id = 0;
		break;
	case IXGBE_DEV_ID_82598_SR_DUAL_PORT_EM:
		sfp_id = 1;
		break;
	default:
		ret_val = IXGBE_ERR_PHY;
		goto out;
	}

	/*
	 * Find the matching sfp ID in the EEPROM
	 * and program the init sequence
	 */
	hw->eeprom.ops.read(hw, list_offset, &id);

	while (!((id == IXGBE_CONTROL_EOL_NL) || (end_data == TRUE))) {
		if (id == sfp_id) {
			list_offset++;
			hw->eeprom.ops.read(hw, list_offset, &data_offset);
			if ((!data_offset) || (data_offset == 0xFFFF))
				break;
			ret_val = hw->eeprom.ops.read(hw, data_offset,
			                              &block_crc);
			data_offset++;
			while (!end_data) {
				/*
				 * Read control word from PHY init contents
				 * offset
				 */
				ret_val = hw->eeprom.ops.read(hw, data_offset,
				                              &eword);
				control = (eword & IXGBE_CONTROL_MASK_NL) >>
				          IXGBE_CONTROL_SHIFT_NL;
				edata = eword & IXGBE_DATA_MASK_NL;
				switch (control) {
				case IXGBE_DELAY_NL:
					data_offset++;
					DEBUGOUT1("DELAY: %d MS\n", edata);
					msec_delay(edata);
					break;
				case IXGBE_DATA_NL:
					DEBUGOUT("DATA:  \n");
					data_offset++;
					hw->eeprom.ops.read(hw, data_offset++,
					                    &phy_offset);
					for (i = 0; i < edata; i++) {
						hw->eeprom.ops.read(hw,
						                   data_offset,
						                   &eword);
						hw->phy.ops.write_reg(hw,
						              phy_offset,
						              IXGBE_TWINAX_DEV,
						              eword);
						DEBUGOUT2("Wrote %4.4x to %4.4x\n",
						          eword, phy_offset);
						data_offset++;
						phy_offset++;
					}
					break;
				case IXGBE_CONTROL_NL:
					data_offset++;
					DEBUGOUT("CONTROL: \n");
					if (edata == IXGBE_CONTROL_EOL_NL) {
						DEBUGOUT("EOL\n");
						end_data = TRUE;
					} else if (edata == IXGBE_CONTROL_SOL_NL) {
						DEBUGOUT("SOL\n");
					} else {
						DEBUGOUT("Bad control value\n");
						ret_val = IXGBE_ERR_PHY;
						goto out;
					}
					break;
				default:
					DEBUGOUT("Bad control type\n");
					ret_val = IXGBE_ERR_PHY;
					goto out;
				}
			}
		} else {
			list_offset += 2;
			ret_val = hw->eeprom.ops.read(hw, list_offset, &id);
			if (ret_val)
				goto out;
		}
	}

out:
	return ret_val;
}
