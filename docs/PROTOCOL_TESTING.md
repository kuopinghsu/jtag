# JTAG/cJTAG Protocol Testing Guide

This document describes how to test the actual JTAG and cJTAG protocol operations in this project.

## Overview

The project provides two levels of testing:

1. **Connectivity Testing** - Automated via `make test-jtag` and `make test-cjtag`
   - Verifies OpenOCD can connect to VPI server
   - Tests OpenOCD initialization in respective modes
   - Validates TAP/JTAG interface detection
   - Validates telnet interface responsiveness

2. **Protocol Testing** - Manual via `./openocd/test_protocol <mode>`
  - Tests JTAG, cJTAG, or legacy protocols (modes: `jtag`, `cjtag`, `legacy`)
  - Uses direct VPI communication (not via OpenOCD)

## Automated Connectivity Testing

### JTAG Mode
```bash
make test-jtag
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
./openocd/test_protocol jtag     # Modern jtag_vpi protocol
./openocd/test_protocol cjtag    # Two-wire cJTAG OScan1 (CMD_OSCAN1)
./openocd/test_protocol legacy   # Legacy 8-byte protocol
```

### JTAG Protocol Tests (mode: jtag)
- TAP reset via CMD_RESET
- Mode query
- 8-bit SCAN transfer through jtag_vpi

### cJTAG Protocol Tests (mode: cjtag)
- CMD_OSCAN1 availability check
- OAC + JSCAN_OSCAN_ON bring-up
- Bit stuffing, SF0 transfer, CRC-8 check, TAP reset

### Legacy Protocol Tests (mode: legacy)
- Legacy CMD_RESET
- Legacy CMD_SCAN (8-bit payload)

### Sample Output (JTAG)

```
=== Unified Protocol Test Client ===
Mode: jtag
Target: 127.0.0.1:3333

✓ Connected to VPI server

Test 1: JTAG TAP Reset (CMD_RESET)
  ✓ PASS: TAP reset acknowledged
Test 2: JTAG Mode Query (CMD_SET_PORT)
  ✓ PASS: Mode=JTAG
Test 3: JTAG Scan 8 bits (CMD_SCAN)
  ✓ PASS: SCAN completed (TDO captured)

=== Test Summary ===
Total Tests: 3
Passed: 3
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
    │   └── Used by: make test-jtag, make test-cjtag
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
- Ensure VPI server is already running before test-jtag
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
- [openocd/test_protocol.c](./test_protocol.c) - Unified protocol test client (jtag/cjtag/legacy)
- [openocd/test_openocd.sh](./test_openocd.sh) - Automated test script
- [tb/jtag_tb.sv](../tb/jtag_tb.sv) - Testbench with cJTAG mode testing
