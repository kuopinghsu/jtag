# Yosys synthesis script for Debug Module
# Target: ASAP7 PDK

# Read design files
read_verilog -sv ../src/jtag/jtag_dmi_pkg.sv
read_verilog -sv ../src/dbg/riscv_debug_module.sv

# Elaborate design
hierarchy -check -top riscv_debug_module

# High-level synthesis
proc
opt
fsm
opt
memory
opt

# Technology mapping
techmap
opt

# Map flip-flops
dfflibmap -liberty pdk/asap7sc7p5t_SEQ_RVT_TT_nldm_201020.lib

# Map combinational logic - use simpler command to avoid ABC crashes
abc -fast -liberty pdk/asap7sc7p5t_SIMPLE_RVT_TT_nldm_201020.lib

# Cleanup
clean

# Write outputs
write_verilog -noattr results/riscv_debug_module_synth.v
write_json results/riscv_debug_module_synth.json

# Print statistics
stat -liberty pdk/asap7sc7p5t_SIMPLE_RVT_TT_nldm_201020.lib

# Save log
tee -o results/debug_module_synth.log
