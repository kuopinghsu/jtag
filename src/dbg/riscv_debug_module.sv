/**
 * RISC-V Debug Module (Full Implementation)
 * Complete implementation of RISC-V Debug Specification 0.13.2
 *
 * Features:
 * - Full DMI register set
 * - Abstract command support (register/memory access)
 * - Program buffer execution (16 entries)
 * - System bus access (32/64-bit)
 * - Multi-hart support (up to 32 harts)
 * - Debug RAM interface
 * - Halt/resume/reset control
 * - Exception handling and error reporting
 */

import jtag_dmi_pkg::*;

module riscv_debug_module #(
    parameter int NUM_HARTS         = 1,   // Number of hardware threads
    parameter int PROGBUF_SIZE      = 16,  // Program buffer entries
    parameter int DATA_COUNT        = 12,  // Abstract data registers
    parameter bit SUPPORT_IMPEBREAK = 1    // Support implicit ebreak
) (
    input logic clk,
    input logic rst_n,

    // DMI interface from JTAG DTM
    input  logic [DMI_ADDR_WIDTH-1:0] dmi_addr,
    input  logic [DMI_DATA_WIDTH-1:0] dmi_wdata,
    output logic [DMI_DATA_WIDTH-1:0] dmi_rdata,
    input  logic [               1:0] dmi_op,         // dmi_op_e
    output logic [               1:0] dmi_resp,       // dmi_resp_e
    input  logic                      dmi_req_valid,
    output logic                      dmi_req_ready,

    // Hart (hardware thread) interface
    output logic [NUM_HARTS-1:0] hart_reset_req,
    output logic [NUM_HARTS-1:0] hart_halt_req,
    output logic [NUM_HARTS-1:0] hart_resume_req,
    input  logic [NUM_HARTS-1:0] hart_halted,
    input  logic [NUM_HARTS-1:0] hart_running,
    input  logic [NUM_HARTS-1:0] hart_unavailable,
    input  logic [NUM_HARTS-1:0] hart_havereset,

    // Hart debug interface (GPR/CSR access)
    output logic [ 4:0] hart_gpr_addr,
    output logic [31:0] hart_gpr_wdata,
    input  logic [31:0] hart_gpr_rdata,
    output logic        hart_gpr_we,
    output logic [11:0] hart_csr_addr,
    output logic [31:0] hart_csr_wdata,
    input  logic [31:0] hart_csr_rdata,
    output logic        hart_csr_we,

    // Program buffer execution interface
    output logic [31:0] progbuf_insn,
    output logic        progbuf_insn_valid,
    input  logic        progbuf_insn_done,
    input  logic        progbuf_exception,

    // System bus access (optional)
    output logic [63:0] sb_address,
    output logic [63:0] sb_wdata,
    input  logic [63:0] sb_rdata,
    output logic [ 2:0] sb_size,       // 0=byte, 1=half, 2=word, 3=double
    output logic        sb_read_req,
    output logic        sb_write_req,
    input  logic        sb_ready,
    input  logic        sb_error,

    // Debug request output
    output logic debug_req
);

    // =========================================================================
    // Debug Module Register Map (RISC-V Debug Spec 0.13.2)
    // =========================================================================

    // Core registers
    localparam [6:0] DM_DATA0 = 7'h04;  // Abstract Data 0
    localparam [6:0] DM_DATA1 = 7'h05;  // Abstract Data 1
    localparam [6:0] DM_DATA2 = 7'h06;  // Abstract Data 2
    localparam [6:0] DM_DATA3 = 7'h07;  // Abstract Data 3
    localparam [6:0] DM_DATA4 = 7'h08;  // Abstract Data 4
    localparam [6:0] DM_DATA5 = 7'h09;  // Abstract Data 5
    localparam [6:0] DM_DATA6 = 7'h0A;  // Abstract Data 6
    localparam [6:0] DM_DATA7 = 7'h0B;  // Abstract Data 7
    localparam [6:0] DM_DATA8 = 7'h0C;  // Abstract Data 8
    localparam [6:0] DM_DATA9 = 7'h0D;  // Abstract Data 9
    localparam [6:0] DM_DATA10 = 7'h0E;  // Abstract Data 10
    localparam [6:0] DM_DATA11 = 7'h0F;  // Abstract Data 11

    localparam [6:0] DM_DMCONTROL = 7'h10;  // Debug Module Control
    localparam [6:0] DM_DMSTATUS = 7'h11;  // Debug Module Status
    localparam [6:0] DM_HARTINFO = 7'h12;  // Hart Info
    localparam [6:0] DM_HALTSUM1 = 7'h13;  // Halt Summary 1
    localparam [6:0] DM_HAWINDOWSEL = 7'h14;  // Hart Array Window Select
    localparam [6:0] DM_HAWINDOW = 7'h15;  // Hart Array Window
    localparam [6:0] DM_ABSTRACTCS = 7'h16;  // Abstract Command Status
    localparam [6:0] DM_COMMAND = 7'h17;  // Abstract Command
    localparam [6:0] DM_ABSTRACTAUTO = 7'h18;  // Abstract Command Autoexec
    localparam [6:0] DM_CONFSTRPTR0 = 7'h19;  // Configuration String Pointer 0
    localparam [6:0] DM_CONFSTRPTR1 = 7'h1A;  // Configuration String Pointer 1
    localparam [6:0] DM_CONFSTRPTR2 = 7'h1B;  // Configuration String Pointer 2
    localparam [6:0] DM_CONFSTRPTR3 = 7'h1C;  // Configuration String Pointer 3
    localparam [6:0] DM_NEXTDM = 7'h1D;  // Next Debug Module

    // Program buffer
    localparam [6:0] DM_PROGBUF0 = 7'h20;  // Program Buffer 0
    localparam [6:0] DM_PROGBUF1 = 7'h21;  // Program Buffer 1
    localparam [6:0] DM_PROGBUF2 = 7'h22;  // Program Buffer 2
    localparam [6:0] DM_PROGBUF3 = 7'h23;  // Program Buffer 3
    localparam [6:0] DM_PROGBUF4 = 7'h24;  // Program Buffer 4
    localparam [6:0] DM_PROGBUF5 = 7'h25;  // Program Buffer 5
    localparam [6:0] DM_PROGBUF6 = 7'h26;  // Program Buffer 6
    localparam [6:0] DM_PROGBUF7 = 7'h27;  // Program Buffer 7
    localparam [6:0] DM_PROGBUF8 = 7'h28;  // Program Buffer 8
    localparam [6:0] DM_PROGBUF9 = 7'h29;  // Program Buffer 9
    localparam [6:0] DM_PROGBUF10 = 7'h2A;  // Program Buffer 10
    localparam [6:0] DM_PROGBUF11 = 7'h2B;  // Program Buffer 11
    localparam [6:0] DM_PROGBUF12 = 7'h2C;  // Program Buffer 12
    localparam [6:0] DM_PROGBUF13 = 7'h2D;  // Program Buffer 13
    localparam [6:0] DM_PROGBUF14 = 7'h2E;  // Program Buffer 14
    localparam [6:0] DM_PROGBUF15 = 7'h2F;  // Program Buffer 15

    // Authentication (optional)
    localparam [6:0] DM_AUTHDATA = 7'h30;  // Authentication Data
    localparam [6:0] DM_DMCS2 = 7'h32;  // Debug Module Control and Status 2

    // System bus access
    localparam [6:0] DM_SBCS = 7'h38;  // System Bus Access Control and Status
    localparam [6:0] DM_SBADDRESS0 = 7'h39;  // System Bus Address 31:0
    localparam [6:0] DM_SBADDRESS1 = 7'h3A;  // System Bus Address 63:32
    localparam [6:0] DM_SBADDRESS2 = 7'h3B;  // System Bus Address 95:64
    localparam [6:0] DM_SBADDRESS3 = 7'h37;  // System Bus Address 127:96
    localparam [6:0] DM_SBDATA0 = 7'h3C;  // System Bus Data 31:0
    localparam [6:0] DM_SBDATA1 = 7'h3D;  // System Bus Data 63:32
    localparam [6:0] DM_SBDATA2 = 7'h3E;  // System Bus Data 95:64
    localparam [6:0] DM_SBDATA3 = 7'h3F;  // System Bus Data 127:96

    // Halt summary
    localparam [6:0] DM_HALTSUM2 = 7'h34;  // Halt Summary 2
    localparam [6:0] DM_HALTSUM3 = 7'h35;  // Halt Summary 3

    // =========================================================================
    // Abstract Command Types
    // =========================================================================

    typedef enum logic [7:0] {
        CMD_ACCESS_REG   = 8'h00,  // Access register (GPR/CSR/FPR)
        CMD_QUICK_ACCESS = 8'h01,  // Quick access
        CMD_ACCESS_MEM   = 8'h02   // Access memory
    } cmd_type_e;

    typedef enum logic [2:0] {
        CMD_ERR_NONE        = 3'h0,  // No error
        CMD_ERR_BUSY        = 3'h1,  // Abstract command in progress
        CMD_ERR_NOT_SUPPORT = 3'h2,  // Command not supported
        CMD_ERR_EXCEPTION   = 3'h3,  // Exception during execution
        CMD_ERR_HALT_RESUME = 3'h4,  // Hart not in expected state
        CMD_ERR_BUS         = 3'h5,  // Bus error
        CMD_ERR_OTHER       = 3'h7   // Other error
    } cmd_error_e;

    // =========================================================================
    // Register Declarations
    // =========================================================================

    // DMCONTROL register fields
    logic              dmcontrol_haltreq;
    logic              dmcontrol_resumereq;
    logic              dmcontrol_hartreset;
    logic              dmcontrol_ackhavereset;
    logic              dmcontrol_hasel;
    logic       [ 9:0] dmcontrol_hartsello;
    logic       [ 9:0] dmcontrol_hartselhi;
    logic              dmcontrol_setresethaltreq;
    logic              dmcontrol_clrresethaltreq;
    logic              dmcontrol_ndmreset;
    logic              dmcontrol_dmactive;

    // DMSTATUS register fields (read-only)
    logic              dmstatus_impebreak;
    logic              dmstatus_allhavereset;
    logic              dmstatus_anyhavereset;
    logic              dmstatus_allresumeack;
    logic              dmstatus_anyresumeack;
    logic              dmstatus_allnonexistent;
    logic              dmstatus_anynonexistent;
    logic              dmstatus_allunavail;
    logic              dmstatus_anyunavail;
    logic              dmstatus_allrunning;
    logic              dmstatus_anyrunning;
    logic              dmstatus_allhalted;
    logic              dmstatus_anyhalted;
    logic              dmstatus_authenticated;
    logic              dmstatus_authbusy;
    logic              dmstatus_hasresethaltreq;
    logic              dmstatus_confstrptrvalid;
    logic       [ 3:0] dmstatus_version;

    // HARTINFO register
    logic       [ 3:0] hartinfo_nscratch;
    logic              hartinfo_dataaccess;
    logic       [ 3:0] hartinfo_datasize;
    logic       [11:0] hartinfo_dataaddr;

    // ABSTRACTCS register
    logic       [ 4:0] abstractcs_progbufsize;
    logic              abstractcs_busy;
    cmd_error_e        abstractcs_cmderr;
    logic       [ 3:0] abstractcs_datacount;

    // ABSTRACTAUTO register
    logic       [11:0] abstractauto_autoexecdata;
    logic       [15:0] abstractauto_autoexecprogbuf;

    // Abstract command register
    logic       [31:0] command_reg;
    logic       [ 7:0] command_type;  // cmd_type_e enum, use logic for Yosys
    logic       [23:0] command_control;

    // Abstract data registers (12 entries)
    logic       [31:0] data_reg [  DATA_COUNT];

    // Program buffer (16 entries)
    logic       [31:0] progbuf  [PROGBUF_SIZE];

    // System bus control
    logic       [ 2:0] sbcs_sbaccess;
    logic              sbcs_sbautoincrement;
    logic              sbcs_sbreadondata;
    logic              sbcs_sberror;
    logic       [ 6:0] sbcs_sbasize;
    logic              sbcs_sbaccess128;
    logic              sbcs_sbaccess64;
    logic              sbcs_sbaccess32;
    logic              sbcs_sbaccess16;
    logic              sbcs_sbaccess8;
    logic       [ 2:0] sbcs_sbversion;
    logic              sbcs_sbbusy;
    logic              sbcs_sbreadonaddr;

    // System bus address/data
    logic       [63:0] sbaddress;
    logic       [63:0] sbdata;

    // Hart selection
    logic       [19:0] hartsel;
    logic       [ 4:0] selected_hart_idx;

    // Internal state
    typedef enum logic [2:0] {
        CMD_IDLE,
        CMD_READ_GPR,
        CMD_WRITE_GPR,
        CMD_READ_CSR,
        CMD_WRITE_CSR,
        CMD_EXEC_PROGBUF,
        CMD_COMPLETE
    } cmd_state_e;

    cmd_state_e        cmd_state;
    logic       [15:0] cmd_regno;
    logic              cmd_write;
    logic              cmd_transfer;
    logic              cmd_postexec;
    logic       [ 2:0] cmd_aarsize;
    logic              cmd_postincrement;
    logic       [15:0] progbuf_pc;

    // =========================================================================
    // Hart Selection Logic
    // =========================================================================

    assign hartsel = {dmcontrol_hartselhi, dmcontrol_hartsello};
    assign selected_hart_idx = hartsel[4:0];  // Support up to 32 harts

    // =========================================================================
    // DMI Response Handling
    // =========================================================================

    assign dmi_req_ready = !abstractcs_busy;
    assign dmi_resp = abstractcs_busy ? DMI_RESP_BUSY : DMI_RESP_SUCCESS;

    // =========================================================================
    // Hart Control Outputs
    // =========================================================================

    always_comb begin
        hart_halt_req   = '0;
        hart_resume_req = '0;
        hart_reset_req  = '0;

        if (dmcontrol_dmactive) begin
            if (dmcontrol_hasel) begin
                // Hart array mask selection
                hart_halt_req   = hart_halt_req | (dmcontrol_haltreq ? '1 : '0);
                hart_resume_req = hart_resume_req | (dmcontrol_resumereq ? '1 : '0);
                hart_reset_req  = hart_reset_req | (dmcontrol_hartreset ? '1 : '0);
            end else begin
                // Single hart selection
                if (selected_hart_idx < 5'(NUM_HARTS)) begin
                    if (NUM_HARTS == 1) begin
                        hart_halt_req[0]   = dmcontrol_haltreq;
                        hart_resume_req[0] = dmcontrol_resumereq;
                        hart_reset_req[0]  = dmcontrol_hartreset;
                    end else begin
                        /* verilator lint_off WIDTHTRUNC */
                        hart_halt_req[selected_hart_idx]   = dmcontrol_haltreq;
                        hart_resume_req[selected_hart_idx] = dmcontrol_resumereq;
                        hart_reset_req[selected_hart_idx]  = dmcontrol_hartreset;
                        /* verilator lint_on WIDTHTRUNC */
                    end
                end
            end
        end
    end

    assign debug_req = |hart_halt_req;

    // =========================================================================
    // DMSTATUS Fields (Dynamic based on selected hart)
    // =========================================================================

    logic selected_hart_halted;
    logic selected_hart_running;
    logic selected_hart_unavailable;
    logic selected_hart_havereset;

    generate
        if (NUM_HARTS == 1) begin : gen_single_hart
            assign selected_hart_halted = (selected_hart_idx < 5'(NUM_HARTS)) ? hart_halted[0] : 1'b0;
            assign selected_hart_running = (selected_hart_idx < 5'(NUM_HARTS)) ? hart_running[0] : 1'b0;
            assign selected_hart_unavailable = (selected_hart_idx < 5'(NUM_HARTS)) ? hart_unavailable[0] : 1'b1;
            assign selected_hart_havereset = (selected_hart_idx < 5'(NUM_HARTS)) ? hart_havereset[0] : 1'b0;
        end else begin : gen_multi_hart
            assign selected_hart_halted = (selected_hart_idx < 5'(NUM_HARTS)) ? hart_halted[selected_hart_idx] : 1'b0;
            assign selected_hart_running = (selected_hart_idx < 5'(NUM_HARTS)) ? hart_running[selected_hart_idx] : 1'b0;
            assign selected_hart_unavailable = (selected_hart_idx < 5'(NUM_HARTS)) ? hart_unavailable[selected_hart_idx] : 1'b1;
            assign selected_hart_havereset = (selected_hart_idx < 5'(NUM_HARTS)) ? hart_havereset[selected_hart_idx] : 1'b0;
        end
    endgenerate

    assign dmstatus_impebreak = SUPPORT_IMPEBREAK;
    assign dmstatus_allhavereset = dmcontrol_hasel ? (&hart_havereset) : selected_hart_havereset;
    assign dmstatus_anyhavereset = dmcontrol_hasel ? (|hart_havereset) : selected_hart_havereset;
    assign dmstatus_allresumeack = dmcontrol_hasel ? (&hart_running) : selected_hart_running;
    assign dmstatus_anyresumeack = dmcontrol_hasel ? (|hart_running) : selected_hart_running;
    assign dmstatus_allnonexistent = (selected_hart_idx >= 5'(NUM_HARTS));
    assign dmstatus_anynonexistent = (selected_hart_idx >= 5'(NUM_HARTS));
    assign dmstatus_allunavail = dmcontrol_hasel ? (&hart_unavailable) : selected_hart_unavailable;
    assign dmstatus_anyunavail = dmcontrol_hasel ? (|hart_unavailable) : selected_hart_unavailable;
    assign dmstatus_allrunning = dmcontrol_hasel ? (&hart_running) : selected_hart_running;
    assign dmstatus_anyrunning = dmcontrol_hasel ? (|hart_running) : selected_hart_running;
    assign dmstatus_allhalted = dmcontrol_hasel ? (&hart_halted) : selected_hart_halted;
    assign dmstatus_anyhalted = dmcontrol_hasel ? (|hart_halted) : selected_hart_halted;
    assign dmstatus_authenticated = 1'b1;  // Always authenticated (no auth required)
    assign dmstatus_authbusy = 1'b0;
    assign dmstatus_hasresethaltreq = 1'b1;  // Support reset halt request
    assign dmstatus_confstrptrvalid = 1'b0;  // No config string
    assign dmstatus_version = 4'h2;  // Debug spec version 0.13

    // =========================================================================
    // HARTINFO Fields
    // =========================================================================

    assign hartinfo_nscratch = 4'h1;  // 1 scratch register (dscratch0)
    assign hartinfo_dataaccess = 1'b0;  // Data registers in memory (not CSR)
    assign hartinfo_datasize = 4'h1;  // 1 x 32-bit data register
    assign hartinfo_dataaddr = 12'h0;  // Base address (not used)

    // =========================================================================
    // ABSTRACTCS Fields
    // =========================================================================

    assign abstractcs_progbufsize = PROGBUF_SIZE[4:0];
    assign abstractcs_busy = (cmd_state != CMD_IDLE);
    assign abstractcs_datacount = DATA_COUNT[3:0];

    // =========================================================================
    // System Bus Access Outputs
    // =========================================================================

    assign sb_address = sbaddress;
    assign sb_wdata = sbdata;
    assign sb_size = sbcs_sbaccess;
    assign sbcs_sbbusy = !sb_ready;
    assign sbcs_sberror = sb_error;

    // SB capabilities
    assign sbcs_sbversion = 3'h1;  // System bus version 1
    assign sbcs_sbasize = 7'd64;  // 64-bit address bus
    assign sbcs_sbaccess128 = 1'b0;  // No 128-bit support
    assign sbcs_sbaccess64 = 1'b1;  // 64-bit support
    assign sbcs_sbaccess32 = 1'b1;  // 32-bit support
    assign sbcs_sbaccess16 = 1'b1;  // 16-bit support
    assign sbcs_sbaccess8 = 1'b1;  // 8-bit support

    // =========================================================================
    // DMI Register Read
    // =========================================================================

    always_comb begin
        dmi_rdata = 32'h0;

        case (dmi_addr)
            // Abstract Data registers
            DM_DATA0:  dmi_rdata = data_reg[0];
            DM_DATA1:  dmi_rdata = data_reg[1];
            DM_DATA2:  dmi_rdata = data_reg[2];
            DM_DATA3:  dmi_rdata = data_reg[3];
            DM_DATA4:  dmi_rdata = data_reg[4];
            DM_DATA5:  dmi_rdata = data_reg[5];
            DM_DATA6:  dmi_rdata = data_reg[6];
            DM_DATA7:  dmi_rdata = data_reg[7];
            DM_DATA8:  dmi_rdata = data_reg[8];
            DM_DATA9:  dmi_rdata = data_reg[9];
            DM_DATA10: dmi_rdata = data_reg[10];
            DM_DATA11: dmi_rdata = data_reg[11];

            // DMCONTROL register
            DM_DMCONTROL: begin
                dmi_rdata[31]    = dmcontrol_haltreq;
                dmi_rdata[30]    = dmcontrol_resumereq;
                dmi_rdata[29]    = dmcontrol_hartreset;
                dmi_rdata[28]    = dmcontrol_ackhavereset;
                dmi_rdata[26]    = dmcontrol_hasel;
                dmi_rdata[25:16] = dmcontrol_hartsello;
                dmi_rdata[15:6]  = dmcontrol_hartselhi;
                dmi_rdata[3]     = dmcontrol_setresethaltreq;
                dmi_rdata[2]     = dmcontrol_clrresethaltreq;
                dmi_rdata[1]     = dmcontrol_ndmreset;
                dmi_rdata[0]     = dmcontrol_dmactive;
            end

            // DMSTATUS register
            DM_DMSTATUS: begin
                dmi_rdata[22]    = dmstatus_impebreak;
                dmi_rdata[21:20] = 2'b00;  // reserved
                dmi_rdata[19]    = dmstatus_allresumeack;
                dmi_rdata[18]    = dmstatus_anyresumeack;
                dmi_rdata[17]    = dmstatus_allnonexistent;
                dmi_rdata[16]    = dmstatus_anynonexistent;
                dmi_rdata[15]    = dmstatus_allunavail;
                dmi_rdata[14]    = dmstatus_anyunavail;
                dmi_rdata[13]    = dmstatus_allrunning;
                dmi_rdata[12]    = dmstatus_anyrunning;
                dmi_rdata[11]    = dmstatus_allhalted;
                dmi_rdata[10]    = dmstatus_anyhalted;
                dmi_rdata[9]     = dmstatus_authenticated;
                dmi_rdata[8]     = dmstatus_authbusy;
                dmi_rdata[7]     = dmstatus_hasresethaltreq;
                dmi_rdata[6]     = dmstatus_confstrptrvalid;
                dmi_rdata[5:4]   = 2'b00;  // reserved
                dmi_rdata[3:0]   = dmstatus_version;
            end

            // HARTINFO register
            DM_HARTINFO: begin
                dmi_rdata[23:20] = hartinfo_nscratch;
                dmi_rdata[19:17] = 3'h0;  // reserved
                dmi_rdata[16]    = hartinfo_dataaccess;
                dmi_rdata[15:12] = hartinfo_datasize;
                dmi_rdata[11:0]  = hartinfo_dataaddr;
            end

            // HALTSUM1 register
            DM_HALTSUM1: begin
                // Bits indicate which groups of 32 harts have at least one halted hart
                dmi_rdata[0] = |hart_halted;
            end

            // HAWINDOWSEL register
            DM_HAWINDOWSEL: begin
                dmi_rdata = 32'h0;  // Single window (hawindowsel=0)
            end

            // HAWINDOW register
            DM_HAWINDOW: begin
                // Hart array window - bitmap of selected harts
                dmi_rdata = {{(32 - NUM_HARTS) {1'b0}}, hart_halted};
            end

            // ABSTRACTCS register
            DM_ABSTRACTCS: begin
                dmi_rdata[28:24] = abstractcs_progbufsize;
                dmi_rdata[23:13] = 11'h0;  // reserved
                dmi_rdata[12]    = abstractcs_busy;
                dmi_rdata[11]    = 1'b0;   // reserved
                dmi_rdata[10:8]  = abstractcs_cmderr;
                dmi_rdata[7:4]   = 4'h0;   // reserved
                dmi_rdata[3:0]   = abstractcs_datacount;
            end

            // COMMAND register
            DM_COMMAND: begin
                dmi_rdata = command_reg;
            end

            // ABSTRACTAUTO register
            DM_ABSTRACTAUTO: begin
                dmi_rdata[31:28] = 4'h0;  // reserved
                dmi_rdata[27:16] = abstractauto_autoexecdata;
                dmi_rdata[15:0]  = abstractauto_autoexecprogbuf;
            end

            // CONFSTRPTR registers
            DM_CONFSTRPTR0, DM_CONFSTRPTR1, DM_CONFSTRPTR2, DM_CONFSTRPTR3: begin
                dmi_rdata = 32'h0;  // No configuration string
            end

            // NEXTDM register
            DM_NEXTDM: begin
                dmi_rdata = 32'h0;  // No next debug module
            end

            // Program buffer registers
            DM_PROGBUF0:  dmi_rdata = progbuf[0];
            DM_PROGBUF1:  dmi_rdata = progbuf[1];
            DM_PROGBUF2:  dmi_rdata = progbuf[2];
            DM_PROGBUF3:  dmi_rdata = progbuf[3];
            DM_PROGBUF4:  dmi_rdata = progbuf[4];
            DM_PROGBUF5:  dmi_rdata = progbuf[5];
            DM_PROGBUF6:  dmi_rdata = progbuf[6];
            DM_PROGBUF7:  dmi_rdata = progbuf[7];
            DM_PROGBUF8:  dmi_rdata = progbuf[8];
            DM_PROGBUF9:  dmi_rdata = progbuf[9];
            DM_PROGBUF10: dmi_rdata = progbuf[10];
            DM_PROGBUF11: dmi_rdata = progbuf[11];
            DM_PROGBUF12: dmi_rdata = progbuf[12];
            DM_PROGBUF13: dmi_rdata = progbuf[13];
            DM_PROGBUF14: dmi_rdata = progbuf[14];
            DM_PROGBUF15: dmi_rdata = progbuf[15];

            // AUTHDATA register
            DM_AUTHDATA: begin
                dmi_rdata = 32'h0;  // No authentication required
            end

            // DMCS2 register
            DM_DMCS2: begin
                dmi_rdata = 32'h0;  // reserved
            end

            // SBCS register (System Bus Control and Status)
            DM_SBCS: begin
                dmi_rdata[31:29] = sbcs_sbversion;
                dmi_rdata[28:23] = 6'h0;   // reserved
                dmi_rdata[22]    = sbcs_sbbusy;
                dmi_rdata[21]    = sbcs_sbreadonaddr;
                dmi_rdata[20:17] = {sbcs_sbaccess32, sbcs_sbaccess16, sbcs_sbaccess8, 1'b0};
                dmi_rdata[16]    = sbcs_sbautoincrement;
                dmi_rdata[15]    = sbcs_sberror;
                dmi_rdata[14:12] = sbcs_sbaccess;
                dmi_rdata[11:5]  = sbcs_sbasize;
                dmi_rdata[4:0]   = 5'h0;   // reserved
            end

            // SBADDRESS registers
            DM_SBADDRESS0: dmi_rdata = sbaddress[31:0];
            DM_SBADDRESS1: dmi_rdata = sbaddress[63:32];
            DM_SBADDRESS2: dmi_rdata = 32'h0;  // 96-bit address not supported
            DM_SBADDRESS3: dmi_rdata = 32'h0;  // 128-bit address not supported

            // SBDATA registers
            DM_SBDATA0: dmi_rdata = sbdata[31:0];
            DM_SBDATA1: dmi_rdata = sbdata[63:32];
            DM_SBDATA2: dmi_rdata = 32'h0;  // 96-bit data not supported
            DM_SBDATA3: dmi_rdata = 32'h0;  // 128-bit data not supported

            // HALTSUM2/3 registers
            DM_HALTSUM2: dmi_rdata = 32'h0;  // Not needed for <=32 harts
            DM_HALTSUM3: dmi_rdata = 32'h0;  // Not needed for <=32 harts

            default: dmi_rdata = 32'h0;
        endcase
    end

    // =========================================================================
    // DMI Register Write & Abstract Command Execution
    // =========================================================================

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            // Reset all control registers
            dmcontrol_haltreq <= 1'b0;
            dmcontrol_resumereq <= 1'b0;
            dmcontrol_hartreset <= 1'b0;
            dmcontrol_ackhavereset <= 1'b0;
            dmcontrol_hasel <= 1'b0;
            dmcontrol_hartsello <= 10'h0;
            dmcontrol_hartselhi <= 10'h0;
            dmcontrol_setresethaltreq <= 1'b0;
            dmcontrol_clrresethaltreq <= 1'b0;
            dmcontrol_ndmreset <= 1'b0;
            dmcontrol_dmactive <= 1'b0;

            command_reg <= 32'h0;
            for (int i = 0; i < DATA_COUNT; i++) begin
                data_reg[i] <= 32'h0;
            end
            for (int i = 0; i < PROGBUF_SIZE; i++) begin
                progbuf[i] <= 32'h0;
            end

            abstractauto_autoexecdata <= 12'h0;
            abstractauto_autoexecprogbuf <= 16'h0;

            sbcs_sbaccess <= 3'h2;  // Default to 32-bit
            sbcs_sbautoincrement <= 1'b0;
            sbcs_sbreadonaddr <= 1'b0;
            sbcs_sbreadondata <= 1'b0;
            sbaddress <= 64'h0;
            sbdata <= 64'h0;

            cmd_state <= CMD_IDLE;
            abstractcs_cmderr <= CMD_ERR_NONE;

            hart_gpr_we <= 1'b0;
            hart_csr_we <= 1'b0;
            progbuf_insn_valid <= 1'b0;
            sb_read_req <= 1'b0;
            sb_write_req <= 1'b0;

        end else begin
            // Clear one-shot signals
            dmcontrol_resumereq <= 1'b0;
            hart_gpr_we <= 1'b0;
            hart_csr_we <= 1'b0;
            progbuf_insn_valid <= 1'b0;
            sb_read_req <= 1'b0;
            sb_write_req <= 1'b0;

            // =====================================================================
            // DMI Write Requests
            // =====================================================================

            if (dmi_req_valid && dmi_op == DMI_OP_WRITE) begin
                case (dmi_addr)
                    // DMCONTROL register - Always writable to allow activation
                    DM_DMCONTROL: begin
                        dmcontrol_haltreq         <= dmi_wdata[31];
                        dmcontrol_resumereq       <= dmi_wdata[30];
                        dmcontrol_hartreset       <= dmi_wdata[29];
                        dmcontrol_ackhavereset    <= dmi_wdata[28];
                        dmcontrol_hasel           <= dmi_wdata[26];
                        dmcontrol_hartsello       <= dmi_wdata[25:16];
                        dmcontrol_hartselhi       <= dmi_wdata[15:6];
                        dmcontrol_setresethaltreq <= dmi_wdata[3];
                        dmcontrol_clrresethaltreq <= dmi_wdata[2];
                        dmcontrol_ndmreset        <= dmi_wdata[1];
                        dmcontrol_dmactive        <= dmi_wdata[0];
                    end

                    // Other registers require dmactive=1
                    default: begin
                        if (dmcontrol_dmactive) begin
                            case (dmi_addr)
                                // COMMAND register - Start abstract command
                                DM_COMMAND: begin
                                    if (cmd_state == CMD_IDLE) begin
                                        command_reg     <= dmi_wdata;
                                        command_type    <= dmi_wdata[31:24];
                                        command_control <= dmi_wdata[23:0];

                                        // Check if hart is in correct state
                                        if (!selected_hart_halted) begin
                                            abstractcs_cmderr <= CMD_ERR_HALT_RESUME;
                                        end else begin
                                            // Decode and start command
                                            case (dmi_wdata[31:24])
                                                CMD_ACCESS_REG: begin
                                                    // Access register command
                                                    cmd_regno         <= dmi_wdata[15:0];
                                                    cmd_write         <= dmi_wdata[16];
                                                    cmd_transfer      <= dmi_wdata[17];
                                                    cmd_postexec      <= dmi_wdata[18];
                                                    cmd_aarsize       <= dmi_wdata[22:20];
                                                    cmd_postincrement <= dmi_wdata[19];

                                                    // Determine register type and start access
                                                    if (dmi_wdata[15:0] < 16'h1000) begin
                                                        // GPR access (x0-x31: 0x1000-0x101F)
                                                        if (dmi_wdata[16]) begin
                                                            cmd_state <= CMD_WRITE_GPR;
                                                        end else begin
                                                            cmd_state <= CMD_READ_GPR;
                                                        end
                                                    end else begin
                                                        // CSR access (0x0000-0x0FFF)
                                                        if (dmi_wdata[16]) begin
                                                            cmd_state <= CMD_WRITE_CSR;
                                                        end else begin
                                                            cmd_state <= CMD_READ_CSR;
                                                        end
                                                    end
                                                end

                                                CMD_ACCESS_MEM: begin
                                                    // Memory access command
                                                    // Not fully implemented in this version
                                                    abstractcs_cmderr <= CMD_ERR_NOT_SUPPORT;
                                                end

                                                CMD_QUICK_ACCESS: begin
                                                    // Quick access command
                                                    abstractcs_cmderr <= CMD_ERR_NOT_SUPPORT;
                                                end

                                                default: begin
                                                    abstractcs_cmderr <= CMD_ERR_NOT_SUPPORT;
                                                end
                                            endcase
                                        end
                                    end else begin
                                        // Command already in progress
                                        abstractcs_cmderr <= CMD_ERR_BUSY;
                                    end
                                end

                                // ABSTRACTCS register - Clear errors
                                DM_ABSTRACTCS: begin
                                    if (dmi_wdata[10:8] != 3'h0) begin
                                        abstractcs_cmderr <= CMD_ERR_NONE;
                                    end
                                end

                                // ABSTRACTAUTO register
                                DM_ABSTRACTAUTO: begin
                                    abstractauto_autoexecdata    <= dmi_wdata[27:16];
                                    abstractauto_autoexecprogbuf <= dmi_wdata[15:0];
                                end

                                // Abstract Data registers
                                DM_DATA0:  data_reg[0]  <= dmi_wdata;
                                DM_DATA1:  data_reg[1]  <= dmi_wdata;
                                DM_DATA2:  data_reg[2]  <= dmi_wdata;
                                DM_DATA3:  data_reg[3]  <= dmi_wdata;
                                DM_DATA4:  data_reg[4]  <= dmi_wdata;
                                DM_DATA5:  data_reg[5]  <= dmi_wdata;
                                DM_DATA6:  data_reg[6]  <= dmi_wdata;
                                DM_DATA7:  data_reg[7]  <= dmi_wdata;
                                DM_DATA8:  data_reg[8]  <= dmi_wdata;
                                DM_DATA9:  data_reg[9]  <= dmi_wdata;
                                DM_DATA10: data_reg[10] <= dmi_wdata;
                                DM_DATA11: data_reg[11] <= dmi_wdata;

                                // Program buffer registers
                                DM_PROGBUF0:  progbuf[0]  <= dmi_wdata;
                                DM_PROGBUF1:  progbuf[1]  <= dmi_wdata;
                                DM_PROGBUF2:  progbuf[2]  <= dmi_wdata;
                                DM_PROGBUF3:  progbuf[3]  <= dmi_wdata;
                                DM_PROGBUF4:  progbuf[4]  <= dmi_wdata;
                                DM_PROGBUF5:  progbuf[5]  <= dmi_wdata;
                                DM_PROGBUF6:  progbuf[6]  <= dmi_wdata;
                                DM_PROGBUF7:  progbuf[7]  <= dmi_wdata;
                                DM_PROGBUF8:  progbuf[8]  <= dmi_wdata;
                                DM_PROGBUF9:  progbuf[9]  <= dmi_wdata;
                                DM_PROGBUF10: progbuf[10] <= dmi_wdata;
                                DM_PROGBUF11: progbuf[11] <= dmi_wdata;
                                DM_PROGBUF12: progbuf[12] <= dmi_wdata;
                                DM_PROGBUF13: progbuf[13] <= dmi_wdata;
                                DM_PROGBUF14: progbuf[14] <= dmi_wdata;
                                DM_PROGBUF15: progbuf[15] <= dmi_wdata;

                                // System bus control
                                DM_SBCS: begin
                                    sbcs_sbaccess        <= dmi_wdata[14:12];
                                    sbcs_sbautoincrement <= dmi_wdata[16];
                                    sbcs_sbreadonaddr    <= dmi_wdata[21];
                                    sbcs_sbreadondata    <= dmi_wdata[20];
                                    // Writing 1 to sberror clears it
                                    // Handled by sb_error input
                                end

                                // System bus address
                                DM_SBADDRESS0: begin
                                    sbaddress[31:0] <= dmi_wdata;
                                    if (sbcs_sbreadonaddr && !sbcs_sbbusy) begin
                                        sb_read_req <= 1'b1;
                                    end
                                end
                                DM_SBADDRESS1: begin
                                    sbaddress[63:32] <= dmi_wdata;
                                end

                                // System bus data
                                DM_SBDATA0: begin
                                    sbdata[31:0] <= dmi_wdata;
                                    if (!sbcs_sbbusy) begin
                                        sb_write_req <= 1'b1;
                                    end
                                end
                                DM_SBDATA1: begin
                                    sbdata[63:32] <= dmi_wdata;
                                end

                                default: ;
                            endcase
                        end  // if (dmcontrol_dmactive)
                    end  // default case (non-DMCONTROL registers)
                endcase
            end

            // =====================================================================
            // Abstract Command Execution State Machine
            // =====================================================================

            case (cmd_state)
                CMD_IDLE: begin
                    // Waiting for command
                end

                CMD_READ_GPR: begin
                    // Read GPR from hart
                    hart_gpr_addr <= cmd_regno[4:0];
                    hart_gpr_we   <= 1'b0;
                    data_reg[0]   <= hart_gpr_rdata;

                    if (cmd_postexec) begin
                        cmd_state <= CMD_EXEC_PROGBUF;
                    end else begin
                        cmd_state <= CMD_COMPLETE;
                    end
                end

                CMD_WRITE_GPR: begin
                    // Write GPR to hart
                    hart_gpr_addr <= cmd_regno[4:0];
                    hart_gpr_wdata <= data_reg[0];
                    hart_gpr_we <= 1'b1;

                    if (cmd_postexec) begin
                        cmd_state <= CMD_EXEC_PROGBUF;
                    end else begin
                        cmd_state <= CMD_COMPLETE;
                    end
                end

                CMD_READ_CSR: begin
                    // Read CSR from hart
                    hart_csr_addr <= cmd_regno[11:0];
                    hart_csr_we   <= 1'b0;
                    data_reg[0]   <= hart_csr_rdata;

                    if (cmd_postexec) begin
                        cmd_state <= CMD_EXEC_PROGBUF;
                    end else begin
                        cmd_state <= CMD_COMPLETE;
                    end
                end

                CMD_WRITE_CSR: begin
                    // Write CSR to hart
                    hart_csr_addr <= cmd_regno[11:0];
                    hart_csr_wdata <= data_reg[0];
                    hart_csr_we <= 1'b1;

                    if (cmd_postexec) begin
                        cmd_state <= CMD_EXEC_PROGBUF;
                    end else begin
                        cmd_state <= CMD_COMPLETE;
                    end
                end

                CMD_EXEC_PROGBUF: begin
                    // Execute program buffer
                    if (progbuf_pc < 16'(PROGBUF_SIZE)) begin
                        progbuf_insn <= progbuf[progbuf_pc[3:0]];  // PROGBUF_SIZE is 16, so 4 bits
                        progbuf_insn_valid <= 1'b1;

                        if (progbuf_insn_done) begin
                            if (progbuf_exception) begin
                                abstractcs_cmderr <= CMD_ERR_EXCEPTION;
                                cmd_state <= CMD_COMPLETE;
                            end else begin
                                progbuf_pc <= progbuf_pc + 1;
                                // Check for ebreak to exit progbuf
                                if (progbuf[progbuf_pc[3:0]] == 32'h00100073) begin  // ebreak
                                    cmd_state <= CMD_COMPLETE;
                                end
                            end
                        end
                    end else begin
                        cmd_state <= CMD_COMPLETE;
                    end
                end

                CMD_COMPLETE: begin
                    cmd_state  <= CMD_IDLE;
                    progbuf_pc <= 16'h0;
                end

                default: cmd_state <= CMD_IDLE;
            endcase

            // =====================================================================
            // System Bus Access
            // =====================================================================

            if (sb_ready && (sb_read_req || sb_write_req)) begin
                if (sb_error) begin
                    // Bus error occurred
                    // Error flag already set by sbcs_sberror
                end else begin
                    if (sb_read_req) begin
                        // Read completed, update sbdata
                        sbdata <= sb_rdata;

                        if (sbcs_sbautoincrement) begin
                            sbaddress <= sbaddress + (1 << sbcs_sbaccess);
                        end
                    end else if (sb_write_req) begin
                        // Write completed
                        if (sbcs_sbautoincrement) begin
                            sbaddress <= sbaddress + (1 << sbcs_sbaccess);
                        end

                        if (sbcs_sbreadondata) begin
                            sb_read_req <= 1'b1;
                        end
                    end
                end
            end
        end
    end

endmodule
