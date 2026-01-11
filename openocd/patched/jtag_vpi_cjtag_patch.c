/*
 * Extended JTAG VPI driver with cJTAG/OScan1 support
 * Based on OPENOCD_CJTAG_PATCH_GUIDE.md
 * 
 * This file extends OpenOCD's src/jtag/drivers/jtag_vpi.c
 * 
 * PATCH INSTRUCTIONS:
 * 1. Add #include "oscan1.h" at the top
 * 2. Add the static variables below to the existing jtag_vpi.c
 * 3. Add the new functions to jtag_vpi.c
 * 4. Modify jtag_vpi_init() to call jtag_vpi_oscan1_init() when in cJTAG mode
 * 5. Modify jtag_vpi_tms_seq() to use jtag_vpi_sf0_scan() when in cJTAG mode
 */

/* ========== ADD TO TOP OF jtag_vpi.c ========== */

#include "oscan1.h"

/* cJTAG mode state */
static int jtag_vpi_cjtag_mode = 0;
static bool jtag_vpi_oscan1_initialized = false;

/* ========== ADD THESE FUNCTIONS TO jtag_vpi.c ========== */

/**
 * Send two-wire TCKC/TMSC command via VPI
 * This extends the VPI protocol to support cJTAG two-wire mode
 */
static int jtag_vpi_send_tckc_tmsc(uint8_t tckc, uint8_t tmsc)
{
	/* VPI command structure for two-wire mode:
	 * Byte 0: Command (0x03 = CMD_SET_PORT for pin control)
	 * Byte 1-2: Reserved
	 * Byte 3: Reserved  
	 * Byte 4-7: Port data
	 *   - Bit 0: TCKC value
	 *   - Bit 1: TMSC value (when output enabled)
	 */
	
	uint8_t cmd[8];
	memset(cmd, 0, sizeof(cmd));
	
	cmd[0] = CMD_SET_PORT;  /* 0x03 */
	
	/* Set port data: TCKC on bit 0, TMSC on bit 1 */
	uint32_t port_data = (tckc & 1) | ((tmsc & 1) << 1);
	cmd[4] = (port_data >> 24) & 0xFF;
	cmd[5] = (port_data >> 16) & 0xFF;
	cmd[6] = (port_data >> 8) & 0xFF;
	cmd[7] = port_data & 0xFF;
	
	/* Send command */
	if (write(sockfd, cmd, sizeof(cmd)) != sizeof(cmd)) {
		LOG_ERROR("Failed to send TCKC/TMSC command");
		return ERROR_FAIL;
	}
	
	return ERROR_OK;
}

/**
 * Receive TMSC value (TDO) via VPI
 */
static uint8_t jtag_vpi_receive_tmsc(void)
{
	/* In two-wire mode, read TMSC line to get TDO value
	 * This would typically be part of the VPI response
	 */
	
	uint8_t resp[4];
	if (read(sockfd, resp, sizeof(resp)) != sizeof(resp)) {
		LOG_ERROR("Failed to receive TMSC response");
		return 0;
	}
	
	/* TDO value is in bit 1 of response */
	return (resp[1] >> 1) & 1;
}

/**
 * Initialize OScan1 protocol for cJTAG mode
 */
static int jtag_vpi_oscan1_init(void)
{
	if (jtag_vpi_oscan1_initialized)
		return ERROR_OK;
	
	LOG_INFO("Initializing VPI adapter for cJTAG/OScan1 mode");
	
	/* Connect oscan1.c functions to VPI adapter */
	/* Note: This requires modifying oscan1.c to use function pointers
	 * or implementing the low-level functions here */
	
	/* Initialize OScan1 protocol */
	int result = oscan1_init();
	if (result != ERROR_OK) {
		LOG_ERROR("Failed to initialize OScan1 protocol");
		return result;
	}
	
	jtag_vpi_oscan1_initialized = true;
	LOG_INFO("cJTAG/OScan1 mode initialized successfully");
	
	return ERROR_OK;
}

/**
 * Perform scan operation using Scanning Format 0
 * This converts JTAG TMS/TDI operations to two-wire SF0 encoding
 */
static int jtag_vpi_sf0_scan(unsigned num_bits, const uint8_t *tms, const uint8_t *tdi, uint8_t *tdo)
{
	if (!jtag_vpi_oscan1_initialized) {
		LOG_ERROR("OScan1 not initialized");
		return ERROR_FAIL;
	}
	
	/* Scan each bit using SF0 encoding */
	for (unsigned bit = 0; bit < num_bits; bit++) {
		/* Extract TMS and TDI bit values */
		uint8_t tms_val = (tms[bit / 8] >> (bit % 8)) & 1;
		uint8_t tdi_val = (tdi[bit / 8] >> (bit % 8)) & 1;
		uint8_t tdo_val = 0;
		
		/* Encode and send using SF0 */
		if (oscan1_sf0_encode(tms_val, tdi_val, &tdo_val) != ERROR_OK) {
			LOG_ERROR("SF0 encoding failed at bit %u", bit);
			return ERROR_FAIL;
		}
		
		/* Store TDO value */
		if (tdo) {
			if (tdo_val)
				tdo[bit / 8] |= (1 << (bit % 8));
			else
				tdo[bit / 8] &= ~(1 << (bit % 8));
		}
	}
	
	return ERROR_OK;
}

/**
 * Modified jtag_vpi_tms_seq - add cJTAG support
 * 
 * PATCH: Replace the existing jtag_vpi_tms_seq function with this version
 * or add a check at the beginning to redirect to SF0 when in cJTAG mode
 */
static int jtag_vpi_tms_seq(const uint8_t *tms, int num_bits)
{
	/* If in cJTAG mode, use SF0 encoding */
	if (jtag_vpi_cjtag_mode) {
		/* Create TDI buffer (all zeros for TMS-only sequence) */
		uint8_t tdi[(num_bits + 7) / 8];
		memset(tdi, 0, sizeof(tdi));
		
		return jtag_vpi_sf0_scan(num_bits, tms, tdi, NULL);
	}
	
	/* Otherwise, use standard JTAG protocol (existing code) */
	/* ... existing jtag_vpi_tms_seq implementation ... */
	
	return ERROR_OK;
}

/**
 * Modified jtag_vpi_scan - add cJTAG support
 * 
 * PATCH: Similar to tms_seq, check for cJTAG mode and redirect to SF0
 */
static int jtag_vpi_scan(int num_bits, const uint8_t *tms, const uint8_t *tdi, uint8_t *tdo)
{
	/* If in cJTAG mode, use SF0 encoding */
	if (jtag_vpi_cjtag_mode) {
		return jtag_vpi_sf0_scan(num_bits, tms, tdi, tdo);
	}
	
	/* Otherwise, use standard JTAG protocol (existing code) */
	/* ... existing jtag_vpi_scan implementation ... */
	
	return ERROR_OK;
}

/**
 * Modified jtag_vpi_init - add cJTAG initialization
 * 
 * PATCH: Add this code after successful connection to VPI server
 */
static int jtag_vpi_init(void)
{
	/* ... existing initialization code ... */
	
	/* Check if cJTAG mode is enabled */
	if (jtag_vpi_cjtag_mode) {
		LOG_INFO("cJTAG mode enabled, initializing OScan1 protocol");
		
		int result = jtag_vpi_oscan1_init();
		if (result != ERROR_OK) {
			LOG_ERROR("Failed to initialize cJTAG/OScan1 mode");
			return result;
		}
	}
	
	return ERROR_OK;
}

/* ========== ADD THESE TCL COMMAND HANDLERS ========== */

COMMAND_HANDLER(jtag_vpi_handle_enable_cjtag_command)
{
	if (CMD_ARGC != 0)
		return ERROR_COMMAND_SYNTAX_ERROR;
	
	jtag_vpi_cjtag_mode = 1;
	LOG_INFO("cJTAG mode enabled");
	
	return ERROR_OK;
}

COMMAND_HANDLER(jtag_vpi_handle_scanning_format_command)
{
	if (CMD_ARGC != 1)
		return ERROR_COMMAND_SYNTAX_ERROR;
	
	unsigned format;
	COMMAND_PARSE_NUMBER(uint, CMD_ARGV[0], format);
	
	if (format > 3) {
		LOG_ERROR("Invalid scanning format: %u (must be 0-3)", format);
		return ERROR_COMMAND_SYNTAX_ERROR;
	}
	
	oscan1_set_scanning_format(format);
	LOG_INFO("Scanning format set to SF%u", format);
	
	return ERROR_OK;
}

COMMAND_HANDLER(jtag_vpi_handle_enable_crc_command)
{
	if (CMD_ARGC != 1)
		return ERROR_COMMAND_SYNTAX_ERROR;
	
	bool enable = strcmp(CMD_ARGV[0], "on") == 0 || strcmp(CMD_ARGV[0], "1") == 0;
	oscan1_enable_crc(enable);
	
	return ERROR_OK;
}

COMMAND_HANDLER(jtag_vpi_handle_enable_parity_command)
{
	if (CMD_ARGC != 1)
		return ERROR_COMMAND_SYNTAX_ERROR;
	
	bool enable = strcmp(CMD_ARGV[0], "on") == 0 || strcmp(CMD_ARGV[0], "1") == 0;
	oscan1_enable_parity(enable);
	
	return ERROR_OK;
}

/* ========== ADD TO jtag_vpi_command_handlers[] ========== */

static const struct command_registration jtag_vpi_command_handlers[] = {
	/* ... existing commands ... */
	{
		.name = "enable_cjtag",
		.handler = &jtag_vpi_handle_enable_cjtag_command,
		.mode = COMMAND_CONFIG,
		.help = "enable cJTAG/OScan1 mode",
		.usage = "",
	},
	{
		.name = "scanning_format",
		.handler = &jtag_vpi_handle_scanning_format_command,
		.mode = COMMAND_CONFIG,
		.help = "set OScan1 scanning format",
		.usage = "<0-3>",
	},
	{
		.name = "enable_crc",
		.handler = &jtag_vpi_handle_enable_crc_command,
		.mode = COMMAND_CONFIG,
		.help = "enable CRC-8 checking",
		.usage = "on|off",
	},
	{
		.name = "enable_parity",
		.handler = &jtag_vpi_handle_enable_parity_command,
		.mode = COMMAND_CONFIG,
		.help = "enable parity checking",
		.usage = "on|off",
	},
	COMMAND_REGISTRATION_DONE
};
