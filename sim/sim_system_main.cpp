/**
 * Verilator System Simulation Main
 * Top-level C++ wrapper for System integration testbench
 */

#include "Vsystem_tb.h"
#include "verilated.h"
#include "svdpi.h"  // For DPI scope management
#ifndef ENABLE_FST
#define ENABLE_FST 0
#endif
#if ENABLE_FST
#include "verilated_fst_c.h"
#endif
#include <iostream>
#include <iomanip>

// DPI function declaration for verification status
extern "C" int get_verification_status_dpi();

// Global exit code for VL_USER_FINISH
static int global_exit_code = 0;
static Vsystem_tb* global_top = nullptr;

#ifdef VL_USER_FINISH
// Custom finish handler for VL_USER_FINISH
void vl_finish(const char* filename, int linenum, const char* hier) {
    std::cout << "SystemVerilog $finish called from " << filename << ":" << linenum << std::endl;

    int status = 1;  // Default to failed
    if (global_top != nullptr) {
        try {
            // Set proper SystemVerilog scope context for DPI call
            svScope scope = svGetScopeFromName("TOP.system_tb");
            if (scope) {
                svSetScope(scope);
            }
            status = get_verification_status_dpi();
            std::cout << "Testbench exit status: " << status << " (0=passed, 1=failed, 2=timeout)" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Warning: Unable to get verification status: " << e.what() << std::endl;
            status = 1;  // Default to failed on error
        }
    }

    global_exit_code = status;
    Verilated::threadContextp()->gotFinish(true);
}
#endif

int main(int argc, char** argv) {
    // Create context
    const std::unique_ptr<VerilatedContext> contextp{new VerilatedContext};
    contextp->commandArgs(argc, argv);

    // Create simulator instance
    Vsystem_tb* top = new Vsystem_tb{contextp.get()};
    global_top = top;  // Set global pointer for VL_USER_FINISH

    // Enable waveform tracing if requested

#if ENABLE_FST
    VerilatedFstC* trace = NULL;
    if (argc > 1 && std::string(argv[1]) == "--trace") {
        contextp->traceEverOn(true);
        trace = new VerilatedFstC;
        top->trace(trace, 99);
        trace->open("system_sim.fst");
        std::cout << "FST trace enabled: system_sim.fst" << std::endl;
    }
#else
    void* trace = NULL;
    if (argc > 1 && std::string(argv[1]) == "--trace") {
        std::cout << "FST tracing requested but disabled at build-time (ENABLE_FST=0)" << std::endl;
    }
#endif

    std::cout << "\n=== System Integration Simulation ===" << std::endl;
    std::cout << "Simulation starting..." << std::endl;

    // Run simulation
    while (!contextp->gotFinish()) {
        top->eval();

#if ENABLE_FST
        if (trace) {
            trace->dump(contextp->time());
        }
#endif

        contextp->timeInc(1);
    }

    // VL_USER_FINISH: Exit code handled by custom finish handler
    int exit_code = global_exit_code;

    // Show clear test result status instead of generic completion message
    if (exit_code == 0) {
        std::cout << "\n✓ SIMULATION PASSED" << std::endl;
    } else if (exit_code == 1) {
        std::cout << "\n✗ SIMULATION FAILED" << std::endl;
    } else if (exit_code == 2) {
        std::cout << "\n⏰ SIMULATION TIMEOUT" << std::endl;
    } else {
        std::cout << "\n❌ SIMULATION ERROR (code: " << exit_code << ")" << std::endl;
    }
    std::cout << "Total simulation time: " << contextp->time() << " ns" << std::endl;

    // Cleanup

#if ENABLE_FST
    if (trace) {
        trace->close();
        delete trace;
    }
#endif

    top->final();
    delete top;

    return exit_code;
}
