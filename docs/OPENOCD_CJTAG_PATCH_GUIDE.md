# OpenOCD cJTAG/OScan1 Patch Guide

This document describes what is needed to add IEEE 1149.7 cJTAG support to OpenOCD. Patch files and reference implementations are in [openocd/patched/](../openocd/patched/).

## Quick Start

⚠️ **Important**: The unified patch may not apply directly to your OpenOCD version. Use the automated script method:

```bash
# Try Unified Patch (May Fail)
cd {OPENOCD_DIR}
patch -p1 < /path/to/jtag/openocd/patched/001-jtag_vpi-cjtag-support.patch
# If this fails, use follow steps 1-10 in MANUAL_APPLICATION_GUIDE.md

# Create source files
cat /path/to/jtag/openocd/patched/002-oscan1-new-file.txt > src/jtag/drivers/oscan1.c
cat /path/to/jtag/openocd/patched/003-oscan1-header-new-file.txt > src/jtag/drivers/oscan1.h

# Build
./configure --enable-jtag_vpi --enable-internal-jimtcl && make clean && make -j4 && sudo make install
```

See [openocd/patched/MANUAL_APPLICATION_GUIDE.md](../openocd/patched/MANUAL_APPLICATION_GUIDE.md) for detailed manual instructions.

## Current Status

### ✓ Hardware Ready
- **OScan1 Controller**: Fully implemented in `src/jtag/oscan1_controller.sv`
- **Features**: OAC detection, JScan parser, SF0 decoder, zero deletion, CRC-8
- **Interface**: Two-wire TCKC/TMSC with proper bidirectional support
- **Validation**: Hardware simulation tested with VPI server

### ✓ Patch Files Available
- **001-jtag_vpi-cjtag-support.patch**: Main driver modifications
- **002-oscan1-new-file.txt**: OScan1 protocol implementation
- **003-oscan1-header-new-file.txt**: OScan1 protocol header
- **Location**: [openocd/patched/](../openocd/patched/)

### ✓ Standard OpenOCD (Updated 2026-01-12)
- **OpenOCD**: Standard jtag_vpi adapter now WORKS with cJTAG after VPI fix
- **Protocol**: VPI server correctly handles 1036-byte packets (fixed in v2.1)
- **VPI**: cJTAG operations fully functional without patches
- **Test Status**: `make test-cjtag` passes 15/15 tests

### Optional Patches Available
- **Purpose**: Add explicit JScan commands and SF format selection to OpenOCD
- **Status**: Optional enhancement - not required for basic cJTAG operation
- **Benefit**: Provides more granular control over OScan1 protocol
- **Location**: [openocd/patched/](../openocd/patched/)

## Test Results (v2.1 Status)

Standard OpenOCD (no patches):
```bash
make test-jtag   # ✓ PASSES - 19/19 tests
make test-cjtag  # ✓ PASSES - 15/15 tests (FIXED in v2.1)
```

**What Changed**: VPI server packet parsing was fixed to wait for full 1036-byte OpenOCD VPI packets instead of treating 8-byte headers as complete. This resolved the "IR/DR scans returning zeros" issue (v2.1 fix).

With optional patches applied (enhanced features):
```bash
make test-jtag   # ✓ PASSES - Standard JTAG still works
make test-cjtag  # ✓ PASSES - cJTAG with enhanced OScan1 commands
```

**Patches provide**: Explicit JScan command generation, SF format selection, more granular control. Basic cJTAG operation works without patches.

## Patch Application

For detailed instructions on applying the OpenOCD cJTAG patches, see:
- **[openocd/patched/MANUAL_APPLICATION_GUIDE.md](../openocd/patched/MANUAL_APPLICATION_GUIDE.md)** - Complete step-by-step manual application guide

The manual guide provides:
- Detailed patch file descriptions and contents
- Step-by-step application instructions for each patch
- Build system integration steps
- Troubleshooting for common issues
- Verification procedures

## Testing Strategy

### Quick Test (after applying patches)
```bash
# Start VPI simulation
make vpi-sim &
sleep 2

# Test both modes
make test-jtag    # Standard JTAG (regression test)
make test-cjtag   # cJTAG with OScan1 (new capability)

# Both should pass with all tests green
```

### Full Test Suite
```bash
# With patches applied and OpenOCD built
make test-jtag    # Should pass all tests
make test-cjtag   # Should pass all tests
```

### Integration Tests
```bash
# Test standard JTAG still works (no regression)
make test-jtag

# Test cJTAG mode with full protocol support
make test-cjtag

# Test with OpenOCD CLI (interactive)
openocd -d2 -f openocd/cjtag.cfg
# Should successfully:
# 1. Connect via two-wire TCKC/TMSC
# 2. Initialize OScan1 (send OAC)
# 3. Read IDCODE over SF0
# 4. Access JTAG TAP states via two-wire
```

### Validation Criteria

**JTAG Mode Tests** (make test-jtag):
```
✓ OpenOCD VPI Connection
✓ OpenOCD Initialization
✓ JTAG Interface Detection
✓ Telnet Interface Responsive
✓ JTAG Protocol Tests (8/8)
```

**cJTAG Mode Tests** (make test-cjtag):
```
✓ OpenOCD VPI Connection
✓ OpenOCD Initialization
✓ JTAG Interface Detection
✓ Telnet Interface Responsive
✓ cJTAG Protocol Tests (8/8):
  ✓ Two-wire mode detection
  ✓ OAC sequence (16 TCKC edges)
  ✓ JScan OSCAN_ON command
  ✓ Zero insertion/deletion
  ✓ SF0 TMS/TDI encoding
  ✓ CRC-8 error detection
  ✓ Full cJTAG TAP reset sequence
  ✓ Mode select flag verification
```

**Success**:
```bash
$ make test-cjtag
...
=== Final Test Summary ===
OpenOCD connectivity: PASS
cJTAG protocol:       PASS

✓ ALL TESTS PASSED
```

## Hardware Interface

### Pin Mapping
```
Standard JTAG (4-wire):        cJTAG OScan1 (2-wire):
├─ Pin 0: TCK (input)          ├─ Pin 0: TCKC (input)
├─ Pin 1: TMS (input)          ├─ Pin 1: TMSC (bidir)
├─ Pin 2: TDI (input)          └─ (Pins 2-3 not used)
└─ Pin 3: TDO (output)
```

### VPI Server Support
The VPI server supports both modes:
- **JTAG mode**: `./build/jtag_vpi --timeout 300`
- **cJTAG mode**: `./build/jtag_vpi --timeout 300 --cjtag`

When `--cjtag` flag is set:
- Sets `mode_select=1` in simulation
- OScan1 controller is activated
- Two-wire interface becomes active
- Waiting for OAC to enter JScan mode

## Validation Criteria

All 8 protocol tests must pass:
1. ✓ Two-wire mode detection
2. ✓ OAC sequence (16 TCKC edges)
3. ✓ JScan OSCAN_ON command
4. ✓ Zero insertion/deletion
5. ✓ SF0 TMS/TDI encoding
6. ✓ CRC-8 error detection
7. ✓ Full cJTAG TAP reset sequence
8. ✓ Mode select flag verification

**Success criteria**:
```bash
$ make test-cjtag
...
Total Tests:  8
Passed:       8
Failed:       0

✓ ALL TESTS PASSED - OpenOCD has cJTAG support!
```

## References

### Patch Files
All patches and reference implementations are in [openocd/patched/](../openocd/patched/):
- **README.md**: Detailed application instructions and troubleshooting
- **001-jtag_vpi-cjtag-support.patch**: Main driver patch (unified diff)
- **002-oscan1-new-file.txt**: OScan1 protocol implementation (~300 lines)
- **003-oscan1-header-new-file.txt**: OScan1 protocol header (~100 lines)

### Standards
- **IEEE 1149.7-2009**: Standard for Reduced-Pin and Enhanced-Functionality Test Access Port and Boundary-Scan Architecture
- **Section 5**: OScan1 Protocol Definition
- **Section 6**: Scanning Formats (SF0, SF1, SF2, SF3)
- **Appendix B**: JScan Command Set

### Project Documentation
- [openocd/patched/README.md](../openocd/patched/README.md): Patch application guide
- [docs/OSCAN1_IMPLEMENTATION.md](OSCAN1_IMPLEMENTATION.md): Hardware implementation details
- [docs/CJTAG_CRC_PARITY.md](CJTAG_CRC_PARITY.md): Error detection mechanisms
- [src/jtag/oscan1_controller.sv](../src/jtag/oscan1_controller.sv): Reference hardware implementation
- [openocd/test_protocol.c](../openocd/test_protocol.c): Unified protocol test suite

### OpenOCD Resources
- OpenOCD source: https://github.com/openocd-org/openocd
- Developer guide: https://openocd.org/doc/doxygen/html/
- Driver examples: https://github.com/openocd-org/openocd/tree/master/src/jtag/drivers

## Troubleshooting

### Patch Doesn't Apply
- Verify OpenOCD version is recent
- Try `patch --dry-run` first to debug
- Check for line-ending differences (CRLF vs LF)
- Manually merge if version differs significantly

### Build Fails
```bash
# Check for missing includes
grep -r "#include.*oscan1.h" ~/openocd/src

# Verify files exist
ls -la ~/openocd/src/jtag/drivers/oscan1.{c,h}

# Check compiler errors
make 2>&1 | head -20
```

### Tests Still Fail
- Verify patched OpenOCD is installed: `which openocd`
- Check version includes cJTAG: `openocd --version`
- Enable debug: `openocd -d3 -f openocd/cjtag.cfg`
- Check VPI server is running: `lsof -i :3333`
- Review [openocd/patched/README.md](../openocd/patched/README.md) Troubleshooting section

### Reverting Patches
See [openocd/patched/README.md](../openocd/patched/README.md#reverting-patches) for revert instructions.

## Getting Started

For complete patch application instructions, see [openocd/patched/MANUAL_APPLICATION_GUIDE.md](../openocd/patched/MANUAL_APPLICATION_GUIDE.md).

Once patches are applied and OpenOCD is built:
- Both JTAG and cJTAG modes will work
- Standard JTAG tests continue to pass (no regression)
- cJTAG protocol tests will validate two-wire operation
- OpenOCD can now be used for IEEE 1149.7 debug on cJTAG devices

For detailed reference on what the patches do, see [openocd/patched/README.md](../openocd/patched/README.md).

## Questions?

Review the hardware implementation in `src/jtag/oscan1_controller.sv` to see how the receiving side works. This is your reference for what the OpenOCD patch needs to generate.

Good luck with the OpenOCD patch! The test suite is ready to validate your work.
