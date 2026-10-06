`timescale 1ns/1ps
// Plain Verilog-2005 version of ../../tb/tb_smart_room.sv for old Vivado (2014.x). Same 88 checks.
module tb_smart_room;
	localparam signed [31:0] CPS = 100;
	reg clk = 1'b0;
	reg rst_n = 1'b0;
	reg card_tap = 0;
	reg card_known = 0;
	reg card_admin = 0;
	reg face_ok = 0;
	reg face_bad = 0;
	reg esp_heartbeat = 0;
	reg [4:0] hour = 5'd8;
	reg [5:0] minute = 6'd50;
	reg exit_btn = 0;
	reg panic_btn = 0;
	reg door_closed = 1;
	reg beam_out = 0;
	reg beam_in = 0;
	reg motion = 0;
	reg smoke = 0;
	reg gas = 0;
	reg [7:0] temp_c = 8'd25;
	reg [7:0] air_q = 8'd50;
	reg ev_ack = 0;
	wire door_unlock;
	wire buzzer;
	wire lights_on;
	wire vent_on;
	wire fire_mode;
	wire gas_mode;
	wire evac_done;
	wire night_mode;
	wire security_alarm;
	wire locked_out;
	wire held_warn;
	wire room_full;
	wire esp_fault;
	wire ev_valid;
	wire [2:0] buzz_mode;
	wire [1:0] fan_speed;
	wire [3:0] alarm_flags;
	wire [5:0] people;
	wire [7:0] present_today;
	wire [7:0] late_today;
	wire [7:0] ev_lost;
	wire [23:0] ev_data;
	wire [4:0] ev_count;
	always #(5) clk = ~clk;
	smart_room_top #(
		.CLKS_PER_SEC(CPS),
		.MAX_PEOPLE(4)
	) dut(
		.clk(clk),
		.rst_n(rst_n),
		.card_tap(card_tap),
		.card_known(card_known),
		.card_admin(card_admin),
		.face_ok(face_ok),
		.face_bad(face_bad),
		.esp_heartbeat(esp_heartbeat),
		.hour(hour),
		.minute(minute),
		.exit_btn(exit_btn),
		.panic_btn(panic_btn),
		.door_closed(door_closed),
		.beam_out(beam_out),
		.beam_in(beam_in),
		.motion(motion),
		.smoke(smoke),
		.gas(gas),
		.temp_c(temp_c),
		.air_q(air_q),
		.ev_ack(ev_ack),
		.door_unlock(door_unlock),
		.buzzer(buzzer),
		.buzz_mode(buzz_mode),
		.lights_on(lights_on),
		.fan_speed(fan_speed),
		.vent_on(vent_on),
		.fire_mode(fire_mode),
		.gas_mode(gas_mode),
		.evac_done(evac_done),
		.night_mode(night_mode),
		.security_alarm(security_alarm),
		.alarm_flags(alarm_flags),
		.locked_out(locked_out),
		.held_warn(held_warn),
		.room_full(room_full),
		.people(people),
		.esp_fault(esp_fault),
		.present_today(present_today),
		.late_today(late_today),
		.ev_valid(ev_valid),
		.ev_data(ev_data),
		.ev_count(ev_count),
		.ev_lost(ev_lost)
	);
	reg signed [31:0] pass_n = 0;
	reg signed [31:0] fail_n = 0;
	reg signed [31:0] seen [0:15];
	integer si;
	initial for (si = 0; si < 16; si = si + 1) seen[si] = 0;
	reg [3:0] rx_code [0:255];
	reg [4:0] rx_hour [0:255];
	reg signed [31:0] rx_n = 0;
	reg wifi_up = 1;
	reg esp_alive = 1;
	reg [7:0] ev_h;
	reg [7:0] ev_m;
	reg [7:0] ev_p;
	localparam [3:0] smart_room_pkg_EV_DENY_FULL = 4'd3;
	localparam [3:0] smart_room_pkg_EV_DENY_NIGHT = 4'd4;
	localparam [3:0] smart_room_pkg_EV_ENTRY = 4'd1;
	localparam [3:0] smart_room_pkg_EV_ESP_DOWN = 4'd8;
	localparam [3:0] smart_room_pkg_EV_EVAC_DONE = 4'd12;
	localparam [3:0] smart_room_pkg_EV_FIRE = 4'd15;
	localparam [3:0] smart_room_pkg_EV_FORCED = 4'd10;
	localparam [3:0] smart_room_pkg_EV_GAS = 4'd14;
	localparam [3:0] smart_room_pkg_EV_HELD_OPEN = 4'd7;
	localparam [3:0] smart_room_pkg_EV_INTRUDER = 4'd11;
	localparam [3:0] smart_room_pkg_EV_LATE = 4'd2;
	localparam [3:0] smart_room_pkg_EV_LOCKOUT = 4'd6;
	localparam [3:0] smart_room_pkg_EV_PANIC = 4'd13;
	localparam [3:0] smart_room_pkg_EV_TAILGATE = 4'd9;
	localparam [3:0] smart_room_pkg_EV_UNKNOWN = 4'd5;
	function [8*16-1:0] ev_name;
		input reg [3:0] c;
		case (c)
			smart_room_pkg_EV_ENTRY: ev_name = "ENTRY";
			smart_room_pkg_EV_LATE: ev_name = "LATE ENTRY";
			smart_room_pkg_EV_DENY_FULL: ev_name = "DENIED-FULL";
			smart_room_pkg_EV_DENY_NIGHT: ev_name = "DENIED-NIGHT";
			smart_room_pkg_EV_UNKNOWN: ev_name = "UNKNOWN CARD";
			smart_room_pkg_EV_LOCKOUT: ev_name = "FACE LOCKOUT";
			smart_room_pkg_EV_HELD_OPEN: ev_name = "DOOR LEFT OPEN";
			smart_room_pkg_EV_ESP_DOWN: ev_name = "ESP32 DOWN";
			smart_room_pkg_EV_TAILGATE: ev_name = "TAILGATING";
			smart_room_pkg_EV_FORCED: ev_name = "DOOR FORCED";
			smart_room_pkg_EV_INTRUDER: ev_name = "INTRUDER";
			smart_room_pkg_EV_EVAC_DONE: ev_name = "EVACUATION DONE";
			smart_room_pkg_EV_PANIC: ev_name = "PANIC / SOS";
			smart_room_pkg_EV_GAS: ev_name = "GAS LEAK";
			smart_room_pkg_EV_FIRE: ev_name = "FIRE";
			default: ev_name = "?";
		endcase
	endfunction
	task automatic pstr;
		input [8*80-1:0] s;
		input integer w;
		integer k;
		integer n;
		begin
			n = 0;
			for (k = 8*80/8 - 1; k >= 0; k = k - 1)
				if (s[k*8 +: 8] != 8'd0) begin
					$write("%c", s[k*8 +: 8]);
					n = n + 1;
				end
			for (k = n; k < w; k = k + 1)
				$write(" ");
		end
	endtask
	task automatic check;
		input reg ok;
		input [8*80-1:0] what;
		if (ok) begin
			pass_n = pass_n + 1;
			begin $write("    PASS  "); pstr(what, 0); $write("\n"); end
		end
		else begin
			fail_n = fail_n + 1;
			begin $write("    FAIL  "); pstr(what, 0); $display("   <-- at %0t ns", $time); end
		end
	endtask
	task automatic step;
		input [8*80-1:0] s;
		begin
			$display("");
			$write("[%02d:%02d] ", hour, minute); pstr(s, 0); $write("\n");
		end
	endtask
	task automatic settle;
		repeat (12) @(posedge clk)
			;
	endtask
	task automatic wait_s;
		input reg signed [31:0] n;
		repeat (n * CPS) @(posedge clk)
			;
	endtask
	function automatic signed [4:0] sv2v_cast_5_signed;
		input reg signed [4:0] inp;
		sv2v_cast_5_signed = inp;
	endfunction
	function automatic signed [5:0] sv2v_cast_6_signed;
		input reg signed [5:0] inp;
		sv2v_cast_6_signed = inp;
	endfunction
	task automatic set_time;
		input reg signed [31:0] h;
		input reg signed [31:0] m;
		begin
			@(negedge clk)
				;
			hour = sv2v_cast_5_signed(h);
			minute = sv2v_cast_6_signed(m);
			settle;
		end
	endtask
	task automatic tap;
		input reg known;
		input reg admin;
		begin
			@(negedge clk)
				;
			card_known = known;
			card_admin = admin;
			card_tap = 1;
			@(negedge clk)
				;
			card_tap = 0;
			settle;
		end
	endtask
	task automatic face;
		input reg ok;
		begin
			@(negedge clk)
				;
			if (ok)
				face_ok = 1;
			else
				face_bad = 1;
			@(negedge clk)
				;
			face_ok = 0;
			face_bad = 0;
			settle;
		end
	endtask
	task automatic press_exit;
		begin
			@(negedge clk)
				;
			exit_btn = 1;
			repeat (8) @(negedge clk)
				;
			exit_btn = 0;
			settle;
		end
	endtask
	task automatic press_panic;
		begin
			@(negedge clk)
				;
			panic_btn = 1;
			repeat (8) @(negedge clk)
				;
			panic_btn = 0;
			settle;
		end
	endtask
	task automatic walk_in;
		begin
			@(negedge clk)
				;
			beam_out = 1;
			repeat (8) @(negedge clk)
				;
			beam_in = 1;
			repeat (8) @(negedge clk)
				;
			beam_out = 0;
			repeat (8) @(negedge clk)
				;
			beam_in = 0;
			settle;
		end
	endtask
	task automatic walk_out;
		begin
			@(negedge clk)
				;
			beam_in = 1;
			repeat (8) @(negedge clk)
				;
			beam_out = 1;
			repeat (8) @(negedge clk)
				;
			beam_in = 0;
			repeat (8) @(negedge clk)
				;
			beam_out = 0;
			settle;
		end
	endtask
	task automatic door;
		input reg open;
		begin
			@(negedge clk)
				;
			door_closed = !open;
			settle;
		end
	endtask
	task automatic person_enters;
		input reg known;
		begin
			tap(known, 0);
			face(1);
			door(1);
			walk_in;
			door(0);
		end
	endtask
	task automatic expect_event;
		input reg [3:0] code;
		input reg signed [31:0] n;
		input [8*80-1:0] what;
		begin
			begin : sv2v_autoblock_1
				reg signed [31:0] k;
				for (k = 0; (k < 3000) && (seen[code] < n); k = k + 1)
					@(posedge clk)
						;
			end
			check(seen[code] == n, what);
		end
	endtask
	initial forever begin
		repeat (200) @(posedge clk)
			;
		if (esp_alive) begin
			@(negedge clk)
				;
			esp_heartbeat = 1;
			@(negedge clk)
				;
			esp_heartbeat = 0;
		end
	end
	initial forever begin
		@(posedge clk)
			;
		if (wifi_up && ev_valid) begin
			rx_code[rx_n[7:0]] = ev_data[23:20];
			rx_hour[rx_n[7:0]] = ev_data[19:15];
			rx_n = rx_n + 1;
			seen[ev_data[23:20]] = seen[ev_data[23:20]] + 1;
			ev_h = ev_data[19:15];
			ev_m = ev_data[14:9];
			ev_p = ev_data[8:3];
			$write("           cloud <- "); pstr(ev_name(ev_data[23:20]), 15); $display(" time %02d:%02d  people=%0d", ev_h, ev_m, ev_p);
			@(negedge clk)
				;
			ev_ack = 1;
			@(negedge clk)
				;
			ev_ack = 0;
			repeat (6) @(posedge clk)
				;
		end
	end
	reg signed [31:0] rx_start;
	localparam [2:0] smart_room_pkg_BZ_ALARM = 3'd4;
	localparam [2:0] smart_room_pkg_BZ_DENY = 3'd2;
	localparam [2:0] smart_room_pkg_BZ_OFF = 3'd0;
	localparam [2:0] smart_room_pkg_BZ_OK = 3'd1;
	localparam [2:0] smart_room_pkg_BZ_SIREN = 3'd5;
	localparam [2:0] smart_room_pkg_BZ_WARN = 3'd3;
	initial begin
		$display("==============================================================");
		$display("  SMART ROOM CONTROLLER - one full day at the classroom door");
		$display("  (room size for this test = 4 people)");
		$display("==============================================================");
		repeat (5) @(posedge clk)
			;
		rst_n = 1;
		settle;
		step("Morning. Power on.");
		check(!door_unlock, "door is locked");
		check((people == 0) && !lights_on, "room empty, lights off (no power wasted)");
		check(!security_alarm && (buzz_mode == smart_room_pkg_BZ_OFF), "no alarm, buzzer quiet");
		set_time(8, 55);
		step("Ravi taps his card (class starts 09:00)");
		tap(1, 0);
		check(!door_unlock, "card alone does NOT open the door (face needed)");
		face(1);
		check(door_unlock, "face OK -> door unlocks");
		check(buzz_mode == smart_room_pkg_BZ_OK, "one short 'welcome' beep");
		door(1);
		walk_in;
		door(0);
		check(people == 1, "people counter = 1");
		check(lights_on, "lights switch ON by themselves");
		check(!security_alarm, "no tailgating alarm (1 card, 1 person)");
		expect_event(smart_room_pkg_EV_ENTRY, 1, "ENTRY (on time) sent to cloud");
		wait_s(4);
		check(!door_unlock, "door locks again by itself after 3 s");
		step("Room gets hot");
		@(negedge clk)
			;
		temp_c = 8'd33;
		settle;
		check(fan_speed == 2, "33 C -> fan speed 2");
		@(negedge clk)
			;
		temp_c = 8'd30;
		settle;
		check(fan_speed == 1, "30 C -> fan slows to speed 1");
		@(negedge clk)
			;
		temp_c = 8'd27;
		settle;
		check(fan_speed == 1, "27 C -> fan stays 1 (no flicker on/off at the edge)");
		@(negedge clk)
			;
		temp_c = 8'd25;
		settle;
		check(fan_speed == 0, "25 C -> fan off");
		set_time(9, 20);
		step("Priya arrives after 09:10");
		person_enters(1);
		expect_event(smart_room_pkg_EV_LATE, 1, "LATE ENTRY sent to cloud");
		check((late_today == 1) && (present_today == 2), "attendance: 2 present, 1 late");
		check(people == 2, "people counter = 2");
		wait_s(4);
		set_time(9, 25);
		step("Arun opens the door, but his friend walks in behind him without a card");
		tap(1, 0);
		face(1);
		door(1);
		walk_in;
		walk_in;
		door(0);
		check(people == 4, "both are counted: people = 4");
		check(security_alarm && alarm_flags[1], "TAILGATING alarm");
		check(buzz_mode == smart_room_pkg_BZ_ALARM, "continuous alarm sound");
		expect_event(smart_room_pkg_EV_TAILGATE, 1, "TAILGATING sent to cloud");
		step("Teacher taps the ADMIN card to silence it");
		tap(1, 1);
		check(!security_alarm, "alarm stopped only by admin card");
		wait_s(11);
		step("Kiran taps a valid card, but the room is full (4/4)");
		check(room_full, "room is FULL");
		tap(1, 0);
		check(!door_unlock, "door stays locked");
		check(buzz_mode == smart_room_pkg_BZ_DENY, "two short 'no' beeps");
		expect_event(smart_room_pkg_EV_DENY_FULL, 1, "DENIED-FULL sent to cloud");
		step("Ravi presses the EXIT button and leaves");
		press_exit;
		check(door_unlock, "exit button opens the door");
		door(1);
		walk_out;
		door(0);
		check((people == 3) && !room_full, "people = 3, room not full any more");
		wait_s(4);
		set_time(10, 0);
		step("A stranger uses Ravi's lost card: 3 wrong faces");
		tap(1, 0);
		face(0);
		face(0);
		face(0);
		check(locked_out, "door LOCKED for 30 s after 3 wrong faces");
		expect_event(smart_room_pkg_EV_LOCKOUT, 1, "FACE LOCKOUT sent to cloud");
		tap(1, 0);
		face(1);
		check(!door_unlock, "even a correct face cannot open during lockout");
		step("Someone inside wants to go out during the lockout");
		press_exit;
		check(door_unlock, "EXIT button still works: nobody is ever trapped");
		wait_s(31);
		check(!locked_out, "lockout ends by itself after 30 s");
		step("Somebody taps an unknown card");
		tap(0, 0);
		check(!door_unlock, "door stays locked");
		check(buzz_mode == smart_room_pkg_BZ_DENY, "two short 'no' beeps");
		expect_event(smart_room_pkg_EV_UNKNOWN, 1, "UNKNOWN CARD sent to cloud");
		wait_s(1);
		set_time(10, 30);
		step("Priya goes out and leaves the door open");
		press_exit;
		door(1);
		walk_out;
		check(people == 2, "people = 2");
		wait_s(21);
		check(held_warn && (buzz_mode == smart_room_pkg_BZ_WARN), "after 20 s: 'close the door' beep every second");
		check(!security_alarm, "not a break-in (door was opened legally)");
		expect_event(smart_room_pkg_EV_HELD_OPEN, 1, "DOOR LEFT OPEN sent to cloud");
		door(0);
		check(!held_warn, "door closed -> beeping stops");
		step("Air gets stuffy (many people breathing)");
		@(negedge clk)
			;
		air_q = 8'd180;
		settle;
		check(vent_on, "fresh-air vent ON");
		@(negedge clk)
			;
		air_q = 8'd130;
		settle;
		check(vent_on, "still ON until the air is really fresh");
		@(negedge clk)
			;
		air_q = 8'd100;
		settle;
		check(!vent_on, "fresh again -> vent OFF");
		set_time(11, 0);
		@(negedge clk)
			;
		temp_c = 8'd33;
		settle;
		check(fan_speed == 2, "hot room, fan running");
		step("Dust puff: smoke sensor flickers for 1 second");
		@(negedge clk)
			;
		smoke = 1;
		wait_s(1);
		@(negedge clk)
			;
		smoke = 0;
		wait_s(4);
		check(!fire_mode && (seen[smart_room_pkg_EV_FIRE] == 0), "glitch ignored: NO false fire alarm");
		step("Real smoke (lasts more than 3 s)");
		@(negedge clk)
			;
		smoke = 1;
		wait_s(4);
		check(fire_mode, "FIRE detected");
		check(door_unlock, "door unlocked for escape");
		check(buzz_mode == smart_room_pkg_BZ_SIREN, "siren sound");
		check(fan_speed == 0, "fan OFF (fan would spread smoke)");
		check(lights_on, "lights ON to see the way out");
		expect_event(smart_room_pkg_EV_FIRE, 1, "FIRE sent to cloud");
		step("Everybody runs out");
		door(1);
		walk_out;
		walk_out;
		door(0);
		check((people == 0) && !security_alarm, "people = 0, no tailgating alarm in a fire");
		check(evac_done, "EVACUATION COMPLETE: the room is empty");
		expect_event(smart_room_pkg_EV_EVAC_DONE, 1, "EVACUATION DONE sent to cloud");
		step("Smoke clears");
		@(negedge clk)
			;
		smoke = 0;
		temp_c = 8'd25;
		wait_s(4);
		settle;
		check(!fire_mode && !door_unlock, "back to normal, door locked again");
		set_time(12, 0);
		step("Wi-Fi goes down. One entry + 19 unknown-card taps happen");
		wifi_up = 0;
		person_enters(1);
		wait_s(4);
		begin : sv2v_autoblock_2
			reg signed [31:0] i;
			for (i = 0; i < 19; i = i + 1)
				tap(0, 0);
		end
		check(ev_count == 16, "16 events kept safely in the chip memory");
		check(ev_lost == 4, "4 extra events counted as lost (never silently)");
		step("Wi-Fi comes back");
		rx_start = rx_n;
		wifi_up = 1;
		begin : sv2v_autoblock_3
			reg signed [31:0] k;
			for (k = 0; (k < 5000) && (ev_count != 0); k = k + 1)
				@(posedge clk)
					;
		end
		check((ev_count == 0) && ((rx_n - rx_start) == 16), "all 16 stored events reach the cloud");
		check((rx_code[rx_start[7:0]] == smart_room_pkg_EV_LATE) && (rx_hour[rx_start[7:0]] == 5'd12), "in the right order, with the right time (12:00 entry first)");
		set_time(16, 0);
		@(negedge clk)
			;
		temp_c = 8'd33;
		settle;
		check((fan_speed == 2) && lights_on, "1 person inside: lights on, fan on");
		step("Last person leaves and forgets the lights and fan");
		press_exit;
		door(1);
		walk_out;
		door(0);
		check((people == 0) && lights_on, "room empty, lights still on for a minute");
		wait_s(61);
		check(!lights_on && (fan_speed == 0), "after 60 s: lights and fan OFF by themselves");
		@(negedge clk)
			;
		temp_c = 8'd25;
		set_time(22, 30);
		step("Night. A student taps a valid card");
		check(night_mode, "night lock is ON (22:00-06:00)");
		tap(1, 0);
		check(!door_unlock, "normal cards don't work at night");
		expect_event(smart_room_pkg_EV_DENY_NIGHT, 1, "DENIED-NIGHT sent to cloud");
		step("Movement inside the EMPTY room");
		@(negedge clk)
			;
		motion = 1;
		settle;
		@(negedge clk)
			;
		motion = 0;
		settle;
		check(security_alarm && alarm_flags[2], "INTRUDER alarm");
		check(lights_on, "lights flash ON (scare intruder, help camera)");
		expect_event(smart_room_pkg_EV_INTRUDER, 1, "INTRUDER sent to cloud");
		tap(1, 1);
		check(!security_alarm, "admin card silences it");
		wait_s(11);
		set_time(23, 0);
		step("Someone forces the locked door open");
		door(1);
		check(security_alarm && alarm_flags[0], "DOOR FORCED alarm");
		check(buzz_mode == smart_room_pkg_BZ_ALARM, "continuous alarm sound");
		expect_event(smart_room_pkg_EV_FORCED, 1, "DOOR FORCED sent to cloud");
		door(0);
		tap(1, 1);
		check(!security_alarm, "admin card silences it");
		wait_s(11);
		step("A person inside feels unsafe and presses the PANIC button");
		press_panic;
		check(security_alarm && alarm_flags[3], "PANIC alarm");
		expect_event(smart_room_pkg_EV_PANIC, 1, "PANIC / SOS sent to cloud");
		tap(1, 1);
		wait_s(11);
		step("Gas leak");
		@(negedge clk)
			;
		gas = 1;
		wait_s(4);
		check(gas_mode && door_unlock, "GAS detected, door unlocked");
		check(vent_on && (fan_speed == 0), "exhaust vent ON, fan OFF (no sparks)");
		check(buzz_mode == smart_room_pkg_BZ_SIREN, "siren sound");
		expect_event(smart_room_pkg_EV_GAS, 1, "GAS LEAK sent to cloud");
		@(negedge clk)
			;
		gas = 0;
		wait_s(4);
		settle;
		check(!gas_mode && !door_unlock, "gas gone, back to normal");
		step("ESP32 board freezes (no heartbeat)");
		esp_alive = 0;
		wait_s(12);
		check(esp_fault, "chip notices ESP32 is DOWN within 10 s");
		expect_event(smart_room_pkg_EV_ESP_DOWN, 1, "ESP32 DOWN event stored");
		press_exit;
		check(door_unlock, "safety still works without the ESP32 (exit opens)");
		esp_alive = 1;
		wait_s(3);
		check(!esp_fault, "ESP32 back -> fault clears");
		step("End of the day");
		check((present_today == 4) && (late_today == 3), "today: 4 entries, 3 late");
		set_time(0, 0);
		check((present_today == 0) && (late_today == 0), "midnight: counters reset for the new day");
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
