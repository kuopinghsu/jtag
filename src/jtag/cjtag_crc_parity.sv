/**
 * cJTAG CRC and Parity Checker
 * Implements error detection for IEEE 1149.7 cJTAG protocol
 *
 * Features:
 * - CRC-8 calculation for data packets
 * - Even/odd parity checking
 * - Configurable error detection modes
 * - Error statistics and reporting
 */

module cjtag_crc_parity #(
    parameter bit ENABLE_CRC = 1,      // Enable CRC checking
    parameter bit ENABLE_PARITY = 1,   // Enable parity checking
    parameter logic [7:0] CRC_POLYNOMIAL = 8'h07  // CRC-8 polynomial (x^8 + x^2 + x + 1)
)(
    input  logic        clk,
    input  logic        rst_n,

    // Data input interface
    input  logic [7:0]  data_in,       // Input data byte
    input  logic        data_valid,    // Data valid strobe
    input  logic        data_last,     // Last byte in packet

    // CRC interface
    output logic [7:0]  crc_value,     // Computed CRC value
    input  logic [7:0]  crc_expected,  // Expected CRC from packet
    input  logic        crc_check,     // Check CRC strobe
    output logic        crc_error,     // CRC mismatch detected

    // Parity interface
    output logic        parity_bit,    // Computed parity bit
    input  logic        parity_expected, // Expected parity
    input  logic        parity_check,  // Check parity strobe
    output logic        parity_error,  // Parity mismatch detected

    // Error statistics
    output logic [15:0] crc_error_count,
    output logic [15:0] parity_error_count,

    // Control
    input  logic        clear_errors   // Clear error counters
);

    // =========================================================================
    // CRC-8 Calculation
    // =========================================================================

    logic [7:0] crc_reg;
    logic [7:0] crc_next;

    // CRC-8 calculation (polynomial: x^8 + x^2 + x + 1)
    function automatic logic [7:0] crc8_update(
        input logic [7:0] crc_in,
        input logic [7:0] data
    );
        logic [7:0] crc0;
        logic [7:0] crc1;
        logic [7:0] crc2;
        logic [7:0] crc3;
        logic [7:0] crc4;
        logic [7:0] crc5;
        logic [7:0] crc6;
        logic [7:0] crc7;
        logic [7:0] crc8;

        // Initialize
        crc0 = crc_in ^ data;

        // Manually unroll 8 iterations
        crc1 = crc0[7] ? ((crc0 << 1) ^ CRC_POLYNOMIAL) : (crc0 << 1);
        crc2 = crc1[7] ? ((crc1 << 1) ^ CRC_POLYNOMIAL) : (crc1 << 1);
        crc3 = crc2[7] ? ((crc2 << 1) ^ CRC_POLYNOMIAL) : (crc2 << 1);
        crc4 = crc3[7] ? ((crc3 << 1) ^ CRC_POLYNOMIAL) : (crc3 << 1);
        crc5 = crc4[7] ? ((crc4 << 1) ^ CRC_POLYNOMIAL) : (crc4 << 1);
        crc6 = crc5[7] ? ((crc5 << 1) ^ CRC_POLYNOMIAL) : (crc5 << 1);
        crc7 = crc6[7] ? ((crc6 << 1) ^ CRC_POLYNOMIAL) : (crc6 << 1);
        crc8 = crc7[7] ? ((crc7 << 1) ^ CRC_POLYNOMIAL) : (crc7 << 1);

        crc8_update = crc8;
    endfunction

    // CRC register update
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            crc_reg <= 8'hFF;  // Initial CRC value
        end else begin
            if (data_last) begin
                crc_reg <= 8'hFF;  // Reset for next packet
            end else if (data_valid && ENABLE_CRC) begin
                crc_reg <= crc8_update(crc_reg, data_in);
            end
        end
    end

    assign crc_value = crc_reg;

    // CRC error detection
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            crc_error <= 1'b0;
        end else begin
            if (crc_check && ENABLE_CRC) begin
                crc_error <= (crc_value != crc_expected);
            end else begin
                crc_error <= 1'b0;
            end
        end
    end

    // =========================================================================
    // Parity Calculation
    // =========================================================================

    logic parity_reg;

    // Even parity calculation
    function logic calc_parity(input logic [7:0] data);
        calc_parity = ^data;  // XOR all bits
    endfunction

    // Parity accumulator
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            parity_reg <= 1'b0;
        end else begin
            if (data_last) begin
                parity_reg <= 1'b0;  // Reset for next packet
            end else if (data_valid && ENABLE_PARITY) begin
                parity_reg <= parity_reg ^ calc_parity(data_in);
            end
        end
    end

    assign parity_bit = parity_reg;

    // Parity error detection
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            parity_error <= 1'b0;
        end else begin
            if (parity_check && ENABLE_PARITY) begin
                parity_error <= (parity_bit != parity_expected);
            end else begin
                parity_error <= 1'b0;
            end
        end
    end

    // =========================================================================
    // Error Statistics
    // =========================================================================

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            crc_error_count <= 16'h0;
            parity_error_count <= 16'h0;
        end else begin
            if (clear_errors) begin
                crc_error_count <= 16'h0;
                parity_error_count <= 16'h0;
            end else begin
                if (crc_error && crc_check) begin
                    crc_error_count <= crc_error_count + 1;
                end
                if (parity_error && parity_check) begin
                    parity_error_count <= parity_error_count + 1;
                end
            end
        end
    end

endmodule
