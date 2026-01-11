/**
 * System Top Module
 * Integrates JTAG interface with RISC-V Debug Module
 * Example system for testing DMI connectivity
 */

import jtag_dmi_pkg::*;

module system_top (
    input  logic        clk,
    input  logic        rst_n,
    
    // 4 Shared Physical I/O Pins (JTAG 4-wire / cJTAG 2-wire)
    input  logic        jtag_pin0_i,
    input  logic        jtag_pin1_i,
    output logic        jtag_pin1_o,
    output logic        jtag_pin1_oen,
    input  logic        jtag_pin2_i,
    output logic        jtag_pin3_o,
    output logic        jtag_pin3_oen,
    input  logic        jtag_trst_n_i,
    
    // Mode control
    input  logic        mode_select,
    
    // Debug outputs for monitoring
    output logic [31:0] idcode,
    output logic        debug_req,
    output logic        hart_halted,
    output logic        active_mode
);

    // DMI interface signals
    logic [DMI_ADDR_WIDTH-1:0] dmi_addr;
    logic [DMI_DATA_WIDTH-1:0] dmi_wdata;
    logic [DMI_DATA_WIDTH-1:0] dmi_rdata;
    logic [1:0]                dmi_op;      // dmi_op_e
    logic [1:0]                dmi_resp;    // dmi_resp_e
    logic                      dmi_req_valid;
    logic                      dmi_req_ready;
    
    // Hart interface signals
    logic [0:0]  hart_reset_req;
    logic [0:0]  hart_halt_req;
    logic [0:0]  hart_resume_req;
    logic [0:0]  hart_halted_bus;
    logic [0:0]  hart_running_bus;
    logic [0:0]  hart_unavailable;
    logic [0:0]  hart_havereset;
    
    // Hart debug interface (GPR/CSR access)
    logic [4:0]  hart_gpr_addr;
    logic [31:0] hart_gpr_wdata;
    logic [31:0] hart_gpr_rdata;
    logic        hart_gpr_we;
    logic [11:0] hart_csr_addr;
    logic [31:0] hart_csr_wdata;
    logic [31:0] hart_csr_rdata;
    logic        hart_csr_we;
    
    // Program buffer execution
    logic [31:0] progbuf_insn;
    logic        progbuf_insn_valid;
    logic        progbuf_insn_done;
    logic        progbuf_exception;
    
    // System bus interface signals
    logic [63:0] sb_address;
    logic [63:0] sb_wdata;
    logic [63:0] sb_rdata;
    logic [2:0]  sb_size;
    logic        sb_read_req;
    logic        sb_write_req;
    logic        sb_ready;
    logic        sb_error;
    
    // Convert single hart to bus
    assign hart_halted = hart_halted_bus[0];
    logic hart_running;
    assign hart_running = hart_running_bus[0];

    // ========================================
    // JTAG Top Module
    // ========================================
    jtag_top jtag (
        .clk              (clk),
        .rst_n            (rst_n),
        .jtag_pin0_i      (jtag_pin0_i),
        .jtag_pin1_i      (jtag_pin1_i),
        .jtag_pin1_o      (jtag_pin1_o),
        .jtag_pin1_oen    (jtag_pin1_oen),
        .jtag_pin2_i      (jtag_pin2_i),
        .jtag_pin3_o      (jtag_pin3_o),
        .jtag_pin3_oen    (jtag_pin3_oen),
        .jtag_trst_n_i    (jtag_trst_n_i),
        .mode_select      (mode_select),
        .dmi_addr         (dmi_addr),
        .dmi_wdata        (dmi_wdata),
        .dmi_rdata        (dmi_rdata),
        .dmi_op           (dmi_op),
        .dmi_resp         (dmi_resp),
        .dmi_req_valid    (dmi_req_valid),
        .dmi_req_ready    (dmi_req_ready),
        .idcode           (idcode),
        .active_mode      (active_mode)
    );

    // ========================================
    // RISC-V Debug Module
    // ========================================
    riscv_debug_module #(
        .NUM_HARTS(1),
        .PROGBUF_SIZE(16),
        .DATA_COUNT(12),
        .SUPPORT_IMPEBREAK(1)
    ) debug_module (
        .clk              (clk),
        .rst_n            (rst_n),
        .dmi_addr         (dmi_addr),
        .dmi_wdata        (dmi_wdata),
        .dmi_rdata        (dmi_rdata),
        .dmi_op           (dmi_op),
        .dmi_resp         (dmi_resp),
        .dmi_req_valid    (dmi_req_valid),
        .dmi_req_ready    (dmi_req_ready),
        .hart_reset_req   (hart_reset_req),
        .hart_halt_req    (hart_halt_req),
        .hart_resume_req  (hart_resume_req),
        .hart_halted      (hart_halted_bus),
        .hart_running     (hart_running_bus),
        .hart_unavailable (hart_unavailable),
        .hart_havereset   (hart_havereset),
        .hart_gpr_addr    (hart_gpr_addr),
        .hart_gpr_wdata   (hart_gpr_wdata),
        .hart_gpr_rdata   (hart_gpr_rdata),
        .hart_gpr_we      (hart_gpr_we),
        .hart_csr_addr    (hart_csr_addr),
        .hart_csr_wdata   (hart_csr_wdata),
        .hart_csr_rdata   (hart_csr_rdata),
        .hart_csr_we      (hart_csr_we),
        .progbuf_insn     (progbuf_insn),
        .progbuf_insn_valid (progbuf_insn_valid),
        .progbuf_insn_done  (progbuf_insn_done),
        .progbuf_exception  (progbuf_exception),
        .sb_address       (sb_address),
        .sb_wdata         (sb_wdata),
        .sb_rdata         (sb_rdata),
        .sb_size          (sb_size),
        .sb_read_req      (sb_read_req),
        .sb_write_req     (sb_write_req),
        .sb_ready         (sb_ready),
        .sb_error         (sb_error),
        .debug_req        (debug_req)
    );

    // ========================================
    // Simple Hart Model (for testing)
    // ========================================
    // Simulates a basic RISC-V hart responding to debug requests
    logic hart_state;  // 0=running, 1=halted
    logic [31:0] hart_gprs [32];  // General purpose registers
    logic [31:0] hart_csrs [4096];  // CSR space
    logic [15:0] progbuf_exec_count;
    
    assign hart_halted_bus[0] = hart_state;
    assign hart_running_bus[0] = !hart_state;
    assign hart_unavailable[0] = 1'b0;  // Hart is always available
    assign hart_havereset[0] = 1'b0;    // No recent reset
    
    // GPR/CSR access
    assign hart_gpr_rdata = hart_gprs[hart_gpr_addr];
    assign hart_csr_rdata = hart_csrs[hart_csr_addr];
    
    // Program buffer execution (simple model - executes in 1 cycle)
    assign progbuf_insn_done = progbuf_insn_valid;
    assign progbuf_exception = 1'b0;  // No exceptions in simple model
    
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            hart_state <= 1'b0;  // Start running
            progbuf_exec_count <= 16'h0;
            
            // Initialize GPRs to zero
            for (int i = 0; i < 32; i++) begin
                hart_gprs[i] <= 32'h0;
            end
            
            // Initialize some test CSRs
            hart_csrs[12'h300] <= 32'h00001800;  // mstatus
            hart_csrs[12'h301] <= 32'h40001104;  // misa (RV32I)
            hart_csrs[12'hF11] <= 32'h00000000;  // mvendorid
            hart_csrs[12'hF12] <= 32'hDEAD0001;  // marchid
            hart_csrs[12'hF13] <= 32'h00000001;  // mimpid
            hart_csrs[12'hF14] <= 32'h00000000;  // mhartid
            
        end else begin
            // Hart state control
            if (hart_reset_req[0]) begin
                hart_state <= 1'b0;
            end else if (hart_halt_req[0]) begin
                hart_state <= 1'b1;
            end else if (hart_resume_req[0]) begin
                hart_state <= 1'b0;
            end
            
            // GPR write
            if (hart_gpr_we && hart_state) begin
                if (hart_gpr_addr != 5'h0) begin  // x0 is hardwired to 0
                    hart_gprs[hart_gpr_addr] <= hart_gpr_wdata;
                end
            end
            
            // CSR write
            if (hart_csr_we && hart_state) begin
                hart_csrs[hart_csr_addr] <= hart_csr_wdata;
            end
            
            // Program buffer execution tracking
            if (progbuf_insn_valid) begin
                progbuf_exec_count <= progbuf_exec_count + 1;
            end
        end
    end

    // ========================================
    // Simple System Bus Model (for testing)
    // ========================================
    // Simulates memory-mapped peripheral access
    logic [63:0] memory [0:255];
    logic        sb_busy;
    
    assign sb_ready = !sb_busy;
    assign sb_error = 1'b0;  // No errors in this simple model
    
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            sb_rdata <= 64'h0;
            sb_busy <= 1'b0;
            // Initialize some test data
            memory[0] <= 64'h00000000_DEADBEEF;
            memory[1] <= 64'h00000000_CAFEBABE;
            memory[2] <= 64'h12345678_9ABCDEF0;
        end else begin
            if (sb_read_req && !sb_busy) begin
                sb_busy <= 1'b1;
                case (sb_size)
                    3'h0: sb_rdata <= {56'h0, memory[sb_address[7:0]][7:0]};   // Byte
                    3'h1: sb_rdata <= {48'h0, memory[sb_address[7:0]][15:0]};  // Half
                    3'h2: sb_rdata <= {32'h0, memory[sb_address[7:0]][31:0]};  // Word
                    3'h3: sb_rdata <= memory[sb_address[7:0]];                 // Double
                    default: sb_rdata <= memory[sb_address[7:0]];
                endcase
            end else if (sb_write_req && !sb_busy) begin
                sb_busy <= 1'b1;
                case (sb_size)
                    3'h0: memory[sb_address[7:0]][7:0]   <= sb_wdata[7:0];    // Byte
                    3'h1: memory[sb_address[7:0]][15:0]  <= sb_wdata[15:0];   // Half
                    3'h2: memory[sb_address[7:0]][31:0]  <= sb_wdata[31:0];   // Word
                    3'h3: memory[sb_address[7:0]]        <= sb_wdata;          // Double
                    default: memory[sb_address[7:0]]     <= sb_wdata;
                endcase
            end else if (sb_busy) begin
                sb_busy <= 1'b0;  // Complete in next cycle
            end
        end
    end

endmodule
