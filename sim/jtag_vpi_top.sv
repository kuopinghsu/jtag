/**
 * JTAG VPI Top Module
 * Simple top-level wrapper for VPI/OpenOCD integration
 * Uses 4-pin multiplexed interface
 */

module jtag_vpi_top 
    import jtag_dmi_pkg::*;
(
    input  logic clk,
    input  logic rst_n,
    
    // 4 Shared Physical I/O Pins
    input  logic jtag_pin0_i,      // Pin 0: TCK/TCKC (input)
    input  logic jtag_pin1_i,      // Pin 1: TMS/TMSC (bidir input)
    output logic jtag_pin1_o,      // Pin 1: TMSC output
    output logic jtag_pin1_oen,    // Pin 1: Output enable
    input  logic jtag_pin2_i,      // Pin 2: TDI (input)
    output logic jtag_pin3_o,      // Pin 3: TDO (output)
    output logic jtag_pin3_oen,    // Pin 3: Output enable
    input  logic jtag_trst_n_i,    // Optional TRST_N
    input  logic mode_select,      // 0=JTAG, 1=cJTAG
    
    // Expose outputs
    output logic [31:0] idcode,
    output logic active_mode
);

    // DMI interface signals (internal)
    logic [DMI_ADDR_WIDTH-1:0] dmi_addr;
    logic [DMI_DATA_WIDTH-1:0] dmi_wdata;
    logic [DMI_DATA_WIDTH-1:0] dmi_rdata;
    jtag_dmi_pkg::dmi_op_e     dmi_op;
    jtag_dmi_pkg::dmi_resp_e   dmi_resp;
    logic                      dmi_req_valid;
    logic                      dmi_req_ready;
    
    // DMI dummy response - always ready with success
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            dmi_rdata <= '0;
            dmi_resp <= DMI_RESP_SUCCESS;
            dmi_req_ready <= 1'b1;
        end else begin
            dmi_req_ready <= 1'b1;
            dmi_resp <= DMI_RESP_SUCCESS;
            if (dmi_req_valid) begin
                dmi_rdata <= 32'hDEADBEEF;  // Dummy data
            end
        end
    end

    // Instantiate JTAG top module
    jtag_top dut (
        .clk(clk),
        .rst_n(rst_n),
        .jtag_pin0_i(jtag_pin0_i),
        .jtag_pin1_i(jtag_pin1_i),
        .jtag_pin1_o(jtag_pin1_o),
        .jtag_pin1_oen(jtag_pin1_oen),
        .jtag_pin2_i(jtag_pin2_i),
        .jtag_pin3_o(jtag_pin3_o),
        .jtag_pin3_oen(jtag_pin3_oen),
        .jtag_trst_n_i(jtag_trst_n_i),
        .mode_select(mode_select),
        .dmi_addr(dmi_addr),
        .dmi_wdata(dmi_wdata),
        .dmi_rdata(dmi_rdata),
        .dmi_op(dmi_op),
        .dmi_resp(dmi_resp),
        .dmi_req_valid(dmi_req_valid),
        .dmi_req_ready(dmi_req_ready),
        .idcode(idcode),
        .active_mode(active_mode)
    );

    // VPI control:
    // Input pins: jtag_pin0_i, jtag_pin1_i, jtag_pin2_i, jtag_trst_n_i, mode_select
    // Output pins: jtag_pin1_o/oen, jtag_pin3_o/oen, idcode, active_mode

endmodule
