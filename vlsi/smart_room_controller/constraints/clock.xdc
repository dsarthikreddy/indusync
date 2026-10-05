# 100 MHz clock (10 ns). Lets Vivado report how fast the chip can run.
create_clock -period 10.000 -name clk [get_ports clk]
