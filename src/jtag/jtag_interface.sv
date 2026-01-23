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

            // TDO output in JTAG mode
            tdo = jtag_tdo;
        end
    end

`ifdef VERBOSE
    // Enhanced mode switching debug and signal change detection
    logic prev_mode_select;
    logic prev_jtag_clk, prev_jtag_tms, prev_jtag_tdi, prev_jtag_tdo;
    logic prev_oscan1_active, prev_oscan1_error;
    logic prev_tmsc_out, prev_tmsc_oen;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            prev_mode_select <= 1'b0;
            prev_jtag_clk <= 1'b0;
            prev_jtag_tms <= 1'b0;
            prev_jtag_tdi <= 1'b0;
            prev_jtag_tdo <= 1'b0;
            prev_oscan1_active <= 1'b0;
            prev_oscan1_error <= 1'b0;
            prev_tmsc_out <= 1'b0;
            prev_tmsc_oen <= 1'b0;
        end else begin
            prev_mode_select <= mode_select;

            if (`VERBOSE) begin
                // Debug mode switches
                if (mode_select != prev_mode_select) begin
                    $display("[INTF] @%0t *** MODE SWITCH ***", $time);
                    $display("[INTF]   From: %s", prev_mode_select ? "cJTAG" : "JTAG");
                    $display("[INTF]   To:   %s", mode_select ? "cJTAG" : "JTAG");
                end

                // Debug signal changes only (not periodic)
                if (jtag_clk != prev_jtag_clk || jtag_tms != prev_jtag_tms ||
                    jtag_tdi != prev_jtag_tdi || jtag_tdo != prev_jtag_tdo ||
                    (mode_select && (oscan1_active != prev_oscan1_active ||
                                    oscan1_error != prev_oscan1_error ||
                                    tmsc_out != prev_tmsc_out ||
                                    tmsc_oen != prev_tmsc_oen))) begin

                    $display("[INTF] @%0t [%s] jtag_clk=%b, jtag_tms=%b, jtag_tdi=%b, jtag_tdo=%b",
                             $time, mode_select ? "cJTAG" : "JTAG",
                             jtag_clk, jtag_tms, jtag_tdi, jtag_tdo);
                    if (mode_select) begin
                        $display("[INTF]   OScan1: active=%b, error=%b", oscan1_active, oscan1_error);
                        $display("[INTF]   TMSC: out=%b, oen=%b", tmsc_out, tmsc_oen);
                    end
                    $fflush();
                end

                // Always update previous values every cycle (not just on changes)
                prev_jtag_clk <= jtag_clk;
                prev_jtag_tms <= jtag_tms;
                prev_jtag_tdi <= jtag_tdi;
                prev_jtag_tdo <= jtag_tdo;
                if (mode_select) begin
                    prev_oscan1_active <= oscan1_active;
                    prev_oscan1_error <= oscan1_error;
                    prev_tmsc_out <= tmsc_out;
                    prev_tmsc_oen <= tmsc_oen;
                end
            end
        end
    end
`endif

endmodule
