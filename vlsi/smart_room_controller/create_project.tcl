# ONE-CLICK SETUP for Vivado.
# In the Vivado Tcl Console type:
#   cd C:/Smart_Room_Controller
#   source create_project.tcl
# It creates the project, adds all files, runs the "one full day" test
# and opens the waveform with the important signals already added.

set root [file dirname [file normalize [info script]]]
set proj smart_room_controller

create_project $proj $root/vivado_project -part xc7a35tcpg236-1 -force
set_property target_language Verilog [current_project]

# Design files (the chip)
add_files -norecurse [list \
  $root/rtl/smart_room_pkg.sv \
  $root/rtl/tick_gen.sv \
  $root/rtl/sync_edge.sv \
  $root/rtl/debounce.sv \
  $root/rtl/door_fsm.sv \
  $root/rtl/people_counter.sv \
  $root/rtl/room_comfort.sv \
  $root/rtl/event_logger.sv \
  $root/rtl/buzzer.sv \
  $root/rtl/smart_room_top.sv ]
set_property top smart_room_top [get_filesets sources_1]

# Clock speed target (100 MHz) for the timing report
add_files -fileset constrs_1 -norecurse $root/constraints/clock.xdc

# Test file (the "one day" story)
add_files -fileset sim_1 -norecurse $root/tb/tb_smart_room.sv
set_property top tb_smart_room [get_filesets sim_1]
set_property top_lib xil_defaultlib [get_filesets sim_1]

# Simulation: record every signal, start at time 0, we press "run all" ourselves
set_property -name {xsim.simulate.log_all_signals} -value {true} -objects [get_filesets sim_1]
set_property -name {xsim.simulate.runtime} -value {1ns} -objects [get_filesets sim_1]

# Synthesis without a board: no pin assignment needed (out-of-context)
set_property -name {STEPS.SYNTH_DESIGN.ARGS.MORE OPTIONS} -value {-mode out_of_context} -objects [get_runs synth_1]

update_compile_order -fileset sources_1
update_compile_order -fileset sim_1

# Run the test
launch_simulation

# Put the interesting signals in the waveform window
catch {
  add_wave_divider "TIME and PEOPLE"
  add_wave -radix unsigned /tb_smart_room/hour /tb_smart_room/minute /tb_smart_room/people
  add_wave_divider "DOOR"
  add_wave /tb_smart_room/card_tap /tb_smart_room/face_ok /tb_smart_room/face_bad /tb_smart_room/door_unlock /tb_smart_room/locked_out /tb_smart_room/held_warn
  add_wave_divider "SOUND (0 off,1 ok,2 no,3 warn,4 alarm,5 siren)"
  add_wave -radix unsigned /tb_smart_room/buzz_mode
  add_wave /tb_smart_room/buzzer
  add_wave_divider "ROOM"
  add_wave /tb_smart_room/lights_on
  add_wave -radix unsigned /tb_smart_room/fan_speed /tb_smart_room/temp_c
  add_wave /tb_smart_room/vent_on
  add_wave_divider "SAFETY and SECURITY"
  add_wave /tb_smart_room/fire_mode /tb_smart_room/gas_mode /tb_smart_room/evac_done /tb_smart_room/security_alarm
  add_wave -radix binary /tb_smart_room/alarm_flags
  add_wave /tb_smart_room/night_mode /tb_smart_room/esp_fault
  add_wave_divider "CLOUD EVENTS"
  add_wave /tb_smart_room/wifi_up /tb_smart_room/ev_valid
  add_wave -radix unsigned /tb_smart_room/ev_count /tb_smart_room/ev_lost /tb_smart_room/present_today /tb_smart_room/late_today
}

run all
puts ""
puts "DONE. Scroll up in the Tcl Console to read the PASS lines."
puts "In the waveform window click 'Zoom Fit' to see the whole day."
