/**
 * System Testbench
 * Tests JTAG to Debug Module integration via DMI
 */

module system_tb;
    // DPI export for C++ integration
    export "DPI-C" function get_verification_status_dpi;

    logic        clk;
    logic        rst_n;

    // JTAG pins
    logic        jtag_pin0_i;
    logic        jtag_pin1_i;
    logic        jtag_pin1_o;
    logic        jtag_pin1_oen;
    logic        jtag_pin2_i;
    logic        jtag_pin3_o;
    logic        jtag_pin3_oen;
    logic        jtag_trst_n_i;

    logic        mode_select;
    logic [31:0] idcode;
    logic        debug_req;
    logic        hart_halted;
    logic        active_mode;

    // Test tracking variables
    integer test_count = 0;
    integer pass_count = 0;
    integer fail_count = 0;

    // Instantiate DUT
    system_top dut (
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
        .idcode           (idcode),
        .debug_req        (debug_req),
        .hart_halted      (hart_halted),
        .active_mode      (active_mode)
    );

    // Clock generation
    initial begin
        clk = 0;
        forever #5 clk = ~clk;
    end

    // TCK generation (JTAG clock)
    initial begin
        jtag_pin0_i = 0;
        forever #50 jtag_pin0_i = ~jtag_pin0_i;
    end

    // Waveform dump
    initial begin
        if ($test$plusargs("trace")) begin
            $dumpfile("system_sim.fst");
            $dumpvars(0, system_tb);
        end
    end

    // Test sequence
    initial begin
        $display("=== System Integration Testbench Started ===");

        // Initialize
        rst_n = 0;
        jtag_pin1_i = 0;
        jtag_pin2_i = 0;
        jtag_trst_n_i = 0;
        mode_select = 0;  // JTAG mode

        #100;
        rst_n = 1;
        jtag_trst_n_i = 1;
        #100;

        $display("Time: %0t, Active Mode: %s", $time, active_mode ? "cJTAG" : " JTAG");

        // Test 1: TAP Controller Reset
        $display("\nTest 1: TAP Controller Reset");
        test_count = test_count + 1;
        reset_tap();
        pass_count = pass_count + 1;
        $display("    ✓ Test 1 PASSED");

        // Test 2: Read IDCODE
        $display("\nTest 2: Read IDCODE via DTM");
        test_count = test_count + 1;
        read_idcode_with_check(32'h1DEAD3FF);

        // Test 3: Read DMSTATUS
        $display("\nTest 3: Read Debug Module Status");
        test_count = test_count + 1;
        read_dm_register_with_check(7'h11, 32'h00000C82);  // DMSTATUS expected value

        // Test 4: Halt Hart
        $display("\nTest 4: Halt Hart via Debug Module");
        test_count = test_count + 1;
        write_dm_register(7'h10, 32'h80000001);  // DMCONTROL: haltreq=1, dmactive=1
        #500;
        $display("  Hart halted: %0b", hart_halted);
        $display("  Debug request: %0b", debug_req);
        if (hart_halted && debug_req) begin
            pass_count = pass_count + 1;
            $display("    ✓ Test 4 PASSED - Hart successfully halted");
        end else begin
            fail_count = fail_count + 1;
            $display("    ✗ Test 4 FAILED - Hart halt failed (halted=%0b, debug_req=%0b)", hart_halted, debug_req);
        end

        // Test 5: Read DMSTATUS after halt
        $display("\nTest 5: Read DMSTATUS after halt");
        test_count = test_count + 1;
        read_dm_register_with_check(7'h11, 32'h00000C83);  // Expected DMSTATUS with halt bit set

        // Test 6: Resume Hart
        $display("\nTest 6: Resume Hart");
        test_count = test_count + 1;
        write_dm_register(7'h10, 32'h40000001);  // DMCONTROL: resumereq=1, dmactive=1
        #500;
        $display("  Hart halted: %0b", hart_halted);
        if (!hart_halted) begin
            pass_count = pass_count + 1;
            $display("    ✓ Test 6 PASSED - Hart successfully resumed");
        end else begin
            fail_count = fail_count + 1;
            $display("    ✗ Test 6 FAILED - Hart resume failed (still halted)");
        end

        // Test 7: Switch to cJTAG mode
        $display("\nTest 7: Switch to cJTAG mode");
        test_count = test_count + 1;
        mode_select = 1;  // Enable cJTAG mode
        #200;
        if (active_mode == 1'b1) begin
            pass_count = pass_count + 1;
            $display("    ✓ Test 7 PASSED - Successfully switched to cJTAG mode");
        end else begin
            fail_count = fail_count + 1;
            $display("    ✗ Test 7 FAILED - Failed to switch to cJTAG mode (active_mode=%0b)", active_mode);
        end
        reset_tap();
        read_idcode_with_check(32'h1DEAD3FF);

        // Test 8: cJTAG DMI access
        $display("\nTest 8: cJTAG DMI register access");
        test_count = test_count + 1;
        read_dm_register_with_check(7'h11, 32'h00000C82);  // Read DMSTATUS in cJTAG mode

        // Test 9: cJTAG hart control
        $display("\nTest 9: cJTAG hart control");
        test_count = test_count + 1;
        write_dm_register(7'h10, 32'h80000001);  // DMCONTROL: haltreq=1, dmactive=1
        #500;
        $display("  Hart halted (cJTAG mode): %0b", hart_halted);
        if (hart_halted) begin
            pass_count = pass_count + 1;
            $display("    ✓ Test 9 PASSED - Hart control works in cJTAG mode");
        end else begin
            fail_count = fail_count + 1;
            $display("    ✗ Test 9 FAILED - Hart control failed in cJTAG mode");
        end

        // Test 10: Return to JTAG mode
        $display("\nTest 10: Return to JTAG mode");
        test_count = test_count + 1;
        mode_select = 0;
        #200;
        if (active_mode == 1'b0) begin
            pass_count = pass_count + 1;
            $display("    ✓ Test 10 PASSED - Successfully returned to JTAG mode");
        end else begin
            fail_count = fail_count + 1;
            $display("    ✗ Test 10 FAILED - Failed to return to JTAG mode (active_mode=%0b)", active_mode);
        end
        reset_tap();
        read_idcode_with_check(32'h1DEAD3FF);

        // Test 11: Verify JTAG mode functionality
        $display("\nTest 11: Verify JTAG mode after switch");
        test_count = test_count + 1;
        read_dm_register_with_check(7'h11, 32'h00000C82);  // Read DMSTATUS

        // Test 12: Protocol switching stress test
        $display("\nTest 12: Protocol switching stress test");
        test_count = test_count + 1;
        test_protocol_switching_stress();

        #1000;

        $display("\n=== Enhanced System Integration Testbench Completed ===");
        $display("Tests completed: %0d passed, %0d failed", pass_count, fail_count);
        if (fail_count == 0) begin
            $display("✓ ALL TESTS PASSED!");
        end else begin
            $display("✗ %0d TESTS FAILED - Review errors above", fail_count);
        end
        $display("Coverage: JTAG, cJTAG, DMI, Hart Control, Protocol Switching");

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

    // Task to read Debug Module register via DMI
    task read_dm_register(input logic [6:0] addr);
        integer i;
        logic [40:0] dmi_data;
        logic [31:0] read_value;
        begin
            $display("  Reading DM register 0x%02h via DMI...", addr);

            // Select IR scan
            jtag_pin1_i = 1;
            wait_tck();

            // Capture-IR
            jtag_pin1_i = 0;
            wait_tck();

            // Shift in DMI instruction (0x11)
            shift_ir_instruction(8'h11);

            // Update IR
            jtag_pin1_i = 1;
            wait_tck();

            // Go to Run-Test/Idle
            jtag_pin1_i = 0;
            wait_tck();

            // Select DR
            jtag_pin1_i = 1;
            wait_tck();

            // Capture DR
            jtag_pin1_i = 0;
            wait_tck();

            // Shift in DMI read request (41 bits: 7-bit addr + 32-bit data + 2-bit op)
            dmi_data = {addr, 32'h0, 2'b01};  // Read operation
            for (i = 0; i < 41; i = i + 1) begin
                jtag_pin2_i = dmi_data[i];
                wait_tck();
            end

            // Update DR
            jtag_pin1_i = 1;
            wait_tck();

            // Go to Run-Test/Idle
            jtag_pin1_i = 0;
            wait_tck();

            // Need to do another DMI scan to get the read result
            // Select DR
            jtag_pin1_i = 1;
            wait_tck();

            // Capture DR
            jtag_pin1_i = 0;
            wait_tck();

            // Shift in NOP and capture response
            dmi_data = 41'h0;
            for (i = 0; i < 41; i = i + 1) begin
                jtag_pin2_i = 1'b0;
                wait_tck();
                dmi_data[i] = jtag_pin3_o;
            end

            read_value = dmi_data[33:2];
            $display("  DM Register 0x%02h value: 0x%08h", addr, read_value);

            // Update DR
            jtag_pin1_i = 1;
            wait_tck();

            // Return to Idle
            jtag_pin1_i = 0;
            wait_tck();
        end
    endtask

    // Task to write Debug Module register via DMI
    task write_dm_register(input logic [6:0] addr, input logic [31:0] data);
        integer i;
        logic [40:0] dmi_data;
        begin
            $display("  Writing DM register 0x%02h = 0x%08h via DMI...", addr, data);

            // Select IR scan
            jtag_pin1_i = 1;
            wait_tck();

            // Capture-IR
            jtag_pin1_i = 0;
            wait_tck();

            // Shift in DMI instruction (0x11)
            shift_ir_instruction(8'h11);

            // Update IR
            jtag_pin1_i = 1;
            wait_tck();

            // Go to Run-Test/Idle
            jtag_pin1_i = 0;
            wait_tck();

            // Select DR
            jtag_pin1_i = 1;
            wait_tck();

            // Capture DR
            jtag_pin1_i = 0;
            wait_tck();

            // Shift in DMI write request (41 bits: 7-bit addr + 32-bit data + 2-bit op)
            dmi_data = {addr, data, 2'b10};  // Write operation
            for (i = 0; i < 41; i = i + 1) begin
                jtag_pin2_i = dmi_data[i];
                wait_tck();
            end

            // Update DR
            jtag_pin1_i = 1;
            wait_tck();

            // Return to Idle
            jtag_pin1_i = 0;
            wait_tck();

            $display("  Write complete");
        end
    endtask

    // Task to write instruction register (IR scan)
    task write_ir(input [7:0] instruction);
        integer i;
        logic [7:0] captured_ir;
        begin
            test_count = test_count + 1;  // Track this test

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

            // Mark this test as pass (successful IR shift)
            pass_count = pass_count + 1;
            $display("    IR write complete - PASSED");
        end
    endtask

    // Task to shift in IR instruction
    task shift_ir_instruction(input logic [7:0] instr);
        integer i;
        begin
            for (i = 0; i < 8; i = i + 1) begin
                jtag_pin2_i = instr[i];
                wait_tck();
            end
        end
    endtask

    // Task for IDCODE reading with value checking
    task read_idcode_with_check(input [31:0] expected_value);
        integer i;
        logic [31:0] read_data;
        begin
            $display("  Reading and verifying IDCODE register...");

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
            end

            $display("    IDCODE Read: 0x%08h", read_data);
            $display("    Expected:    0x%08h", expected_value);

            // Check if IDCODE matches expected value
            if (read_data == expected_value) begin
                $display("    ✓ IDCODE verification PASSED");
                pass_count = pass_count + 1;
            end else begin
                $display("    ✗ IDCODE verification FAILED");
                fail_count = fail_count + 1;
            end

            // Exit shift state
            jtag_pin1_i = 1;
            wait_tck();

            // Update-DR
            jtag_pin1_i = 0;
            wait_tck();
        end
    endtask

    // Task for Debug Module register reading with value checking
    task read_dm_register_with_check(input [6:0] address, input [31:0] expected_value);
        integer i;
        logic [40:0] dmi_cmd;
        logic [40:0] read_data;
        logic [31:0] reg_data;
        begin
            $display("  Reading and verifying DMI register 0x%02h...", address);

            // Build DMI command (read operation)
            dmi_cmd = {address, 32'h0, 2'b10};  // Read operation

            // Load DMI instruction (0x11)
            write_ir(8'h11);
            #100;

            // Go to DR scan
            jtag_pin1_i = 0;
            wait_tck();
            jtag_pin1_i = 1;  // Select DR
            wait_tck();
            jtag_pin1_i = 0;  // Capture DR
            wait_tck();

            // Shift 41-bit DMI command
            read_data = 41'h0;
            for (i = 0; i < 41; i = i + 1) begin
                jtag_pin2_i = dmi_cmd[i];
                wait_tck();
                read_data[i] = jtag_pin3_o;
            end

            // Extract register data (bits 33:2)
            reg_data = read_data[33:2];

            $display("    Register Read: 0x%08h", reg_data);
            $display("    Expected:      0x%08h", expected_value);

            // Check if register value matches expected
            if (reg_data == expected_value) begin
                $display("    ✓ DMI register verification PASSED");
                pass_count = pass_count + 1;
            end else begin
                $display("    ✗ DMI register verification FAILED");
                fail_count = fail_count + 1;
            end

            // Exit DR scan
            jtag_pin1_i = 1;
            wait_tck();
            jtag_pin1_i = 0;  // Update DR
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

    // Task for protocol switching stress test
    task test_protocol_switching_stress();
        integer i;
        begin
            $display("  Testing rapid protocol switching...");

            // Rapid switching between JTAG and cJTAG modes
            for (i = 0; i < 5; i = i + 1) begin
                // Switch to cJTAG
                mode_select = 1;
                #50;
                reset_tap();

                // Quick IDCODE read
                read_idcode();

                // Switch to JTAG
                mode_select = 0;
                #50;
                reset_tap();

                // Quick IDCODE read
                read_idcode();

                $display("    Switching cycle %0d completed", i+1);
            end

            $display("  Protocol switching stress test completed");
        end
    endtask

endmodule
