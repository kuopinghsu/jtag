# JTAG VPI Server - Debugging Notes

## Current Status
- ✅ VPI server connects successfully with OpenOCD
- ✅ IDCODE readback works (0x1dead3ff confirmed)
- ✅ Server listens on port 3333
- ❌ IR capture returns 0x00 instead of expected 0x01
- ❌ OpenOCD never progresses past initialization (stuck in CMD_RESET loop)
- ❌ Scan operations are not executed by OpenOCD

## Root Cause Analysis

### OpenOCD Behavior
OpenOCD sends:
- Multiple CMD_RESET (0x00) commands
- Never sends CMD_SCAN (0x02) commands
- This is because OpenOCD initialization fails when IR capture returns 0x00

### IR Capture Issue
When OpenOCD attempts IR capture (reads IR register):
- Expected: 0x01 (capture pattern from 5-bit IR register)
- Actual: 0x00 (all zeros)
- This suggests RTL IR output is not being driven or is being reset immediately

### VPI Server State
- Bit-level logging code was added but is never reached (scan_state never enters SCAN_PROCESSING)
- All commands from OpenOCD are CMD_RESET - this is abnormal
- The expected sequence: Reset → IR capture → IDCODE read → DTM operations
- Actual sequence: Reset only (infinite loop)

## Attempted Fixes

### 1. TDO Enable Gating
**Theory**: TDO output was being gated off after shift operations
**Action**: Removed the gating logic to always capture TDO
**Result**: No improvement - IR still returns 0x00

### 2. TDO Stabilization Delay
**Theory**: TDO signal needs time to propagate/stabilize
**Action**: Added 3-cycle delay before capturing TDO
**Result**: No improvement

### 3. Bit-level Logging
**Theory**: Would reveal bit packing or timing issues
**Action**: Added printf for every captured bit
**Result**: Logging never executes because scan operations never start

## Next Diagnostic Steps

### Option 1: Direct RTL Verification
Create a simple testbench that manually exercises the JTAG TAP:
1. Load known IR pattern
2. Verify IR output appears on TDO
3. Check timing: IR should appear on TDO when IR_SHIFT is asserted

### Option 2: Instrument RTL Signals
Add Verilator probes to capture:
- `ir_out` value during shifts
- `ir_capture_en` (or equivalent shift enable)
- `jtag_pin3_o` (TDO output) directly
- Correlate with VPI server's `current_tdo` value

### Option 3: Simplify Test Case
Instead of full JTAG protocol:
- Send manual TDI/TMS sequences via CMD_SCAN
- Verify each bit returned matches expected pattern
- Trace through which bits are being captured incorrectly

## Files Modified

- `sim/jtag_vpi_server.cpp`: Added bit-level logging and TDO capture diagnostics
- `src/jtag/jtag_instruction_register.sv`: Reduced to 5-bit width
- `src/jtag/jtag_top.sv`: Updated IR width references
- `src/jtag/jtag_dtm.sv`: Updated IR width and opcodes
- `openocd/jtag.cfg`: Set `-irlen 5` for OpenOCD

## Hypothesis

The JTAG IR register capture pattern (2'b01 for 5-bit IR) is not appearing on the TDO output. This could be due to:

1. **Capture logic not firing**: IR_SHIFT state machine may not be advancing to CAPTURE state correctly
2. **IR output not connected**: RTL `ir_out` may not be properly routed to TDO mux
3. **Enable signal issue**: Output enable for IR data may be gated incorrectly
4. **Timing violation**: IR bits may shift before being captured, or captured before being valid

## Recommended Action

Run RTL simulation with internal signal tracing to see the actual state of:
- `jtag_top.ir_out` values during IR shift
- JTAG TAP state during IR operations
- TDO output value (including enable signal state)

This will confirm whether the problem is in RTL logic or in VPI server's TDO capture timing/polarity.
