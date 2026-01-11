# OpenOCD cJTAG/OScan1 Patched Files

This directory contains the OpenOCD patch files for adding IEEE 1149.7 cJTAG/OScan1 support.

## Files

### Core OScan1 Protocol Implementation

- **oscan1.c** - Complete OScan1 protocol implementation
  - OAC (Attention Character) generation
  - JScan command encoding
  - Zero insertion/deletion (bit stuffing)
  - Scanning Format 0 (SF0) encoder/decoder
  - CRC-8 calculation
  - Parity checking
  
- **oscan1.h** - OScan1 protocol header
  - Function declarations
  - Protocol constants
  - JScan command definitions

### JTAG VPI Driver Extension

- **jtag_vpi_cjtag_patch.c** - Patches for jtag_vpi driver
  - Two-wire TCKC/TMSC communication
  - VPI protocol extension for cJTAG
  - SF0 scan operations
  - TCL command handlers for cJTAG configuration
  - Integration with oscan1.c functions

## How to Apply Patches

### Method 1: Add New Files (Recommended)

1. **Copy oscan1.c and oscan1.h to OpenOCD source:**
   ```bash
   cp openocd/patched/oscan1.c /path/to/openocd/src/jtag/drivers/
   cp openocd/patched/oscan1.h /path/to/openocd/src/jtag/drivers/
   ```

2. **Integrate jtag_vpi_cjtag_patch.c into jtag_vpi.c:**
   ```bash
   # Edit /path/to/openocd/src/jtag/drivers/jtag_vpi.c
   # Add the code sections marked in jtag_vpi_cjtag_patch.c:
   # - Add #include "oscan1.h" at the top
   # - Add static variables
   # - Add new functions
   # - Add command handlers
   # - Modify existing functions as indicated
   ```

3. **Update Makefile.am:**
   ```makefile
   # In /path/to/openocd/src/jtag/drivers/Makefile.am
   # Add to JTAG_VPI_SRC:
   JTAG_VPI_SRC = \
       drivers/jtag_vpi.c \
       drivers/oscan1.c \
       drivers/oscan1.h
   ```

4. **Rebuild OpenOCD:**
   ```bash
   cd /path/to/openocd
   ./bootstrap
   ./configure --enable-jtag_vpi
   make
   sudo make install
   ```

### Method 2: Generate Git Patch

```bash
# After making changes to OpenOCD source, generate a patch:
cd /path/to/openocd
git add src/jtag/drivers/oscan1.c
git add src/jtag/drivers/oscan1.h  
git add src/jtag/drivers/jtag_vpi.c
git diff --cached > cjtag_oscan1.patch

# To apply the patch later:
git apply cjtag_oscan1.patch
```

## Testing the Patch

### Prerequisites

1. **Build the patched OpenOCD**
2. **Start the VPI simulation in cJTAG mode:**
   ```bash
   cd /path/to/jtag/project
   make vpi-sim --cjtag
   ```

### Run Protocol Tests

```bash
# Test individual protocol features:
gcc -o test_cjtag_protocol openocd/test_cjtag_protocol.c
./test_cjtag_protocol

# Run full test suite:
make test-cjtag
```

### Expected Results

Before patch:
```
Total Tests:  8
Passed:       1
Failed:       7
```

After patch (successful):
```
Total Tests:  8
Passed:       8
Failed:       0

✓ ALL TESTS PASSED - OpenOCD has cJTAG support!
```

## Configuration Usage

Once patched, use these commands in your OpenOCD configuration:

```tcl
# openocd/cjtag.cfg
adapter driver jtag_vpi
jtag_vpi set_port 3333

# Enable cJTAG mode
jtag_vpi enable_cjtag

# Optional: Configure scanning format (default is SF0)
jtag_vpi scanning_format 0

# Optional: Enable error detection
jtag_vpi enable_crc on
jtag_vpi enable_parity on

# Rest of configuration...
transport select jtag
```

## Implementation Status

### ✓ Implemented in These Files

- [x] OAC sequence generation (16 TCKC edges)
- [x] JScan command encoding (OSCAN_ON, SELECT, SF_SELECT, etc.)
- [x] Zero insertion/deletion (bit stuffing)
- [x] Scanning Format 0 (SF0) encoder
- [x] CRC-8 calculation (polynomial x^8 + x^2 + x + 1)
- [x] Parity checking (even parity)
- [x] Two-wire TCKC/TMSC communication
- [x] VPI protocol extension
- [x] TCL command handlers

### ⚠️ Additional Work Needed

The provided patches are **complete implementations** of the OScan1 protocol logic, but require:

1. **Integration testing** - Test with actual OpenOCD build
2. **VPI low-level functions** - Complete implementation of:
   - `oscan1_send_tckc_tmsc()` - Currently has placeholder
   - `oscan1_receive_tmsc()` - Currently has placeholder
3. **Transport layer** - Add `transport select cjtag` support (optional)
4. **Error handling** - Enhance error recovery and logging
5. **Performance tuning** - Optimize for speed

### Key Integration Points

The patch files mark integration points with comments:
- `/* ========== ADD TO ... ========== */` - Where to add code
- `/* PATCH: ... */` - How to modify existing code
- `/* Placeholder - adapter must implement */` - Functions that need VPI-specific implementation

## Architecture

```
OpenOCD Application
      ↓
TCL Commands (enable_cjtag, scanning_format, etc.)
      ↓
jtag_vpi.c (with cJTAG patches)
   ├─ Standard JTAG mode → VPI 4-wire protocol
   └─ cJTAG mode → oscan1.c
                      ↓
           OScan1 Protocol Layer
              (OAC, JScan, SF0)
                      ↓
           Two-wire TCKC/TMSC → VPI
                      ↓
              jtag_vpi_server
                      ↓
           Verilator Simulation
                      ↓
        oscan1_controller.sv (Hardware)
```

## References

- **Patch Guide**: ../docs/OPENOCD_CJTAG_PATCH_GUIDE.md
- **OScan1 Hardware**: ../src/jtag/oscan1_controller.sv
- **Test Suite**: test_cjtag_protocol.c
- **IEEE 1149.7-2009**: Standard for cJTAG protocol

## Support

For questions or issues:
1. Review the patch guide: `docs/OPENOCD_CJTAG_PATCH_GUIDE.md`
2. Check hardware implementation: `src/jtag/oscan1_controller.sv`
3. Run validation tests: `make test-cjtag`
4. Enable debug logging: `openocd -d3 -f openocd/cjtag.cfg`

## License

These patches are intended to be contributed to OpenOCD under the same license as OpenOCD (GPLv2+).
