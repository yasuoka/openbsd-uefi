/*	$OpenBSD: ixgbe.c,v 1.3 2008/06/08 21:15:34 reyk Exp $	*/

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
/*$FreeBSD: src/sys/dev/ixgbe/ixgbe_common.c,v 1.4 2008/05/16 18:46:30 jfv Exp $*/

#include <dev/pci/ixgbe.h>

static int32_t ixgbe_poll_eeprom_eerd_done(struct ixgbe_hw *hw);
static int32_t ixgbe_acquire_eeprom(struct ixgbe_hw *hw);
static int32_t ixgbe_get_eeprom_semaphore(struct ixgbe_hw *hw);
static void ixgbe_release_eeprom_semaphore(struct ixgbe_hw *hw);
static int32_t ixgbe_ready_eeprom(struct ixgbe_hw *hw);
static void ixgbe_standby_eeprom(struct ixgbe_hw *hw);
static void ixgbe_shift_out_eeprom_bits(struct ixgbe_hw *hw, uint16_t data,
                                        uint16_t count);
static uint16_t ixgbe_shift_in_eeprom_bits(struct ixgbe_hw *hw, uint16_t count);
static void ixgbe_raise_eeprom_clk(struct ixgbe_hw *hw, uint32_t *eec);
static void ixgbe_lower_eeprom_clk(struct ixgbe_hw *hw, uint32_t *eec);
static void ixgbe_release_eeprom(struct ixgbe_hw *hw);
static uint16_t ixgbe_calc_eeprom_checksum(struct ixgbe_hw *hw);

static void ixgbe_enable_rar(struct ixgbe_hw *hw, uint32_t index);
static void ixgbe_disable_rar(struct ixgbe_hw *hw, uint32_t index);
static int32_t ixgbe_mta_vector(struct ixgbe_hw *hw, uint8_t *mc_addr);
void ixgbe_add_mc_addr(struct ixgbe_hw *hw, uint8_t *mc_addr);
void ixgbe_add_uc_addr(struct ixgbe_hw *hw, uint8_t *addr, uint32_t vmdq);

/**
 *  ixgbe_init_ops_generic - Inits function ptrs
 *  @hw: pointer to the hardware structure
 *
 *  Initialize the function pointers.
 **/
int32_t ixgbe_init_ops_generic(struct ixgbe_hw *hw)
{
	struct ixgbe_eeprom_info *eeprom = &hw->eeprom;
	struct ixgbe_mac_info *mac = &hw->mac;
	uint32_t eec = IXGBE_READ_REG(hw, IXGBE_EEC);

	/* EEPROM */
	eeprom->ops.init_params = &ixgbe_init_eeprom_params_generic;
	/* If EEPROM is valid (bit 8 = 1), use EERD otherwise use bit bang */
	if (eec & (1 << 8))
		eeprom->ops.read = &ixgbe_read_eeprom_generic;
	else
		eeprom->ops.read = &ixgbe_read_eeprom_bit_bang_generic;
	eeprom->ops.write = &ixgbe_write_eeprom_generic;
	eeprom->ops.validate_checksum =
	                              &ixgbe_validate_eeprom_checksum_generic;
	eeprom->ops.update_checksum = &ixgbe_update_eeprom_checksum_generic;

	/* MAC */
	mac->ops.init_hw = &ixgbe_init_hw_generic;
	mac->ops.reset_hw = NULL;
	mac->ops.start_hw = &ixgbe_start_hw_generic;
	mac->ops.clear_hw_cntrs = &ixgbe_clear_hw_cntrs_generic;
	mac->ops.get_media_type = NULL;
	mac->ops.get_mac_addr = &ixgbe_get_mac_addr_generic;
	mac->ops.stop_adapter = &ixgbe_stop_adapter_generic;
	mac->ops.get_bus_info = &ixgbe_get_bus_info_generic;
	mac->ops.read_analog_reg8 = &ixgbe_read_analog_reg8_generic;
	mac->ops.write_analog_reg8 = &ixgbe_write_analog_reg8_generic;

	/* LEDs */
	mac->ops.led_on = &ixgbe_led_on_generic;
	mac->ops.led_off = &ixgbe_led_off_generic;
	mac->ops.blink_led_start = NULL;
	mac->ops.blink_led_stop = NULL;

	/* RAR, Multicast, VLAN */
	mac->ops.set_rar = &ixgbe_set_rar_generic;
	mac->ops.set_vmdq = NULL;
	mac->ops.init_rx_addrs = &ixgbe_init_rx_addrs_generic;
	mac->ops.update_uc_addr_list = &ixgbe_update_uc_addr_list_generic;
	mac->ops.update_mc_addr_list = &ixgbe_update_mc_addr_list_generic;
	mac->ops.enable_mc = &ixgbe_enable_mc_generic;
	mac->ops.disable_mc = &ixgbe_disable_mc_generic;
	mac->ops.clear_vfta = &ixgbe_clear_vfta_generic;
	mac->ops.set_vfta = &ixgbe_set_vfta_generic;

	/* Flow Control */
	mac->ops.setup_fc = NULL;

	/* Link */
	mac->ops.get_link_capabilities = NULL;
	mac->ops.setup_link = NULL;
	mac->ops.setup_link_speed = NULL;
	mac->ops.check_link = NULL;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_start_hw_generic - Prepare hardware for Tx/Rx
 *  @hw: pointer to hardware structure
 *
 *  Starts the hardware by filling the bus info structure and media type, clears
 *  all on chip counters, initializes receive address registers, multicast
 *  table, VLAN filter table, calls routine to set up link and flow control
 *  settings, and leaves transmit and receive units disabled and uninitialized
 **/
int32_t ixgbe_start_hw_generic(struct ixgbe_hw *hw)
{
	uint32_t ctrl_ext;

	/* Set the media type */
	hw->phy.media_type = hw->mac.ops.get_media_type(hw);

	/* Set bus info */
	hw->mac.ops.get_bus_info(hw);

	/* Identify the PHY */
	hw->phy.ops.identify(hw);

	/*
	 * Store MAC address from RAR0, clear receive address registers, and
	 * clear the multicast table
	 */
	hw->mac.ops.init_rx_addrs(hw);

	/* Clear the VLAN filter table */
	hw->mac.ops.clear_vfta(hw);

	/* Set up link */
	hw->mac.ops.setup_link(hw);

	/* Clear statistics registers */
	hw->mac.ops.clear_hw_cntrs(hw);

	/* Set No Snoop Disable */
	ctrl_ext = IXGBE_READ_REG(hw, IXGBE_CTRL_EXT);
	ctrl_ext |= IXGBE_CTRL_EXT_NS_DIS;
	IXGBE_WRITE_REG(hw, IXGBE_CTRL_EXT, ctrl_ext);
	IXGBE_WRITE_FLUSH(hw);

	/* Clear adapter stopped flag */
	hw->adapter_stopped = FALSE;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_init_hw_generic - Generic hardware initialization
 *  @hw: pointer to hardware structure
 *
 *  Initialize the hardware by resetting the hardware, filling the bus info
 *  structure and media type, clears all on chip counters, initializes receive
 *  address registers, multicast table, VLAN filter table, calls routine to set
 *  up link and flow control settings, and leaves transmit and receive units
 *  disabled and uninitialized
 **/
int32_t ixgbe_init_hw_generic(struct ixgbe_hw *hw)
{
	/* Reset the hardware */
	hw->mac.ops.reset_hw(hw);

	/* Start the HW */
	hw->mac.ops.start_hw(hw);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_clear_hw_cntrs_generic - Generic clear hardware counters
 *  @hw: pointer to hardware structure
 *
 *  Clears all hardware statistics counters by reading them from the hardware
 *  Statistics counters are clear on read.
 **/
int32_t ixgbe_clear_hw_cntrs_generic(struct ixgbe_hw *hw)
{
	uint16_t i = 0;

	IXGBE_READ_REG(hw, IXGBE_CRCERRS);
	IXGBE_READ_REG(hw, IXGBE_ILLERRC);
	IXGBE_READ_REG(hw, IXGBE_ERRBC);
	IXGBE_READ_REG(hw, IXGBE_MSPDC);
	for (i = 0; i < 8; i++)
		IXGBE_READ_REG(hw, IXGBE_MPC(i));

	IXGBE_READ_REG(hw, IXGBE_MLFC);
	IXGBE_READ_REG(hw, IXGBE_MRFC);
	IXGBE_READ_REG(hw, IXGBE_RLEC);
	IXGBE_READ_REG(hw, IXGBE_LXONTXC);
	IXGBE_READ_REG(hw, IXGBE_LXONRXC);
	IXGBE_READ_REG(hw, IXGBE_LXOFFTXC);
	IXGBE_READ_REG(hw, IXGBE_LXOFFRXC);

	for (i = 0; i < 8; i++) {
		IXGBE_READ_REG(hw, IXGBE_PXONTXC(i));
		IXGBE_READ_REG(hw, IXGBE_PXONRXC(i));
		IXGBE_READ_REG(hw, IXGBE_PXOFFTXC(i));
		IXGBE_READ_REG(hw, IXGBE_PXOFFRXC(i));
	}

	IXGBE_READ_REG(hw, IXGBE_PRC64);
	IXGBE_READ_REG(hw, IXGBE_PRC127);
	IXGBE_READ_REG(hw, IXGBE_PRC255);
	IXGBE_READ_REG(hw, IXGBE_PRC511);
	IXGBE_READ_REG(hw, IXGBE_PRC1023);
	IXGBE_READ_REG(hw, IXGBE_PRC1522);
	IXGBE_READ_REG(hw, IXGBE_GPRC);
	IXGBE_READ_REG(hw, IXGBE_BPRC);
	IXGBE_READ_REG(hw, IXGBE_MPRC);
	IXGBE_READ_REG(hw, IXGBE_GPTC);
	IXGBE_READ_REG(hw, IXGBE_GORCL);
	IXGBE_READ_REG(hw, IXGBE_GORCH);
	IXGBE_READ_REG(hw, IXGBE_GOTCL);
	IXGBE_READ_REG(hw, IXGBE_GOTCH);
	for (i = 0; i < 8; i++)
		IXGBE_READ_REG(hw, IXGBE_RNBC(i));
	IXGBE_READ_REG(hw, IXGBE_RUC);
	IXGBE_READ_REG(hw, IXGBE_RFC);
	IXGBE_READ_REG(hw, IXGBE_ROC);
	IXGBE_READ_REG(hw, IXGBE_RJC);
	IXGBE_READ_REG(hw, IXGBE_MNGPRC);
	IXGBE_READ_REG(hw, IXGBE_MNGPDC);
	IXGBE_READ_REG(hw, IXGBE_MNGPTC);
	IXGBE_READ_REG(hw, IXGBE_TORL);
	IXGBE_READ_REG(hw, IXGBE_TORH);
	IXGBE_READ_REG(hw, IXGBE_TPR);
	IXGBE_READ_REG(hw, IXGBE_TPT);
	IXGBE_READ_REG(hw, IXGBE_PTC64);
	IXGBE_READ_REG(hw, IXGBE_PTC127);
	IXGBE_READ_REG(hw, IXGBE_PTC255);
	IXGBE_READ_REG(hw, IXGBE_PTC511);
	IXGBE_READ_REG(hw, IXGBE_PTC1023);
	IXGBE_READ_REG(hw, IXGBE_PTC1522);
	IXGBE_READ_REG(hw, IXGBE_MPTC);
	IXGBE_READ_REG(hw, IXGBE_BPTC);
	for (i = 0; i < 16; i++) {
		IXGBE_READ_REG(hw, IXGBE_QPRC(i));
		IXGBE_READ_REG(hw, IXGBE_QBRC(i));
		IXGBE_READ_REG(hw, IXGBE_QPTC(i));
		IXGBE_READ_REG(hw, IXGBE_QBTC(i));
	}

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_read_pba_num - Reads part number from EEPROM
 *  @hw: pointer to hardware structure
 *  @pba_num: stores the part number from the EEPROM
 *
 *  Reads the part number from the EEPROM.
 **/
int32_t ixgbe_read_pba_num_generic(struct ixgbe_hw *hw, uint32_t *pba_num)
{
	int32_t ret_val;
	uint16_t data;

	DEBUGFUNC("ixgbe_read_pba_num_generic");

	ret_val = hw->eeprom.ops.read(hw, IXGBE_PBANUM0_PTR, &data);
	if (ret_val) {
		DEBUGOUT("NVM Read Error\n");
		return ret_val;
	}
	*pba_num = (uint32_t)(data << 16);

	ret_val = hw->eeprom.ops.read(hw, IXGBE_PBANUM1_PTR, &data);
	if (ret_val) {
		DEBUGOUT("NVM Read Error\n");
		return ret_val;
	}
	*pba_num |= data;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_get_mac_addr_generic - Generic get MAC address
 *  @hw: pointer to hardware structure
 *  @mac_addr: Adapter MAC address
 *
 *  Reads the adapter's MAC address from first Receive Address Register (RAR0)
 *  A reset of the adapter must be performed prior to calling this function
 *  in order for the MAC address to have been loaded from the EEPROM into RAR0
 **/
int32_t ixgbe_get_mac_addr_generic(struct ixgbe_hw *hw, uint8_t *mac_addr)
{
	uint32_t rar_high;
	uint32_t rar_low;
	uint16_t i;

	rar_high = IXGBE_READ_REG(hw, IXGBE_RAH(0));
	rar_low = IXGBE_READ_REG(hw, IXGBE_RAL(0));

	for (i = 0; i < 4; i++)
		mac_addr[i] = (uint8_t)(rar_low >> (i*8));

	for (i = 0; i < 2; i++)
		mac_addr[i+4] = (uint8_t)(rar_high >> (i*8));

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_get_bus_info_generic - Generic set PCI bus info
 *  @hw: pointer to hardware structure
 *
 *  Sets the PCI bus info (speed, width, type) within the ixgbe_hw structure
 **/
int32_t ixgbe_get_bus_info_generic(struct ixgbe_hw *hw)
{
	uint16_t link_status;

	hw->bus.type = ixgbe_bus_type_pci_express;

	/* Get the negotiated link width and speed from PCI config space */
	link_status = IXGBE_READ_PCIE_WORD(hw, IXGBE_PCI_LINK_STATUS);

	switch (link_status & IXGBE_PCI_LINK_WIDTH) {
	case IXGBE_PCI_LINK_WIDTH_1:
		hw->bus.width = ixgbe_bus_width_pcie_x1;
		break;
	case IXGBE_PCI_LINK_WIDTH_2:
		hw->bus.width = ixgbe_bus_width_pcie_x2;
		break;
	case IXGBE_PCI_LINK_WIDTH_4:
		hw->bus.width = ixgbe_bus_width_pcie_x4;
		break;
	case IXGBE_PCI_LINK_WIDTH_8:
		hw->bus.width = ixgbe_bus_width_pcie_x8;
		break;
	default:
		hw->bus.width = ixgbe_bus_width_unknown;
		break;
	}

	switch (link_status & IXGBE_PCI_LINK_SPEED) {
	case IXGBE_PCI_LINK_SPEED_2500:
		hw->bus.speed = ixgbe_bus_speed_2500;
		break;
	case IXGBE_PCI_LINK_SPEED_5000:
		hw->bus.speed = ixgbe_bus_speed_5000;
		break;
	default:
		hw->bus.speed = ixgbe_bus_speed_unknown;
		break;
	}

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_stop_adapter_generic - Generic stop Tx/Rx units
 *  @hw: pointer to hardware structure
 *
 *  Sets the adapter_stopped flag within ixgbe_hw struct. Clears interrupts,
 *  disables transmit and receive units. The adapter_stopped flag is used by
 *  the shared code and drivers to determine if the adapter is in a stopped
 *  state and should not touch the hardware.
 **/
int32_t ixgbe_stop_adapter_generic(struct ixgbe_hw *hw)
{
	uint32_t number_of_queues;
	uint32_t reg_val;
	uint16_t i;

	/*
	 * Set the adapter_stopped flag so other driver functions stop touching
	 * the hardware
	 */
	hw->adapter_stopped = TRUE;

	/* Disable the receive unit */
	reg_val = IXGBE_READ_REG(hw, IXGBE_RXCTRL);
	reg_val &= ~(IXGBE_RXCTRL_RXEN);
	IXGBE_WRITE_REG(hw, IXGBE_RXCTRL, reg_val);
	IXGBE_WRITE_FLUSH(hw);
	msec_delay(2);

	/* Clear interrupt mask to stop from interrupts being generated */
	IXGBE_WRITE_REG(hw, IXGBE_EIMC, IXGBE_IRQ_CLEAR_MASK);

	/* Clear any pending interrupts */
	IXGBE_READ_REG(hw, IXGBE_EICR);

	/* Disable the transmit unit.  Each queue must be disabled. */
	number_of_queues = hw->mac.max_tx_queues;
	for (i = 0; i < number_of_queues; i++) {
		reg_val = IXGBE_READ_REG(hw, IXGBE_TXDCTL(i));
		if (reg_val & IXGBE_TXDCTL_ENABLE) {
			reg_val &= ~IXGBE_TXDCTL_ENABLE;
			IXGBE_WRITE_REG(hw, IXGBE_TXDCTL(i), reg_val);
		}
	}

	/*
	 * Prevent the PCI-E bus from from hanging by disabling PCI-E master
	 * access and verify no pending requests
	 */
	if (ixgbe_disable_pcie_master(hw) != IXGBE_SUCCESS) {
		DEBUGOUT("PCI-E Master disable polling has failed.\n");
	}

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_led_on_generic - Turns on the software controllable LEDs.
 *  @hw: pointer to hardware structure
 *  @index: led number to turn on
 **/
int32_t ixgbe_led_on_generic(struct ixgbe_hw *hw, uint32_t index)
{
	uint32_t led_reg = IXGBE_READ_REG(hw, IXGBE_LEDCTL);

	/* To turn on the LED, set mode to ON. */
	led_reg &= ~IXGBE_LED_MODE_MASK(index);
	led_reg |= IXGBE_LED_ON << IXGBE_LED_MODE_SHIFT(index);
	IXGBE_WRITE_REG(hw, IXGBE_LEDCTL, led_reg);
	IXGBE_WRITE_FLUSH(hw);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_led_off_generic - Turns off the software controllable LEDs.
 *  @hw: pointer to hardware structure
 *  @index: led number to turn off
 **/
int32_t ixgbe_led_off_generic(struct ixgbe_hw *hw, uint32_t index)
{
	uint32_t led_reg = IXGBE_READ_REG(hw, IXGBE_LEDCTL);

	/* To turn off the LED, set mode to OFF. */
	led_reg &= ~IXGBE_LED_MODE_MASK(index);
	led_reg |= IXGBE_LED_OFF << IXGBE_LED_MODE_SHIFT(index);
	IXGBE_WRITE_REG(hw, IXGBE_LEDCTL, led_reg);
	IXGBE_WRITE_FLUSH(hw);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_init_eeprom_params_generic - Initialize EEPROM params
 *  @hw: pointer to hardware structure
 *
 *  Initializes the EEPROM parameters ixgbe_eeprom_info within the
 *  ixgbe_hw struct in order to set up EEPROM access.
 **/
int32_t ixgbe_init_eeprom_params_generic(struct ixgbe_hw *hw)
{
	struct ixgbe_eeprom_info *eeprom = &hw->eeprom;
	uint32_t eec;
	uint16_t eeprom_size;

	if (eeprom->type == ixgbe_eeprom_uninitialized) {
		eeprom->type = ixgbe_eeprom_none;

		/*
		 * Check for EEPROM present first.
		 * If not present leave as none
		 */
		eec = IXGBE_READ_REG(hw, IXGBE_EEC);
		if (eec & IXGBE_EEC_PRES) {
			eeprom->type = ixgbe_eeprom_spi;

			/*
			 * SPI EEPROM is assumed here.  This code would need to
			 * change if a future EEPROM is not SPI.
			 */
			eeprom_size = (uint16_t)((eec & IXGBE_EEC_SIZE) >>
					    IXGBE_EEC_SIZE_SHIFT);
			eeprom->word_size = 1 << (eeprom_size +
						  IXGBE_EEPROM_WORD_SIZE_SHIFT);
		}

		if (eec & IXGBE_EEC_ADDR_SIZE)
			eeprom->address_bits = 16;
		else
			eeprom->address_bits = 8;
		DEBUGOUT3("Eeprom params: type = %d, size = %d, address bits: "
			  "%d\n", eeprom->type, eeprom->word_size,
			  eeprom->address_bits);
	}

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_write_eeprom_generic - Writes 16 bit value to EEPROM
 *  @hw: pointer to hardware structure
 *  @offset: offset within the EEPROM to be written to
 *  @data: 16 bit word to be written to the EEPROM
 *
 *  If ixgbe_eeprom_update_checksum is not called after this function, the
 *  EEPROM will most likely contain an invalid checksum.
 **/
int32_t ixgbe_write_eeprom_generic(struct ixgbe_hw *hw, uint16_t offset, uint16_t data)
{
	int32_t status;
	uint8_t write_opcode = IXGBE_EEPROM_WRITE_OPCODE_SPI;

	hw->eeprom.ops.init_params(hw);

	if (offset >= hw->eeprom.word_size) {
		status = IXGBE_ERR_EEPROM;
		goto out;
	}

	/* Prepare the EEPROM for writing  */
	status = ixgbe_acquire_eeprom(hw);

	if (status == IXGBE_SUCCESS) {
		if (ixgbe_ready_eeprom(hw) != IXGBE_SUCCESS) {
			ixgbe_release_eeprom(hw);
			status = IXGBE_ERR_EEPROM;
		}
	}

	if (status == IXGBE_SUCCESS) {
		ixgbe_standby_eeprom(hw);

		/*  Send the WRITE ENABLE command (8 bit opcode )  */
		ixgbe_shift_out_eeprom_bits(hw, IXGBE_EEPROM_WREN_OPCODE_SPI,
		                            IXGBE_EEPROM_OPCODE_BITS);

		ixgbe_standby_eeprom(hw);

		/*
		 * Some SPI eeproms use the 8th address bit embedded in the
		 * opcode
		 */
		if ((hw->eeprom.address_bits == 8) && (offset >= 128))
			write_opcode |= IXGBE_EEPROM_A8_OPCODE_SPI;

		/* Send the Write command (8-bit opcode + addr) */
		ixgbe_shift_out_eeprom_bits(hw, write_opcode,
		                            IXGBE_EEPROM_OPCODE_BITS);
		ixgbe_shift_out_eeprom_bits(hw, (uint16_t)(offset*2),
		                            hw->eeprom.address_bits);

		/* Send the data */
		data = (data >> 8) | (data << 8);
		ixgbe_shift_out_eeprom_bits(hw, data, 16);
		ixgbe_standby_eeprom(hw);

		msec_delay(10);

		/* Done with writing - release the EEPROM */
		ixgbe_release_eeprom(hw);
	}

out:
	return status;
}

/**
 *  ixgbe_read_eeprom_bit_bang_generic - Read EEPROM word using bit-bang
 *  @hw: pointer to hardware structure
 *  @offset: offset within the EEPROM to be read
 *  @data: read 16 bit value from EEPROM
 *
 *  Reads 16 bit value from EEPROM through bit-bang method
 **/
int32_t ixgbe_read_eeprom_bit_bang_generic(struct ixgbe_hw *hw, uint16_t offset,
                                       uint16_t *data)
{
	int32_t status;
	uint16_t word_in;
	uint8_t read_opcode = IXGBE_EEPROM_READ_OPCODE_SPI;

	hw->eeprom.ops.init_params(hw);

	if (offset >= hw->eeprom.word_size) {
		status = IXGBE_ERR_EEPROM;
		goto out;
	}

	/* Prepare the EEPROM for reading  */
	status = ixgbe_acquire_eeprom(hw);

	if (status == IXGBE_SUCCESS) {
		if (ixgbe_ready_eeprom(hw) != IXGBE_SUCCESS) {
			ixgbe_release_eeprom(hw);
			status = IXGBE_ERR_EEPROM;
		}
	}

	if (status == IXGBE_SUCCESS) {
		ixgbe_standby_eeprom(hw);

		/*
		 * Some SPI eeproms use the 8th address bit embedded in the
		 * opcode
		 */
		if ((hw->eeprom.address_bits == 8) && (offset >= 128))
			read_opcode |= IXGBE_EEPROM_A8_OPCODE_SPI;

		/* Send the READ command (opcode + addr) */
		ixgbe_shift_out_eeprom_bits(hw, read_opcode,
		                            IXGBE_EEPROM_OPCODE_BITS);
		ixgbe_shift_out_eeprom_bits(hw, (uint16_t)(offset*2),
		                            hw->eeprom.address_bits);

		/* Read the data. */
		word_in = ixgbe_shift_in_eeprom_bits(hw, 16);
		*data = (word_in >> 8) | (word_in << 8);

		/* End this read operation */
		ixgbe_release_eeprom(hw);
	}

out:
	return status;
}

/**
 *  ixgbe_read_eeprom_generic - Read EEPROM word using EERD
 *  @hw: pointer to hardware structure
 *  @offset: offset of  word in the EEPROM to read
 *  @data: word read from the EEPROM
 *
 *  Reads a 16 bit word from the EEPROM using the EERD register.
 **/
int32_t ixgbe_read_eeprom_generic(struct ixgbe_hw *hw, uint16_t offset, uint16_t *data)
{
	uint32_t eerd;
	int32_t status;

	hw->eeprom.ops.init_params(hw);

	if (offset >= hw->eeprom.word_size) {
		status = IXGBE_ERR_EEPROM;
		goto out;
	}

	eerd = (offset << IXGBE_EEPROM_READ_ADDR_SHIFT) +
	       IXGBE_EEPROM_READ_REG_START;

	IXGBE_WRITE_REG(hw, IXGBE_EERD, eerd);
	status = ixgbe_poll_eeprom_eerd_done(hw);

	if (status == IXGBE_SUCCESS)
		*data = (IXGBE_READ_REG(hw, IXGBE_EERD) >>
		         IXGBE_EEPROM_READ_REG_DATA);
	else
		DEBUGOUT("Eeprom read timed out\n");

out:
	return status;
}

/**
 *  ixgbe_poll_eeprom_eerd_done - Poll EERD status
 *  @hw: pointer to hardware structure
 *
 *  Polls the status bit (bit 1) of the EERD to determine when the read is done.
 **/
static int32_t ixgbe_poll_eeprom_eerd_done(struct ixgbe_hw *hw)
{
	uint32_t i;
	uint32_t reg;
	int32_t status = IXGBE_ERR_EEPROM;

	for (i = 0; i < IXGBE_EERD_ATTEMPTS; i++) {
		reg = IXGBE_READ_REG(hw, IXGBE_EERD);
		if (reg & IXGBE_EEPROM_READ_REG_DONE) {
			status = IXGBE_SUCCESS;
			break;
		}
		usec_delay(5);
	}
	return status;
}

/**
 *  ixgbe_acquire_eeprom - Acquire EEPROM using bit-bang
 *  @hw: pointer to hardware structure
 *
 *  Prepares EEPROM for access using bit-bang method. This function should
 *  be called before issuing a command to the EEPROM.
 **/
static int32_t ixgbe_acquire_eeprom(struct ixgbe_hw *hw)
{
	int32_t status = IXGBE_SUCCESS;
	uint32_t eec;
	uint32_t i;

	if (ixgbe_acquire_swfw_sync(hw, IXGBE_GSSR_EEP_SM) != IXGBE_SUCCESS)
		status = IXGBE_ERR_SWFW_SYNC;

	if (status == IXGBE_SUCCESS) {
		eec = IXGBE_READ_REG(hw, IXGBE_EEC);

		/* Request EEPROM Access */
		eec |= IXGBE_EEC_REQ;
		IXGBE_WRITE_REG(hw, IXGBE_EEC, eec);

		for (i = 0; i < IXGBE_EEPROM_GRANT_ATTEMPTS; i++) {
			eec = IXGBE_READ_REG(hw, IXGBE_EEC);
			if (eec & IXGBE_EEC_GNT)
				break;
			usec_delay(5);
		}

		/* Release if grant not acquired */
		if (!(eec & IXGBE_EEC_GNT)) {
			eec &= ~IXGBE_EEC_REQ;
			IXGBE_WRITE_REG(hw, IXGBE_EEC, eec);
			DEBUGOUT("Could not acquire EEPROM grant\n");

			ixgbe_release_swfw_sync(hw, IXGBE_GSSR_EEP_SM);
			status = IXGBE_ERR_EEPROM;
		}
	}

	/* Setup EEPROM for Read/Write */
	if (status == IXGBE_SUCCESS) {
		/* Clear CS and SK */
		eec &= ~(IXGBE_EEC_CS | IXGBE_EEC_SK);
		IXGBE_WRITE_REG(hw, IXGBE_EEC, eec);
		IXGBE_WRITE_FLUSH(hw);
		usec_delay(1);
	}
	return status;
}

/**
 *  ixgbe_get_eeprom_semaphore - Get hardware semaphore
 *  @hw: pointer to hardware structure
 *
 *  Sets the hardware semaphores so EEPROM access can occur for bit-bang method
 **/
static int32_t ixgbe_get_eeprom_semaphore(struct ixgbe_hw *hw)
{
	int32_t status = IXGBE_ERR_EEPROM;
	uint32_t timeout;
	uint32_t i;
	uint32_t swsm;

	/* Set timeout value based on size of EEPROM */
	timeout = hw->eeprom.word_size + 1;

	/* Get SMBI software semaphore between device drivers first */
	for (i = 0; i < timeout; i++) {
		/*
		 * If the SMBI bit is 0 when we read it, then the bit will be
		 * set and we have the semaphore
		 */
		swsm = IXGBE_READ_REG(hw, IXGBE_SWSM);
		if (!(swsm & IXGBE_SWSM_SMBI)) {
			status = IXGBE_SUCCESS;
			break;
		}
		msec_delay(1);
	}

	/* Now get the semaphore between SW/FW through the SWESMBI bit */
	if (status == IXGBE_SUCCESS) {
		for (i = 0; i < timeout; i++) {
			swsm = IXGBE_READ_REG(hw, IXGBE_SWSM);

			/* Set the SW EEPROM semaphore bit to request access */
			swsm |= IXGBE_SWSM_SWESMBI;
			IXGBE_WRITE_REG(hw, IXGBE_SWSM, swsm);

			/*
			 * If we set the bit successfully then we got the
			 * semaphore.
			 */
			swsm = IXGBE_READ_REG(hw, IXGBE_SWSM);
			if (swsm & IXGBE_SWSM_SWESMBI)
				break;

			usec_delay(50);
		}

		/*
		 * Release semaphores and return error if SW EEPROM semaphore
		 * was not granted because we don't have access to the EEPROM
		 */
		if (i >= timeout) {
			DEBUGOUT("Driver can't access the Eeprom - Semaphore "
			         "not granted.\n");
			ixgbe_release_eeprom_semaphore(hw);
			status = IXGBE_ERR_EEPROM;
		}
	}

	return status;
}

/**
 *  ixgbe_release_eeprom_semaphore - Release hardware semaphore
 *  @hw: pointer to hardware structure
 *
 *  This function clears hardware semaphore bits.
 **/
static void ixgbe_release_eeprom_semaphore(struct ixgbe_hw *hw)
{
	uint32_t swsm;

	swsm = IXGBE_READ_REG(hw, IXGBE_SWSM);

	/* Release both semaphores by writing 0 to the bits SWESMBI and SMBI */
	swsm &= ~(IXGBE_SWSM_SWESMBI | IXGBE_SWSM_SMBI);
	IXGBE_WRITE_REG(hw, IXGBE_SWSM, swsm);
	IXGBE_WRITE_FLUSH(hw);
}

/**
 *  ixgbe_ready_eeprom - Polls for EEPROM ready
 *  @hw: pointer to hardware structure
 **/
static int32_t ixgbe_ready_eeprom(struct ixgbe_hw *hw)
{
	int32_t status = IXGBE_SUCCESS;
	uint16_t i;
	uint8_t spi_stat_reg;

	/*
	 * Read "Status Register" repeatedly until the LSB is cleared.  The
	 * EEPROM will signal that the command has been completed by clearing
	 * bit 0 of the internal status register.  If it's not cleared within
	 * 5 milliseconds, then error out.
	 */
	for (i = 0; i < IXGBE_EEPROM_MAX_RETRY_SPI; i += 5) {
		ixgbe_shift_out_eeprom_bits(hw, IXGBE_EEPROM_RDSR_OPCODE_SPI,
		                            IXGBE_EEPROM_OPCODE_BITS);
		spi_stat_reg = (uint8_t)ixgbe_shift_in_eeprom_bits(hw, 8);
		if (!(spi_stat_reg & IXGBE_EEPROM_STATUS_RDY_SPI))
			break;

		usec_delay(5);
		ixgbe_standby_eeprom(hw);
	};

	/*
	 * On some parts, SPI write time could vary from 0-20mSec on 3.3V
	 * devices (and only 0-5mSec on 5V devices)
	 */
	if (i >= IXGBE_EEPROM_MAX_RETRY_SPI) {
		DEBUGOUT("SPI EEPROM Status error\n");
		status = IXGBE_ERR_EEPROM;
	}

	return status;
}

/**
 *  ixgbe_standby_eeprom - Returns EEPROM to a "standby" state
 *  @hw: pointer to hardware structure
 **/
static void ixgbe_standby_eeprom(struct ixgbe_hw *hw)
{
	uint32_t eec;

	eec = IXGBE_READ_REG(hw, IXGBE_EEC);

	/* Toggle CS to flush commands */
	eec |= IXGBE_EEC_CS;
	IXGBE_WRITE_REG(hw, IXGBE_EEC, eec);
	IXGBE_WRITE_FLUSH(hw);
	usec_delay(1);
	eec &= ~IXGBE_EEC_CS;
	IXGBE_WRITE_REG(hw, IXGBE_EEC, eec);
	IXGBE_WRITE_FLUSH(hw);
	usec_delay(1);
}

/**
 *  ixgbe_shift_out_eeprom_bits - Shift data bits out to the EEPROM.
 *  @hw: pointer to hardware structure
 *  @data: data to send to the EEPROM
 *  @count: number of bits to shift out
 **/
static void ixgbe_shift_out_eeprom_bits(struct ixgbe_hw *hw, uint16_t data,
                                        uint16_t count)
{
	uint32_t eec;
	uint32_t mask;
	uint32_t i;

	eec = IXGBE_READ_REG(hw, IXGBE_EEC);

	/*
	 * Mask is used to shift "count" bits of "data" out to the EEPROM
	 * one bit at a time.  Determine the starting bit based on count
	 */
	mask = 0x01 << (count - 1);

	for (i = 0; i < count; i++) {
		/*
		 * A "1" is shifted out to the EEPROM by setting bit "DI" to a
		 * "1", and then raising and then lowering the clock (the SK
		 * bit controls the clock input to the EEPROM).  A "0" is
		 * shifted out to the EEPROM by setting "DI" to "0" and then
		 * raising and then lowering the clock.
		 */
		if (data & mask)
			eec |= IXGBE_EEC_DI;
		else
			eec &= ~IXGBE_EEC_DI;

		IXGBE_WRITE_REG(hw, IXGBE_EEC, eec);
		IXGBE_WRITE_FLUSH(hw);

		usec_delay(1);

		ixgbe_raise_eeprom_clk(hw, &eec);
		ixgbe_lower_eeprom_clk(hw, &eec);

		/*
		 * Shift mask to signify next bit of data to shift in to the
		 * EEPROM
		 */
		mask = mask >> 1;
	};

	/* We leave the "DI" bit set to "0" when we leave this routine. */
	eec &= ~IXGBE_EEC_DI;
	IXGBE_WRITE_REG(hw, IXGBE_EEC, eec);
	IXGBE_WRITE_FLUSH(hw);
}

/**
 *  ixgbe_shift_in_eeprom_bits - Shift data bits in from the EEPROM
 *  @hw: pointer to hardware structure
 **/
static uint16_t ixgbe_shift_in_eeprom_bits(struct ixgbe_hw *hw, uint16_t count)
{
	uint32_t eec;
	uint32_t i;
	uint16_t data = 0;

	/*
	 * In order to read a register from the EEPROM, we need to shift
	 * 'count' bits in from the EEPROM. Bits are "shifted in" by raising
	 * the clock input to the EEPROM (setting the SK bit), and then reading
	 * the value of the "DO" bit.  During this "shifting in" process the
	 * "DI" bit should always be clear.
	 */
	eec = IXGBE_READ_REG(hw, IXGBE_EEC);

	eec &= ~(IXGBE_EEC_DO | IXGBE_EEC_DI);

	for (i = 0; i < count; i++) {
		data = data << 1;
		ixgbe_raise_eeprom_clk(hw, &eec);

		eec = IXGBE_READ_REG(hw, IXGBE_EEC);

		eec &= ~(IXGBE_EEC_DI);
		if (eec & IXGBE_EEC_DO)
			data |= 1;

		ixgbe_lower_eeprom_clk(hw, &eec);
	}

	return data;
}

/**
 *  ixgbe_raise_eeprom_clk - Raises the EEPROM's clock input.
 *  @hw: pointer to hardware structure
 *  @eec: EEC register's current value
 **/
static void ixgbe_raise_eeprom_clk(struct ixgbe_hw *hw, uint32_t *eec)
{
	/*
	 * Raise the clock input to the EEPROM
	 * (setting the SK bit), then delay
	 */
	*eec = *eec | IXGBE_EEC_SK;
	IXGBE_WRITE_REG(hw, IXGBE_EEC, *eec);
	IXGBE_WRITE_FLUSH(hw);
	usec_delay(1);
}

/**
 *  ixgbe_lower_eeprom_clk - Lowers the EEPROM's clock input.
 *  @hw: pointer to hardware structure
 *  @eecd: EECD's current value
 **/
static void ixgbe_lower_eeprom_clk(struct ixgbe_hw *hw, uint32_t *eec)
{
	/*
	 * Lower the clock input to the EEPROM (clearing the SK bit), then
	 * delay
	 */
	*eec = *eec & ~IXGBE_EEC_SK;
	IXGBE_WRITE_REG(hw, IXGBE_EEC, *eec);
	IXGBE_WRITE_FLUSH(hw);
	usec_delay(1);
}

/**
 *  ixgbe_release_eeprom - Release EEPROM, release semaphores
 *  @hw: pointer to hardware structure
 **/
static void ixgbe_release_eeprom(struct ixgbe_hw *hw)
{
	uint32_t eec;

	eec = IXGBE_READ_REG(hw, IXGBE_EEC);

	eec |= IXGBE_EEC_CS;  /* Pull CS high */
	eec &= ~IXGBE_EEC_SK; /* Lower SCK */

	IXGBE_WRITE_REG(hw, IXGBE_EEC, eec);
	IXGBE_WRITE_FLUSH(hw);

	usec_delay(1);

	/* Stop requesting EEPROM access */
	eec &= ~IXGBE_EEC_REQ;
	IXGBE_WRITE_REG(hw, IXGBE_EEC, eec);

	ixgbe_release_swfw_sync(hw, IXGBE_GSSR_EEP_SM);
}

/**
 *  ixgbe_calc_eeprom_checksum - Calculates and returns the checksum
 *  @hw: pointer to hardware structure
 **/
static uint16_t ixgbe_calc_eeprom_checksum(struct ixgbe_hw *hw)
{
	uint16_t i;
	uint16_t j;
	uint16_t checksum = 0;
	uint16_t length = 0;
	uint16_t pointer = 0;
	uint16_t word = 0;

	/* Include 0x0-0x3F in the checksum */
	for (i = 0; i < IXGBE_EEPROM_CHECKSUM; i++) {
		if (hw->eeprom.ops.read(hw, i, &word) != IXGBE_SUCCESS) {
			DEBUGOUT("EEPROM read failed\n");
			break;
		}
		checksum += word;
	}

	/* Include all data from pointers except for the fw pointer */
	for (i = IXGBE_PCIE_ANALOG_PTR; i < IXGBE_FW_PTR; i++) {
		hw->eeprom.ops.read(hw, i, &pointer);

		/* Make sure the pointer seems valid */
		if (pointer != 0xFFFF && pointer != 0) {
			hw->eeprom.ops.read(hw, pointer, &length);

			if (length != 0xFFFF && length != 0) {
				for (j = pointer+1; j <= pointer+length; j++) {
					hw->eeprom.ops.read(hw, j, &word);
					checksum += word;
				}
			}
		}
	}

	checksum = (uint16_t)IXGBE_EEPROM_SUM - checksum;

	return checksum;
}

/**
 *  ixgbe_validate_eeprom_checksum_generic - Validate EEPROM checksum
 *  @hw: pointer to hardware structure
 *  @checksum_val: calculated checksum
 *
 *  Performs checksum calculation and validates the EEPROM checksum.  If the
 *  caller does not need checksum_val, the value can be NULL.
 **/
int32_t ixgbe_validate_eeprom_checksum_generic(struct ixgbe_hw *hw,
                                           uint16_t *checksum_val)
{
	int32_t status;
	uint16_t checksum;
	uint16_t read_checksum = 0;

	/*
	 * Read the first word from the EEPROM. If this times out or fails, do
	 * not continue or we could be in for a very long wait while every
	 * EEPROM read fails
	 */
	status = hw->eeprom.ops.read(hw, 0, &checksum);

	if (status == IXGBE_SUCCESS) {
		checksum = ixgbe_calc_eeprom_checksum(hw);

		hw->eeprom.ops.read(hw, IXGBE_EEPROM_CHECKSUM, &read_checksum);

		/*
		 * Verify read checksum from EEPROM is the same as
		 * calculated checksum
		 */
		if (read_checksum != checksum)
			status = IXGBE_ERR_EEPROM_CHECKSUM;

		/* If the user cares, return the calculated checksum */
		if (checksum_val)
			*checksum_val = checksum;
	} else {
		DEBUGOUT("EEPROM read failed\n");
	}

	return status;
}

/**
 *  ixgbe_update_eeprom_checksum_generic - Updates the EEPROM checksum
 *  @hw: pointer to hardware structure
 **/
int32_t ixgbe_update_eeprom_checksum_generic(struct ixgbe_hw *hw)
{
	int32_t status;
	uint16_t checksum;

	/*
	 * Read the first word from the EEPROM. If this times out or fails, do
	 * not continue or we could be in for a very long wait while every
	 * EEPROM read fails
	 */
	status = hw->eeprom.ops.read(hw, 0, &checksum);

	if (status == IXGBE_SUCCESS) {
		checksum = ixgbe_calc_eeprom_checksum(hw);
		status = hw->eeprom.ops.write(hw, IXGBE_EEPROM_CHECKSUM,
		                            checksum);
	} else {
		DEBUGOUT("EEPROM read failed\n");
	}

	return status;
}

/**
 *  ixgbe_validate_mac_addr - Validate MAC address
 *  @mac_addr: pointer to MAC address.
 *
 *  Tests a MAC address to ensure it is a valid Individual Address
 **/
int32_t ixgbe_validate_mac_addr(uint8_t *mac_addr)
{
	int32_t status = IXGBE_SUCCESS;

	/* Make sure it is not a multicast address */
	if (IXGBE_IS_MULTICAST(mac_addr)) {
		DEBUGOUT("MAC address is multicast\n");
		status = IXGBE_ERR_INVALID_MAC_ADDR;
	/* Not a broadcast address */
	} else if (IXGBE_IS_BROADCAST(mac_addr)) {
		DEBUGOUT("MAC address is broadcast\n");
		status = IXGBE_ERR_INVALID_MAC_ADDR;
	/* Reject the zero address */
	} else if (mac_addr[0] == 0 && mac_addr[1] == 0 && mac_addr[2] == 0 &&
	           mac_addr[3] == 0 && mac_addr[4] == 0 && mac_addr[5] == 0) {
		DEBUGOUT("MAC address is all zeros\n");
		status = IXGBE_ERR_INVALID_MAC_ADDR;
	}
	return status;
}

/**
 *  ixgbe_set_rar_generic - Set Rx address register
 *  @hw: pointer to hardware structure
 *  @index: Receive address register to write
 *  @addr: Address to put into receive address register
 *  @vmdq: VMDq "set" or "pool" index
 *  @enable_addr: set flag that address is active
 *
 *  Puts an ethernet address into a receive address register.
 **/
int32_t ixgbe_set_rar_generic(struct ixgbe_hw *hw, uint32_t index, uint8_t *addr, uint32_t vmdq,
                          uint32_t enable_addr)
{
	uint32_t rar_low, rar_high;
	uint32_t rar_entries = hw->mac.num_rar_entries;

	/* setup VMDq pool selection before this RAR gets enabled */
	hw->mac.ops.set_vmdq(hw, index, vmdq);

	/* Make sure we are using a valid rar index range */
	if (index < rar_entries) {
		/*
		 * HW expects these in little endian so we reverse the byte
		 * order from network order (big endian) to little endian
		 */
		rar_low = ((uint32_t)addr[0] |
		           ((uint32_t)addr[1] << 8) |
		           ((uint32_t)addr[2] << 16) |
		           ((uint32_t)addr[3] << 24));
		/*
		 * Some parts put the VMDq setting in the extra RAH bits,
		 * so save everything except the lower 16 bits that hold part
		 * of the address and the address valid bit.
		 */
		rar_high = IXGBE_READ_REG(hw, IXGBE_RAH(index));
		rar_high &= ~(0x0000FFFF | IXGBE_RAH_AV);
		rar_high |= ((uint32_t)addr[4] | ((uint32_t)addr[5] << 8));

		if (enable_addr != 0)
			rar_high |= IXGBE_RAH_AV;

		IXGBE_WRITE_REG(hw, IXGBE_RAL(index), rar_low);
		IXGBE_WRITE_REG(hw, IXGBE_RAH(index), rar_high);
	} else {
		DEBUGOUT("Current RAR index is out of range.");
	}

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_enable_rar - Enable Rx address register
 *  @hw: pointer to hardware structure
 *  @index: index into the RAR table
 *
 *  Enables the select receive address register.
 **/
static void ixgbe_enable_rar(struct ixgbe_hw *hw, uint32_t index)
{
	uint32_t rar_high;

	rar_high = IXGBE_READ_REG(hw, IXGBE_RAH(index));
	rar_high |= IXGBE_RAH_AV;
	IXGBE_WRITE_REG(hw, IXGBE_RAH(index), rar_high);
}

/**
 *  ixgbe_disable_rar - Disable Rx address register
 *  @hw: pointer to hardware structure
 *  @index: index into the RAR table
 *
 *  Disables the select receive address register.
 **/
static void ixgbe_disable_rar(struct ixgbe_hw *hw, uint32_t index)
{
	uint32_t rar_high;

	rar_high = IXGBE_READ_REG(hw, IXGBE_RAH(index));
	rar_high &= (~IXGBE_RAH_AV);
	IXGBE_WRITE_REG(hw, IXGBE_RAH(index), rar_high);
}

/**
 *  ixgbe_init_rx_addrs_generic - Initializes receive address filters.
 *  @hw: pointer to hardware structure
 *
 *  Places the MAC address in receive address register 0 and clears the rest
 *  of the receive address registers. Clears the multicast table. Assumes
 *  the receiver is in reset when the routine is called.
 **/
int32_t ixgbe_init_rx_addrs_generic(struct ixgbe_hw *hw)
{
	uint32_t i;
	uint32_t rar_entries = hw->mac.num_rar_entries;

	/*
	 * If the current mac address is valid, assume it is a software override
	 * to the permanent address.
	 * Otherwise, use the permanent address from the eeprom.
	 */
	if (ixgbe_validate_mac_addr(hw->mac.addr) ==
	    IXGBE_ERR_INVALID_MAC_ADDR) {
		/* Get the MAC address from the RAR0 for later reference */
		hw->mac.ops.get_mac_addr(hw, hw->mac.addr);

		DEBUGOUT3(" Keeping Current RAR0 Addr =%.2X %.2X %.2X ",
		          hw->mac.addr[0], hw->mac.addr[1],
		          hw->mac.addr[2]);
		DEBUGOUT3("%.2X %.2X %.2X\n", hw->mac.addr[3],
		          hw->mac.addr[4], hw->mac.addr[5]);
	} else {
		/* Setup the receive address. */
		DEBUGOUT("Overriding MAC Address in RAR[0]\n");
		DEBUGOUT3(" New MAC Addr =%.2X %.2X %.2X ",
		          hw->mac.addr[0], hw->mac.addr[1],
		          hw->mac.addr[2]);
		DEBUGOUT3("%.2X %.2X %.2X\n", hw->mac.addr[3],
		          hw->mac.addr[4], hw->mac.addr[5]);

		hw->mac.ops.set_rar(hw, 0, hw->mac.addr, 0, IXGBE_RAH_AV);
	}
	hw->addr_ctrl.overflow_promisc = 0;

	hw->addr_ctrl.rar_used_count = 1;

	/* Zero out the other receive addresses. */
	DEBUGOUT1("Clearing RAR[1-%d]\n", rar_entries - 1);
	for (i = 1; i < rar_entries; i++) {
		IXGBE_WRITE_REG(hw, IXGBE_RAL(i), 0);
		IXGBE_WRITE_REG(hw, IXGBE_RAH(i), 0);
	}

	/* Clear the MTA */
	hw->addr_ctrl.mc_addr_in_rar_count = 0;
	hw->addr_ctrl.mta_in_use = 0;
	IXGBE_WRITE_REG(hw, IXGBE_MCSTCTRL, hw->mac.mc_filter_type);

	DEBUGOUT(" Clearing MTA\n");
	for (i = 0; i < hw->mac.mcft_size; i++)
		IXGBE_WRITE_REG(hw, IXGBE_MTA(i), 0);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_add_uc_addr - Adds a secondary unicast address.
 *  @hw: pointer to hardware structure
 *  @addr: new address
 *
 *  Adds it to unused receive address register or goes into promiscuous mode.
 **/
void ixgbe_add_uc_addr(struct ixgbe_hw *hw, uint8_t *addr, uint32_t vmdq)
{
	uint32_t rar_entries = hw->mac.num_rar_entries;
	uint32_t rar;

	DEBUGOUT6(" UC Addr = %.2X %.2X %.2X %.2X %.2X %.2X\n",
	          addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

	/*
	 * Place this address in the RAR if there is room,
	 * else put the controller into promiscuous mode
	 */
	if (hw->addr_ctrl.rar_used_count < rar_entries) {
		rar = hw->addr_ctrl.rar_used_count -
		      hw->addr_ctrl.mc_addr_in_rar_count;
		hw->mac.ops.set_rar(hw, rar, addr, vmdq, IXGBE_RAH_AV);
		DEBUGOUT1("Added a secondary address to RAR[%d]\n", rar);
		hw->addr_ctrl.rar_used_count++;
	} else {
		hw->addr_ctrl.overflow_promisc++;
	}

	DEBUGOUT("ixgbe_add_uc_addr Complete\n");
}

/**
 *  ixgbe_update_uc_addr_list_generic - Updates MAC list of secondary addresses
 *  @hw: pointer to hardware structure
 *  @addr_list: the list of new addresses
 *  @addr_count: number of addresses
 *  @next: iterator function to walk the address list
 *
 *  The given list replaces any existing list.  Clears the secondary addrs from
 *  receive address registers.  Uses unused receive address registers for the
 *  first secondary addresses, and falls back to promiscuous mode as needed.
 *
 *  Drivers using secondary unicast addresses must set user_set_promisc when
 *  manually putting the device into promiscuous mode.
 **/
int32_t ixgbe_update_uc_addr_list_generic(struct ixgbe_hw *hw, uint8_t *addr_list,
                                      uint32_t addr_count, ixgbe_mc_addr_itr next)
{
	uint8_t *addr;
	uint32_t i;
	uint32_t old_promisc_setting = hw->addr_ctrl.overflow_promisc;
	uint32_t uc_addr_in_use;
	uint32_t fctrl;
	uint32_t vmdq;

	/*
	 * Clear accounting of old secondary address list,
	 * don't count RAR[0]
	 */
	uc_addr_in_use = hw->addr_ctrl.rar_used_count -
	                 hw->addr_ctrl.mc_addr_in_rar_count - 1;
	hw->addr_ctrl.rar_used_count -= uc_addr_in_use;
	hw->addr_ctrl.overflow_promisc = 0;

	/* Zero out the other receive addresses */
	DEBUGOUT1("Clearing RAR[1-%d]\n", uc_addr_in_use);
	for (i = 1; i <= uc_addr_in_use; i++) {
		IXGBE_WRITE_REG(hw, IXGBE_RAL(i), 0);
		IXGBE_WRITE_REG(hw, IXGBE_RAH(i), 0);
	}

	/* Add the new addresses */
	for (i = 0; i < addr_count; i++) {
		DEBUGOUT(" Adding the secondary addresses:\n");
		addr = next(hw, &addr_list, &vmdq);
		ixgbe_add_uc_addr(hw, addr, vmdq);
	}

	if (hw->addr_ctrl.overflow_promisc) {
		/* enable promisc if not already in overflow or set by user */
		if (!old_promisc_setting && !hw->addr_ctrl.user_set_promisc) {
			DEBUGOUT( " Entering address overflow promisc mode\n");
			fctrl = IXGBE_READ_REG(hw, IXGBE_FCTRL);
			fctrl |= IXGBE_FCTRL_UPE;
			IXGBE_WRITE_REG(hw, IXGBE_FCTRL, fctrl);
		}
	} else {
		/* only disable if set by overflow, not by user */
		if (old_promisc_setting && !hw->addr_ctrl.user_set_promisc) {
			DEBUGOUT(" Leaving address overflow promisc mode\n");
			fctrl = IXGBE_READ_REG(hw, IXGBE_FCTRL);
			fctrl &= ~IXGBE_FCTRL_UPE;
			IXGBE_WRITE_REG(hw, IXGBE_FCTRL, fctrl);
		}
	}

	DEBUGOUT("ixgbe_update_uc_addr_list_generic Complete\n");
	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_mta_vector - Determines bit-vector in multicast table to set
 *  @hw: pointer to hardware structure
 *  @mc_addr: the multicast address
 *
 *  Extracts the 12 bits, from a multicast address, to determine which
 *  bit-vector to set in the multicast table. The hardware uses 12 bits, from
 *  incoming rx multicast addresses, to determine the bit-vector to check in
 *  the MTA. Which of the 4 combination, of 12-bits, the hardware uses is set
 *  by the MO field of the MCSTCTRL. The MO field is set during initialization
 *  to mc_filter_type.
 **/
static int32_t ixgbe_mta_vector(struct ixgbe_hw *hw, uint8_t *mc_addr)
{
	uint32_t vector = 0;

	switch (hw->mac.mc_filter_type) {
	case 0:   /* use bits [47:36] of the address */
		vector = ((mc_addr[4] >> 4) | (((uint16_t)mc_addr[5]) << 4));
		break;
	case 1:   /* use bits [46:35] of the address */
		vector = ((mc_addr[4] >> 3) | (((uint16_t)mc_addr[5]) << 5));
		break;
	case 2:   /* use bits [45:34] of the address */
		vector = ((mc_addr[4] >> 2) | (((uint16_t)mc_addr[5]) << 6));
		break;
	case 3:   /* use bits [43:32] of the address */
		vector = ((mc_addr[4]) | (((uint16_t)mc_addr[5]) << 8));
		break;
	default:  /* Invalid mc_filter_type */
		DEBUGOUT("MC filter type param set incorrectly\n");
		panic("ixgbe");
		break;
	}

	/* vector can only be 12-bits or boundary will be exceeded */
	vector &= 0xFFF;
	return vector;
}

/**
 *  ixgbe_set_mta - Set bit-vector in multicast table
 *  @hw: pointer to hardware structure
 *  @hash_value: Multicast address hash value
 *
 *  Sets the bit-vector in the multicast table.
 **/
void ixgbe_set_mta(struct ixgbe_hw *hw, uint8_t *mc_addr)
{
	uint32_t vector;
	uint32_t vector_bit;
	uint32_t vector_reg;
	uint32_t mta_reg;

	hw->addr_ctrl.mta_in_use++;

	vector = ixgbe_mta_vector(hw, mc_addr);
	DEBUGOUT1(" bit-vector = 0x%03X\n", vector);

	/*
	 * The MTA is a register array of 128 32-bit registers. It is treated
	 * like an array of 4096 bits.  We want to set bit
	 * BitArray[vector_value]. So we figure out what register the bit is
	 * in, read it, OR in the new bit, then write back the new value.  The
	 * register is determined by the upper 7 bits of the vector value and
	 * the bit within that register are determined by the lower 5 bits of
	 * the value.
	 */
	vector_reg = (vector >> 5) & 0x7F;
	vector_bit = vector & 0x1F;
	mta_reg = IXGBE_READ_REG(hw, IXGBE_MTA(vector_reg));
	mta_reg |= (1 << vector_bit);
	IXGBE_WRITE_REG(hw, IXGBE_MTA(vector_reg), mta_reg);
}

/**
 *  ixgbe_add_mc_addr - Adds a multicast address.
 *  @hw: pointer to hardware structure
 *  @mc_addr: new multicast address
 *
 *  Adds it to unused receive address register or to the multicast table.
 **/
void ixgbe_add_mc_addr(struct ixgbe_hw *hw, uint8_t *mc_addr)
{
	uint32_t rar_entries = hw->mac.num_rar_entries;
	uint32_t rar;

	DEBUGOUT6(" MC Addr =%.2X %.2X %.2X %.2X %.2X %.2X\n",
	          mc_addr[0], mc_addr[1], mc_addr[2],
	          mc_addr[3], mc_addr[4], mc_addr[5]);

	/*
	 * Place this multicast address in the RAR if there is room,
	 * else put it in the MTA
	 */
	if (hw->addr_ctrl.rar_used_count < rar_entries) {
		/* use RAR from the end up for multicast */
		rar = rar_entries - hw->addr_ctrl.mc_addr_in_rar_count - 1;
		hw->mac.ops.set_rar(hw, rar, mc_addr, 0, IXGBE_RAH_AV);
		DEBUGOUT1("Added a multicast address to RAR[%d]\n", rar);
		hw->addr_ctrl.rar_used_count++;
		hw->addr_ctrl.mc_addr_in_rar_count++;
	} else {
		ixgbe_set_mta(hw, mc_addr);
	}

	DEBUGOUT("ixgbe_add_mc_addr Complete\n");
}

/**
 *  ixgbe_update_mc_addr_list_generic - Updates MAC list of multicast addresses
 *  @hw: pointer to hardware structure
 *  @mc_addr_list: the list of new multicast addresses
 *  @mc_addr_count: number of addresses
 *  @next: iterator function to walk the multicast address list
 *
 *  The given list replaces any existing list. Clears the MC addrs from receive
 *  address registers and the multicast table. Uses unused receive address
 *  registers for the first multicast addresses, and hashes the rest into the
 *  multicast table.
 **/
int32_t ixgbe_update_mc_addr_list_generic(struct ixgbe_hw *hw, uint8_t *mc_addr_list,
                                      uint32_t mc_addr_count, ixgbe_mc_addr_itr next)
{
	uint32_t i;
	uint32_t rar_entries = hw->mac.num_rar_entries;
	uint32_t vmdq;

	/*
	 * Set the new number of MC addresses that we are being requested to
	 * use.
	 */
	hw->addr_ctrl.num_mc_addrs = mc_addr_count;
	hw->addr_ctrl.rar_used_count -= hw->addr_ctrl.mc_addr_in_rar_count;
	hw->addr_ctrl.mc_addr_in_rar_count = 0;
	hw->addr_ctrl.mta_in_use = 0;

	/* Zero out the other receive addresses. */
	DEBUGOUT2("Clearing RAR[%d-%d]\n", hw->addr_ctrl.rar_used_count,
	          rar_entries - 1);
	for (i = hw->addr_ctrl.rar_used_count; i < rar_entries; i++) {
		IXGBE_WRITE_REG(hw, IXGBE_RAL(i), 0);
		IXGBE_WRITE_REG(hw, IXGBE_RAH(i), 0);
	}

	/* Clear the MTA */
	DEBUGOUT(" Clearing MTA\n");
	for (i = 0; i < hw->mac.mcft_size; i++)
		IXGBE_WRITE_REG(hw, IXGBE_MTA(i), 0);

	/* Add the new addresses */
	for (i = 0; i < mc_addr_count; i++) {
		DEBUGOUT(" Adding the multicast addresses:\n");
		ixgbe_add_mc_addr(hw, next(hw, &mc_addr_list, &vmdq));
	}

	/* Enable mta */
	if (hw->addr_ctrl.mta_in_use > 0)
		IXGBE_WRITE_REG(hw, IXGBE_MCSTCTRL,
		                IXGBE_MCSTCTRL_MFE | hw->mac.mc_filter_type);

	DEBUGOUT("ixgbe_update_mc_addr_list_generic Complete\n");
	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_enable_mc_generic - Enable multicast address in RAR
 *  @hw: pointer to hardware structure
 *
 *  Enables multicast address in RAR and the use of the multicast hash table.
 **/
int32_t ixgbe_enable_mc_generic(struct ixgbe_hw *hw)
{
	uint32_t i;
	uint32_t rar_entries = hw->mac.num_rar_entries;
	struct ixgbe_addr_filter_info *a = &hw->addr_ctrl;

	if (a->mc_addr_in_rar_count > 0)
		for (i = (rar_entries - a->mc_addr_in_rar_count);
		     i < rar_entries; i++)
			ixgbe_enable_rar(hw, i);

	if (a->mta_in_use > 0)
		IXGBE_WRITE_REG(hw, IXGBE_MCSTCTRL, IXGBE_MCSTCTRL_MFE |
		                hw->mac.mc_filter_type);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_disable_mc_generic - Disable multicast address in RAR
 *  @hw: pointer to hardware structure
 *
 *  Disables multicast address in RAR and the use of the multicast hash table.
 **/
int32_t ixgbe_disable_mc_generic(struct ixgbe_hw *hw)
{
	uint32_t i;
	uint32_t rar_entries = hw->mac.num_rar_entries;
	struct ixgbe_addr_filter_info *a = &hw->addr_ctrl;

	if (a->mc_addr_in_rar_count > 0)
		for (i = (rar_entries - a->mc_addr_in_rar_count);
		     i < rar_entries; i++)
			ixgbe_disable_rar(hw, i);

	if (a->mta_in_use > 0)
		IXGBE_WRITE_REG(hw, IXGBE_MCSTCTRL, hw->mac.mc_filter_type);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_clear_vfta_generic - Clear VLAN filter table
 *  @hw: pointer to hardware structure
 *
 *  Clears the VLAN filer table, and the VMDq index associated with the filter
 **/
int32_t ixgbe_clear_vfta_generic(struct ixgbe_hw *hw)
{
	uint32_t offset;
	uint32_t vlanbyte;

	for (offset = 0; offset < hw->mac.vft_size; offset++)
		IXGBE_WRITE_REG(hw, IXGBE_VFTA(offset), 0);

	for (vlanbyte = 0; vlanbyte < 4; vlanbyte++)
		for (offset = 0; offset < hw->mac.vft_size; offset++)
			IXGBE_WRITE_REG(hw, IXGBE_VFTAVIND(vlanbyte, offset),
			                0);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_set_vfta_generic - Set VLAN filter table
 *  @hw: pointer to hardware structure
 *  @vlan: VLAN id to write to VLAN filter
 *  @vind: VMDq output index that maps queue to VLAN id in VFTA
 *  @vlan_on: intean flag to turn on/off VLAN in VFTA
 *
 *  Turn on/off specified VLAN in the VLAN filter table.
 **/
int32_t ixgbe_set_vfta_generic(struct ixgbe_hw *hw, uint32_t vlan, uint32_t vind,
                           int vlan_on)
{
	uint32_t VftaIndex;
	uint32_t BitOffset;
	uint32_t VftaReg;
	uint32_t VftaByte;

	/* Determine 32-bit word position in array */
	VftaIndex = (vlan >> 5) & 0x7F;   /* upper seven bits */

	/* Determine the location of the (VMD) queue index */
	VftaByte =  ((vlan >> 3) & 0x03); /* bits (4:3) indicating byte array */
	BitOffset = (vlan & 0x7) << 2;    /* lower 3 bits indicate nibble */

	/* Set the nibble for VMD queue index */
	VftaReg = IXGBE_READ_REG(hw, IXGBE_VFTAVIND(VftaByte, VftaIndex));
	VftaReg &= (~(0x0F << BitOffset));
	VftaReg |= (vind << BitOffset);
	IXGBE_WRITE_REG(hw, IXGBE_VFTAVIND(VftaByte, VftaIndex), VftaReg);

	/* Determine the location of the bit for this VLAN id */
	BitOffset = vlan & 0x1F;   /* lower five bits */

	VftaReg = IXGBE_READ_REG(hw, IXGBE_VFTA(VftaIndex));
	if (vlan_on)
		/* Turn on this VLAN id */
		VftaReg |= (1 << BitOffset);
	else
		/* Turn off this VLAN id */
		VftaReg &= ~(1 << BitOffset);
	IXGBE_WRITE_REG(hw, IXGBE_VFTA(VftaIndex), VftaReg);

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_disable_pcie_master - Disable PCI-express master access
 *  @hw: pointer to hardware structure
 *
 *  Disables PCI-Express master access and verifies there are no pending
 *  requests. IXGBE_ERR_MASTER_REQUESTS_PENDING is returned if master disable
 *  bit hasn't caused the master requests to be disabled, else IXGBE_SUCCESS
 *  is returned signifying master requests disabled.
 **/
int32_t ixgbe_disable_pcie_master(struct ixgbe_hw *hw)
{
	uint32_t ctrl;
	int32_t i;
	int32_t status = IXGBE_ERR_MASTER_REQUESTS_PENDING;

	ctrl = IXGBE_READ_REG(hw, IXGBE_CTRL);
	ctrl |= IXGBE_CTRL_GIO_DIS;
	IXGBE_WRITE_REG(hw, IXGBE_CTRL, ctrl);

	for (i = 0; i < IXGBE_PCI_MASTER_DISABLE_TIMEOUT; i++) {
		if (!(IXGBE_READ_REG(hw, IXGBE_STATUS) & IXGBE_STATUS_GIO)) {
			status = IXGBE_SUCCESS;
			break;
		}
		usec_delay(100);
	}

	return status;
}


/**
 *  ixgbe_acquire_swfw_sync - Acquire SWFW semaphore
 *  @hw: pointer to hardware structure
 *  @mask: Mask to specify which semaphore to acquire
 *
 *  Acquires the SWFW semaphore thought the GSSR register for the specified
 *  function (CSR, PHY0, PHY1, EEPROM, Flash)
 **/
int32_t ixgbe_acquire_swfw_sync(struct ixgbe_hw *hw, uint16_t mask)
{
	uint32_t gssr;
	uint32_t swmask = mask;
	uint32_t fwmask = mask << 5;
	int32_t timeout = 200;

	while (timeout) {
		if (ixgbe_get_eeprom_semaphore(hw))
			return -IXGBE_ERR_SWFW_SYNC;

		gssr = IXGBE_READ_REG(hw, IXGBE_GSSR);
		if (!(gssr & (fwmask | swmask)))
			break;

		/*
		 * Firmware currently using resource (fwmask) or other software
		 * thread currently using resource (swmask)
		 */
		ixgbe_release_eeprom_semaphore(hw);
		msec_delay(5);
		timeout--;
	}

	if (!timeout) {
		DEBUGOUT("Driver can't access resource, GSSR timeout.\n");
		return -IXGBE_ERR_SWFW_SYNC;
	}

	gssr |= swmask;
	IXGBE_WRITE_REG(hw, IXGBE_GSSR, gssr);

	ixgbe_release_eeprom_semaphore(hw);
	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_release_swfw_sync - Release SWFW semaphore
 *  @hw: pointer to hardware structure
 *  @mask: Mask to specify which semaphore to release
 *
 *  Releases the SWFW semaphore thought the GSSR register for the specified
 *  function (CSR, PHY0, PHY1, EEPROM, Flash)
 **/
void ixgbe_release_swfw_sync(struct ixgbe_hw *hw, uint16_t mask)
{
	uint32_t gssr;
	uint32_t swmask = mask;

	ixgbe_get_eeprom_semaphore(hw);

	gssr = IXGBE_READ_REG(hw, IXGBE_GSSR);
	gssr &= ~swmask;
	IXGBE_WRITE_REG(hw, IXGBE_GSSR, gssr);

	ixgbe_release_eeprom_semaphore(hw);
}

/**
 *  ixgbe_read_analog_reg8_generic - Reads 8 bit Atlas analog register
 *  @hw: pointer to hardware structure
 *  @reg: analog register to read
 *  @val: read value
 *
 *  Performs read operation to Atlas analog register specified.
 **/
int32_t ixgbe_read_analog_reg8_generic(struct ixgbe_hw *hw, uint32_t reg, uint8_t *val)
{
	uint32_t  atlas_ctl;

	IXGBE_WRITE_REG(hw, IXGBE_ATLASCTL, IXGBE_ATLASCTL_WRITE_CMD | (reg << 8));
	IXGBE_WRITE_FLUSH(hw);
	usec_delay(10);
	atlas_ctl = IXGBE_READ_REG(hw, IXGBE_ATLASCTL);
	*val = (uint8_t)atlas_ctl;

	return IXGBE_SUCCESS;
}

/**
 *  ixgbe_write_analog_reg8_generic - Writes 8 bit Atlas analog register
 *  @hw: pointer to hardware structure
 *  @reg: atlas register to write
 *  @val: value to write
 *
 *  Performs write operation to Atlas analog register specified.
 **/
int32_t ixgbe_write_analog_reg8_generic(struct ixgbe_hw *hw, uint32_t reg, uint8_t val)
{
	uint32_t  atlas_ctl;

	atlas_ctl = (reg << 8) | val;
	IXGBE_WRITE_REG(hw, IXGBE_ATLASCTL, atlas_ctl);
	IXGBE_WRITE_FLUSH(hw);
	usec_delay(10);

	return IXGBE_SUCCESS;
}

