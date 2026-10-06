`timescale 1ns/1ps
// Auto-converted from ../../rtl/*.sv (SystemVerilog) to plain Verilog-2005 for old Vivado (2014.x). Edit the .sv originals if you can.
module sync_edge (
	clk,
	rst_n,
	din,
	level,
	rise
);
	input wire clk;
	input wire rst_n;
	input wire din;
	output wire level;
	output wire rise;
	reg s1;
	reg s2;
	reg s3;
	always @(posedge clk or negedge rst_n)
		if (!rst_n) begin
			s1 <= 1'b0;
			s2 <= 1'b0;
			s3 <= 1'b0;
		end
		else begin
			s1 <= din;
			s2 <= s1;
			s3 <= s2;
		end
	assign level = s2;
	assign rise = s2 & ~s3;
endmodule
