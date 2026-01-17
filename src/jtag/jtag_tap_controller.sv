/**
 * JTAG TAP Controller
 * Implements the JTAG Test Access Port state machine according to IEEE 1149.1
 */

module jtag_tap_controller (
    input  logic       clk,
    input  logic       rst_n,
    input  logic       tms,      // Test Mode Select
    output logic [3:0] state,    // Current TAP state (for visibility)

    // Control signals to shift registers
    output logic       shift_dr,
    output logic       shift_ir,
    output logic       update_dr,
    output logic       update_ir,
    output logic       capture_dr,
    output logic       capture_ir,

    // TAP reset signal (asserted when in TEST_LOGIC_RESET state)
    output logic       tap_reset
);

    // TAP Controller States
    typedef enum logic [3:0] {
        TEST_LOGIC_RESET = 4'h0,
        RUN_TEST_IDLE    = 4'h1,
        DR_SELECT_SCAN   = 4'h2,
        DR_CAPTURE       = 4'h3,
        DR_SHIFT         = 4'h4,
        DR_EXIT1         = 4'h5,
        DR_PAUSE         = 4'h6,
        DR_EXIT2         = 4'h7,
        DR_UPDATE        = 4'h8,
        IR_SELECT_SCAN   = 4'h9,
        IR_CAPTURE       = 4'hA,
        IR_SHIFT         = 4'hB,
        IR_EXIT1         = 4'hC,
        IR_PAUSE         = 4'hD,
        IR_EXIT2         = 4'hE,
        IR_UPDATE        = 4'hF
    } tap_state_t;

    tap_state_t current_state, next_state;

    // TAP State Machine
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            current_state <= TEST_LOGIC_RESET;
`ifdef VERBOSE
            if (`VERBOSE) $display("[TAP] Reset to TEST_LOGIC_RESET");
`endif
        end else begin
            current_state <= next_state;
            if (next_state != current_state) begin
`ifdef VERBOSE
                if (`VERBOSE) $display("[TAP] State transition: %s (%h) -> %s (%h) [TMS=%b]",
                    get_state_name(current_state), current_state,
                    get_state_name(next_state), next_state, tms);
`endif
            end
        end
    end

    // Next state logic
    always_comb begin
        next_state = current_state;

        case (current_state)
            TEST_LOGIC_RESET: next_state = tms ? TEST_LOGIC_RESET : RUN_TEST_IDLE;
            RUN_TEST_IDLE:    next_state = tms ? DR_SELECT_SCAN : RUN_TEST_IDLE;

            DR_SELECT_SCAN:   next_state = tms ? IR_SELECT_SCAN : DR_CAPTURE;
            DR_CAPTURE:       next_state = tms ? DR_EXIT1 : DR_SHIFT;
            DR_SHIFT:         next_state = tms ? DR_EXIT1 : DR_SHIFT;
            DR_EXIT1:         next_state = tms ? DR_UPDATE : DR_PAUSE;
            DR_PAUSE:         next_state = tms ? DR_EXIT2 : DR_PAUSE;
            DR_EXIT2:         next_state = tms ? DR_UPDATE : DR_SHIFT;
            DR_UPDATE:        next_state = tms ? DR_SELECT_SCAN : RUN_TEST_IDLE;

            IR_SELECT_SCAN:   next_state = tms ? TEST_LOGIC_RESET : IR_CAPTURE;
            IR_CAPTURE:       next_state = tms ? IR_EXIT1 : IR_SHIFT;
            IR_SHIFT:         next_state = tms ? IR_EXIT1 : IR_SHIFT;
            IR_EXIT1:         next_state = tms ? IR_UPDATE : IR_PAUSE;
            IR_PAUSE:         next_state = tms ? IR_EXIT2 : IR_PAUSE;
            IR_EXIT2:         next_state = tms ? IR_UPDATE : IR_SHIFT;
            IR_UPDATE:        next_state = tms ? DR_SELECT_SCAN : RUN_TEST_IDLE;

            default:          next_state = TEST_LOGIC_RESET;
        endcase
    end

    // Control signal generation
    assign shift_dr  = (current_state == DR_SHIFT) || (current_state == DR_EXIT2 && next_state == DR_SHIFT);
    assign shift_ir  = (current_state == IR_SHIFT) || (current_state == IR_EXIT2 && next_state == IR_SHIFT);
    assign update_dr = (current_state == DR_UPDATE);
    assign update_ir = (current_state == IR_UPDATE);
    assign capture_dr = (current_state == DR_CAPTURE);
    assign capture_ir = (current_state == IR_CAPTURE);

    // Export state for debugging
    assign state = current_state;

    // TAP reset signal generation (active when in TEST_LOGIC_RESET state)
    assign tap_reset = (current_state == TEST_LOGIC_RESET);

`ifdef VERBOSE
    // Enhanced debug output for TAP control signals
    always @(posedge clk) begin
        if (`VERBOSE) begin
            if (capture_ir) $display("[TAP] *** CAPTURE_IR asserted *** state=%s (%h)", get_state_name(current_state), current_state);
            if (shift_ir)   $display("[TAP] *** SHIFT_IR asserted *** state=%s (%h)", get_state_name(current_state), current_state);
            if (update_ir)  $display("[TAP] *** UPDATE_IR asserted *** state=%s (%h)", get_state_name(current_state), current_state);
            if (capture_dr) $display("[TAP] *** CAPTURE_DR asserted *** state=%s (%h)", get_state_name(current_state), current_state);
            if (shift_dr)   $display("[TAP] *** SHIFT_DR asserted *** state=%s (%h)", get_state_name(current_state), current_state);
            if (update_dr)  $display("[TAP] *** UPDATE_DR asserted *** state=%s (%h)", get_state_name(current_state), current_state);
        end
    end

    // State name function for readable debug output
    function string get_state_name(tap_state_t state);
        case (state)
            TEST_LOGIC_RESET: return "TEST_LOGIC_RESET";
            RUN_TEST_IDLE:    return "RUN_TEST_IDLE";
            DR_SELECT_SCAN:   return "DR_SELECT_SCAN";
            DR_CAPTURE:       return "DR_CAPTURE";
            DR_SHIFT:         return "DR_SHIFT";
            DR_EXIT1:         return "DR_EXIT1";
            DR_PAUSE:         return "DR_PAUSE";
            DR_EXIT2:         return "DR_EXIT2";
            DR_UPDATE:        return "DR_UPDATE";
            IR_SELECT_SCAN:   return "IR_SELECT_SCAN";
            IR_CAPTURE:       return "IR_CAPTURE";
            IR_SHIFT:         return "IR_SHIFT";
            IR_EXIT1:         return "IR_EXIT1";
            IR_PAUSE:         return "IR_PAUSE";
            IR_EXIT2:         return "IR_EXIT2";
            IR_UPDATE:        return "IR_UPDATE";
            default:          return "UNKNOWN";
        endcase
    endfunction
`endif

endmodule
