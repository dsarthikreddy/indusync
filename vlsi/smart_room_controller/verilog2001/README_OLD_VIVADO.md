# Smart Room Controller for OLD Vivado (2014.x) — plain Verilog files

Why this folder exists: the files in `../rtl` and `../tb` are modern SystemVerilog (`.sv`). Vivado 2014.4 (college Linux PCs)
understands only part of SystemVerilog. The files here are the SAME design converted to plain Verilog-2005 (`.v`).

Tested (in the cloud, NOT in Vivado 2014.4 itself): Icarus Verilog in strict `-g2005` mode and Verilator both print
**ALL 88 TESTS PASSED**, identical to the SystemVerilog version; the Yosys Verilog-2005 parser reads the design without errors.
If Vivado 2014.4 shows any error, photograph the Messages / Tcl Console and send it to Claude.

Folder: `rtl/` = 9 chip files, `tb/tb_smart_room.v` = the test (88 checks), `../constraints/clock.xdc` = 100 MHz clock.

## 1. Start Vivado (type WITHOUT the `$`, that is only the prompt)
```
csh
source ~/vivado.csh
vivado &
```

## 2. Create the project
1. Quick Start -> **Create New Project** -> **Next**.
2. Project name `smart_room`, location = a folder with no spaces (for example your home folder) -> **Next**.
3. **RTL Project** -> tick **Do not specify sources at this time** (if shown) -> **Next**.
4. On **Default Part** choose `xc7a35tcpg236-1` (Artix-7). If it is missing, any Artix-7 part is fine -> **Next** -> **Finish**.

## 3. Add the chip (design sources)
1. In the **Sources** panel right-click **Design Sources** -> **Add Sources...**.
2. **Add or Create Design Sources** -> **Next** -> **Add Files...**.
3. Open `verilog2001/rtl`, select ALL 9 `.v` files (Ctrl+A) -> **OK**.
4. Tick **Copy sources into project** (your originals stay safe) -> **Finish**.
5. Expand `smart_room_top` to see the hierarchy.

## 4. Add the test (simulation source)
1. Right-click **Simulation Sources** -> **Add Sources...** -> **Add or Create Simulation Sources** -> **Next**.
2. **Add Files...** -> `verilog2001/tb/tb_smart_room.v` -> **OK** -> tick **Copy sources** -> **Finish**.
3. Under **Simulation Sources -> sim_1** right-click `tb_smart_room` -> **Set as Top**.

## 5. Run
1. Flow Navigator -> **Simulation** -> **Run Simulation** -> **Run Behavioral Simulation**.
2. When the waveform opens, click the **Tcl Console** and type `run all` + Enter (the default run time is only 1 us).
3. Scroll the console: PASS lines for every scene, last line **ALL 88 TESTS PASSED**.

## 6. Waveform
1. In **Scope** click `tb_smart_room`; in **Objects** select signals (Ctrl+click), right-click -> **Add To Wave Window**.
2. Click **Zoom Fit**. After `run all` the time bar shows about 227 us = the whole simulated day.

## 7. Experiments (this is how you learn)
- Open `door_fsm.v` and change a number (lockout time). Save. Click **Restart** (circular arrow in the simulation toolbar), `run all`: matching `FAIL` lines appear. Change it back: 88 PASS again.
- Read `tb_smart_room.v`: every `check(...)` is one test. Compare with the 25 problems in `../README.md`.
- Add a new signal (`ev_count`, `fan_speed`, `temp_c`) to the waveform and follow it through the day.
- **Schematic:** Flow Navigator -> **Elaborated Design** (or RTL Analysis) -> **Schematic**.
- **Size and speed:** **Add Sources -> Add or Create Constraints** -> `../constraints/clock.xdc`; then **Run Synthesis**; open the synthesized design; **Report Utilization** and **Report Timing Summary**.
- Write your own small module (counter, enum-style FSM) with its own testbench using the same steps.

## 8. If the compile fails ("compile step failed", VRFC errors)
"module ... ignored due to previous errors" is NOT the real error. Find the FIRST error:
1. Open the **Log** tab (or **Tcl Console**) at the bottom and scroll up to the first line that starts with `ERROR: [VRFC ...`.
2. Or open the file `<project folder>/smart_room.sim/sim_1/behav/xvlog.log` and read the first `ERROR`.
3. Take a photo of that first error (it names the file and line number) and send it to Claude.

## 8. Known differences
- Output text is identical to the SystemVerilog run (`../docs/expected_output.txt`); numbers of LUTs may differ slightly in old Vivado.
- Do not edit the converted files and the `.sv` originals at the same time: pick one set.
