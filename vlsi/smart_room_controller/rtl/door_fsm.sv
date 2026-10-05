`timescale 1ns/1ps
// Door brain: card + face -> open for OPEN_S seconds, then lock by itself.
//  - unknown card, night (non-admin), room full (non-admin) -> refused
//  - MAX_TRIES wrong faces in a row -> door locks for LOCKOUT_S seconds
//    (re-tapping the card does NOT reset the wrong-face count)
//  - exit button always opens (nobody is ever trapped), even in lockout
//  - fire/gas (emergency) keeps the door unlocked
//  - door opened while locked -> "forced"; door open too long -> "held open"
module door_fsm #(
  parameter int FACE_WAIT_S = 10,
  parameter int OPEN_S      = 3,
  parameter int LOCKOUT_S   = 30,
  parameter int HELD_S      = 20,
  parameter int MAX_TRIES   = 3
)(
  input  logic clk,
  input  logic rst_n,
  input  logic tick,
  input  logic card_tap,
  input  logic card_known,
  input  logic card_admin,
  input  logic face_ok,
  input  logic face_bad,
  input  logic exit_press,
  input  logic door_closed,
  input  logic night,
  input  logic room_full,
  input  logic emergency,
  output logic unlock,
  output logic locked_out,
  output logic held_warn,
  output logic grant,
  output logic deny,
  output logic ev_unknown,
  output logic ev_full,
  output logic ev_night,
  output logic ev_lockout,
  output logic ev_forced,
  output logic ev_held
);
  typedef enum logic [1:0] {S_IDLE, S_WAIT_FACE, S_OPEN, S_LOCKOUT} state_t;

  state_t     state;
  logic [7:0] timer;
  logic [7:0] exit_t;
  logic [7:0] held_t;
  logic [1:0] tries;
  logic       door_was_open;
  logic       door_open;

  assign door_open  = ~door_closed;
  assign unlock     = (state == S_OPEN) || (exit_t != 8'd0) || emergency;
  assign locked_out = (state == S_LOCKOUT);

  // Main access state machine
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      state      <= S_IDLE;
      timer      <= '0;
      tries      <= '0;
      grant      <= 1'b0;
      deny       <= 1'b0;
      ev_unknown <= 1'b0;
      ev_full    <= 1'b0;
      ev_night   <= 1'b0;
      ev_lockout <= 1'b0;
    end else begin
      grant      <= 1'b0;
      deny       <= 1'b0;
      ev_unknown <= 1'b0;
      ev_full    <= 1'b0;
      ev_night   <= 1'b0;
      ev_lockout <= 1'b0;

      if (emergency) begin
        state <= S_IDLE;
        timer <= '0;
      end else begin
        case (state)
          S_IDLE: begin
            if (card_tap) begin
              if (!card_known) begin
                ev_unknown <= 1'b1;
                deny       <= 1'b1;
              end else if (night && !card_admin) begin
                ev_night <= 1'b1;
                deny     <= 1'b1;
              end else if (room_full && !card_admin) begin
                ev_full <= 1'b1;
                deny    <= 1'b1;
              end else begin
                state <= S_WAIT_FACE;
                timer <= 8'(FACE_WAIT_S);
              end
            end
          end

          S_WAIT_FACE: begin
            if (face_ok) begin
              state <= S_OPEN;
              timer <= 8'(OPEN_S);
              tries <= '0;
              grant <= 1'b1;
            end else if (face_bad) begin
              deny <= 1'b1;
              if (tries == 2'(MAX_TRIES - 1)) begin
                state      <= S_LOCKOUT;
                timer      <= 8'(LOCKOUT_S);
                tries      <= '0;
                ev_lockout <= 1'b1;
              end else begin
                tries <= tries + 2'd1;
              end
            end else if (tick) begin
              if (timer <= 8'd1) begin
                state <= S_IDLE;
                timer <= '0;
              end else begin
                timer <= timer - 8'd1;
              end
            end
          end

          S_OPEN: begin
            if (tick) begin
              if (timer <= 8'd1) begin
                state <= S_IDLE;
                timer <= '0;
              end else begin
                timer <= timer - 8'd1;
              end
            end
          end

          S_LOCKOUT: begin
            if (card_tap) deny <= 1'b1;
            if (tick) begin
              if (timer <= 8'd1) begin
                state <= S_IDLE;
                timer <= '0;
              end else begin
                timer <= timer - 8'd1;
              end
            end
          end

          default: state <= S_IDLE;
        endcase
      end
    end
  end

  // Exit button timer, forced-door and held-open watch
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      exit_t        <= '0;
      held_t        <= '0;
      held_warn     <= 1'b0;
      door_was_open <= 1'b0;
      ev_forced     <= 1'b0;
      ev_held       <= 1'b0;
    end else begin
      door_was_open <= door_open;
      ev_forced     <= door_open && !door_was_open && !unlock;
      ev_held       <= 1'b0;

      if (exit_press)
        exit_t <= 8'(OPEN_S);
      else if (tick && exit_t != 8'd0)
        exit_t <= exit_t - 8'd1;

      if (!door_open || emergency) begin
        held_t    <= '0;
        held_warn <= 1'b0;
      end else if (tick && !held_warn) begin
        if (held_t == 8'(HELD_S - 1)) begin
          held_warn <= 1'b1;
          ev_held   <= 1'b1;
        end else begin
          held_t <= held_t + 8'd1;
        end
      end
    end
  end
endmodule
