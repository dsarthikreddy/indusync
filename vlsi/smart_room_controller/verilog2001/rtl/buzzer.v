`timescale 1ns/1ps
// Auto-converted from ../../rtl/*.sv (SystemVerilog) to plain Verilog-2005 for old Vivado (2014.x). Edit the .sv originals if you can.
module buzzer (
	clk,
	rst_n,
	beat,
	ok,
	deny,
	warn,
	alarm,
	siren,
	buzz,
	mode
);
	reg _sv2v_0;
	input wire clk;
	input wire rst_n;
	input wire beat;
	input wire ok;
	input wire deny;
	input wire warn;
	input wire alarm;
	input wire siren;
	output reg buzz;
	output reg [2:0] mode;
	reg [3:0] phase;
	reg [3:0] shot_t;
	reg [2:0] shot;
	localparam [2:0] smart_room_pkg_BZ_DENY = 3'd2;
	localparam [2:0] smart_room_pkg_BZ_OFF = 3'd0;
	localparam [2:0] smart_room_pkg_BZ_OK = 3'd1;
	always @(posedge clk or negedge rst_n)
		if (!rst_n) begin
			phase <= 1'sb0;
			shot_t <= 1'sb0;
			shot <= smart_room_pkg_BZ_OFF;
		end
		else begin
			if (beat)
				phase <= (phase == 4'd9 ? 4'd0 : phase + 4'd1);
			if (ok) begin
				shot <= smart_room_pkg_BZ_OK;
				shot_t <= 4'd3;
			end
			else if (deny) begin
				shot <= smart_room_pkg_BZ_DENY;
				shot_t <= 4'd6;
			end
			else if (beat && (shot_t != 4'd0)) begin
				shot_t <= shot_t - 4'd1;
				if (shot_t == 4'd1)
					shot <= smart_room_pkg_BZ_OFF;
			end
		end
	localparam [2:0] smart_room_pkg_BZ_ALARM = 3'd4;
	localparam [2:0] smart_room_pkg_BZ_SIREN = 3'd5;
	localparam [2:0] smart_room_pkg_BZ_WARN = 3'd3;
	always @(*) begin
		if (_sv2v_0)
			;
		if (siren) begin
			mode = smart_room_pkg_BZ_SIREN;
			buzz = phase < 4'd5;
		end
		else if (alarm) begin
			mode = smart_room_pkg_BZ_ALARM;
			buzz = 1'b1;
		end
		else if (warn) begin
			mode = smart_room_pkg_BZ_WARN;
			buzz = phase == 4'd0;
		end
		else begin
			mode = shot;
			case (shot)
				smart_room_pkg_BZ_OK: buzz = 1'b1;
				smart_room_pkg_BZ_DENY: buzz = (shot_t >= 4'd5) || (shot_t <= 4'd2);
				default: buzz = 1'b0;
			endcase
		end
	end
	initial _sv2v_0 = 0;
endmodule
