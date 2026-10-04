# Project history — versions, bugs and their real root causes (keep adding at the top)

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
