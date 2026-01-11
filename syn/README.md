# ASAP7 Synthesis with OSS CAD Suite

This directory contains the synthesis setup for JTAG/cJTAG design using OSS CAD Suite and ASAP7 PDK.

## Prerequisites

- OSS CAD Suite installed at `/opt/oss-cad-suite`
- ASAP7 PDK liberty files in `pdk/` directory

## Quick Start

```bash
# From project root:
make synth-jtag      # Synthesize JTAG top module
make synth-dbg       # Synthesize Debug Module  
make synth-system    # Synthesize System Top
make synth           # Synthesize all modules
make synth-reports   # Generate area/timing reports
```

## Directory Structure

```
syn/
├── pdk/                                    # ASAP7 PDK liberty files
│   ├── asap7sc7p5t_SEQ_RVT_TT_nldm_201020.lib     # Sequential cells
│   └── asap7sc7p5t_SIMPLE_RVT_TT_nldm_201020.lib  # Combinational cells
├── scripts/                                # Synthesis TCL scripts
│   ├── synth_jtag.tcl                     # JTAG synthesis script
│   ├── synth_debug.tcl                    # Debug module synthesis script
│   └── synth_system.tcl                   # System top synthesis script
├── results/                                # Synthesis outputs (generated)
│   ├── *_synth.v                          # Gate-level netlists
│   ├── *_synth.json                       # JSON representations
│   └── *_synthesis.log                    # Synthesis logs
├── reports/                                # Synthesis reports (generated)
│   └── *_stats.rpt                        # Area/timing statistics
└── README.md                              # This file
```

## Synthesis Flow

The synthesis uses Yosys with the following steps:

1. **Read Design** - Parse SystemVerilog files
2. **Elaborate** - Build design hierarchy 
3. **Proc** - Process behavioral code
4. **Opt** - Optimize logic
5. **FSM** - Optimize finite state machines
6. **Memory** - Optimize memory structures
7. **Techmap** - Technology mapping
8. **DFF Mapping** - Map flip-flops to ASAP7 sequential cells
9. **ABC** - Logic optimization and mapping to ASAP7 combinational cells
10. **Write Netlist** - Generate synthesized Verilog and JSON

## Output Files

After synthesis, the following files are generated:

### Synthesis Results (`results/` directory)
- `*_synth.v` - Gate-level Verilog netlist
- `*_synth.json` - JSON representation for visualization
- `*_synth.log` - Detailed synthesis statistics
- `*_synthesis.log` - Complete synthesis output with all messages

### Reports (`reports/` directory)
- `*_stats.rpt` - Area and cell count statistics

All synthesis outputs can be cleaned with `make clean` or `make synth-clean`.

## Known Issues

### SystemVerilog Compatibility

Yosys has limited SystemVerilog support. The following constructs may require manual modification:

1. **Package imports in module header** - Move `import` statements before `module` declaration
2. **Automatic functions** - Remove `automatic` keyword
3. **Return statements** - Use function name assignment instead (e.g., `func = value;` vs `return value;`)
4. **For loops in functions** - Manually unroll loops or use generate blocks

### Workarounds Applied

The source files have been modified for synthesis compatibility:

- `cjtag_crc_parity.sv` - Manually unrolled CRC computation loop
- `jtag_dtm.sv` - Moved package import outside module declaration

## Synthesis Reports

Run `make synth-reports` to generate detailed reports including:

- Cell count and types
- Estimated area
- Critical path timing
- Power consumption estimates

Reports are saved in `reports/` directory.

## ASAP7 PDK Information

- **Technology**: 7nm ASAP (Predictive PDK)
- **Voltage**: 0.7V nominal
- **Temperature**: 25°C (TT corner)
- **Cells**: RVT (Regular Threshold Voltage)

## Troubleshooting

### Synthesis Errors

1. Check `results/*_synthesis.log` for detailed error messages
2. Verify PDK files are present in `pdk/` directory
3. Ensure OSS CAD Suite is installed correctly: `which yosys`

### Cleaning Build Artifacts

```bash
make synth-clean    # Remove synthesis results and reports only
make clean          # Remove all build artifacts (including syn/results/)
```

### Missing OSS CAD Suite

If Yosys is not found, install OSS CAD Suite:

```bash
# macOS with Homebrew
brew install --cask oss-cad-suite

# Or download from: https://github.com/YosysHQ/oss-cad-suite-build/releases
```

## Future Enhancements

- [ ] Place & Route with OpenROAD
- [ ] Timing analysis with OpenSTA
- [ ] Power analysis
- [ ] Design visualization with netlistsvg
- [ ] Physical design with ASAP7 PDK

## References

- [Yosys Documentation](http://yosyshq.net/yosys/)
- [ASAP7 PDK](http://asap.asu.edu/asap/)
- [OSS CAD Suite](https://github.com/YosysHQ/oss-cad-suite-build)
