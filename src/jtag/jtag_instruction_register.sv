/**
 * JTAG Instruction Register (IR)
 * Stores and shifts JTAG instructions
 */

module jtag_instruction_register (
    input  logic       clk,
    input  logic       rst_n,
    input  logic       tap_reset,      // TAP reset from TAP controller
    input  logic       tdi,
    output logic       tdo,
    input  logic       shift_ir,
    input  logic       capture_ir,
    input  logic       update_ir,

    output logic [4:0] ir_out     // Current instruction (5-bit IR)
);

    localparam [4:0] DEFAULT_IR = 5'h01;  // IDCODE

    logic [4:0] ir_shift_reg;
    logic [4:0] ir_latch;

    // Shift logic with TAP reset support
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n || tap_reset) begin
            ir_shift_reg <= DEFAULT_IR;
            ir_latch     <= DEFAULT_IR;
        end else if (capture_ir) begin
            ir_shift_reg <= 5'b00001;      // IR capture: LSBs = 2'b01 (IEEE 1149.1 compliant)
        end else if (shift_ir) begin
            ir_shift_reg <= {tdi, ir_shift_reg[4:1]};
        end else if (update_ir) begin
            ir_latch <= ir_shift_reg;
        end
    end

    assign tdo = ir_shift_reg[0];
    // During IR shift/capture, use the shift register; after update, use the latch
    assign ir_out = ir_latch;

`ifdef VERBOSE
    // Enhanced debug output with VERBOSE control
    always @(posedge clk) begin
        if (`VERBOSE) begin
            #1; // Small delay to see updated values after clock edge
            if (capture_ir) begin
                $display("[IR] *** CAPTURE_IR EXECUTED ***");
                $display("[IR]   Before: ir_shift_reg = 5'b%05b (0x%h)", ir_shift_reg, ir_shift_reg);
                $display("[IR]   Pattern: 5'b00001 (IEEE 1149.1 compliant)");
                $display("[IR]   TDO out: %b (LSB of captured pattern)", tdo);
                $display("[IR]   Reset state: rst_n=%b", rst_n);
            end
            if (shift_ir) begin
                $display("[IR] *** SHIFT_IR EXECUTED ***");
                $display("[IR]   Before: ir_shift_reg = 5'b%05b", ir_shift_reg);
                $display("[IR]   After:  ir_shift_reg = 5'b%05b (shifted)", {tdi, ir_shift_reg[4:1]});
                $display("[IR]   TDI in: %b, TDO out: %b", tdi, tdo);
            end
            if (update_ir) begin
                $display("[IR] *** UPDATE_IR EXECUTED ***");
                $display("[IR]   New instruction: ir_out = 5'b%05b (0x%h)", ir_latch, ir_latch);
                case (ir_latch)
                    5'h01: $display("[IR]   -> IDCODE instruction");
                    5'h10: $display("[IR]   -> DTMCS instruction");
                    5'h11: $display("[IR]   -> DMI instruction");
                    5'h1F: $display("[IR]   -> BYPASS instruction");
                    default: $display("[IR]   -> Unknown instruction: 0x%h", ir_latch);
                endcase
            end
        end
    end
`else
    // Minimal debug output when VERBOSE is disabled
    always @(negedge clk) begin
        if (capture_ir) begin
            $display("[IR] Capture: ir_shift_reg = 5'b%05b", ir_shift_reg);
        end
    end
`endif

endmodule
