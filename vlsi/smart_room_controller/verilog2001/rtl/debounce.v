`timescale 1ns/1ps
// Auto-converted from ../../rtl/*.sv (SystemVerilog) to plain Verilog-2005 for old Vivado (2014.x). Edit the .sv originals if you can.
module debounce (
	clk,
	rst_n,
	en,
	din,
	dout
);
	parameter signed [31:0] N = 4;
	parameter [0:0] INIT = 1'b0;
	input wire clk;
	input wire rst_n;
	input wire en;
	input wire din;
	output reg dout;
	localparam signed [31:0] W = (N > 1 ? $clog2(N) : 1);
	reg s1;
	reg s2;
	reg [W - 1:0] cnt;
	function automatic signed [W - 1:0] sv2v_cast_E16C3_signed;
		input reg signed [W - 1:0] inp;
		sv2v_cast_E16C3_signed = inp;
	endfunction
	always @(posedge clk or negedge rst_n)
		if (!rst_n) begin
			s1 <= INIT;
			s2 <= INIT;
			dout <= INIT;
			cnt <= 1'sb0;
		end
		else begin
			s1 <= din;
			s2 <= s1;
			if (s2 == dout)
				cnt <= 1'sb0;
			else if (en) begin
				if (cnt == sv2v_cast_E16C3_signed(N - 1)) begin
					dout <= s2;
					cnt <= 1'sb0;
				end
				else
					cnt <= cnt + sv2v_cast_E16C3_signed(1);
			end
		end
endmodule
