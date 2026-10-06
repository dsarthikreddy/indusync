`timescale 1ns/1ps
// Auto-converted from ../../rtl/*.sv (SystemVerilog) to plain Verilog-2005 for old Vivado (2014.x). Edit the .sv originals if you can.
module tick_gen (
	clk,
	rst_n,
	beat,
	tick
);
	parameter signed [31:0] CLKS_PER_SEC = 100000000;
	function integer clog2;
		input integer value;
		integer v;
		begin
			v = value - 1;
			for (clog2 = 0; v > 0; clog2 = clog2 + 1)
				v = v >> 1;
		end
	endfunction
	input wire clk;
	input wire rst_n;
	output reg beat;
	output reg tick;
	localparam signed [31:0] BEAT_CLKS = ((CLKS_PER_SEC / 10) > 0 ? CLKS_PER_SEC / 10 : 1);
	localparam signed [31:0] BW = (BEAT_CLKS > 1 ? clog2(BEAT_CLKS) : 1);
	reg [BW - 1:0] bc;
	reg [3:0] beats;
	function signed [BW - 1:0] sv2v_cast_F35F2_signed;
		input reg signed [BW - 1:0] inp;
		sv2v_cast_F35F2_signed = inp;
	endfunction
	always @(posedge clk or negedge rst_n)
		if (!rst_n) begin
			bc <= 1'b0;
			beats <= 1'b0;
			beat <= 1'b0;
			tick <= 1'b0;
		end
		else begin
			beat <= 1'b0;
			tick <= 1'b0;
			if (bc == sv2v_cast_F35F2_signed(BEAT_CLKS - 1)) begin
				bc <= 1'b0;
				beat <= 1'b1;
				if (beats == 4'd9) begin
					beats <= 1'b0;
					tick <= 1'b1;
				end
				else
					beats <= beats + 4'd1;
			end
			else
				bc <= bc + sv2v_cast_F35F2_signed(1);
		end
endmodule
