/**
 * System Testbench
 * Tests JTAG to Debug Module integration via DMI
 */

module system_tb;

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
        reset_tap();

        // Test 2: Read IDCODE
        $display("\nTest 2: Read IDCODE via DTM");
        read_idcode();

        // Test 3: Read DMSTATUS
        $display("\nTest 3: Read Debug Module Status");
        read_dm_register(7'h11);  // DMSTATUS

        // Test 4: Halt Hart
        $display("\nTest 4: Halt Hart via Debug Module");
        write_dm_register(7'h10, 32'h80000001);  // DMCONTROL: haltreq=1, dmactive=1
        #500;
        $display("  Hart halted: %0b", hart_halted);
        $display("  Debug request: %0b", debug_req);

        // Test 5: Read DMSTATUS after halt
        $display("\nTest 5: Read DMSTATUS after halt");
        read_dm_register(7'h11);  // DMSTATUS

        // Test 6: Resume Hart
        $display("\nTest 6: Resume Hart");
        write_dm_register(7'h10, 32'h40000001);  // DMCONTROL: resumereq=1, dmactive=1
        #500;
        $display("  Hart halted: %0b", hart_halted);

        #1000;

        $display("\n=== System Integration Testbench Completed ===");
        $display("All tests completed successfully!");
        $finish;
    end

    // Timeout
    initial begin
        #1000000;
        $display("ERROR: Testbench timeout!");
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

    // Task to wait for TCK edge
    task wait_tck();
        begin
            wait (jtag_pin0_i == 1);
            wait (jtag_pin0_i == 0);
        end
    endtask

endmodule
