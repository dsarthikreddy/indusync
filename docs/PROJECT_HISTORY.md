# Project history — versions, bugs and their real root causes (keep adding at the top)

## VLSI: Smart Room Controller chip (2026-10-05)
- NEW: SystemVerilog hardware controller for door safety, synthesized for Artix-7 FPGA (xc7a35t). 10 modules: tick_gen, sync_edge,
  debounce, door_fsm (finite state machine), people_counter (bidirectional beams + tailgating), room_comfort (fan/lights/vent),
  event_logger (16-entry FIFO priority queue), buzzer (5 sounds), smart_room_top (integration). 88-check self-checking testbench
  simulates one full day (08:50-23:00) with all edge cases. Verified in Verilator (lint-clean), Icarus Verilog (all PASS), ready for
  Vivado xsim. One-click setup via create_project.tcl. Mutation testing: 8 deliberately injected bugs, all caught by testbench.
  VERIFIED on the owner's laptop in Vivado 2026.1 (free BASIC licence): ALL 88 TESTS PASSED, implementation OK, WNS +4.565 ns at
  100 MHz, 378 LUTs, 383 FFs, 0.075 W. Setup notes: extracting the ZIP created a nested folder Smart_Room_Controller\Smart_Room_Controller
  (cd into the inner one); the Timing/Utilization menu items stay greyed out until an implemented design is open.
  College Linux PCs have Vivado 2014.4 (no real SystemVerilog support): added vlsi/smart_room_controller/verilog2001/ = same design converted with
  sv2v to plain Verilog-2005 (+ hand-fixed testbench strings and 2 constant expressions in event_logger). Icarus -g2005 and Verilator both print
  ALL 88 TESTS PASSED, output identical to expected_output.txt. NOT tested in Vivado 2014.4 itself.
  First try on 2014.4 failed to compile event_logger.v (real first error not seen yet); hardened: removed sv2v helper reg _sv2v_0, $clog2 -> own clog2 function, 1'sb0 -> 1'b0, no 'automatic' functions.
  Honest limits: simulation only (FPGA board not yet wired); light beams, motion, gas, temp, air sensors are mocked; one "second" =
  100 clock cycles in test (real = 100 MHz).

## Firmware 8.2.5 / Sheet script (2026-10-03)
- Board->Sheet POST showed "code 400": HTTPClient follows HTTP 302 even in STRICT mode (rewrites POST to GET) and lands on a
  Google 400 page although doPost already ran -> board retried forever. FIX: POST uses HTTPC_DISABLE_FOLLOW_REDIRECTS, 302 = success.
- Entry e-mails missing when the instant path failed: route() added rows but never e-mailed entries. FIX: route() calls noticeEvent_().
- Blynk "add V10": Blynk answers 400 "Requested pin doesn't have any value yet" for a never-pressed pin. FIX: treat as 0, write V10=0 once.
## Fix 5
- Apps Script web app 403/400 for everyone (UptimeRobot "Forbidden", n8n "not run yet"): code used GmailApp; the user's
  granular consent left Gmail unticked, so Google blocked the whole web app. FIX: removed GmailApp; route() every 1 min as safety net.
- Board re-probes missing Blynk pins every 10 min (was 6 h); instant Sheet path skips events older than 10 min.
## Fix 4 (fw 8.2.3)
- New: instant Telegram "ENTRY:" for card+face / phone key / QR; instant board->Sheet POST (row + e-mail); laptop webhook "all" with
  Sheet-side filter + 30-min throttle; e-mail cap; Blynk missing-pin learning; ThingSpeak B field 8 = visitors; SHEET_HOOK in secrets.h.
## Fix 3 (fw 8.2.2) — the big one
- All cloud calls "no answer", website "offline", log "esp-sha: Failed to allocate buf memory": TLS buffers (2 x 16 KB) were in
  internal RAM (only ~20-70 KB free). FIX: mbedtls_platform_set_calloc_free -> buffers >=1 KB in PSRAM; memory gate before TLS;
  error codes shown on the website; service messages JSON-escaped (a quote in a message broke the status update).
## Manual edits (fw 8.2.1 -> numbers)
- Command poll 10->15 s, Telegram poll 15->30 s, console lines 40->12, max backoff 5 min->60 s, HTTP timeout 8->15 s.
## Fix 2 (setup)
- Setup created ThingSpeak channels BEFORE checking the password -> each failed run left 2 channels (hit the 4-channel limit).
  FIX: password first, re-use channels by name, visible-typing retry.
## Fix 1 (fw 8.2.1)
- Firebase "login: bad answer": JSON reader stopped at 600 chars; real ID tokens are ~1000 chars. Tests had used a 4-char fake token.
- Telegram never answered: 10-digit chat id overflowed a 32-bit long. FIX: compare as text.
- n8n showed 0 workflows: manual import used a different n8n user folder. FIX: setup imports into 5_n8n/n8n_data; workflows are
  timer + HTTP request only (no Code node / env vars, works on n8n 2.x).
- Security self-check showed 5/6: the TEST was wrong (admin "changing" role to admin). Rules were always correct.
- New: website Board page (OLED mirror + Serial lines), help boxes, PWA/offline, Sentry reporter, UptimeRobot via setup.
## Stage 3 build (fw 8.2)
- 15 sensor slots, Firebase/ThingSpeak/Blynk/Telegram, Firebase site + rules, Apps Script router, n8n, setup wizard. No-card free plan.
## Earlier
- fw 8.1: network work moved to core-0 task, coex prefer BT (fixed BLE/QR degradation), website commands, learncard, better QR reader.
- Laptop website v4 "SARTHIK Room": security hardening, Control page, chat, autostart, backups.
- fw v6/v3: RFID + on-board face recognition + attendance + servo door + OLED (original stage 1).
