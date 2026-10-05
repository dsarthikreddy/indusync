`timescale 1ns/1ps
// Shared codes for the Smart Room Controller.
package smart_room_pkg;
  // Event codes sent to the ESP32 / cloud. Higher number = more urgent (sent first).
  localparam logic [3:0] EV_ENTRY      = 4'd1;
  localparam logic [3:0] EV_LATE       = 4'd2;
  localparam logic [3:0] EV_DENY_FULL  = 4'd3;
  localparam logic [3:0] EV_DENY_NIGHT = 4'd4;
  localparam logic [3:0] EV_UNKNOWN    = 4'd5;
  localparam logic [3:0] EV_LOCKOUT    = 4'd6;
  localparam logic [3:0] EV_HELD_OPEN  = 4'd7;
  localparam logic [3:0] EV_ESP_DOWN   = 4'd8;
  localparam logic [3:0] EV_TAILGATE   = 4'd9;
  localparam logic [3:0] EV_FORCED     = 4'd10;
  localparam logic [3:0] EV_INTRUDER   = 4'd11;
  localparam logic [3:0] EV_EVAC_DONE  = 4'd12;
  localparam logic [3:0] EV_PANIC      = 4'd13;
  localparam logic [3:0] EV_GAS        = 4'd14;
  localparam logic [3:0] EV_FIRE       = 4'd15;

  // Buzzer sound patterns.
  localparam logic [2:0] BZ_OFF   = 3'd0; // silent
  localparam logic [2:0] BZ_OK    = 3'd1; // one short beep: welcome
  localparam logic [2:0] BZ_DENY  = 3'd2; // two short beeps: not allowed
  localparam logic [2:0] BZ_WARN  = 3'd3; // one beep every second: close the door
  localparam logic [2:0] BZ_ALARM = 3'd4; // continuous: security alarm
  localparam logic [2:0] BZ_SIREN = 3'd5; // on/off siren: fire or gas
endpackage
