`timescale 1ns/1ps
// SMART ROOM CONTROLLER - top level.
// The ESP32 keeps doing face recognition, Wi-Fi and cloud. This chip does the
// safety and room rules in hardware, so they never wait, crash or run out of memory.
module smart_room_top
  import smart_room_pkg::*;
#(
  parameter int CLKS_PER_SEC    = 100_000_000,  // 100 MHz clock
  parameter int MAX_PEOPLE      = 40,
  parameter int CLASS_START_MIN = 9 * 60,       // 09:00
  parameter int LATE_GRACE_MIN  = 10,           // late after 09:10
  parameter int CLASS_END_MIN   = 17 * 60,      // 17:00
  parameter int NIGHT_START_H   = 22,           // night lock 22:00 ...
  parameter int NIGHT_END_H     = 6,            // ... to 06:00
  parameter int SMOKE_CONFIRM_S = 3,            // smoke/gas must last 3 s
  parameter int ESP_TIMEOUT_S   = 10,           // ESP32 silent 10 s = fault
  parameter int EMPTY_OFF_S     = 60,           // lights/fan off 60 s after room empties
  parameter int EVENT_DEPTH     = 16
)(
  input  logic        clk,
  input  logic        rst_n,
  // from ESP32 (it reads the card and checks the face)
  input  logic        card_tap,
  input  logic        card_known,
  input  logic        card_admin,
  input  logic        face_ok,
  input  logic        face_bad,
  input  logic        esp_heartbeat,
  input  logic [4:0]  hour,
  input  logic [5:0]  minute,
  // buttons and sensors
  input  logic        exit_btn,
  input  logic        panic_btn,
  input  logic        door_closed,
  input  logic        beam_out,
  input  logic        beam_in,
  input  logic        motion,
  input  logic        smoke,
  input  logic        gas,
  input  logic [7:0]  temp_c,
  input  logic [7:0]  air_q,
  // ESP32 says "I have read the event"
  input  logic        ev_ack,
  // room outputs
  output logic        door_unlock,
  output logic        buzzer,
  output logic [2:0]  buzz_mode,
  output logic        lights_on,
  output logic [1:0]  fan_speed,
  output logic        vent_on,
  // status
  output logic        fire_mode,
  output logic        gas_mode,
  output logic        evac_done,
  output logic        night_mode,
  output logic        security_alarm,
  output logic [3:0]  alarm_flags,     // {panic, intruder, tailgate, forced}
  output logic        locked_out,
  output logic        held_warn,
  output logic        room_full,
  output logic [5:0]  people,
  output logic        esp_fault,
  output logic [7:0]  present_today,
  output logic [7:0]  late_today,
  // events to ESP32/cloud
  output logic        ev_valid,
  output logic [23:0] ev_data,
  output logic [$clog2(EVENT_DEPTH):0] ev_count,
  output logic [7:0]  ev_lost
);
  localparam int DEB_N = (CLKS_PER_SEC / 50 > 0) ? CLKS_PER_SEC / 50 : 1; // 20 ms

  // ---------------- timing ----------------
  logic tick, beat;
  tick_gen #(.CLKS_PER_SEC(CLKS_PER_SEC)) u_tick (
    .clk(clk), .rst_n(rst_n), .beat(beat), .tick(tick));

  // ---------------- ESP32 signals ----------------
  logic tap_r, fok_r, fbad_r, hb_r, ack_r;
  logic known_l, admin_l;
  logic tap_l, fok_l, fbad_l, hb_l, ack_l, known_r, admin_r;
  sync_edge u_s_tap   (.clk(clk), .rst_n(rst_n), .din(card_tap),      .level(tap_l),   .rise(tap_r));
  sync_edge u_s_known (.clk(clk), .rst_n(rst_n), .din(card_known),    .level(known_l), .rise(known_r));
  sync_edge u_s_admin (.clk(clk), .rst_n(rst_n), .din(card_admin),    .level(admin_l), .rise(admin_r));
  sync_edge u_s_fok   (.clk(clk), .rst_n(rst_n), .din(face_ok),       .level(fok_l),   .rise(fok_r));
  sync_edge u_s_fbad  (.clk(clk), .rst_n(rst_n), .din(face_bad),      .level(fbad_l),  .rise(fbad_r));
  sync_edge u_s_hb    (.clk(clk), .rst_n(rst_n), .din(esp_heartbeat), .level(hb_l),    .rise(hb_r));
  sync_edge u_s_ack   (.clk(clk), .rst_n(rst_n), .din(ev_ack),        .level(ack_l),   .rise(ack_r));

  // ---------------- buttons and sensors ----------------
  logic exit_db, panic_db, closed_db, bout_db, bin_db, motion_db, smoke_db, gas_db;
  debounce #(.N(DEB_N))              u_d_exit  (.clk(clk), .rst_n(rst_n), .en(1'b1), .din(exit_btn),    .dout(exit_db));
  debounce #(.N(DEB_N))              u_d_panic (.clk(clk), .rst_n(rst_n), .en(1'b1), .din(panic_btn),   .dout(panic_db));
  debounce #(.N(DEB_N), .INIT(1'b1)) u_d_door  (.clk(clk), .rst_n(rst_n), .en(1'b1), .din(door_closed), .dout(closed_db));
  debounce #(.N(DEB_N))              u_d_bout  (.clk(clk), .rst_n(rst_n), .en(1'b1), .din(beam_out),    .dout(bout_db));
  debounce #(.N(DEB_N))              u_d_bin   (.clk(clk), .rst_n(rst_n), .en(1'b1), .din(beam_in),     .dout(bin_db));
  debounce #(.N(DEB_N))              u_d_mot   (.clk(clk), .rst_n(rst_n), .en(1'b1), .din(motion),      .dout(motion_db));
  debounce #(.N(SMOKE_CONFIRM_S))    u_d_smoke (.clk(clk), .rst_n(rst_n), .en(tick), .din(smoke),       .dout(smoke_db));
  debounce #(.N(SMOKE_CONFIRM_S))    u_d_gas   (.clk(clk), .rst_n(rst_n), .en(tick), .din(gas),         .dout(gas_db));

  logic exit_q, panic_q, motion_q, fire_q, gas_q, evac_q;
  logic exit_press, panic_press, motion_rise;
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      exit_q <= 1'b0; panic_q <= 1'b0; motion_q <= 1'b0;
      fire_q <= 1'b0; gas_q <= 1'b0; evac_q <= 1'b0;
    end else begin
      exit_q <= exit_db; panic_q <= panic_db; motion_q <= motion_db;
      fire_q <= fire_mode; gas_q <= gas_mode; evac_q <= evac_done;
    end
  end
  assign exit_press  = exit_db & ~exit_q;
  assign panic_press = panic_db & ~panic_q;
  assign motion_rise = motion_db & ~motion_q;

  // ---------------- clock of the day ----------------
  logic [4:0]  hr;
  logic [5:0]  mn;
  logic [10:0] tod, tod_q;
  logic        is_late, day_reset;
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      hr    <= '0;
      mn    <= '0;
      tod_q <= '0;
    end else begin
      hr    <= hour;
      mn    <= minute;
      tod_q <= tod;
    end
  end
  assign tod        = 11'(hr) * 11'd60 + 11'(mn);
  assign night_mode = (hr >= 5'(NIGHT_START_H)) || (hr < 5'(NIGHT_END_H));
  assign is_late    = (tod > 11'(CLASS_START_MIN + LATE_GRACE_MIN)) && (tod < 11'(CLASS_END_MIN));
  assign day_reset  = (tod == 11'd0) && (tod_q != 11'd0);

  // ---------------- safety ----------------
  logic emergency;
  assign fire_mode = smoke_db;
  assign gas_mode  = gas_db;
  assign emergency = fire_mode || gas_mode;

  // ---------------- door ----------------
  logic grant, deny;
  logic ev_unknown, ev_full, ev_night, ev_lockout, ev_forced, ev_held;
  door_fsm u_door (
    .clk(clk), .rst_n(rst_n), .tick(tick),
    .card_tap(tap_r), .card_known(known_l), .card_admin(admin_l),
    .face_ok(fok_r), .face_bad(fbad_r),
    .exit_press(exit_press), .door_closed(closed_db),
    .night(night_mode), .room_full(room_full), .emergency(emergency),
    .unlock(door_unlock), .locked_out(locked_out), .held_warn(held_warn),
    .grant(grant), .deny(deny),
    .ev_unknown(ev_unknown), .ev_full(ev_full), .ev_night(ev_night),
    .ev_lockout(ev_lockout), .ev_forced(ev_forced), .ev_held(ev_held));

  // ---------------- people ----------------
  logic pass_in, pass_out, tailgate;
  people_counter #(.MAX_PEOPLE(MAX_PEOPLE)) u_people (
    .clk(clk), .rst_n(rst_n), .tick(tick),
    .beam_out(bout_db), .beam_in(bin_db), .grant(grant), .emergency(emergency),
    .people(people), .full(room_full),
    .pass_in(pass_in), .pass_out(pass_out), .tailgate(tailgate));

  assign evac_done = fire_mode && (people == 6'd0);

  // ---------------- security alarms (only an admin card silences them) ----------------
  logic admin_tap, ev_intruder;
  logic f_forced, f_tail, f_intr, f_panic;
  assign admin_tap   = tap_r && known_l && admin_l;
  assign ev_intruder = night_mode && (people == 6'd0) && motion_rise;

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      f_forced <= 1'b0; f_tail <= 1'b0; f_intr <= 1'b0; f_panic <= 1'b0;
    end else begin
      if (admin_tap) begin
        f_forced <= 1'b0; f_tail <= 1'b0; f_intr <= 1'b0; f_panic <= 1'b0;
      end
      if (ev_forced)   f_forced <= 1'b1;
      if (tailgate)    f_tail   <= 1'b1;
      if (ev_intruder) f_intr   <= 1'b1;
      if (panic_press) f_panic  <= 1'b1;
    end
  end
  assign alarm_flags    = {f_panic, f_intr, f_tail, f_forced};
  assign security_alarm = |alarm_flags;

  // ---------------- ESP32 watchdog ----------------
  logic [7:0] hb_t;
  logic       ev_esp;
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      hb_t      <= '0;
      esp_fault <= 1'b0;
      ev_esp    <= 1'b0;
    end else begin
      ev_esp <= 1'b0;
      if (hb_r) begin
        hb_t      <= '0;
        esp_fault <= 1'b0;
      end else if (tick && !esp_fault) begin
        if (hb_t == 8'(ESP_TIMEOUT_S - 1)) begin
          esp_fault <= 1'b1;
          ev_esp    <= 1'b1;
        end else begin
          hb_t <= hb_t + 8'd1;
        end
      end
    end
  end

  // ---------------- attendance ----------------
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      present_today <= '0;
      late_today    <= '0;
    end else if (day_reset) begin
      present_today <= '0;
      late_today    <= '0;
    end else if (grant) begin
      if (present_today != 8'hFF) present_today <= present_today + 8'd1;
      if (is_late && late_today != 8'hFF) late_today <= late_today + 8'd1;
    end
  end

  // ---------------- comfort ----------------
  room_comfort #(.EMPTY_OFF_S(EMPTY_OFF_S)) u_comfort (
    .clk(clk), .rst_n(rst_n), .tick(tick),
    .occupied(people != 6'd0), .motion(motion_db),
    .fire(fire_mode), .gas(gas_mode),
    .temp_c(temp_c), .air_q(air_q),
    .lights_on(lights_on), .fan_speed(fan_speed), .vent_on(vent_on));

  // ---------------- buzzer ----------------
  buzzer u_buzz (
    .clk(clk), .rst_n(rst_n), .beat(beat),
    .ok(grant), .deny(deny), .warn(held_warn),
    .alarm(security_alarm), .siren(emergency),
    .buzz(buzzer), .mode(buzz_mode));

  // ---------------- events ----------------
  logic [15:0] ev_req;
  always_comb begin
    ev_req                = '0;
    ev_req[EV_ENTRY]      = grant && !is_late;
    ev_req[EV_LATE]       = grant && is_late;
    ev_req[EV_DENY_FULL]  = ev_full;
    ev_req[EV_DENY_NIGHT] = ev_night;
    ev_req[EV_UNKNOWN]    = ev_unknown;
    ev_req[EV_LOCKOUT]    = ev_lockout;
    ev_req[EV_HELD_OPEN]  = ev_held;
    ev_req[EV_ESP_DOWN]   = ev_esp;
    ev_req[EV_TAILGATE]   = tailgate;
    ev_req[EV_FORCED]     = ev_forced;
    ev_req[EV_INTRUDER]   = ev_intruder;
    ev_req[EV_EVAC_DONE]  = evac_done && !evac_q;
    ev_req[EV_PANIC]      = panic_press;
    ev_req[EV_GAS]        = gas_mode && !gas_q;
    ev_req[EV_FIRE]       = fire_mode && !fire_q;
  end

  event_logger #(.DEPTH(EVENT_DEPTH)) u_log (
    .clk(clk), .rst_n(rst_n), .ev_req(ev_req),
    .hour(hr), .minute(mn), .people(people),
    .ev_ack(ack_r), .ev_valid(ev_valid), .ev_data(ev_data),
    .ev_count(ev_count), .ev_lost(ev_lost));

  // levels/rises not needed for every ESP32 pin
  logic unused_ok;
  assign unused_ok = &{1'b0, tap_l, fok_l, fbad_l, hb_l, ack_l, known_r, admin_r, pass_in, pass_out};
endmodule
