/**
 * JTAG DTM (Debug Transport Module)
 * Provides DMI interface for RISC-V Debug Module
 * Implements DTMCS and DMI registers as per RISC-V Debug Spec
 */

import jtag_dmi_pkg::*;

module jtag_dtm (
    input  logic                    clk,
    input  logic                    rst_n,

    // JTAG TAP interface
    input  logic                    tdi,
    output logic                    tdo,
    input  logic                    shift_dr,
    input  logic                    update_dr,
    input  logic                    capture_dr,
    input  logic [4:0]              ir_out,

    // DMI interface to Debug Module
    output logic [DMI_ADDR_WIDTH-1:0] dmi_addr,
    output logic [DMI_DATA_WIDTH-1:0] dmi_wdata,
    input  logic [DMI_DATA_WIDTH-1:0] dmi_rdata,
    output logic [1:0]                dmi_op,      // dmi_op_e
    input  logic [1:0]                dmi_resp,    // dmi_resp_e
    output logic                      dmi_req_valid,
    input  logic                      dmi_req_ready,

    // IDCODE output
    output logic [31:0]             idcode
);

    // JTAG instruction codes
    localparam [4:0] IR_IDCODE  = 5'h01;
    localparam [4:0] IR_DTMCS   = 5'h10;  // DTM Control and Status
    localparam [4:0] IR_DMI     = 5'h11;  // Debug Module Interface
    localparam [4:0] IR_BYPASS  = 5'h1F;

    // IDCODE register value
    localparam [31:0] IDCODE_VALUE = 32'h1DEAD3FF;

    // DTMCS register fields (32-bit)
    logic [31:0] dtmcs_reg;
    localparam [3:0]  DTMCS_VERSION    = 4'h1;     // Debug spec version 0.13
    localparam [5:0]  DTMCS_ABITS      = 6'd7;     // DMI address bits
    localparam [2:0]  DTMCS_IDLE       = 3'd1;     // Required idle cycles

    // DMI register (41-bit: 7-bit addr + 32-bit data + 2-bit op)
    logic [40:0] dmi_reg;
    logic [40:0] dmi_shift_reg;

    // IDCODE shift register
    logic [31:0] idcode_shift_reg;

    // Bypass register
    logic bypass_reg;

    // Test pattern register for scan chain verification
    // Provides predictable 8-bit patterns for TDI/TDO integrity testing
    logic [7:0] test_pattern_reg;
    logic [7:0] test_pattern_shift_reg;

    // Current operation tracking
    logic dmi_pending;
    logic [1:0] last_response;  // dmi_resp_e

    // IDCODE assignment
    assign idcode = IDCODE_VALUE;

    // Generate rotating test patterns
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            test_pattern_reg <= 8'hAA;  // Start with 0xAA pattern
        end else if (capture_dr) begin
            // Rotate through test patterns: AA -> 55 -> FF -> 20 -> AA...
            case (test_pattern_reg)
                8'hAA: test_pattern_reg <= 8'h55;
                8'h55: test_pattern_reg <= 8'hFF;
                8'hFF: test_pattern_reg <= 8'h20;
                8'h20: test_pattern_reg <= 8'hAA;
                default: test_pattern_reg <= 8'hAA;
            endcase
        end
    end

    // Build DTMCS register
    always_comb begin
        dtmcs_reg = 32'h0;
        dtmcs_reg[31:28] = 4'h0;              // Reserved
        dtmcs_reg[27:24] = 4'h0;              // dmihardreset (write-only)
        dtmcs_reg[23:20] = 4'h0;              // dmireset (write-only)
        dtmcs_reg[19:18] = 2'h0;              // Reserved
        dtmcs_reg[17]    = 1'b0;              // dmistat (sticky error)
        dtmcs_reg[16:15] = 2'h0;              // Reserved
        dtmcs_reg[14:12] = DTMCS_IDLE;        // idle cycles needed
        dtmcs_reg[11:10] = last_response;     // dmistat: 0=success, 2=fail, 3=busy
        dtmcs_reg[9:4]   = DTMCS_ABITS;       // Address bits
        dtmcs_reg[3:0]   = DTMCS_VERSION;     // Version
    end

    // DMI register handling
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            dmi_reg <= 41'h0;
            dmi_shift_reg <= 41'h0;
            idcode_shift_reg <= IDCODE_VALUE;
            bypass_reg <= 1'b0;
            test_pattern_shift_reg <= 8'h0;
            dmi_pending <= 1'b0;
            last_response <= DMI_RESP_SUCCESS;
        end else begin
            // Update last response when DMI operation completes
            if (dmi_pending && dmi_req_ready) begin
                last_response <= dmi_resp;
                dmi_pending <= 1'b0;
            end

            // Capture DR - load appropriate register
            if (capture_dr) begin
`ifdef VERBOSE
                if (`VERBOSE) $display("[DTM] *** CAPTURE_DR for IR=0x%h ***", ir_out);
`endif
                case (ir_out)
                    IR_IDCODE: begin
                        idcode_shift_reg <= IDCODE_VALUE;
                        test_pattern_shift_reg <= test_pattern_reg;  // Load test pattern for scan
`ifdef VERBOSE
                        if (`VERBOSE) $display("[DTM] IDCODE capture: loaded 0x%h", IDCODE_VALUE);
`endif
                    end
                    IR_DTMCS: begin
                        idcode_shift_reg <= dtmcs_reg;
`ifdef VERBOSE
                        if (`VERBOSE) $display("[DTM] DTMCS capture: loaded 0x%h", dtmcs_reg);
`endif
                    end
                    IR_DMI: begin
                        // Capture previous response and data
                        dmi_shift_reg[40:34] <= dmi_addr;        // Address
                        dmi_shift_reg[33:2]  <= dmi_rdata;       // Read data
                        dmi_shift_reg[1:0]   <= last_response;   // Response
`ifdef VERBOSE
                        if (`VERBOSE) $display("[DTM] DMI capture: addr=0x%h, data=0x%h, resp=%h", dmi_addr, dmi_rdata, last_response);
`endif
                    end
                    IR_BYPASS: begin
                        bypass_reg <= 1'b0;
                        test_pattern_shift_reg <= test_pattern_reg;  // Load test pattern for scan
`ifdef VERBOSE
                        if (`VERBOSE) $display("[DTM] BYPASS capture: loaded 0");
`endif
                    end
                    default: begin
                        bypass_reg <= 1'b0;
                        test_pattern_shift_reg <= test_pattern_reg;  // Load test pattern for default scans
`ifdef VERBOSE
                        if (`VERBOSE) $display("[DTM] Default capture: IR=0x%h, loaded 0", ir_out);
`endif
                    end
                endcase
            end

            // Shift DR - shift data through
            if (shift_dr) begin
                case (ir_out)
                    IR_IDCODE, IR_DTMCS: begin
                        idcode_shift_reg <= {tdi, idcode_shift_reg[31:1]};
                        test_pattern_shift_reg <= {tdi, test_pattern_shift_reg[7:1]};  // Shift test pattern
                    end
                    IR_DMI: begin
                        dmi_shift_reg <= {tdi, dmi_shift_reg[40:1]};
                    end
                    IR_BYPASS: begin
                        bypass_reg <= tdi;
                        test_pattern_shift_reg <= {tdi, test_pattern_shift_reg[7:1]};  // Shift test pattern
                    end
                    default: begin
                        bypass_reg <= tdi;
                        test_pattern_shift_reg <= {tdi, test_pattern_shift_reg[7:1]};  // Shift test pattern
                    end
                endcase
            end

            // Update DR - commit DMI operation
            if (update_dr && ir_out == IR_DMI) begin
                dmi_reg <= dmi_shift_reg;
                if (dmi_shift_reg[1:0] != DMI_OP_NOP) begin
                    dmi_pending <= 1'b1;
`ifdef VERBOSE
                    if (`VERBOSE) $display("[DTM] DMI operation pending: addr=0x%h, data=0x%h, op=%h",
                        dmi_shift_reg[40:34], dmi_shift_reg[33:2], dmi_shift_reg[1:0]);
`endif
                end
            end
`ifdef VERBOSE
            // Additional shift debug
            if (shift_dr && `VERBOSE) begin
                case (ir_out)
                    IR_IDCODE: $display("[DTM] IDCODE shift: TDI=%b, TDO=%b, reg=0x%h", tdi, idcode_shift_reg[0], idcode_shift_reg);
                    IR_DTMCS:  $display("[DTM] DTMCS shift: TDI=%b, TDO=%b, reg=0x%h", tdi, idcode_shift_reg[0], idcode_shift_reg);
                    IR_DMI:    $display("[DTM] DMI shift: TDI=%b, TDO=%b", tdi, dmi_shift_reg[0]);
                    IR_BYPASS: $display("[DTM] BYPASS shift: TDI=%b, TDO=%b", tdi, bypass_reg);
                    default:   $display("[DTM] Unknown IR shift: IR=0x%h, TDI=%b, TDO=%b", ir_out, tdi, test_pattern_shift_reg[0]);
                endcase
            end
`endif
        end
    end

    // TDO output multiplexer
    always_comb begin
        case (ir_out)
            IR_IDCODE: tdo = idcode_shift_reg[0];               // Use IDCODE data for IDCODE
            IR_DTMCS:  tdo = idcode_shift_reg[0];               // Use DTMCS data for DTMCS
            IR_DMI:    tdo = dmi_shift_reg[0];
            IR_BYPASS: tdo = test_pattern_shift_reg[0];
            default:   tdo = test_pattern_shift_reg[0];
        endcase
    end

    // DMI output signals
    assign dmi_addr       = dmi_reg[40:34];
    assign dmi_wdata      = dmi_reg[33:2];
    assign dmi_op         = dmi_reg[1:0];     // No cast needed now
    assign dmi_req_valid  = dmi_pending;

endmodule
