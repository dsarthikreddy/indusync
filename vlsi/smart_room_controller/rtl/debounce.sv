`timescale 1ns/1ps
// Glitch filter. The output only changes after the input has stayed at the
// new value for N "en" pulses in a row. A shorter glitch is ignored.
//   en = every clock -> filters switch bounce (milliseconds)
//   en = 1 s tick    -> filters sensor glitches (seconds), e.g. false smoke alarms
module debounce #(
  parameter int N    = 4,
  parameter bit INIT = 1'b0
)(
  input  logic clk,
  input  logic rst_n,
  input  logic en,
  input  logic din,
  output logic dout
);
  localparam int W = (N > 1) ? $clog2(N) : 1;

  logic         s1, s2;
  logic [W-1:0] cnt;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      s1   <= INIT;
      s2   <= INIT;
      dout <= INIT;
      cnt  <= '0;
    end else begin
      s1 <= din;
      s2 <= s1;
      if (s2 == dout) begin
        cnt <= '0;
      end else if (en) begin
        if (cnt == W'(N - 1)) begin
          dout <= s2;
          cnt  <= '0;
        end else begin
          cnt <= cnt + W'(1);
        end
      end
    end
  end
endmodule
