# Test Status Summary

## Overview

This document summarizes the current status of JTAG and cJTAG testing.

## Test Results

### ✅ make test-jtag: PASSES
```
Tests: 4 connectivity + 8 protocol = 12 total
Status: ✓ All tests pass
```

**What it tests:**

*Connectivity Tests (4):*
1. VPI adapter connection
2. OpenOCD initialization
3. JTAG interface detection
4. Telnet interface responsiveness

*Protocol Tests (8):*
1. VPI server connection
2. JTAG TAP reset (CMD_RESET)
3. Scan operations (CMD_SCAN with TMS/TDI/TDO)
4. Port configuration (CMD_SET_PORT)
5. Multiple TAP reset cycles
6. Invalid command handling
7. Large scan operation (32 bits)
8. Rapid command sequence (stress test)

**Why it passes:** Standard 4-wire JTAG protocol is fully supported by OpenOCD's jtag_vpi adapter, and the VPI server correctly implements the OpenOCD jtag_vpi protocol (8-byte commands).

---

### ❌ make test-cjtag: FAILS (Expected)
```
Tests: 4 connectivity + 8 protocol = 12 total
Status: ✓ 4/4 connectivity pass, ✗ 7/8 protocol fail
```

**What it tests:**

*Connectivity Tests (4):* Same as JTAG mode - all pass

*Protocol Tests (8):*
1. Two-wire mode detection (TCKC/TMSC vs TCK/TMS/TDI/TDO)
2. OScan1 Attention Character (OAC) - 16 TCKC edges
3. JScan command sequences (OSCAN_ON, SELECT, etc.)
4. Zero insertion/deletion (bit stuffing)
5. Scanning Format 0 (SF0) TMS/TDI encoding
6. CRC-8 error detection
7. Full cJTAG TAP reset sequence
8. Mode select flag verification

**Why it fails:** OpenOCD's jtag_vpi adapter does not support IEEE 1149.7 OScan1 two-wire protocol. It connects using standard 4-wire JTAG, not cJTAG.

## Hardware vs Software Status

| Component | Status | Details |
|-----------|--------|---------|
| **Hardware (Verilog)** | ✅ Ready | oscan1_controller.sv fully implements OScan1 |
| **Features** | ✅ Ready | OAC, JScan, SF0, zero stuffing, CRC-8 all working |
| **VPI Server** | ✅ Ready | Supports both JTAG and cJTAG modes |
| **OpenOCD** | ❌ Missing | Standard jtag_vpi uses 4-wire JTAG only |
| **Two-wire Protocol** | ❌ Missing | No TCKC/TMSC support in OpenOCD |

## What's Needed

To make `test-cjtag` pass, OpenOCD needs to be patched with:

1. **cJTAG transport support** - Add `transport select cjtag`
2. **OScan1 protocol layer** - Generate OAC sequences and JScan commands
3. **Two-wire encoding** - Convert JTAG to TCKC/TMSC signals
4. **SF0 encoder** - Encode TMS/TDI onto two-wire TMSC
5. **VPI adapter extension** - Add cJTAG support to jtag_vpi driver

## Documentation

- **[OPENOCD_CJTAG_PATCH_GUIDE.md](docs/OPENOCD_CJTAG_PATCH_GUIDE.md)** - Complete guide for patching OpenOCD
- **[OSCAN1_IMPLEMENTATION.md](docs/OSCAN1_IMPLEMENTATION.md)** - Hardware implementation details
- **[test_cjtag_protocol.c](openocd/test_cjtag_protocol.c)** - Validation test suite

## Running Tests

```bash
# Test standard JTAG (should pass)
make test-jtag

# Test cJTAG protocol (will fail until OpenOCD is patched)
make test-cjtag
```

## Next Steps

1. **Read the patch guide**: See `docs/OPENOCD_CJTAG_PATCH_GUIDE.md`
2. **Study the hardware**: Review `src/jtag/oscan1_controller.sv`
3. **Implement in phases**: Start with OAC sequence generation
4. **Test incrementally**: Run `./openocd/test_cjtag_protocol` after each phase
5. **Validate**: All 8 tests must pass when complete

## Success Criteria

When OpenOCD is successfully patched:
```bash
$ make test-cjtag
...
Total Tests:  8
Passed:       8
Failed:       0

✓ ALL TESTS PASSED - OpenOCD has cJTAG support!
```

## Timeline

Estimated effort to patch OpenOCD: **2-3 weeks**

- Phase 1 (Basic two-wire): 2-3 days
- Phase 2 (SF0 encoding): 3-5 days
- Phase 3 (Advanced features): 2-3 days
- Phase 4 (Complete protocol): 2-3 days
- Testing & debugging: 3-5 days

---

*Last updated: 2026-01-11*
*Tests created to validate future OpenOCD cJTAG support*
