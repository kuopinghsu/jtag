# Legacy VPI Protocol Test Guide

## Purpose
- Validate backward compatibility of the legacy 8-byte VPI protocol alongside modern OpenOCD jtag_vpi.
- Ensure protocol auto-detection, command handling, and scan behavior remain stable in regression runs.

## Protocol Format (Legacy 8-byte)
```c
struct legacy_vpi_cmd {
    uint8_t cmd;       // Command
    uint8_t mode;      // Mode/flags
    uint8_t reserved[2];
    uint32_t length;   // Payload length (big-endian)
} __attribute__((packed));
```
- Commands: 0x00 RESET, 0x01 TMS_SEQ, 0x02 SCAN, 0x03 RUNTEST.
- Auto-detect: exactly 8 bytes → legacy; >8 bytes → modern OpenOCD jtag_vpi.

## Test Assets
- Source: `openocd/test_legacy_protocol.c` (~550 LOC)
- Binary: `openocd/test_legacy_protocol`
- Harness: `openocd/test_openocd.sh` (invoked by Makefile targets)
- Waveforms: `jtag_vpi.fst` (optional, via `--trace`)

## Coverage (10 Cases)
1) VPI connection (port 3333)
2) RESET (0x00)
3) TMS sequence (0x01)
4) SCAN 8-bit (0x02)
5) Multiple sequential commands (3× reset)
6) Reset then scan sequence
7) Large scan (32-bit)
8) Unknown command robustness (0xFF)
9) Protocol auto-detection (8-byte trigger)
10) Rapid command sequence (stress)

## Integration Points
- `make test-jtag` → starts VPI server (modern mode) and runs `test_openocd.sh jtag`, which conditionally runs legacy tests when modern protocol is not already active.
- `make test-cjtag` → starts VPI server (cJTAG mode) and runs `test_openocd.sh cjtag`, which conditionally runs legacy tests when modern protocol is not already active.
- `test_openocd.sh` handles compilation of `test_legacy_protocol` on demand and coordinates protocol sequencing.

## Execution
### Integrated (recommended)
```bash
make test-jtag    # JTAG + legacy compatibility
make test-cjtag   # cJTAG + legacy compatibility
```

### Standalone
```bash
# Build VPI server
make verilator

# Terminal 1: legacy-only server
./build/jtag_vpi --trace --timeout 60 --proto=legacy

# Terminal 2: run tests
./openocd/test_legacy_protocol
```

## Server Requirements
- Accept 8-byte packets and auto-detect legacy mode on the first 8-byte packet.
- Implement commands: RESET (0x00), TMS_SEQ (0x01), SCAN (0x02), RUNTEST (0x03).
- Handle sequential commands, large bit counts, and rapid sequences without timeout.
- Graceful handling of invalid commands (no crash).

## Artifacts & References
- VPI server implementation: `sim/jtag_vpi_server.cpp`
- Protocol-aware simulation main: `sim/sim_vpi_main.cpp`
- OpenOCD integration script: `openocd/test_openocd.sh`
- Hardware implementation (scan/ TAP): `src/jtag/*.sv`
