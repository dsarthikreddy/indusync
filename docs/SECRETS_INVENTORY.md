# Secrets inventory — NAMES and LOCATIONS only (values live only on the owner's laptop)
Everything below exists and works on the laptop; Claude must never ask for the values or commit them.
| Secret | Where on the laptop (C:\SARTHIK_Room_Stage3\...) | Used by |
|---|---|---|
| NET1_SSID/PASS (home Wi-Fi "ACT" network), NET2_SSID/PASS (phone hotspot) | 1_Door_Board_v8_2\classync_stage1_v8_2\secrets.h | board Wi-Fi |
| DOOR_API_KEY | secrets.h AND laptop website Settings (door_key) — must be equal | laptop website -> board |
| NTFY_TOPIC | secrets.h + laptop website settings | old phone alerts (ntfy app) |
| QR_SECRET | must be added to secrets.h; equals SECRET_KEY in 6_Visitor_Pass_Maker\make_pass.py | visitor QR passes |
| FB_API_KEY, FB_PROJECT_ID (project id: sarthik-room), FB_DEVICE_EMAIL/PASS | secrets.h (+ public web config in 4_Cloud_Website\public\config.js) | board -> Firestore |
| TS_WRITE_KEY_A/B (channels 3519220 / 3519221) | secrets.h | ThingSpeak |
| BLYNK_TOKEN (region BLR1) | secrets.h | Blynk |
| TG_BOT_TOKEN, TG_CHAT_ID | secrets.h + Google Sheet Script Properties | Telegram |
| SHEET_HOOK (Sheet /exec URL + ?key=HOOK_KEY) | secrets.h; laptop website webhook_url; 5_n8n\n8n.env | board/laptop/n8n -> Sheet |
| Reporter login, HOOK_KEY, SENTRY_DSN, (WA_TOKEN later) | Google Sheet: Extensions > Apps Script > Project Settings > Script Properties | Sheet script |
| Admin website password, device/reporter logins | tools\stage3_state.json (setup state, SECRET) | setup wizard |
| UptimeRobot / Sentry / Blynk / ThingSpeak / Meta accounts | the owner's Gmail-based accounts | dashboards |
Rotate any secret that ever appears in a screenshot or chat: create a new one in that service, re-run Setup, upload firmware.
