# Yosys synthesis script for JTAG Top Module
# Target: ASAP7 PDK

# Read design files
read_verilog -sv ../src/jtag/jtag_dmi_pkg.sv
read_verilog -sv ../src/jtag/jtag_tap_pkg.sv
read_verilog -sv ../src/jtag/jtag_tap_controller.sv
read_verilog -sv ../src/jtag/jtag_instruction_register.sv
read_verilog -sv ../src/jtag/oscan1_controller.sv
read_verilog -sv ../src/jtag/cjtag_crc_parity.sv
read_verilog -sv ../src/jtag/jtag_interface.sv
read_verilog -sv ../src/jtag/jtag_dtm.sv
read_verilog -sv ../src/jtag/jtag_scan_chain.sv
read_verilog -sv ../src/jtag/jtag_top.sv

# Elaborate design
hierarchy -check -top jtag_top

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
write_verilog -noattr results/jtag_top_synth.v
write_json results/jtag_top_synth.json

# Print statistics
stat -liberty pdk/asap7sc7p5t_SIMPLE_RVT_TT_nldm_201020.lib

# Save log
tee -o results/jtag_top_synth.log
