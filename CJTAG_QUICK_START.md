# OpenOCD cJTAG Patch - Quick Start Guide

## ✓ Patch Status: APPLIED

The OpenOCD cJTAG/OScan1 patches have been successfully applied to `~/openocd/src/jtag/drivers/`.

## Quick Build & Test (5 minutes)

### 1. Build Patched OpenOCD
```bash
cd ~/openocd
make distclean
./configure --enable-jtag_vpi
make -j4
sudo make install
```

### 2. Test JTAG (Should Still Pass)
```bash
cd /Users/kuoping/Projects/jtag
make test-jtag
```

### 3. Test cJTAG (New - Will Pass With Patch)
```bash
cd /Users/kuoping/Projects/jtag
make test-cjtag
```

## What Was Patched

### Files Modified
- `~/openocd/src/jtag/drivers/jtag_vpi.c` - Main driver (13 changes)
- `~/openocd/src/jtag/drivers/Makefile.am` - Build system (1 change)

### Files Added
- `~/openocd/src/jtag/drivers/oscan1.c` - OScan1 protocol (300+ lines)
- `~/openocd/src/jtag/drivers/oscan1.h` - Protocol header (100+ lines)

### New Features Added
- **4 Support Functions**: TCKC/TMSC, TDO read, OScan1 init, SF0 scan
- **4 TCL Commands**: enable_cjtag, scanning_format, enable_crc, enable_parity
- **Full Protocol**: OAC, JScan, SF0, CRC-8, parity

## Usage Example

### Configuration File
```tcl
# openocd/cjtag.cfg
adapter driver jtag_vpi
jtag_vpi set_port 3333

# Enable cJTAG mode
jtag_vpi enable_cjtag

# Optional configuration
jtag_vpi scanning_format 0
jtag_vpi enable_crc on
jtag_vpi enable_parity on
```

### Run with Patched OpenOCD
```bash
# Terminal 1: Start VPI simulation
cd /Users/kuoping/Projects/jtag
make vpi-sim

# Terminal 2: Run patched OpenOCD
openocd -f /Users/kuoping/Projects/jtag/openocd/patched/cjtag_patched.cfg
```

## Verification

### Check Patch Applied
```bash
# Verify oscan1.h included
grep "oscan1.h" ~/openocd/src/jtag/drivers/jtag_vpi.c

# Verify new functions present
grep "jtag_vpi_oscan1_init" ~/openocd/src/jtag/drivers/jtag_vpi.c

# Verify commands registered
grep "enable_cjtag" ~/openocd/src/jtag/drivers/jtag_vpi.c
```

### Expected Test Results

**Before Build**: No cJTAG commands available
```
openocd: error: Unknown jtag_vpi subcommand "enable_cjtag"
```

**After Build**: cJTAG commands available
```
jtag_vpi enable_cjtag     # ✓ Works
jtag_vpi scanning_format 0 # ✓ Works
jtag_vpi enable_crc on     # ✓ Works
jtag_vpi enable_parity on  # ✓ Works
```

## Files Reference

| Purpose | Location |
|---------|----------|
| Patch guide | `/Users/kuoping/Projects/jtag/docs/OPENOCD_CJTAG_PATCH_GUIDE.md` |
| Patched files | `/Users/kuoping/Projects/jtag/openocd/patched/` |
| Patch application summary | `/Users/kuoping/Projects/jtag/PATCH_APPLICATION_SUMMARY.md` |
| Automated patch script | `/Users/kuoping/Projects/jtag/apply_cjtag_patch.sh` |
| Test suite | `/Users/kuoping/Projects/jtag/openocd/test_cjtag_protocol.c` |
| Hardware implementation | `/Users/kuoping/Projects/jtag/src/jtag/oscan1_controller.sv` |

## Expected Test Output

### After Successful Build and `make test-cjtag`
```
Total Tests:  8
Passed:       8
Failed:       0

✓ ALL TESTS PASSED - OpenOCD has cJTAG support!
```

## Troubleshooting

### Build Fails
```bash
# Check for errors
cd ~/openocd
make distclean
./configure --enable-jtag_vpi 2>&1 | tee configure.log
make 2>&1 | tee build.log
```

### OpenOCD Won't Start
```bash
# Check for cJTAG command support
openocd --help 2>&1 | grep -A5 "jtag_vpi"

# Enable debug logging
openocd -d3 -f config.cfg
```

### Tests Still Fail
```bash
# Verify VPI is running
lsof -i:3333

# Check if tests can connect
./openocd/test_cjtag_protocol
```

## Architecture Overview

```
OpenOCD (Patched)
    ↓
jtag_vpi.c (with cJTAG support)
    ├─ Standard JTAG → 4-wire protocol
    └─ cJTAG mode → oscan1.c
         ↓
    OScan1 Protocol Layer
    (OAC, JScan, SF0, CRC-8, Parity)
         ↓
    Two-wire TCKC/TMSC
         ↓
    VPI Server
         ↓
    Verilator Simulation
         ↓
    Hardware (oscan1_controller.sv)
```

## Key Implementation Details

### Scanning Format 0 (SF0)
- TMS sent on TCKC rising edge
- TDI sent on TCKC falling edge
- Both over two-wire TMSC line
- TDO read during scan cycle

### OAC Sequence
- 16 consecutive TCKC edges
- TMSC held high
- Enters JScan mode

### JScan Commands
- OSCAN_ON (0x01) - Enable OScan1
- OSCAN_OFF (0x00) - Disable OScan1
- SELECT (0x02) - Select device
- JSCAN_SF_SELECT (0x04) - Select SF0 format
- JSCAN_RESET (0x0F) - Reset TAP

## Next Steps

1. **Build**: `cd ~/openocd && make -j4`
2. **Install**: `sudo make install`
3. **Test**: `cd /Users/kuoping/Projects/jtag && make test-cjtag`
4. **Validate**: All 8 tests should pass
5. **Deploy**: Use patched OpenOCD with VPI for cJTAG debugging

## Support

For detailed information:
- Patch details: `PATCH_APPLICATION_SUMMARY.md`
- Protocol guide: `docs/OPENOCD_CJTAG_PATCH_GUIDE.md`
- Patch source: `openocd/patched/`
- Test source: `openocd/test_cjtag_protocol.c`

---

**Ready to build?** `cd ~/openocd && make -j4 && sudo make install`
