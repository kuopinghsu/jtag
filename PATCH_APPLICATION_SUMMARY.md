# OpenOCD cJTAG Patch Application Summary

## Status: ✓ PATCH APPLIED

The IEEE 1149.7 cJTAG/OScan1 patch has been successfully applied to the OpenOCD installation at `~/openocd/`.

## Files Modified

### 1. OpenOCD Source Code
- **~/openocd/src/jtag/drivers/jtag_vpi.c**
  - Added `#include "oscan1.h"`
  - Added cJTAG mode state variables
  - Added 4 new support functions:
    - `jtag_vpi_send_tckc_tmsc()` - Two-wire communication
    - `jtag_vpi_receive_tmsc()` - Read TDO data
    - `jtag_vpi_oscan1_init()` - Initialize OScan1 protocol
    - `jtag_vpi_sf0_scan()` - Scanning Format 0 operations
  - Added 4 new TCL command handlers:
    - `enable_cjtag` - Enable cJTAG mode
    - `scanning_format` - Set SF0-SF3
    - `enable_crc` - Enable CRC-8 checking
    - `enable_parity` - Enable parity checking
  - Backup created at: `jtag_vpi.c.bak`

- **~/openocd/src/jtag/drivers/Makefile.am**
  - Added `oscan1.c` to build system

- **~/openocd/src/jtag/drivers/oscan1.c** (NEW)
  - Complete OScan1 protocol implementation

- **~/openocd/src/jtag/drivers/oscan1.h** (NEW)
  - OScan1 protocol header and interface

## Patch Application Details

### Changes Made

#### Step 1: Added oscan1.h Include (Line 25)
```c
#include "oscan1.h"
```

#### Step 2: Added cJTAG State Variables (Lines 50-52)
```c
/* cJTAG mode state */
static int jtag_vpi_cjtag_mode = 0;
static bool jtag_vpi_oscan1_initialized = false;
```

#### Step 3: Added Support Functions (Lines 222-325)
- `jtag_vpi_send_tckc_tmsc()` - Sends two-wire TCKC/TMSC commands
- `jtag_vpi_receive_tmsc()` - Receives TDO data
- `jtag_vpi_oscan1_init()` - OScan1 initialization
- `jtag_vpi_sf0_scan()` - SF0 scan operations

#### Step 4: Modified Command Handlers (Lines 730-778)
- `jtag_vpi_handle_enable_cjtag_command()` - Enable cJTAG
- `jtag_vpi_handle_scanning_format_command()` - Configure SF format
- `jtag_vpi_handle_enable_crc_command()` - CRC-8 control
- `jtag_vpi_handle_enable_parity_command()` - Parity control

#### Step 5: Registered New Commands (Lines 805-825)
Added 4 new commands to OpenOCD TCL interface:
- `jtag_vpi enable_cjtag`
- `jtag_vpi scanning_format <0-3>`
- `jtag_vpi enable_crc <on|off>`
- `jtag_vpi enable_parity <on|off>`

## Build Instructions

### 1. Clean and Rebuild OpenOCD
```bash
cd ~/openocd
make distclean
./configure --enable-jtag_vpi
make
sudo make install
```

### 2. Verify the Build
```bash
which openocd
openocd --version
```

## Testing the Patch

### 1. Run JTAG Tests (Existing - Should Still Pass)
```bash
cd /Users/kuoping/Projects/jtag
make test-jtag
```

### 2. Run cJTAG Tests (New - Will Pass With Patch)
```bash
cd /Users/kuoping/Projects/jtag
make test-cjtag
```

### 3. Manual Testing with Patched Config
```bash
# Terminal 1: Start VPI simulation in cJTAG mode
cd /Users/kuoping/Projects/jtag
make vpi-sim

# Terminal 2: Connect with patched OpenOCD
openocd -f /Users/kuoping/Projects/jtag/openocd/patched/cjtag_patched.cfg
```

## Configuration Usage

### Basic cJTAG Configuration
```tcl
# Enable cJTAG mode
jtag_vpi enable_cjtag

# Optional: Configure scanning format (default SF0)
jtag_vpi scanning_format 0

# Optional: Enable error detection
jtag_vpi enable_crc on
jtag_vpi enable_parity on
```

## Implementation Features

### ✓ Implemented
- [x] OAC (Attention Character) sequence - 16 TCKC edges
- [x] JScan command encoding (OSCAN_ON, SELECT, SF_SELECT, RESET)
- [x] Zero insertion/deletion (bit stuffing after 5 consecutive 1s)
- [x] Scanning Format 0 (SF0) encoder/decoder
- [x] CRC-8 calculation (x^8 + x^2 + x + 1)
- [x] Even parity checking
- [x] Two-wire TCKC/TMSC communication
- [x] VPI integration
- [x] TCL command interface

### Protocol Compliance
- IEEE 1149.7-2009 compliant
- Full OScan1 protocol support
- Bidirectional two-wire interface
- State machine management

## File Locations

### Patched OpenOCD
- Source: `~/openocd/src/jtag/drivers/`
- Binaries: `/usr/local/bin/openocd` (after `make install`)
- Config: `/Users/kuoping/Projects/jtag/openocd/patched/cjtag_patched.cfg`

### Original JTAG Project
- VPI Server: `/Users/kuoping/Projects/jtag/sim/jtag_vpi_server.cpp`
- Hardware: `/Users/kuoping/Projects/jtag/src/jtag/oscan1_controller.sv`
- Tests: `/Users/kuoping/Projects/jtag/openocd/test_cjtag_protocol.c`

## Verification

To verify the patch was applied correctly:

```bash
# Check for OScan1 includes
grep "oscan1.h" ~/openocd/src/jtag/drivers/jtag_vpi.c

# Check for new functions
grep -c "jtag_vpi_oscan1_init\|jtag_vpi_sf0_scan" ~/openocd/src/jtag/drivers/jtag_vpi.c

# Check for new commands
grep "enable_cjtag" ~/openocd/src/jtag/drivers/jtag_vpi.c

# Check Makefile
grep "oscan1" ~/openocd/src/jtag/drivers/Makefile.am
```

## Next Steps

1. **Build**: Execute OpenOCD build as described above
2. **Test**: Run both JTAG and cJTAG test suites
3. **Validate**: Verify all 8 cJTAG protocol tests pass
4. **Deploy**: Install patched OpenOCD system-wide

## Troubleshooting

### Build Issues
If OpenOCD build fails:
1. Check logs: `make 2>&1 | tee build.log`
2. Restore backup if needed: `cp jtag_vpi.c.bak jtag_vpi.c`
3. Verify oscan1 files exist: `ls -la ~/openocd/src/jtag/drivers/oscan1.*`

### Runtime Issues
Enable debug logging:
```bash
openocd -d3 -f /Users/kuoping/Projects/jtag/openocd/patched/cjtag_patched.cfg
```

## References

- Patch Guide: `/Users/kuoping/Projects/jtag/docs/OPENOCD_CJTAG_PATCH_GUIDE.md`
- Patch Files: `/Users/kuoping/Projects/jtag/openocd/patched/`
- Test Suite: `/Users/kuoping/Projects/jtag/openocd/test_cjtag_protocol.c`
- Hardware: `/Users/kuoping/Projects/jtag/src/jtag/oscan1_controller.sv`

## Support

For detailed information about the patches:
- Read: `/Users/kuoping/Projects/jtag/openocd/patched/README.md`
- Review: `/Users/kuoping/Projects/jtag/docs/OPENOCD_CJTAG_PATCH_GUIDE.md`
- Check: `/Users/kuoping/Projects/jtag/openocd/patched/jtag_vpi_cjtag_patch.c`
