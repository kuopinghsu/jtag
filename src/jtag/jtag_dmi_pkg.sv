/**
 * Debug Module Interface (DMI) Package
 * Defines the interface between JTAG and RISC-V Debug Module
 * Based on RISC-V Debug Specification 0.13.2
 */

package jtag_dmi_pkg;

    // DMI address width (typically 7 bits for RISC-V debug)
    parameter int DMI_ADDR_WIDTH = 7;
    
    // DMI data width (32 bits as per spec)
    parameter int DMI_DATA_WIDTH = 32;
    
    // DMI operation codes
    typedef enum logic [1:0] {
        DMI_OP_NOP    = 2'b00,  // No operation
        DMI_OP_READ   = 2'b01,  // Read operation
        DMI_OP_WRITE  = 2'b10,  // Write operation
        DMI_OP_RSVD   = 2'b11   // Reserved
    } dmi_op_e;
    
    // DMI response codes
    typedef enum logic [1:0] {
        DMI_RESP_SUCCESS = 2'b00,  // Operation successful
        DMI_RESP_FAILED  = 2'b10,  // Operation failed
        DMI_RESP_BUSY    = 2'b11   // Operation in progress
    } dmi_resp_e;

endpackage : jtag_dmi_pkg
