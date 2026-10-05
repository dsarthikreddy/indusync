`timescale 1ns/1ps
// Brings a signal from the ESP32 safely into the chip's clock (2 flip-flops)
// and gives a 1-cycle pulse when it goes from 0 to 1.
module sync_edge (
  input  logic clk,
  input  logic rst_n,
  input  logic din,
  output logic level,
  output logic rise
);
  logic s1, s2, s3;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      s1 <= 1'b0;
      s2 <= 1'b0;
      s3 <= 1'b0;
    end else begin
      s1 <= din;
      s2 <= s1;
      s3 <= s2;
    end
  end

  assign level = s2;
  assign rise  = s2 & ~s3;
endmodule
