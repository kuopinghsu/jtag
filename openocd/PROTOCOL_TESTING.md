# JTAG/cJTAG Protocol Testing Guide

This document describes how to test the actual JTAG and cJTAG protocol operations in this project.

## Overview

The project provides two levels of testing:

1. **Connectivity Testing** - Automated via `make test-openocd` and `make test-cjtag`
   - Verifies OpenOCD can connect to VPI server
   - Tests OpenOCD initialization in respective modes
   - Validates TAP/JTAG interface detection
   - Validates telnet interface responsiveness

2. **Protocol Testing** - Manual via `./openocd/test_protocol`
   - Tests actual JTAG protocol commands (TAP reset, IR scan, DR scan)
   - Supports both JTAG and cJTAG modes
   - Uses direct VPI communication (not via OpenOCD)

## Automated Connectivity Testing

### JTAG Mode
```bash
make test-openocd
```

Expected output:
```
✓ OPENOCD CONNECTIVITY TESTS PASSED
OpenOCD connectivity: PASS
✓ OpenOCD JTAG test PASSED
```

### cJTAG Mode
```bash
make test-cjtag
```

Expected output:
```
✓ OPENOCD CONNECTIVITY TESTS PASSED
OpenOCD connectivity: PASS
✓ OpenOCD cJTAG test PASSED
```

## Protocol-Level Testing

### Prerequisites
The protocol test must be compiled and run while VPI server is active:

```bash
# Terminal 1: Start VPI server
make vpi-sim

# Terminal 2: Compile and run protocol tests
gcc -o openocd/test_protocol openocd/test_protocol.c
./openocd/test_protocol jtag   # JTAG protocol test
./openocd/test_protocol cjtag  # cJTAG protocol test
```

### JTAG Protocol Tests

The `test_protocol jtag` command executes:

1. **TAP Reset** - Sends 5 TMS=1 pulses to reset TAP controller
   - Expected: TAP enters Test-Logic-Reset state
   - Validates: Basic VPI command execution

2. **IR Scan** - Loads instruction 0x01 (IDCODE) into instruction register
   - Expected: 8-bit instruction loaded over JTAG
   - Validates: State machine transitions (Capture-IR → Shift-IR → Exit1-IR → Update-IR)

3. **Mode Query** - Reads current active mode (JTAG vs cJTAG)
   - Expected: Returns 0 for JTAG mode
   - Validates: Mode detection capability

4. **IDCODE Read** - Reads 32-bit IDCODE register via VPI command
   - Expected: 0x1DEAD3FF (device ID)
   - Validates: DR capture and shift operations

### cJTAG Protocol Tests

The `test_protocol cjtag` command additionally:
- Sets cJTAG mode before running protocol tests
- Validates OScan1 protocol mode detection
- Ensures cJTAG mode switching works

### Sample Output

```
=== JTAG/cJTAG Protocol Test Client ===
Mode: jtag
Target: 127.0.0.1:3333

✓ Connected to VPI server

=== JTAG Protocol Tests ===

Test 1: JTAG TAP Reset (5 TMS=1 pulses)
  ✓ PASS: TAP controller reset successful
Test 2: JTAG IR Scan (load 0x01 IDCODE instruction)
  ✓ PASS: IR scan executed (loaded instruction 0x01)
Test 3: Query Active Mode (JTAG vs cJTAG)
  Active Mode: JTAG
  ✓ PASS: Mode query successful
Test 4: JTAG Read IDCODE (via command 0x02)
  IDCODE: 0x1dead3ff
  ✓ PASS: IDCODE matches expected value (0x1DEAD3FF)

=== Test Summary ===
Total Tests: 4
Passed: 4
Failed: 0

✓ All tests PASSED
```

## VPI Protocol Details

The test client uses direct VPI protocol (defined in `vpi/jtag_vpi.c`):

### Command Format
```c
typedef struct {
    unsigned char cmd;      // Command code (0x01-0x06)
    unsigned char tms_val;  // TMS signal value
    unsigned char tdi_val;  // TDI signal value
    unsigned char pad;      // Padding/mode select
} jtag_cmd_t;
```

### Response Format
```c
typedef struct {
    unsigned char response; // Echo of command code
    unsigned char tdo_val;  // TDO signal sampled
    unsigned char mode;     // Current mode (0=JTAG, 1=cJTAG)
    unsigned char status;   // Status/extra data
} jtag_resp_t;
```

### Supported Commands
- **0x01**: Set TMS/TDI and pulse TCK
- **0x02**: Read IDCODE register
- **0x03**: Get active mode
- **0x04**: Set mode select
- **0x05**: Get TDO value
- **0x06**: Get debug request status

## Testing Architecture

```
JTAG Implementation
    ↓
JTAG Testbench (jtag_tb.sv)
    ↓
VPI Interface (vpi/jtag_vpi.c)
    ├── Accepts client connections on port 3333
    ├── Processes JTAG protocol commands
    └── Reads/writes signals from/to simulation
    
External Tests
    ├── OpenOCD (via VPI adapter)
    │   └── Used by: make test-openocd, make test-cjtag
    │
    └── test_protocol (direct VPI client)
        ├── Used by: ./openocd/test_protocol jtag/cjtag
        ├── Sends raw JTAG commands
        └── Validates protocol operations
```

## Troubleshooting

### Protocol Test Hangs
- Ensure VPI server is running: `make vpi-sim`
- Check port 3333 is not in use: `lsof -i :3333`
- VPI server may only accept one client at a time

### IDCODE Returns 0x00000000
- Check simulation is running properly
- Verify IDCODE signal is properly connected in testbench
- Try restarting VPI server

### OpenOCD Connection Fails
- Ensure VPI server is already running before test-openocd
- Check OpenOCD version supports VPI adapter
- Verify configuration files in `openocd/*.cfg`

### cJTAG Mode Not Switching
- Mode switching is handled by simulation, not VPI client
- VPI server only relays commands, doesn't implement OScan1 translation
- Full cJTAG support requires custom OpenOCD patches or VPI enhancements

## Implementation Notes

### Single-Client Limitation
The VPI server (as currently implemented) accepts only one client connection at a time. This is why `test_protocol` cannot run simultaneously with OpenOCD.

**Future Enhancement**: Enhance VPI server to support multiple concurrent clients or implement client queuing.

### cJTAG Support Status
- **Simulation**: Full cJTAG (OScan1) protocol implemented in RTL
- **OpenOCD**: Standard version has no cJTAG support
- **VPI Server**: Acts as transparent JTAG relay, not protocol translator
- **Protocol Test**: Can detect cJTAG mode but doesn't validate OScan1 specifics

**To fully support cJTAG**: Would require either:
1. OpenOCD patches to implement cJTAG protocol
2. Custom protocol translator in VPI server
3. Dedicated cJTAG test client

## Related Files

- [sim/jtag_vpi_server.cpp](../sim/jtag_vpi_server.cpp) - VPI server implementation
- [vpi/jtag_vpi.c](../vpi/jtag_vpi.c) - VPI command interface
- [openocd/test_protocol.c](./test_protocol.c) - Protocol test client
- [openocd/test_openocd.sh](./test_openocd.sh) - Automated test script
- [tb/jtag_tb.sv](../tb/jtag_tb.sv) - Testbench with cJTAG mode testing
