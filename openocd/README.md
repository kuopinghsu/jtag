# OpenOCD Configuration for JTAG/cJTAG Testing

This directory contains OpenOCD configuration files for testing the JTAG and cJTAG (OScan1) implementation.

## Quick Start (Automated Testing)

The easiest way to test with OpenOCD:

```bash
# Test JTAG mode (automated - builds, runs sim, tests with OpenOCD)
make test-jtag

# Test cJTAG mode (fully supported as of v2.1)
make test-cjtag

# Test VPI interface only (without OpenOCD)
make test-vpi
```

**Testing Status**:
- **VPI Server**: ✅ Fully functional - accepts connections on port 3333
- **JTAG TAP**: ✅ Hardware implemented and functional in simulation
- **OpenOCD Integration**: ✅ Working - can read IDCODE, run telnet commands
- **Automated Tests**: ✅ All passing (test-vpi, test-jtag)

**cJTAG Testing Status (Updated 2026-01-12)**:
- **Hardware**: ✅ Fully implemented (OScan1 controller, 2-wire operation)
- **VPI Server**: ✅ FIXED - Now correctly handles OpenOCD VPI packets
- **OpenOCD Integration**: ✅ WORKING - All 15 cJTAG tests passing
- **Test Result**: `make test-cjtag` passes 15/15 tests including IR/DR scans

**Recent Fix (v2.1)**:
VPI server packet parsing was fixed to wait for full 1036-byte OpenOCD VPI packets instead of treating 8-byte headers as complete. This resolved the "IR/DR scans returning zeros" issue. See [../FIX_SUMMARY.md](../FIX_SUMMARY.md) for technical details.

**Known Limitations**:
1. **VPI Client Incompatibility**: `jtag_vpi_client.c` uses legacy 4-byte protocol
   - OpenOCD uses 1036-byte jtag_vpi protocol
   - Client will hang when connecting
   - Use OpenOCD for integration testing instead

2. **Optional Enhancement**: Custom OpenOCD patches available
   - See [patched/](patched/) directory for OScan1-specific commands
   - Current OpenOCD integration works without patches
   - Patches add JScan commands and SF format selection

**To Test cJTAG**:
- **Recommended**: Use automated test: `make test-cjtag`
- **Standalone sim**: `make sim` verifies cJTAG mode switching
- **Waveforms**: `gtkwave jtag_sim.fst` to see OScan1 signals
- **OpenOCD**: Fully operational with standard OpenOCD jtag_vpi driver

These commands automatically:
1. Build the VPI simulation if needed
2. Start the simulation server on port 3333
3. Run OpenOCD with the appropriate configuration
4. Execute test commands
5. Report results and clean up

## Prerequisites

1. **OpenOCD with JTAG VPI support**:
   ```bash
   # macOS
   brew install open-ocd

   # Ubuntu/Debian
   sudo apt-get install openocd
   ```

2. **Running VPI simulation**:
   ```bash
   # In terminal 1 - start the simulation
   cd {PROJECT_DIR}
   make vpi-sim
   ```

## Configuration Files

### jtag.cfg
Standard 5-wire JTAG mode configuration:
- Uses JTAG VPI adapter on port 3333
- 8-bit instruction register
- IDCODE: 0x1DEAD3FF
- RISC-V target configuration

### cjtag.cfg
2-wire cJTAG (OScan1) mode configuration:
- Same VPI adapter settings
- Requires mode_select=1 in simulation
- OScan1 protocol handling via VPI bridge

### telnet_test.tcl
Automated test script with:
- `test_jtag` - Complete JTAG test suite
- `test_cjtag` - cJTAG specific tests
- `quick_test` - Fast connectivity check

## Usage

### Automated Testing (Recommended)

Use the Makefile targets for automated testing:

```bash
# JTAG mode test
make test-jtag

# cJTAG mode test
make test-cjtag

# VPI interface test only
make test-vpi
```

These run end-to-end tests automatically and report results.

### Manual Interactive Testing

For interactive debugging and development:

#### Basic JTAG Testing

**Terminal 1** - Start simulation:
```bash
# Default 300 second timeout with verbose output
make vpi-sim

# Quiet mode (suppress cycle status messages)
./build/jtag_vpi --trace --timeout 300 --quiet

# Or with custom timeout (both formats work)
./build/jtag_vpi --trace --timeout 600   # Space-separated
./build/jtag_vpi --trace --timeout=600   # Equals sign
```

**Terminal 2** - Run OpenOCD:
```bash
openocd -f openocd/jtag.cfg
```

**Terminal 3** - Connect and test:
```bash
telnet localhost 4444

# Run test suite
> source openocd/telnet_test.tcl
> test_jtag
```

#### cJTAG (OScan1) Testing

**Terminal 1** - Start simulation in cJTAG mode:
```bash
# Modify testbench to set mode_select=1
make vpi-sim
```

**Terminal 2** - Run OpenOCD with cJTAG config:
```bash
openocd -f openocd/cjtag.cfg
```

**Terminal 3** - Connect and test:
```bash
telnet localhost 4444

# Run cJTAG test suite
> source openocd/telnet_test.tcl
> test_cjtag
```

### VPI Interface Testing (No OpenOCD)

To test the VPI interface without OpenOCD:

```bash
# Run automated VPI client test
make test-vpi

# Or manually in two terminals:
# Terminal 1:
make vpi-sim

# Terminal 2:
./build/jtag_vpi_client
```

This tests the VPI server/client communication directly.

## Expected Output

### Successful JTAG Test
```
=== JTAG Test Suite ===

[Test 1] Scan chain verification...
TapName             Enabled  IdCode     Expected   IrLen IrCap IrMask
-- ---------------  -------  ---------- ---------- ----- ----- ------
 0 riscv.cpu           Y     0x1dead3ff 0x1dead3ff     8 0x01  0x03
  PASS: TAP is enabled

[Test 2] Check TAP state...
  PASS: TAP is enabled

[Test 3] Read IDCODE...
  IDCODE: 0x1dead3ff
  PASS: IDCODE matches expected value

[Test 4] IR scan test...
  PASS: IR scan completed

[Test 5] Examine target...
  PASS: Target examined

=== Test Suite Complete ===
```

### Quick Test
```
> quick_test
=== Quick Connectivity Test ===
TapName             Enabled  IdCode     Expected   IrLen IrCap IrMask
-- ---------------  -------  ---------- ---------- ----- ----- ------
 0 riscv.cpu           Y     0x1dead3ff 0x1dead3ff     8 0x01  0x03
IDCODE: 0x1dead3ff
PASS: Device connected successfully
```

## Manual Commands

Useful OpenOCD commands for interactive testing:

```tcl
# Scan chain information
scan_chain

# Read IDCODE
jtag cget riscv.cpu -idcode

# Check TAP status
jtag tapisenabled riscv.cpu

# IR scan (select IDCODE instruction)
irscan riscv.cpu 0x01

# DR scan (read 32-bit value)
drscan riscv.cpu 32 0

# Reset TAP controller
jtag arp_init-reset

# Examine target
riscv.cpu arp_examine

# Halt target (if debug module supports it)
halt

# Read memory (requires working debug module)
mdw 0x80000000 16

# Show target state
targets
```

## Troubleshooting

### "Simulation timeout reached"
- **Cause**: VPI simulation reached its timeout limit (default: 300 seconds for interactive, 60 seconds for automated tests)
- **Fix**:
  ```bash
  # Increase timeout for manual testing (both formats supported)
  ./build/jtag_vpi --trace --timeout 600    # Space-separated
  ./build/jtag_vpi --trace --timeout=3600   # Equals sign (1 hour)
  ```

### "Error: JTAG tap: riscv.cpu tap/device found: 0x00000000"
- **Cause**: VPI simulation not running or not connected
- **Fix**: Start `make vpi-sim` first, verify port 3333 is listening

### "Error: Connection refused"
- **Cause**: VPI server port not available
- **Fix**:
  ```bash
  # Check if port is in use
  lsof -i:3333

  # Kill existing process if needed
  lsof -ti:3333 | xargs kill
  ```

### "Error: IDCODE mismatch"
- **Cause**: Wrong device or communication error
- **Fix**:
  - Verify simulation is running correctly
  - Check waveforms for proper signal timing
  - Ensure mode_select is set correctly for JTAG/cJTAG

### cJTAG Mode Status
- **Status**: ✅ Fully operational after VPI packet parsing fix (v2.1)
- **Tests**: All 15 cJTAG tests passing with standard OpenOCD
- **Hardware**: OScan1 controller fully implemented and validated
- **VPI Server**: Correctly handles 1036-byte OpenOCD VPI packets
- **Testing**: Use `make test-cjtag` to run automated test suite
- **See**: [../FIX_SUMMARY.md](../FIX_SUMMARY.md) for technical details of v2.1 fix

## Advanced Configuration

### Custom Target Configuration

For different RISC-V cores, modify the cfg file:

```tcl
# Example: Multi-hart configuration
target create riscv0.cpu riscv -chain-position riscv.cpu
target create riscv1.cpu riscv -chain-position riscv.cpu

# Set active hart
riscv.cpu riscv set_current_hartid 0
```

### Debug Module Access

```tcl
# DMI register access (if supported)
riscv.cpu riscv dmi_read 0x10   # Read DMCONTROL
riscv.cpu riscv dmi_write 0x10 0x80000001  # Write DMCONTROL
```

## Integration with GDB

Once OpenOCD is running, connect GDB:

```bash
riscv64-unknown-elf-gdb

(gdb) target extended-remote localhost:3333
(gdb) info registers
(gdb) x/16x 0x80000000
```

## References

- OpenOCD User Guide: https://openocd.org/doc/
- JTAG VPI Adapter: https://github.com/fjullien/jtag_vpi
- RISC-V Debug Spec: https://github.com/riscv/riscv-debug-spec
- IEEE 1149.7 (cJTAG/OScan1 Standard)
