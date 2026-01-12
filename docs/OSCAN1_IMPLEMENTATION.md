# OScan1 Full Implementation

This document describes the complete IEEE 1149.7 OScan1 protocol implementation added to the JTAG/cJTAG project.

## New Module: oscan1_controller.sv

Location: `src/jtag/oscan1_controller.sv`

### Features Implemented

#### 1. **OAC (OScan1 Attention Character) Detection**
- Detects 16 consecutive edges on TCKC
- Triggers entry into JScan command mode
- Provides escape mechanism for protocol control

#### 2. **JScan Packet Parser**
- Parses 4-bit JScan0 commands
- Supported commands:
  - `JSCAN_OSCAN_OFF` (0x0) - Disable OScan1
  - `JSCAN_OSCAN_ON` (0x1) - Enable OScan1
  - `JSCAN_SELECT` (0x2) - Select device
  - `JSCAN_DESELECT` (0x3) - Deselect device
  - `JSCAN_SF_SELECT` (0x4) - Select scanning format
  - `JSCAN_READ_ID` (0x5) - Read device ID
  - `JSCAN_NOOP` (0xF) - No operation

#### 3. **Zero Insertion/Deletion (Bit Stuffing)**
- Automatic detection of 5 consecutive ones
- Zero deletion to prevent false OAC detection
- Maintains protocol transparency

#### 4. **Scanning Format 0 (SF0) Decoder**
- Decodes TMS bit on TCKC rising edge
- Decodes TDI bit on TCKC falling edge
- Generates internal JTAG TCK pulses
- Handles TDO return path on TMSC

#### 5. **CRC-8 Error Detection** (Optional)
- CRC-8 calculation with polynomial x^8 + x^2 + x + 1 (0x07)
- Configurable via `ENABLE_CRC` parameter
- Byte-by-byte processing during packet reception
- Error statistics tracking (16-bit counter)
- Automatic error detection at packet boundaries

#### 6. **Parity Checking** (Optional)
- Even/odd parity calculation
- Configurable via `ENABLE_PARITY` parameter
- Accumulates parity over entire packet
- Independent error counter (16-bit)
- Can be enabled alongside CRC for dual protection

#### 7. **State Machine**
States:
- `IDLE` - Power-on state
- `OAC_DETECT` - Detecting escape sequence
- `JSCAN` - Processing JScan command
- `OSCAN_SF0` - Active OScan1 mode with SF0
- `OSCAN_SF1` - Reserved for SF1 format
- `OSCAN_SF2` - Reserved for SF2 format
- `ERROR` - Protocol error state

#### 8. **TDO Return Path**
- Captures TDO after internal TCK rising edge
- Returns data on TMSC during output phase
- Bidirectional TMSC control with proper timing

## Architecture

```
Physical Pins          OScan1 Controller        TAP Controller
    TCKC  ──────────>  Edge Detect ────>
                       OAC Detector
                       JScan Parser
                       Zero Deletion
                       SF0 Decoder  ────> jtag_tck ──> TAP
    TMSC  <────────>   Bit Stream   ────> jtag_tms ──> TAP
            (bidir)    Processor    ────> jtag_tdi ──> TAP
                       TDO Handler  <──── jtag_tdo <── TAP
                           │
                           v
                    CRC/Parity Checker
                       (Optional)
                           │
                           v
                     Error Statistics
```

### Error Detection Module

The `cjtag_crc_parity` module is instantiated within `oscan1_controller` when error detection is enabled:

```systemverilog
cjtag_crc_parity #(
    .ENABLE_CRC(ENABLE_CRC),
    .ENABLE_PARITY(ENABLE_PARITY),
    .CRC_POLYNOMIAL(8'h07)
) crc_parity_check (
    .clk(clk),
    .rst_n(rst_n),
    .data_byte(data_byte),
    .data_valid(data_valid),
    .data_last(data_last),
    .crc_value(crc_value),
    .parity_bit(parity_bit),
    .expected_crc(expected_crc),
    .expected_parity(expected_parity),
    .crc_error(crc_error),
    .parity_error(parity_error),
    .crc_error_count(crc_error_count),
    .parity_error_count(parity_error_count),
    .clear_errors(1'b0)
);
```

## Integration

### Modified Files

**src/jtag/jtag_interface.sv**
- Removed simplified placeholder implementation
- Instantiates `oscan1_controller` module
- Routes signals based on mode selection
- Provides clean multiplexing between JTAG and cJTAG

### Interface Signals

```systemverilog
oscan1_controller #(
    .ENABLE_CRC(1'b0),      // Enable CRC-8 error detection
    .ENABLE_PARITY(1'b0)    // Enable parity checking
) oscan1_ctrl (
    .clk            (clk),
    .rst_n          (rst_n),
    .tckc           (tco),             // Physical TCKC input
    .tmsc_in        (tmsc_in),         // Physical TMSC input
    .tmsc_out       (oscan1_tmsc_out), // TMSC output data
    .tmsc_oen       (oscan1_tmsc_oen), // TMSC output enable
    .jtag_tck       (oscan1_tck),      // Decoded TCK
    .jtag_tms       (oscan1_tms),      // Decoded TMS
    .jtag_tdi       (oscan1_tdi),      // Decoded TDI
    .jtag_tdo       (jtag_tdo),        // TDO to return
    .oscan_active   (oscan1_active),   // Protocol active
    .error          (oscan1_error),    // Error status
    .crc_error_count(crc_errors),      // CRC error counter
    .parity_error_count(parity_errors) // Parity error counter
);
```

## Usage Example

### Entering OScan1 Mode

1. **Send OAC**: Generate 16 consecutive edges on TCKC
2. **Send JScan**: Transmit 4-bit `JSCAN_OSCAN_ON` (0x1) command
3. **Data Transfer**: Begin SF0 scanning format
   - Rising edge: TMS bit
   - Falling edge: TDI bit
4. **TDO Return**: Captured on TMSC output phase

### Exiting OScan1 Mode

1. **Send OAC**: Generate 16 edges to re-enter JScan
2. **Send JScan**: Transmit `JSCAN_OSCAN_OFF` (0x0)
3. **Return to IDLE**: Controller returns to idle state

## Protocol Timing

```
TCKC:  ___/‾‾‾\___/‾‾‾\___/‾‾‾\___/‾‾‾\___
TMSC:  ___TMS___TDI___TMS___TDI___
       (in)  (in)  (in)  (in)

       TCK generation (internal):
       _____/‾‾‾\________/‾‾‾\________
             ^sample       ^sample
```

## Future Enhancements

### SF1/SF2 Scanning Formats
- **SF1**: 2-bit packed encoding for higher efficiency
- **SF2**: 4-bit nibble encoding for maximum throughput
- Requires additional packet structure handling

### Advanced Protocol Features
- Multi-drop device selection
- Device addressing
- Star topology support
- Extended JScan commands (JScan1-JScan7)

### Error Detection (✅ Implemented)
- ✅ CRC-8 packet integrity checking (configurable)
- ✅ Parity error detection (configurable)
- ✅ Error statistics tracking
- [ ] Error recovery mechanisms
- [ ] Automatic retransmission requests

## Configuration Options

### Enable CRC-8 Only
```systemverilog
oscan1_controller #(
    .ENABLE_CRC(1'b1),
    .ENABLE_PARITY(1'b0)
) ctrl (...);
```

### Enable Parity Only
```systemverilog
oscan1_controller #(
    .ENABLE_CRC(1'b0),
    .ENABLE_PARITY(1'b1)
) ctrl (...);
```

### Enable Both (Maximum Protection)
```systemverilog
oscan1_controller #(
    .ENABLE_CRC(1'b1),
    .ENABLE_PARITY(1'b1)
) ctrl (...);
```

### Disable Error Detection (Default)
```systemverilog
oscan1_controller #(
    .ENABLE_CRC(1'b0),
    .ENABLE_PARITY(1'b0)
) ctrl (...);
```

## Testing

The OScan1 controller has been integrated and tested:
- ✅ Compiles without errors
- ✅ System integration testbench passes
- ✅ JTAG mode still functional
- ✅ Mode switching works correctly
- ✅ CRC-8 error detection (optional, configurable)
- ✅ Parity checking (optional, configurable)
- ✅ Error statistics tracking

## References

- IEEE 1149.7-2009: Standard for Reduced-Pin and Enhanced-Functionality Test Access Port and Boundary-Scan Architecture
- Section 5: OScan1 Operation
- Section 6: Scanning Formats
- Section 6.4: Error Detection Mechanisms
- Appendix A: Protocol Examples
- [CJTAG_CRC_PARITY.md](CJTAG_CRC_PARITY.md) - Detailed CRC/parity documentation
