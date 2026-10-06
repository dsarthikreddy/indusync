`timescale 1ns/1ps
// Auto-converted from ../../rtl/*.sv (SystemVerilog) to plain Verilog-2005 for old Vivado (2014.x). Edit the .sv originals if you can.
module door_fsm (
	clk,
	rst_n,
	tick,
	card_tap,
	card_known,
	card_admin,
	face_ok,
	face_bad,
	exit_press,
	door_closed,
	night,
	room_full,
	emergency,
	unlock,
	locked_out,
	held_warn,
	grant,
	deny,
	ev_unknown,
	ev_full,
	ev_night,
	ev_lockout,
	ev_forced,
	ev_held
);
	parameter signed [31:0] FACE_WAIT_S = 10;
	parameter signed [31:0] OPEN_S = 3;
	parameter signed [31:0] LOCKOUT_S = 30;
	parameter signed [31:0] HELD_S = 20;
	parameter signed [31:0] MAX_TRIES = 3;
	input wire clk;
	input wire rst_n;
	input wire tick;
	input wire card_tap;
	input wire card_known;
	input wire card_admin;
	input wire face_ok;
	input wire face_bad;
	input wire exit_press;
	input wire door_closed;
	input wire night;
	input wire room_full;
	input wire emergency;
	output wire unlock;
	output wire locked_out;
	output reg held_warn;
	output reg grant;
	output reg deny;
	output reg ev_unknown;
	output reg ev_full;
	output reg ev_night;
	output reg ev_lockout;
	output reg ev_forced;
	output reg ev_held;
	reg [1:0] state;
	reg [7:0] timer;
	reg [7:0] exit_t;
	reg [7:0] held_t;
	reg [1:0] tries;
	reg door_was_open;
	wire door_open;
	assign door_open = ~door_closed;
	assign unlock = ((state == 2'd2) || (exit_t != 8'd0)) || emergency;
	assign locked_out = state == 2'd3;
	function automatic signed [7:0] sv2v_cast_8_signed;
		input reg signed [7:0] inp;
		sv2v_cast_8_signed = inp;
	endfunction
	function automatic signed [1:0] sv2v_cast_2_signed;
		input reg signed [1:0] inp;
		sv2v_cast_2_signed = inp;
	endfunction
	always @(posedge clk or negedge rst_n)
		if (!rst_n) begin
			state <= 2'd0;
			timer <= 1'sb0;
			tries <= 1'sb0;
			grant <= 1'b0;
			deny <= 1'b0;
			ev_unknown <= 1'b0;
			ev_full <= 1'b0;
			ev_night <= 1'b0;
			ev_lockout <= 1'b0;
		end
		else begin
			grant <= 1'b0;
			deny <= 1'b0;
			ev_unknown <= 1'b0;
			ev_full <= 1'b0;
			ev_night <= 1'b0;
			ev_lockout <= 1'b0;
			if (emergency) begin
				state <= 2'd0;
				timer <= 1'sb0;
			end
			else
				case (state)
					2'd0:
						if (card_tap) begin
							if (!card_known) begin
								ev_unknown <= 1'b1;
								deny <= 1'b1;
							end
							else if (night && !card_admin) begin
								ev_night <= 1'b1;
								deny <= 1'b1;
							end
							else if (room_full && !card_admin) begin
								ev_full <= 1'b1;
								deny <= 1'b1;
							end
							else begin
								state <= 2'd1;
								timer <= sv2v_cast_8_signed(FACE_WAIT_S);
							end
						end
					2'd1:
						if (face_ok) begin
							state <= 2'd2;
							timer <= sv2v_cast_8_signed(OPEN_S);
							tries <= 1'sb0;
							grant <= 1'b1;
						end
						else if (face_bad) begin
							deny <= 1'b1;
							if (tries == sv2v_cast_2_signed(MAX_TRIES - 1)) begin
								state <= 2'd3;
								timer <= sv2v_cast_8_signed(LOCKOUT_S);
								tries <= 1'sb0;
								ev_lockout <= 1'b1;
							end
							else
								tries <= tries + 2'd1;
						end
						else if (tick) begin
							if (timer <= 8'd1) begin
								state <= 2'd0;
								timer <= 1'sb0;
							end
							else
								timer <= timer - 8'd1;
						end
					2'd2:
						if (tick) begin
							if (timer <= 8'd1) begin
								state <= 2'd0;
								timer <= 1'sb0;
							end
							else
								timer <= timer - 8'd1;
						end
					2'd3: begin
						if (card_tap)
							deny <= 1'b1;
						if (tick) begin
							if (timer <= 8'd1) begin
								state <= 2'd0;
								timer <= 1'sb0;
							end
							else
								timer <= timer - 8'd1;
						end
					end
					default: state <= 2'd0;
				endcase
		end
	always @(posedge clk or negedge rst_n)
		if (!rst_n) begin
			exit_t <= 1'sb0;
			held_t <= 1'sb0;
			held_warn <= 1'b0;
			door_was_open <= 1'b0;
			ev_forced <= 1'b0;
			ev_held <= 1'b0;
		end
		else begin
			door_was_open <= door_open;
			ev_forced <= (door_open && !door_was_open) && !unlock;
			ev_held <= 1'b0;
			if (exit_press)
				exit_t <= sv2v_cast_8_signed(OPEN_S);
			else if (tick && (exit_t != 8'd0))
				exit_t <= exit_t - 8'd1;
			if (!door_open || emergency) begin
				held_t <= 1'sb0;
				held_warn <= 1'b0;
			end
			else if (tick && !held_warn) begin
				if (held_t == sv2v_cast_8_signed(HELD_S - 1)) begin
					held_warn <= 1'b1;
					ev_held <= 1'b1;
				end
				else
					held_t <= held_t + 8'd1;
			end
		end
endmodule
