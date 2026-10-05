`timescale 1ns/1ps
// Counts people with two light beams in the doorway:
//   beam_out (outside) then beam_in (inside) = one person IN
//   beam_in then beam_out                     = one person OUT
// Each approved card+face gives one "entry ticket" (valid CREDIT_S seconds).
// A person walking in without a ticket = TAILGATING.
module people_counter #(
  parameter int MAX_PEOPLE = 40,
  parameter int CREDIT_S   = 10
)(
  input  logic       clk,
  input  logic       rst_n,
  input  logic       tick,
  input  logic       beam_out,
  input  logic       beam_in,
  input  logic       grant,
  input  logic       emergency,
  output logic [5:0] people,
  output logic       full,
  output logic       pass_in,
  output logic       pass_out,
  output logic       tailgate
);
  typedef enum logic [2:0] {IDLE, IN1, IN2, IN3, OUT1, OUT2, OUT3} st_t;

  st_t        st;
  logic [2:0] credits;
  logic [7:0] credit_t;
  logic       a, b;

  assign a    = beam_out;
  assign b    = beam_in;
  assign full = (people >= 6'(MAX_PEOPLE));

  // Direction detector
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      st       <= IDLE;
      pass_in  <= 1'b0;
      pass_out <= 1'b0;
    end else begin
      pass_in  <= 1'b0;
      pass_out <= 1'b0;
      case (st)
        IDLE: if (a && !b) st <= IN1;  else if (b && !a) st <= OUT1;
        IN1:  if (a && b)  st <= IN2;  else if (!a)      st <= IDLE;
        IN2:  if (!a && b) st <= IN3;  else if (!a && !b) st <= IDLE; else if (a && !b) st <= IN1;
        IN3:  if (!a && !b) begin st <= IDLE; pass_in <= 1'b1; end else if (a) st <= IN2;
        OUT1: if (a && b)  st <= OUT2; else if (!b)      st <= IDLE;
        OUT2: if (a && !b) st <= OUT3; else if (!a && !b) st <= IDLE; else if (!a && b) st <= OUT1;
        OUT3: if (!a && !b) begin st <= IDLE; pass_out <= 1'b1; end else if (b) st <= OUT2;
        default: st <= IDLE;
      endcase
    end
  end

  // Head count + entry tickets + tailgating
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      people   <= '0;
      credits  <= '0;
      credit_t <= '0;
      tailgate <= 1'b0;
    end else begin
      tailgate <= 1'b0;

      if (pass_in && people != 6'd63)
        people <= people + 6'd1;
      else if (pass_out && people != 6'd0)
        people <= people - 6'd1;

      case ({grant, pass_in})
        2'b10: if (credits != 3'd7) credits <= credits + 3'd1;
        2'b01: begin
          if (credits != 3'd0) credits <= credits - 3'd1;
          else if (!emergency)  tailgate <= 1'b1;
        end
        default: ;
      endcase

      if (grant) begin
        credit_t <= 8'(CREDIT_S);
      end else if (tick && credit_t != 8'd0) begin
        credit_t <= credit_t - 8'd1;
        if (credit_t == 8'd1) credits <= '0;
      end
    end
  end
endmodule
