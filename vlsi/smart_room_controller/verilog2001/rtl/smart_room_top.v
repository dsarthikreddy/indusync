`timescale 1ns/1ps
// Auto-converted from ../../rtl/*.sv (SystemVerilog) to plain Verilog-2005 for old Vivado (2014.x). Edit the .sv originals if you can.
module smart_room_top (
	clk,
	rst_n,
	card_tap,
	card_known,
	card_admin,
	face_ok,
	face_bad,
	esp_heartbeat,
	hour,
	minute,
	exit_btn,
	panic_btn,
	door_closed,
	beam_out,
	beam_in,
	motion,
	smoke,
	gas,
	temp_c,
	air_q,
	ev_ack,
	door_unlock,
	buzzer,
	buzz_mode,
	lights_on,
	fan_speed,
	vent_on,
	fire_mode,
	gas_mode,
	evac_done,
	night_mode,
	security_alarm,
	alarm_flags,
	locked_out,
	held_warn,
	room_full,
	people,
	esp_fault,
	present_today,
	late_today,
	ev_valid,
	ev_data,
	ev_count,
	ev_lost
);
	reg _sv2v_0;
	parameter signed [31:0] CLKS_PER_SEC = 100000000;
	parameter signed [31:0] MAX_PEOPLE = 40;
	parameter signed [31:0] CLASS_START_MIN = 540;
	parameter signed [31:0] LATE_GRACE_MIN = 10;
	parameter signed [31:0] CLASS_END_MIN = 1020;
	parameter signed [31:0] NIGHT_START_H = 22;
	parameter signed [31:0] NIGHT_END_H = 6;
	parameter signed [31:0] SMOKE_CONFIRM_S = 3;
	parameter signed [31:0] ESP_TIMEOUT_S = 10;
	parameter signed [31:0] EMPTY_OFF_S = 60;
	parameter signed [31:0] EVENT_DEPTH = 16;
	input wire clk;
	input wire rst_n;
	input wire card_tap;
	input wire card_known;
	input wire card_admin;
	input wire face_ok;
	input wire face_bad;
	input wire esp_heartbeat;
	input wire [4:0] hour;
	input wire [5:0] minute;
	input wire exit_btn;
	input wire panic_btn;
	input wire door_closed;
	input wire beam_out;
	input wire beam_in;
	input wire motion;
	input wire smoke;
	input wire gas;
	input wire [7:0] temp_c;
	input wire [7:0] air_q;
	input wire ev_ack;
	output wire door_unlock;
	output wire buzzer;
	output wire [2:0] buzz_mode;
	output wire lights_on;
	output wire [1:0] fan_speed;
	output wire vent_on;
	output wire fire_mode;
	output wire gas_mode;
	output wire evac_done;
	output wire night_mode;
	output wire security_alarm;
	output wire [3:0] alarm_flags;
	output wire locked_out;
	output wire held_warn;
	output wire room_full;
	output wire [5:0] people;
	output reg esp_fault;
	output reg [7:0] present_today;
	output reg [7:0] late_today;
	output wire ev_valid;
	output wire [23:0] ev_data;
	output wire [$clog2(EVENT_DEPTH):0] ev_count;
	output wire [7:0] ev_lost;
	localparam signed [31:0] DEB_N = ((CLKS_PER_SEC / 50) > 0 ? CLKS_PER_SEC / 50 : 1);
	wire tick;
	wire beat;
	tick_gen #(.CLKS_PER_SEC(CLKS_PER_SEC)) u_tick(
		.clk(clk),
		.rst_n(rst_n),
		.beat(beat),
		.tick(tick)
	);
	wire tap_r;
	wire fok_r;
	wire fbad_r;
	wire hb_r;
	wire ack_r;
	wire known_l;
	wire admin_l;
	wire tap_l;
	wire fok_l;
	wire fbad_l;
	wire hb_l;
	wire ack_l;
	wire known_r;
	wire admin_r;
	sync_edge u_s_tap(
		.clk(clk),
		.rst_n(rst_n),
		.din(card_tap),
		.level(tap_l),
		.rise(tap_r)
	);
	sync_edge u_s_known(
		.clk(clk),
		.rst_n(rst_n),
		.din(card_known),
		.level(known_l),
		.rise(known_r)
	);
	sync_edge u_s_admin(
		.clk(clk),
		.rst_n(rst_n),
		.din(card_admin),
		.level(admin_l),
		.rise(admin_r)
	);
	sync_edge u_s_fok(
		.clk(clk),
		.rst_n(rst_n),
		.din(face_ok),
		.level(fok_l),
		.rise(fok_r)
	);
	sync_edge u_s_fbad(
		.clk(clk),
		.rst_n(rst_n),
		.din(face_bad),
		.level(fbad_l),
		.rise(fbad_r)
	);
	sync_edge u_s_hb(
		.clk(clk),
		.rst_n(rst_n),
		.din(esp_heartbeat),
		.level(hb_l),
		.rise(hb_r)
	);
	sync_edge u_s_ack(
		.clk(clk),
		.rst_n(rst_n),
		.din(ev_ack),
		.level(ack_l),
		.rise(ack_r)
	);
	wire exit_db;
	wire panic_db;
	wire closed_db;
	wire bout_db;
	wire bin_db;
	wire motion_db;
	wire smoke_db;
	wire gas_db;
	debounce #(.N(DEB_N)) u_d_exit(
		.clk(clk),
		.rst_n(rst_n),
		.en(1'b1),
		.din(exit_btn),
		.dout(exit_db)
	);
	debounce #(.N(DEB_N)) u_d_panic(
		.clk(clk),
		.rst_n(rst_n),
		.en(1'b1),
		.din(panic_btn),
		.dout(panic_db)
	);
	debounce #(
		.N(DEB_N),
		.INIT(1'b1)
	) u_d_door(
		.clk(clk),
		.rst_n(rst_n),
		.en(1'b1),
		.din(door_closed),
		.dout(closed_db)
	);
	debounce #(.N(DEB_N)) u_d_bout(
		.clk(clk),
		.rst_n(rst_n),
		.en(1'b1),
		.din(beam_out),
		.dout(bout_db)
	);
	debounce #(.N(DEB_N)) u_d_bin(
		.clk(clk),
		.rst_n(rst_n),
		.en(1'b1),
		.din(beam_in),
		.dout(bin_db)
	);
	debounce #(.N(DEB_N)) u_d_mot(
		.clk(clk),
		.rst_n(rst_n),
		.en(1'b1),
		.din(motion),
		.dout(motion_db)
	);
	debounce #(.N(SMOKE_CONFIRM_S)) u_d_smoke(
		.clk(clk),
		.rst_n(rst_n),
		.en(tick),
		.din(smoke),
		.dout(smoke_db)
	);
	debounce #(.N(SMOKE_CONFIRM_S)) u_d_gas(
		.clk(clk),
		.rst_n(rst_n),
		.en(tick),
		.din(gas),
		.dout(gas_db)
	);
	reg exit_q;
	reg panic_q;
	reg motion_q;
	reg fire_q;
	reg gas_q;
	reg evac_q;
	wire exit_press;
	wire panic_press;
	wire motion_rise;
	always @(posedge clk or negedge rst_n)
		if (!rst_n) begin
			exit_q <= 1'b0;
			panic_q <= 1'b0;
			motion_q <= 1'b0;
			fire_q <= 1'b0;
			gas_q <= 1'b0;
			evac_q <= 1'b0;
		end
		else begin
			exit_q <= exit_db;
			panic_q <= panic_db;
			motion_q <= motion_db;
			fire_q <= fire_mode;
			gas_q <= gas_mode;
			evac_q <= evac_done;
		end
	assign exit_press = exit_db & ~exit_q;
	assign panic_press = panic_db & ~panic_q;
	assign motion_rise = motion_db & ~motion_q;
	reg [4:0] hr;
	reg [5:0] mn;
	wire [10:0] tod;
	reg [10:0] tod_q;
	wire is_late;
	wire day_reset;
	always @(posedge clk or negedge rst_n)
		if (!rst_n) begin
			hr <= 1'sb0;
			mn <= 1'sb0;
			tod_q <= 1'sb0;
		end
		else begin
			hr <= hour;
			mn <= minute;
			tod_q <= tod;
		end
	function automatic [10:0] sv2v_cast_11;
		input reg [10:0] inp;
		sv2v_cast_11 = inp;
	endfunction
	assign tod = (sv2v_cast_11(hr) * 11'd60) + sv2v_cast_11(mn);
	function automatic signed [4:0] sv2v_cast_5_signed;
		input reg signed [4:0] inp;
		sv2v_cast_5_signed = inp;
	endfunction
	assign night_mode = (hr >= sv2v_cast_5_signed(NIGHT_START_H)) || (hr < sv2v_cast_5_signed(NIGHT_END_H));
	function automatic signed [10:0] sv2v_cast_11_signed;
		input reg signed [10:0] inp;
		sv2v_cast_11_signed = inp;
	endfunction
	assign is_late = (tod > sv2v_cast_11_signed(CLASS_START_MIN + LATE_GRACE_MIN)) && (tod < sv2v_cast_11_signed(CLASS_END_MIN));
	assign day_reset = (tod == 11'd0) && (tod_q != 11'd0);
	wire emergency;
	assign fire_mode = smoke_db;
	assign gas_mode = gas_db;
	assign emergency = fire_mode || gas_mode;
	wire grant;
	wire deny;
	wire ev_unknown;
	wire ev_full;
	wire ev_night;
	wire ev_lockout;
	wire ev_forced;
	wire ev_held;
	door_fsm u_door(
		.clk(clk),
		.rst_n(rst_n),
		.tick(tick),
		.card_tap(tap_r),
		.card_known(known_l),
		.card_admin(admin_l),
		.face_ok(fok_r),
		.face_bad(fbad_r),
		.exit_press(exit_press),
		.door_closed(closed_db),
		.night(night_mode),
		.room_full(room_full),
		.emergency(emergency),
		.unlock(door_unlock),
		.locked_out(locked_out),
		.held_warn(held_warn),
		.grant(grant),
		.deny(deny),
		.ev_unknown(ev_unknown),
		.ev_full(ev_full),
		.ev_night(ev_night),
		.ev_lockout(ev_lockout),
		.ev_forced(ev_forced),
		.ev_held(ev_held)
	);
	wire pass_in;
	wire pass_out;
	wire tailgate;
	people_counter #(.MAX_PEOPLE(MAX_PEOPLE)) u_people(
		.clk(clk),
		.rst_n(rst_n),
		.tick(tick),
		.beam_out(bout_db),
		.beam_in(bin_db),
		.grant(grant),
		.emergency(emergency),
		.people(people),
		.full(room_full),
		.pass_in(pass_in),
		.pass_out(pass_out),
		.tailgate(tailgate)
	);
	assign evac_done = fire_mode && (people == 6'd0);
	wire admin_tap;
	wire ev_intruder;
	reg f_forced;
	reg f_tail;
	reg f_intr;
	reg f_panic;
	assign admin_tap = (tap_r && known_l) && admin_l;
	assign ev_intruder = (night_mode && (people == 6'd0)) && motion_rise;
	always @(posedge clk or negedge rst_n)
		if (!rst_n) begin
			f_forced <= 1'b0;
			f_tail <= 1'b0;
			f_intr <= 1'b0;
			f_panic <= 1'b0;
		end
		else begin
			if (admin_tap) begin
				f_forced <= 1'b0;
				f_tail <= 1'b0;
				f_intr <= 1'b0;
				f_panic <= 1'b0;
			end
			if (ev_forced)
				f_forced <= 1'b1;
			if (tailgate)
				f_tail <= 1'b1;
			if (ev_intruder)
				f_intr <= 1'b1;
			if (panic_press)
				f_panic <= 1'b1;
		end
	assign alarm_flags = {f_panic, f_intr, f_tail, f_forced};
	assign security_alarm = |alarm_flags;
	reg [7:0] hb_t;
	reg ev_esp;
	function automatic signed [7:0] sv2v_cast_8_signed;
		input reg signed [7:0] inp;
		sv2v_cast_8_signed = inp;
	endfunction
	always @(posedge clk or negedge rst_n)
		if (!rst_n) begin
			hb_t <= 1'sb0;
			esp_fault <= 1'b0;
			ev_esp <= 1'b0;
		end
		else begin
			ev_esp <= 1'b0;
			if (hb_r) begin
				hb_t <= 1'sb0;
				esp_fault <= 1'b0;
			end
			else if (tick && !esp_fault) begin
				if (hb_t == sv2v_cast_8_signed(ESP_TIMEOUT_S - 1)) begin
					esp_fault <= 1'b1;
					ev_esp <= 1'b1;
				end
				else
					hb_t <= hb_t + 8'd1;
			end
		end
	always @(posedge clk or negedge rst_n)
		if (!rst_n) begin
			present_today <= 1'sb0;
			late_today <= 1'sb0;
		end
		else if (day_reset) begin
			present_today <= 1'sb0;
			late_today <= 1'sb0;
		end
		else if (grant) begin
			if (present_today != 8'hff)
				present_today <= present_today + 8'd1;
			if (is_late && (late_today != 8'hff))
				late_today <= late_today + 8'd1;
		end
	room_comfort #(.EMPTY_OFF_S(EMPTY_OFF_S)) u_comfort(
		.clk(clk),
		.rst_n(rst_n),
		.tick(tick),
		.occupied(people != 6'd0),
		.motion(motion_db),
		.fire(fire_mode),
		.gas(gas_mode),
		.temp_c(temp_c),
		.air_q(air_q),
		.lights_on(lights_on),
		.fan_speed(fan_speed),
		.vent_on(vent_on)
	);
	buzzer u_buzz(
		.clk(clk),
		.rst_n(rst_n),
		.beat(beat),
		.ok(grant),
		.deny(deny),
		.warn(held_warn),
		.alarm(security_alarm),
		.siren(emergency),
		.buzz(buzzer),
		.mode(buzz_mode)
	);
	reg [15:0] ev_req;
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
	always @(*) begin
		if (_sv2v_0)
			;
		ev_req = 1'sb0;
		ev_req[smart_room_pkg_EV_ENTRY] = grant && !is_late;
		ev_req[smart_room_pkg_EV_LATE] = grant && is_late;
		ev_req[smart_room_pkg_EV_DENY_FULL] = ev_full;
		ev_req[smart_room_pkg_EV_DENY_NIGHT] = ev_night;
		ev_req[smart_room_pkg_EV_UNKNOWN] = ev_unknown;
		ev_req[smart_room_pkg_EV_LOCKOUT] = ev_lockout;
		ev_req[smart_room_pkg_EV_HELD_OPEN] = ev_held;
		ev_req[smart_room_pkg_EV_ESP_DOWN] = ev_esp;
		ev_req[smart_room_pkg_EV_TAILGATE] = tailgate;
		ev_req[smart_room_pkg_EV_FORCED] = ev_forced;
		ev_req[smart_room_pkg_EV_INTRUDER] = ev_intruder;
		ev_req[smart_room_pkg_EV_EVAC_DONE] = evac_done && !evac_q;
		ev_req[smart_room_pkg_EV_PANIC] = panic_press;
		ev_req[smart_room_pkg_EV_GAS] = gas_mode && !gas_q;
		ev_req[smart_room_pkg_EV_FIRE] = fire_mode && !fire_q;
	end
	event_logger #(.DEPTH(EVENT_DEPTH)) u_log(
		.clk(clk),
		.rst_n(rst_n),
		.ev_req(ev_req),
		.hour(hr),
		.minute(mn),
		.people(people),
		.ev_ack(ack_r),
		.ev_valid(ev_valid),
		.ev_data(ev_data),
		.ev_count(ev_count),
		.ev_lost(ev_lost)
	);
	wire unused_ok;
	assign unused_ok = &{1'b0, tap_l, fok_l, fbad_l, hb_l, ack_l, known_r, admin_r, pass_in, pass_out};
	initial _sv2v_0 = 0;
endmodule
