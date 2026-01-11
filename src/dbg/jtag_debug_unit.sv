/**
 * JTAG Debug Unit
 * Implements basic debug commands including IDCODE reading
 */

module jtag_debug_unit (
    input  logic       clk,
    input  logic       rst_n,
    
    // JTAG interface
    input  logic       tdi,
    output logic       tdo,
    input  logic       shift_dr,
    input  logic       update_dr,
    input  logic       capture_dr,
    input  logic [7:0] ir_out,      // Instruction register output
    
    // Debug interface
    output logic [31:0] idcode,
    output logic [31:0] status_reg,
    output logic        debug_req
);

    // Instruction codes
    localparam [7:0] IR_IDCODE  = 8'h01;
    localparam [7:0] IR_BYPASS  = 8'hFF;
    localparam [7:0] IR_DEBUG   = 8'h08;
    
    // IDCODE register - Device identification
    // Format: [31:28]=Version, [27:12]=PartNumber, [11:1]=Manufacturer, [0]=1
    localparam [31:0] IDCODE_VALUE = {
        4'h1,           // Version
        16'hDEAD,       // Part number (DEAD - debug unit example)
        11'h1FF,        // JEDEC manufacturer ID (ARM example)
        1'b1
    };

    logic [31:0] dr_shift_reg;   // Data register for shifting
    logic [31:0] dr_latch;       // Data register latch for update
    logic [7:0]  bypass_reg;
    logic        bypass_tdo;

    // Shift register and bypass logic
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            dr_shift_reg <= 32'h0;
            bypass_reg   <= 1'b0;
        end else if (shift_dr) begin
            // Shift data register
            dr_shift_reg <= {tdi, dr_shift_reg[31:1]};
            bypass_reg   <= tdi;
        end else if (capture_dr) begin
            // Capture phase - load data based on current instruction
            case (ir_out)
                IR_IDCODE: dr_shift_reg <= IDCODE_VALUE;
                IR_DEBUG:  dr_shift_reg <= {status_reg[30:0], 1'b0};
                IR_BYPASS: dr_shift_reg <= 32'h0;
                default:   dr_shift_reg <= 32'h0;
            endcase
        end
    end

    // TDO output
    always_comb begin
        case (ir_out)
            IR_IDCODE: tdo = dr_shift_reg[0];
            IR_BYPASS: tdo = bypass_reg;
            IR_DEBUG:  tdo = dr_shift_reg[0];
            default:   tdo = 1'b0;
        endcase
    end

    // Update phase - latch data when exiting shift state
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            dr_latch <= 32'h0;
        end else if (update_dr) begin
            dr_latch <= dr_shift_reg;
        end
    end

    // Output registers
    assign idcode = IDCODE_VALUE;
    assign status_reg = {31'h0, debug_req};
    
    // Debug request - set if debug command received
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            debug_req <= 1'b0;
        end else if (update_dr && ir_out == IR_DEBUG && dr_latch[0]) begin
            debug_req <= 1'b1;
        end else if (!dr_latch[0]) begin
            debug_req <= 1'b0;
        end
    end

endmodule
