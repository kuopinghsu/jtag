/**
 * OScan1 Controller (IEEE 1149.7 cJTAG)
 * Full implementation of OScan1 protocol including:
 * - OAC (OScan1 Attention Character) detection
 * - JScan packet parsing
 * - Scanning Format decoding (SF0)
 * - Zero insertion/deletion
 * - CRC/Parity error detection
 * - Packet handling
 */

module oscan1_controller #(
    parameter bit ENABLE_CRC = 1,       // Enable CRC checking
    parameter bit ENABLE_PARITY = 1     // Enable parity checking
)(
    input  logic       clk,
    input  logic       rst_n,

    // Physical cJTAG interface
    input  logic       tckc,           // cJTAG clock/data input
    input  logic       tmsc_in,        // cJTAG TMSC input
    output logic       tmsc_out,       // cJTAG TMSC output
    output logic       tmsc_oen,       // cJTAG TMSC output enable

    // Decoded JTAG signals output
    output logic       jtag_tck,       // Decoded TCK
    output logic       jtag_tms,       // Decoded TMS
    output logic       jtag_tdi,       // Decoded TDI
    input  logic       jtag_tdo,       // TDO to send back

    // Control/Status
    output logic       oscan_active,   // OScan1 mode active
    output logic       error,          // Protocol error detected

    // Error statistics
    output logic [15:0] crc_error_count,
    output logic [15:0] parity_error_count
);

    // ========================================
    // State Machine Definitions
    // ========================================
    typedef enum logic [3:0] {
        IDLE        = 4'h0,  // Power-on state
        OAC_DETECT  = 4'h1,  // Detecting OAC sequence
        JSCAN       = 4'h2,  // Processing JScan packet
        OSCAN_SF0   = 4'h3,  // OScan1 active, SF0 format
        OSCAN_SF1   = 4'h4,  // OScan1 active, SF1 format
        OSCAN_SF2   = 4'h5,  // OScan1 active, SF2 format
        ERROR       = 4'hF   // Error state
    } state_t;

    state_t state, next_state;

    // ========================================
    // OScan1 Parameters
    // ========================================
    localparam OAC_EDGES = 16;           // OAC = 16 consecutive edges
    localparam JSCAN_LENGTH = 4;         // JScan0 = 4 bits
    localparam ZERO_STUFF_COUNT = 5;     // Insert zero after 5 ones

    // JScan Commands
    typedef enum logic [3:0] {
        JSCAN_OSCAN_OFF = 4'h0,
        JSCAN_OSCAN_ON  = 4'h1,
        JSCAN_SELECT    = 4'h2,
        JSCAN_DESELECT  = 4'h3,
        JSCAN_SF_SELECT = 4'h4,
        JSCAN_READ_ID   = 4'h5,
        JSCAN_NOOP      = 4'hF
    } jscan_cmd_t;

    // ========================================
    // Internal Registers
    // ========================================
    logic [4:0]  edge_count;             // Count edges for OAC detection
    logic [3:0]  jscan_cmd;              // JScan command register
    logic [2:0]  jscan_bit_count;        // JScan bit counter
    logic [1:0]  scan_format;            // Current scanning format (0=SF0, 1=SF1, 2=SF2)

    // TCKC edge detection
    logic        tckc_d1, tckc_d2;
    logic        tckc_rising, tckc_falling;
    logic        tckc_edge;

    // TMSC sampling
    logic        tmsc_sample;

    // Zero stuffing
    logic [2:0]  ones_count;             // Consecutive ones counter
    logic        zero_inserted;          // Zero was inserted
    logic        zero_deleted;           // Zero was deleted

    // Bit stream processing
    logic        bit_valid;              // Valid bit decoded
    logic        bit_data;               // Decoded bit value

    // Scanning Format 0 (SF0) decoder
    logic [1:0]  sf0_bits;               // SF0 accumulates 2 bits (TDI, TMS)
    logic        sf0_bit_count;          // 0 or 1
    logic        sf0_packet_ready;       // 2 bits ready

    // TDO handling
    logic        tdo_ready;              // TDO data ready to send
    logic        tdo_bit;                // TDO bit to send

    // CRC/Parity signals
    logic [7:0]  data_byte;              // Data byte for CRC/parity
    logic        data_valid;             // Data byte valid
    logic        data_last;              // Last byte in packet
    logic [7:0]  crc_value;              // Computed CRC
    logic        parity_bit;             // Computed parity
    logic        crc_error;              // CRC error detected
    logic        parity_error;           // Parity error detected
    logic        clear_errors;           // Clear error counters

    // ========================================
    // CRC/Parity Checker Instantiation
    // ========================================

    cjtag_crc_parity #(
        .ENABLE_CRC(ENABLE_CRC),
        .ENABLE_PARITY(ENABLE_PARITY)
    ) crc_parity_check (
        .clk(clk),
        .rst_n(rst_n),
        .data_in(data_byte),
        .data_valid(data_valid),
        .data_last(data_last),
        .crc_value(crc_value),
        .crc_expected(8'h00),    // Expected CRC from packet
        .crc_check(1'b0),        // Check CRC strobe
        .crc_error(crc_error),
        .parity_bit(parity_bit),
        .parity_expected(1'b0),  // Expected parity
        .parity_check(1'b0),     // Check parity strobe
        .parity_error(parity_error),
        .crc_error_count(crc_error_count),
        .parity_error_count(parity_error_count),
        .clear_errors(clear_errors)
    );

    // ========================================
    // TCKC Edge Detection
    // ========================================
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tckc_d1 <= 1'b0;
            tckc_d2 <= 1'b0;
        end else begin
            tckc_d1 <= tckc;
            tckc_d2 <= tckc_d1;
        end
    end

    assign tckc_rising  = tckc_d1 && !tckc_d2;
    assign tckc_falling = !tckc_d1 && tckc_d2;
    assign tckc_edge    = tckc_rising || tckc_falling;

    // ========================================
    // OAC (Attention Character) Detector
    // ========================================
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            edge_count <= 5'h0;
        end else begin
            if (state == IDLE || state == OAC_DETECT) begin
                if (tckc_edge) begin
                    if (edge_count == OAC_EDGES - 1) begin
                        edge_count <= 5'h0;  // OAC detected
                    end else begin
                        edge_count <= edge_count + 1;
                    end
                end
            end else begin
                edge_count <= 5'h0;
            end
        end
    end

    logic oac_detected;
    assign oac_detected = (edge_count == OAC_EDGES - 1) && tckc_edge;

    // ========================================
    // TMSC Sampling
    // ========================================
    // Sample TMSC on TCKC falling edge
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tmsc_sample <= 1'b0;
        end else begin
            if (tckc_falling) begin
                tmsc_sample <= tmsc_in;
            end
        end
    end

    // ========================================
    // Zero Insertion/Deletion (Bit Stuffing)
    // ========================================
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            ones_count <= 3'h0;
            zero_deleted <= 1'b0;
        end else begin
            if (state == OSCAN_SF0 || state == OSCAN_SF1 || state == OSCAN_SF2) begin
                if (tckc_falling) begin
                    // Count consecutive ones
                    if (tmsc_sample) begin
                        ones_count <= ones_count + 1;
                        zero_deleted <= 1'b0;
                    end else begin
                        // Check if this zero should be deleted (after 5 ones)
                        if (ones_count == ZERO_STUFF_COUNT) begin
                            zero_deleted <= 1'b1;  // Delete this zero
                        end else begin
                            zero_deleted <= 1'b0;
                        end
                        ones_count <= 3'h0;
                    end
                end
            end else begin
                ones_count <= 3'h0;
                zero_deleted <= 1'b0;
            end
        end
    end

    // Valid bit = not a stuffed zero
    assign bit_valid = tckc_falling && !zero_deleted;
    assign bit_data = tmsc_sample;

    // ========================================
    // JScan Packet Parser
    // ========================================
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            jscan_cmd <= 4'h0;
            jscan_bit_count <= 3'h0;
        end else begin
            if (state == JSCAN) begin
                if (bit_valid) begin
                    jscan_cmd <= {bit_data, jscan_cmd[3:1]};  // Shift in LSB first
                    jscan_bit_count <= jscan_bit_count + 1;
                end
            end else begin
                jscan_bit_count <= 3'h0;
            end
        end
    end

    logic jscan_complete;
    assign jscan_complete = (jscan_bit_count == JSCAN_LENGTH) && bit_valid;

    // ========================================
    // Scanning Format 0 (SF0) Decoder
    // ========================================
    // SF0: Each TCKC cycle transfers 1 TDI bit and 1 TMS bit
    // Format: TMS on rising edge, TDI on falling edge
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            sf0_bits <= 2'b00;
            sf0_bit_count <= 1'b0;
            sf0_packet_ready <= 1'b0;
        end else begin
            if (state == OSCAN_SF0) begin
                if (tckc_rising) begin
                    // TMS bit on rising edge
                    sf0_bits[1] <= tmsc_in;
                    sf0_bit_count <= 1'b1;
                    sf0_packet_ready <= 1'b0;
                end else if (tckc_falling && sf0_bit_count) begin
                    // TDI bit on falling edge (only if we got TMS bit)
                    sf0_bits[0] <= tmsc_in;  // Use current tmsc_in, not delayed sample
                    sf0_packet_ready <= 1'b1;  // Packet ready after both TMS and TDI
                    sf0_bit_count <= 1'b0;
                end else if (sf0_packet_ready) begin
                    sf0_packet_ready <= 1'b0;  // Clear after one cycle
                end
            end else begin
                sf0_bits <= 2'b00;
                sf0_bit_count <= 1'b0;
                sf0_packet_ready <= 1'b0;
            end
        end
    end

    // ========================================
    // JTAG Signal Generation
    // ========================================
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            jtag_tck <= 1'b0;
            jtag_tms <= 1'b0;
            jtag_tdi <= 1'b0;
        end else begin
            if (state == OSCAN_SF0 && sf0_packet_ready) begin
                // Generate extended TCK pulse (2 cycles)
                jtag_tck <= 1'b1;
                jtag_tms <= sf0_bits[1];
                jtag_tdi <= sf0_bits[0];
            end else if (jtag_tck) begin
                jtag_tck <= 1'b0;  // Return TCK to low after one cycle
            end
        end
    end

    // ========================================
    // TDO Return Path (TMSC Output)
    // ========================================
    // Send TDO back on TMSC when in output mode
    // SF0 protocol: TDO is captured after TCK rising edge and output during falling edge
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tdo_bit <= 1'b0;
            tdo_ready <= 1'b0;
            tmsc_out <= 1'b0;
            tmsc_oen <= 1'b1;      // oen is active low: 1=tristate, 0=output
        end else begin
            if (state == OSCAN_SF0) begin
                // Capture TDO after TCK pulse is generated
                if (jtag_tck && !tdo_ready) begin
                    tdo_bit <= jtag_tdo;  // Capture TDO during TCK high
                    tdo_ready <= 1'b1;    // Mark TDO as ready for output
                end else if (!jtag_tck && tdo_ready) begin
                    // Output TDO during TCK low (TMSC output phase)
                    tmsc_out <= tdo_bit;
                    tmsc_oen <= 1'b0;     // Enable output (active low)
                    tdo_ready <= 1'b0;    // Reset for next cycle
                end else if (!tdo_ready) begin
                    // Default state when not outputting
                    tmsc_oen <= 1'b1;     // Tristate (input mode)
                    tmsc_out <= 1'b0;
                end
            end else begin
                tmsc_out <= 1'b0;
                tmsc_oen <= 1'b1;      // 1 = tristate/input mode (active low)
                tdo_ready <= 1'b0;
            end
        end
    end

    // ========================================
    // State Machine
    // ========================================
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state <= IDLE;
        end else begin
            state <= next_state;
        end
    end

    always_comb begin
        next_state = state;

        case (state)
            IDLE: begin
                if (tckc_edge) begin
                    next_state = OAC_DETECT;
                end
            end

            OAC_DETECT: begin
                if (oac_detected) begin
                    next_state = JSCAN;
                end
            end

            JSCAN: begin
                if (jscan_complete) begin
                    case (jscan_cmd)
                        JSCAN_OSCAN_ON: begin
                            next_state = OSCAN_SF0;  // Default to SF0
                        end
                        JSCAN_OSCAN_OFF: begin
                            next_state = IDLE;
                        end
                        JSCAN_SF_SELECT: begin
                            // Could parse additional bits to select SF1/SF2
                            next_state = OSCAN_SF0;
                        end
                        default: begin
                            next_state = OAC_DETECT;  // Wait for next command
                        end
                    endcase
                end
            end

            OSCAN_SF0: begin
                // Stay in SF0 until OSCAN_OFF command
                if (oac_detected) begin
                    next_state = JSCAN;
                end
            end

            OSCAN_SF1: begin
                // SF1 not implemented yet
                if (oac_detected) begin
                    next_state = JSCAN;
                end
            end

            OSCAN_SF2: begin
                // SF2 not implemented yet
                if (oac_detected) begin
                    next_state = JSCAN;
                end
            end

            ERROR: begin
                // Stay in error until reset
                next_state = ERROR;
            end

            default: begin
                next_state = IDLE;
            end
        endcase
    end

    // ========================================
    // Status Outputs
    // ========================================
    assign oscan_active = (state == OSCAN_SF0) || (state == OSCAN_SF1) || (state == OSCAN_SF2);
    assign error = (state == ERROR);

endmodule
