// COPY this file to secrets.h (same folder) and fill in your values. secrets.h is in .gitignore and is NEVER committed.
// SARTHIK Room door board - PRIVATE settings. Never upload this file to GitHub or share it.
// Change them here only. DOOR_API_KEY must be the same as "Door key" on the website (Control page).
#pragma once
#define NET1_SSID     ""                           // home Wi-Fi (first choice)
#define NET1_PASS     ""
#define NET2_SSID     ""                         // phone hotspot (backup)
#define NET2_PASS     ""
#define NTFY_TOPIC    ""   // phone notifications (ntfy app)
#define DOOR_API_KEY  ""              // website -> door commands

// ---------------- v8.2 CLOUD KEYS (leave "" to switch a service off) ----------------
// Fill these by following STAGE3_SETUP_GUIDE.txt, step by step. Copy-paste between the quotes only.
#define FB_API_KEY       ""   // Firebase: Project settings > General > Web API Key   (starts with AIza)
#define FB_PROJECT_ID    ""   // Firebase: Project settings > General > Project ID    (e.g. sarthik-room-1234)
#define FB_DEVICE_EMAIL  ""   // the door board's own login you create in Firebase Authentication
#define FB_DEVICE_PASS   ""   // its password
#define TS_WRITE_KEY_A   ""   // ThingSpeak channel "Room sensors A" > API Keys > Write API Key (16 letters)
#define TS_WRITE_KEY_B   ""   // ThingSpeak channel "Room sensors B" > API Keys > Write API Key
#define BLYNK_TOKEN      ""   // Blynk: device > Device info > BLYNK_AUTH_TOKEN (32 letters)
#define TG_BOT_TOKEN     ""   // Telegram: from @BotFather, looks like 123456789:AAH...
#define TG_CHAT_ID       ""   // Telegram: your chat number from @userinfobot, e.g. 987654321
#define SHEET_HOOK       ""   // Google Sheet web app URL + ?key=... (Setup / Fix 4 fills it)
#define QR_SECRET        ""   // visitor-pass signing key: SAME text as SECRET_KEY in tools/visitor_pass/make_pass.py
