# JTAG/cJTAG Documentation

This directory contains comprehensive technical documentation for the JTAG/cJTAG with RISC-V Debug Module Integration project.

## 📑 Documentation Index

### Core Protocol Implementation

#### [JTAG_MODULE_HIERARCHY.md](JTAG_MODULE_HIERARCHY.md)
**SystemVerilog module organization and interfaces**
- Complete module hierarchy and dependencies
- Interface specifications and signal definitions
- Integration guidelines for JTAG/cJTAG components

#### [OSCAN1_IMPLEMENTATION.md](OSCAN1_IMPLEMENTATION.md)
**IEEE 1149.7 OScan1 cJTAG protocol implementation**
- Complete OScan1 controller with OAC detection
- JScan packet parser and SF0 decoder
- Zero insertion/deletion (bit stuffing)
- Two-wire TCKC/TMSC interface implementation

#### [CJTAG_CRC_PARITY.md](CJTAG_CRC_PARITY.md)
**Error detection mechanisms for cJTAG**
- CRC-8 calculation (polynomial x^8 + x^2 + x + 1)
- Even/odd parity checking
- Error statistics tracking
- Configurable error detection modes

### Debug Module Integration

#### [RISCV_DEBUG_MODULE.md](RISCV_DEBUG_MODULE.md)
**RISC-V Debug Module Interface (DMI) implementation**
- Debug Module registers (DMCONTROL, DMSTATUS, etc.)
- Abstract command support
- System bus access interface
- Hart halt/resume control mechanisms

#### [MULTI_TAP_SCAN_CHAIN.md](MULTI_TAP_SCAN_CHAIN.md)
**Multi-TAP daisy chain support**
- Up to 8 TAPs in scan chain
- Automatic bypass management
- Dynamic TAP selection and routing
- Chain length calculation and optimization

### Testing and Validation

#### [PROTOCOL_TESTING.md](PROTOCOL_TESTING.md)
**Complete testing procedures and current status**
- SystemVerilog testbench coverage (30 tests)
- OpenOCD integration testing (34 tests)
- VPI server testing procedures
- Current test status: All 64 tests passing ✅

#### [PROTOCOL_TEST_COMPARISON.md](PROTOCOL_TEST_COMPARISON.md)
**Comprehensive protocol testing comparison matrix**
- Dual-layer testing architecture analysis
- Cross-protocol test coverage comparison
- Implementation status across all components
- Detailed test matrices for JTAG/cJTAG protocols

#### [LEGACY_PROTOCOL_TESING.md](LEGACY_PROTOCOL_TESING.md)
**Legacy protocol testing procedures**
- Historical testing methods and procedures
- Backward compatibility verification
- Migration guidelines from legacy implementations

### Integration and Tools

#### [OPENOCD_VPI_TECHNICAL_GUIDE.md](OPENOCD_VPI_TECHNICAL_GUIDE.md)
**OpenOCD VPI adapter technical implementation**
- VPI protocol specification (1036-byte packets)
- Network architecture and timing
- State machine implementation details
- Performance characteristics and optimization

#### [OPENOCD_CJTAG_PATCH_GUIDE.md](OPENOCD_CJTAG_PATCH_GUIDE.md)
**OpenOCD cJTAG patch application guide**
- Patch files and installation procedures
- OScan1 protocol layer implementation
- Build system integration steps
- Troubleshooting and validation

## 🎯 Quick Start Guide

### For Protocol Implementation
1. Start with [JTAG_MODULE_HIERARCHY.md](JTAG_MODULE_HIERARCHY.md) for system overview
2. Review [OSCAN1_IMPLEMENTATION.md](OSCAN1_IMPLEMENTATION.md) for cJTAG details
3. Check [RISCV_DEBUG_MODULE.md](RISCV_DEBUG_MODULE.md) for debug integration

### For Testing and Validation
1. Read [PROTOCOL_TESTING.md](PROTOCOL_TESTING.md) for current test status
2. Use [PROTOCOL_TEST_COMPARISON.md](PROTOCOL_TEST_COMPARISON.md) for comprehensive coverage
3. Follow [OPENOCD_VPI_TECHNICAL_GUIDE.md](OPENOCD_VPI_TECHNICAL_GUIDE.md) for VPI setup

### For OpenOCD Integration
1. Follow [OPENOCD_VPI_TECHNICAL_GUIDE.md](OPENOCD_VPI_TECHNICAL_GUIDE.md) for standard integration
2. Apply [OPENOCD_CJTAG_PATCH_GUIDE.md](OPENOCD_CJTAG_PATCH_GUIDE.md) for enhanced cJTAG support

## 📊 Implementation Status

### Core Features ✅
- **JTAG Protocol**: Complete IEEE 1149.1 implementation
- **cJTAG Protocol**: Complete IEEE 1149.7 OScan1 implementation
- **RISC-V Debug**: Full Debug Module Interface (DMI)
- **Multi-TAP Support**: Up to 8 TAPs in daisy chain
- **Error Detection**: CRC-8 and parity checking

### Testing Status ✅
- **SystemVerilog Tests**: 30/30 passing (18 JTAG + 12 System)
- **OpenOCD Integration**: 34/34 passing (19 JTAG + 15 cJTAG)
- **Total Test Coverage**: 64/64 tests passing
- **VPI Packet Parsing**: Fixed in v2.1 (1036-byte packets)

### Integration Status ✅
- **Verilator Simulation**: Fully operational
- **VPI Server**: Complete with protocol detection
- **OpenOCD Compatibility**: Standard jtag_vpi driver works
- **Optional Patches**: Available for enhanced cJTAG features

## 🛠️ Technical Specifications

### Protocols Supported
- **IEEE 1149.1**: Standard 4-wire JTAG
- **IEEE 1149.7**: 2-wire cJTAG with OScan1 protocol
- **RISC-V Debug Spec v0.13.2**: DMI interface compliance

### Performance Characteristics
- **Simulation Speed**: ~200K cycles/sec (Verilator)
- **VPI Throughput**: 1-2 Mbps practical bandwidth
- **TAP Clock**: Up to 100 MHz (typical FPGA implementation)
- **Resource Usage**: ~500 LUTs, ~100 FFs (typical FPGA)

### Supported Tools
- **Verilator**: v5.0+ for HDL simulation
- **OpenOCD**: Standard jtag_vpi adapter support
- **GTKWave**: FST/VCD waveform viewing
- **OSS CAD Suite**: Synthesis support (ASAP7 7nm PDK)

## 📋 Documentation Standards

### Format Guidelines
- **Markdown**: All documentation uses GitHub-flavored Markdown
- **Code Blocks**: SystemVerilog syntax highlighting
- **Diagrams**: ASCII art for timing and architecture diagrams
- **Cross-References**: Extensive linking between documents

### Update Policy
- **Version Tracking**: Documentation updated with code changes
- **Test Alignment**: Test documentation reflects actual implementation
- **Status Indicators**: Clear success/failure indicators (✅/❌)

## 🔗 Related Resources

### Project Root Documentation
- [../README.md](../README.md) - Main project overview and features
- [../QUICKSTART.md](../QUICKSTART.md) - 5-minute getting started guide
- [../Makefile](../Makefile) - Complete build system documentation

### External References
- [IEEE 1149.1-2013](https://standards.ieee.org/ieee/1149.1/4816/) - JTAG Standard
- [IEEE 1149.7-2009](https://standards.ieee.org/ieee/1149.7/4225/) - cJTAG Standard
- [RISC-V Debug Specification v0.13.2](https://riscv.org/technical/specifications/)
- [OpenOCD Documentation](https://openocd.org/doc/)

---

**Last Updated**: January 15, 2026  
**Documentation Version**: 2.1  
**Project Status**: Production Ready ✅  
**Total Documentation**: 11 comprehensive guides