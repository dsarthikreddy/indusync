`timescale 1ns/1ps
// One buzzer, different sounds, so people know what happened without a screen.
// Priority: siren (fire/gas) > alarm (security) > warn (door open) > deny/ok beeps.
module buzzer
  import smart_room_pkg::*;
(
  input  logic       clk,
  input  logic       rst_n,
  input  logic       beat,
  input  logic       ok,
  input  logic       deny,
  input  logic       warn,
  input  logic       alarm,
  input  logic       siren,
  output logic       buzz,
  output logic [2:0] mode
);
  logic [3:0] phase;
  logic [3:0] shot_t;
  logic [2:0] shot;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      phase  <= '0;
      shot_t <= '0;
      shot   <= BZ_OFF;
    end else begin
      if (beat) phase <= (phase == 4'd9) ? 4'd0 : phase + 4'd1;

      if (ok) begin
        shot   <= BZ_OK;
        shot_t <= 4'd3;
      end else if (deny) begin
        shot   <= BZ_DENY;
        shot_t <= 4'd6;
      end else if (beat && shot_t != 4'd0) begin
        shot_t <= shot_t - 4'd1;
        if (shot_t == 4'd1) shot <= BZ_OFF;
      end
    end
  end

  always_comb begin
    if (siren) begin
      mode = BZ_SIREN;
      buzz = (phase < 4'd5);
    end else if (alarm) begin
      mode = BZ_ALARM;
      buzz = 1'b1;
    end else if (warn) begin
      mode = BZ_WARN;
      buzz = (phase == 4'd0);
    end else begin
      mode = shot;
      case (shot)
        BZ_OK:   buzz = 1'b1;
        BZ_DENY: buzz = (shot_t >= 4'd5) || (shot_t <= 4'd2);
        default: buzz = 1'b0;
      endcase
    end
  end
endmodule
