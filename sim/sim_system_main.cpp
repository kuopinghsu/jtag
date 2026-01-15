/**
 * Verilator System Simulation Main
 * Top-level C++ wrapper for System integration testbench
 */

#include "Vsystem_tb.h"
#include "verilated.h"
#ifndef ENABLE_FST
#define ENABLE_FST 0
#endif
#if ENABLE_FST
#include "verilated_fst_c.h"
#endif
#include <iostream>
#include <iomanip>

int main(int argc, char** argv) {
    // Create context
    const std::unique_ptr<VerilatedContext> contextp{new VerilatedContext};
    contextp->commandArgs(argc, argv);

    // Create simulator instance
    Vsystem_tb* top = new Vsystem_tb{contextp.get()};

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

    std::cout << "\n=== Simulation Complete ===" << std::endl;
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

    return 0;
}
