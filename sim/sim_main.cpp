/**
 * Verilator Simulation Main
 * Top-level C++ wrapper for Verilator simulation
 */

#include "Vjtag_tb.h"
#include "verilated.h"
#include "verilated_fst_c.h"
#include <iostream>
#include <iomanip>

int main(int argc, char** argv) {
    // Create context
    const std::unique_ptr<VerilatedContext> contextp{new VerilatedContext};
    contextp->commandArgs(argc, argv);
    
    // Create simulator instance
    Vjtag_tb* top = new Vjtag_tb{contextp.get()};
    
    // Enable waveform tracing if requested
    VerilatedFstC* trace = NULL;
    if (argc > 1 && std::string(argv[1]) == "--trace") {
        contextp->traceEverOn(true);
        trace = new VerilatedFstC;
        top->trace(trace, 99);
        trace->open("jtag_sim.fst");
        std::cout << "FST trace enabled: jtag_sim.fst" << std::endl;
    }
    
    std::cout << "\n=== JTAG Verilator Simulation ===" << std::endl;
    std::cout << "Simulation starting..." << std::endl;
    std::cout << "Note: VPI server support available via separate testbench" << std::endl;
    
    // Run simulation
    while (!contextp->gotFinish()) {
        top->eval();
        
        // Dump trace
        if (trace) {
            trace->dump(contextp->time());
        }
        
        // Advance time
        contextp->timeInc(1);
    }
    
    // Cleanup
    if (trace) {
        trace->close();
        delete trace;
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
