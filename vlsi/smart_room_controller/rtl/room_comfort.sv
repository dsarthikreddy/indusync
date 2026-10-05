`timescale 1ns/1ps
// Lights, fan and fresh-air vent.
//  - lights ON while someone is inside or moving; OFF EMPTY_OFF_S s after the room empties
//  - fan speed 0..3 from temperature, with 1 degree hysteresis (no on/off flicker)
//  - fan OFF in fire (would spread smoke) and gas (motor spark)
//  - vent ON when air is stuffy (with hysteresis) or gas leaks; OFF in fire
module room_comfort #(
  parameter int EMPTY_OFF_S = 60,
  parameter int T1          = 28,
  parameter int T2          = 32,
  parameter int T3          = 36,
  parameter int AIR_ON      = 150,
  parameter int AIR_OFF     = 120
)(
  input  logic       clk,
  input  logic       rst_n,
  input  logic       tick,
  input  logic       occupied,
  input  logic       motion,
  input  logic       fire,
  input  logic       gas,
  input  logic [7:0] temp_c,
  input  logic [7:0] air_q,
  output logic       lights_on,
  output logic [1:0] fan_speed,
  output logic       vent_on
);
  logic [7:0] idle_t;
  logic       air_bad;
  logic       active;
  logic [1:0] up_lvl, dn_lvl;

  function automatic logic [1:0] level(input logic [8:0] t);
    if (t >= 9'(T3))      level = 2'd3;
    else if (t >= 9'(T2)) level = 2'd2;
    else if (t >= 9'(T1)) level = 2'd1;
    else                  level = 2'd0;
  endfunction

  assign up_lvl    = level({1'b0, temp_c});
  assign dn_lvl    = level({1'b0, temp_c} + 9'd1);
  assign active    = (idle_t != 8'd0);
  assign lights_on = fire || active;
  assign vent_on   = !fire && (gas || air_bad);

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      idle_t    <= '0;
      air_bad   <= 1'b0;
      fan_speed <= 2'd0;
    end else begin
      if (occupied || motion)
        idle_t <= 8'(EMPTY_OFF_S);
      else if (tick && idle_t != 8'd0)
        idle_t <= idle_t - 8'd1;

      if (air_q >= 8'(AIR_ON))      air_bad <= 1'b1;
      else if (air_q < 8'(AIR_OFF)) air_bad <= 1'b0;

      if (!active || fire || gas)  fan_speed <= 2'd0;
      else if (up_lvl > fan_speed) fan_speed <= up_lvl;
      else if (dn_lvl < fan_speed) fan_speed <= dn_lvl;
    end
  end
endmodule
