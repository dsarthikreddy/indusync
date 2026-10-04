import sys, json, re, builtins, base64, importlib.util, os
from pathlib import Path
spec = importlib.util.spec_from_file_location("wz", "/tmp/wz2/pkg/tools/setup_stage3.py"); wz = importlib.util.module_from_spec(spec); spec.loader.exec_module(wz)
wz.Path.home = staticmethod(lambda: Path("/tmp/wz2/home"))
# the user's REAL current state: everything done, ThingSpeak + WhatsApp skipped, old Sheet code, door key mismatch
st = {"done": {k: True for k in ["firebase","deploy","accounts","thingspeak","blynk","telegram","whatsapp","files","sheet","link"]},
      "fb": {"apiKey": "AIzaFAKE-FAKEfakeFAKEfakeFAKEfake00000", "authDomain": "sarthik-room.firebaseapp.com", "projectId": "sarthik-room", "appId": "1:2:web:3"},
      "site": "https://sarthik-room.web.app", "acc": {"admin": "owner@gmail.com", "device": {"email": "door-1.board@sarthik.room", "pass": "Sr-devpass"}, "reporter": {"email": "sheet.reporter@sarthik.room", "pass": "Sr-reppass"}},
      "blynk": "b"*32, "tg": {"token": "123:AAH"+"x"*30, "chat": "1234567890", "bot": "x_bot"}, "hook": "a1b2"*8,
      "sheet_url": "https://script.google.com/macros/s/AKfyTEST/exec", "notify": "sarthikreddy,goutam,rozy", "report_to": "owner@gmail.com", "sheet_code": "old"}
Path("/tmp/wz2/pkg/tools/stage3_state.json").write_text(json.dumps(st))
LOG, MON, IMPORTED, DEPLOYS = [], [], [], []
SHEET_NEW = [False]
def fake_http(method, url, data=None, headers=None, form=False, timeout=25):
    LOG.append(url.split("?")[0])
    if "signInWithPassword" in url: return 200, json.dumps({"idToken": "TOK"})
    if "firestore.googleapis.com" in url: return 200, "{}"
    if "thingspeak.com/channels.json" in url and method == "GET": return 200, "[]"
    if "thingspeak.com/channels.json" in url:
        n = 3000 + len([l for l in LOG if "channels.json" in l])
        return 200, json.dumps({"id": n, "api_keys": [{"api_key": "W%015d" % n, "write_flag": True}, {"api_key": "R%015d" % n, "write_flag": False}]})
    if "getAlertContacts" in url: return 200, json.dumps({"stat": "ok", "alert_contacts": [{"id": "777", "type": 2}]})
    if "getMonitors" in url: return 200, json.dumps({"stat": "ok", "monitors": [m for m in MON]})
    if "newMonitor" in url: MON.append({"friendly_name": data["friendly_name"]}); LOG.append(json.dumps(data)); return 200, json.dumps({"stat": "ok", "monitor": {"id": 1}})
    if "action=uptime" in url: return (200, '{"ok":true,"door_online":true}') if SHEET_NEW[0] else (200, '{"ok":false,"error":"wrong key"}')
    return 404, ""
wz.http = fake_http
def fake_run(args, **k):
    if "import:workflow" in " ".join(map(str, args)): IMPORTED.append((str(args[-1]), k.get("env", {}).get("N8N_USER_FOLDER")))
    if "deploy" in args: DEPLOYS.append(args)
    return type("R", (), {"returncode": 0})()
wz.subprocess.run = fake_run; wz.shutil.which = lambda x: "/usr/bin/npx"
ans = ["y", "ABCD1234EFGH5678", "Str0ng#Password",            # ThingSpeak: yes, key, admin password
       "n",                                                     # WhatsApp: not now
       "y", "https://0123456789abcdef0123456789abcdef@o4508.ingest.us.sentry.io/4508123",   # Sentry
       "y", "u1234567-abcdefabcdefabcdefabcdef",              # UptimeRobot main key
       "", ""]                                                  # Sheet: first Enter (old version still live), second Enter (new)
def inp(p=""):
    if "When steps 1-5 are done" in p and ans and ans[0] == "" and len([a for a in ans if a == ""]) == 1: SHEET_NEW[0] = True
    return ans.pop(0)
builtins.input = inp; wz.getpass.getpass = inp
wz.main()
s2 = json.loads(Path("/tmp/wz2/pkg/tools/stage3_state.json").read_text())
res = []
def chk(n, c, x=""): res.append(bool(c)); print(("  PASS  " if c else "  FAIL  ") + n + ("" if c else "  -> " + str(x)[:300]))
chk("website re-published (first run) and finally WITH the Sentry setting", len(DEPLOYS) >= 1, DEPLOYS)
chk("ThingSpeak (skipped before) now set up: 2 channels", len(s2.get("ts", [])) == 2)
sec = Path("/tmp/wz2/pkg/1_Door_Board_v8_2/classync_stage1_v8_2/secrets.h").read_text()
vals = dict(re.findall(r'#define\s+(\w+)\s+"([^"]*)"', sec))
chk("board secrets.h: ThingSpeak keys added, Telegram/Blynk kept", vals["TS_WRITE_KEY_A"].startswith("W") and vals["TG_CHAT_ID"] == "1234567890" and vals["BLYNK_TOKEN"] == "b"*32, vals)
code = json.loads(base64.b64decode(Path("/tmp/wz2/pkg/tools/sheet_settings_code.txt").read_text()))
chk("new Sheet settings code includes Sentry + same web key + same people", code["SENTRY_DSN"].startswith("https://0123") and code["HOOK_KEY"] == "a1b2"*8 and code["NOTIFY_PEOPLE"] == "sarthikreddy,goutam,rozy", code)
cfg = Path("/tmp/wz2/pkg/4_Cloud_Website/public/config.js").read_text()
chk("website config keeps Firebase + adds Sentry DSN", '"projectId": "sarthik-room"' in cfg and '"sentryDsn": "https://0123' in cfg, cfg)
chk("UptimeRobot: 2 monitors created with your e-mail alert contact", [m["friendly_name"] for m in MON] == ["SARTHIK Room website", "SARTHIK Room door board"] and all('"alert_contacts": "777_0_0"' in l for l in LOG if "newMonitor" not in l and "friendly_name" in l), MON)
door = [json.loads(l) for l in LOG if "door board" in l and l.startswith("{")][0]
chk("door monitor = keyword check on the PUBLIC uptime address (no key in it)", door["url"].endswith("/exec?action=uptime") and "key=" not in door["url"] and door["keyword_value"] == '"door_online":true', door)
chk("Sheet step waits until the NEW Sheet version is really live", s2.get("sheet_v") == 2)
lap = json.loads(Path("/tmp/wz2/home/Desktop/SARTHIK_Room_v4/2_Website_v4/data/settings.json").read_text())
chk("laptop website FOUND automatically: door key fixed to the board's key", lap["door_key"] == vals["DOOR_API_KEY"] and lap["door_key"] != "WRONGKEY1234", lap)
chk("laptop website: CCTV webhook set, backup made", lap["webhook_url"].endswith("?key=" + "a1b2"*8) and Path("/tmp/wz2/home/Desktop/SARTHIK_Room_v4/2_Website_v4/data/settings_backup_before_stage3_fix.json").exists())
chk("n8n: exactly 2 workflows imported into the SAME n8n data folder Start n8n.bat uses", len(IMPORTED) == 2 and all(x[1].endswith("5_n8n/n8n_data") for x in IMPORTED), IMPORTED)
wf = [json.loads(Path(x[0].split("=", 1)[1]).read_text()) for x in IMPORTED]
chk("workflows: timer + web request only (no code, no env), retries on", all([n["type"] for n in w["nodes"]] == ["n8n-nodes-base.scheduleTrigger", "n8n-nodes-base.httpRequest"] and w["nodes"][1]["retryOnFail"] for w in wf))
chk("workflow 1 hourly 'reports', workflow 2 every 10 min 'watchdog'", wf[0]["nodes"][1]["parameters"]["url"].endswith("action=reports") and wf[1]["nodes"][0]["parameters"]["rule"]["interval"][0] == {"field": "minutes", "minutesInterval": 10} and wf[1]["nodes"][1]["parameters"]["url"].endswith("action=watchdog"))
LOG.clear(); DEPLOYS.clear(); IMPORTED.clear(); ans[:] = ["n"]
wz.main()
chk("running Setup again: nothing repeated (only asks about WhatsApp)", not DEPLOYS and not IMPORTED and not any("channels.json" in l for l in LOG), (DEPLOYS, IMPORTED, LOG))
print(f"\nSETUP REPAIR: {sum(res)}/{len(res)} pass")
