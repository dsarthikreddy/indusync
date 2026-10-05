`timescale 1ns/1ps
// Event memory (FIFO) between the chip and the ESP32/cloud.
//  - events are stored with time and head count, oldest first
//  - when Wi-Fi is down they wait here; nothing is lost until it is full
//  - if several events happen together, the most urgent is stored first
//  - when full, new events are counted in ev_lost (so loss is never silent)
// Event word (24 bit) = {code[3:0], hour[4:0], minute[5:0], people[5:0], 3'b000}
module event_logger #(
  parameter int DEPTH = 16
)(
  input  logic                    clk,
  input  logic                    rst_n,
  input  logic [15:0]             ev_req,
  input  logic [4:0]              hour,
  input  logic [5:0]              minute,
  input  logic [5:0]              people,
  input  logic                    ev_ack,
  output logic                    ev_valid,
  output logic [23:0]             ev_data,
  output logic [$clog2(DEPTH):0]  ev_count,
  output logic [7:0]              ev_lost
);
  localparam int AW = $clog2(DEPTH);
  localparam logic [AW:0] FULL_CNT = DEPTH[AW:0];
  localparam logic [AW:0] ONE_CNT  = {{AW{1'b0}}, 1'b1};

  logic [23:0]   mem [DEPTH];
  logic [AW-1:0] wp, rp;
  logic [15:1]   pend;
  logic [15:1]   clr;
  logic [3:0]    sel;
  logic          any;
  logic          do_pop, do_push;

  always_comb begin
    sel = 4'd0;
    any = 1'b0;
    for (int i = 1; i < 16; i++) begin
      if (pend[i]) begin
        sel = 4'(i);
        any = 1'b1;
      end
    end
  end

  always_comb begin
    clr = '0;
    for (int i = 1; i < 16; i++) clr[i] = any && (sel == 4'(i));
  end

  assign do_pop   = ev_ack && (ev_count != '0);
  assign do_push  = any && ((ev_count != FULL_CNT) || do_pop);
  assign ev_valid = (ev_count != '0);
  assign ev_data  = mem[rp];

  always_ff @(posedge clk) begin
    if (do_push) mem[wp] <= {sel, hour, minute, people, 3'b000};
  end

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      wp       <= '0;
      rp       <= '0;
      ev_count <= '0;
      ev_lost  <= '0;
      pend     <= '0;
    end else begin
      pend <= (pend & ~clr) | ev_req[15:1];
      if (do_push) wp <= wp + AW'(1);
      if (do_pop)  rp <= rp + AW'(1);
      case ({do_push, do_pop})
        2'b10:   ev_count <= ev_count + ONE_CNT;
        2'b01:   ev_count <= ev_count - ONE_CNT;
        default: ;
      endcase
      if (any && !do_push && ev_lost != 8'hFF) ev_lost <= ev_lost + 8'd1;
    end
  end

  logic unused_ok;
  assign unused_ok = ev_req[0];
endmodule
