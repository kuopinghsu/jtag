# OpenOCD cJTAG/OScan1 Patch Guide

This document describes what needs to be implemented in OpenOCD to support IEEE 1149.7 cJTAG protocol and make the test suite pass.

## Current Status

### ✓ Hardware Ready
- **OScan1 Controller**: Fully implemented in `src/jtag/oscan1_controller.sv`
- **Features**: OAC detection, JScan parser, SF0 decoder, zero deletion, CRC-8
- **Interface**: Two-wire TCKC/TMSC with proper bidirectional support
- **Validation**: Hardware simulation tested with VPI server

### ✗ Software Missing
- **OpenOCD**: Standard jtag_vpi adapter uses 4-wire JTAG only
- **Protocol**: No OScan1/two-wire support in current OpenOCD
- **VPI**: Current VPI adapter doesn't translate JTAG ↔ cJTAG

## Test Results

Run tests to see current status:
```bash
make test-jtag   # ✓ PASSES - Standard JTAG works
make test-cjtag  # ✗ FAILS - cJTAG protocol not supported
```

## Required OpenOCD Modifications

### 1. Add cJTAG Transport Support

**Location**: `src/jtag/core.c`, `src/jtag/transport.c`

**Required**:
- Add `transport select cjtag` command
- Register cJTAG as a valid transport alongside JTAG/SWD
- Handle transport-specific initialization

**Reference**: See how SWD transport is implemented

### 2. Implement OScan1 Protocol Layer

**New file**: `src/jtag/drivers/oscan1.c`

**Features needed**:
```c
// OScan1 Attention Character (OAC)
void oscan1_send_oac(void);  // Send 16 consecutive TCKC edges

// JScan command generation
void oscan1_send_jscan_cmd(uint8_t cmd);
// Commands: OSCAN_ON, OSCAN_OFF, SELECT, DESELECT, SF_SELECT, etc.

// Zero insertion (bit stuffing)
// After 5 consecutive 1s, insert a 0
void oscan1_encode_with_stuffing(uint8_t *data, size_t len);

// Scanning Format 0 (SF0) encoder
// Encode TMS/TDI onto two-wire TMSC
void oscan1_sf0_encode(uint8_t tms, uint8_t tdi);

// CRC-8 calculation (optional)
uint8_t oscan1_calc_crc8(uint8_t *data, size_t len);
```

### 3. Extend jtag_vpi Adapter

**Location**: `src/jtag/drivers/jtag_vpi.c`

**Current**: Only sends 4-wire JTAG signals (TCK/TMS/TDI/TDO)

**Required additions**:
```c
// Mode selection
static int jtag_vpi_cjtag_mode = 0;

// Two-wire protocol encoding
static int jtag_vpi_send_tckc_tmsc(uint8_t tckc, uint8_t tmsc);

// OScan1 initialization sequence
static int jtag_vpi_oscan1_init(void) {
    // 1. Send OAC (16 TCKC edges)
    oscan1_send_oac();
    
    // 2. Send JSCAN_OSCAN_ON
    oscan1_send_jscan_cmd(0x1);
    
    // 3. Send JSCAN_SELECT (select device)
    oscan1_send_jscan_cmd(0x2);
    
    // 4. Select Scanning Format 0
    oscan1_send_jscan_cmd(0x4);
    
    return ERROR_OK;
}

// Convert JTAG operations to SF0
static int jtag_vpi_jtag_to_sf0(uint8_t tms, uint8_t tdi, uint8_t *tdo) {
    // SF0: TMS on TCKC rising edge, TDI on falling edge
    // Send on two-wire TMSC
    oscan1_sf0_encode(tms, tdi);
    *tdo = oscan1_sf0_receive_tdo();
    return ERROR_OK;
}
```

### 4. Add Configuration Commands

**Location**: `src/jtag/drivers/jtag_vpi.c`

**New TCL commands**:
```tcl
# Enable cJTAG mode
jtag_vpi enable_cjtag

# Select scanning format (0, 1, 2, 3)
jtag_vpi scanning_format 0

# Enable CRC-8 checking
jtag_vpi enable_crc

# Enable parity checking
jtag_vpi enable_parity
```

### 5. VPI Protocol Extension

**Option A**: Extend existing VPI protocol
- Add command 0x10: Enable cJTAG mode
- Add command 0x11: Send OScan1 sequence
- Add command 0x12: SF0 encoded scan

**Option B**: Create new cjtag_vpi adapter
- Separate adapter specifically for cJTAG
- Independent from standard jtag_vpi
- Cleaner separation of concerns

## Implementation Checklist

### Phase 1: Basic Two-Wire Support
- [ ] Add cJTAG transport to OpenOCD
- [ ] Implement OAC sequence generation
- [ ] Implement JScan command encoding
- [ ] Test: Can enter OScan1 mode

### Phase 2: Scanning Format 0
- [ ] Implement SF0 TMS/TDI encoding
- [ ] Implement SF0 TDO reception
- [ ] Handle bidirectional TMSC
- [ ] Test: Can perform TAP reset via SF0

### Phase 3: Advanced Features
- [ ] Implement zero insertion/deletion
- [ ] Add CRC-8 calculation
- [ ] Add parity checking
- [ ] Test: Data integrity over two-wire

### Phase 4: Full Protocol
- [ ] Support all JScan commands
- [ ] Implement multiple scanning formats
- [ ] Add device selection/deselection
- [ ] Test: Complete cJTAG operation

## Testing Strategy

### Unit Tests (per phase)
```bash
# After Phase 1
./openocd/test_cjtag_protocol  # Should pass tests 1-3

# After Phase 2
./openocd/test_cjtag_protocol  # Should pass tests 1-5

# After Phase 3
./openocd/test_cjtag_protocol  # Should pass tests 1-6

# After Phase 4 (Complete)
make test-cjtag                # Should pass ALL tests (8/8)
```

### Integration Tests
```bash
# Test against actual hardware
make test-jtag   # Should still pass (regression test)
make test-cjtag  # Should pass with full protocol support

# Test with OpenOCD directly
openocd -f openocd/cjtag.cfg
# Should successfully:
# 1. Connect via two-wire
# 2. Initialize OScan1
# 3. Read IDCODE
# 4. Access JTAG TAP states
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
The VPI server already supports both modes:
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

### Standards
- **IEEE 1149.7-2009**: Standard for Reduced-Pin and Enhanced-Functionality Test Access Port and Boundary-Scan Architecture
- **Section 5**: OScan1 Protocol Definition
- **Section 6**: Scanning Formats (SF0, SF1, SF2, SF3)
- **Appendix B**: JScan Command Set

### Project Documentation
- `docs/OSCAN1_IMPLEMENTATION.md`: Hardware implementation details
- `docs/CJTAG_CRC_PARITY.md`: Error detection mechanisms
- `src/jtag/oscan1_controller.sv`: Reference hardware implementation
- `openocd/test_cjtag_protocol.c`: Validation test suite

### OpenOCD Resources
- OpenOCD source: https://github.com/openocd-org/openocd
- Developer guide: https://openocd.org/doc/doxygen/html/
- Transport implementation: `src/jtag/transport.c`
- Driver examples: `src/jtag/drivers/`

## Development Tips

### Building OpenOCD
```bash
git clone https://github.com/openocd-org/openocd.git
cd openocd
./bootstrap
./configure --enable-jtag_vpi
make
sudo make install
```

### Testing Changes
```bash
# 1. Rebuild OpenOCD with your changes
make clean && make

# 2. Test against simulation
cd /path/to/jtag/project
make vpi-sim --cjtag &   # Start cJTAG simulation
openocd -f openocd/cjtag.cfg  # Connect with patched OpenOCD

# 3. Run validation tests
make test-cjtag
```

### Debug Logging
Enable verbose logging in OpenOCD:
```bash
openocd -d3 -f openocd/cjtag.cfg
```

Enable VPI trace in simulation:
```bash
make vpi-sim --cjtag --verbose
```

## Expected Timeline

- **Phase 1** (Basic): 2-3 days
- **Phase 2** (SF0): 3-5 days
- **Phase 3** (Advanced): 2-3 days
- **Phase 4** (Complete): 2-3 days
- **Testing & Debug**: 3-5 days

**Total**: ~2-3 weeks for full implementation

## Getting Started

1. **Study the hardware**: Read `docs/OSCAN1_IMPLEMENTATION.md`
2. **Understand the protocol**: Review IEEE 1149.7 Section 5-6
3. **Examine tests**: Look at `openocd/test_cjtag_protocol.c`
4. **Start simple**: Implement Phase 1 (OAC sequence)
5. **Test incrementally**: Run tests after each phase
6. **Validate**: Ensure all 8 tests pass

## Questions?

Review the hardware implementation in `src/jtag/oscan1_controller.sv` to see how the receiving side works. This is your reference for what the OpenOCD patch needs to generate.

Good luck with the OpenOCD patch! The test suite is ready to validate your work.
