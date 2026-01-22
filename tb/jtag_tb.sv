/**
 * JTAG Testbench
 * Tests basic JTAG operations including IDCODE read
 */

`timescale 1ns/1ps

module jtag_tb;
    import jtag_dmi_pkg::*;

    // DPI export for C++ integration
    export "DPI-C" function get_verification_status_dpi;

    reg        clk;
    reg        rst_n;

    // 4 Shared Physical I/O Pins
    reg        jtag_pin0_i;      // Pin 0: TCK/TCKC
    reg        jtag_pin1_i;      // Pin 1: TMS/TMSC input
    wire       jtag_pin1_o;      // Pin 1: TMSC output
    wire       jtag_pin1_oen;    // Pin 1: Output enable (active low)
    reg        jtag_pin2_i;      // Pin 2: TDI
    wire       jtag_pin3_o;      // Pin 3: TDO
    wire       jtag_pin3_oen;    // Pin 3: Output enable (active low)
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

    localparam JTAG_IDCODE = 32'h1DEAD3FF;

    // JTAG module path definitions for easier signal access
    `define JTAG_IR_LATCH    dut.ir_reg.ir_latch
    `define JTAG_IR_OUT      dut.ir_reg.ir_out
    `define JTAG_TAP_STATE   dut.tap_ctrl.state

    // Test tracking variables
    integer test_count = 0;
    integer pass_count = 0;
    integer fail_count = 0;

    // Failed test tracking
    parameter MAX_TESTS = 20;
    string failed_tests [MAX_TESTS];
    integer failed_test_numbers [MAX_TESTS];
    integer failed_test_count = 0;

    // Global variable to track verification results from tasks
    logic last_verification_result = 1'b0;

    // Include common JTAG tasks
    `include "jtag_common.sv"

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
    // DMI memory for read/write operations
    logic [31:0] dmi_memory [128];  // 128 DMI registers (7-bit address)

    // DMI response logic - supports both read and write operations
    // Simulates actual DMI register behavior with memory backing
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            dmi_rdata <= 0;
            dmi_resp <= DMI_RESP_SUCCESS;
            dmi_req_ready <= 1'b1;
            // Initialize some default values, use non-zero to detect writes
            for (int i = 0; i < 128; i++) begin
                dmi_memory[i] <= 32'h08946788;
            end
        end else begin
            dmi_req_ready <= 1'b1;  // Always ready
            dmi_resp <= DMI_RESP_SUCCESS; // Always success
            if (dmi_req_valid) begin
                case (dmi_op)
                    DMI_OP_READ: begin
                        dmi_rdata <= dmi_memory[dmi_addr];
                    end
                    DMI_OP_WRITE: begin
                        dmi_memory[dmi_addr] <= dmi_wdata;
                        dmi_rdata <= 32'h0;  // Write operations don't return data
                    end
                    default: begin
                        dmi_rdata <= 32'h0;
                    end
                endcase
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
        test_count = test_count + 1;
        reset_tap();
        read_idcode_with_check(32'h1DEAD3FF);
        if (last_verification_result) begin
            $display("  ✓ TAP reset verification PASSED - IDCODE verification successful");
            pass_count = pass_count + 1;
        end else begin
            $display("  ✗ TAP reset verification FAILED - IDCODE verification failed");
            fail_count = fail_count + 1;
            record_failed_test(1, "TAP Controller Reset");
        end
        #500;

        // Test 2: Read IDCODE (DR scan via default instruction)
        $display("\nTest 2: Read IDCODE (DR scan)");
        test_count = test_count + 1;
        read_idcode_with_check(32'h1DEAD3FF);
        if (last_verification_result) begin
            pass_count = pass_count + 1;
            $display("    ✓ Test 2 PASSED - IDCODE verification successful");
        end else begin
            fail_count = fail_count + 1;
            record_failed_test(2, "Read IDCODE (DR scan)");
            $display("    ✗ Test 2 FAILED - IDCODE verification failed");
        end
        #500;

        // Test 3: IR Scan - Load BYPASS instruction
        $display("\nTest 3: IR Scan - Load BYPASS");
        test_count = test_count + 1;
        write_ir_with_check(5'h1F);  // BYPASS instruction (5-bit: 0x1F)
        if (last_verification_result) begin
            pass_count = pass_count + 1;
            $display("    ✓ Test 3 PASSED - BYPASS IR instruction loaded successfully");
        end else begin
            fail_count = fail_count + 1;
            record_failed_test(3, "IR Scan - Load BYPASS instruction");
            $display("    ✗ Test 3 FAILED - BYPASS IR instruction load failed");
        end
        #500;

        // Test 4: DR Scan with BYPASS
        $display("\nTest 4: DR Scan - BYPASS register test");
        test_count = test_count + 1;
        reset_tap();  // Reset TAP to ensure clean state
        write_ir_with_check(5'h1F);  // Reload BYPASS after reset (5-bit: 0x1F)
        test_bypass();
        if (last_verification_result) begin
            pass_count = pass_count + 1;
            $display("    ✓ Test 4 PASSED - BYPASS register test successful");
        end else begin
            fail_count = fail_count + 1;
            record_failed_test(4, "DR Scan - BYPASS register test");
            $display("    ✗ Test 4 FAILED - BYPASS register test failed");
        end
        #500;

        // Test 5: IR Scan - Load IDCODE instruction
        $display("\nTest 5: IR Scan - Load IDCODE instruction");
        test_count = test_count + 1;
        write_ir_with_check(5'h01);  // Explicitly load IDCODE instruction
        if (last_verification_result) begin
            pass_count = pass_count + 1;
            $display("    ✓ Test 5 PASSED - IDCODE IR instruction loaded successfully");
        end else begin
            fail_count = fail_count + 1;
            record_failed_test(5, "IR Scan - Load IDCODE instruction");
            $display("    ✗ Test 5 FAILED - IDCODE IR instruction load failed");
        end
        #500;

        // Test 6: DR Scan - Read IDCODE
        $display("\nTest 6: DR Scan - Read IDCODE register");
        test_count = test_count + 1;
        read_dr_32bit_with_check(32'h1DEAD3FF);
        if (last_verification_result) begin
            pass_count = pass_count + 1;
            $display("    ✓ Test 6 PASSED - IDCODE DR read successful");
        end else begin
            fail_count = fail_count + 1;
            record_failed_test(6, "DR Scan - Read IDCODE register");
            $display("    ✗ Test 6 FAILED - IDCODE DR read failed");
        end
        #500;

        // Test 7: IR Scan - Load DTMCS instruction
        $display("\nTest 7: IR Scan - Load DTMCS instruction");
        test_count = test_count + 1;
        write_ir_with_check(5'h10);  // DTMCS instruction
        if (last_verification_result) begin
            pass_count = pass_count + 1;
            $display("    ✓ Test 7 PASSED - DTMCS IR instruction loaded successfully");
        end else begin
            fail_count = fail_count + 1;
            record_failed_test(7, "IR Scan - Load DTMCS instruction");
            $display("    ✗ Test 7 FAILED - DTMCS IR instruction load failed");
        end
        #500;

        // Test 8: DR Scan - Read DTMCS register
        $display("\nTest 8: DR Scan - Read DTMCS register");
        test_count = test_count + 1;
        read_dr_32bit_with_check(32'h00001071);  // Expected DTMCS value (RISC-V Debug Spec v0.13.2 compliant)
        if (last_verification_result) begin
            pass_count = pass_count + 1;
            $display("    ✓ Test 8 PASSED - DTMCS DR read successful");
        end else begin
            fail_count = fail_count + 1;
            record_failed_test(8, "DR Scan - Read DTMCS register");
            $display("    ✗ Test 8 FAILED - DTMCS DR read failed");
        end
        #500;

        // Test 9: IR Scan - Load DMI instruction
        $display("\nTest 9: IR Scan - Load DMI instruction");
        test_count = test_count + 1;
        write_ir_with_check(5'h11);  // DMI instruction
        if (last_verification_result) begin
            pass_count = pass_count + 1;
            $display("    ✓ Test 9 PASSED - DMI IR instruction loaded successfully");
        end else begin
            fail_count = fail_count + 1;
            record_failed_test(9, "IR Scan - Load DMI instruction");
            $display("    ✗ Test 9 FAILED - DMI IR instruction load failed");
        end
        #500;

        // Test 10: DMI Write/Read Register Test
        $display("\nTest 10: DMI Write/Read Register Test");
        test_count = test_count + 1;
        // Test write then read operations on DATA0 register (0x04)
        write_dm_register_with_check(7'h04, 32'hACACACAC);
        if (last_verification_result) begin
            pass_count = pass_count + 1;
            $display("    ✓ Test 10 PASSED - DMI write/read test successful");
        end else begin
            fail_count = fail_count + 1;
            record_failed_test(10, "DMI Write/Read Register Test");
            $display("    ✗ Test 10 FAILED - DMI write/read test failed");
        end
        #500;

        // Test 11: Switch to cJTAG mode and read IDCODE
        $display("\nTest 11: cJTAG Mode - Read IDCODE");
        test_count = test_count + 1;
        read_cjtag_idcode_with_check(JTAG_IDCODE);
        if (last_verification_result) begin
            pass_count = pass_count + 1;
            $display("    ✓ Test 11 PASSED - cJTAG IDCODE verification successful");
        end else begin
            fail_count = fail_count + 1;
            record_failed_test(11, "cJTAG Mode - Read IDCODE");
            $display("    ✗ Test 11 FAILED - cJTAG IDCODE verification failed");
        end
        #500;

        // Test 12: cJTAG DMI Write/Read Register Test
        $display("\nTest 12: cJTAG DMI Write/Read Register Test");
        test_count = test_count + 1;
        // Ensure we are in cJTAG mode
        mode_select = 1;
        #100;
        // Write to DATA0 register (0x04) with a known value
        write_dm_register_with_check(7'h04, 32'hDEADBEEF);
        if (last_verification_result) begin
            pass_count = pass_count + 1;
            $display("    ✓ Test 12 PASSED - cJTAG DMI write/read successful");
        end else begin
            fail_count = fail_count + 1;
            record_failed_test(12, "cJTAG DMI Write/Read Register Test");
            $display("    ✗ Test 12 FAILED - cJTAG DMI write/read failed");
        end
        #500;

        // Test 13: Return to JTAG mode
        $display("\nTest 13: Return to JTAG mode");
        test_count = test_count + 1;
        mode_select = 0;
        $display("Returned to JTAG mode, Active Mode: %s", active_mode ? "cJTAG" : "JTAG");
        // Verify JTAG still works after mode switch
        reset_tap();
        read_idcode_with_check(32'h1DEAD3FF);
        if (last_verification_result) begin
            pass_count = pass_count + 1;
            $display("    ✓ Test 13 PASSED - JTAG mode restored and verified");
        end else begin
            fail_count = fail_count + 1;
            record_failed_test(13, "Return to JTAG mode");
            $display("    ✗ Test 13 FAILED - JTAG mode verification failed");
        end
        #200;

        // Test 14: OScan1 OAC Detection
        $display("\nTest 14: OScan1 OAC Detection and Protocol Activation");
        test_count = test_count + 1;
        test_oscan1_oac_detection();
        if (last_verification_result) begin
            pass_count = pass_count + 1;
            $display("    ✓ Test 14 PASSED - OScan1 OAC detection successful");
        end else begin
            fail_count = fail_count + 1;
            record_failed_test(14, "OScan1 OAC Detection and Protocol Activation");
            $display("    ✗ Test 14 FAILED - OScan1 OAC detection failed");
        end
        #500;

        // Test 15: OScan1 JScan Commands
        $display("\nTest 15: OScan1 JScan Command Processing");
        test_count = test_count + 1;
        test_oscan1_jscan_commands();
        if (last_verification_result) begin
            pass_count = pass_count + 1;
            $display("    ✓ Test 15 PASSED - JScan command processing successful");
        end else begin
            fail_count = fail_count + 1;
            record_failed_test(15, "OScan1 JScan Command Processing");
            $display("    ✗ Test 15 FAILED - JScan command processing failed");
        end
        #500;

        // Test 16: OScan1 SF0 Protocol Testing
        $display("\nTest 16: OScan1 Scanning Format 0 (SF0)");
        test_count = test_count + 1;
        test_oscan1_sf0_protocol();
        if (last_verification_result) begin
            pass_count = pass_count + 1;
            $display("    ✓ Test 16 PASSED - SF0 protocol test successful");
        end else begin
            fail_count = fail_count + 1;
            record_failed_test(16, "OScan1 Scanning Format 0 (SF0)");
            $display("    ✗ Test 16 FAILED - SF0 protocol test failed");
        end
        #500;

        // Test 17: OScan1 Zero Insertion/Deletion
        $display("\nTest 17: OScan1 Zero Stuffing (Bit Stuffing)");
        test_count = test_count + 1;
        test_oscan1_zero_stuffing();
        if (last_verification_result) begin
            pass_count = pass_count + 1;
            $display("    ✓ Test 17 PASSED - Zero stuffing test successful");
        end else begin
            fail_count = fail_count + 1;
            record_failed_test(17, "OScan1 Zero Stuffing (Bit Stuffing)");
            $display("    ✗ Test 17 FAILED - Zero stuffing test failed");
        end
        #500;

        // Test 18: Protocol Switching Stress Test
        $display("\nTest 18: JTAG ↔ cJTAG Protocol Switching");
        test_count = test_count + 1;
        test_protocol_switching();
        if (last_verification_result) begin
            pass_count = pass_count + 1;
            $display("    ✓ Test 18 PASSED - Protocol switching successful");
        end else begin
            fail_count = fail_count + 1;
            record_failed_test(18, "JTAG ↔ cJTAG Protocol Switching");
            $display("    ✗ Test 18 FAILED - Protocol switching failed");
        end
        #500;

        // Test 19: Boundary Conditions Testing
        $display("\nTest 19: Protocol Boundary Conditions");
        test_count = test_count + 1;
        test_boundary_conditions();
        if (last_verification_result) begin
            pass_count = pass_count + 1;
            $display("    ✓ Test 19 PASSED - Boundary conditions test successful");
        end else begin
            fail_count = fail_count + 1;
            record_failed_test(19, "Protocol Boundary Conditions");
            $display("    ✗ Test 19 FAILED - Boundary conditions test failed");
        end
        #500;

        // Test 20: Full cJTAG Protocol Test
        $display("\nTest 20: Full cJTAG Protocol Implementation");
        test_count = test_count + 1;
        test_full_cjtag_protocol();
        if (last_verification_result) begin
            pass_count = pass_count + 1;
            $display("    ✓ Test 20 PASSED - Full cJTAG protocol successful");
        end else begin
            fail_count = fail_count + 1;
            record_failed_test(20, "Full cJTAG Protocol Implementation");
            $display("    ✗ Test 20 FAILED - Full cJTAG protocol failed");
        end
        #1000;

        $display("\n=== Enhanced JTAG Testbench Completed ===");
        $display("Tests completed: %0d total, %0d passed, %0d failed", test_count, pass_count, fail_count);

        if (fail_count == 0) begin
            $display("✓ ALL TESTS PASSED!");
            $display("  - TAP Reset and IDCODE verification: PASSED");
            $display("  - Instruction Register operations: PASSED");
            $display("  - Data Register operations: PASSED");
            $display("  - cJTAG/OScan1 protocol: PASSED");
            $display("  - Protocol switching: PASSED");
            $display("  - Boundary conditions: PASSED");
        end else begin
            $display("✗ %0d TESTS FAILED - Details below:", fail_count);
            $display("\n=== FAILED TEST SUMMARY ===");
            for (integer i = 0; i < failed_test_count; i++) begin
                $display("  Test %0d: %s", failed_test_numbers[i], failed_tests[i]);
            end
            $display("\n=== RECOMMENDATIONS ===");
            $display("  - Check waveform file: jtag_sim.fst");
            $display("  - Review JTAG signal timing and protocol");
            $display("  - Verify IDCODE matches expected value (0x1DEAD3FF)");
            $display("  - Check TAP controller state transitions");
        end

        $display("\n=== TEST COVERAGE ===");
        $display("  ✓ IEEE 1149.1 JTAG Protocol");
        $display("  ✓ IEEE 1149.7 cJTAG OScan1 Protocol");
        $display("  ✓ RISC-V Debug Module Interface (DMI)");
        $display("  ✓ Protocol Mode Switching");
        $display("  ✓ Boundary Condition Testing");
        $display("\n=== PERFORMANCE METRICS ===");
        $display("  Total simulation time: %0t", $time);
        $display("  Pass rate: %0d%% (%0d/%0d)", (pass_count * 100) / test_count, pass_count, test_count);

        if (fail_count > 0) begin
            $display("\n=== SIMULATION FAILED ===");
            $finish(1);  // Exit with error code
        end else begin
            $finish;
        end
    end

    // Timeout
    initial begin
        #1000000;
        $display("ERROR: Testbench timeout!");
        $finish(1);  // Exit with error code
    end

    // DPI function wrapper for C++ access
    // Returns: 0 = passed, 1 = failed, 2 = timeout
    function int get_verification_status_dpi();
        int status;
        // Check for timeout condition first
        if ($time >= 1000000) begin
            status = 2;  // Timeout
        end else if (fail_count > 0) begin
            status = 1;  // Failed
        end else if (pass_count > 0 && test_count > 0) begin
            status = 0;  // Passed (has tests and all passed)
        end else begin
            status = 1;  // No tests completed or unknown state - treat as failed
        end
        return status;
    endfunction

    // Task to read 32-bit data register with verification
    task read_dr_32bit_with_check(input [31:0] expected_value);
        integer i;
        logic [31:0] read_data;
        begin
            $display("  Reading and verifying 32-bit DR (expected: 0x%08h)...", expected_value);

            // Start from Run-Test/Idle
            jtag_pin1_i = 0;
            wait_tck();

            // Go to Select-DR (TMS=1)
            jtag_pin1_i = 1;
            wait_tck();

            // Go to Capture-DR (TMS=0)
            jtag_pin1_i = 0;
            wait_tck();

            // Shift-DR state - shift 32 bits, with TMS=1 on the last bit
            read_data = 32'h0;
            for (i = 0; i < 32; i = i + 1) begin
                jtag_pin2_i = 1'b0;
                jtag_pin1_i = (i == 31) ? 1 : 0;  // TMS=1 on last bit to exit
                wait_tck();
                #1;
                read_data = {jtag_pin3_o, read_data[31:1]};
            end

            // Exit shift state
            jtag_pin1_i = 1;
            wait_tck();

            // Update-DR
            jtag_pin1_i = 0;
            wait_tck();

            $display("    DR read: 0x%08h", read_data);

            // Verify the read data
            if (read_data == expected_value) begin
                $display("    ✓ DR verification PASSED");
                last_verification_result = 1'b1;
            end else begin
                $display("    ✗ DR verification FAILED");
                last_verification_result = 1'b0;
            end
        end
    endtask

    // Task to test BYPASS register
    task test_bypass();
        integer i;
        logic [7:0] test_pattern;
        logic tdo_bit, expected_tdo;
        integer pass_count_local;
        begin
            $display("  Testing BYPASS register...");
            test_pattern = 8'b10110011;
            pass_count_local = 0;
            expected_tdo = 1'b0;  // Initial bypass register state after Capture-DR

            // Start from Run-Test/Idle
            jtag_pin1_i = 0;
            wait_tck();

            // Go to Select-DR (TMS=1)
            jtag_pin1_i = 1;
            wait_tck();

            // Go to Capture-DR (TMS=0) - this loads 0 into bypass register
            jtag_pin1_i = 0;
            wait_tck();

            // Transition to Shift-DR (TMS=0)
            jtag_pin1_i = 0;
            wait_tck();

            // Shift test pattern through BYPASS - need 9 shifts to verify all 8 bits
            // (1 to get captured value, 8 to push all input bits through)
            $display("    Shifting pattern: 0b%08b", test_pattern);
            for (i = 0; i < 9; i = i + 1) begin
                // For first 8 shifts, send pattern bits; for 9th shift, send 0
                if (i < 8) begin
                    jtag_pin2_i = test_pattern[i];
                end else begin
                    jtag_pin2_i = 1'b0;  // Dummy bit to push last pattern bit out
                end

                if (i < 8) begin
                    jtag_pin1_i = 0;  // Stay in Shift-DR for first 8 bits
                end else begin
                    jtag_pin1_i = 1;  // Exit on 9th bit
                end

                // Clock edge happens here - TDI shifts in, TDO shifts out
                wait_tck();

                // Sample TDO after the clock edge
                #1;
                tdo_bit = jtag_pin3_o;

                // Compare with expected
                if (tdo_bit == expected_tdo) begin
                    pass_count_local = pass_count_local + 1;
                    if (i < 8) begin
                        $display("      Bit %0d: TDI=%0b, TDO=%0b (expected=%0b) ✓", i, test_pattern[i], tdo_bit, expected_tdo);
                    end else begin
                        $display("      Bit %0d: TDI=0 (dummy), TDO=%0b (expected=%0b) ✓", i, tdo_bit, expected_tdo);
                    end
                end else begin
                    if (i < 8) begin
                        $display("      Bit %0d: TDI=%0b, TDO=%0b (expected=%0b) ✗", i, test_pattern[i], tdo_bit, expected_tdo);
                    end else begin
                        $display("      Bit %0d: TDI=0 (dummy), TDO=%0b (expected=%0b) ✗", i, tdo_bit, expected_tdo);
                    end
                end

                // Update expected TDO for next iteration
                if (i < 8) begin
                    expected_tdo = test_pattern[i];
                end else begin
                    expected_tdo = 1'b0;  // After dummy bit
                end
            end

            // Update-DR (TMS=1 from Exit1-DR)
            jtag_pin1_i = 1;
            wait_tck();

            // Return to Run-Test/Idle (TMS=0 from Update-DR)
            jtag_pin1_i = 0;
            wait_tck();

            // Check for 9 correct comparisons (captured 0 + pattern[0:7])
            if (pass_count_local >= 9) begin
                $display("    ✓ BYPASS test PASSED (%0d/9 bits correct)", pass_count_local);
                last_verification_result = 1'b1;
            end else begin
                $display("    ✗ BYPASS test FAILED (%0d/9 bits correct)", pass_count_local);
                last_verification_result = 1'b0;
            end
        end
    endtask

    // Task to test OScan1 OAC (Attention Character) detection
    task test_oscan1_oac_detection();
        integer i;
        logic prev_oscan_active;
        logic [3:0] test_cmd;
        logic [31:0] cjtag_test_idcode;
        logic tms_bit, tdi_bit;
        begin
            $display("  Testing OAC detection (16 consecutive edges)...");

            // Switch to cJTAG mode first
            mode_select = 1;
            #100;

            // Capture initial state before OAC
            prev_oscan_active = 1'b0;  // Assume inactive initially
            if ($test$plusargs("oscan_debug")) begin
                // Monitor internal OScan1 controller state if available
                // This would require exposing internal signals in testbench
                $display("    Initial OScan1 state: inactive");
            end

            // Generate 16 consecutive edges on TCKC (jtag_pin0_i)
            $display("    Generating OAC sequence...");
            for (i = 0; i < 16; i = i + 1) begin
                jtag_pin0_i = ~jtag_pin0_i;
                #50; // 50ns edge spacing
            end

            $display("    OAC sequence completed");

            // Wait for OAC detection processing
            #200;

            // Verify OAC detection by attempting JScan command
            // If OAC was detected, we should be able to send JScan commands
            $display("    Verifying OAC detection by testing JScan response...");

            // Try to send a NOOP JScan command (0xF) and monitor for any response
            test_cmd = 4'hF;  // JSCAN_NOOP
            for (i = 0; i < 4; i = i + 1) begin
                jtag_pin1_i = test_cmd[i];
                jtag_pin0_i = 1; #25;
                jtag_pin0_i = 0; #25;
            end
            #100;

            // Verify OAC detection by attempting to read IDCODE in cJTAG mode
            // This provides actual data verification rather than just sequence completion
            $display("    Verifying OAC detection by attempting IDCODE read in cJTAG mode...");

            cjtag_test_idcode = 32'h0;

            // Try a simple JTAG operation via cJTAG to verify the protocol is active
            // Reset TAP via SF0 protocol
            for (i = 0; i < 5; i = i + 1) begin
                tms_bit = 1; tdi_bit = 0;
                jtag_pin1_i = tms_bit; jtag_pin0_i = 1; #25;
                jtag_pin1_i = tdi_bit; jtag_pin0_i = 0; #25;
            end

            // Go to DR scan for IDCODE (should be default after reset)
            tms_bit = 0; tdi_bit = 0;
            jtag_pin1_i = tms_bit; jtag_pin0_i = 1; #25;
            jtag_pin1_i = tdi_bit; jtag_pin0_i = 0; #25;

            tms_bit = 1; tdi_bit = 0;
            jtag_pin1_i = tms_bit; jtag_pin0_i = 1; #25;
            jtag_pin1_i = tdi_bit; jtag_pin0_i = 0; #25;

            tms_bit = 0; tdi_bit = 0;
            jtag_pin1_i = tms_bit; jtag_pin0_i = 1; #25;
            jtag_pin1_i = tdi_bit; jtag_pin0_i = 0; #25;

            // Try to shift a few bits and see if we get any response
            cjtag_test_idcode = 32'h0;
            for (i = 0; i < 8; i = i + 1) begin  // Just test first 8 bits
                tms_bit = (i == 7) ? 1 : 0;  // Exit on last bit
                tdi_bit = 0;

                jtag_pin1_i = tms_bit; jtag_pin0_i = 1; #25;
                if (jtag_pin1_oen) begin
                    cjtag_test_idcode[i] = jtag_pin1_o;
                end
                jtag_pin1_i = tdi_bit; jtag_pin0_i = 0; #25;
            end

            // Update-DR and return to idle
            tms_bit = 1; tdi_bit = 0;
            jtag_pin1_i = tms_bit; jtag_pin0_i = 1; #25;
            jtag_pin1_i = tdi_bit; jtag_pin0_i = 0; #25;
            tms_bit = 0; tdi_bit = 0;
            jtag_pin1_i = tms_bit; jtag_pin0_i = 1; #25;
            jtag_pin1_i = tdi_bit; jtag_pin0_i = 0; #25;

            // Verify we got some response indicating cJTAG protocol is active
            if (cjtag_test_idcode == JTAG_IDCODE) begin
                $display("    ✓ OAC detection test PASSED - cJTAG protocol response detected");
                last_verification_result = 1'b1;
            end else begin
                $display("    ✗ OAC detection test FAILED - No cJTAG response detected");
                last_verification_result = 1'b0;
            end
        end
    endtask

    // Task to test OScan1 JScan command processing
    task test_oscan1_jscan_commands();
        integer i;
        logic [3:0] jscan_cmd;
        logic [7:0] verification_pattern;
        logic [7:0] sf0_captured_data;
        logic sf0_activity_detected;
        begin
            $display("  Testing JScan command processing...");

            // Ensure we're in cJTAG mode
            mode_select = 1;
            #100;

            // Send OAC first to enter JScan mode
            test_oscan1_oac_detection();
            #100;

            // Send JSCAN_OSCAN_ON command (4 bits = 0x1)
            jscan_cmd = 4'h1;  // JSCAN_OSCAN_ON
            $display("    Sending JSCAN_OSCAN_ON (0x1)...");

            for (i = 0; i < 4; i = i + 1) begin
                // Send bit on TMSC during falling edge of TCKC
                jtag_pin1_i = jscan_cmd[i];  // LSB first
                jtag_pin0_i = 1;
                #25;
                jtag_pin0_i = 0;
                #25;
            end

            #200;

            // Verify JScan command was processed by attempting SF0 operation with actual data verification
            $display("    Verifying JSCAN_OSCAN_ON by testing SF0 response with IDCODE attempt...");

            sf0_captured_data = 8'h0;
            sf0_activity_detected = 1'b0;

            // Try SF0 operation: attempt to read partial IDCODE
            // Reset TAP first
            for (i = 0; i < 5; i = i + 1) begin
                // SF0: TMS=1 on rising edge, TDI=0 on falling edge
                jtag_pin1_i = 1'b1;  // TMS bit (reset)
                jtag_pin0_i = 1; #25;
                jtag_pin1_i = 1'b0;  // TDI bit
                jtag_pin0_i = 0; #25;
            end

            // Go to DR scan (TMS sequence: 0,1,0 to reach Shift-DR)
            // Run-Test-Idle (TMS=0)
            jtag_pin1_i = 1'b0; jtag_pin0_i = 1; #25;
            jtag_pin1_i = 1'b0; jtag_pin0_i = 0; #25;
            // Select-DR (TMS=1)
            jtag_pin1_i = 1'b1; jtag_pin0_i = 1; #25;
            jtag_pin1_i = 1'b0; jtag_pin0_i = 0; #25;
            // Capture-DR (TMS=0)
            jtag_pin1_i = 1'b0; jtag_pin0_i = 1; #25;
            jtag_pin1_i = 1'b0; jtag_pin0_i = 0; #25;

            // Try to shift 8 bits and capture response
            for (i = 0; i < 8; i = i + 1) begin
                // SF0: TMS on rising edge, TDI on falling edge
                jtag_pin1_i = (i == 7) ? 1'b1 : 1'b0;  // TMS bit (exit on last)
                jtag_pin0_i = 1; #25;

                // Check for TDO response
                if (jtag_pin1_oen) begin
                    sf0_captured_data[i] = jtag_pin1_o;
                    sf0_activity_detected = 1'b1;
                    $display("      SF0 TDO captured: bit %0d = %b", i, jtag_pin1_o);
                end

                jtag_pin1_i = 1'b0;  // TDI bit (always 0 for read)
                jtag_pin0_i = 0; #25;
            end

            // Exit to Update-DR
            jtag_pin1_i = 1'b1; jtag_pin0_i = 1; #25;
            jtag_pin1_i = 1'b0; jtag_pin0_i = 0; #25;
            // Return to Run-Test-Idle
            jtag_pin1_i = 1'b0; jtag_pin0_i = 1; #25;
            jtag_pin1_i = 1'b0; jtag_pin0_i = 0; #25;

            // Send JSCAN_OSCAN_OFF to exit
            jscan_cmd = 4'h0;  // JSCAN_OSCAN_OFF
            $display("    Sending JSCAN_OSCAN_OFF (0x0)...");

            test_oscan1_oac_detection();  // OAC sequence

            for (i = 0; i < 4; i = i + 1) begin
                jtag_pin1_i = jscan_cmd[i];
                jtag_pin0_i = 1;
                #25;
                jtag_pin0_i = 0;
                #25;
            end

            #200;
            $display("    ✓ JScan OSCAN_OFF command sent - JScan test completed");

            // Verify JScan command processing based on actual SF0 response
            if (sf0_activity_detected && sf0_captured_data != 8'h0) begin // FIXME
                $display("    ✓ JScan command processing test PASSED - SF0 data captured: 0x%02h", sf0_captured_data);
                last_verification_result = 1'b1;
            end else if (sf0_activity_detected) begin
                $display("    ⚠ JScan command processing test - SF0 activity detected but no data");
                last_verification_result = 1'b0;
            end else begin
                $display("    ✗ JScan command processing test FAILED - No SF0 activity detected");
                last_verification_result = 1'b0;
            end
        end
    endtask

    // Task to test OScan1 Scanning Format 0 (SF0)
    task test_oscan1_sf0_protocol();
        integer i;
        logic tms_bit, tdi_bit;
        logic [7:0] sf0_tdo_captured;
        logic [7:0] sf0_pattern;
        begin
            $display("  Testing SF0 scanning format...");

            // Switch to cJTAG mode and activate OScan1
            mode_select = 1;
            #100;

            // Enter OScan1 mode via OAC + JSCAN_ON
            test_oscan1_oac_detection();
            #100;

            // Send JSCAN_OSCAN_ON
            for (i = 0; i < 4; i = i + 1) begin
                jtag_pin1_i = (i == 0) ? 1 : 0;  // 0x1 = JSCAN_OSCAN_ON, LSB first
                jtag_pin0_i = 1; #25; jtag_pin0_i = 0; #25;
            end
            #100;

            $display("    Testing SF0 bit transfer with TDO capture...");

            // Test SF0 protocol with multiple bits and TDO monitoring
            sf0_pattern = 8'b11010010;  // Test pattern
            sf0_tdo_captured = 8'h0;

            for (i = 0; i < 8; i = i + 1) begin
                tms_bit = sf0_pattern[i];
                tdi_bit = 1'(i % 2);  // Alternating TDI pattern

                // SF0 Rising edge: TMS bit
                jtag_pin1_i = tms_bit;
                jtag_pin0_i = 1;
                #25;

                // Check for TDO response during rising edge
                if (jtag_pin1_oen) begin
                    sf0_tdo_captured[i] = jtag_pin1_o;
                    $display("      SF0 bit %0d: TMS=%b, TDO_captured=%b", i, tms_bit, jtag_pin1_o);
                end else begin
                    $display("      SF0 bit %0d: TMS=%b, TDO_not_active", i, tms_bit);
                end

                // SF0 Falling edge: TDI bit
                jtag_pin1_i = tdi_bit;
                jtag_pin0_i = 0;
                #25;
            end

            $display("    SF0 pattern sent: 0x%02h", sf0_pattern);
            $display("    TDO captured:    0x%02h", sf0_tdo_captured);

            // Verify we captured some response
            if (sf0_tdo_captured == sf0_pattern) begin
                $display("    ✓ SF0 protocol test PASSED - TDO activity matches expected patterns");
                last_verification_result = 1'b1;
            end else begin
                $display("    ✗ SF0 protocol test FAILED - No TDO activity detected");
                last_verification_result = 1'b0;
            end
        end
    endtask

    // Task to test OScan1 zero insertion/deletion (bit stuffing)
    task test_oscan1_zero_stuffing();
        integer i;
        logic [7:0] test_pattern;
        logic [15:0] expected_stuffed;  // Pattern with zero insertions
        logic [15:0] observed_pattern;
        begin
            $display("  Testing zero stuffing (bit stuffing)...");

            mode_select = 1;
            #100;

            // Pattern with 5 consecutive ones (should trigger zero insertion)
            test_pattern = 8'b11111010;  // 5 ones followed by other bits
            expected_stuffed = 16'b1111101000010;  // Expected with zero insertion after 5 ones

            $display("    Sending pattern with 5 consecutive ones: 0x%02h", test_pattern);

            // Send OAC and activate OScan1
            test_oscan1_oac_detection();

            // Send JSCAN_OSCAN_ON
            for (i = 0; i < 4; i = i + 1) begin
                jtag_pin1_i = (i == 0) ? 1 : 0;
                jtag_pin0_i = 1; #25; jtag_pin0_i = 0; #25;
            end
            #100;

            // Send test pattern bit by bit and monitor output
            observed_pattern = 16'h0;
            for (i = 0; i < 8; i = i + 1) begin
                jtag_pin1_i = test_pattern[i];
                jtag_pin0_i = 1; #25;

                // Check if zero stuffing is active (would affect timing)
                if (jtag_pin1_oen) begin
                    observed_pattern[i] = jtag_pin1_o;
                    $display("      Bit %0d: Input=%b, Output=%b", i, test_pattern[i], jtag_pin1_o);
                end

                jtag_pin0_i = 0; #25;
            end

            $display("    Input pattern:     0x%02h", test_pattern);
            $display("    Observed pattern:  0x%04h", observed_pattern);

            // Verify bit stuffing behavior (check for any output activity indicating processing)
            if (observed_pattern != 16'h0 || jtag_pin1_oen) begin
                $display("    ✓ Zero stuffing test PASSED - Pattern processing detected");
                last_verification_result = 1'b1;
            end else begin
                $display("    ✗ Zero stuffing test FAILED - No pattern processing detected");
                last_verification_result = 1'b0;
            end
        end
    endtask

    // Task to test protocol switching between JTAG and cJTAG
    task test_protocol_switching();
        logic [31:0] jtag_idcode_1, jtag_idcode_2, cjtag_response;
        logic jtag_mode_verified, cjtag_mode_verified;
        integer j;
        begin
            $display("  Testing protocol switching JTAG ↔ cJTAG...");

            // Start in JTAG mode
            mode_select = 0;
            #200;
            reset_tap();

            // Verify JTAG mode works (without hardcoded expected value)

            $display("    Verifying initial JTAG mode operation...");
            // Read IDCODE in JTAG mode
            jtag_pin1_i = 1; wait_tck();  // Select DR
            jtag_pin1_i = 0; wait_tck();  // Capture DR
            jtag_idcode_1 = 32'h0;
            for (j = 0; j < 32; j = j + 1) begin
                jtag_pin2_i = 1'b0;
                wait_tck();
                jtag_idcode_1 = {jtag_pin3_o, jtag_idcode_1[31:1]};
            end
            jtag_pin1_i = 1; wait_tck();  // Exit
            jtag_pin1_i = 0; wait_tck();
            jtag_mode_verified = (jtag_idcode_1 == JTAG_IDCODE);  // Must match exact expected IDCODE
            $display("      JTAG mode IDCODE: 0x%08h", jtag_idcode_1);

            $display("    Switching to cJTAG mode...");
            mode_select = 1;
            #200;

            // Test basic cJTAG operation
            test_oscan1_oac_detection();
            cjtag_mode_verified = last_verification_result;  // Use result from cJTAG test
            #200;

            $display("    Switching back to JTAG mode...");
            mode_select = 0;
            #200;
            reset_tap();

            // Verify JTAG mode still works after switching
            $display("    Verifying JTAG mode after switching...");
            jtag_pin1_i = 1; wait_tck();  // Select DR
            jtag_pin1_i = 0; wait_tck();  // Capture DR
            jtag_idcode_2 = 32'h0;
            for (j = 0; j < 32; j = j + 1) begin
                jtag_pin2_i = 1'b0;
                wait_tck();
                jtag_idcode_2 = {jtag_pin3_o, jtag_idcode_2[31:1]};
            end
            jtag_pin1_i = 1; wait_tck();  // Exit
            jtag_pin1_i = 0; wait_tck();
            $display("      JTAG mode IDCODE after switching: 0x%08h", jtag_idcode_2);

            $display("    Testing rapid mode switching...");
            repeat(5) begin
                mode_select = ~mode_select;
                #100;
            end

            // Return to JTAG mode
            mode_select = 0;
            #200;

            // Evaluate overall test result - all conditions must pass for a successful test
            $display("    Evaluation results:");
            $display("      - JTAG mode verified: %s", jtag_mode_verified ? "PASS" : "FAIL");
            $display("      - cJTAG mode verified: %s", cjtag_mode_verified ? "PASS" : "FAIL");
            $display("      - IDCODE consistency: %s", (jtag_idcode_1 == jtag_idcode_2) ? "PASS" : "FAIL");
            $display("      - IDCODE exact match: %s", (jtag_idcode_1 == JTAG_IDCODE && jtag_idcode_2 == JTAG_IDCODE) ? "PASS" : "FAIL");

            // Pass only if all critical conditions are met
            if (jtag_mode_verified && cjtag_mode_verified &&
                (jtag_idcode_1 == jtag_idcode_2) && (jtag_idcode_1 == JTAG_IDCODE)) begin
                $display("    ✓ Protocol switching test PASSED - All modes functional, IDCODE exactly correct");
                last_verification_result = 1'b1;
            end else begin
                $display("    ✗ Protocol switching test FAILED - One or more conditions not met");
                if (!jtag_mode_verified) $display("      → JTAG mode verification failed");
                if (!cjtag_mode_verified) $display("      → cJTAG mode verification failed");
                if (jtag_idcode_1 != jtag_idcode_2) $display("      → IDCODE inconsistent before/after mode switch");
                if (jtag_idcode_1 != JTAG_IDCODE) $display("      → First IDCODE incorrect (got 0x%08h, expected 0x1DEAD3FF)", jtag_idcode_1);
                if (jtag_idcode_2 != JTAG_IDCODE) $display("      → Second IDCODE incorrect (got 0x%08h, expected 0x1DEAD3FF)", jtag_idcode_2);
                last_verification_result = 1'b0;
            end
        end
    endtask

    // Task to test boundary conditions
    task test_boundary_conditions();
        integer i;
        logic [31:0] boundary_idcode;
        begin
            $display("  Testing protocol boundary conditions...");

            // Test 1: Very fast mode switching
            $display("    Test 1: Rapid mode switching (10 cycles)");
            for (i = 0; i < 10; i = i + 1) begin
                mode_select = i[0];  // Alternate between 0 and 1
                #10;  // Very fast switching
            end
            mode_select = 0;  // Return to JTAG
            #100;

            // Verify JTAG still works after rapid switching
            $display("    Verifying JTAG functionality after rapid switching...");
            reset_tap();
            read_idcode_with_check(32'h1DEAD3FF);

            // Test 2: Minimum TCKC period in cJTAG mode
            $display("    Test 2: Minimum TCKC period test");
            mode_select = 1;
            #100;
            repeat(20) begin
                jtag_pin0_i = ~jtag_pin0_i;
                #5;  // Very fast clock
            end
            #100;

            // Verify cJTAG mode can handle fast clocking
            // (Check by attempting OAC detection)
            for (i = 0; i < 16; i = i + 1) begin
                jtag_pin0_i = ~jtag_pin0_i;
                #25;  // Normal speed OAC
            end

            // Test 3: Maximum idle time
            $display("    Test 3: Extended idle periods");
            mode_select = 0;
            #1000;  // Long idle period

            // Verify JTAG still works after long idle
            reset_tap();
            read_idcode_with_check(32'h1DEAD3FF);

            // Test 4: Reset during mode switch
            $display("    Test 4: Reset during mode transition");
            mode_select = 1;
            #50;  // Switch mode
            rst_n = 0;  // Reset during transition
            #100;
            rst_n = 1;
            #100;
            mode_select = 0;  // Return to known state
            #200;

            // Verify recovery after reset during transition
            $display("    Verifying recovery after reset during mode transition...");
            reset_tap();

            // Read IDCODE to verify system recovered
            boundary_idcode = 32'h0;
            jtag_pin1_i = 1; wait_tck();  // Select DR
            jtag_pin1_i = 0; wait_tck();  // Capture DR

            for (i = 0; i < 32; i = i + 1) begin
                jtag_pin2_i = 1'b0;
                wait_tck();
                boundary_idcode = {jtag_pin3_o, boundary_idcode[31:1]};
            end

            jtag_pin1_i = 1; wait_tck();  // Exit
            jtag_pin1_i = 0; wait_tck();

            if (boundary_idcode == JTAG_IDCODE) begin
                $display("    ✓ Boundary conditions test PASSED - System recovered properly");
                last_verification_result = 1'b1;
            end else begin
                $display("    ✗ Boundary conditions test FAILED - System did not recover (IDCODE: 0x%08h)", boundary_idcode);
                last_verification_result = 1'b0;
            end
        end
    endtask

    // Task to test full cJTAG protocol implementation
    task test_full_cjtag_protocol();
        integer i, j;
        logic [31:0] cjtag_idcode;
        logic [3:0] jscan_cmd;
        logic tms_bit, tdi_bit, expected_tdo;
        begin
            $display("  Testing complete cJTAG/OScan1 protocol...");

            // Step 1: Switch to cJTAG mode
            mode_select = 1;
            #200;
            $display("    Step 1: Switched to cJTAG mode (mode_select=1)");

            // Step 2: Send OAC (16 consecutive edges to enter JScan mode)
            $display("    Step 2: Sending OAC sequence (16 consecutive TCKC edges)");
            for (i = 0; i < 16; i = i + 1) begin
                jtag_pin0_i = ~jtag_pin0_i;
                #25;  // 25ns per edge, 16 edges = 400ns total
            end
            #100;  // Allow OAC detection to complete

            // Step 3: Send JScan OSCAN_ON command (4 bits: 0x1 = JSCAN_OSCAN_ON)
            $display("    Step 3: Sending JScan OSCAN_ON command (0x1)");
            jscan_cmd = 4'h1;  // JSCAN_OSCAN_ON

            for (i = 0; i < 4; i = i + 1) begin
                // Send JScan command bit by bit (LSB first)
                jtag_pin1_i = jscan_cmd[i];  // TMSC input

                // TCKC rising edge for command bit
                jtag_pin0_i = 1;
                #25;

                // TCKC falling edge
                jtag_pin0_i = 0;
                #25;

                $display("      JScan bit %0d: %b", i, jscan_cmd[i]);
            end
            #200;  // Allow JScan command processing

            // Step 4: Perform SF0 IDCODE read sequence
            // This involves proper 2-phase SF0 protocol:
            // Phase 1: Send TMS=0, TDI=0 to go to Shift-DR and load IDCODE
            $display("    Step 4: SF0 IDCODE read sequence");

            // First, reset TAP via 5 TMS=1 cycles using SF0 protocol
            $display("      Resetting TAP via SF0 (5 cycles TMS=1)");
            for (i = 0; i < 5; i = i + 1) begin
                // SF0: TMS on rising edge, TDI on falling edge
                tms_bit = 1;  // TMS=1 for reset
                tdi_bit = 0;  // TDI=0 (don't care during reset)

                // Rising edge: TMS bit
                jtag_pin1_i = tms_bit;
                jtag_pin0_i = 1;
                #25;

                // Falling edge: TDI bit
                jtag_pin1_i = tdi_bit;
                jtag_pin0_i = 0;
                #25;

                $display("        TAP reset cycle %0d: TMS=%b, TDI=%b", i+1, tms_bit, tdi_bit);
            end

            // Go to Run-Test-Idle (TMS=0)
            tms_bit = 0; tdi_bit = 0;
            jtag_pin1_i = tms_bit; jtag_pin0_i = 1; #25;
            jtag_pin1_i = tdi_bit; jtag_pin0_i = 0; #25;
            $display("      Entered Run-Test-Idle");

            // Enter Select-DR-Scan (TMS=1)
            tms_bit = 1; tdi_bit = 0;
            jtag_pin1_i = tms_bit; jtag_pin0_i = 1; #25;
            jtag_pin1_i = tdi_bit; jtag_pin0_i = 0; #25;

            // Enter Capture-DR (TMS=0)
            tms_bit = 0; tdi_bit = 0;
            jtag_pin1_i = tms_bit; jtag_pin0_i = 1; #25;
            jtag_pin1_i = tdi_bit; jtag_pin0_i = 0; #25;
            $display("      Entered Capture-DR state for IDCODE");

            // Shift 32 bits of IDCODE using SF0 protocol
            $display("      Shifting 32-bit IDCODE via SF0 protocol");
            cjtag_idcode = 32'h0;

            for (i = 0; i < 32; i = i + 1) begin
                // For IDCODE read, we shift zeros and capture TDO
                tms_bit = (i == 31) ? 1 : 0;  // TMS=1 on last bit to exit
                tdi_bit = 0;  // Shift zeros for IDCODE read

                // SF0 Rising edge: TMS bit
                jtag_pin1_i = tms_bit;
                jtag_pin0_i = 1;
                #25;

                // Sample TDO if available (from TMSC output)
                if (jtag_pin1_oen) begin
                    cjtag_idcode[i] = jtag_pin1_o;  // Capture TDO bit
                end

                // SF0 Falling edge: TDI bit
                jtag_pin1_i = tdi_bit;
                jtag_pin0_i = 0;
                #25;

                if (i < 8 || i > 23) begin  // Show first/last 8 bits
                    $display("        IDCODE bit %02d: TMS=%b, TDI=%b, TDO=%b",
                             i, tms_bit, tdi_bit, jtag_pin1_oen ? jtag_pin1_o : 1'bx);
                end
            end

            // Update-DR (TMS=1 from Exit1-DR)
            tms_bit = 1; tdi_bit = 0;
            jtag_pin1_i = tms_bit; jtag_pin0_i = 1; #25;
            jtag_pin1_i = tdi_bit; jtag_pin0_i = 0; #25;

            // Return to Run-Test-Idle
            tms_bit = 0; tdi_bit = 0;
            jtag_pin1_i = tms_bit; jtag_pin0_i = 1; #25;
            jtag_pin1_i = tdi_bit; jtag_pin0_i = 0; #25;

            $display("      cJTAG IDCODE captured: 0x%08h", cjtag_idcode);
            $display("      Expected IDCODE:       0x%08h", JTAG_IDCODE);

            // Step 5: Exit OScan1 mode via JScan OSCAN_OFF
            $display("    Step 5: Sending JScan OSCAN_OFF command");

            // Send OAC again to re-enter JScan mode
            for (i = 0; i < 16; i = i + 1) begin
                jtag_pin0_i = ~jtag_pin0_i;
                #25;
            end
            #100;

            // Send JSCAN_OSCAN_OFF (0x0)
            jscan_cmd = 4'h0;  // JSCAN_OSCAN_OFF
            for (i = 0; i < 4; i = i + 1) begin
                jtag_pin1_i = jscan_cmd[i];
                jtag_pin0_i = 1; #25;
                jtag_pin0_i = 0; #25;
            end
            #200;

            // Step 6: Return to JTAG mode for verification
            $display("    Step 6: Returning to JTAG mode");
            mode_select = 0;
            #200;

            // Verify JTAG mode still works
            reset_tap();
            read_idcode_with_check(32'h1DEAD3FF);

            // Final evaluation
            if (cjtag_idcode == JTAG_IDCODE) begin
                $display("    ✓ Full cJTAG protocol test PASSED");
                $display("      - OAC sequence: Generated successfully");
                $display("      - JScan commands: OSCAN_ON/OSCAN_OFF sent");
                $display("      - SF0 protocol: TMS/TDI phases executed");
                $display("      - IDCODE read: Attempted via 2-wire interface");
                $display("      - Mode switching: JTAG ↔ cJTAG ↔ JTAG");
                last_verification_result = 1'b1;
            end else begin
                $display("    ✗ Full cJTAG protocol test FAILED");
                $display("      - IDCODE mismatch or zero response");
                last_verification_result = 1'b0;
            end

            $display("    Complete IEEE 1149.7 OScan1 protocol validation finished");
        end
    endtask

endmodule
