# JTAG/cJTAG Protocol Testing Guide

This document describes how to test the actual JTAG and cJTAG protocol operations in this project.

## Current Status (2026-01-12)

✅ **All Tests Passing**
- JTAG OpenOCD Integration: **19/19 PASSED**
- cJTAG OpenOCD Integration: **15/15 PASSED**
- Core JTAG Testbench: **All 18 tests PASSED**
- System Integration Testbench: **All 12 tests PASSED**
- VPI Packet Handling: **FIXED** - Full 1036-byte OpenOCD packets now correctly processed

**Recent Fix**: The VPI server packet parsing issue has been resolved. The server now correctly waits for full 1036-byte OpenOCD VPI packets instead of treating 8-byte headers as complete packets. This fixed the "cJTAG IR/DR scans returning zeros" issue (v2.1 fix).

## Testbench Test Coverage

The project includes comprehensive testbenches covering both basic and advanced protocol features:

### JTAG Core Testbench (tb/jtag_tb.sv)
**18 comprehensive tests** covering basic JTAG operations and advanced OScan1 features:

**Basic JTAG Tests (1-10)**:
- Test 1: TAP controller reset
- Test 2: IDCODE read (32-bit JTAG mode)
- Test 3: Debug request
- Test 4: IDCODE read (cJTAG mode)
- Test 5: Return to JTAG mode
- Test 6: IR scan - BYPASS instruction
- Test 7: DR scan with BYPASS register
- Test 8: IR scan - IDCODE instruction
- Test 9: DR scan - IDCODE register
- Test 10: IR scan - DTMCS instruction

**Protocol Switching Tests (11-12)**:
- Test 11: Switch to cJTAG mode and read IDCODE
- Test 12: Return to JTAG mode

**OScan1 Advanced Tests (13-18)**:
- Test 13: OScan1 OAC detection and protocol activation
- Test 14: OScan1 JScan command processing
- Test 15: OScan1 SF0 (Scanning Format 0) protocol
- Test 16: OScan1 zero stuffing (bit insertion/deletion)
- Test 17: JTAG ↔ cJTAG protocol switching stress test
- Test 18: Protocol boundary conditions testing

### System Integration Testbench (tb/system_tb.sv)
**12 system-level tests** covering full system integration:

**Basic System Operations (1-6)**:
- Test 1: TAP controller reset
- Test 2: System initialization
- Test 3: IDCODE register read
- Test 4: DMSTATUS register access
- Test 5: Hart state control (halt/resume)
- Test 6: DMI register write/read

**cJTAG Mode Operations (7-10)**:
- Test 7: Switch to cJTAG mode
- Test 8: IDCODE verification in cJTAG mode
- Test 9: DMI access in cJTAG mode
- Test 10: Hart control in cJTAG mode

**Mode Verification (11-12)**:
- Test 11: Protocol mode verification
- Test 12: System stress test

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
=== Final Test Summary ===
Total Tests:  19
Passed:       19
Failed:       0

✅ ALL TESTS PASSED```

### cJTAG Mode
```bash
make test-cjtag
```

Expected output:
```
✓ OPENOCD CONNECTIVITY TESTS PASSED
OpenOCD connectivity: PASS
✓ OpenOCD cJTAG test PASSED
=== Final Test Summary ===
Total Tests:  15
Passed:       15
Failed:       0

✅ ALL TESTS PASSED```

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

### cJTAG Support Status (Updated 2026-01-12)
- **Simulation**: ✅ Full cJTAG (OScan1) protocol implemented in RTL and WORKING
- **VPI Server**: ✅ FIXED - Now correctly handles full OpenOCD VPI packets
- **OpenOCD Integration**: ✅ WORKING - All 15 cJTAG tests pass
- **Packet Parsing**: ✅ FIXED - VPI server now waits for complete 1036-byte packets
- **SF0 Protocol**: ✅ WORKING - Two-phase bit protocol (TCKC rising/falling) operational
- **IR/DR Scans**: ✅ FIXED - Now return correct data (was returning zeros)

**What Was Fixed**:
- VPI server was treating 8-byte packet headers as complete packets
- Changed protocol detection to wait for full 1036-byte OpenOCD VPI packets
- Both JTAG and cJTAG modes now fully functional with OpenOCD

**To fully support cJTAG in stock OpenOCD** (optional enhancement):
1. Apply OpenOCD patches from [../openocd/patched/](../openocd/patched/)
2. Custom OScan1 protocol layer (oscan1.c/oscan1.h)
3. See [OPENOCD_CJTAG_PATCH_GUIDE.md](OPENOCD_CJTAG_PATCH_GUIDE.md) for details

Note: Current implementation works with standard OpenOCD via the fixed VPI server.

## Related Files

- [sim/jtag_vpi_server.cpp](../sim/jtag_vpi_server.cpp) - VPI server implementation
- [vpi/jtag_vpi.c](../vpi/jtag_vpi.c) - VPI command interface
- [openocd/test_protocol.c](./test_protocol.c) - Unified protocol test client (jtag/cjtag/legacy)
- [openocd/test_openocd.sh](./test_openocd.sh) - Automated test script
- [tb/jtag_tb.sv](../tb/jtag_tb.sv) - Testbench with cJTAG mode testing
