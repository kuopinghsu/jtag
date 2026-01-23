# Quick Start Guide

Get the JTAG/cJTAG simulation running in under 5 minutes.

## Prerequisites

**Install Verilator:**

```bash
# macOS
brew install verilator

# Ubuntu/Debian
sudo apt-get install verilator build-essential

# Verify installation
verilator --version  # Should be 5.x or later
```

## Option 1: Testbench Verification (Recommended First Run)

Run automated tests to verify everything works:

```bash
# Clone/navigate to project directory
cd jtag/

# Build and run simulation
make sim
```

**Expected output:**
```
=== JTAG Testbench Starting ===

Test 1: TAP Controller Reset
✓ TAP reset verification PASSED

Test 2: Read IDCODE
IDCODE: 0x1dead3ff
✓ IDCODE verification PASSED

Test 3: Debug Request
✓ Debug request PASSED

Test 4: cJTAG Mode - Read IDCODE
IDCODE in cJTAG mode: 0x1dead3ff
✓ cJTAG IDCODE verification PASSED

Test 5: Return to JTAG mode
✓ Mode switch PASSED

=== All tests completed successfully! ===
```

**Waveform viewing (optional):**
```bash
# Install GTKWave if not already installed
brew install gtkwave  # macOS
sudo apt-get install gtkwave  # Linux

# View waveforms (format depends on WAVE parameter)
gtkwave jtag_sim.fst    # For WAVE=fst or WAVE=1
gtkwave jtag_sim.vcd    # For WAVE=vcd
```

**Note on cJTAG Testing**: Test 4 and 5 verify cJTAG mode switching in standalone simulation. For VPI/OpenOCD testing, cJTAG is now fully supported - see `make test-cjtag` (15/15 tests passing as of v2.1).

## Option 2: Interactive VPI Testing

Control the simulation in real-time via TCP/IP:

### Step 1: Build VPI Clients

```bash
make client
```

### Step 2: Start VPI Simulation

**Terminal 1:**
```bash
make vpi-sim
```

You should see:
```
=== JTAG VPI Interactive Simulation ===
[VPI] Server listening on port 3333
[VPI] Waiting for client connections...
```

### Step 3: Connect Client

**Terminal 2:**
```bash
# Simple client (reads IDCODE)
./build/jtag_vpi_client
```

**Expected output:**
```
Connecting to JTAG VPI server at 127.0.0.1:3333...
✓ Connected successfully

Performing TAP reset...
Reading IDCODE...
IDCODE: 0x1DEAD3FF

Parsed IDCODE:
  Version:      0x1
  Part Number:  0xDEAD
  Manufacturer: 0x1FF

✓ Test completed successfully
```

**Stop simulation:** Press `Ctrl+C` in Terminal 1

### Automated Test

Run everything automatically:
```bash
make test-vpi
```

## Common Commands

```bash
# Show all build targets
make help

# Clean build artifacts
make clean

# Rebuild everything
make clean && make sim

# Build simulation
make verilator

# Build VPI client
make client

# Run automated VPI test
make test-vpi

# Debug Parameters
# Enable SystemVerilog debug messages
VERBOSE=1 make sim

# Enable VPI server debug output
DEBUG=1 make vpi-sim

# Enable both SystemVerilog and VPI debug
VERBOSE=1 DEBUG=2 make vpi-sim

# Enable waveform tracing
WAVE=fst make sim    # Generate FST format
WAVE=vcd make sim    # Generate VCD format
WAVE=1 make sim      # Generate FST format (default)
```

## Verification Checklist

- [ ] `make sim` completes successfully
- [ ] All 5 tests pass
- [ ] IDCODE reads as `0x1DEAD3FF`
- [ ] Waveform file `jtag_sim.fst` (FST format) or `jtag_sim.vcd` (VCD format) is generated when tracing enabled
- [ ] VPI simulation starts and listens on port 3333
- [ ] VPI client connects and reads IDCODE

## Troubleshooting

### "verilator: command not found"
Install Verilator (see Prerequisites above).

### Build errors
```bash
make clean
make verilator
```

### Port 3333 already in use
```bash
# Find process using port
lsof -ti:3333

# Kill it
kill $(lsof -ti:3333)
```

### VPI client can't connect
1. Verify simulation is running: `ps aux | grep jtag_vpi`
2. Check port is listening: `lsof -i:3333`
3. Try restarting: `killall jtag_vpi && make vpi-sim`

## Next Steps

After successful quick start:

1. **Explore waveforms**: Open `jtag_sim.fst` or `jtag_sim.vcd` in GTKWave (depending on WAVE parameter used)
2. **Run synthesis**: Try `make synth` to generate gate-level netlists
3. **Modify IDCODE**: Edit `src/jtag/jtag_dtm.sv`
4. **Add instructions**: Extend instruction register
5. **OpenOCD integration**: See [openocd/README.md](openocd/README.md)
6. **FPGA synthesis**: Port to your target FPGA

## Synthesis Quick Start

If you have OSS CAD Suite installed:

```bash
# Synthesize all modules with ASAP7 7nm PDK
make synth

# View results
ls syn/results/    # Netlists: *_synth.v, *_synth.json
ls syn/reports/    # Statistics: *_stats.rpt

# Generate detailed reports
make synth-reports

# Clean synthesis outputs
make synth-clean
```

See [syn/README.md](syn/README.md) for detailed synthesis documentation.

## File Overview

```
Key files to understand:
├── src/jtag/jtag_top.sv              # Top-level JTAG integration (start here)
├── src/jtag/jtag_tap_controller.sv   # TAP state machine
├── src/jtag/jtag_dtm.sv              # Debug Transport Module (IDCODE)
├── src/dbg/riscv_debug_module.sv     # RISC-V Debug Module
├── tb/jtag_tb.sv                     # JTAG testbench (see tests)
├── tb/system_tb.sv                   # System integration testbench
└── Makefile                          # Build commands

VPI interface:
├── sim/jtag_vpi_server.cpp      # TCP/IP server
├── vpi/jtag_vpi_client.c        # Simple client
└── vpi/jtag_vpi_advanced.cpp    # Advanced client

Synthesis:
├── syn/scripts/                 # Yosys synthesis scripts
├── syn/results/                 # Generated netlists (after synthesis)
└── syn/reports/                 # Area/timing reports
```

## Quick Reference

### JTAG Signals
- `tck`: Test clock
- `tms`: Test mode select
- `tdi`: Test data in
- `tdo`: Test data out
- `trst_n`: Test reset

### cJTAG Signals
- `tco`: Combined clock/data
- `tmsc_in`: Data input
- `tmsc_out`: Data output
- `mode_select`: Pin to switch modes

### IDCODE Value
`0x1DEAD3FF` = Version `1`, Part `DEAD`, Mfg `1FF`

### VPI Commands
- `0x01`: Reset TAP
- `0x02`: Shift data
- `0x03`: Read IDCODE
- `0x04`: Set mode
- `0x05`: Get status

## Success!

If you've reached here and all tests pass, your JTAG/cJTAG implementation is working correctly!

For detailed documentation, see [README.md](README.md).
