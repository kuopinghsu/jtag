/**
 * Main JTAG Interface with dual-mode support
 * Supports both standard JTAG and cJTAG OScan1 mode
 * Now includes full OScan1 protocol implementation
 */

module jtag_interface (
    input  logic       clk,
    input  logic       rst_n,
    input  logic       mode_select,     // 0=JTAG, 1=cJTAG

    // Standard JTAG pins (4-wire)
    input  logic       tck,
    input  logic       tms,
    input  logic       tdi,
    output logic       tdo,
    input  logic       trst_n,

    // cJTAG OScan1 pins (2-wire with bidirectional)
    input  logic       tco,             // cJTAG clock/data (input only)
    input  logic       tmsc_in,         // cJTAG TMSC input (bidirectional)
    output logic       tmsc_out,        // cJTAG TMSC output
    output logic       tmsc_oen,        // cJTAG TMSC output enable
    output logic       tdi_oscan,       // cJTAG return data (for debug)

    // Internal JTAG signals
    output logic       jtag_clk,
    output logic       jtag_tms,
    output logic       jtag_tdi,
    input  logic       jtag_tdo,        // TDO from TAP (input only)
    output logic       jtag_rst_n,

    // Mode status
    output logic       active_mode,     // 0=JTAG, 1=cJTAG

    // Error statistics (from cJTAG)
    output logic [15:0] crc_error_count,
    output logic [15:0] parity_error_count
);

    // OScan1 controller signals
    logic oscan1_tck, oscan1_tms, oscan1_tdi;
    logic oscan1_tmsc_out, oscan1_tmsc_oen;
    logic oscan1_active, oscan1_error;
    logic [15:0] oscan1_crc_errors, oscan1_parity_errors;

    // Mode selection
    assign active_mode = mode_select;
    assign crc_error_count = oscan1_crc_errors;
    assign parity_error_count = oscan1_parity_errors;

    // ========================================
    // OScan1 Controller Instantiation
    // ========================================
    oscan1_controller oscan1_ctrl (
        .clk            (clk),
        .rst_n          (rst_n),
        .tckc           (tco),
        .tmsc_in        (tmsc_in),
        .tmsc_out       (oscan1_tmsc_out),
        .tmsc_oen       (oscan1_tmsc_oen),
        .jtag_tck       (oscan1_tck),
        .jtag_tms       (oscan1_tms),
        .jtag_tdi       (oscan1_tdi),
        .jtag_tdo       (jtag_tdo),
        .oscan_active   (oscan1_active),
        .error          (oscan1_error),
        .crc_error_count(oscan1_crc_errors),
        .parity_error_count(oscan1_parity_errors)
    );

    // ========================================
    // Mode Multiplexing
    // ========================================
    // Route signals based on mode selection
    always_comb begin
        if (mode_select) begin
            // cJTAG mode - use OScan1 controller outputs
            jtag_clk = oscan1_tck;
            jtag_tms = oscan1_tms;
            jtag_tdi = oscan1_tdi;
            jtag_rst_n = 1'b1;           // No hardware reset in cJTAG

            // TMSC bidirectional control from OScan1 controller
            tmsc_out = oscan1_tmsc_out;
            tmsc_oen = oscan1_tmsc_oen;

            // TDO return for debugging
            tdi_oscan = jtag_tdo;
            tdo = jtag_tdo;              // Pass through TDO in cJTAG mode
        end else begin
            // Standard JTAG mode - direct connection
            jtag_clk = tck;
            jtag_tms = tms;
            jtag_tdi = tdi;
            jtag_rst_n = trst_n;

            // TMSC not used in JTAG mode
            tmsc_out = 1'b0;
            tmsc_oen = 1'b0;
            tdi_oscan = 1'b0;

            // TDO output in JTAG mode
            tdo = jtag_tdo;
        end
    end

`ifdef VERBOSE
    // Enhanced mode switching debug
    logic prev_mode_select;
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            prev_mode_select <= 1'b0;
        end else begin
            prev_mode_select <= mode_select;

            if (`VERBOSE) begin
                // Debug mode switches
                if (mode_select != prev_mode_select) begin
                    $display("[INTF] *** MODE SWITCH ***");
                    $display("[INTF]   From: %s", prev_mode_select ? "cJTAG" : "JTAG");
                    $display("[INTF]   To:   %s", mode_select ? "cJTAG" : "JTAG");
                end

                // Debug signal routing every few clocks
                if ($time % (100*1000) == 0) begin // Every 100ms
                    $display("[INTF] Signal routing check:");
                    $display("[INTF]   Mode: %s", mode_select ? "cJTAG" : "JTAG");
                    $display("[INTF]   jtag_clk=%b, jtag_tms=%b, jtag_tdi=%b, jtag_tdo=%b",
                             jtag_clk, jtag_tms, jtag_tdi, jtag_tdo);
                    if (mode_select) begin
                        $display("[INTF]   OScan1: active=%b, error=%b", oscan1_active, oscan1_error);
                        $display("[INTF]   TMSC: out=%b, oen=%b", tmsc_out, tmsc_oen);
                    end
                    $fflush();
                end
            end
        end
    end
`endif

endmodule
