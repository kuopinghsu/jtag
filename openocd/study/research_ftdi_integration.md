# FTDI Integration Research for RTL Simulator

## Current Status Analysis

### What I Found in OpenOCD's ftdi.c:

**✅ OpenOCD HAS Native OScan1 (cJTAG) Support!**

From examining OpenOCD's source code, the FTDI driver includes:

1. **Complete OScan1 Implementation** (`BUILD_FTDI_CJTAG == 1`):
   - `oscan1_mpsse_clock_data()` - OScan1 data transfer protocol
   - `oscan1_mpsse_clock_tms_cs()` - OScan1 TMS/TDI protocol conversion
   - `cjtag_reset_online_activate()` - Full OAC/JScan activation sequence
   - 3-wire protocol: TMS=1/TDI=N → TMSC output N, TMS=0 → TMSC input

2. **OScan1 Protocol Features**:
   - OAC (Attention Character) sequences
   - TAP reset via cJTAG escape sequences
   - Online activation with proper timing
   - JScan3 mode (4-wire cJTAG) support
   - JTAG_SEL signal handling

3. **Configuration Commands**:
   ```tcl
   ftdi oscan1_mode on    # Enable OScan1 2-wire mode
   ftdi jscan3_mode on    # Enable JScan3 4-wire mode
   ```

### Your Current Architecture:

```
OpenOCD jtag_vpi ←→ TCP/IP ←→ VPI Server ←→ RTL Simulation
```

### Potential FTDI Integration:

```
OpenOCD ftdi ←→ Virtual FTDI ←→ VPI Server ←→ RTL Simulation
```

## Integration Approaches

### Option 1: FTDI-to-VPI Bridge
**Create a bridge that translates FTDI MPSSE commands to VPI protocol**

**Pros:**
- Leverage OpenOCD's mature OScan1 implementation
- No OpenOCD patches needed
- IEEE 1149.7 compliant
- Full OpenOCD cJTAG feature set

**Cons:**
- Complex FTDI MPSSE protocol to implement
- USB device emulation required
- Additional software layer

### Option 2: libftdi Virtual Device
**Use libftdi to create a virtual FTDI device that forwards to VPI**

**Technical Requirements:**
- Implement FTDI device enumeration
- Handle MPSSE (Multi-Protocol Synchronous Serial Engine) commands
- Map FTDI GPIO signals to JTAG pins
- USB device virtualization

### Option 3: Keep Current VPI + Document Limitations
**Stick with current working approach**

**Current Status:**
- ✅ 15/15 cJTAG tests passing
- ✅ Works with standard OpenOCD
- ✅ No patches required
- ❌ Custom protocol instead of standard FTDI

## Technical Investigation

### OpenOCD FTDI Requirements:

From ftdi.c analysis, OpenOCD's FTDI driver expects:

1. **MPSSE Protocol Support**: Multi-Protocol Synchronous Serial Engine
2. **GPIO Signal Mapping**:
   ```
   TCK, TMS, TDI, TDO  - Standard JTAG
   JTAG_SEL            - cJTAG mode selection
   TMSC_EN             - TMSC output enable (bidirectional control)
   ```
3. **OScan1 Signal Timing**: 3 TCK cycles per JTAG cycle
4. **USB Device Interface**: FTDI VID/PID identification

### Current VPI Server Capabilities:

Your VPI server already implements:
- ✅ TCP/IP protocol handling
- ✅ JTAG signal control
- ✅ cJTAG/OScan1 protocol support
- ✅ Mode switching (JTAG ↔ cJTAG)
- ✅ OpenOCD integration (15/15 tests passing)

## Feasibility Assessment

### High Complexity Factors:
1. **MPSSE Protocol**: Complex serial protocol used by FTDI chips
2. **USB Virtualization**: Creating virtual USB device
3. **Signal Mapping**: Translating FTDI GPIO to RTL simulator pins
4. **Timing Requirements**: Precise FTDI timing expectations

### Medium Complexity Alternative:
**Enhance current VPI server to emulate FTDI interface**
- Create FTDI device emulation layer
- Map MPSSE commands to existing VPI operations
- Use USB-over-TCP bridges (like ser2net, but for USB)

### Lower Complexity (Current Approach):
**Continue with VPI + Optional Patches**
- ✅ Already working (15/15 tests)
- ✅ No additional dependencies
- ✅ Direct RTL integration
- Optional patches for enhanced OpenOCD features

## Research Recommendations

### Immediate Next Steps:

1. **Test OpenOCD FTDI Configuration**:
   ```bash
   # Create test config using FTDI driver
   openocd -c "adapter driver ftdi; ftdi device_desc 'Virtual JTAG'; ftdi oscan1_mode on" -c "exit"
   ```

2. **Examine MPSSE Protocol Requirements**:
   - What specific FTDI commands does OpenOCD's cJTAG support use?
   - Can we create a minimal MPSSE emulator?

3. **USB Virtualization Research**:
   - Tools like USBIP (USB over IP)
   - Virtual USB device frameworks
   - ser2net alternatives for USB

4. **Cost-Benefit Analysis**:
   - Development time vs. current working solution
   - Maintenance overhead of FTDI emulation
   - Feature completeness comparison

## Conclusion

**Your current VPI approach is excellent and working well.**

The FTDI integration is technically feasible but adds significant complexity. Since you already have:
- ✅ Working cJTAG implementation
- ✅ 15/15 tests passing
- ✅ OpenOCD integration
- ✅ No patches required (as of v2.1)

**Recommendation**: Continue with current approach unless you specifically need features only available in OpenOCD's native FTDI OScan1 implementation.

The effort to implement FTDI emulation might be better spent on other project enhancements.