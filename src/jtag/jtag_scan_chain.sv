/**
 * Multi-TAP Scan Chain Controller
 * Supports multiple JTAG TAPs in a daisy-chain configuration
 * Implements bypass register management and TAP selection
 *
 * Features:
 * - Up to 8 TAPs in a single chain
 * - Automatic bypass register insertion
 * - TAP selection via IR length configuration
 * - Pre/post padding for non-selected TAPs
 */

module jtag_scan_chain #(
    parameter int NUM_TAPS = 1,                    // Number of TAPs in chain (1-8)
    parameter int MAX_IR_LENGTH = 8,               // Maximum IR length
    parameter int IR_LENGTH_0 = 8,                 // IR length for TAP 0
    parameter int IR_LENGTH_1 = 8,                 // IR length for TAP 1
    parameter int IR_LENGTH_2 = 8,                 // IR length for TAP 2
    parameter int IR_LENGTH_3 = 8,                 // IR length for TAP 3
    parameter int IR_LENGTH_4 = 8,                 // IR length for TAP 4
    parameter int IR_LENGTH_5 = 8,                 // IR length for TAP 5
    parameter int IR_LENGTH_6 = 8,                 // IR length for TAP 6
    parameter int IR_LENGTH_7 = 8                  // IR length for TAP 7
)(
    input  logic        clk,
    input  logic        rst_n,

    // Upstream JTAG signals (from jtag_interface)
    input  logic        tap_tck,
    input  logic        tap_tms,
    input  logic        tap_tdi,
    output logic        tap_tdo,

    // TAP control signals
    input  logic        shift_dr,
    input  logic        shift_ir,
    input  logic        capture_dr,
    input  logic        capture_ir,
    input  logic        update_dr,
    input  logic        update_ir,

    // Downstream TAP interfaces (to individual TAPs)
    output logic [NUM_TAPS-1:0] tap_tck_out,
    output logic [NUM_TAPS-1:0] tap_tms_out,
    output logic [NUM_TAPS-1:0] tap_tdi_out,
    input  logic [NUM_TAPS-1:0] tap_tdo_in,

    // TAP selection and configuration
    input  logic [$clog2(NUM_TAPS)-1:0] selected_tap,   // Currently selected TAP
    output logic [NUM_TAPS-1:0]         tap_active,     // Active TAP indicators

    // Chain status
    output logic [15:0]         total_ir_length,        // Total IR chain length
    output logic [15:0]         total_dr_length         // Total DR chain length (dynamic)
);

    // Helper function to get IR length by index (Yosys workaround for array parameters)
    function int get_ir_length;
        input int tap_idx;
        begin
            case (tap_idx)
                0: get_ir_length = IR_LENGTH_0;
                1: get_ir_length = IR_LENGTH_1;
                2: get_ir_length = IR_LENGTH_2;
                3: get_ir_length = IR_LENGTH_3;
                4: get_ir_length = IR_LENGTH_4;
                5: get_ir_length = IR_LENGTH_5;
                6: get_ir_length = IR_LENGTH_6;
                7: get_ir_length = IR_LENGTH_7;
                default: get_ir_length = 8;
            endcase
        end
    endfunction

    // Calculate total IR length (sum of all TAP IR lengths)
    always_comb begin
        total_ir_length = 0;
        for (int i = 0; i < NUM_TAPS; i++) begin
            total_ir_length = total_ir_length + get_ir_length(i);
        end
    end

    // Bypass registers for each TAP (used when TAP is not selected)
    logic [NUM_TAPS-1:0] bypass_reg;

    // IR shift registers for chain management
    logic [MAX_IR_LENGTH-1:0] ir_shift_reg [NUM_TAPS];

    // DR shift state tracking
    logic [15:0] shift_count;
    logic [15:0] pre_padding;   // Bits to shift before selected TAP
    logic [15:0] post_padding;  // Bits to shift after selected TAP

    // =========================================================================
    // TAP Clock and Control Signal Distribution
    // =========================================================================

    // All TAPs receive the same clock and TMS
    always_comb begin
        for (int i = 0; i < NUM_TAPS; i++) begin
            tap_tck_out[i] = tap_tck;
            tap_tms_out[i] = tap_tms;
        end
    end

    // =========================================================================
    // TAP Selection and Active Indicators
    // =========================================================================

    always_comb begin
        tap_active = '0;
        if (selected_tap < NUM_TAPS) begin
            tap_active[selected_tap] = 1'b1;
        end
    end

    // =========================================================================
    // IR Scan Chain Management
    // =========================================================================

    // Calculate pre and post padding for IR shifts
    always_comb begin
        pre_padding = 0;
        post_padding = 0;

        // Pre-padding: sum of IR lengths before selected TAP
        for (int i = 0; i < NUM_TAPS; i++) begin
            if (i < selected_tap) begin
                pre_padding = pre_padding + get_ir_length(i);
            end else if (i > selected_tap) begin
                post_padding = post_padding + get_ir_length(i);
            end
        end
    end

    // =========================================================================
    // Data Shift Chain (IR and DR)
    // =========================================================================

    logic tap_tdi_chain [NUM_TAPS+1];  // TDI chain: [0]=input, [NUM_TAPS]=output

    // Chain input
    assign tap_tdi_chain[0] = tap_tdi;

    // Chain connections between TAPs
    generate
        for (genvar i = 0; i < NUM_TAPS; i++) begin : g_chain
            // IR Shift: Use bypass or actual TAP output
            always_comb begin
                if (shift_ir) begin
                    // During IR shift, non-selected TAPs use bypass (fixed 1)
                    if (tap_active[i]) begin
                        tap_tdi_out[i] = tap_tdi_chain[i];
                        tap_tdi_chain[i+1] = tap_tdo_in[i];
                    end else begin
                        tap_tdi_out[i] = 1'b1;  // IR bypass value
                        tap_tdi_chain[i+1] = 1'b1;
                    end
                end else if (shift_dr) begin
                    // During DR shift, non-selected TAPs use 1-bit bypass register
                    if (tap_active[i]) begin
                        tap_tdi_out[i] = tap_tdi_chain[i];
                        tap_tdi_chain[i+1] = tap_tdo_in[i];
                    end else begin
                        tap_tdi_out[i] = bypass_reg[i];
                        tap_tdi_chain[i+1] = bypass_reg[i];
                    end
                end else begin
                    tap_tdi_out[i] = tap_tdi_chain[i];
                    tap_tdi_chain[i+1] = tap_tdo_in[i];
                end
            end
        end
    endgenerate

    // Chain output
    assign tap_tdo = tap_tdi_chain[NUM_TAPS];

    // =========================================================================
    // Bypass Register Management
    // =========================================================================

    always_ff @(posedge tap_tck or negedge rst_n) begin
        if (!rst_n) begin
            bypass_reg <= '1;  // Bypass registers default to 1
            shift_count <= 16'h0;
        end else begin
            if (capture_dr) begin
                // Load bypass registers with 0
                bypass_reg <= '0;
                shift_count <= 16'h0;
            end else if (shift_dr) begin
                // Shift bypass registers for non-selected TAPs
                for (int i = 0; i < NUM_TAPS; i++) begin
                    if (!tap_active[i]) begin
                        bypass_reg[i] <= tap_tdi_chain[i];
                    end
                end
                shift_count <= shift_count + 1;
            end
        end
    end

    // =========================================================================
    // IR Length Tracking
    // =========================================================================

    // Track IR shifts for proper IR chain management
    logic [MAX_IR_LENGTH-1:0] ir_shift_count;

    always_ff @(posedge tap_tck or negedge rst_n) begin
        if (!rst_n) begin
            ir_shift_count <= '0;
        end else begin
            if (capture_ir) begin
                ir_shift_count <= '0;
            end else if (shift_ir) begin
                ir_shift_count <= ir_shift_count + 1;
            end
        end
    end

    // =========================================================================
    // Dynamic DR Length Calculation
    // =========================================================================

    // DR length is dynamic and depends on the current instruction
    // For simplicity, assume each non-selected TAP contributes 1 bit (bypass)
    always_comb begin
        total_dr_length = NUM_TAPS - 1;  // Bypass bits
        // Selected TAP contributes its DR length (implementation-specific)
        // This would need to be provided by the selected TAP
        total_dr_length = total_dr_length + 32;  // Assume 32-bit DR for selected TAP
    end

endmodule
