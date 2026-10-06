`timescale 1ns/1ps
// Auto-converted from ../../rtl/*.sv (SystemVerilog) to plain Verilog-2005 for old Vivado (2014.x). Edit the .sv originals if you can.
module event_logger (
	clk,
	rst_n,
	ev_req,
	hour,
	minute,
	people,
	ev_ack,
	ev_valid,
	ev_data,
	ev_count,
	ev_lost
);
	parameter signed [31:0] DEPTH = 16;
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
	input wire [15:0] ev_req;
	input wire [4:0] hour;
	input wire [5:0] minute;
	input wire [5:0] people;
	input wire ev_ack;
	output wire ev_valid;
	output wire [23:0] ev_data;
	output reg [clog2(DEPTH):0] ev_count;
	output reg [7:0] ev_lost;
	localparam signed [31:0] AW = clog2(DEPTH);
	localparam [AW:0] FULL_CNT = DEPTH;
	localparam [AW:0] ONE_CNT = {{AW {1'b0}}, 1'b1};
	reg [23:0] mem [0:DEPTH - 1];
	reg [AW - 1:0] wp;
	reg [AW - 1:0] rp;
	reg [15:1] pend;
	reg [15:1] clr;
	reg [3:0] sel;
	reg any;
	wire do_pop;
	wire do_push;
	function signed [3:0] sv2v_cast_4_signed;
		input reg signed [3:0] inp;
		sv2v_cast_4_signed = inp;
	endfunction
	always @(*) begin
		sel = 4'd0;
		any = 1'b0;
		begin : sv2v_autoblock_1
			reg signed [31:0] i;
			for (i = 1; i < 16; i = i + 1)
				if (pend[i]) begin
					sel = sv2v_cast_4_signed(i);
					any = 1'b1;
				end
		end
	end
	always @(*) begin
		clr = 1'b0;
		begin : sv2v_autoblock_2
			reg signed [31:0] i;
			for (i = 1; i < 16; i = i + 1)
				clr[i] = any && (sel == sv2v_cast_4_signed(i));
		end
	end
	assign do_pop = ev_ack && (ev_count != {(AW + 1) {1'b0}});
	assign do_push = any && ((ev_count != FULL_CNT) || do_pop);
	assign ev_valid = ev_count != {(AW + 1) {1'b0}};
	assign ev_data = mem[rp];
	always @(posedge clk)
		if (do_push)
			mem[wp] <= {sel, hour, minute, people, 3'b000};
	function signed [AW - 1:0] sv2v_cast_DE851_signed;
		input reg signed [AW - 1:0] inp;
		sv2v_cast_DE851_signed = inp;
	endfunction
	always @(posedge clk or negedge rst_n)
		if (!rst_n) begin
			wp <= 1'b0;
			rp <= 1'b0;
			ev_count <= 1'b0;
			ev_lost <= 1'b0;
			pend <= 1'b0;
		end
		else begin
			pend <= (pend & ~clr) | ev_req[15:1];
			if (do_push)
				wp <= wp + sv2v_cast_DE851_signed(1);
			if (do_pop)
				rp <= rp + sv2v_cast_DE851_signed(1);
			case ({do_push, do_pop})
				2'b10: ev_count <= ev_count + ONE_CNT;
				2'b01: ev_count <= ev_count - ONE_CNT;
				default:
					;
			endcase
			if ((any && !do_push) && (ev_lost != 8'hff))
				ev_lost <= ev_lost + 8'd1;
		end
	wire unused_ok;
	assign unused_ok = ev_req[0];
endmodule
