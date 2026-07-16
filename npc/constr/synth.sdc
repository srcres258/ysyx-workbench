# NPC Synthesis SDC — ysyx_25070190 (standalone CPU core)
#
# Clock port:   clock  (top-level port name from RTL)
# Reset:        reset  (active-high, synchronous de-assertion preferred)
#
# Variables are read from environment:
#   CLK_PORT_NAME  – clock port name  (default: clock)
#   CLK_FREQ_MHZ   – target frequency in MHz (default: 100)
#
# Usage:  CLK_PORT_NAME=clock CLK_FREQ_MHZ=100  yosys-sta ...  -sdc this_file

set CLK_PORT_NAME clock
if {[info exists env(CLK_PORT_NAME)]} {
  set CLK_PORT_NAME $::env(CLK_PORT_NAME)
} else {
  puts "Warning: CLK_PORT_NAME not set in environment, using '$CLK_PORT_NAME'"
}

set CLK_FREQ_MHZ 100
if {[info exists env(CLK_FREQ_MHZ)]} {
  set CLK_FREQ_MHZ $::env(CLK_FREQ_MHZ)
} else {
  puts "Warning: CLK_FREQ_MHZ not set in environment, using ${CLK_FREQ_MHZ} MHz"
}

# ── Clock definition ──────────────────────────────────────────────
set clk_period_ns [expr 1000.0 / $CLK_FREQ_MHZ]
set clk_port      [get_ports $CLK_PORT_NAME]
create_clock -name core_clock -period $clk_period_ns $clk_port

# ── Reset (active-high, treated as false path for simplicity) ─────
# The reset input is not a clock; it is a slow-changing control signal.
# If the design uses synchronous reset, add a set_input_delay constraint.
# For async reset, this is sufficient.

# ── Input / Output delays (conservative defaults) ─────────────────
# Assume 30% of clock period for external logic + board delay
set io_delay_ns [expr $clk_period_ns * 0.3]

# Apply to all non-clock, non-reset ports
set all_inputs  [remove_from_collection [all_inputs]  $clk_port]
set all_outputs [all_outputs]

if {[sizeof_collection $all_inputs] > 0} {
  set_input_delay  -clock core_clock -max $io_delay_ns $all_inputs
  set_input_delay  -clock core_clock -min 0.0           $all_inputs
}
if {[sizeof_collection $all_outputs] > 0} {
  set_output_delay -clock core_clock -max $io_delay_ns $all_outputs
  set_output_delay -clock core_clock -min 0.0           $all_outputs
}

# ── Clock uncertainty (jitter + skew margin) ─────────────────────
set_clock_uncertainty -setup 0.20 [get_clocks core_clock]
set_clock_uncertainty -hold  0.05 [get_clocks core_clock]
