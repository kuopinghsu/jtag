/**
 * Verilator Simulation with VPI Server
 * Interactive JTAG control via TCP/IP socket
 */

#include "Vjtag_vpi_top.h"
#include "Vjtag_vpi_top___024root.h"  // For internal signal access
#include "verilated.h"
#ifndef ENABLE_FST
#define ENABLE_FST 0
#endif
#ifndef ENABLE_VCD
#define ENABLE_VCD 0
#endif
#if ENABLE_FST
#include "verilated_fst_c.h"
#endif
#if ENABLE_VCD
#include "verilated_vcd_c.h"
#endif

#include <iostream>
#include <iomanip>

// Global exit code for VL_USER_FINISH
static int global_exit_code = 0;
static Vjtag_vpi_top* global_top = nullptr;

#ifdef VL_USER_FINISH
// Custom finish handler for VL_USER_FINISH
void vl_finish(const char* filename, int linenum, const char* hier) {
    std::cout << "SystemVerilog $finish called from " << filename << ":" << linenum << std::endl;
    global_exit_code = 1; // Set default error code, will be overridden if needed
    Verilated::threadContextp()->gotFinish(true);
}
#endif
#include "jtag_vpi_server.h"
#include <iostream>
#include <iomanip>
#include <cstring>
#include <chrono>

// Default timeout: 0 = unlimited (no timeout)
// Can be overridden with --timeout parameter (0 = unlimited, >0 = timeout in seconds)
#define DEFAULT_TIMEOUT_SECONDS 0

int main(int argc, char** argv) {
    // Create context
    const std::unique_ptr<VerilatedContext> contextp{new VerilatedContext};
    contextp->commandArgs(argc, argv);

    // Create simulator instance
    Vjtag_vpi_top* top = new Vjtag_vpi_top{contextp.get()};
    global_top = top;  // Set global pointer for VL_USER_FINISH

    // Initialize signals
    top->clk = 0;
    top->rst_n = 0;
    top->jtag_pin0_i = 0;
    top->jtag_pin1_i = 0;
    top->jtag_pin2_i = 0;
    top->jtag_trst_n_i = 0;
    top->mode_select = 0;

    // Initialize VPI server
    JtagVpiServer vpi_server(3333);
    if (!vpi_server.init()) {
        std::cerr << "[VPI] Failed to initialize server on port 3333" << std::endl;
        std::cerr << "[VPI] Make sure port 3333 is not already in use" << std::endl;
        delete top;
        return 1;
    }

    std::cout << "\n=== JTAG VPI Interactive Simulation ===" << std::endl;
    std::cout << "[VPI] Server listening on port 3333" << std::endl;
    std::cout << "[VPI] Waiting for client connections..." << std::endl;
    std::cout << "[VPI] Connect using: ./build/jtag_vpi_client" << std::endl;

    // Parse command line arguments

#if ENABLE_FST || ENABLE_VCD
    void* trace = nullptr;
#endif
    bool trace_enabled = false;
    bool verbose = true;  // Default: show status messages
    bool cjtag_mode = false;  // Default: JTAG mode
    bool msb_first = false;    // Default: LSB-first bit packing
    std::string proto_mode = "auto"; // Default: auto-detect protocol
    uint64_t timeout_seconds = DEFAULT_TIMEOUT_SECONDS;
    int debug_level = 0;  // Default: no debug output

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--trace") {
            trace_enabled = true;
        } else if (arg == "--cjtag") {
            cjtag_mode = true;
        } else if (arg == "--quiet" || arg == "-q") {
            verbose = false;
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "--msb-first") {
            msb_first = true;
        } else if (arg == "--proto" && i + 1 < argc) {
            proto_mode = argv[++i];
        } else if (arg.rfind("--proto=", 0) == 0) {
            proto_mode = arg.substr(8);
        } else if (arg == "--timeout" && i + 1 < argc) {
            // Format: --timeout 60
            timeout_seconds = std::stoull(argv[++i]);
        } else if (arg.substr(0, 10) == "--timeout=") {
            // Format: --timeout=60
            timeout_seconds = std::stoull(arg.substr(10));
        } else if (arg == "--debug" && i + 1 < argc) {
            // Format: --debug 1 or --debug 2
            debug_level = std::stoi(argv[++i]);
        } else if (arg.rfind("--debug=", 0) == 0) {
            // Format: --debug=1
            debug_level = std::stoi(arg.substr(8));
        } else if (arg == "-d" && i + 1 < argc) {
            // Format: -d 1
            debug_level = std::stoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "\nUsage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --trace                  Enable FST waveform tracing" << std::endl;
            std::cout << "  --cjtag                  Enable cJTAG mode (default: JTAG)" << std::endl;
            std::cout << "  --timeout <seconds>      Set simulation timeout (default: unlimited, 0=unlimited)" << std::endl;
            std::cout << "  --timeout=<seconds>      Alternative timeout format" << std::endl;
            std::cout << "  --quiet, -q              Suppress cycle status messages" << std::endl;
            std::cout << "  --verbose, -v            Show cycle status messages (default)" << std::endl;
            std::cout << "  --proto <mode>           Protocol: auto | openocd | legacy (default: auto)" << std::endl;
            std::cout << "  --debug <level>          Debug output: 0=off, 1=basic, 2=verbose (default: 0)" << std::endl;
            std::cout << "  -d <level>               Short form of --debug" << std::endl;
            std::cout << "  --help, -h               Show this help message" << std::endl;
            delete top;
            return 0;
        }
    }

    uint64_t max_cycles = timeout_seconds * 100000000ULL; // 100MHz clock (fallback)
    auto start_time = std::chrono::steady_clock::now();
    auto deadline   = (timeout_seconds == 0) ?
                      std::chrono::steady_clock::time_point::max() :
                      start_time + std::chrono::seconds(timeout_seconds);
    std::cout << "[SIM] Mode: " << (cjtag_mode ? "cJTAG" : "JTAG") << std::endl;
    if (timeout_seconds == 0) {
        std::cout << "[SIM] Timeout: unlimited" << std::endl;
    } else {
        std::cout << "[SIM] Timeout: " << timeout_seconds << "s (wall-clock) | fallback cycles: " << max_cycles << std::endl;
    }
    std::cout << "[SIM] Bit order: " << (msb_first ? "MSB-first" : "LSB-first") << std::endl;
    std::cout << "[SIM] Protocol: " << (proto_mode) << std::endl;

    // Set mode_select based on cjtag_mode flag
    top->mode_select = cjtag_mode ? 1 : 0;
    // Configure VPI server bit order
    vpi_server.set_msb_first(msb_first);
    // Configure debug level
    vpi_server.set_debug_level(debug_level);
    if (debug_level > 0) {
        std::cout << "[SIM] Debug level: " << debug_level << std::endl;
    }
    // Configure protocol mode
    if (proto_mode == "openocd") {
        vpi_server.set_protocol_mode(JtagVpiServer::PROTO_OPENOCD_VPI);
    } else if (proto_mode == "legacy") {
        vpi_server.set_protocol_mode(JtagVpiServer::PROTO_LEGACY_8BYTE);
    } else {
        vpi_server.set_protocol_mode(JtagVpiServer::PROTO_UNKNOWN);
    }
    // Set initial mode from command-line flag
    vpi_server.set_mode(cjtag_mode ? 1 : 0);

    if (trace_enabled) {
#if ENABLE_FST
        contextp->traceEverOn(true);
        VerilatedFstC* fst_trace = new VerilatedFstC;
        top->trace(fst_trace, 99);
        fst_trace->open("jtag_vpi.fst");
        trace = fst_trace;
        std::cout << "[TRACE] FST waveform enabled: jtag_vpi.fst" << std::endl;
#elif ENABLE_VCD
        contextp->traceEverOn(true);
        VerilatedVcdC* vcd_trace = new VerilatedVcdC;
        top->trace(vcd_trace, 99);
        vcd_trace->open("jtag_vpi.vcd");
        trace = vcd_trace;
        std::cout << "[TRACE] VCD waveform enabled: jtag_vpi.vcd" << std::endl;
#else
        std::cout << "[TRACE] Tracing requested but disabled at build-time (no waveform format enabled)" << std::endl;
#endif
    }

    uint64_t cycle_count = 0;
    uint64_t last_status = 0;
    bool client_connected_once = false;

    // Main VPI simulation state machine
    enum vpi_sim_state_t {
        SIM_RESET_SYSTEM,      // System reset phase (50 cycles)
        SIM_RESET_JTAG_INIT,   // Initialize JTAG reset (TMS high for 5 TCK cycles)
        SIM_RESET_JTAG_PULSE,  // Execute JTAG reset pulses
        SIM_IDLE,              // Waiting for VPI client connection
        SIM_VPI_ACTIVE,        // Active VPI communication
        SIM_VPI_PROCESSING,    // Processing VPI requests
        SIM_SHUTDOWN           // Shutting down simulation
    };

    vpi_sim_state_t sim_state = SIM_RESET_SYSTEM;
    int reset_cycle_count = 0;
    int reset_tck_cycles = 0;
    const int SYSTEM_RESET_CYCLES = 50;
    const int JTAG_RESET_TCK_CYCLES = 5;
    const int TCK_CLK_RATIO = 4;  // TCK frequency = CLK frequency / 4

    // TCK generation counters for constant frequency
    int tck_clk_counter = 0;
    bool tck_pulse_phase = false;  // false=low, true=high
    int clk_div_counter = 0;       // For VPI processing timing

    // Release reset after initial system reset cycles
    std::cout << "[SIM] Starting system reset phase..." << std::endl;

    // Main simulation loop with integrated reset
    while (!contextp->gotFinish()) {
        // Generate constant 50% duty cycle CLK (100MHz)
        // Clock period = 10ns, so toggle every 5000ps
        uint8_t new_clk = (contextp->time() / 5000) & 1;
        top->clk = new_clk;

        // Comprehensive VPI simulation state machine
        switch (sim_state) {
            case SIM_RESET_SYSTEM:
                // Keep system in reset for initial cycles
                top->rst_n = 0;
                top->jtag_trst_n_i = 0;
                top->jtag_pin0_i = 0;  // TCK low
                top->jtag_pin1_i = 0;  // TMS low
                top->jtag_pin2_i = 0;  // TDI low

                reset_cycle_count++;
                if (reset_cycle_count >= SYSTEM_RESET_CYCLES) {
                    top->rst_n = 1;
                    top->jtag_trst_n_i = 1;
                    sim_state = SIM_RESET_JTAG_INIT;
                    reset_cycle_count = 0;
                    std::cout << "[SIM] System reset released, initializing JTAG TAP reset..." << std::endl;
                }
                break;

            case SIM_RESET_JTAG_INIT:
                // Set TMS high for JTAG reset sequence
                top->jtag_pin1_i = 1;  // TMS high
                top->jtag_pin2_i = 0;  // TDI low
                reset_tck_cycles = 0;
                sim_state = SIM_RESET_JTAG_PULSE;
                break;

            case SIM_RESET_JTAG_PULSE:
                // Generate TCK pulses with TMS high (TCK/CLK = 1/4 ratio)
                tck_clk_counter++;
                if (tck_clk_counter >= TCK_CLK_RATIO) {
                    tck_clk_counter = 0;

                    if (!tck_pulse_phase) {
                        // TCK rising edge
                        top->jtag_pin0_i = 1;
                        tck_pulse_phase = true;
                    } else {
                        // TCK falling edge
                        top->jtag_pin0_i = 0;
                        tck_pulse_phase = false;
                        reset_tck_cycles++;

                        if (reset_tck_cycles >= JTAG_RESET_TCK_CYCLES) {
                            top->jtag_pin1_i = 0;  // TMS back to low
                            sim_state = SIM_IDLE;
                            std::cout << "[SIM] JTAG TAP reset complete, entering idle state" << std::endl;
                            std::cout << "[SIM] Cycle: " << cycle_count
                                      << " | IDCODE: 0x" << std::hex << top->idcode
                                      << " | Mode: cfg=" << (cjtag_mode ? "cJTAG" : "JTAG")
                                      << " active=" << (top->active_mode ? "cJTAG" : "JTAG")
                                      << std::dec << std::endl;
                        }
                    }
                }
                break;

            case SIM_IDLE:
                // Idle state: wait for VPI activity or handle background tasks
                vpi_server.poll();

                // Check if VPI server becomes active (has pending operations)
                uint8_t tms, tdi, mode_sel;
                bool tck_pulse;
                if (vpi_server.get_pending_signals(&tms, &tdi, &mode_sel, &tck_pulse)) {
                    sim_state = SIM_VPI_ACTIVE;
                }
                break;

            case SIM_VPI_ACTIVE:
                // Track client connection status
                if (!client_connected_once && vpi_server.is_client_connected()) {
                    std::cout << "[VPI] ✓ OpenOCD/Client connected successfully!" << std::endl;
                    client_connected_once = true;
                }

                // Update VPI server with current signal values
                // TDO tri-state: when tdo_en=0 (high-z), JTAG default is 1
                {
                    uint8_t tdo_value = (top->jtag_pin3_oen) ? top->jtag_pin3_o : 1;
                    vpi_server.update_signals(
                        tdo_value,
                        top->jtag_pin3_oen,
                        top->idcode,
                        top->active_mode
                    );
                }

                // Check for VPI signals and transition to processing
                {
                    uint8_t tms, tdi, mode_sel;
                    bool tck_pulse, tckc_toggle = false;
                    bool client_connected = vpi_server.is_client_connected();

                    if (client_connected && vpi_server.get_pending_signals(&tms, &tdi, &mode_sel, &tck_pulse, &tckc_toggle)) {
                        sim_state = SIM_VPI_PROCESSING;
                    }
                }
                break;

            case SIM_VPI_PROCESSING:
                // Handle VPI signal processing
                {
                    // DEBUG: Log RTL internal signals
                    static uint8_t last_tdo = 0;
                    static uint8_t last_tdo_en = 0;
                    static uint8_t last_tap_state = 0xFF;
                    static int signal_log_count = 0;
                    uint8_t tap_state = top->rootp->jtag_vpi_top__DOT__dut__DOT__tap_ctrl__DOT__current_state;

                    if (signal_log_count < 1 && (top->jtag_pin3_o != last_tdo || top->jtag_pin3_oen != last_tdo_en || tap_state != last_tap_state)) {
                        last_tdo = top->jtag_pin3_o;
                        last_tdo_en = top->jtag_pin3_oen;
                        last_tap_state = tap_state;
                        signal_log_count++;
                    }

                    // TCK generation with constant frequency ratio (TCK/CLK = 1/4)
                    uint8_t tms, tdi, mode_sel;
                    bool tck_pulse, tckc_toggle = false;
                    bool client_connected = vpi_server.is_client_connected();
                    bool has_pending_signals = client_connected && vpi_server.get_pending_signals(&tms, &tdi, &mode_sel, &tck_pulse, &tckc_toggle);

                    // TCK/CLK = 1/4 ratio control: Only process VPI requests every 4 CLK cycles
                    static int clk_div_counter = 0;
                    bool process_vpi_this_cycle = (clk_div_counter == 0);
                    clk_div_counter = (clk_div_counter + 1) % 4;

                    if (has_pending_signals && process_vpi_this_cycle) {
                        top->jtag_pin1_i = tms;
                        top->jtag_pin2_i = tdi;
                        top->mode_select = mode_sel;

                        uint8_t tdo_value = (top->jtag_pin3_oen) ? top->jtag_pin3_o : 1;

                        if (tckc_toggle) {
                            // cJTAG mode: toggle TCKC to create one edge
                            static uint8_t tckc_state = 0;
                            tckc_state = !tckc_state;
                            top->jtag_pin0_i = tckc_state;
                            top->eval();
#if ENABLE_FST
                            if (trace) static_cast<VerilatedFstC*>(trace)->dump(contextp->time());
#elif ENABLE_VCD
                            if (trace) static_cast<VerilatedVcdC*>(trace)->dump(contextp->time());
#endif
                            contextp->timeInc(1);

                            // Update TDO after toggle
                            if (mode_sel == 1) {
                                // cJTAG: TMSC on pin1 (bidirectional)
                                tdo_value = (top->jtag_pin1_oen) ? top->jtag_pin1_o : 1;
                            } else {
                                // JTAG: TDO on pin3
                                tdo_value = (top->jtag_pin3_oen) ? top->jtag_pin3_o : 1;
                            }
                            vpi_server.update_signals(
                                tdo_value,
                                top->jtag_pin3_oen,
                                top->idcode,
                                top->active_mode
                            );
                        }
                        else if (tck_pulse) {
                            // JTAG mode: Execute TCK pulse (0→1→0)
                            // TCK pulse: 5ns high, 5ns low (10ns total = 100MHz / 10 = 10MHz JTAG clock)
                            top->jtag_pin0_i = 1;
                            top->eval();
#if ENABLE_FST
                            if (trace) static_cast<VerilatedFstC*>(trace)->dump(contextp->time());
#elif ENABLE_VCD
                            if (trace) static_cast<VerilatedVcdC*>(trace)->dump(contextp->time());
#endif
                            contextp->timeInc(5000);  // 5ns high phase

                            top->jtag_pin0_i = 0;
                            top->eval();
#if ENABLE_FST
                            if (trace) static_cast<VerilatedFstC*>(trace)->dump(contextp->time());
#elif ENABLE_VCD
                            if (trace) static_cast<VerilatedVcdC*>(trace)->dump(contextp->time());
#endif
                            contextp->timeInc(5000);  // 5ns low phase

                            // Update TDO after pulse
                            if (mode_sel == 1) {
                                // cJTAG: TMSC on pin1 (bidirectional)
                                tdo_value = (top->jtag_pin1_oen) ? top->jtag_pin1_o : 1;
                            } else {
                                // JTAG: TDO on pin3
                                tdo_value = (top->jtag_pin3_oen) ? top->jtag_pin3_o : 1;
                            }
                            vpi_server.update_signals(
                                tdo_value,
                                top->jtag_pin3_oen,
                                top->idcode,
                                top->active_mode
                            );
                        }

                        // Return to VPI_ACTIVE for next signal check
                        sim_state = SIM_VPI_ACTIVE;
                    } else {
                        // No VPI client connected or no pending signals: Keep pins in stable state
                        top->jtag_pin0_i = 0;  // TCK idle (low)
                        top->jtag_pin1_i = 0;  // TMS=0 (stay in current state)
                        top->jtag_pin2_i = 0;  // TDI=0 (no data input)

                        // Return to VPI_ACTIVE for continued polling
                        sim_state = SIM_VPI_ACTIVE;
                    }

                    // Print status every 20000000 cycles (20M cycles = less frequent logging)
                    if (verbose && (cycle_count - last_status) >= 20000000) {
                        std::cout << "[SIM] Cycle: " << cycle_count
                                  << " | IDCODE: 0x" << std::hex << top->idcode
                                  << " | Mode: cfg=" << (cjtag_mode ? "cJTAG" : "JTAG")
                                  << " active=" << (top->active_mode ? "cJTAG" : "JTAG")
                                  << std::dec << std::endl;
                        last_status = cycle_count;
                    }
                }
                break;

            case SIM_SHUTDOWN:
                // Shutdown state - cleanup and exit
                contextp->gotFinish(true);
                break;

            default:
                // Unknown state - reset to idle
                std::cout << "[SIM] Warning: Unknown state " << sim_state << ", resetting to idle" << std::endl;
                sim_state = SIM_IDLE;
                break;
        }

        // Common simulation step operations for all states
        top->eval();

        // Dump trace

#if ENABLE_FST
        if (trace) {
            static_cast<VerilatedFstC*>(trace)->dump(contextp->time());
        }
#elif ENABLE_VCD
        if (trace) {
            static_cast<VerilatedVcdC*>(trace)->dump(contextp->time());
        }
#endif

        // Advance CLK time: 5ns per half-cycle for 100MHz system clock
        contextp->timeInc(5000);
        cycle_count++;

        // Exit condition: Ctrl+C or wall-clock timeout (with cycle fallback)
        // Skip timeout check if timeout_seconds is 0 (unlimited)
        if (timeout_seconds > 0) {
            if (std::chrono::steady_clock::now() >= deadline || cycle_count > max_cycles) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_time).count();
                std::cout << "\n[SIM] Timeout reached (elapsed " << elapsed << "s, configured " << timeout_seconds << "s)" << std::endl;
                break;
            }
        }
    }

    // Cleanup

#if ENABLE_FST
    if (trace) {
        static_cast<VerilatedFstC*>(trace)->close();
        delete static_cast<VerilatedFstC*>(trace);
    }
#elif ENABLE_VCD
    if (trace) {
        static_cast<VerilatedVcdC*>(trace)->close();
        delete static_cast<VerilatedVcdC*>(trace);
    }
#endif

    top->final();
    delete top;

    // VL_USER_FINISH: Exit code handled by custom finish handler
    int exit_code = global_exit_code;

    std::cout << "\n=== VPI Simulation Complete ===" << std::endl;
    std::cout << "Total cycles: " << cycle_count << std::endl;
    std::cout << "Simulation time: " << contextp->time() << " ns" << std::endl;

    return exit_code;
}
