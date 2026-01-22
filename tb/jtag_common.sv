/**
 * JTAG Common Tasks
 * Shared testbench tasks for JTAG operations
 * Include in testbenches with: `include "jtag_common.sv"
 */

// Task to wait for TCK edge
task wait_tck();
    begin
        wait (jtag_pin0_i == 1);
        wait (jtag_pin0_i == 0);
    end
endtask

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

// Task to read IDCODE with proper verification
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

        // Shift IDCODE (shift 32 bits) - use same method as legacy function
        read_data = 32'h0;
        for (i = 0; i < 32; i = i + 1) begin
            jtag_pin2_i = 1'b0;
            jtag_pin1_i = (i == 31) ? 1 : 0;  // TMS=1 on last bit to exit
            wait_tck();
            // Small delay to ensure TDO is stable
            #1;
            read_data = {jtag_pin3_o, read_data[31:1]};
        end

        $display("    IDCODE Read: 0x%08h", read_data);
        $display("    Expected:    0x%08h", expected_value);

        // Check if IDCODE matches expected value and set global result
        if (read_data == expected_value) begin
            $display("    ✓ IDCODE verification PASSED");
            last_verification_result = 1'b1;
        end else begin
            $display("    ✗ IDCODE verification FAILED");
            last_verification_result = 1'b0;
        end

        // Exit shift state (same as legacy function)
        jtag_pin1_i = 1;
        wait_tck();

        // Update-DR
        jtag_pin1_i = 0;
        wait_tck();
    end
endtask

// Task to perform proper cJTAG IDCODE read
task read_cjtag_idcode_with_check(input [31:0] expected_value);
    integer i, j;
    logic [31:0] cjtag_idcode;
    logic [3:0] jscan_cmd;
    logic tms_bit, tdi_bit, tdo_captured;
    logic cjtag_working;
    begin
        $display("  Testing cJTAG IDCODE read via OScan1 protocol...");

        // Switch to cJTAG mode
        mode_select = 1;
        #200;
        $display("    Switched to cJTAG mode, Active Mode: %s", active_mode ? "cJTAG" : "JTAG");

        // Send OAC (16 consecutive edges)
        $display("    Sending OAC sequence...");
        for (i = 0; i < 16; i = i + 1) begin
            jtag_pin0_i = ~jtag_pin0_i;
            #25;
        end
        #100;

        // Send JSCAN_OSCAN_ON command (4 bits = 0x1)
        jscan_cmd = 4'h1;  // JSCAN_OSCAN_ON
        $display("    Sending JSCAN_OSCAN_ON command...");
        for (i = 0; i < 4; i = i + 1) begin
            jtag_pin1_i = jscan_cmd[i];  // Send LSB first
            jtag_pin0_i = 1; #25;
            jtag_pin0_i = 0; #25;
        end
        #200;

        // Perform TAP reset via SF0
        $display("    Resetting TAP via SF0 protocol...");
        for (i = 0; i < 5; i = i + 1) begin
            // SF0: TMS on rising edge, TDI on falling edge
            tms_bit = 1; tdi_bit = 0;  // TMS=1 for reset
            jtag_pin1_i = tms_bit; jtag_pin0_i = 1; #25;
            jtag_pin1_i = tdi_bit; jtag_pin0_i = 0; #25;
        end

        // Go to Run-Test-Idle
        tms_bit = 0; tdi_bit = 0;
        jtag_pin1_i = tms_bit; jtag_pin0_i = 1; #25;
        jtag_pin1_i = tdi_bit; jtag_pin0_i = 0; #25;

        // Go to DR scan for IDCODE (should be default after reset)
        // But let's explicitly load IDCODE instruction first

        // Go to IR scan to load IDCODE instruction
        tms_bit = 1; tdi_bit = 0;  // Select-DR to Select-IR
        jtag_pin1_i = tms_bit; jtag_pin0_i = 1; #25;
        jtag_pin1_i = tdi_bit; jtag_pin0_i = 0; #25;

        // Enter Capture-IR (TMS=0)
        tms_bit = 0; tdi_bit = 0;
        jtag_pin1_i = tms_bit; jtag_pin0_i = 1; #25;
        jtag_pin1_i = tdi_bit; jtag_pin0_i = 0; #25;

        // Shift IDCODE instruction (0x01) - 5 bits
        for (j = 0; j < 5; j = j + 1) begin
            tms_bit = (j == 4) ? 1 : 0;  // Exit on last bit
            tdi_bit = (j == 0) ? 1 : 0;  // Send 0x01 (bit 0 = 1, others = 0)
            jtag_pin1_i = tms_bit; jtag_pin0_i = 1; #25;
            jtag_pin1_i = tdi_bit; jtag_pin0_i = 0; #25;
        end

        // Update-IR (TMS=1 from Exit1-IR)
        tms_bit = 1; tdi_bit = 0;
        jtag_pin1_i = tms_bit; jtag_pin0_i = 1; #25;
        jtag_pin1_i = tdi_bit; jtag_pin0_i = 0; #25;

        // Return to Run-Test-Idle (TMS=0)
        tms_bit = 0; tdi_bit = 0;
        jtag_pin1_i = tms_bit; jtag_pin0_i = 1; #25;
        jtag_pin1_i = tdi_bit; jtag_pin0_i = 0; #25;

        // Now go to DR scan for IDCODE
        tms_bit = 1; tdi_bit = 0;
        jtag_pin1_i = tms_bit; jtag_pin0_i = 1; #25;
        jtag_pin1_i = tdi_bit; jtag_pin0_i = 0; #25;

        // Enter Capture-DR (TMS=0)
        tms_bit = 0; tdi_bit = 0;
        jtag_pin1_i = tms_bit; jtag_pin0_i = 1; #25;
        jtag_pin1_i = tdi_bit; jtag_pin0_i = 0; #25;

        // Shift 32 bits of IDCODE using proper SF0 protocol
        $display("    Shifting IDCODE via SF0...");
        cjtag_idcode = 32'h0;
        cjtag_working = 1'b0;

        for (i = 0; i < 32; i = i + 1) begin
            tms_bit = (i == 31) ? 1 : 0;  // TMS=1 on last bit to exit
            tdi_bit = 0;  // Shift zeros for IDCODE read

            // SF0 Rising edge: send TMS bit
            jtag_pin1_i = tms_bit;
            jtag_pin0_i = 1;
            #25;

            // Wait for potential TDO response (give more time)
            #25;

            // Sample TDO if available (check both output enable and data)
            if (jtag_pin1_oen == 1'b0) begin  // oen is active low
                tdo_captured = jtag_pin1_o;
                cjtag_idcode = {tdo_captured, cjtag_idcode[31:1]};  // Shift right (LSB first)
                cjtag_working = 1'b1;  // We got some activity
                if (i < 8 || i > 23) begin  // Show first/last 8 bits for debug
                    $display("      IDCODE bit %02d: TMS=%b, TDO=%b (captured)", i, tms_bit, tdo_captured);
                end
            end else begin
                // No output, use zero
                cjtag_idcode = {1'b0, cjtag_idcode[31:1]};  // Shift right with zero
                if (i < 4) begin  // Show first few bits for debug
                    $display("      IDCODE bit %02d: TMS=%b, TDO=0 (no output)", i, tms_bit);
                end
            end

            // SF0 Falling edge: send TDI bit
            jtag_pin1_i = tdi_bit;
            jtag_pin0_i = 0;
            #25;
        end

        // Exit to Update-DR
        tms_bit = 1; tdi_bit = 0;
        jtag_pin1_i = tms_bit; jtag_pin0_i = 1; #25;
        jtag_pin1_i = tdi_bit; jtag_pin0_i = 0; #25;

        // Return to Run-Test-Idle
        tms_bit = 0; tdi_bit = 0;
        jtag_pin1_i = tms_bit; jtag_pin0_i = 1; #25;
        jtag_pin1_i = tdi_bit; jtag_pin0_i = 0; #25;

        // Check results
        $display("    cJTAG IDCODE: 0x%08h", cjtag_idcode);
        $display("    Expected:     0x%08h", expected_value);

        // More lenient verification - check if we got valid cJTAG activity
        if (cjtag_idcode == expected_value) begin
            $display("    ✓ cJTAG IDCODE verification PASSED");
            last_verification_result = 1'b1;
        end else begin
            $display("    ✗ cJTAG IDCODE verification FAILED");
            last_verification_result = 1'b0;
        end
    end
endtask

// Task to write instruction register with verification
task write_ir_with_check(input [4:0] instruction);
    integer i;
    logic [4:0] captured_ir;
    logic [4:0] ir_value;
    logic [4:0] expected_value;
    logic capture_passed;
    logic load_passed;
    begin
        ir_value = instruction[4:0];
        expected_value = 5'h01;  // IR capture always returns 0x01 per IEEE 1149.1
        $display("  Writing and verifying IR: 0x%02h (capture will be: 0x%02h)", instruction, expected_value);

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

        // Transition from Capture-IR to Shift-IR (TMS=0)
        jtag_pin1_i = 0;
        wait_tck();

        // Now in Shift-IR state - shift all 5 bits with proper timing
        captured_ir = 5'h0;
        for (i = 0; i < 5; i = i + 1) begin
            jtag_pin2_i = ir_value[i];  // Set TDI for this bit
            jtag_pin1_i = (i == 4) ? 1 : 0;  // TMS=1 on last bit to exit
            captured_ir = {jtag_pin3_o, captured_ir[4:1]};
            wait_tck();
        end
        $display("    Captured IR: 0x%02h", captured_ir);

        // Now in Exit1-IR state, go to Update-IR (TMS=1)
        jtag_pin1_i = 1;
        wait_tck();

        // Return to Run-Test/Idle (TMS=0 from Update-IR)
        jtag_pin1_i = 0;
        wait_tck();

        // Stay in Run-Test/Idle for a few cycles to let instruction take effect
        repeat(5) wait_tck();

        $display("    Wrote IR: 0x%02h, Captured IR: 0x%02h", ir_value, captured_ir);

        // Dual verification: IEEE 1149.1 compliance + actual instruction load
        capture_passed = (captured_ir == expected_value);
        load_passed = (`JTAG_IR_LATCH == ir_value);

        if (capture_passed) begin
            $display("    ✓ IR capture verification PASSED - IEEE 1149.1 pattern (0x%02h)", expected_value);
        end else begin
            $display("    ✗ IR capture verification FAILED - captured: 0x%02h, expected: 0x%02h", captured_ir, expected_value);
        end

        if (load_passed) begin
            $display("    ✓ IR load verification PASSED - instruction loaded (0x%02h)", ir_value);
        end else begin
            $display("    ✗ IR load verification FAILED - ir_latch: 0x%02h, expected: 0x%02h",
                     `JTAG_IR_LATCH, ir_value);
        end

        // Both verifications must pass
        last_verification_result = capture_passed && load_passed;

        // Debug info
        $display("    Debug: ir_out=0x%02h, tap_state=0x%h", `JTAG_IR_OUT, `JTAG_TAP_STATE);
    end
endtask

// Task to write DMI register (41 bits) with proper DMI operation
task write_dm_register(input [6:0] address, input [31:0] write_value);
    // Write a DMI register via the JTAG interface (shift the 41‑bit DMI command)
    integer i;
    logic [40:0] write_data;
    begin
        $display("  Writing DMI register 0x%02h with value 0x%08h via JTAG", address, write_value);

        // First, load IR with DMI instruction (0x11)
        $display("    Loading IR with DMI instruction (0x11)...");
        write_ir_with_check(5'h11);

        // Construct 41‑bit DMI command: [addr:7][data:32][op:2]
        write_data = {address, write_value, 2'b10}; // DMI write op (0x2)

        $display("    DMI command: addr=0x%02h, data=0x%08h, op=0x%01h", address, write_value, 2'b10);
        $display("    41-bit value: 0x%011h", write_data);
        $display("    Checking current IR after load: ir_out=0x%02h", `JTAG_IR_OUT);

        // Start from Run‑Test/Idle
        jtag_pin1_i = 0;
        wait_tck();

        // Go to Select‑DR (TMS=1)
        jtag_pin1_i = 1;
        wait_tck();

        // Go to Capture‑DR (TMS=0)
        jtag_pin1_i = 0;
        wait_tck();

        // Transition from Capture-DR to Shift-DR (TMS=0)
        jtag_pin1_i = 0;
        wait_tck();

        // Now in Shift‑DR state – shift in the DMI command (41 bits)
        $display("    Shifting 41-bit DMI command (LSB first)...");
        for (i = 0; i < 41; i = i + 1) begin
            jtag_pin2_i = write_data[i]; // LSB first
            jtag_pin1_i = (i == 40) ? 1 : 0; // TMS=1 on last bit to exit
            if (i < 5 || i > 38) begin  // Show first 5 and last 2 bits
                $display("      Bit %02d: TDI=%b (TMS=%b)", i, write_data[i], (i == 40) ? 1'b1 : 1'b0);
            end
            wait_tck();
        end

        $display("    All 41 bits shifted, exiting Shift-DR state");

        // Update‑DR (TMS=1 from Exit1‑DR)
        jtag_pin1_i = 1;
        wait_tck();
        $display("    Entered Update-DR state");

        // Return to Run‑Test/Idle (TMS=0 from Update‑DR)
        jtag_pin1_i = 0;
        wait_tck();
        $display("    Returned to Run-Test-Idle");

        // Small delay to let the DUT process the write
        repeat(5) wait_tck();

        $display("    ✓ DMI register write operation completed via JTAG");
    end
endtask

// Helper task that writes a DMI register and verifies the write by reading back
task write_dm_register_with_check(input [6:0] address, input [31:0] write_value);
    begin
        write_dm_register(address, write_value);
        // Read back and verify
        read_dm_register_with_check(address, write_value);
    end
endtask

// Task to read DMI register (41 bits) with proper DMI operation
// DMI reads require 2 transactions:
// 1. First transaction: Send read command
// 2. Second transaction: Get the read result
task read_dm_register_with_check(input [6:0] address, input [31:0] expected_value);
    integer i;
    logic [40:0] write_data, read_data;
    begin
        $display("  Reading 41-bit DMI register with proper DMI operation...");

        // First, ensure IR is loaded with DMI instruction (0x11)
        $display("    Loading IR with DMI instruction (0x11)...");
        write_ir_with_check(5'h11);

        $display("    Checking current IR after load: ir_out=0x%02h", `JTAG_IR_OUT);

        // Transaction 1: Write a DMI read command to the specified address
        // Construct 41-bit DMI command: [addr:7][data:32][op:2]
        write_data = {address, 32'h0, 2'b01}; // DMI read op (0x1)

        $display("    DMI read command: addr=0x%02h, op=read(0x1)", address);

        $display("    Transaction 1: Writing DMI read command (addr=0x%02h, op=read)", address);

        // Start from Run-Test/Idle
        jtag_pin1_i = 0;
        wait_tck();

        // Go to Select-DR (TMS=1)
        jtag_pin1_i = 1;
        wait_tck();

        // Go to Capture-DR (TMS=0)
        jtag_pin1_i = 0;
        wait_tck();

        // Transition from Capture-DR to Shift-DR (TMS=0)
        jtag_pin1_i = 0;
        wait_tck();

        // Now in Shift-DR state - shift in the DMI read command (41 bits)
        for (i = 0; i < 41; i = i + 1) begin
            jtag_pin2_i = write_data[i];  // Shift in LSB first
            jtag_pin1_i = (i == 40) ? 1 : 0;  // TMS=1 on last bit to exit
            wait_tck();
        end

        // Update-DR (TMS=1 from Exit1-DR)
        jtag_pin1_i = 1;
        wait_tck();

        // Return to Run-Test/Idle (TMS=0 from Update-DR)
        jtag_pin1_i = 0;
        wait_tck();

        // Wait for DMI operation to process
        repeat(5) wait_tck();

        $display("    Transaction 2: Reading DMI response...");

        // Transaction 2: Send a NOP to get the read result
        // Construct 41-bit DMI NOP command: [addr:7][data:32][op:2]
        write_data = {7'h0, 32'h0, 2'b00}; // DMI NOP op

        // Go to Select-DR (TMS=1)
        jtag_pin1_i = 1;
        wait_tck();

        // Go to Capture-DR (TMS=0)
        jtag_pin1_i = 0;
        wait_tck();

        // Transition from Capture-DR to Shift-DR (TMS=0)
        // This clock edge shifts out bit 0!
        jtag_pin1_i = 0;
        wait_tck();

        // Capture bit 0 that was shifted out during transition
        #1;
        read_data[0] = jtag_pin3_o;

        // Now in Shift-DR state - shift NOP while capturing response (remaining 40 bits)
        for (i = 1; i <= 40; i = i + 1) begin
            // Setup TDI/TMS for this bit
            jtag_pin2_i = write_data[i];
            jtag_pin1_i = (i == 40) ? 1 : 0;  // TMS=1 on last bit to exit

            // Clock edge happens here
            wait_tck();

            // Capture TDO after the clock edge
            #1;
            read_data[i] = jtag_pin3_o;
        end

        // Update-DR (TMS=1 from Exit1-DR)
        jtag_pin1_i = 1;
        wait_tck();

        // Return to Run-Test/Idle (TMS=0 from Update-DR)
        jtag_pin1_i = 0;
        wait_tck();

        $display("    DMI read response: data=0x%08h, op=%0d", read_data[33:2], read_data[1:0]);
        $display("      Expected Data: 0x%08h", expected_value);

        // Verify the read data matches expected value
        if (read_data[33:2] == expected_value) begin
            $display("    ✓ DMI register read operation successful");
            last_verification_result = 1'b1;
        end else begin
            $display("    ✗ DMI register read operation failed");
            last_verification_result = 1'b0;
        end
    end
endtask

// Task to record failed test
task record_failed_test(input integer test_num, input string test_name);
    if (failed_test_count < MAX_TESTS) begin
        failed_tests[failed_test_count] = test_name;
        failed_test_numbers[failed_test_count] = test_num;
        failed_test_count = failed_test_count + 1;
    end
endtask

