`timescale 1ns/1ps
// Makes a 1-cycle "beat" pulse 10 times per second and a "tick" pulse once per second.
module tick_gen #(
  parameter int CLKS_PER_SEC = 100_000_000
)(
  input  logic clk,
  input  logic rst_n,
  output logic beat,
  output logic tick
);
  localparam int BEAT_CLKS = (CLKS_PER_SEC / 10 > 0) ? CLKS_PER_SEC / 10 : 1;
  localparam int BW        = (BEAT_CLKS > 1) ? $clog2(BEAT_CLKS) : 1;

  logic [BW-1:0] bc;
  logic [3:0]    beats;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      bc    <= '0;
      beats <= '0;
      beat  <= 1'b0;
      tick  <= 1'b0;
    end else begin
      beat <= 1'b0;
      tick <= 1'b0;
      if (bc == BW'(BEAT_CLKS - 1)) begin
        bc   <= '0;
        beat <= 1'b1;
        if (beats == 4'd9) begin
          beats <= '0;
          tick  <= 1'b1;
        end else begin
          beats <= beats + 4'd1;
        end
      end else begin
        bc <= bc + BW'(1);
      end
    end
  end
endmodule
