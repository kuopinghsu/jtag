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
#include <iomanip>

int main(int argc, char** argv) {
    // Create context
    const std::unique_ptr<VerilatedContext> contextp{new VerilatedContext};
    contextp->commandArgs(argc, argv);

    // Create simulator instance
    Vjtag_tb* top = new Vjtag_tb{contextp.get()};

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

    std::cout << "\n=== Simulation Complete ===" << std::endl;
    std::cout << "Total simulation time: " << contextp->time() << " ns" << std::endl;
    std::cout << "\nFor VPI/OpenOCD integration, the simulation provides:" << std::endl;
    std::cout << "  - Standard JTAG interface verification via testbench" << std::endl;
    std::cout << "  - cJTAG mode testing and validation" << std::endl;
    std::cout << "  - IDCODE read operations" << std::endl;
    std::cout << "\nVPI clients can connect to dedicated VPI testbench (see documentation)" << std::endl;

    return 0;
}
