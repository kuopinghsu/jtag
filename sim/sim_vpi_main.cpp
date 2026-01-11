/**
 * Verilator Simulation with VPI Server
 * Interactive JTAG control via TCP/IP socket
 */

#include "Vjtag_vpi_top.h"
#include "Vjtag_vpi_top___024root.h"  // For internal signal access
#include "verilated.h"
#include "verilated_fst_c.h"
#include "jtag_vpi_server.h"
#include <iostream>
#include <iomanip>
#include <cstring>

// Default timeout: 300 seconds = 30B cycles at 100MHz
// Can be overridden with --timeout parameter
#define DEFAULT_TIMEOUT_SECONDS 300

int main(int argc, char** argv) {
    // Create context
    const std::unique_ptr<VerilatedContext> contextp{new VerilatedContext};
    contextp->commandArgs(argc, argv);
    
    // Create simulator instance
    Vjtag_vpi_top* top = new Vjtag_vpi_top{contextp.get()};
    
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
    VerilatedFstC* trace = nullptr;
    bool trace_enabled = false;
    bool verbose = true;  // Default: show status messages
    bool cjtag_mode = false;  // Default: JTAG mode
    bool msb_first = false;    // Default: LSB-first bit packing
    std::string proto_mode = "auto"; // Default: auto-detect protocol
    uint64_t timeout_seconds = DEFAULT_TIMEOUT_SECONDS;
    
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
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "\nUsage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --trace                  Enable FST waveform tracing" << std::endl;
            std::cout << "  --cjtag                  Enable cJTAG mode (default: JTAG)" << std::endl;
            std::cout << "  --timeout <seconds>      Set simulation timeout (default: 300s)" << std::endl;
            std::cout << "  --timeout=<seconds>      Alternative timeout format" << std::endl;
            std::cout << "  --quiet, -q              Suppress cycle status messages" << std::endl;
            std::cout << "  --verbose, -v            Show cycle status messages (default)" << std::endl;
            std::cout << "  --proto <mode>           Protocol: auto | openocd | legacy (default: auto)" << std::endl;
            std::cout << "  --help, -h               Show this help message" << std::endl;
            delete top;
            return 0;
        }
    }
    
    uint64_t max_cycles = timeout_seconds * 100000000ULL; // 100MHz clock
    std::cout << "[SIM] Mode: " << (cjtag_mode ? "cJTAG" : "JTAG") << std::endl;
    std::cout << "[SIM] Timeout: " << timeout_seconds << "s (" << max_cycles << " cycles)" << std::endl;
    std::cout << "[SIM] Bit order: " << (msb_first ? "MSB-first" : "LSB-first") << std::endl;
    std::cout << "[SIM] Protocol: " << (proto_mode) << std::endl;
    
    // Set mode_select based on cjtag_mode flag
    top->mode_select = cjtag_mode ? 1 : 0;
    // Configure VPI server bit order
    vpi_server.set_msb_first(msb_first);
    // Configure protocol mode
    if (proto_mode == "openocd") {
        vpi_server.set_protocol_mode(JtagVpiServer::PROTO_OPENOCD_VPI);
    } else if (proto_mode == "legacy") {
        vpi_server.set_protocol_mode(JtagVpiServer::PROTO_LEGACY_8BYTE);
    } else {
        vpi_server.set_protocol_mode(JtagVpiServer::PROTO_UNKNOWN);
    }
    
    if (trace_enabled) {
        contextp->traceEverOn(true);
        trace = new VerilatedFstC;
        top->trace(trace, 99);
        trace->open("jtag_vpi.fst");
        std::cout << "[TRACE] FST waveform enabled: jtag_vpi.fst" << std::endl;
    }
    
    uint64_t cycle_count = 0;
    uint64_t last_status = 0;
    bool client_connected_once = false;
    
    // Release reset after a few cycles
    for (int i = 0; i < 10; i++) {
        top->clk = !top->clk;
        top->eval();
        if (trace) trace->dump(contextp->time());
        contextp->timeInc(1);
    }
    top->rst_n = 1;
    top->jtag_trst_n_i = 1;
    std::cout << "[SIM] Reset released" << std::endl;
    
    // Main simulation loop
    while (!contextp->gotFinish()) {
        // Toggle clock
        top->clk = !top->clk;
        
        // On positive clock edge, poll VPI server
        if (top->clk && (cycle_count % 10) == 0) {
            vpi_server.poll();
            
            // Track client connection status
            if (!client_connected_once && vpi_server.is_client_connected()) {
                std::cout << "[VPI] ✓ OpenOCD/Client connected successfully!" << std::endl;
                client_connected_once = true;
            }
            
            // Update VPI server with current signal values
            // TDO tri-state: when tdo_en=0 (high-z), JTAG default is 1
            uint8_t tdo_value = (top->jtag_pin3_oen) ? top->jtag_pin3_o : 1;
            vpi_server.update_signals(
                tdo_value,
                top->jtag_pin3_oen,
                top->idcode,
                top->active_mode
            );
            
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
            
            // Get pending commands from VPI clients
            uint8_t tms, tdi, mode_sel;
            bool tck_pulse;
            if (vpi_server.get_pending_signals(&tms, &tdi, &mode_sel, &tck_pulse)) {
                top->jtag_pin1_i = tms;
                top->jtag_pin2_i = tdi;
                top->mode_select = mode_sel;
                
                if (tck_pulse) {
                    // Execute TCK pulse
                    top->jtag_pin0_i = 1;
                    top->eval();
                    if (trace) trace->dump(contextp->time());
                    contextp->timeInc(1);
                    
                    top->jtag_pin0_i = 0;
                    top->eval();
                    if (trace) trace->dump(contextp->time());
                    contextp->timeInc(1);
                    
                    // Update TDO after pulse
                    // TDO tri-state: when tdo_en=0 (high-z), JTAG default is 1
                    tdo_value = (top->jtag_pin3_oen) ? top->jtag_pin3_o : 1;
                    vpi_server.update_signals(
                        tdo_value,
                        top->jtag_pin3_oen,
                        top->idcode,
                        top->active_mode
                    );
                }
            }
            
            // Print status every 10000 cycles
            if (verbose && (cycle_count - last_status) >= 10000) {
                std::cout << "[SIM] Cycle: " << cycle_count 
                          << " | IDCODE: 0x" << std::hex << top->idcode
                          << " | Mode: cfg=" << (cjtag_mode ? "cJTAG" : "JTAG")
                          << " active=" << (top->active_mode ? "cJTAG" : "JTAG")
                          << std::dec << std::endl;
                last_status = cycle_count;
            }
        }
        
        top->eval();
        
        // Dump trace
        if (trace) {
            trace->dump(contextp->time());
        }
        
        // Advance time
        contextp->timeInc(1);
        cycle_count++;
        
        // Exit condition: Ctrl+C or timeout
        if (cycle_count > max_cycles) {
            std::cout << "\n[SIM] Timeout reached (" << timeout_seconds << "s)" << std::endl;
            break;
        }
    }
    
    // Cleanup
    if (trace) {
        trace->close();
        delete trace;
    }
    
    top->final();
    delete top;
    
    std::cout << "\n=== VPI Simulation Complete ===" << std::endl;
    std::cout << "Total cycles: " << cycle_count << std::endl;
    std::cout << "Simulation time: " << contextp->time() << " ns" << std::endl;
    
    return 0;
}
