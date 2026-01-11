# RISC-V Debug Module - Full Implementation

Complete implementation of the RISC-V Debug Specification 0.13.2 with full register set and abstract command execution.

## Features

### Core Capabilities
- ✅ **Full DMI Register Set** - All Debug Module registers from spec 0.13.2
- ✅ **Abstract Commands** - Register access (GPR/CSR), memory access support
- ✅ **Program Buffer** - 16-entry program buffer with execution support
- ✅ **System Bus Access** - 32/64-bit memory-mapped peripheral access
- ✅ **Multi-Hart Support** - Up to 32 hardware threads (configurable)
- ✅ **Error Handling** - Complete error detection and reporting
- ✅ **Hart Control** - Halt, resume, reset operations

### Register Map (DMI Address Space)

#### Abstract Data Registers (0x04-0x0F)
```
0x04: DATA0  - Abstract data register 0
0x05: DATA1  - Abstract data register 1
...
0x0F: DATA11 - Abstract data register 11
```

#### Core Control Registers (0x10-0x1D)
```
0x10: DMCONTROL    - Debug Module Control
0x11: DMSTATUS     - Debug Module Status
0x12: HARTINFO     - Hart Information
0x13: HALTSUM1     - Halt Summary 1
0x14: HAWINDOWSEL  - Hart Array Window Select
0x15: HAWINDOW     - Hart Array Window
0x16: ABSTRACTCS   - Abstract Command Status
0x17: COMMAND      - Abstract Command
0x18: ABSTRACTAUTO - Abstract Command Autoexec
0x19-0x1C: CONFSTRPTR0-3 - Configuration String Pointers
0x1D: NEXTDM       - Next Debug Module
```

#### Program Buffer (0x20-0x2F)
```
0x20: PROGBUF0  - Program buffer entry 0
0x21: PROGBUF1  - Program buffer entry 1
...
0x2F: PROGBUF15 - Program buffer entry 15
```

#### System Bus Access (0x38-0x3F)
```
0x38: SBCS       - System Bus Access Control/Status
0x39: SBADDRESS0 - System Bus Address [31:0]
0x3A: SBADDRESS1 - System Bus Address [63:32]
0x3C: SBDATA0    - System Bus Data [31:0]
0x3D: SBDATA1    - System Bus Data [63:32]
```

## Module Interface

### Parameters
```systemverilog
parameter int NUM_HARTS = 1;           // Number of hardware threads (1-32)
parameter int PROGBUF_SIZE = 16;       // Program buffer entries (0-16)
parameter int DATA_COUNT = 12;         // Abstract data registers (1-12)
parameter bit SUPPORT_IMPEBREAK = 1;   // Support implicit ebreak
```

### Ports

#### DMI Interface (from JTAG DTM)
```systemverilog
input  logic [6:0]  dmi_addr;       // DMI register address
input  logic [31:0] dmi_wdata;      // DMI write data
output logic [31:0] dmi_rdata;      // DMI read data
input  dmi_op_e     dmi_op;         // Operation: READ/WRITE
output dmi_resp_e   dmi_resp;       // Response: SUCCESS/BUSY/FAILED
input  logic        dmi_req_valid;  // Request valid
output logic        dmi_req_ready;  // Ready to accept request
```

#### Hart Control Interface
```systemverilog
output logic [NUM_HARTS-1:0] hart_reset_req;   // Hart reset request
output logic [NUM_HARTS-1:0] hart_halt_req;    // Hart halt request
output logic [NUM_HARTS-1:0] hart_resume_req;  // Hart resume request
input  logic [NUM_HARTS-1:0] hart_halted;      // Hart halted status
input  logic [NUM_HARTS-1:0] hart_running;     // Hart running status
input  logic [NUM_HARTS-1:0] hart_unavailable; // Hart unavailable
input  logic [NUM_HARTS-1:0] hart_havereset;   // Hart reset occurred
```

#### Hart Debug Interface (GPR/CSR Access)
```systemverilog
output logic [4:0]   hart_gpr_addr;   // GPR address (x0-x31)
output logic [31:0]  hart_gpr_wdata;  // GPR write data
input  logic [31:0]  hart_gpr_rdata;  // GPR read data
output logic         hart_gpr_we;     // GPR write enable

output logic [11:0]  hart_csr_addr;   // CSR address
output logic [31:0]  hart_csr_wdata;  // CSR write data
input  logic [31:0]  hart_csr_rdata;  // CSR read data
output logic         hart_csr_we;     // CSR write enable
```

#### Program Buffer Execution
```systemverilog
output logic [31:0]  progbuf_insn;        // Program buffer instruction
output logic         progbuf_insn_valid;  // Instruction valid
input  logic         progbuf_insn_done;   // Execution complete
input  logic         progbuf_exception;   // Exception occurred
```

#### System Bus Access
```systemverilog
output logic [63:0]  sb_address;   // System bus address
output logic [63:0]  sb_wdata;     // System bus write data
input  logic [63:0]  sb_rdata;     // System bus read data
output logic [2:0]   sb_size;      // Access size (0=byte, 1=half, 2=word, 3=double)
output logic         sb_read_req;  // Read request
output logic         sb_write_req; // Write request
input  logic         sb_ready;     // Transaction complete
input  logic         sb_error;     // Bus error
```

## Abstract Commands

### Command Types

#### 1. Access Register (CMD_ACCESS_REG = 0x00)
Access GPR, CSR, or FPR registers in the hart.

**Command Format (COMMAND register 0x17):**
```
[31:24] cmdtype    = 0x00 (Access Register)
[23]    aarpostincrement - Auto-increment regno
[22:20] aarsize    - Access size (0=8b, 1=16b, 2=32b, 3=64b, 4=128b)
[19]    postexec   - Execute program buffer after
[18]    transfer   - Transfer data to/from DATA0
[17]    write      - 0=read, 1=write
[16]    reserved
[15:0]  regno      - Register number
```

**Register Number Encoding:**
- `0x1000-0x101F`: GPR x0-x31 (General Purpose Registers)
- `0x0000-0x0FFF`: CSRs (Control and Status Registers)
- `0x1020-0x103F`: FPR f0-f31 (Floating Point Registers)

**Example: Read GPR x5**
```systemverilog
// Write COMMAND register
dmi_write(0x17, 32'h00020005);  // cmdtype=0, transfer=1, write=0, regno=0x1005

// Read result from DATA0
result = dmi_read(0x04);
```

**Example: Write CSR mstatus (0x300)**
```systemverilog
// Write value to DATA0
dmi_write(0x04, 32'h00001800);

// Write COMMAND register
dmi_write(0x17, 32'h00030300);  // cmdtype=0, transfer=1, write=1, regno=0x300
```

#### 2. Access Memory (CMD_ACCESS_MEM = 0x02)
Direct memory access (not fully implemented in current version).

#### 3. Quick Access (CMD_QUICK_ACCESS = 0x01)
Quick command execution (not implemented).

### Error Codes (ABSTRACTCS.cmderr)

```
0: None         - No error
1: Busy         - Abstract command in progress
2: Not Support  - Command not supported
3: Exception    - Exception during execution
4: Halt/Resume  - Hart not in expected state
5: Bus          - Bus error occurred
7: Other        - Other error
```

**Clear Errors:**
```systemverilog
// Write 1 to cmderr bits to clear
dmi_write(0x16, 32'h00000700);
```

## Program Buffer

### Overview
The program buffer allows execution of arbitrary RISC-V instructions in the context of the halted hart.

### Usage Flow
1. **Halt the hart** via DMCONTROL
2. **Write instructions** to PROGBUF0-PROGBUF15
3. **Set postexec=1** in abstract command
4. **Program buffer executes** after register access
5. **End with ebreak** instruction (0x00100073)

### Example: Custom Instruction Sequence
```systemverilog
// Halt hart
dmi_write(0x10, 32'h80000001);

// Write program buffer
dmi_write(0x20, 32'h00102513);  // addi x10, x0, 1    (load immediate)
dmi_write(0x21, 32'h00A02623);  // sw x10, 12(x0)     (store word)
dmi_write(0x22, 32'h00100073);  // ebreak             (exit progbuf)

// Execute with postexec
dmi_write(0x17, 32'h00270000);  // cmdtype=0, postexec=1
```

## System Bus Access

### Configuration (SBCS Register 0x38)

**Fields:**
```
[31:29] sbversion        - Version (1 = v0.13)
[22]    sbbusy           - Bus operation in progress
[21]    sbreadonaddr     - Read on address write
[20:17] sbaccess         - Access width support bits
[16]    sbautoincrement  - Auto-increment address
[15]    sberror          - Bus error flag
[14:12] sbaccess         - Current access size
[11:5]  sbasize          - Address bus width (64)
```

### Usage

#### Simple Read
```systemverilog
// Set access size to 32-bit
dmi_write(0x38, 32'h00002000);  // sbaccess = 2 (word)

// Write address
dmi_write(0x39, 32'h80000000);  // SBADDRESS0

// Read data
data = dmi_read(0x3C);  // SBDATA0
```

#### Auto-increment Read
```systemverilog
// Enable auto-increment and read-on-address
dmi_write(0x38, 32'h00212000);  // sbaccess=2, sbautoincrement=1, sbreadonaddr=1

// Write starting address (triggers first read)
dmi_write(0x39, 32'h80000000);

// Subsequent reads from SBDATA0 auto-increment address
data1 = dmi_read(0x3C);  // Address 0x80000000
data2 = dmi_read(0x3C);  // Address 0x80000004 (auto-incremented)
data3 = dmi_read(0x3C);  // Address 0x80000008 (auto-incremented)
```

#### Write with Auto-increment
```systemverilog
// Set write address
dmi_write(0x39, 32'h80000000);

// Write data (auto-increments)
dmi_write(0x3C, 32'hDEADBEEF);  // Write to 0x80000000
dmi_write(0x3C, 32'hCAFEBABE);  // Write to 0x80000004
```

## Hart Control

### DMCONTROL Register (0x10)

**Key Fields:**
```
[31]    haltreq           - Request hart halt
[30]    resumereq         - Request hart resume (one-shot)
[29]    hartreset         - Hart reset request
[28]    ackhavereset      - Acknowledge hart reset
[26]    hasel             - Hart array selection
[25:16] hartsello         - Hart select low
[15:6]  hartselhi         - Hart select high
[3]     setresethaltreq   - Set reset-halt request
[2]     clrresethaltreq   - Clear reset-halt request
[1]     ndmreset          - Non-debug module reset
[0]     dmactive          - Debug module active
```

### Common Operations

#### Activate Debug Module
```systemverilog
dmi_write(0x10, 32'h00000001);  // dmactive = 1
```

#### Halt Hart
```systemverilog
dmi_write(0x10, 32'h80000001);  // haltreq = 1, dmactive = 1
```

#### Resume Hart
```systemverilog
dmi_write(0x10, 32'h40000001);  // resumereq = 1, dmactive = 1
```

#### Select Different Hart (Multi-hart)
```systemverilog
// Select hart 2
dmi_write(0x10, 32'h00020001);  // hartsello = 2, dmactive = 1
```

#### Hart Array Selection
```systemverilog
// Select all harts
dmi_write(0x10, 32'h04000001);  // hasel = 1, dmactive = 1

// Halt all harts
dmi_write(0x10, 32'h84000001);  // hasel = 1, haltreq = 1, dmactive = 1
```

### DMSTATUS Register (0x11)

**Key Fields (Read-Only):**
```
[22]    impebreak         - Implicit ebreak support
[19]    allresumeack      - All selected harts resumed
[18]    anyresumeack      - Any selected hart resumed
[17]    allnonexistent    - All selected harts don't exist
[16]    anynonexistent    - Any selected hart doesn't exist
[15]    allunavail        - All selected harts unavailable
[14]    anyunavail        - Any selected hart unavailable
[13]    allrunning        - All selected harts running
[12]    anyrunning        - Any selected hart running
[11]    allhalted         - All selected harts halted
[10]    anyhalted         - Any selected hart halted
[9]     authenticated     - Debugger authenticated
[8]     authbusy          - Authentication in progress
[7]     hasresethaltreq   - Reset-halt request supported
[6]     confstrptrvalid   - Config string pointer valid
[3:0]   version           - Debug spec version (2 = v0.13)
```

## Integration Example

### System Top Module
```systemverilog
module system_top(
    input logic clk, rst_n,
    // JTAG/cJTAG pins
    ...
);
    // DMI signals
    logic [6:0]  dmi_addr;
    logic [31:0] dmi_wdata, dmi_rdata;
    dmi_op_e     dmi_op;
    dmi_resp_e   dmi_resp;
    
    // JTAG with DTM
    jtag_top jtag (
        .clk(clk), .rst_n(rst_n),
        .dmi_addr(dmi_addr),
        .dmi_wdata(dmi_wdata),
        .dmi_rdata(dmi_rdata),
        .dmi_op(dmi_op),
        .dmi_resp(dmi_resp),
        ...
    );
    
    // Debug Module
    riscv_debug_module #(
        .NUM_HARTS(4),
        .PROGBUF_SIZE(16),
        .DATA_COUNT(12)
    ) dbg (
        .clk(clk), .rst_n(rst_n),
        .dmi_addr(dmi_addr),
        .dmi_wdata(dmi_wdata),
        .dmi_rdata(dmi_rdata),
        .dmi_op(dmi_op),
        .dmi_resp(dmi_resp),
        // Connect to RISC-V core
        ...
    );
endmodule
```

## Testing

### Build and Run
```bash
# Build system integration
make system

# Run simulation
make sim-system

# View waveforms
gtkwave system_sim.fst
```

### Test Coverage
The implementation includes comprehensive tests for:
- ✅ IDCODE read via JTAG
- ✅ DMI register access
- ✅ Hart halt/resume control
- ✅ DMSTATUS monitoring
- ✅ Abstract command execution
- ✅ GPR/CSR read/write
- ✅ Program buffer execution
- ✅ System bus access

## Limitations & Future Work

### Current Limitations
- Memory access commands not fully implemented
- Quick access commands not implemented
- Authentication not implemented (always authenticated)
- Configuration string not provided
- Single debug module (no NEXTDM chaining)

### Future Enhancements
- [ ] Complete memory access abstract commands
- [ ] Trigger module support
- [ ] Trace module integration
- [ ] Multi-drop debug module chaining
- [ ] Authentication mechanism
- [ ] External debug interface (SBA master)
- [ ] Instruction trace buffer

## Reference Documents

1. **RISC-V Debug Specification 0.13.2**
   - https://github.com/riscv/riscv-debug-spec/releases/tag/v0.13.2

2. **RISC-V Privileged Specification**
   - CSR definitions and hart state

3. **JTAG/DMI Interface**
   - [src/jtag/jtag_dmi_pkg.sv](../src/jtag/jtag_dmi_pkg.sv)
   - [src/jtag/jtag_dtm.sv](../src/jtag/jtag_dtm.sv)

4. **Debug Module Implementation**
   - [src/dbg/riscv_debug_module.sv](../src/dbg/riscv_debug_module.sv)

## License

This implementation is provided for educational and development purposes.
Based on the RISC-V Debug Specification 0.13.2.
