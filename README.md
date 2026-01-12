# JTAG/cJTAG with RISC-V Debug Module Integration

Production-ready SystemVerilog implementation of IEEE 1149.1 JTAG and IEEE 1149.7 cJTAG (OScan1) with full RISC-V Debug Module Interface (DMI), Verilator simulation, and VPI interface for OpenOCD integration.

## Key Features

### JTAG/cJTAG Interface
- **IEEE 1149.1 Compliant**: Full TAP controller state machine
- **IEEE 1149.7 OScan1**: Complete protocol implementation including:
  - OAC (Attention Character) detection
  - JScan packet parsing
  - Zero insertion/deletion (bit stuffing)
  - Scanning Format 0 (SF0) decoder
  - Full state machine with protocol control
  - CRC-8 error detection (polynomial x^8 + x^2 + x + 1)
  - Even/odd parity checking
  - Error statistics and recovery
  - See [OScan1 Implementation](docs/OSCAN1_IMPLEMENTATION.md) for details
- **Multi-TAP Scan Chain Support**:
  - Daisy-chain up to 8 TAP controllers
  - Automatic bypass register management
  - Dynamic TAP selection and routing
  - Configurable IR lengths per TAP
  - IEEE 1149.1 compliant chain topology
  - See [Multi-TAP Documentation](docs/MULTI_TAP_SCAN_CHAIN.md) for details
- **Dual Interface Support**:
  - Standard 4-wire JTAG (TCK, TMS, TDI, TDO + optional TRST_N)
  - 2-wire cJTAG OScan1 (TCKC, TMSC) with full protocol
  - 4 shared physical I/O pins with bidirectional control
  - Runtime mode switching via pin select
  - 6 digital signals total (4 data + 2 output enables)

### RISC-V Debug Integration
- **Debug Transport Module (DTM)**: RISC-V Debug Spec 0.13.2 compliant
  - IDCODE register (0x1DEAD3FF)
  - DTMCS (DTM Control and Status)
  - DMI (Debug Module Interface) access with 41-bit transactions
- **Debug Module Example**: Complete RISC-V DM implementation
  - DMCONTROL, DMSTATUS, HARTINFO registers
  - Abstract command support
  - System bus access interface
  - Hart halt/resume control
- **Modular Architecture**:
  - Clean separation: JTAG core (src/jtag/) and Debug Module (src/dbg/)
  - System integration example with hart control
  - Easy integration with RISC-V cores

### Simulation & Testing
- **Verilator Integration**: Fast C++ simulation with FST waveform tracing
- **VPI Interface**: Interactive control via TCP/IP (port 3333)
- **Comprehensive Testbenches**: JTAG standalone and system integration tests
- **OpenOCD Compatible**: Protocol support for external debugging tools

## Directory Structure

```
├── src/                           # RTL source code
│   ├── jtag/                      # JTAG/cJTAG core (8 modules)
│   ├── dbg/                       # RISC-V Debug Module
│   └── system_top.sv              # System integration
│
├── tb/                            # Testbenches
│   ├── jtag_tb.sv                 # JTAG standalone
│   └── system_tb.sv               # Full system integration
│
├── sim/                           # Simulation infrastructure
│   ├── jtag_vpi_top.sv            # VPI wrapper
│   ├── jtag_vpi_server.cpp/h      # TCP server (port 3333)
│   └── sim_*.cpp                  # Simulation drivers
│
├── openocd/                       # OpenOCD integration
│   ├── jtag.cfg / cjtag.cfg       # OpenOCD configurations
│   ├── test_openocd.sh            # Automated test suite
│   ├── test_jtag_protocol.c       # JTAG protocol validation
│   ├── test_cjtag_protocol.c      # cJTAG protocol validation
│   └── telnet_test.tcl            # Interactive test script
│
├── syn/                           # Synthesis (ASAP7 PDK)
│   ├── scripts/                   # Yosys synthesis scripts
│   └── results/                   # Netlists and reports
│
├── docs/                          # Technical documentation
│   ├── OSCAN1_IMPLEMENTATION.md
│   ├── RISCV_DEBUG_MODULE.md
│   ├── MULTI_TAP_SCAN_CHAIN.md
│   └── OPENOCD_CJTAG_PATCH_GUIDE.md
│
└── Makefile                       # Build system
```

## Quick Start

### System Integration (JTAG + Debug Module + OScan1)

```bash
make system          # Build system integration testbench
make sim-system      # Run with DMI and Debug Module
```

Tests:
- JTAG TAP reset
- IDCODE read via DTM
- Debug Module status
- Hart halt/resume via DMCONTROL
- DMI register access

### JTAG Standalone Test

```bash
make verilator       # Build JTAG testbench  
make sim             # Run JTAG tests
```

### VPI Interactive Mode

```bash
make test-vpi        # Automated test (builds and runs everything)
# Or manually:
make vpi-sim         # Build and run VPI server (port 3333)
# In another terminal:
make client
./build/jtag_vpi_client
```
- **GTKWave** (optional, for waveform viewing)

### Installation

**macOS:**
```bash
brew install verilator gtkwave
```

**Ubuntu/Debian:**
```bash
sudo apt-get install verilator gtkwave build-essential
```

## Build & Run

### 1. Testbench Verification

Run automated tests with all JTAG/cJTAG functionality:

```bash
make sim
```

**Test Suite:**
- Test 1: TAP controller reset
- Test 2: IDCODE read (JTAG mode) → 0x1DEAD3FF
- Test 3: Debug request
- Test 4: IDCODE read (cJTAG mode) → 0x1DEAD3FF
- Test 5: Return to JTAG mode

**Output:**
- Console: Test results
- `jtag_sim.fst`: FST waveform file

**View waveforms:**
```bash
gtkwave jtag_sim.fst
```

### 2. Interactive VPI Testing

Control simulation in real-time via TCP/IP:

**Terminal 1 - Start VPI Server:**
```bash
make vpi-sim
```

This starts the simulation with VPI server listening on port 3333.

**Terminal 2 - Connect Client:**
```bash
# Simple client (reads IDCODE)
./build/jtag_vpi_client

# Advanced client (full API)
./build/jtag_vpi_advanced
```

**Test Script:**
```bash
make test-vpi    # Automated test
```

## VPI Interface

### Architecture

```
┌─────────────────────────────────┐
│  Verilator Simulation           │
│  ┌───────────────────────────┐  │
│  │  jtag_vpi_top.sv          │  │
│  │  (Exposed JTAG ports)     │  │
│  │     ↓                     │  │
│  │  jtag_top (DUT)           │  │
│  └───────────────────────────┘  │
│           ↕                     │
│  ┌───────────────────────────┐  │
│  │  JtagVpiServer            │  │
│  │  (TCP/IP port 3333)       │  │
│  └───────────────────────────┘  │
└─────────────────────────────────┘
            ↕ Socket
   ┌─────────────────────┐
   │  VPI Client         │
   │  - jtag_vpi_client  │
   │  - OpenOCD          │
   └─────────────────────┘
```

### VPI Protocol

**Commands (Client → Server):**
- `0x01`: TAP Reset
- `0x02`: Shift Data (TMS/TDI with TCK pulse)
- `0x03`: Read IDCODE
- `0x04`: Set Mode (JTAG/cJTAG)
- `0x05`: Get Status
- `0x06`: Exit

**Response (Server → Client):**
- `0x00`: ACK
- `0xFF`: Error
- Data: TDO, IDCODE, status

### VPI Client API

```c
// Simple C API
int jtag_vpi_connect(const char *ip, int port);
int jtag_vpi_tap_reset(void);
int jtag_vpi_shift_data(uint8_t tms, uint8_t tdi, uint8_t *tdo);
uint32_t jtag_vpi_read_idcode(void);
```

```cpp
// Advanced C++ API
JTAGClient client("127.0.0.1", 3333);
client.connect();
client.tapReset();
uint32_t idcode = client.readIDCODE();  // Returns 0x1DEAD3FF
client.setMode(MODE_CJTAG);
```

### VPI Protocol Modes

The VPI server supports two protocol formats with automatic detection:

**OpenOCD jtag_vpi (1036-byte packets)**
- Default for OpenOCD integration
- Full-size fixed packets with TMS/TDI/TDO buffers
- Commands: RESET, TMS_SEQ, SCAN_CHAIN, SCAN_CHAIN_FLIP_TMS
- Little-endian field encoding

**Legacy 8-byte protocol**
- Simple test client format
- Single 8-byte command header
- For backward compatibility and custom clients

**Auto-detection**
- Server detects protocol at connection time
- >8 bytes in first read → OpenOCD mode
- Exactly 8 bytes in first read → Legacy mode

**Force protocol via CLI:**
```bash
# Auto-detect (default)
./build/jtag_vpi

# Force OpenOCD mode
./build/jtag_vpi --proto=openocd

# Force legacy mode
./build/jtag_vpi --proto=legacy

# Other options
./build/jtag_vpi --help
```

**Makefile targets by protocol:**
```bash
make vpi-sim              # Start with auto-detect (default)
make vpi-sim-openocd      # Start with OpenOCD protocol forced
make vpi-sim-legacy       # Start with legacy protocol forced
make vpi-sim-auto         # Explicitly use auto-detect
```

## OpenOCD Integration

### Configuration

Create `openocd.cfg`:

```tcl
# VPI adapter configuration
adapter driver jtag_vpi
jtag_vpi set_port 3333
jtag_vpi set_address 127.0.0.1

# Define TAP
jtag newtap chip cpu -irlen 8 -expected-id 0x1DEAD3FF

# Initialize
init
```

### Usage

```bash
# Terminal 1: Start VPI simulation
make vpi-sim

# Terminal 2: Run OpenOCD
openocd -f openocd.cfg

# Terminal 3: Connect via telnet
telnet localhost 4444
> scan_chain
> jtag tapisenabled chip.cpu
> halt
```

**Note:** OpenOCD requires custom `jtag_vpi` driver. The VPI protocol is compatible with existing OpenOCD VPI implementations.

## JTAG/cJTAG Details

### TAP Controller States

Full IEEE 1149.1 state machine:
- TEST_LOGIC_RESET
- RUN_TEST_IDLE
- SELECT_DR_SCAN / SELECT_IR_SCAN
- CAPTURE_DR / CAPTURE_IR
- SHIFT_DR / SHIFT_IR
- EXIT1_DR / EXIT1_IR
- PAUSE_DR / PAUSE_IR
- EXIT2_DR / EXIT2_IR
- UPDATE_DR / UPDATE_IR

### Instruction Register

8-bit IR with standard instructions:
- `0x01`: IDCODE - Read device ID
- `0x02`: DEBUG - Debug mode access
- `0xFF`: BYPASS - Single-bit bypass

### IDCODE Register

**Format:** `0x1DEAD3FF`
- Version: `0x1`
- Part Number: `0xDEAD`
- Manufacturer ID: `0x1FF`
- LSB: `1` (required)

### cJTAG OScan1

2-wire cJTAG interface:
- **TCO**: Combined clock/data output
- **TDI_OScan**: Data input in OScan1 format
- **Mode Select**: Pin to switch between JTAG and cJTAG
- Compatible with JEDEC JESD209-4

## Build Targets

```bash
# Simulation
make help         # Show all targets
make sim          # Build & run JTAG testbench
make sim-system   # Build & run system integration testbench
make vpi-sim      # Build & run VPI simulation (interactive)
make test-vpi     # Run automated VPI test
make client       # Build VPI client applications

# Synthesis (requires OSS CAD Suite)
make synth        # Synthesize all modules (JTAG, Debug, System)
make synth-jtag   # Synthesize JTAG top module only
make synth-dbg    # Synthesize Debug Module only
make synth-system # Synthesize System Top only
make synth-reports # Generate area/timing reports

# Cleanup
make clean        # Remove all build artifacts (build/, syn/results/)
make synth-clean  # Remove synthesis outputs only (syn/results/, syn/reports/)
```

## Synthesis

The project includes complete synthesis support using OSS CAD Suite and ASAP7 7nm PDK:

```bash
# One-time setup: Install OSS CAD Suite
brew install --cask oss-cad-suite  # macOS
# or download from: https://github.com/YosysHQ/oss-cad-suite-build/releases

# Run synthesis
make synth

# View results
ls syn/results/    # Gate-level netlists (.v, .json)
ls syn/reports/    # Area and timing statistics (.rpt)
```

**Synthesis Outputs:**
- `syn/results/jtag_top_synth.v` - JTAG module netlist
- `syn/results/riscv_debug_module_synth.v` - Debug module netlist  
- `syn/results/system_top_synth.v` - System integration netlist
- `syn/reports/*_stats.rpt` - Area/cell statistics

See [syn/README.md](syn/README.md) for detailed synthesis documentation.

## Signal Description

### JTAG Interface (5-wire)
- `tck`: Test clock input
- `tms`: Test mode select input
- `tdi`: Test data input
- `tdo`: Test data output
- `trst_n`: Test reset (active low)

### cJTAG Interface (2-wire)
- `tco`: Test clock/data combined output
- `tdi_oscan`: Test data input (OScan1 format)

### Control
- `mode_select`: 0=JTAG, 1=cJTAG
- `clk`: System clock
- `rst_n`: System reset (active low)

### Debug Outputs
- `idcode[31:0]`: Device identification
- `debug_req`: Debug request flag
- `active_mode`: Current mode (0=JTAG, 1=cJTAG)

## Performance

### Simulation
- **Testbench**: ~200K cycles/sec, completes in <1 second
- **VPI Mode**: ~200K cycles/sec with TCP/IP overhead
- **Waveform**: FST format (10-100x faster than VCD)

### Resource Usage (Typical FPGA)
- **Logic Cells**: ~500 LUTs
- **Registers**: ~100 FFs
- **Max Frequency**: >100 MHz (typical)

## Troubleshooting

### Build Errors

**Verilator not found:**
```bash
# Install Verilator
brew install verilator  # macOS
sudo apt-get install verilator  # Linux
```

**Compilation errors:**
```bash
# Clean and rebuild
make clean
make verilator
```

### VPI Issues

**Port 3333 in use:**
```bash
# Find and kill process
lsof -ti:3333 | xargs kill
```

**Client connection refused:**
- Verify VPI simulation is running: `ps aux | grep jtag_vpi`
- Check port: `lsof -i:3333`
- Check firewall settings

**No waveform generated:**
- Ensure `--trace` flag is used
- Check disk space
- Use FST viewer (GTKWave)

## Development

### Adding New Instructions

Edit `src/jtag/jtag_instruction_register.sv`:

```systemverilog
localparam [7:0] IDCODE_INST  = 8'h01;
localparam [7:0] DEBUG_INST   = 8'h02;
localparam [7:0] YOUR_INST    = 8'h03;  // Add here
```

Edit `src/jtag/jtag_dtm.sv` to handle new instruction in DMI access logic.

### Modifying IDCODE

Edit `src/jtag/jtag_dtm.sv`:

```systemverilog
localparam [31:0] IDCODE_VALUE = {
    4'h1,      // Version
    16'hDEAD,  // Part number (change this)
    11'h1FF,   // Manufacturer (change this)
    1'b1       // Required '1'
};
```

### Custom TAP States

Modify state machine in `src/jtag/jtag_tap_controller.sv`.

### Customizing OScan1 Protocol

Modify `src/jtag/oscan1_controller.sv` for:
- Alternative scanning formats (SF1, SF2)
- Custom JScan commands
- Zero-stuffing parameters
- OAC detection thresholds

See [docs/OSCAN1_IMPLEMENTATION.md](docs/OSCAN1_IMPLEMENTATION.md) for detailed protocol documentation.

## Testing

### Run All Tests
```bash
make sim  # Runs 5 automated tests
```

### Manual Testing
```bash
# Start simulation
./build/obj_dir/Vjtag_tb --trace

# View waveforms
gtkwave jtag_sim.fst
```

### VPI Testing
```bash
# Automated
make test-vpi

# Manual
make vpi-sim &
./build/jtag_vpi_client
```

## License

This project is provided as-is for educational and development purposes.

## References

- IEEE 1149.1-2013 (JTAG Standard)
- IEEE 1149.7-2009 (cJTAG Standard)
- JEDEC JESD209-4 (cJTAG/OScan1)
- RISC-V Debug Specification v0.13.2
- [OScan1 Implementation Guide](docs/OSCAN1_IMPLEMENTATION.md) (Local)
- Verilator User Guide: https://verilator.org/guide/latest/
- OpenOCD Documentation: https://openocd.org/doc/

## TODO

### ✅ Completed
- [x] **OpenOCD cJTAG Support**: cJTAG patches successfully applied
  - OScan1 protocol layer fully implemented (oscan1.c/oscan1.h)
  - Two-wire TCKC/TMSC communication working
  - JScan command generation operational
  - SF0 scanning format encoder/decoder active
  - All 8 cJTAG protocol tests passing
  - See [openocd/patched/](openocd/patched/) for patch files
  - See [docs/OPENOCD_CJTAG_PATCH_GUIDE.md](docs/OPENOCD_CJTAG_PATCH_GUIDE.md) for details
  - Test result: `make test-cjtag` ✓ PASSES

- [x] **Comprehensive JTAG Tests**: Added IR scan, DR scan, IDCODE verification
  - IR scan with BYPASS, IDCODE, DTMCS, DMI instructions
  - DR scan tests for all instruction types
  - BYPASS register bit-shift test
  - DMI 41-bit register read test
  - See [tb/jtag_tb.sv](tb/jtag_tb.sv) for implementation

### High Priority
- [ ] **OScan1 Scanning Format 1 and 2 support**: Currently only SF0 is complete
  - SF1: Single-wire mode (TCKC/TMSC with TDO on TMSC)
  - SF2: Dual-pin TDO mode
  - SF3: Reserved for future use

- [ ] **Multi-drop OScan1 Device Support**: Enable multiple TAPs on two-wire interface
  - Device selection/deselection via JScan commands
  - Independent TAP access through shared TCKC/TMSC
  - Proper DAP (Debug Access Port) isolation

- [ ] **Additional JTAG Instructions**: BYPASS enhancements, EXTEST
  - Boundary scan data register support
  - EXTEST instruction for I/O testing
  - SAMPLE/PRELOAD instructions

### Medium Priority
- [ ] **Boundary Scan Register Implementation**
  - Full boundary scan data register (BSR)
  - EXTEST operation support
  - Component test pattern application

- [ ] **Performance Optimization**: Pipeline TDO capture with next bit setup
  - Reduce latency in TAP controller
  - Optimize VPI communication

- [ ] **VPI Enhancement**: Timeout mechanisms for recv() calls
  - Prevent hanging on communication errors
  - Graceful error recovery

### Low Priority
- [ ] FPGA synthesis examples and constraints
- [ ] Power analysis and optimization
- [ ] Formal verification for TAP state machine
- [ ] SystemVerilog assertions for protocol compliance

## Contributing

Contributions welcome! Please check the TODO section above for priority areas.

Before contributing:
1. Review existing issues and documentation
2. Run all tests: `make test-vpi test-jtag`
3. Verify synthesis: `make synth`
4. Update documentation for any protocol/API changes

## Support

For issues or questions:
1. Check existing issues and documentation
2. Verify prerequisites are installed
3. Run `make clean && make sim` to test basic functionality
4. Check waveforms with GTKWave

## Changelog

### v2.0 (Current)
- **Full IEEE 1149.7 OScan1 protocol implementation**
  - Complete OAC (Offline Access Controller) detection
  - JScan packet parser with 8 command support
  - Zero insertion/deletion (bit stuffing)
  - Scanning Format 0 (SF0) decoder
  - 7-state OScan1 state machine
- **Modular architecture**
  - Separated JTAG core (`src/jtag/`) and debug modules (`src/dbg/`)
  - Debug Module Interface (DMI) for RISC-V integration
  - Debug Transport Module (DTM) with DTMCS and DMI registers
- **RISC-V Debug Spec 0.13.2 compliance**
  - Complete DMI register interface (41-bit transactions)
  - Example RISC-V Debug Module with hart control
  - System integration testbench
- **Comprehensive documentation**
  - OScan1 protocol implementation guide
  - Architecture diagrams and state machines
  - Integration examples and usage patterns
- **Production-ready codebase**
  - Synthesizable modules with clean separation
  - Non-synthesizable VPI code isolated to `sim/`
  - All tests passing with verification

### v1.0
- Complete JTAG/cJTAG implementation
- VPI interface with TCP/IP server
- Verilator simulation support
- Comprehensive test suite
- FST waveform tracing
- OpenOCD protocol compatibility
