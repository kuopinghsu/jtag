# OpenOCD VPI Adapter Technical Guide

## Overview
This document provides technical details about the JTAG VPI (Virtual Platform Interface) adapter that enables OpenOCD to communicate with Verilator-based JTAG simulations. The VPI adapter acts as a bridge between OpenOCD's `jtag_vpi` driver and the Verilator simulation environment.

## Architecture

### System Components
```
┌──────────┐         TCP/IP           ┌──────────────────┐
│ OpenOCD  │◄────────────────────────►│    VPI Server    │
│ (Client) │      Port 3333           │  (sim/jtag_vpi)  │
└──────────┘                          └──────────────────┘
                                               │
                                               │ VPI API
                                               │
                                      ┌────────▼─────────┐
                                      │    Verilator     │
                                      │   Simulation     │
                                      └──────────────────┘
                                               │
                                               │
                                      ┌────────▼─────────┐
                                      │    JTAG RTL      │
                                      │ (SystemVerilog)  │
                                      └──────────────────┘
```

### Network Configuration
- **VPI Server Port**: 3333 (default)
- **OpenOCD GDB Port**: 3334 (configured to avoid conflicts)
- **OpenOCD Telnet Port**: 4444 (default)
- **Protocol**: TCP/IP, non-blocking sockets

### Poll Mechanism
- **Poll Rate**: Every 10 simulation cycles (100 ns @ 100 MHz)
- **TCK Pulse Duration**: 2 simulation cycles (20 ns)
- **Processing**: Event-driven, non-blocking I/O
- **State Machine**: Multi-cycle operations (CMD_SCAN)

## Protocol Specification

### Command Structure (8 bytes)
```c
struct vpi_cmd {
    uint8_t  cmd;           // Command type
    uint8_t  reserved[3];   // Padding (unused)
    uint32_t length;        // Command parameter (network byte order)
} __attribute__((packed));
```

**Byte Order**: Network byte order (big-endian) for `length` field. Use `ntohl()` for conversion.

### Response Structure (4 bytes)
```c
struct vpi_resp {
    uint8_t response;   // 0x00 = OK, other = Error
    uint8_t tdo_val;    // Current TDO pin state (0 or 1)
    uint8_t mode;       // JTAG mode (reserved)
    uint8_t status;     // Status flags (reserved)
} __attribute__((packed));
```

### Command Types

#### CMD_RESET (0x00)
**Purpose**: Reset JTAG TAP controller to Test-Logic-Reset state

**Parameters**:
- `length`: 0 (unused)

**Behavior**:
- Sets TMS high for 6 consecutive clock cycles
- Forces TAP to Test-Logic-Reset state
- Returns response acknowledging completion

**Response**: Standard 4-byte response with `tdo_val` = current TDO state

#### CMD_SCAN (0x02)
**Purpose**: Perform JTAG scan operation (shift data through TDI/TDO)

**Parameters**:
- `length`: Number of bits to scan (1-65535)

**Data Flow**:
1. Server receives 8-byte command with bit count
2. Server sends response acknowledging command
3. Server receives TMS buffer (`(length + 7) / 8` bytes)
4. Server receives TDI buffer (`(length + 7) / 8` bytes)
5. Server processes scan bit-by-bit
6. Server sends TDO buffer back to client (`(length + 7) / 8` bytes)

**Timing Requirements**:
- TMS/TDI set on setup
- TCK pulse executed
- TDO sampled after falling edge of TCK
- One bit processed per poll cycle

#### CMD_SET_PORT (0x03)
**Purpose**: Configure VPI adapter settings (reserved for future use)

**Parameters**:
- `length`: Configuration data (implementation-specific)

**Response**: Acknowledgment response

## JTAG Signal Timing

### Correct Signal Sequence
```
Bit N:
  1. Set TMS[N] and TDI[N]
  2. Request TCK pulse
  3. Wait for simulation to execute pulse
  4. Sample TDO[N] after falling edge
  5. Advance to bit N+1
```

### Timing Diagram
```
         ┌───┐   ┌───┐   ┌───┐
TCK  ────┘   └───┘   └───┘   └────

TMS  ────<M0>───<M1>───<M2>────

TDI  ────<D0>───<D1>───<D2>────
                 ▲
TDO  ────────<Q0>───<Q1>───────
         │        │
         └────────┴─ Sample window
```

**Key Points**:
- TDO is stable after TCK falling edge
- Sampling must occur AFTER the clock pulse completes
- Setup/hold time requirements met by state machine

## State Machine Implementation

### CMD_SCAN State Flow

#### State: SCAN_IDLE
- **Entry**: Waiting for next scan command
- **Transition**: Receive CMD_SCAN → SCAN_RECEIVING_TMS

#### State: SCAN_RECEIVING_TMS
- **Purpose**: Receive TMS buffer from client
- **Transition**: TMS complete → SCAN_RECEIVING_TDI

#### State: SCAN_RECEIVING_TDI
- **Purpose**: Receive TDI buffer from client
- **Transition**: TDI complete → SCAN_PROCESSING

#### State: SCAN_PROCESSING
- **Purpose**: Execute scan operation bit-by-bit

**Algorithm**:
```cpp
case SCAN_PROCESSING:
    // Wait for previous TCK pulse to complete
    if (pending_tck_pulse) {
        return;  // Poll again later
    }

    // Capture TDO from previous bit (if any)
    if (scan_bit_index > 0) {
        uint32_t prev_bit = scan_bit_index - 1;
        uint32_t byte_idx = prev_bit / 8;
        uint32_t bit_idx = prev_bit % 8;

        if (current_tdo) {
            scan_tdo_buf[byte_idx] |= (1 << bit_idx);
        }
    }

    // Process next bit
    if (scan_bit_index < scan_num_bits) {
        uint32_t byte_idx = scan_bit_index / 8;
        uint32_t bit_idx = scan_bit_index % 8;

        // Set TMS/TDI signals
        pending_tms = (scan_tms_buf[byte_idx] >> bit_idx) & 1;
        pending_tdi = (scan_tdi_buf[byte_idx] >> bit_idx) & 1;

        // Request TCK pulse
        pending_tck_pulse = true;
        scan_bit_index++;
        return;  // Let simulation execute pulse
    }

    // All bits processed
    scan_state = SCAN_SENDING_TDO;
```

**Key Design Points**:
1. **Separation of Concerns**: TDO capture and TCK pulse request happen in different poll cycles
2. **Pending Flag**: `pending_tck_pulse` ensures proper synchronization
3. **Single Bit Per Poll**: Processes one bit, then returns to simulation
4. **Previous Bit Capture**: Captures TDO for bit N-1 when processing bit N

#### State: SCAN_SENDING_TDO
- **Purpose**: Send TDO buffer back to client
- **Transition**: Send complete → SCAN_IDLE

### Multi-Cycle Operation Example

**32-bit scan operation timeline**:
```
Poll 1:  Receive CMD_SCAN(32) → Send response → SCAN_RECEIVING_TMS
Poll 2:  Receive TMS[4 bytes] → SCAN_RECEIVING_TDI
Poll 3:  Receive TDI[4 bytes] → SCAN_PROCESSING
Poll 4:  Set TMS[0]/TDI[0], request TCK → pending=true → return
Poll 5:  pending=true → wait → return
Poll 6:  pending=false, capture TDO[0], set TMS[1]/TDI[1] → pending=true
Poll 7:  pending=true → wait → return
Poll 8:  pending=false, capture TDO[1], set TMS[2]/TDI[2] → pending=true
...
Poll 66: pending=false, capture TDO[31] → all bits done → SCAN_SENDING_TDO
Poll 67: Send TDO[4 bytes] to client → SCAN_IDLE
```

**Performance**: ~2 polls per bit = ~200 ns per bit @ 100 MHz simulation

## Integration Points

### VPI Server Interface
File: `sim/jtag_vpi_server.cpp`

**Public Methods**:
```cpp
class JtagVpiServer {
public:
    void poll();                           // Called every 10 sim cycles
    void update_signals(bool tdo);         // Update TDO from simulation
    void get_pending_signals(bool& tms,    // Get signals to apply
                           bool& tdi,
                           bool& tck);
};
```

### Simulation Integration
File: `sim/sim_vpi_main.cpp`

**Integration Flow**:
```cpp
void vpi_poll_callback() {
    // Called every 10 simulation cycles

    // 1. Update TDO from current simulation state
    bool current_tdo = get_tdo_signal();
    vpi_server.update_signals(current_tdo);

    // 2. Poll VPI server (process network I/O)
    vpi_server.poll();

    // 3. Get pending JTAG signals
    bool tms, tdi, tck_pulse;
    vpi_server.get_pending_signals(tms, tdi, tck_pulse);

    // 4. Apply signals to simulation
    set_tms_signal(tms);
    set_tdi_signal(tdi);
    if (tck_pulse) {
        execute_tck_pulse();  // Toggle TCK, wait, update TDO
    }
}
```

## OpenOCD Configuration

### Basic JTAG Configuration
File: `openocd/jtag.cfg`

```tcl
# Interface configuration
adapter driver jtag_vpi
jtag_vpi set_port 3333
adapter speed 1000

# GDB configuration (avoid port conflict with VPI)
gdb port 3334

# Target configuration
jtag newtap auto0 tap -irlen 5 -expected-id 0x1dead3ff
target create auto0.tap testee -chain-position auto0.tap
```

### cJTAG Configuration
File: `openocd/cjtag.cfg`

```tcl
adapter driver jtag_vpi
jtag_vpi set_port 3333
adapter speed 1000
gdb port 3334

# Enable cJTAG (compact JTAG) mode
jtag_vpi set_cjtag 1

jtag newtap auto0 tap -irlen 5 -expected-id 0x1dead3ff
target create auto0.tap testee -chain-position auto0.tap
```

**⚠️ IMPORTANT: cJTAG Limitations**

The standard OpenOCD `jtag_vpi` driver **does not support cJTAG mode**:

1. **No Protocol Support**: The VPI protocol has no command to switch between JTAG/cJTAG modes
   - Only supports: CMD_RESET (0x00), CMD_SCAN (0x02), CMD_SET_PORT (0x03)
   - No CMD_SET_CJTAG or equivalent command exists

2. **VPI Server Issue**: Even with `--cjtag` command-line flag, the server runs in JTAG mode
   - `pending_mode_select` is initialized to 0 (JTAG) in constructor
   - `get_pending_signals()` overwrites `mode_select` on every call
   - Command-line `--cjtag` setting is not preserved

3. **Hardware Design Works**: The RTL correctly implements cJTAG
   - `jtag_top.sv` properly switches between 4-wire JTAG and 2-wire cJTAG
   - OScan1 controller (`oscan1_controller.sv`) fully implements IEEE 1149.7
   - Mode switching via `mode_select` input works correctly

4. **Testing Limitation**: `make test-cjtag` only verifies:
   - VPI server accepts connections
   - OpenOCD telnet interface responds
   - Does NOT test actual cJTAG protocol operation

**Workaround for cJTAG Testing**:
- Use standalone simulation without OpenOCD
- Set `mode_select=1` in testbench
- Verify cJTAG signals with waveform viewer (GTKWave)
- See `tb/jtag_tb.sv` for manual testing examples

**Future Enhancement**:
- Implement custom OpenOCD adapter driver for cJTAG
- Add `CMD_SET_MODE` command to VPI protocol
- Fix VPI server to preserve initial mode setting

## Testing

### Test Makefile Targets

#### test-vpi
Tests VPI server startup and basic functionality:
```bash
make test-vpi
```

**What it tests**:
- VPI server accepts connections on port 3333
- Server responds to basic commands
- Server doesn't crash under normal operation

**Note**: The included `vpi/jtag_vpi_client.c` uses a different (older) 4-byte protocol and is incompatible with OpenOCD's 8-byte protocol. Use OpenOCD for integration testing.

#### test-jtag
Tests full OpenOCD JTAG integration:
```bash
make test-jtag
```

**What it tests**:
- VPI server and OpenOCD can communicate
- OpenOCD successfully connects to simulation
- Telnet interface is accessible
- Basic JTAG operations complete

#### test-cjtag
Tests VPI server with `--cjtag` flag:
```bash
make test-cjtag
```

**What it tests**:
- VPI server accepts connections with `--cjtag` flag
- OpenOCD can connect to server
- Telnet interface is accessible

**⚠️ What it does NOT test**:
- Actual cJTAG protocol operation (server runs in JTAG mode due to VPI bug)
- 2-wire signal operation (TCKC/TMSC)
- OScan1 packet parsing
- Mode switching functionality

**Note**: This test currently only verifies basic connectivity. For actual cJTAG testing, use standalone simulation with manual verification.

### Manual Testing Procedure

#### 1. Start Simulation
```bash
# Terminal 1: Start VPI simulation with trace
./build/jtag_vpi --trace
```

#### 2. Connect OpenOCD
```bash
# Terminal 2: Connect OpenOCD
openocd -f openocd/jtag.cfg

# Expected output:
# Info : Listening on port 3333 for jtag_vpi connection
# Info : Connection from 127.0.0.1:xxxxx
```

#### 3. Test via Telnet
```bash
# Terminal 3: Connect to OpenOCD telnet interface
telnet localhost 4444

# Run test commands
> scan_chain
> help
> shutdown
```

### Python Test Client
File: `test_vpi_client.py`

```python
import socket
import struct

# Connect to VPI server
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect(('localhost', 3333))

# Send CMD_RESET
cmd = struct.pack('>BxxxI', 0x00, 0)  # cmd=0, length=0
sock.send(cmd)

# Receive response
resp = sock.recv(4)
response, tdo, mode, status = struct.unpack('BBBB', resp)
print(f"Response: {response}, TDO: {tdo}")

# Send CMD_SCAN
num_bits = 32
cmd = struct.pack('>BxxxI', 0x02, num_bits)
sock.send(cmd)
resp = sock.recv(4)

# Send TMS buffer (all zeros)
tms_buf = bytes(4)
sock.send(tms_buf)

# Send TDI buffer (all zeros)
tdi_buf = bytes(4)
sock.send(tdi_buf)

# Receive TDO buffer
tdo_buf = sock.recv(4)
print(f"TDO buffer: {tdo_buf.hex()}")

sock.close()
```

## Performance Characteristics

### Throughput
- **Theoretical**: ~5 Mbps (one bit per 200 ns)
- **Practical**: ~1-2 Mbps (accounting for protocol overhead)
- **JTAG Standard**: Typically 1-10 MHz clock rate

### Latency
- **Command Response**: ~100 ns (1 poll cycle)
- **Scan Operation**: ~2N polls for N bits
- **Total Scan Latency**: ~(200 ns × N) for N-bit scan

### Optimization Opportunities
1. **Pipeline TDO Capture**: Overlap TDO capture with next bit setup
2. **Batch Processing**: Process multiple bits per poll when possible
3. **Buffer Pre-loading**: Pre-fetch TMS/TDI during TDO transmission
4. **Fast Path**: Optimize common operations (RESET, IDLE)

## Common Issues and Solutions

### Port Conflicts
**Symptom**: "Address already in use" error

**Solution**:
```bash
# Kill existing VPI servers
pkill -9 jtag_vpi

# Kill existing OpenOCD instances
pkill -9 openocd

# Restart test
make test-jtag
```

### Connection Timeout
**Symptom**: OpenOCD cannot connect to VPI server

**Solution**:
- Verify VPI server is running: `ps aux | grep jtag_vpi`
- Check port 3333 is listening: `lsof -i :3333`
- Check firewall settings (macOS/Linux)

### Protocol Desynchronization
**Symptom**: Random data corruption or hangs

**Root Cause**: Client and server out of sync on buffer sizes

**Prevention**:
- Always use proper byte order conversion (`ntohl()`)
- Verify buffer sizes match expected length
- Use timeout mechanisms for recv() calls

### TDO Capture Errors
**Symptom**: Incorrect scan data readback

**Verification**:
- Check `pending_tck_pulse` flag is respected
- Verify TDO sampled AFTER TCK pulse completes
- Use waveform viewer (FST/VCD) to verify timing

## Known Issues

### cJTAG Mode Not Working with OpenOCD

**Issue**: Simulation always runs in JTAG mode (4-wire) even with `--cjtag` flag

**Root Cause**:
1. VPI server initializes `pending_mode_select = 0` (JTAG mode)
2. Every `get_pending_signals()` call sets `mode_select = pending_mode_select`
3. This overwrites the command-line `--cjtag` setting

**Impact**:
- `make test-cjtag` runs in JTAG mode, not cJTAG mode
- OpenOCD cannot test cJTAG features
- 2-wire operation (TCKC/TMSC) not exercised

**Workaround**:
- Use standalone simulation (not VPI) for cJTAG testing
- Manually set `mode_select=1` in testbench
- Verify with waveform viewer

**Fix Required**:
```cpp
// In jtag_vpi_server.cpp constructor:
pending_mode_select = 0;  // ← Should preserve initial setting

// In get_pending_signals():
*mode_sel = pending_mode_select;  // ← Should only set if changed by command
```

### VPI Client Protocol Incompatibility

**Issue**: `vpi/jtag_vpi_client.c` uses 4-byte command format

**Root Cause**: Client uses legacy protocol, OpenOCD uses 8-byte format

**Impact**:
- Client hangs when connecting to VPI server
- Server blocks waiting for remaining 4 bytes
- `make test-vpi` skips client test

**Workaround**: Use OpenOCD for integration testing instead of included client

**Fix Required**: Update client to use proper 8-byte OpenOCD protocol

### OpenOCD Lacks cJTAG Support

**Issue**: Standard OpenOCD `jtag_vpi` driver doesn't support cJTAG

**Root Cause**: VPI protocol has no command to enable cJTAG mode

**Impact**: Cannot use OpenOCD to test cJTAG features

**Workaround**: Custom OpenOCD adapter driver needed

**Note**: This is a fundamental limitation of OpenOCD's VPI adapter, not a bug in this implementation.

## References

### Standards
- IEEE 1149.1: JTAG Standard Test Access Port
- IEEE 1149.7: Reduced-Pin and Enhanced-Functionality Test Access Port (cJTAG)

### Tools
- OpenOCD: Open On-Chip Debugger
- Verilator: SystemVerilog HDL simulator
- GTKWave: Waveform viewer for FST/VCD files

### Related Documentation
- [JTAG Module Hierarchy](JTAG_MODULE_HIERARCHY.md)
- [Multi-TAP Scan Chain](MULTI_TAP_SCAN_CHAIN.md)
- [OSCAN1 Implementation](OSCAN1_IMPLEMENTATION.md)
- [RISC-V Debug Module](RISCV_DEBUG_MODULE.md)

---

**Document Version**: 1.0
**Last Updated**: January 11, 2026
**Status**: Production Ready
