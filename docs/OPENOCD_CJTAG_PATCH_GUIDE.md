# OpenOCD cJTAG/OScan1 Patch Guide

This document describes what is needed to add IEEE 1149.7 cJTAG support to OpenOCD. Patch files and reference implementations are in [openocd/patched/](../openocd/patched/).

## Quick Start

To apply patches to your OpenOCD installation:

```bash
cd ~/openocd
patch -p1 < /path/to/jtag/openocd/patched/001-jtag_vpi-cjtag-support.patch
cat /path/to/jtag/openocd/patched/002-oscan1-new-file.txt > src/jtag/drivers/oscan1.c
cat /path/to/jtag/openocd/patched/003-oscan1-header-new-file.txt > src/jtag/drivers/oscan1.h
./configure --enable-jtag_vpi && make clean && make -j4 && sudo make install
```

See [openocd/patched/README.md](../openocd/patched/README.md) for detailed application instructions.

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

**What Changed**: VPI server packet parsing was fixed to wait for full 1036-byte OpenOCD VPI packets instead of treating 8-byte headers as complete. This resolved the "IR/DR scans returning zeros" issue. See [../FIX_SUMMARY.md](../FIX_SUMMARY.md) for details.

With optional patches applied (enhanced features):
```bash
make test-jtag   # ✓ PASSES - Standard JTAG still works
make test-cjtag  # ✓ PASSES - cJTAG with enhanced OScan1 commands
```

**Patches provide**: Explicit JScan command generation, SF format selection, more granular control. Basic cJTAG operation works without patches.

## Patch Contents

All patch files are located in [openocd/patched/](../openocd/patched/):

### 001-jtag_vpi-cjtag-support.patch
Main driver modifications (unified diff format):
- Adds `oscan1.h` include
- Adds cJTAG mode state variables
- Adds 4 support functions for two-wire communication
- Adds 4 TCL command handlers
- Integrates OScan1 initialization

### 002-oscan1-new-file.txt
New file: `src/jtag/drivers/oscan1.c` (~300 lines)
- OAC (Attention Character) generation
- JScan command encoding
- Zero insertion/deletion (bit stuffing)
- Scanning Format 0 (SF0) encoder/decoder
- CRC-8 and parity calculation
- Two-wire TCKC/TMSC interface

### 003-oscan1-header-new-file.txt
New file: `src/jtag/drivers/oscan1.h` (~100 lines)
- Function declarations
- Protocol constants
- Data structure definitions

## Detailed Implementation

### jtag_vpi.c Changes
The patch (001-jtag_vpi-cjtag-support.patch) makes the following additions:

**1. Add oscan1.h Include**
```c
#include "oscan1.h"  // OScan1 protocol definitions
```

**2. Add cJTAG State Variables**
```c
static int jtag_vpi_cjtag_mode = 0;           // Mode flag
static bool jtag_vpi_oscan1_initialized = false;  // Init flag
```

**3. Add Support Functions**
```c
// Two-wire communication
static int jtag_vpi_send_tckc_tmsc(uint8_t tckc, uint8_t tmsc);
static uint8_t jtag_vpi_receive_tmsc(void);

// OScan1 initialization
static int jtag_vpi_oscan1_init(void) {
    oscan1_send_oac();                    // Send OAC
    oscan1_send_jscan_cmd(JSCAN_OSCAN_ON); // Enable OScan1
    oscan1_send_jscan_cmd(JSCAN_SELECT);  // Select device
    jtag_vpi_oscan1_initialized = true;
    return ERROR_OK;
}

// SF0 scanning
static int jtag_vpi_sf0_scan(uint8_t tms, uint8_t tdi, uint8_t *tdo) {
    oscan1_sf0_encode(tms, tdi);
    *tdo = oscan1_sf0_receive_tdo();
    return ERROR_OK;
}
```

**4. Add TCL Command Handlers**
```c
// Enable cJTAG mode
COMMAND_HANDLER(jtag_vpi_handle_enable_cjtag_command) { ... }

// Set scanning format
COMMAND_HANDLER(jtag_vpi_handle_scanning_format_command) { ... }

// Enable CRC-8
COMMAND_HANDLER(jtag_vpi_handle_enable_crc_command) { ... }

// Enable parity
COMMAND_HANDLER(jtag_vpi_handle_enable_parity_command) { ... }
```

**5. Register Commands**
```c
// Register in jtag_vpi_commands[] array:
{
    .name = "enable_cjtag",
    .handler = jtag_vpi_handle_enable_cjtag_command,
    ...
}
```

### oscan1.c Implementation
New file with OScan1 protocol layer (~300 lines):
- OAC sequence generation (16 TCKC edges)
- JScan command encoding
- Zero insertion/deletion for bit stuffing
- SF0 TMS/TDI encoding on two-wire TMSC
- CRC-8 calculation
- Parity checking

### oscan1.h Header
New file with public interface (~100 lines):
- Function declarations
- Protocol constants and commands
- Data structure definitions

### Makefile.am
Add oscan1.c to build system:
```makefile
DRIVERFILES += %D%/oscan1.c
```

### Configuration Commands

**TCL Interface** (available in OpenOCD after patching):
```tcl
# Enable cJTAG mode
jtag_vpi enable_cjtag

# Select scanning format (0, 1, 2, 3)
jtag_vpi scanning_format 0

# Enable CRC-8 checking
jtag_vpi enable_crc on|off

# Enable parity checking
jtag_vpi enable_parity on|off
```

## Implementation Status

### ✓ Complete (in patch files)
- [x] OAC sequence generation
- [x] JScan command encoding
- [x] Zero insertion/deletion (bit stuffing)
- [x] Scanning Format 0 (SF0) encoder/decoder
- [x] CRC-8 calculation
- [x] Even parity checking
- [x] Two-wire TCKC/TMSC communication
- [x] VPI integration
- [x] TCL command interface

### Integration Checklist
- [ ] Backup original: `cp src/jtag/drivers/jtag_vpi.c{,.backup}`
- [ ] Apply patch: `patch -p1 < 001-jtag_vpi-cjtag-support.patch`
- [ ] Create oscan1.c: `cat 002-oscan1-new-file.txt > src/jtag/drivers/oscan1.c`
- [ ] Create oscan1.h: `cat 003-oscan1-header-new-file.txt > src/jtag/drivers/oscan1.h`
- [ ] Verify Makefile.am has `oscan1.c` in build
- [ ] Configure: `./configure --enable-jtag_vpi`
- [ ] Build: `make clean && make -j4`
- [ ] Install: `sudo make install`
- [ ] Test JTAG: `make test-jtag`  (should still pass)
- [ ] Test cJTAG: `make test-cjtag` (should now pass)

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

## Building with Patches

### Automatic Application
The quickest way to apply patches:

```bash
cd ~/openocd

# 1. Apply jtag_vpi.c patch
patch -p1 < /path/to/jtag/openocd/patched/001-jtag_vpi-cjtag-support.patch

# 2. Create oscan1.c
cp /path/to/jtag/openocd/patched/002-oscan1-new-file.txt src/jtag/drivers/oscan1.c

# 3. Create oscan1.h
cp /path/to/jtag/openocd/patched/003-oscan1-header-new-file.txt src/jtag/drivers/oscan1.h

# 4. Build
./configure --enable-jtag_vpi
make clean && make -j4
sudo make install
```

### Manual Steps
See [openocd/patched/README.md](../openocd/patched/README.md) for detailed step-by-step instructions.

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

## Getting Started with Patches

1. **Review patch contents**: Open [openocd/patched/README.md](../openocd/patched/README.md)
2. **Back up your OpenOCD**: `cp -r ~/openocd ~/openocd.backup`
3. **Apply patches**: Follow quick start in [openocd/patched/README.md](../openocd/patched/README.md)
4. **Build OpenOCD**: `./configure --enable-jtag_vpi && make clean && make -j4`
5. **Install**: `sudo make install`
6. **Test**: `make test-jtag && make test-cjtag`
7. **Verify**: Both tests should pass with cJTAG protocol support

## Next Steps

With patches applied and OpenOCD built:
- Both JTAG and cJTAG modes will work
- Standard JTAG tests continue to pass (no regression)
- cJTAG protocol tests will validate two-wire operation
- OpenOCD can now be used for IEEE 1149.7 debug on cJTAG devices

For detailed reference on what the patches do, see [openocd/patched/README.md](../openocd/patched/README.md).


## Questions?

Review the hardware implementation in `src/jtag/oscan1_controller.sv` to see how the receiving side works. This is your reference for what the OpenOCD patch needs to generate.

Good luck with the OpenOCD patch! The test suite is ready to validate your work.
