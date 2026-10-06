`timescale 1ns/1ps
// Auto-converted from ../../rtl/*.sv (SystemVerilog) to plain Verilog-2005 for old Vivado (2014.x). Edit the .sv originals if you can.
module room_comfort (
	clk,
	rst_n,
	tick,
	occupied,
	motion,
	fire,
	gas,
	temp_c,
	air_q,
	lights_on,
	fan_speed,
	vent_on
);
	parameter signed [31:0] EMPTY_OFF_S = 60;
	parameter signed [31:0] T1 = 28;
	parameter signed [31:0] T2 = 32;
	parameter signed [31:0] T3 = 36;
	parameter signed [31:0] AIR_ON = 150;
	parameter signed [31:0] AIR_OFF = 120;
	input wire clk;
	input wire rst_n;
	input wire tick;
	input wire occupied;
	input wire motion;
	input wire fire;
	input wire gas;
	input wire [7:0] temp_c;
	input wire [7:0] air_q;
	output wire lights_on;
	output reg [1:0] fan_speed;
	output wire vent_on;
	reg [7:0] idle_t;
	reg air_bad;
	wire active;
	wire [1:0] up_lvl;
	wire [1:0] dn_lvl;
	function signed [8:0] sv2v_cast_9_signed;
		input reg signed [8:0] inp;
		sv2v_cast_9_signed = inp;
	endfunction
	function [1:0] level;
		input reg [8:0] t;
		if (t >= sv2v_cast_9_signed(T3))
			level = 2'd3;
		else if (t >= sv2v_cast_9_signed(T2))
			level = 2'd2;
		else if (t >= sv2v_cast_9_signed(T1))
			level = 2'd1;
		else
			level = 2'd0;
	endfunction
	assign up_lvl = level({1'b0, temp_c});
	assign dn_lvl = level({1'b0, temp_c} + 9'd1);
	assign active = idle_t != 8'd0;
	assign lights_on = fire || active;
	assign vent_on = !fire && (gas || air_bad);
	function signed [7:0] sv2v_cast_8_signed;
		input reg signed [7:0] inp;
		sv2v_cast_8_signed = inp;
	endfunction
	always @(posedge clk or negedge rst_n)
		if (!rst_n) begin
			idle_t <= 1'b0;
			air_bad <= 1'b0;
			fan_speed <= 2'd0;
		end
		else begin
			if (occupied || motion)
				idle_t <= sv2v_cast_8_signed(EMPTY_OFF_S);
			else if (tick && (idle_t != 8'd0))
				idle_t <= idle_t - 8'd1;
			if (air_q >= sv2v_cast_8_signed(AIR_ON))
				air_bad <= 1'b1;
			else if (air_q < sv2v_cast_8_signed(AIR_OFF))
				air_bad <= 1'b0;
			if ((!active || fire) || gas)
				fan_speed <= 2'd0;
			else if (up_lvl > fan_speed)
				fan_speed <= up_lvl;
			else if (dn_lvl < fan_speed)
				fan_speed <= dn_lvl;
		end
endmodule
