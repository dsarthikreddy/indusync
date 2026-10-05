`timescale 1ns/1ps
// ONE FULL DAY AT THE CLASSROOM DOOR - self-checking testbench.
// Every check prints PASS or FAIL. The last line says ALL ... TESTS PASSED.
// For speed the chip "second" is 100 clocks here (1 us); the real chip uses 100 MHz.
module tb_smart_room;
  import smart_room_pkg::*;

  localparam int CPS = 100;

  // ---------------- signals ----------------
  logic clk = 1'b0, rst_n = 1'b0;
  logic card_tap = 0, card_known = 0, card_admin = 0, face_ok = 0, face_bad = 0, esp_heartbeat = 0;
  logic [4:0] hour = 5'd8;
  logic [5:0] minute = 6'd50;
  logic exit_btn = 0, panic_btn = 0, door_closed = 1, beam_out = 0, beam_in = 0, motion = 0, smoke = 0, gas = 0;
  logic [7:0] temp_c = 8'd25, air_q = 8'd50;
  logic ev_ack = 0;

  logic door_unlock, buzzer, lights_on, vent_on, fire_mode, gas_mode, evac_done, night_mode;
  logic security_alarm, locked_out, held_warn, room_full, esp_fault, ev_valid;
  logic [2:0] buzz_mode;
  logic [1:0] fan_speed;
  logic [3:0] alarm_flags;
  logic [5:0] people;
  logic [7:0] present_today, late_today, ev_lost;
  logic [23:0] ev_data;
  logic [4:0] ev_count;

  always #5 clk = ~clk;   // 100 MHz simulation clock

  smart_room_top #(.CLKS_PER_SEC(CPS), .MAX_PEOPLE(4)) dut (.*);

  // ---------------- score keeping ----------------
  int pass_n = 0, fail_n = 0;
  int seen [16];
  logic [3:0] rx_code [256];
  logic [4:0] rx_hour [256];
  int rx_n = 0;
  bit wifi_up = 1, esp_alive = 1;
  logic [7:0] ev_h, ev_m, ev_p;

  function automatic string ev_name(input logic [3:0] c);
    case (c)
      EV_ENTRY:      return "ENTRY";
      EV_LATE:       return "LATE ENTRY";
      EV_DENY_FULL:  return "DENIED-FULL";
      EV_DENY_NIGHT: return "DENIED-NIGHT";
      EV_UNKNOWN:    return "UNKNOWN CARD";
      EV_LOCKOUT:    return "FACE LOCKOUT";
      EV_HELD_OPEN:  return "DOOR LEFT OPEN";
      EV_ESP_DOWN:   return "ESP32 DOWN";
      EV_TAILGATE:   return "TAILGATING";
      EV_FORCED:     return "DOOR FORCED";
      EV_INTRUDER:   return "INTRUDER";
      EV_EVAC_DONE:  return "EVACUATION DONE";
      EV_PANIC:      return "PANIC / SOS";
      EV_GAS:        return "GAS LEAK";
      EV_FIRE:       return "FIRE";
      default:       return "?";
    endcase
  endfunction

  task automatic check(input bit ok, input string what);
    if (ok) begin
      pass_n++;
      $display("    PASS  %s", what);
    end else begin
      fail_n++;
      $display("    FAIL  %s   <-- at %0t ns", what, $time);
    end
  endtask

  task automatic step(input string s);
    $display("");
    $display("[%02d:%02d] %s", hour, minute, s);
  endtask

  // ---------------- helpers that act like people / ESP32 ----------------
  task automatic settle();  repeat (12) @(posedge clk); endtask
  task automatic wait_s(input int n); repeat (n * CPS) @(posedge clk); endtask

  task automatic set_time(input int h, input int m);
    @(negedge clk); hour = 5'(h); minute = 6'(m); settle();
  endtask

  task automatic tap(input bit known, input bit admin);
    @(negedge clk); card_known = known; card_admin = admin; card_tap = 1;
    @(negedge clk); card_tap = 0;
    settle();
  endtask

  task automatic face(input bit ok);
    @(negedge clk); if (ok) face_ok = 1; else face_bad = 1;
    @(negedge clk); face_ok = 0; face_bad = 0;
    settle();
  endtask

  task automatic press_exit();
    @(negedge clk); exit_btn = 1; repeat (8) @(negedge clk); exit_btn = 0; settle();
  endtask

  task automatic press_panic();
    @(negedge clk); panic_btn = 1; repeat (8) @(negedge clk); panic_btn = 0; settle();
  endtask

  task automatic walk_in();   // outside beam, both, inside beam, clear
    @(negedge clk); beam_out = 1; repeat (8) @(negedge clk);
    beam_in = 1;  repeat (8) @(negedge clk);
    beam_out = 0; repeat (8) @(negedge clk);
    beam_in = 0;  settle();
  endtask

  task automatic walk_out();
    @(negedge clk); beam_in = 1; repeat (8) @(negedge clk);
    beam_out = 1; repeat (8) @(negedge clk);
    beam_in = 0;  repeat (8) @(negedge clk);
    beam_out = 0; settle();
  endtask

  task automatic door(input bit open);
    @(negedge clk); door_closed = !open; settle();
  endtask

  task automatic person_enters(input bit known);   // card + face + walk in
    tap(known, 0); face(1); door(1); walk_in(); door(0);
  endtask

  // wait (max 3000 clocks) until the cloud has received n events of this kind
  task automatic expect_event(input logic [3:0] code, input int n, input string what);
    for (int k = 0; k < 3000 && seen[code] < n; k++) @(posedge clk);
    check(seen[code] == n, what);
  endtask

  // ---------------- ESP32 model: heartbeat every 2 s ----------------
  initial begin
    forever begin
      repeat (2 * CPS) @(posedge clk);
      if (esp_alive) begin
        @(negedge clk); esp_heartbeat = 1;
        @(negedge clk); esp_heartbeat = 0;
      end
    end
  end

  // ---------------- ESP32 model: sends chip events to the cloud when Wi-Fi is up ----------------
  initial begin
    forever begin
      @(posedge clk);
      if (wifi_up && ev_valid) begin
        rx_code[rx_n[7:0]] = ev_data[23:20];
        rx_hour[rx_n[7:0]] = ev_data[19:15];
        rx_n++;
        seen[ev_data[23:20]]++;
        ev_h = ev_data[19:15]; ev_m = ev_data[14:9]; ev_p = ev_data[8:3];
        $display("           cloud <- %-15s time %02d:%02d  people=%0d",
                 ev_name(ev_data[23:20]), ev_h, ev_m, ev_p);
        @(negedge clk); ev_ack = 1;
        @(negedge clk); ev_ack = 0;
        repeat (6) @(posedge clk);
      end
    end
  end

  // ---------------- THE DAY ----------------
  int rx_start;
  initial begin
`ifdef DUMP_VCD
    $dumpfile("smart_room.vcd");
    $dumpvars(0, tb_smart_room);
`endif
    $display("==============================================================");
    $display("  SMART ROOM CONTROLLER - one full day at the classroom door");
    $display("  (room size for this test = 4 people)");
    $display("==============================================================");
    repeat (5) @(posedge clk);
    rst_n = 1;
    settle();

    step("Morning. Power on.");
    check(!door_unlock,                 "door is locked");
    check(people == 0 && !lights_on,    "room empty, lights off (no power wasted)");
    check(!security_alarm && buzz_mode == BZ_OFF, "no alarm, buzzer quiet");

    // ---------- 1. normal entry ----------
    set_time(8, 55);
    step("Ravi taps his card (class starts 09:00)");
    tap(1, 0);
    check(!door_unlock,                 "card alone does NOT open the door (face needed)");
    face(1);
    check(door_unlock,                  "face OK -> door unlocks");
    check(buzz_mode == BZ_OK,           "one short 'welcome' beep");
    door(1); walk_in(); door(0);
    check(people == 1,                  "people counter = 1");
    check(lights_on,                    "lights switch ON by themselves");
    check(!security_alarm,              "no tailgating alarm (1 card, 1 person)");
    expect_event(EV_ENTRY, 1,           "ENTRY (on time) sent to cloud");
    wait_s(4);
    check(!door_unlock,                 "door locks again by itself after 3 s");

    // ---------- 2. fan ----------
    step("Room gets hot");
    @(negedge clk); temp_c = 8'd33; settle();
    check(fan_speed == 2,               "33 C -> fan speed 2");
    @(negedge clk); temp_c = 8'd30; settle();
    check(fan_speed == 1,               "30 C -> fan slows to speed 1");
    @(negedge clk); temp_c = 8'd27; settle();
    check(fan_speed == 1,               "27 C -> fan stays 1 (no flicker on/off at the edge)");
    @(negedge clk); temp_c = 8'd25; settle();
    check(fan_speed == 0,               "25 C -> fan off");

    // ---------- 3. late ----------
    set_time(9, 20);
    step("Priya arrives after 09:10");
    person_enters(1);
    expect_event(EV_LATE, 1,            "LATE ENTRY sent to cloud");
    check(late_today == 1 && present_today == 2, "attendance: 2 present, 1 late");
    check(people == 2,                  "people counter = 2");
    wait_s(4);

    // ---------- 4. tailgating ----------
    set_time(9, 25);
    step("Arun opens the door, but his friend walks in behind him without a card");
    tap(1, 0); face(1); door(1); walk_in(); walk_in(); door(0);
    check(people == 4,                  "both are counted: people = 4");
    check(security_alarm && alarm_flags[1], "TAILGATING alarm");
    check(buzz_mode == BZ_ALARM,        "continuous alarm sound");
    expect_event(EV_TAILGATE, 1,        "TAILGATING sent to cloud");
    step("Teacher taps the ADMIN card to silence it");
    tap(1, 1);
    check(!security_alarm,              "alarm stopped only by admin card");
    wait_s(11);

    // ---------- 5. room full ----------
    step("Kiran taps a valid card, but the room is full (4/4)");
    check(room_full,                    "room is FULL");
    tap(1, 0);
    check(!door_unlock,                 "door stays locked");
    check(buzz_mode == BZ_DENY,         "two short 'no' beeps");
    expect_event(EV_DENY_FULL, 1,       "DENIED-FULL sent to cloud");
    step("Ravi presses the EXIT button and leaves");
    press_exit();
    check(door_unlock,                  "exit button opens the door");
    door(1); walk_out(); door(0);
    check(people == 3 && !room_full,    "people = 3, room not full any more");
    wait_s(4);

    // ---------- 6. stolen card ----------
    set_time(10, 0);
    step("A stranger uses Ravi's lost card: 3 wrong faces");
    tap(1, 0); face(0); face(0); face(0);
    check(locked_out,                   "door LOCKED for 30 s after 3 wrong faces");
    expect_event(EV_LOCKOUT, 1,         "FACE LOCKOUT sent to cloud");
    tap(1, 0); face(1);
    check(!door_unlock,                 "even a correct face cannot open during lockout");
    step("Someone inside wants to go out during the lockout");
    press_exit();
    check(door_unlock,                  "EXIT button still works: nobody is ever trapped");
    wait_s(31);
    check(!locked_out,                  "lockout ends by itself after 30 s");

    // ---------- 7. unknown card ----------
    step("Somebody taps an unknown card");
    tap(0, 0);
    check(!door_unlock,                 "door stays locked");
    check(buzz_mode == BZ_DENY,         "two short 'no' beeps");
    expect_event(EV_UNKNOWN, 1,         "UNKNOWN CARD sent to cloud");
    wait_s(1);

    // ---------- 8. door left open ----------
    set_time(10, 30);
    step("Priya goes out and leaves the door open");
    press_exit(); door(1); walk_out();
    check(people == 2,                  "people = 2");
    wait_s(21);
    check(held_warn && buzz_mode == BZ_WARN, "after 20 s: 'close the door' beep every second");
    check(!security_alarm,              "not a break-in (door was opened legally)");
    expect_event(EV_HELD_OPEN, 1,       "DOOR LEFT OPEN sent to cloud");
    door(0);
    check(!held_warn,                   "door closed -> beeping stops");

    // ---------- 9. stuffy air ----------
    step("Air gets stuffy (many people breathing)");
    @(negedge clk); air_q = 8'd180; settle();
    check(vent_on,                      "fresh-air vent ON");
    @(negedge clk); air_q = 8'd130; settle();
    check(vent_on,                      "still ON until the air is really fresh");
    @(negedge clk); air_q = 8'd100; settle();
    check(!vent_on,                     "fresh again -> vent OFF");

    // ---------- 10. smoke ----------
    set_time(11, 0);
    @(negedge clk); temp_c = 8'd33; settle();
    check(fan_speed == 2,               "hot room, fan running");
    step("Dust puff: smoke sensor flickers for 1 second");
    @(negedge clk); smoke = 1; wait_s(1); @(negedge clk); smoke = 0; wait_s(4);
    check(!fire_mode && seen[EV_FIRE] == 0, "glitch ignored: NO false fire alarm");
    step("Real smoke (lasts more than 3 s)");
    @(negedge clk); smoke = 1; wait_s(4);
    check(fire_mode,                    "FIRE detected");
    check(door_unlock,                  "door unlocked for escape");
    check(buzz_mode == BZ_SIREN,        "siren sound");
    check(fan_speed == 0,               "fan OFF (fan would spread smoke)");
    check(lights_on,                    "lights ON to see the way out");
    expect_event(EV_FIRE, 1,            "FIRE sent to cloud");
    step("Everybody runs out");
    door(1); walk_out(); walk_out(); door(0);
    check(people == 0 && !security_alarm, "people = 0, no tailgating alarm in a fire");
    check(evac_done,                    "EVACUATION COMPLETE: the room is empty");
    expect_event(EV_EVAC_DONE, 1,       "EVACUATION DONE sent to cloud");
    step("Smoke clears");
    @(negedge clk); smoke = 0; temp_c = 8'd25; wait_s(4); settle();
    check(!fire_mode && !door_unlock,   "back to normal, door locked again");

    // ---------- 11. Wi-Fi down ----------
    set_time(12, 0);
    step("Wi-Fi goes down. One entry + 19 unknown-card taps happen");
    wifi_up = 0;
    person_enters(1);
    wait_s(4);
    for (int i = 0; i < 19; i++) tap(0, 0);
    check(ev_count == 16,               "16 events kept safely in the chip memory");
    check(ev_lost == 4,                 "4 extra events counted as lost (never silently)");
    step("Wi-Fi comes back");
    rx_start = rx_n;
    wifi_up = 1;
    for (int k = 0; k < 5000 && ev_count != 0; k++) @(posedge clk);
    check(ev_count == 0 && rx_n - rx_start == 16, "all 16 stored events reach the cloud");
    check(rx_code[rx_start[7:0]] == EV_LATE && rx_hour[rx_start[7:0]] == 5'd12,
          "in the right order, with the right time (12:00 entry first)");

    // ---------- 12. power saver ----------
    set_time(16, 0);
    @(negedge clk); temp_c = 8'd33; settle();
    check(fan_speed == 2 && lights_on,  "1 person inside: lights on, fan on");
    step("Last person leaves and forgets the lights and fan");
    press_exit(); door(1); walk_out(); door(0);
    check(people == 0 && lights_on,     "room empty, lights still on for a minute");
    wait_s(61);
    check(!lights_on && fan_speed == 0, "after 60 s: lights and fan OFF by themselves");
    @(negedge clk); temp_c = 8'd25;

    // ---------- 13. night ----------
    set_time(22, 30);
    step("Night. A student taps a valid card");
    check(night_mode,                   "night lock is ON (22:00-06:00)");
    tap(1, 0);
    check(!door_unlock,                 "normal cards don't work at night");
    expect_event(EV_DENY_NIGHT, 1,      "DENIED-NIGHT sent to cloud");
    step("Movement inside the EMPTY room");
    @(negedge clk); motion = 1; settle(); @(negedge clk); motion = 0; settle();
    check(security_alarm && alarm_flags[2], "INTRUDER alarm");
    check(lights_on,                    "lights flash ON (scare intruder, help camera)");
    expect_event(EV_INTRUDER, 1,        "INTRUDER sent to cloud");
    tap(1, 1);
    check(!security_alarm,              "admin card silences it");
    wait_s(11);

    // ---------- 14. forced door ----------
    set_time(23, 0);
    step("Someone forces the locked door open");
    door(1);
    check(security_alarm && alarm_flags[0], "DOOR FORCED alarm");
    check(buzz_mode == BZ_ALARM,        "continuous alarm sound");
    expect_event(EV_FORCED, 1,          "DOOR FORCED sent to cloud");
    door(0); tap(1, 1);
    check(!security_alarm,              "admin card silences it");
    wait_s(11);

    // ---------- 15. panic ----------
    step("A person inside feels unsafe and presses the PANIC button");
    press_panic();
    check(security_alarm && alarm_flags[3], "PANIC alarm");
    expect_event(EV_PANIC, 1,           "PANIC / SOS sent to cloud");
    tap(1, 1); wait_s(11);

    // ---------- 16. gas ----------
    step("Gas leak");
    @(negedge clk); gas = 1; wait_s(4);
    check(gas_mode && door_unlock,      "GAS detected, door unlocked");
    check(vent_on && fan_speed == 0,    "exhaust vent ON, fan OFF (no sparks)");
    check(buzz_mode == BZ_SIREN,        "siren sound");
    expect_event(EV_GAS, 1,             "GAS LEAK sent to cloud");
    @(negedge clk); gas = 0; wait_s(4); settle();
    check(!gas_mode && !door_unlock,    "gas gone, back to normal");

    // ---------- 17. ESP32 crash ----------
    step("ESP32 board freezes (no heartbeat)");
    esp_alive = 0;
    wait_s(12);
    check(esp_fault,                    "chip notices ESP32 is DOWN within 10 s");
    expect_event(EV_ESP_DOWN, 1,        "ESP32 DOWN event stored");
    press_exit();
    check(door_unlock,                  "safety still works without the ESP32 (exit opens)");
    esp_alive = 1;
    wait_s(3);
    check(!esp_fault,                   "ESP32 back -> fault clears");

    // ---------- 18. midnight ----------
    step("End of the day");
    check(present_today == 4 && late_today == 3, "today: 4 entries, 3 late");
    set_time(0, 0);
    check(present_today == 0 && late_today == 0, "midnight: counters reset for the new day");

    // ---------- result ----------
    wait_s(1);
    $display("");
    $display("==============================================================");
    $display("  Events sent to cloud: %0d    lost while Wi-Fi was down: %0d", rx_n, ev_lost);
    if (fail_n == 0)
      $display("  ALL %0d TESTS PASSED", pass_n);
    else
      $display("  %0d TESTS FAILED, %0d passed", fail_n, pass_n);
    $display("==============================================================");
    $finish;
  end
endmodule
