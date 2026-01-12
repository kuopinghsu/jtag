/**
 * JTAG Instruction Register (IR)
 * Stores and shifts JTAG instructions
 */

module jtag_instruction_register (
    input  logic       clk,
    input  logic       rst_n,
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

    // Shift logic
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            ir_shift_reg <= DEFAULT_IR;
            ir_latch     <= DEFAULT_IR;
        end else if (capture_ir) begin
            ir_shift_reg <= 5'b00101;      // IR capture: LSBs = 2'b01
        end else if (shift_ir) begin
            ir_shift_reg <= {tdi, ir_shift_reg[4:1]};
        end else if (update_ir) begin
            ir_latch <= ir_shift_reg;
        end
    end

    assign tdo = ir_shift_reg[0];
    // During IR shift/capture, use the shift register; after update, use the latch
    assign ir_out = ir_latch;

endmodule
