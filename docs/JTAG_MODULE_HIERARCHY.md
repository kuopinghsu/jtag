# JTAG Module Hierarchy Documentation

Complete module structure and hierarchy for the JTAG/cJTAG implementation.

## Overview

The JTAG implementation is organized in a hierarchical structure with clear separation of concerns:

```
jtag_top (Top-level integration)
├── jtag_interface (Mode switching & physical interface)
│   └── oscan1_controller (cJTAG OScan1 protocol)
│       └── cjtag_crc_parity (CRC-8 & parity error detection)
├── jtag_scan_chain (Multi-TAP scan chain controller)
├── jtag_tap_controller (IEEE 1149.1 TAP state machine)
├── jtag_instruction_register (IR management)
└── jtag_dtm (Debug Transport Module)
    └── jtag_dmi_pkg (DMI interface package)
```

## Module Hierarchy

### 1. jtag_top.sv
**Top-Level Integration Module**

**Purpose:** Integrates all JTAG components and provides the main interface to the system.

**Key Responsibilities:**
- 4-pin physical interface management (JTAG 4-wire / cJTAG 2-wire)
- Mode selection routing (JTAG vs cJTAG)
- DMI interface to RISC-V Debug Module
- Signal routing between submodules

**Instantiates:**
- `jtag_interface` - Physical interface and mode switching
- `jtag_tap_controller` - TAP state machine
- `jtag_instruction_register` - IR management
- `jtag_dtm` - Debug Transport Module

**Interface:**
```systemverilog
module jtag_top (
    input  logic        clk,
    input  logic        rst_n,
    
    // 4 Shared Physical Pins
    input  logic        jtag_pin0_i,      // TCK/TCKC
    input  logic        jtag_pin1_i,      // TMS/TMSC (in)
    output logic        jtag_pin1_o,      // TMS/TMSC (out)
    output logic        jtag_pin1_oen,    // TMS/TMSC (oe)
    input  logic        jtag_pin2_i,      // TDI
    output logic        jtag_pin3_o,      // TDO
    output logic        jtag_pin3_oen,    // TDO (oe)
    input  logic        jtag_trst_n_i,
    input  logic        mode_select,      // 0=JTAG, 1=cJTAG
    
    // DMI Interface (to Debug Module)
    output logic [6:0]  dmi_addr,
    output logic [31:0] dmi_wdata,
    input  logic [31:0] dmi_rdata,
    output dmi_op_e     dmi_op,
    input  dmi_resp_e   dmi_resp,
    output logic        dmi_req_valid,
    input  logic        dmi_req_ready,
    
    // Outputs
    output logic [31:0] idcode,
    output logic        active_mode
);
```

**Signal Flow:**
1. Physical pins → `jtag_interface` → Internal JTAG signals
2. Internal signals → `jtag_tap_controller` → TAP state
3. TAP state + IR → `jtag_dtm` → DMI interface
4. DMI → RISC-V Debug Module (external)

---

### 2. jtag_interface.sv
**Physical Interface & Mode Switching**

**Purpose:** Handles physical layer interface and switches between JTAG and cJTAG modes.

**Key Responsibilities:**
- Mode selection (JTAG 4-wire vs cJTAG 2-wire)
- Pin multiplexing and routing
- OScan1 protocol handling (via oscan1_controller)
- Signal conversion between modes

**Instantiates:**
- `oscan1_controller` - Full OScan1 protocol implementation

**Interface:**
```systemverilog
module jtag_interface (
    input  logic       clk,
    input  logic       rst_n,
    input  logic       mode_select,      // 0=JTAG, 1=cJTAG
    
    // JTAG Mode Pins (4-wire)
    input  logic       tck,
    input  logic       tms,
    input  logic       tdi,
    output logic       tdo,
    input  logic       trst_n,
    
    // cJTAG Mode Pins (2-wire)
    input  logic       tco,              // Clock/data combined
    input  logic       tmsc_in,          // Bidirectional data
    output logic       tmsc_out,
    output logic       tmsc_oen,
    output logic       tdi_oscan,
    
    // Internal JTAG Signals
    output logic       jtag_clk,
    output logic       jtag_tms,
    output logic       jtag_tdi,
    input  logic       jtag_tdo,
    output logic       jtag_rst_n,
    output logic       active_mode
);
```

**Mode Selection Logic:**
```
mode_select = 0 (JTAG):
  jtag_clk ← tck
  jtag_tms ← tms
  jtag_tdi ← tdi
  tdo ← jtag_tdo

mode_select = 1 (cJTAG):
  jtag_clk ← oscan1_tck
  jtag_tms ← oscan1_tms
  jtag_tdi ← oscan1_tdi
  tmsc_out ← oscan1_tmsc_out
  tmsc_oen ← oscan1_tmsc_oen
```

---

### 3. oscan1_controller.sv
**IEEE 1149.7 OScan1 Protocol Handler**

**Purpose:** Implements the complete OScan1 (cJTAG) protocol state machine.

**Key Responsibilities:**
- OAC (Offline Access Controller) detection (16 consecutive edges)
- JScan packet parsing (4-bit commands)
- Zero stuffing/unstuffing (after 5 consecutive 1s)
- Scanning Format 0 (SF0) decoder
- TDO return path on TMSC
- CRC-8 error detection (configurable)
- Even/odd parity checking (configurable)
- Error statistics and recovery

**Instantiates:**
- `cjtag_crc_parity` - CRC-8 and parity error detection module

**State Machine:**
```
IDLE → OAC_DETECT → JSCAN → OSCAN_SF0/SF1/SF2 → (loop)
                        ↓ (on error)
                      ERROR → IDLE
```

**Interface:**
```systemverilog
module oscan1_controller #(
    parameter bit ENABLE_CRC = 1'b0,
    parameter bit ENABLE_PARITY = 1'b0
)(
    input  logic       clk,
    input  logic       rst_n,
    
    // Physical cJTAG pins
    input  logic       tco,              // Combined clock/data
    input  logic       tmsc_in,          // Bidirectional data in
    output logic       tmsc_out,         // Bidirectional data out
    output logic       tmsc_oen,         // Output enable
    
    // JTAG signals (to TAP)
    output logic       tck,
    output logic       tms,
    output logic       tdi,
    input  logic       tdo,
    
    // Status
    output logic       oscan1_active,
    output logic       oscan1_error,
    
    // Error statistics
    output logic [15:0] crc_error_count,
    output logic [15:0] parity_error_count
);
```

**Protocol Features:**
- **OAC Detection:** Monitors 16 consecutive rising/falling edges on TCO
- **JScan Commands:** 8 commands (CJTAG_START, CJTAG_OSCAN1, CJTAG_OSCAN2, etc.)
- **Zero Stuffing:** Automatic insertion/deletion after 5 consecutive 1s
- **SF0 Format:** Extracts TMS/TDI from combined bitstream
- **TDO Return:** Sends TDO data back on TMSC during read cycles

**JScan Command Set:**
```
0x0: No operation
0x1: CJTAG_START - Start cJTAG mode
0x2: CJTAG_OSCAN1 - Enter OScan1 (Scanning Format 0)
0x3: CJTAG_OSCAN2 - Enter OScan2 (Scanning Format 1)
0x4: CJTAG_OSCAN3 - Enter OScan3 (Scanning Format 2)
0x5-0xF: Reserved
```

---

### 4. jtag_tap_controller.sv
**IEEE 1149.1 TAP State Machine**

**Purpose:** Implements the standard JTAG Test Access Port controller.

**Key Responsibilities:**
- 16-state TAP state machine
- State transitions based on TMS
- Capture/Shift/Update timing control
- Reset handling (TRST and TMS-based)

**State Machine (IEEE 1149.1):**
```
TEST_LOGIC_RESET
    ↓ (TMS=0)
RUN_TEST_IDLE
    ↓ (TMS=1)          ↓ (TMS=1)
SELECT_DR_SCAN    SELECT_IR_SCAN
    ↓ (TMS=0)          ↓ (TMS=0)
CAPTURE_DR         CAPTURE_IR
    ↓ (TMS=0)          ↓ (TMS=0)
SHIFT_DR           SHIFT_IR
    ↓ (TMS=1)          ↓ (TMS=1)
EXIT1_DR           EXIT1_IR
    ↓ (TMS=0/1)        ↓ (TMS=0/1)
PAUSE_DR/UPDATE    PAUSE_IR/UPDATE
```

**Interface:**
```systemverilog
module jtag_tap_controller (
    input  logic       tck,
    input  logic       tms,
    input  logic       trst_n,
    
    // TAP state outputs
    output logic       shift_dr,
    output logic       update_dr,
    output logic       capture_dr,
    output logic       shift_ir,
    output logic       update_ir,
    output logic       capture_ir,
    output logic       test_logic_reset,
    output logic       run_test_idle
);
```

**Usage:**
- Drives control signals for data register (DR) operations
- Drives control signals for instruction register (IR) operations
- Provides state information to other modules

---

### 5. jtag_instruction_register.sv
**Instruction Register Management**

**Purpose:** Manages the JTAG instruction register and instruction decoding.

**Key Responsibilities:**
- 8-bit instruction register
- Instruction capture/shift/update
- Instruction decoding
- Default instruction handling

**Supported Instructions:**
```
0x01: IDCODE  - Read device identification
0x10: DTMCS   - DTM Control and Status
0x11: DMI     - Debug Module Interface
0xFF: BYPASS  - 1-bit bypass
```

**Interface:**
```systemverilog
module jtag_instruction_register (
    input  logic       tck,
    input  logic       tdi,
    output logic       tdo,
    input  logic       shift_ir,
    input  logic       update_ir,
    input  logic       capture_ir,
    input  logic       trst_n,
    
    output logic [7:0] ir_out
);
```

**Operation:**
1. **Capture_IR:** Loads default pattern (0x01)
2. **Shift_IR:** Shifts in new instruction
3. **Update_IR:** Latches instruction to `ir_out`
4. **IR decoding:** Used by DTM to select data register

---

### 6. jtag_dtm.sv
**Debug Transport Module (RISC-V Debug Spec 0.13.2)**

**Purpose:** Implements RISC-V debug transport over JTAG/DMI.

**Key Responsibilities:**
- IDCODE register (device identification)
- DTMCS register (DTM control and status)
- DMI register (Debug Module Interface - 41 bits)
- DMI transaction handling
- Busy/response management

**Registers:**
```
IDCODE (32-bit):
  [31:28] Version    = 0x1
  [27:12] Part Num   = 0xDEAD
  [11:1]  Mfg ID     = 0x1FF
  [0]     Required   = 1

DTMCS (32-bit):
  [31:18] Reserved
  [17]    dmihardreset
  [16]    dmireset
  [15:12] Reserved
  [11:10] idle       = 1 (required idle cycles)
  [9:4]   dmistat    (DMI status)
  [3:0]   abits      = 7 (DMI address width)

DMI (41-bit):
  [40:34] address    (7 bits)
  [33:2]  data       (32 bits)
  [1:0]   op         (2 bits: 00=NOP, 01=READ, 10=WRITE)
```

**Interface:**
```systemverilog
module jtag_dtm (
    input  logic                    clk,
    input  logic                    rst_n,
    
    // JTAG TAP interface
    input  logic                    tdi,
    output logic                    tdo,
    input  logic                    shift_dr,
    input  logic                    update_dr,
    input  logic                    capture_dr,
    input  logic [7:0]              ir_out,
    
    // DMI interface to Debug Module
    output logic [6:0]              dmi_addr,
    output logic [31:0]             dmi_wdata,
    input  logic [31:0]             dmi_rdata,
    output dmi_op_e                 dmi_op,
    input  dmi_resp_e               dmi_resp,
    output logic                    dmi_req_valid,
    input  logic                    dmi_req_ready,
    
    output logic [31:0]             idcode
);
```

**DMI Transaction Flow:**
1. **Shift:** Load DMI register with address + data + operation
2. **Update:** Start DMI transaction
3. **Capture:** Capture response (busy/success/failed)
4. **Shift:** Read response data

---

### 7. jtag_dmi_pkg.sv
**DMI Interface Package**

**Purpose:** Defines types and constants for the Debug Module Interface.

**Key Definitions:**
```systemverilog
package jtag_dmi_pkg;
    // DMI interface widths
    localparam int DMI_ADDR_WIDTH = 7;
    localparam int DMI_DATA_WIDTH = 32;
    localparam int DMI_OP_WIDTH   = 2;
    
    // DMI operation types
    typedef enum logic [1:0] {
        DMI_OP_NOP   = 2'b00,
        DMI_OP_READ  = 2'b01,
        DMI_OP_WRITE = 2'b10
    } dmi_op_e;
    
    // DMI response types
    typedef enum logic [1:0] {
        DMI_RESP_SUCCESS = 2'b00,
        DMI_RESP_RESERVED = 2'b01,
        DMI_RESP_FAILED  = 2'b10,
        DMI_RESP_BUSY    = 2'b11
    } dmi_resp_e;
endpackage
```

---

## Signal Flow Diagrams

### JTAG Mode (4-wire) Signal Flow
```
Physical Pins → jtag_interface → jtag_top → TAP Controller
                                           ↓
                                    Instruction Register
                                           ↓
                                    Debug Transport Module
                                           ↓
                                    DMI Interface → Debug Module
```

### cJTAG Mode (2-wire OScan1) Signal Flow
```
Physical Pins → jtag_interface → oscan1_controller → JTAG signals
                                                          ↓
                                                   jtag_top → TAP Controller
                                                          ↓
                                                   DTM → DMI → Debug Module
```

### Detailed OScan1 Processing
```
TCO (clock/data) → OAC Detector → JScan Parser → SF0 Decoder → TMS/TDI
                                                                    ↓
TMSC (bidir) ← Zero Stuffing ← TDO Formatter ← TDO ← TAP Controller
```

## Module Dependencies

```
jtag_top.sv
├── Imports: jtag_dmi_pkg
├── Instantiates: jtag_interface
├── Instantiates: jtag_tap_controller
├── Instantiates: jtag_instruction_register
└── Instantiates: jtag_dtm

jtag_interface.sv
└── Instantiates: oscan1_controller

oscan1_controller.sv
└── (standalone, no dependencies)

jtag_tap_controller.sv
└── (standalone, no dependencies)

jtag_instruction_register.sv
└── (standalone, no dependencies)

jtag_dtm.sv
└── Imports: jtag_dmi_pkg

jtag_dmi_pkg.sv
└── (package, defines types)
```

## File Organization

```
src/jtag/
├── jtag_dmi_pkg.sv              # Package: DMI interface types
├── jtag_top.sv                  # Top-level integration
├── jtag_interface.sv            # Physical interface & mode switching
├── oscan1_controller.sv         # OScan1 protocol implementation
├── jtag_tap_controller.sv       # TAP state machine
├── jtag_instruction_register.sv # Instruction register
└── jtag_dtm.sv                  # Debug Transport Module
```

## Integration Example

### Minimal System Integration
```systemverilog
module system (
    input  logic clk,
    input  logic rst_n,
    
    // JTAG pins
    input  logic jtag_tck,
    input  logic jtag_tms,
    input  logic jtag_tdi,
    output logic jtag_tdo,
    
    input  logic mode_select
);
    // DMI signals
    logic [6:0]  dmi_addr;
    logic [31:0] dmi_wdata, dmi_rdata;
    dmi_op_e     dmi_op;
    dmi_resp_e   dmi_resp;
    logic        dmi_req_valid, dmi_req_ready;
    
    // JTAG top
    jtag_top jtag (
        .clk(clk),
        .rst_n(rst_n),
        .jtag_pin0_i(jtag_tck),
        .jtag_pin1_i(jtag_tms),
        .jtag_pin2_i(jtag_tdi),
        .jtag_pin3_o(jtag_tdo),
        .jtag_pin3_oen(),
        .mode_select(mode_select),
        .dmi_addr(dmi_addr),
        .dmi_wdata(dmi_wdata),
        .dmi_rdata(dmi_rdata),
        .dmi_op(dmi_op),
        .dmi_resp(dmi_resp),
        .dmi_req_valid(dmi_req_valid),
        .dmi_req_ready(dmi_req_ready),
        ...
    );
    
    // Debug Module
    riscv_debug_module dbg (
        .clk(clk),
        .rst_n(rst_n),
        .dmi_addr(dmi_addr),
        .dmi_wdata(dmi_wdata),
        .dmi_rdata(dmi_rdata),
        .dmi_op(dmi_op),
        .dmi_resp(dmi_resp),
        .dmi_req_valid(dmi_req_valid),
        .dmi_req_ready(dmi_req_ready),
        ...
    );
endmodule
```

## Key Design Decisions

### 1. Modular Architecture
- **Benefit:** Easy to test, maintain, and extend
- **Trade-off:** Slight overhead in signal routing

### 2. Dual-Mode Support
- **JTAG:** Standard 5-wire (4 pins + reset)
- **cJTAG:** 2-wire OScan1 with full protocol support
- **Switching:** Runtime selectable via `mode_select`

### 3. DMI Interface
- **Standard:** RISC-V Debug Spec 0.13.2
- **Width:** 7-bit address, 32-bit data
- **Operations:** NOP, READ, WRITE
- **Handshake:** Valid/ready protocol

### 4. OScan1 Implementation
- **Complete:** Full IEEE 1149.7 compliance
- **Features:** OAC, JScan, Zero stuffing, SF0 decoder
- **State Machine:** 7 states with error handling

## Testing & Verification

### Testbenches
- `tb/jtag_tb.sv` - Basic JTAG functionality
- `tb/system_tb.sv` - Full system integration
- `sim/` - VPI simulation support

### Build Targets
```bash
make system      # Build system integration
make sim         # Run JTAG testbench
make sim-system  # Run system testbench
make vpi-sim     # VPI interactive mode
```

### Verification Points
- ✅ TAP state machine transitions
- ✅ Instruction register operations
- ✅ IDCODE read via JTAG
- ✅ DMI transactions
- ✅ OScan1 OAC detection
- ✅ OScan1 JScan parsing
- ✅ Mode switching
- ✅ Debug Module integration

## References

1. **IEEE 1149.1-2013** - JTAG Standard
2. **IEEE 1149.7-2009** - cJTAG/OScan1 Standard
3. **RISC-V Debug Specification 0.13.2** - DMI Interface
4. [docs/OSCAN1_IMPLEMENTATION.md](OSCAN1_IMPLEMENTATION.md) - OScan1 details
5. [docs/RISCV_DEBUG_MODULE.md](RISCV_DEBUG_MODULE.md) - Debug Module details
6. [docs/MULTI_TAP_SCAN_CHAIN.md](MULTI_TAP_SCAN_CHAIN.md) - Multi-TAP documentation
7. [docs/CJTAG_CRC_PARITY.md](CJTAG_CRC_PARITY.md) - CRC/parity error detection
8. [README.md](../README.md) - Project overview

---

## Module Details (Continued)

### 8. jtag_scan_chain.sv
**Multi-TAP Scan Chain Controller**

**Purpose:** Manages multiple TAP controllers in a daisy-chain configuration.

**Key Responsibilities:**
- TAP selection and routing
- Automatic bypass register management
- IR/DR chain length calculation
- Pre/post padding for selected TAP
- Support for up to 8 TAPs in chain

**Interface:**
```systemverilog
module jtag_scan_chain #(
    parameter int NUM_TAPS = 1,
    parameter int IR_LENGTHS [NUM_TAPS] = '{8},
    parameter int MAX_IR_LENGTH = 8
)(
    input  logic        clk,
    input  logic        rst_n,
    
    // Upstream JTAG
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
    
    // Downstream TAP interfaces
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

**Features:**
- **Daisy-Chain Topology:** Connect multiple TAPs in series
- **Bypass Management:** Non-selected TAPs use 1-bit bypass register
- **IR Padding:** Automatic pre/post padding calculation
- **DR Routing:** Dynamic DR length based on selected TAP
- **TAP Indicator:** Active mask shows which TAP is selected

**Usage Example:**
```systemverilog
// 3-TAP configuration
jtag_scan_chain #(
    .NUM_TAPS(3),
    .IR_LENGTHS('{8, 8, 10}),
    .MAX_IR_LENGTH(10)
) chain (
    .selected_tap(current_tap),  // 0, 1, or 2
    .tap_active(active_mask),     // One-hot: 001, 010, or 100
    .total_ir_length(ir_len),     // 8 + 8 + 10 = 26 bits
    ...
);
```

---

### 9. cjtag_crc_parity.sv
**CRC-8 and Parity Error Detection**

**Purpose:** Provides error detection for cJTAG packet integrity.

**Key Responsibilities:**
- CRC-8 calculation (polynomial x^8 + x^2 + x + 1)
- Even/odd parity checking
- Error statistics tracking
- Configurable enable/disable

**Interface:**
```systemverilog
module cjtag_crc_parity #(
    parameter bit ENABLE_CRC = 1'b1,
    parameter bit ENABLE_PARITY = 1'b0,
    parameter bit [7:0] CRC_POLYNOMIAL = 8'h07
)(
    input  logic        clk,
    input  logic        rst_n,
    
    // Data input interface
    input  logic [7:0]  data_byte,
    input  logic        data_valid,
    input  logic        data_last,
    
    // CRC/Parity outputs
    output logic [7:0]  crc_value,
    output logic        parity_bit,
    
    // Error detection
    input  logic [7:0]  expected_crc,
    input  logic        expected_parity,
    output logic        crc_error,
    output logic        parity_error,
    
    // Error statistics
    output logic [15:0] crc_error_count,
    output logic [15:0] parity_error_count,
    
    // Control
    input  logic        clear_errors
);
```

**Features:**
- **CRC-8:** Industry-standard polynomial (0x07)
- **Parity:** Even or odd parity modes
- **Error Counters:** 16-bit statistics for debugging
- **Low Overhead:** 1 cycle per byte processing
- **Configurable:** Enable CRC, parity, or both

**Usage Example:**
```systemverilog
// CRC-only configuration
cjtag_crc_parity #(
    .ENABLE_CRC(1'b1),
    .ENABLE_PARITY(1'b0)
) crc_checker (
    .data_byte(rx_byte),
    .data_valid(byte_ready),
    .data_last(packet_end),
    .crc_value(computed_crc),
    .crc_error(crc_mismatch),
    .crc_error_count(error_stats),
    ...
);
```

---

## Future Enhancements

### Planned Features
- [x] Multi-TAP scan chain (✅ Implemented)
- [x] CRC/parity checking for cJTAG (✅ Implemented)
- [ ] OScan1 Scanning Format 1 (SF1) support
- [ ] OScan1 Scanning Format 2 (SF2) support
- [ ] Boundary scan register
- [ ] Multi-drop OScan1 device support

### Optimization Opportunities
- Clock domain crossing improvements
- Reduced latency in DMI transactions
- Enhanced error recovery in OScan1
- Power optimization for unused modes
