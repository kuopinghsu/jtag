/*
 * IEEE 1149.7 cJTAG OScan1 Protocol Header
 * Based on OPENOCD_CJTAG_PATCH_GUIDE.md
 * 
 * This file should be placed in OpenOCD's src/jtag/drivers/ directory
 */

#ifndef OPENOCD_JTAG_DRIVERS_OSCAN1_H
#define OPENOCD_JTAG_DRIVERS_OSCAN1_H

#include <stdint.h>
#include <stdbool.h>
#include <helper/types.h>

/* JScan Commands */
#define JSCAN_OSCAN_ON              0x01
#define JSCAN_OSCAN_OFF             0x00
#define JSCAN_SELECT                0x02
#define JSCAN_DESELECT              0x03
#define JSCAN_SF_SELECT             0x04
#define JSCAN_RESET                 0x0F

/* Scanning Formats */
#define SF0                         0
#define SF1                         1
#define SF2                         2
#define SF3                         3

/**
 * Initialize OScan1 protocol
 * Must be called before any OScan1 operations
 * 
 * @return ERROR_OK on success, ERROR_FAIL otherwise
 */
int oscan1_init(void);

/**
 * Reset OScan1 state and exit JScan mode
 * 
 * @return ERROR_OK on success, ERROR_FAIL otherwise
 */
int oscan1_reset(void);

/**
 * Send OScan1 Attention Character (OAC)
 * 16 consecutive TCKC rising edges to enter JScan mode
 * 
 * @return ERROR_OK on success, ERROR_FAIL otherwise
 */
int oscan1_send_oac(void);

/**
 * Send JScan command
 * 
 * @param cmd JScan command code (4 bits)
 * @return ERROR_OK on success, ERROR_FAIL otherwise
 */
int oscan1_send_jscan_cmd(uint8_t cmd);

/**
 * Encode and send data using Scanning Format 0
 * SF0: TMS on TCKC rising edge, TDI on falling edge
 * 
 * @param tms TMS bit value
 * @param tdi TDI bit value  
 * @param tdo Pointer to receive TDO bit value
 * @return ERROR_OK on success, ERROR_FAIL otherwise
 */
int oscan1_sf0_encode(uint8_t tms, uint8_t tdi, uint8_t *tdo);

/**
 * Calculate CRC-8 for data integrity
 * Polynomial: x^8 + x^2 + x + 1
 * 
 * @param data Data buffer
 * @param len Length of data in bytes
 * @return CRC-8 checksum
 */
uint8_t oscan1_calc_crc8(const uint8_t *data, size_t len);

/**
 * Set scanning format
 * 
 * @param format Scanning format (SF0-SF3)
 * @return ERROR_OK on success, ERROR_FAIL otherwise
 */
int oscan1_set_scanning_format(uint8_t format);

/**
 * Enable or disable CRC-8 checking
 * 
 * @param enable true to enable, false to disable
 */
void oscan1_enable_crc(bool enable);

/**
 * Enable or disable parity checking
 * 
 * @param enable true to enable, false to disable
 */
void oscan1_enable_parity(bool enable);

#endif /* OPENOCD_JTAG_DRIVERS_OSCAN1_H */
