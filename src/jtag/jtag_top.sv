/**
 * JTAG Top Module
 * Integrates all JTAG components
 * Uses 4-pin interface with mode multiplexing
 * Provides DMI interface for RISC-V Debug Module
 */

import jtag_dmi_pkg::*;

module jtag_top (
    input  logic        clk,
    input  logic        rst_n,

    // 4 Shared Physical I/O Pins (JTAG 4-wire / cJTAG 2-wire)
    // Pin 0: TCK (JTAG mode) / TCKC (cJTAG mode) - Input only
    input  logic        jtag_pin0_i,

    // Pin 1: TMS (JTAG mode) / TMSC (cJTAG mode) - Bidirectional
    input  logic        jtag_pin1_i,     // Input from pad
    output logic        jtag_pin1_o,     // Output to pad
    output logic        jtag_pin1_oen,   // Output enable active low (0=output, 1=input/tristate)

    // Pin 2: TDI (JTAG mode) / Unused (cJTAG mode) - Input only
    input  logic        jtag_pin2_i,

    // Pin 3: TDO (JTAG mode) / Unused (cJTAG mode) - Output only
    output logic        jtag_pin3_o,     // Output to pad
    output logic        jtag_pin3_oen,   // Output enable active low (0=output, 1=input/tristate)

    // Optional JTAG reset (JTAG mode only)
    input  logic        jtag_trst_n_i,

    // Mode control (from control register)
    input  logic        mode_select,     // 0=JTAG (4-wire), 1=cJTAG (2-wire)

    // DMI interface to Debug Module
    output logic [DMI_ADDR_WIDTH-1:0] dmi_addr,
    output logic [DMI_DATA_WIDTH-1:0] dmi_wdata,
    input  logic [DMI_DATA_WIDTH-1:0] dmi_rdata,
    output logic [1:0]                dmi_op,      // dmi_op_e
    input  logic [1:0]                dmi_resp,    // dmi_resp_e
    output logic                      dmi_req_valid,
    input  logic                      dmi_req_ready,

    // IDCODE output
    output logic [31:0] idcode,

    // Mode status
    output logic        active_mode
);

    // Internal JTAG signals (after mode demux)
    logic       jtag_tck, jtag_tms, jtag_tdi, jtag_trst_n;

    // Internal cJTAG signals (after mode demux)
    logic       cjtag_tco, cjtag_tmsc_in, cjtag_tmsc_out, cjtag_tmsc_oen;

    // TAP controller signals
    logic [3:0] tap_state;
    logic       shift_dr, shift_ir, update_dr, update_ir, capture_dr, capture_ir;
    logic [4:0] ir_out;

    // TAP interface signals (from jtag_interface module)
    logic       tap_clk, tap_tms, tap_tdi, tap_tdo, tap_rst_n;
    logic       ir_out_tdo, dtm_tdo;
    logic       intf_tdi_oscan, intf_tmsc_out, intf_tmsc_oen;

    // TAP reset signal from TAP controller
    logic       tap_reset_signal;

    // ========================================
    // Pin Multiplexing Based on Mode
    // ========================================
    //
    // Physical Pin Mapping:
    // JTAG Mode (4-wire):        cJTAG Mode (2-wire):
    //   Pin 0: TCK (in)            Pin 0: TCKC (in)
    //   Pin 1: TMS (in)            Pin 1: TMSC (bidir)
    //   Pin 2: TDI (in)            Pin 2: Unused
    //   Pin 3: TDO (out)           Pin 3: Unused
    //
    // ========================================

    // Mode status output
    assign active_mode = mode_select;

    // Input demultiplexing from physical pins
    always_comb begin
        if (mode_select) begin
            // ===== cJTAG Mode (2-wire) =====
            // Only Pin 0 and Pin 1 are used
            cjtag_tco       = jtag_pin0_i;     // Pin 0: TCKC input
            cjtag_tmsc_in   = jtag_pin1_i;     // Pin 1: TMSC input
            jtag_tck        = 1'b0;            // JTAG signals unused
            jtag_tms        = 1'b0;
            jtag_tdi        = 1'b0;
            jtag_trst_n     = 1'b1;            // No reset in cJTAG
        end else begin
            // ===== JTAG Mode (4-wire) =====
            // All 4 pins are used
            jtag_tck        = jtag_pin0_i;     // Pin 0: TCK input
            jtag_tms        = jtag_pin1_i;     // Pin 1: TMS input
            jtag_tdi        = jtag_pin2_i;     // Pin 2: TDI input
            jtag_trst_n     = jtag_trst_n_i;   // Optional TRST_N
            cjtag_tco       = 1'b0;            // cJTAG signals unused
            cjtag_tmsc_in   = 1'b0;
        end
    end

    // Output multiplexing to physical pins
    always_comb begin
        if (mode_select) begin
            // ===== cJTAG Mode =====
            // Pin 1: TMSC bidirectional (output when oen=1)
            jtag_pin1_o   = intf_tmsc_out;     // TMSC output data
            jtag_pin1_oen = intf_tmsc_oen;     // TMSC output enable
            // Pin 3: Unused in cJTAG mode
            jtag_pin3_o   = 1'b0;
            jtag_pin3_oen = 1'b1;              // Input/tristate mode (oen active low)
        end else begin
            // ===== JTAG Mode =====
            // Pin 1: TMS is input only
            jtag_pin1_o   = 1'b0;              // Not used
            jtag_pin1_oen = 1'b1;              // Input mode (oen active low)
            // Pin 3: TDO output
            jtag_pin3_o   = tap_tdo_internal;  // TDO data
            // Enable TDO during all shift-related states per IEEE 1149.1
            // oen is active low: 0=output enabled, 1=tristate
            jtag_pin3_oen = ~((tap_state == 4'h3) |  // DR_CAPTURE
                            (tap_state == 4'h4) |   // DR_SHIFT
                            (tap_state == 4'h5) |   // DR_EXIT1
                            (tap_state == 4'h6) |   // DR_PAUSE
                            (tap_state == 4'h7) |   // DR_EXIT2
                            (tap_state == 4'hA) |   // IR_CAPTURE
                            (tap_state == 4'hB) |   // IR_SHIFT
                            (tap_state == 4'hC) |   // IR_EXIT1
                            (tap_state == 4'hD) |   // IR_PAUSE
                            (tap_state == 4'hE));   // IR_EXIT2
        end
    end

    // ========================================
    // JTAG Interface - mode select and signal routing
    // ========================================
    // Internal TDO signal from TAP mux (before jtag_interface)
    logic tap_tdo_mux;

    // Final TDO signal from jtag_interface
    logic tap_tdo_internal;

    jtag_interface jtag_iface (
        .clk              (clk),
        .rst_n            (rst_n),
        .mode_select      (mode_select),
        .tck              (jtag_tck),
        .tms              (jtag_tms),
        .tdi              (jtag_tdi),
        .tdo              (tap_tdo_internal),   // Output to pins (from jtag_interface)
        .trst_n           (jtag_trst_n),
        .tco              (cjtag_tco),
        .tmsc_in          (cjtag_tmsc_in),
        .tmsc_out         (intf_tmsc_out),
        .tmsc_oen         (intf_tmsc_oen),
        .tdi_oscan        (intf_tdi_oscan),
        .jtag_clk         (tap_clk),
        .jtag_tms         (tap_tms),
        .jtag_tdi         (tap_tdi),
        .jtag_tdo         (tap_tdo_mux),        // Input from TAP mux
        .jtag_rst_n       (tap_rst_n),
        .active_mode      (),               // Driven directly by jtag_top
        .crc_error_count  (),               // Not exposed at top level
        .parity_error_count()               // Not exposed at top level
    );

    // TAP Controller State Machine
    jtag_tap_controller tap_ctrl (
        .clk              (tap_clk),
        .rst_n            (tap_rst_n),
        .tms              (tap_tms),
        .state            (tap_state),
        .shift_dr         (shift_dr),
        .shift_ir         (shift_ir),
        .update_dr        (update_dr),
        .update_ir        (update_ir),
        .capture_dr       (capture_dr),
        .capture_ir       (capture_ir),
        .tap_reset        (tap_reset_signal)
    );

    // Instruction Register
    jtag_instruction_register ir_reg (
        .clk              (tap_clk),
        .rst_n            (tap_rst_n),
        .tap_reset        (tap_reset_signal),
        .tdi              (tap_tdi),
        .tdo              (ir_out_tdo),
        .shift_ir         (shift_ir),
        .capture_ir       (capture_ir),
        .update_ir        (update_ir),
        .ir_out           (ir_out)
    );

    // Debug Transport Module (DTM) with DMI interface
    jtag_dtm dtm (
        .clk              (tap_clk),
        .rst_n            (tap_rst_n),
        .tdi              (tap_tdi),
        .tdo              (dtm_tdo),
        .shift_dr         (shift_dr),
        .update_dr        (update_dr),
        .capture_dr       (capture_dr),
        .ir_out           (ir_out),
        .dmi_addr         (dmi_addr),
        .dmi_wdata        (dmi_wdata),
        .dmi_rdata        (dmi_rdata),
        .dmi_op           (dmi_op),
        .dmi_resp         (dmi_resp),
        .dmi_req_valid    (dmi_req_valid),
        .dmi_req_ready    (dmi_req_ready),
        .idcode           (idcode)
    );

    // TDO multiplexer - select between IR and DR data
    // This drives tap_tdo_mux which goes to jtag_interface as jtag_tdo input
    always_comb begin
        case (tap_state)
            4'h9:    tap_tdo_mux = ir_out_tdo;        // IR_SELECT_SCAN
            4'hA:    tap_tdo_mux = ir_out_tdo;        // IR_CAPTURE
            4'hB:    tap_tdo_mux = ir_out_tdo;        // IR_SHIFT
            4'hC:    tap_tdo_mux = ir_out_tdo;        // IR_EXIT1
            4'hD:    tap_tdo_mux = ir_out_tdo;        // IR_PAUSE
            4'hE:    tap_tdo_mux = ir_out_tdo;        // IR_EXIT2
            4'hF:    tap_tdo_mux = ir_out_tdo;        // IR_UPDATE
            default: tap_tdo_mux = dtm_tdo;           // DR operations
        endcase
    end

endmodule
