`timescale 1ns/1ps
// Auto-converted from ../../rtl/*.sv (SystemVerilog) to plain Verilog-2005 for old Vivado (2014.x). Edit the .sv originals if you can.
module people_counter (
	clk,
	rst_n,
	tick,
	beam_out,
	beam_in,
	grant,
	emergency,
	people,
	full,
	pass_in,
	pass_out,
	tailgate
);
	parameter signed [31:0] MAX_PEOPLE = 40;
	parameter signed [31:0] CREDIT_S = 10;
	input wire clk;
	input wire rst_n;
	input wire tick;
	input wire beam_out;
	input wire beam_in;
	input wire grant;
	input wire emergency;
	output reg [5:0] people;
	output wire full;
	output reg pass_in;
	output reg pass_out;
	output reg tailgate;
	reg [2:0] st;
	reg [2:0] credits;
	reg [7:0] credit_t;
	wire a;
	wire b;
	assign a = beam_out;
	assign b = beam_in;
	function signed [5:0] sv2v_cast_6_signed;
		input reg signed [5:0] inp;
		sv2v_cast_6_signed = inp;
	endfunction
	assign full = people >= sv2v_cast_6_signed(MAX_PEOPLE);
	always @(posedge clk or negedge rst_n)
		if (!rst_n) begin
			st <= 3'd0;
			pass_in <= 1'b0;
			pass_out <= 1'b0;
		end
		else begin
			pass_in <= 1'b0;
			pass_out <= 1'b0;
			case (st)
				3'd0:
					if (a && !b)
						st <= 3'd1;
					else if (b && !a)
						st <= 3'd4;
				3'd1:
					if (a && b)
						st <= 3'd2;
					else if (!a)
						st <= 3'd0;
				3'd2:
					if (!a && b)
						st <= 3'd3;
					else if (!a && !b)
						st <= 3'd0;
					else if (a && !b)
						st <= 3'd1;
				3'd3:
					if (!a && !b) begin
						st <= 3'd0;
						pass_in <= 1'b1;
					end
					else if (a)
						st <= 3'd2;
				3'd4:
					if (a && b)
						st <= 3'd5;
					else if (!b)
						st <= 3'd0;
				3'd5:
					if (a && !b)
						st <= 3'd6;
					else if (!a && !b)
						st <= 3'd0;
					else if (!a && b)
						st <= 3'd4;
				3'd6:
					if (!a && !b) begin
						st <= 3'd0;
						pass_out <= 1'b1;
					end
					else if (b)
						st <= 3'd5;
				default: st <= 3'd0;
			endcase
		end
	function signed [7:0] sv2v_cast_8_signed;
		input reg signed [7:0] inp;
		sv2v_cast_8_signed = inp;
	endfunction
	always @(posedge clk or negedge rst_n)
		if (!rst_n) begin
			people <= 1'b0;
			credits <= 1'b0;
			credit_t <= 1'b0;
			tailgate <= 1'b0;
		end
		else begin
			tailgate <= 1'b0;
			if (pass_in && (people != 6'd63))
				people <= people + 6'd1;
			else if (pass_out && (people != 6'd0))
				people <= people - 6'd1;
			case ({grant, pass_in})
				2'b10:
					if (credits != 3'd7)
						credits <= credits + 3'd1;
				2'b01:
					if (credits != 3'd0)
						credits <= credits - 3'd1;
					else if (!emergency)
						tailgate <= 1'b1;
				default:
					;
			endcase
			if (grant)
				credit_t <= sv2v_cast_8_signed(CREDIT_S);
			else if (tick && (credit_t != 8'd0)) begin
				credit_t <= credit_t - 8'd1;
				if (credit_t == 8'd1)
					credits <= 1'b0;
			end
		end
endmodule
