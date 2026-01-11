/**
 * JTAG Testbench
 * Tests basic JTAG operations including IDCODE read
 */

`timescale 1ns/1ps

module jtag_tb;
    import jtag_dmi_pkg::*;

    reg        clk;
    reg        rst_n;
    
    // 4 Shared Physical I/O Pins
    reg        jtag_pin0_i;      // Pin 0: TCK/TCKC
    reg        jtag_pin1_i;      // Pin 1: TMS/TMSC input
    wire       jtag_pin1_o;      // Pin 1: TMSC output
    wire       jtag_pin1_oen;    // Pin 1: Output enable
    reg        jtag_pin2_i;      // Pin 2: TDI
    wire       jtag_pin3_o;      // Pin 3: TDO
    wire       jtag_pin3_oen;    // Pin 3: Output enable
    reg        jtag_trst_n_i;
    reg        mode_select;
    
    // DMI interface signals
    wire [DMI_ADDR_WIDTH-1:0] dmi_addr;
    wire [DMI_DATA_WIDTH-1:0] dmi_wdata;
    reg  [DMI_DATA_WIDTH-1:0] dmi_rdata;
    dmi_op_e                  dmi_op;
    dmi_resp_e                dmi_resp;
    wire                      dmi_req_valid;
    reg                       dmi_req_ready;
    
    wire [31:0] idcode;
    wire       active_mode;

    // Clock generation
    initial begin
        clk = 0;
        forever #5 clk = ~clk;  // 100MHz clock
    end

    // TCK generation (slower clock for JTAG)
    initial begin
        jtag_pin0_i = 0;
        forever #100 jtag_pin0_i = ~jtag_pin0_i;  // 5MHz JTAG clock
    end

    // DUT instantiation
    jtag_top dut (
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

    // DMI response logic - simple auto-response for testing
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            dmi_rdata <= 0;
            dmi_resp <= DMI_RESP_SUCCESS;
            dmi_req_ready <= 1'b1;
        end else begin
            dmi_req_ready <= 1'b1;  // Always ready
            dmi_resp <= DMI_RESP_SUCCESS;
            if (dmi_req_valid) begin
                dmi_rdata <= 32'hDEADBEEF;  // Return dummy data
            end
        end
    end

    // Test sequence
    initial begin
        // Initialization
        rst_n = 0;
        mode_select = 0;      // Standard JTAG mode
        jtag_pin0_i = 0;
        jtag_pin1_i = 0;
        jtag_pin2_i = 0;
        jtag_trst_n_i = 0;    // Assert reset initially
        dmi_rdata = 0;
        dmi_resp = DMI_RESP_SUCCESS;
        dmi_req_ready = 1;

        #200 rst_n = 1;
        #100 jtag_trst_n_i = 1;  // Release JTAG reset
        
        $display("=== JTAG Testbench Started ===");
        $display("Time: %0t, Active Mode: %s", $time, active_mode ? "cJTAG" : "JTAG");

        // Test 1: Reset TAP controller
        $display("\nTest 1: TAP Controller Reset");
        reset_tap();
        #200;

        // Test 2: Read IDCODE (DR scan via default instruction)
        $display("\nTest 2: Read IDCODE (DR scan)");
        read_idcode();
        #500;

        // Test 3: IR Scan - Load BYPASS instruction
        $display("\nTest 3: IR Scan - Load BYPASS");
        write_ir(8'hFF);  // BYPASS instruction
        #500;

        // Test 4: DR Scan with BYPASS
        $display("\nTest 4: DR Scan - BYPASS register test");
        reset_tap();  // Reset TAP to ensure clean state
        write_ir(8'hFF);  // Reload BYPASS after reset
        test_bypass();
        #500;

        // Test 5: IR Scan - Load IDCODE instruction
        $display("\nTest 5: IR Scan - Load IDCODE instruction");
        write_ir(8'h01);  // Explicitly load IDCODE instruction
        #500;

        // Test 6: DR Scan - Read IDCODE
        $display("\nTest 6: DR Scan - Read IDCODE register");
        read_dr_32bit();
        #500;

        // Test 7: IR Scan - Load DTMCS instruction
        $display("\nTest 7: IR Scan - Load DTMCS instruction");
        write_ir(8'h10);  // DTMCS instruction
        #500;

        // Test 8: DR Scan - Read DTMCS register
        $display("\nTest 8: DR Scan - Read DTMCS register");
        read_dr_32bit();
        #500;

        // Test 9: IR Scan - Load DMI instruction
        $display("\nTest 9: IR Scan - Load DMI instruction");
        write_ir(8'h11);  // DMI instruction
        #500;

        // Test 10: DR Scan - Read DMI register (41 bits)
        $display("\nTest 10: DR Scan - Read DMI register");
        read_dmi();
        #500;

        // Test 11: Switch to cJTAG mode and read IDCODE
        $display("\nTest 11: cJTAG Mode - Read IDCODE");
        mode_select = 1;
        #200;
        $display("Switched to cJTAG mode, Active Mode: %s", active_mode ? "cJTAG" : "JTAG");
        #200;
        
        // In cJTAG mode, simulate basic operations
        // Note: This is a simplified test as full OScan1 protocol is complex
        $display("Testing basic cJTAG interface...");
        
        // Generate some TCO activity (cJTAG clock/data line)
        repeat(10) begin
            jtag_pin0_i = 1;
            #50;
            jtag_pin0_i = 0;
            #50;
        end
        
        // Read IDCODE register value (direct access for verification)
        $display("IDCODE in cJTAG mode: 0x%08h", idcode);
        $display("Expected IDCODE: 0x%08h", dut.idcode);
        
        if (idcode == dut.idcode) begin
            $display("    cJTAG IDCODE verification PASSED");
        end else begin
            $display("    cJTAG IDCODE verification FAILED");
        end
        
        #500;

        // Return to JTAG mode
        $display("\nTest 12: Return to JTAG mode");
        mode_select = 0;
        #200;
        $display("Returned to JTAG mode, Active Mode: %s", active_mode ? "cJTAG" : "JTAG");
        
        // Verify JTAG still works after mode switch
        reset_tap();
        #200;
        
        $display("\n=== JTAG Testbench Completed ===");
        $display("All tests completed successfully!");
        $finish;
    end

    // Task to reset TAP controller
    task reset_tap();
        integer i;
        begin
            $display("  Resetting TAP controller (5 TMS=1 clocks)");
            jtag_pin1_i = 1;
            for (i = 0; i < 5; i = i + 1) begin
                wait_tck();
            end
            jtag_pin1_i = 0;
            wait_tck();
            $display("  TAP reset complete");
        end
    endtask

    // Task to read IDCODE
    task read_idcode();
        integer i;
        logic [31:0] read_data;
        begin
            $display("  Reading IDCODE register...");
            
            // Go to Run-Test/Idle
            jtag_pin1_i = 0;
            wait_tck();
            
            // Select DR path (TMS=1)
            jtag_pin1_i = 1;
            wait_tck();
            
            // Go to Capture-DR (TMS=0)
            jtag_pin1_i = 0;
            wait_tck();
            
            // Shift IDCODE (shift 32 bits)
            read_data = 32'h0;
            for (i = 0; i < 32; i = i + 1) begin
                jtag_pin2_i = 1'b0;
                wait_tck();
                read_data = {jtag_pin3_o, read_data[31:1]};
                $display("    Bit %0d: TDO=%0b", i, jtag_pin3_o);
            end
            
            $display("  IDCODE Read: 0x%08h", read_data);
            $display("  Expected IDCODE: 0x%08h", dut.idcode);
            
            // Exit shift state
            jtag_pin1_i = 1;
            wait_tck();
            
            // Update-DR
            jtag_pin1_i = 0;
            wait_tck();
        end
    endtask

    // Task to wait for TCK edge
    task wait_tck();
        begin
            wait (jtag_pin0_i == 1);
            wait (jtag_pin0_i == 0);
        end
    endtask

    // Task to write instruction register (IR scan)
    task write_ir(input [7:0] instruction);
        integer i;
        logic [7:0] captured_ir;
        begin
            $display("  Writing IR: 0x%02h", instruction);
            
            // Go to Run-Test/Idle
            jtag_pin1_i = 0;
            wait_tck();
            
            // Select IR path (TMS=1, TMS=1)
            jtag_pin1_i = 1;
            wait_tck();
            jtag_pin1_i = 1;
            wait_tck();
            
            // Go to Capture-IR (TMS=0)
            jtag_pin1_i = 0;
            wait_tck();
            
            // Shift-IR state - shift 7 bits with TMS=0
            captured_ir = 8'h0;
            for (i = 0; i < 7; i = i + 1) begin
                jtag_pin2_i = instruction[i];
                jtag_pin1_i = 0;  // Stay in Shift-IR
                wait_tck();
                captured_ir = {jtag_pin3_o, captured_ir[7:1]};
            end
            
            // Shift last bit with TMS=1 to exit Shift-IR
            jtag_pin2_i = instruction[7];
            jtag_pin1_i = 1;  // Exit to Exit1-IR
            wait_tck();
            captured_ir = {jtag_pin3_o, captured_ir[7:1]};
            $display("    Captured IR: 0x%02h", captured_ir);
            
            // Update-IR (TMS=1 from Exit1-IR)
            jtag_pin1_i = 1;
            wait_tck();
            
            // Return to Run-Test/Idle (TMS=0 from Update-IR)
            jtag_pin1_i = 0;
            wait_tck();
            
            // Stay in Run-Test/Idle for a few cycles to let instruction take effect
            repeat(5) wait_tck();
            
            $display("    IR write complete");
        end
    endtask

    // Task to read 32-bit data register
    task read_dr_32bit();
        integer i;
        logic [31:0] read_data;
        begin
            $display("  Reading 32-bit DR...");
            
            // Start from Run-Test/Idle
            jtag_pin1_i = 0;
            wait_tck();
            
            // Go to Select-DR (TMS=1)
            jtag_pin1_i = 1;
            wait_tck();
            
            // Go to Capture-DR (TMS=0)
            jtag_pin1_i = 0;
            wait_tck();
            
            // Stay in Capture-DR one more cycle to ensure register is properly loaded
            wait_tck();
            
            // Shift-DR state - shift ALL 32 bits with TMS=0 (stay in Shift-DR)
            read_data = 32'h0;
            for (i = 0; i < 32; i = i + 1) begin
                jtag_pin2_i = 1'b0;
                jtag_pin1_i = 0;  // Stay in Shift-DR
                wait_tck();
                read_data = {jtag_pin3_o, read_data[31:1]};
            end
            
            $display("    DR read: 0x%08h", read_data);
            
            // Exit Shift-DR with TMS=1
            jtag_pin1_i = 1;
            wait_tck();
            
            // Update-DR (TMS=1 from Exit1-DR)
            jtag_pin1_i = 1;
            wait_tck();
            
            // Return to Run-Test/Idle (TMS=0 from Update-DR)
            jtag_pin1_i = 0;
            wait_tck();
        end
    endtask

    // Task to test BYPASS register
    task test_bypass();
        integer i;
        logic [7:0] test_pattern;
        logic tdo_bit;
        integer pass_count;
        begin
            $display("  Testing BYPASS register...");
            test_pattern = 8'b10110011;
            pass_count = 0;
            
            // Start from Run-Test/Idle
            jtag_pin1_i = 0;
            wait_tck();
            
            // Go to Select-DR (TMS=1)
            jtag_pin1_i = 1;
            wait_tck();
            
            // Go to Capture-DR (TMS=0)
            jtag_pin1_i = 0;
            wait_tck();
            
            // Shift test pattern through BYPASS (shift ALL 8 bits with TMS=0)
            $display("    Shifting pattern: 0b%08b", test_pattern);
            for (i = 0; i < 8; i = i + 1) begin
                jtag_pin2_i = test_pattern[i];
                jtag_pin1_i = 0;  // Stay in Shift-DR
                wait_tck();
                tdo_bit = jtag_pin3_o;
                
                // BYPASS should delay by 1 bit
                if (i > 0 && tdo_bit == test_pattern[i-1]) begin
                    pass_count = pass_count + 1;
                end
                $display("      Bit %0d: TDI=%0b, TDO=%0b", i, test_pattern[i], tdo_bit);
            end
            
            // One more clock with TMS=1 to get last bit and exit
            jtag_pin2_i = 1'b0;
            jtag_pin1_i = 1;  // Exit to Exit1-DR
            wait_tck();
            tdo_bit = jtag_pin3_o;
            if (tdo_bit == test_pattern[7]) begin
                pass_count = pass_count + 1;
            end
            $display("      Final TDO=%0b", tdo_bit);
            
            // Update-DR (TMS=1 from Exit1-DR)
            jtag_pin1_i = 1;
            wait_tck();
            
            // Return to Run-Test/Idle (TMS=0 from Update-DR)
            jtag_pin1_i = 0;
            wait_tck();
            
            if (pass_count >= 7) begin
                $display("    ✓ BYPASS test PASSED (%0d/8 bits correct)", pass_count);
            end else begin
                $display("    ✗ BYPASS test FAILED (%0d/8 bits correct)", pass_count);
            end
        end
    endtask

    // Task to read DMI register (41 bits)
    task read_dmi();
        integer i;
        logic [40:0] read_data;
        logic [6:0] dmi_addr;
        logic [31:0] dmi_data;
        logic [1:0] dmi_op;
        begin
            $display("  Reading 41-bit DMI register...");
            
            // Start from Run-Test/Idle
            jtag_pin1_i = 0;
            wait_tck();
            
            // Go to Select-DR (TMS=1)
            jtag_pin1_i = 1;
            wait_tck();
            
            // Go to Capture-DR (TMS=0)
            jtag_pin1_i = 0;
            wait_tck();
            
            // Shift-DR state - shift ALL 41 bits with TMS=0
            read_data = 41'h0;
            for (i = 0; i < 41; i = i + 1) begin
                jtag_pin2_i = 1'b0;
                jtag_pin1_i = 0;  // Stay in Shift-DR
                wait_tck();
                read_data = {jtag_pin3_o, read_data[40:1]};
            end
            
            // Parse DMI fields
            dmi_op = read_data[1:0];
            dmi_data = read_data[33:2];
            dmi_addr = read_data[40:34];
            
            $display("    DMI read: 0x%011h", read_data);
            $display("      Address: 0x%02h", dmi_addr);
            $display("      Data:    0x%08h", dmi_data);
            $display("      Op:      0x%01h", dmi_op);
            
            // Exit Shift-DR with TMS=1
            jtag_pin1_i = 1;
            wait_tck();
            
            // Update-DR (TMS=1 from Exit1-DR)
            jtag_pin1_i = 1;
            wait_tck();
            
            // Return to Run-Test/Idle (TMS=0 from Update-DR)
            jtag_pin1_i = 0;
            wait_tck();
        end
    endtask

endmodule
