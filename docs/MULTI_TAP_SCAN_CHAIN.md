# Multi-TAP Scan Chain Support

Implementation of multi-TAP JTAG scan chain support for daisy-chained TAP controllers.

## Overview

The multi-TAP scan chain controller enables multiple JTAG TAP controllers to be connected in series, allowing a single JTAG interface to control multiple devices or cores. This is essential for system-on-chip (SoC) designs with multiple debug-capable components.

## Features

- ✅ **Up to 8 TAPs in chain** - Configurable number of TAPs
- ✅ **Automatic bypass management** - Transparent bypass for non-selected TAPs
- ✅ **IR length configuration** - Per-TAP instruction register length
- ✅ **TAP selection** - Dynamic selection of active TAP
- ✅ **Chain length calculation** - Automatic total IR/DR length tracking
- ✅ **Standard compliant** - IEEE 1149.1 compatible

## Architecture

### Scan Chain Topology

```
        ┌─────────────────────────────────────────────────┐
        │      jtag_scan_chain Controller                 │
        └─────────────────────────────────────────────────┘
                │                                │
        TDI ────┼────┬────┬────┬────┬────────────┼──── TDO
                     │    │    │    │
                   TAP0  TAP1  TAP2  TAP3
                   (IR=8)(IR=8)(IR=6)(IR=10)
```

### Signal Flow

```
Upstream JTAG → Scan Chain Controller → TAP0 → TAP1 → TAP2 → TAP3 → TDO
                       ↓
                 Bypass Management
                       ↓
                 TAP Selection
```

## Module Interface

### Parameters

```systemverilog
parameter int NUM_TAPS = 1;                    // Number of TAPs (1-8)
parameter int IR_LENGTHS [NUM_TAPS] = '{8};    // IR length per TAP
parameter int MAX_IR_LENGTH = 8;               // Maximum IR length
```

### Ports

```systemverilog
module jtag_scan_chain #(
    parameter int NUM_TAPS = 1,
    parameter int IR_LENGTHS [NUM_TAPS] = '{8},
    parameter int MAX_IR_LENGTH = 8
)(
    input  logic        clk,
    input  logic        rst_n,

    // Upstream JTAG (from jtag_interface)
    input  logic        tap_tck,
    input  logic        tap_tms,
    input  logic        tap_tdi,
    output logic        tap_tdo,

    // TAP control signals
    input  logic        shift_dr,
    input  logic        shift_ir,
    input  logic        capture_dr,
    input  logic        capture_ir,
    input  logic        update_dr,
    input  logic        update_ir,

    // Downstream TAP interfaces (to individual TAPs)
    output logic [NUM_TAPS-1:0] tap_tck_out,
    output logic [NUM_TAPS-1:0] tap_tms_out,
    output logic [NUM_TAPS-1:0] tap_tdi_out,
    input  logic [NUM_TAPS-1:0] tap_tdo_in,

    // TAP selection
    input  logic [$clog2(NUM_TAPS)-1:0] selected_tap,
    output logic [NUM_TAPS-1:0]         tap_active,

    // Chain status
    output logic [15:0] total_ir_length,
    output logic [15:0] total_dr_length
);
```

## Usage

### Basic Configuration (2 TAPs)

```systemverilog
// Instantiate scan chain with 2 TAPs
jtag_scan_chain #(
    .NUM_TAPS(2),
    .IR_LENGTHS('{8, 10}),  // TAP0=8 bits, TAP1=10 bits
    .MAX_IR_LENGTH(10)
) scan_chain (
    .clk(clk),
    .rst_n(rst_n),
    .tap_tck(jtag_tck),
    .tap_tms(jtag_tms),
    .tap_tdi(jtag_tdi),
    .tap_tdo(jtag_tdo),
    .shift_dr(shift_dr),
    .shift_ir(shift_ir),
    .capture_dr(capture_dr),
    .capture_ir(capture_ir),
    .update_dr(update_dr),
    .update_ir(update_ir),
    .tap_tck_out(tap_tck_array),
    .tap_tms_out(tap_tms_array),
    .tap_tdi_out(tap_tdi_array),
    .tap_tdo_in(tap_tdo_array),
    .selected_tap(current_tap),
    .tap_active(tap_active_mask),
    .total_ir_length(ir_chain_length),
    .total_dr_length(dr_chain_length)
);
```

### TAP Selection

```systemverilog
// Select TAP 0
assign current_tap = 2'd0;

// Select TAP 1
assign current_tap = 2'd1;

// Active indicator
// tap_active[0] = 1 when TAP 0 is selected
// tap_active[1] = 1 when TAP 1 is selected
```

### IR Scan with Multiple TAPs

```systemverilog
// Example: 2 TAPs with IR lengths 8 and 10
// Total IR chain length = 8 + 10 = 18 bits

// To load instruction 0x05 into TAP 1:
// Shift pattern: [TAP0_BYPASS][TAP1_INSTRUCTION]
//               = [11111111][0000000101]
//               = 18-bit value
```

## Bypass Management

### Automatic Bypass

When a TAP is **not selected:**
- **IR Shift**: TAP's IR is filled with all 1s (bypass)
- **DR Shift**: TAP uses 1-bit bypass register (initialized to 0)

When a TAP **is selected:**
- **IR Shift**: TAP's actual IR is used
- **DR Shift**: TAP's actual DR is used

### Bypass Register

```
Non-Selected TAPs:
  DR = 1-bit register
  Capture: Load 0
  Shift: Shift TDI → TDO
  Update: No effect
```

## Chain Length Calculation

### IR Chain Length

```
Total IR Length = Σ (IR_LENGTH[i]) for i = 0 to NUM_TAPS-1

Example:
  TAP0: IR=8 bits
  TAP1: IR=10 bits
  TAP2: IR=6 bits
  Total = 8 + 10 + 6 = 24 bits
```

### DR Chain Length

```
Total DR Length = (NUM_TAPS - 1) + DR_LENGTH[selected_tap]

Where:
  (NUM_TAPS - 1) = bypass bits from non-selected TAPs
  DR_LENGTH[selected_tap] = actual DR of selected TAP

Example (TAP1 selected with 32-bit DR):
  Total = 2 (bypass) + 32 (DR) = 34 bits
```

## Padding Calculation

### Pre-Padding

Bits to shift **before** reaching selected TAP:

```
Pre-Padding = Σ IR_LENGTH[i] for i < selected_tap

Example (TAP2 selected):
  Pre-Padding = IR_LENGTH[0] + IR_LENGTH[1]
              = 8 + 10 = 18 bits
```

### Post-Padding

Bits to shift **after** selected TAP:

```
Post-Padding = Σ IR_LENGTH[i] for i > selected_tap

Example (TAP1 selected, 4 TAPs total):
  Post-Padding = IR_LENGTH[2] + IR_LENGTH[3]
               = 6 + 10 = 16 bits
```

## Example: 4-TAP Chain

### Configuration

```systemverilog
parameter int NUM_TAPS = 4;
parameter int IR_LENGTHS [4] = '{8, 8, 6, 10};
```

### Chain Details

| TAP | IR Length | Cumulative Position |
|-----|-----------|---------------------|
| 0   | 8 bits    | 0-7                 |
| 1   | 8 bits    | 8-15                |
| 2   | 6 bits    | 16-21               |
| 3   | 10 bits   | 22-31               |

**Total IR Chain Length**: 32 bits

### Accessing TAP 2

1. **Select TAP**: `selected_tap = 2`
2. **Pre-Padding**: 16 bits (TAP0 + TAP1)
3. **TAP 2 IR**: 6 bits
4. **Post-Padding**: 10 bits (TAP3)
5. **Total Shift**: 32 bits

**Shift Pattern**:
```
[TAP0:8bits][TAP1:8bits][TAP2:6bits][TAP3:10bits]
[11111111  ][11111111  ][XXXXXX    ][1111111111  ]
                         ^------^
                         TAP 2 instruction
```

## Integration Example

### System with 3 RISC-V Cores

```systemverilog
module riscv_soc (
    input  logic clk,
    input  logic rst_n,

    // JTAG interface
    input  logic jtag_tck,
    input  logic jtag_tms,
    input  logic jtag_tdi,
    output logic jtag_tdo
);
    // TAP arrays
    logic [2:0] core_tck, core_tms, core_tdi, core_tdo;
    logic [2:0] tap_active;
    logic [1:0] selected_core;

    // Scan chain controller
    jtag_scan_chain #(
        .NUM_TAPS(3),
        .IR_LENGTHS('{8, 8, 8}),
        .MAX_IR_LENGTH(8)
    ) chain (
        .clk(clk),
        .rst_n(rst_n),
        .tap_tck(jtag_tck),
        .tap_tms(jtag_tms),
        .tap_tdi(jtag_tdi),
        .tap_tdo(jtag_tdo),
        // ... control signals ...
        .tap_tck_out(core_tck),
        .tap_tms_out(core_tms),
        .tap_tdi_out(core_tdi),
        .tap_tdo_in(core_tdo),
        .selected_tap(selected_core),
        .tap_active(tap_active)
    );

    // Core 0 debug TAP
    jtag_top core0_dbg (
        .tck(core_tck[0]),
        .tms(core_tms[0]),
        .tdi(core_tdi[0]),
        .tdo(core_tdo[0]),
        ...
    );

    // Core 1 debug TAP
    jtag_top core1_dbg (
        .tck(core_tck[1]),
        .tms(core_tms[1]),
        .tdi(core_tdi[1]),
        .tdo(core_tdo[1]),
        ...
    );

    // Core 2 debug TAP
    jtag_top core2_dbg (
        .tck(core_tck[2]),
        .tms(core_tms[2]),
        .tdi(core_tdi[2]),
        .tdo(core_tdo[2]),
        ...
    );
endmodule
```

## OpenOCD Configuration

### Multi-TAP Chain

```tcl
# Define 3 TAPs in chain
jtag newtap riscv0 cpu -irlen 8 -expected-id 0x1DEAD3FF
jtag newtap riscv1 cpu -irlen 8 -expected-id 0x2DEAD3FF
jtag newtap riscv2 cpu -irlen 8 -expected-id 0x3DEAD3FF

# Define targets
target create riscv0.cpu riscv -chain-position riscv0.cpu
target create riscv1.cpu riscv -chain-position riscv1.cpu
target create riscv2.cpu riscv -chain-position riscv2.cpu

# Select target
targets riscv1.cpu
```

## Timing Considerations

### Clock Distribution

All TAPs receive the same clock signal:
```systemverilog
for (genvar i = 0; i < NUM_TAPS; i++) begin
    assign tap_tck_out[i] = tap_tck;
    assign tap_tms_out[i] = tap_tms;
end
```

### Propagation Delay

Total TDI-to-TDO delay:
```
t_total = t_controller + (NUM_TAPS × t_tap_propagation)

Example (4 TAPs, each with 5ns delay):
t_total = 2ns + (4 × 5ns) = 22ns
```

## Limitations

1. **Maximum TAPs**: 8 (configurable, can be increased)
2. **IR Length**: Must be known at compile time
3. **DR Length**: Dynamic, calculated per-access
4. **Clock Speed**: Limited by chain propagation delay

## Future Enhancements

- [ ] Dynamic IR length detection
- [ ] Automatic TAP discovery (IDCODE scan)
- [ ] Hot-plug TAP support
- [ ] Multi-level hierarchical chains
- [ ] TAP disabling/enabling
- [ ] Chain integrity checking

## References

1. **IEEE 1149.1-2013** - Section 10.4: Scan Path Linkage
2. **JTAG Primer** - Multi-device scan chains
3. [src/jtag/jtag_scan_chain.sv](../src/jtag/jtag_scan_chain.sv) - Implementation

## Related Documentation

- [JTAG Module Hierarchy](JTAG_MODULE_HIERARCHY.md)
- [RISC-V Debug Module](RISCV_DEBUG_MODULE.md)
- [OScan1 Implementation](OSCAN1_IMPLEMENTATION.md)
