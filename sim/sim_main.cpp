/**
 * Verilator Simulation Main
 * Top-level C++ wrapper for Verilator simulation
 */

#include "Vjtag_tb.h"
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

// DPI function declarations for verification status
extern "C" int get_verification_status_dpi();
#include "svdpi.h"  // For DPI scope management

// Global exit code for VL_USER_FINISH
static int global_exit_code = 0;
static Vjtag_tb* global_top = nullptr;

// Define custom finish handler (VL_USER_FINISH=1 defined via compiler flag)
void vl_finish(const char* filename, int linenum, const char* hier) {
    // SystemVerilog $finish() was called - capture this event
    (void)hier;
    std::cout << "SystemVerilog $finish called from " << filename << ":" << linenum << std::endl;

    // Get verification status from SystemVerilog DPI function
    int status = 1;  // Default to failed
    if (global_top != nullptr) {
        try {
            // Set proper SystemVerilog scope context for DPI call
            svScope scope = svGetScopeFromName("TOP.jtag_tb");
            if (scope) {
                svSetScope(scope);
            }
            // Call DPI function to get verification status
            status = get_verification_status_dpi();
            std::cout << "Testbench exit status: " << status << " (0=passed, 1=failed, 2=timeout)" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Warning: Unable to get verification status from testbench: " << e.what() << std::endl;
            status = 1;  // Default to failed on error
        }
    }

    global_exit_code = status;  // Use status from DPI function
    Verilated::threadContextp()->gotFinish(true);
}
#include <iomanip>

int main(int argc, char** argv) {
    // Create context
    const std::unique_ptr<VerilatedContext> contextp{new VerilatedContext};
    contextp->commandArgs(argc, argv);

    // Create simulator instance
    Vjtag_tb* top = new Vjtag_tb{contextp.get()};

    // Set global pointer for VL_USER_FINISH handler
    global_top = top;

    // Enable waveform tracing if requested
    void* trace = NULL;
    if (argc > 1 && std::string(argv[1]) == "--trace") {
#if ENABLE_FST
        contextp->traceEverOn(true);
        VerilatedFstC* fst_trace = new VerilatedFstC;
        top->trace(fst_trace, 99);
        fst_trace->open("jtag_sim.fst");
        trace = fst_trace;
        std::cout << "FST trace enabled: jtag_sim.fst" << std::endl;
#elif ENABLE_VCD
        contextp->traceEverOn(true);
        VerilatedVcdC* vcd_trace = new VerilatedVcdC;
        top->trace(vcd_trace, 99);
        vcd_trace->open("jtag_sim.vcd");
        trace = vcd_trace;
        std::cout << "VCD trace enabled: jtag_sim.vcd" << std::endl;
#else
        std::cout << "Tracing requested but disabled at build-time (no waveform format enabled)" << std::endl;
#endif
    }

    std::cout << "\n=== JTAG Verilator Simulation ===" << std::endl;
    std::cout << "Simulation starting..." << std::endl;
    std::cout << "Note: VPI server support available via separate testbench" << std::endl;

    // Run simulation
    while (!contextp->gotFinish()) {
        top->eval();

        // Dump trace
        if (trace) {
#if ENABLE_FST
            static_cast<VerilatedFstC*>(trace)->dump(contextp->time());
#elif ENABLE_VCD
            static_cast<VerilatedVcdC*>(trace)->dump(contextp->time());
#endif
        }

        // Advance time
        contextp->timeInc(1);
    }

    // Capture exit code from VL_USER_FINISH handler
    int exit_code = global_exit_code;  // Use global from custom finish handler
    if (contextp->gotFinish()) {
        std::cout << "\nSimulation completed with exit code: " << exit_code << std::endl;
        if (exit_code == 0) {
            std::cout << "✓ All tests PASSED" << std::endl;
        } else if (exit_code == 1) {
            std::cout << "✗ Some tests FAILED" << std::endl;
        } else if (exit_code == 2) {
            std::cout << "⚠ Simulation TIMEOUT" << std::endl;
        } else {
            std::cout << "? Unknown exit condition" << std::endl;
        }
    }

    // Cleanup
    if (trace) {
#if ENABLE_FST
        static_cast<VerilatedFstC*>(trace)->close();
        delete static_cast<VerilatedFstC*>(trace);
#elif ENABLE_VCD
        static_cast<VerilatedVcdC*>(trace)->close();
        delete static_cast<VerilatedVcdC*>(trace);
#endif
    }

    top->final();
    delete top;

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
    std::cout << "\nFor VPI/OpenOCD integration, the simulation provides:" << std::endl;
    std::cout << "  - Standard JTAG interface verification via testbench" << std::endl;
    std::cout << "  - cJTAG mode testing and validation" << std::endl;
    std::cout << "  - IDCODE read operations" << std::endl;
    std::cout << "\nVPI clients can connect to dedicated VPI testbench (see documentation)" << std::endl;

    return exit_code;
}
