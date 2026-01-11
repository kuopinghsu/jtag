/*
 * IEEE 1149.7 cJTAG OScan1 Protocol Implementation
 * Based on OPENOCD_CJTAG_PATCH_GUIDE.md
 * 
 * This file should be placed in OpenOCD's src/jtag/drivers/ directory
 */

#include "config.h"
#include <jtag/interface.h>
#include <helper/bits.h>

/* OScan1 Protocol Constants */
#define OSCAN1_OAC_LENGTH           16      /* Attention Character: 16 TCKC edges */
#define OSCAN1_CRC8_POLYNOMIAL      0x07    /* x^8 + x^2 + x + 1 */

/* JScan Commands (IEEE 1149.7 Table 5-1) */
#define JSCAN_OSCAN_ON              0x01
#define JSCAN_OSCAN_OFF             0x00
#define JSCAN_SELECT                0x02
#define JSCAN_DESELECT              0x03
#define JSCAN_SF_SELECT             0x04    /* Select Scanning Format 0 */
#define JSCAN_RESET                 0x0F

/* Scanning Format Selection */
#define SF0                         0       /* Standard format */
#define SF1                         1       /* Optimized format */
#define SF2                         2       /* Advanced format */
#define SF3                         3       /* High-speed format */

/* OScan1 State */
static struct {
	bool initialized;
	bool oscan_enabled;
	uint8_t scanning_format;
	bool crc_enabled;
	bool parity_enabled;
	uint8_t device_id;
} oscan1_state = {
	.initialized = false,
	.oscan_enabled = false,
	.scanning_format = SF0,
	.crc_enabled = false,
	.parity_enabled = false,
	.device_id = 0
};

/* Forward declarations */
static int oscan1_send_tckc_tmsc(uint8_t tckc, uint8_t tmsc);
static uint8_t oscan1_receive_tmsc(void);

/**
 * Send OScan1 Attention Character (OAC)
 * OAC: 16 consecutive TCKC rising edges with TMSC held high
 * This signals the start of JScan mode
 */
int oscan1_send_oac(void)
{
	/* Send 16 TCKC edges with TMSC=1 */
	for (int i = 0; i < OSCAN1_OAC_LENGTH; i++) {
		/* TCKC rising edge */
		if (oscan1_send_tckc_tmsc(1, 1) != ERROR_OK)
			return ERROR_FAIL;
	}
	
	return ERROR_OK;
}

/**
 * Send JScan command
 * JScan commands are 4-bit commands sent during OScan1 mode
 * 
 * Format: Start bit (1) + 4-bit command + optional parity
 */
int oscan1_send_jscan_cmd(uint8_t cmd)
{
	uint8_t packet = 0;
	int bit_count = 0;
	
	/* Build packet: start bit + 4-bit command */
	packet = (1 << 4) | (cmd & 0x0F);  /* Start bit + command */
	bit_count = 5;
	
	/* Add parity if enabled */
	if (oscan1_state.parity_enabled) {
		uint8_t parity = __builtin_popcount(packet) & 1;  /* Even parity */
		packet = (packet << 1) | parity;
		bit_count++;
	}
	
	/* Send packet bits MSB first */
	for (int i = bit_count - 1; i >= 0; i--) {
		uint8_t bit = (packet >> i) & 1;
		if (oscan1_send_tckc_tmsc(1, bit) != ERROR_OK)
			return ERROR_FAIL;
	}
	
	return ERROR_OK;
}

/**
 * Zero insertion (bit stuffing)
 * After 5 consecutive 1s, insert a 0
 * This prevents accidental OAC detection in data
 */
static void oscan1_apply_zero_insertion(uint8_t *input, size_t input_len, 
                                        uint8_t *output, size_t *output_len)
{
	int ones_count = 0;
	size_t out_idx = 0;
	
	for (size_t byte_idx = 0; byte_idx < input_len; byte_idx++) {
		for (int bit = 7; bit >= 0; bit--) {
			uint8_t bit_val = (input[byte_idx] >> bit) & 1;
			
			/* Write bit */
			if (bit_val)
				output[out_idx / 8] |= (1 << (7 - (out_idx % 8)));
			else
				output[out_idx / 8] &= ~(1 << (7 - (out_idx % 8)));
			out_idx++;
			
			/* Track consecutive ones */
			if (bit_val) {
				ones_count++;
				if (ones_count == 5) {
					/* Insert zero after 5 consecutive ones */
					output[out_idx / 8] &= ~(1 << (7 - (out_idx % 8)));
					out_idx++;
					ones_count = 0;
				}
			} else {
				ones_count = 0;
			}
		}
	}
	
	*output_len = (out_idx + 7) / 8;  /* Round up to bytes */
}

/**
 * Scanning Format 0 (SF0) encoder
 * SF0: TMS on TCKC rising edge, TDI on TCKC falling edge
 * Both transmitted on two-wire TMSC
 */
int oscan1_sf0_encode(uint8_t tms, uint8_t tdi, uint8_t *tdo)
{
	/* Send TMS on rising edge of TCKC */
	if (oscan1_send_tckc_tmsc(1, tms) != ERROR_OK)
		return ERROR_FAIL;
	
	/* Send TDI on falling edge of TCKC (set TCKC=0, TMSC=TDI) */
	if (oscan1_send_tckc_tmsc(0, tdi) != ERROR_OK)
		return ERROR_FAIL;
	
	/* Read TDO during this cycle */
	*tdo = oscan1_receive_tmsc();
	
	return ERROR_OK;
}

/**
 * CRC-8 calculation
 * Polynomial: x^8 + x^2 + x + 1 (0x07)
 * Used for data integrity checking
 */
uint8_t oscan1_calc_crc8(const uint8_t *data, size_t len)
{
	uint8_t crc = 0x00;
	
	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int bit = 0; bit < 8; bit++) {
			if (crc & 0x80)
				crc = (crc << 1) ^ OSCAN1_CRC8_POLYNOMIAL;
			else
				crc = crc << 1;
		}
	}
	
	return crc;
}

/**
 * Initialize OScan1 protocol
 * Sequence:
 * 1. Send OAC (enter JScan mode)
 * 2. Send JSCAN_OSCAN_ON
 * 3. Send JSCAN_SELECT (select device)
 * 4. Select Scanning Format 0
 */
int oscan1_init(void)
{
	if (oscan1_state.initialized)
		return ERROR_OK;
	
	LOG_INFO("Initializing OScan1 protocol...");
	
	/* Step 1: Send OAC to enter JScan mode */
	LOG_DEBUG("Sending OAC (Attention Character)...");
	if (oscan1_send_oac() != ERROR_OK) {
		LOG_ERROR("Failed to send OAC");
		return ERROR_FAIL;
	}
	
	/* Step 2: Enable OScan1 mode */
	LOG_DEBUG("Sending JSCAN_OSCAN_ON command...");
	if (oscan1_send_jscan_cmd(JSCAN_OSCAN_ON) != ERROR_OK) {
		LOG_ERROR("Failed to enable OScan1");
		return ERROR_FAIL;
	}
	oscan1_state.oscan_enabled = true;
	
	/* Step 3: Select device (default device 0) */
	LOG_DEBUG("Sending JSCAN_SELECT command...");
	if (oscan1_send_jscan_cmd(JSCAN_SELECT) != ERROR_OK) {
		LOG_ERROR("Failed to select device");
		return ERROR_FAIL;
	}
	
	/* Step 4: Select Scanning Format 0 */
	LOG_DEBUG("Selecting Scanning Format 0...");
	if (oscan1_send_jscan_cmd(JSCAN_SF_SELECT) != ERROR_OK) {
		LOG_ERROR("Failed to select scanning format");
		return ERROR_FAIL;
	}
	oscan1_state.scanning_format = SF0;
	
	oscan1_state.initialized = true;
	LOG_INFO("OScan1 protocol initialized successfully");
	
	return ERROR_OK;
}

/**
 * Reset OScan1 state
 */
int oscan1_reset(void)
{
	LOG_DEBUG("Resetting OScan1 state");
	
	if (oscan1_state.oscan_enabled) {
		/* Send JSCAN_RESET */
		oscan1_send_jscan_cmd(JSCAN_RESET);
		
		/* Exit OScan1 mode */
		oscan1_send_jscan_cmd(JSCAN_OSCAN_OFF);
	}
	
	oscan1_state.initialized = false;
	oscan1_state.oscan_enabled = false;
	oscan1_state.scanning_format = SF0;
	
	return ERROR_OK;
}

/**
 * Set scanning format
 */
int oscan1_set_scanning_format(uint8_t format)
{
	if (format > SF3) {
		LOG_ERROR("Invalid scanning format: %d", format);
		return ERROR_FAIL;
	}
	
	oscan1_state.scanning_format = format;
	LOG_DEBUG("Scanning format set to SF%d", format);
	
	return ERROR_OK;
}

/**
 * Enable/disable CRC-8 checking
 */
void oscan1_enable_crc(bool enable)
{
	oscan1_state.crc_enabled = enable;
	LOG_DEBUG("CRC-8 checking %s", enable ? "enabled" : "disabled");
}

/**
 * Enable/disable parity checking
 */
void oscan1_enable_parity(bool enable)
{
	oscan1_state.parity_enabled = enable;
	LOG_DEBUG("Parity checking %s", enable ? "enabled" : "disabled");
}

/*
 * Low-level hardware interface functions
 * These should be implemented by the specific adapter (e.g., jtag_vpi)
 */

static int oscan1_send_tckc_tmsc(uint8_t tckc, uint8_t tmsc)
{
	/* This function should be implemented by the adapter driver
	 * Example for jtag_vpi: send VPI command with two-wire data
	 */
	
	/* Placeholder - adapter must implement */
	LOG_ERROR("oscan1_send_tckc_tmsc not implemented by adapter");
	return ERROR_FAIL;
}

static uint8_t oscan1_receive_tmsc(void)
{
	/* This function should be implemented by the adapter driver
	 * Example for jtag_vpi: receive VPI response with TDO data
	 */
	
	/* Placeholder - adapter must implement */
	LOG_ERROR("oscan1_receive_tmsc not implemented by adapter");
	return 0;
}
