"""SARTHIK Room - Stage 3 setup program (run with "Setup Stage 3.bat").

Does for you: makes the logins, makes the ThingSpeak channels, finds your Telegram chat number,
fills secrets.h / config.js / n8n settings / laptop website settings, publishes the website,
and gives the Google Sheet ONE settings code. You only sign up, click, and paste each key once HERE.
It remembers finished steps (tools\\stage3_state.json): if anything stops, just run it again.
"""
import base64
import getpass
import json
import os
import re
import secrets
import shutil
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
STATE = ROOT / "tools" / "stage3_state.json"
BOARD_SECRETS = ROOT / "1_Door_Board_v8_2" / "classync_stage1_v8_2" / "secrets.h"
CLOUD = ROOT / "4_Cloud_Website"
LAPTOP_SETTINGS = ROOT / "2_Website_v4" / "data" / "settings.json"
N8N_ENV = ROOT / "5_n8n" / "n8n.env"
SENSORS_A = ["chip_temp", "occupancy", "wifi_signal", "free_memory", "temperature", "humidity", "air_quality", "light"]
SENSORS_B = ["motion_pir", "sound", "current", "voltage", "gas", "door_contact", "flame", "spare"]
WINDOWS = os.name == "nt"
SITE_VERSION = "8.2.1-site-2"          # bump -> Setup re-publishes the website + rules
N8N_DIR = ROOT / "5_n8n"


# ---------------------------------------------------------------- small helpers
def say(t=""):
    print(t, flush=True)


def head(n, title):
    say("\n" + "=" * 64 + f"\n STEP {n}: {title}\n" + "=" * 64)


def ask(prompt, pattern=None, secret=False, default=None, optional=False, hint=""):
    while True:
        p = f"{prompt}" + (f" [{default}]" if default else "") + (" (Enter = skip)" if optional else "") + ": "
        v = (getpass.getpass(p) if secret else input(p)).strip().strip('"').strip("'")
        if not v and default:
            v = default
        if not v and optional:
            return ""
        if v and (not pattern or re.fullmatch(pattern, v)):
            return v
        say("   That does not look right." + (" " + hint if hint else "") + " Try again (copy it again, then paste with right-click or Ctrl+V).")


def yes(prompt):
    return ask(prompt + " (y/n)", r"[yYnN]") .lower() == "y"


def http(method, url, data=None, headers=None, form=False, timeout=25):
    h = dict(headers or {})
    body = None
    if data is not None:
        if form:
            body = urllib.parse.urlencode(data).encode()
            h["Content-Type"] = "application/x-www-form-urlencoded"
        else:
            body = json.dumps(data).encode()
            h["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=body, method=method, headers=h)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return r.status, r.read().decode("utf-8", "ignore")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8", "ignore")
    except Exception as e:  # no internet, DNS, timeout
        return 0, str(e)


def jload(t):
    try:
        return json.loads(t)
    except Exception:
        return {}


def load_state():
    try:
        return json.loads(STATE.read_text(encoding="utf-8"))
    except Exception:
        return {"done": {}}


def save_state(s):
    STATE.parent.mkdir(parents=True, exist_ok=True)
    STATE.write_text(json.dumps(s, indent=1), encoding="utf-8")


def gen_pass():
    return "Sr-" + secrets.token_urlsafe(18)


def copy_to_clipboard(text):
    if WINDOWS:
        try:
            subprocess.run("clip", input=text.encode("ascii", "ignore"), shell=True, check=True)
            return True
        except Exception:
            return False
    return False


# ---------------------------------------------------------------- Firebase
def fb_login(st, email, password, create=False):
    """Returns idToken. create=True: makes the account if it does not exist yet."""
    key = st["fb"]["apiKey"]
    if create:
        c, t = http("POST", f"https://identitytoolkit.googleapis.com/v1/accounts:signUp?key={key}",
                    {"email": email, "password": password, "returnSecureToken": True})
        if c == 200:
            return jload(t)["idToken"]
        msg = jload(t).get("error", {}).get("message", t[:120])
        if "OPERATION_NOT_ALLOWED" in msg or "ADMIN_ONLY_OPERATION" in msg:
            raise SystemExit("\n   Firebase says new logins are switched OFF. Open Firebase > Authentication > Sign-in method,\n"
                             "   enable Email/Password, and in Settings > User actions tick 'Enable create (sign-up)'. Then run again.")
        if "EMAIL_EXISTS" not in msg:
            raise SystemExit(f"\n   Firebase did not accept {email}: {msg}")
    c, t = http("POST", f"https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key={key}",
                {"email": email, "password": password, "returnSecureToken": True})
    if c != 200:
        raise SystemExit(f"\n   Could not sign in as {email}: {jload(t).get('error', {}).get('message', t[:120])}.\n"
                         "   If this is YOUR account, check the password. Then run Setup again.")
    return jload(t)["idToken"]


def fs_url(st, path=""):
    return f"https://firestore.googleapis.com/v1/projects/{st['fb']['projectId']}/databases/(default)/documents{path}"


def fs_val(v):
    if isinstance(v, bool):
        return {"booleanValue": v}
    if isinstance(v, int):
        return {"integerValue": str(v)}
    return {"stringValue": str(v)}


def fs_set(st, token, path, fields):
    c, t = http("PATCH", fs_url(st, "/" + path), {"fields": {k: fs_val(v) for k, v in fields.items()}},
                {"Authorization": "Bearer " + token})
    if c != 200:
        raise SystemExit(f"\n   Firebase refused to save {path} ({c}). Was the website published (step 2)? Details: {t[:160]}")


def fs_exists(st, token, path):
    c, _ = http("GET", fs_url(st, "/" + path), headers={"Authorization": "Bearer " + token})
    return c == 200


# ---------------------------------------------------------------- steps
def step_firebase(st):
    head(1, "Firebase project (24/7 website + database)")
    say("Do this in your browser (about 5 minutes), exactly as the guide shows:\n"
        "  1. https://console.firebase.google.com  > Create a project > name: sarthik-room > Continue (Analytics OFF)\n"
        "  2. Build > Authentication > Get started > Email/Password > Enable > Save\n"
        "  3. Build > Firestore Database > Create database > location asia-south1 (Mumbai) > Start in PRODUCTION mode\n"
        "  4. Project settings (gear) > General > Your apps > the </> (Web) button > nickname: website > Register app\n"
        "     You now see a box starting with  const firebaseConfig = {")
    say("\nCopy that WHOLE box (from 'const firebaseConfig' to the line with '};'), paste it here, then press Enter twice:")
    lines = []
    while True:
        try:
            line = input()
        except EOFError:
            break
        if not line.strip() and lines:
            break
        lines.append(line)
        if line.strip().startswith("};"):
            break
    text = "\n".join(lines)
    get = lambda k: (re.search(k + r"""\s*:\s*["']([^"']+)["']""", text) or [None, ""])[1]
    fb = {"apiKey": get("apiKey"), "authDomain": get("authDomain"), "projectId": get("projectId"), "appId": get("appId")}
    if not re.fullmatch(r"AIza[\w-]{30,}", fb["apiKey"]) or not fb["projectId"]:
        raise SystemExit("\n   I could not find apiKey / projectId in what you pasted. Run Setup again and paste the whole box.")
    st["fb"] = fb
    (CLOUD / "public" / "config.js").write_text(
        "// Written by Setup Stage 3 (public web settings, not secrets: security comes from firestore.rules).\n"
        f"window.SR_CONFIG = {json.dumps(dict(fb, device='door-1', sentryDsn=st.get('sentry', '')))};\n", encoding="utf-8")
    (CLOUD / ".firebaserc").write_text(json.dumps({"projects": {"default": fb["projectId"]}}, indent=1), encoding="utf-8")
    say(f"   OK: project {fb['projectId']}")


def step_deploy(st):
    head(2, "Publish the website + security rules (free Firebase Hosting)")
    npx = shutil.which("npx")
    if not npx:
        raise SystemExit("\n   Node.js is missing. Install the LTS version from https://nodejs.org (Next, Next, Finish),\n"
                         "   CLOSE this window, and run Setup Stage 3 again.")
    say("A browser window opens: sign in with the SAME Google account you used for Firebase, then Allow.")
    run = lambda args: subprocess.run([npx, "-y", "firebase-tools"] + args, cwd=str(CLOUD), shell=WINDOWS)
    if run(["login"]).returncode != 0:
        raise SystemExit("\n   Login did not finish. Run Setup again.")
    r = run(["deploy", "--only", "firestore,hosting", "--project", st["fb"]["projectId"], "--non-interactive"])
    if r.returncode != 0:
        raise SystemExit("\n   Publishing failed. Most common reason: Firestore database not created yet (step 1.3). Fix it and run again.")
    st["site"] = f"https://{st['fb']['projectId']}.web.app"
    st["site_version"] = SITE_VERSION
    say(f"   OK: your 24/7 website is {st['site']}")


def step_accounts(st):
    head(3, "Logins: you (admin), the door board, the Google Sheet")
    st.setdefault("acc", {})
    email = st["acc"].get("admin") or ask("YOUR e-mail (you will sign in to the website with it)", r"[^@\s]+@[^@\s]+\.[^@\s]+").lower()
    pw = ask("Choose YOUR website password (min 10 characters; typing is hidden)", r".{10,}", secret=True, hint="At least 10 characters.")
    tok = fb_login(st, email, pw, create=True)
    if not fs_exists(st, tok, f"members/{email}"):
        name = ask("Your name", r".{1,40}", default=email.split("@")[0])
        writes = [{"update": {"name": fs_url(st, f"/members/{email}")[len("https://firestore.googleapis.com/v1/"):],
                              "fields": {"role": fs_val("admin"), "name": fs_val(name)}}, "currentDocument": {"exists": False}},
                  {"update": {"name": fs_url(st, "/config/bootstrap")[len("https://firestore.googleapis.com/v1/"):],
                              "fields": {"by": fs_val(email)}}, "currentDocument": {"exists": False}}]
        c, t = http("POST", fs_url(st)[:-len("/documents")] + "/documents:commit", {"writes": writes}, {"Authorization": "Bearer " + tok})
        if c != 200:
            raise SystemExit(f"\n   Could not make you admin ({c}). If someone else already became admin, delete the project's\n"
                             f"   'config/bootstrap' and 'members' documents in the Firestore console, then run again. {t[:160]}")
    st["acc"]["admin"] = email
    for role, mail in (("device", "door-1.board@sarthik.room"), ("reporter", "sheet.reporter@sarthik.room")):
        if role not in st["acc"]:
            p = gen_pass()
            fb_login(st, mail, p, create=True)
            st["acc"][role] = {"email": mail, "pass": p}
        fs_set(st, tok, f"members/{st['acc'][role]['email']}", {"role": role, "name": "door board" if role == "device" else "Google Sheet"})
    wa = ask("YOUR WhatsApp number with country code, digits only (e.g. 919876543210)", r"\d{10,15}", optional=True)
    if wa:
        fs_set(st, tok, f"members/{email}", {"role": "admin", "name": email.split("@")[0], "whatsapp": wa})
    st["admin_token_time"] = time.time()
    st["_tok"] = tok
    say("   OK: 3 logins ready (yours, the door board's, the Sheet's). The board/Sheet passwords were made for you.")


def admin_token(st):
    if st.get("_tok") and time.time() - st.get("admin_token_time", 0) < 3000:
        return st["_tok"]
    email = st["acc"]["admin"]
    key = st["fb"]["apiKey"]
    for attempt in range(3):
        if attempt == 0:
            pw = ask(f"Your website password ({email}; typing is hidden)", r".{8,}", secret=True)
        else:
            say("   Hidden typing can go wrong on Windows. This time the password is VISIBLE (nobody else sees your screen):")
            pw = input("   Type your website password: ").strip()
        c, t = http("POST", f"https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key={key}",
                    {"email": email, "password": pw, "returnSecureToken": True})
        if c == 200:
            st["_tok"] = jload(t)["idToken"]
            st["admin_token_time"] = time.time()
            return st["_tok"]
        say(f"   Firebase did not accept it (I received {len(pw)} characters; yours should have that many).")
    raise SystemExit("\n   Still not accepted. Reset the password (30 seconds):\n"
                     "   Firebase console > Authentication > Users > the three dots next to your e-mail > Reset password,\n"
                     "   open the e-mail Google sends, choose a new password (10+ characters), then run Setup again.")


def step_thingspeak(st):
    head(4, "ThingSpeak (graphs for all 15 sensors)")
    say("  1. https://thingspeak.mathworks.com > Get Started For Free > create a free MathWorks account\n"
        "  2. Top right: Account > My Profile > API Keys > copy the 'User API Key' (16 letters/numbers)")
    key = ask("Paste your ThingSpeak USER API Key", r"[A-Z0-9]{16}", optional=True, hint="16 capital letters/numbers.")
    if not key:
        say("   Skipped. Sensors still show live on the website; only the history graphs need ThingSpeak.")
        return
    tok = admin_token(st)                     # check the password FIRST: nothing is created if it fails
    c, t = http("GET", f"https://api.thingspeak.com/channels.json?api_key={key}")
    try:
        existing = json.loads(t) if c == 200 else None
    except Exception:
        existing = None
    if not isinstance(existing, list):
        raise SystemExit(f"\n   ThingSpeak refused the key ({c}). Copy the 'User API Key' again (Account > My Profile > API Keys).")
    chans, created = [], 0
    for name, fields in (("SARTHIK Room sensors A (1-8)", SENSORS_A), ("SARTHIK Room sensors B (9-15)", SENSORS_B)):
        mine = sorted([ch for ch in existing if ch.get("name") == name], key=lambda ch: ch.get("id", 0))
        data = {"api_key": key, "name": name, "public_flag": "false", "description": "SARTHIK Room - Stage 3"}
        data.update({f"field{i + 1}": f for i, f in enumerate(fields)})
        if mine:                               # re-use the channel an earlier (failed) run already made
            j = mine[0]
            http("PUT", f"https://api.thingspeak.com/channels/{j['id']}.json", data, form=True)
            say(f"   Re-using your existing channel '{name}' (id {j['id']}).")
            if len(mine) > 1:
                say(f"   (You have {len(mine) - 1} extra copy/copies of it: delete them later in ThingSpeak > Channels > My Channels.)")
        else:
            if len(existing) + created >= 4:
                raise SystemExit("\n   Your free ThingSpeak account already holds 4 other channels (the free limit).\n"
                                 "   ThingSpeak > Channels > My Channels > open one you do not need > Channel Settings > bottom: Delete channel.\n"
                                 "   Delete at least 2, then run Setup again.")
            c, t = http("POST", "https://api.thingspeak.com/channels.json", data, form=True)
            j = jload(t)
            if c != 200 or "id" not in j:
                raise SystemExit(f"\n   ThingSpeak refused ({c}). {t[:120]}")
            created += 1
            say(f"   Created channel '{name}' (id {j['id']}).")
        keys = j.get("api_keys") or jload(http("GET", f"https://api.thingspeak.com/channels/{j['id']}.json?api_key={key}")[1]).get("api_keys", [])
        w = next((k["api_key"] for k in keys if k.get("write_flag")), "")
        r = next((k["api_key"] for k in keys if not k.get("write_flag")), "")
        if not w:
            raise SystemExit("\n   ThingSpeak did not return the channel keys. Run Setup again.")
        chans.append({"id": j["id"], "write": w, "read": r})
    st["ts"] = chans
    fs_set(st, tok, "config/site", {"ts_channel_a": str(chans[0]["id"]), "ts_read_a": chans[0]["read"],
                                   "ts_channel_b": str(chans[1]["id"]), "ts_read_b": chans[1]["read"]})
    say(f"   OK: channels {chans[0]['id']} and {chans[1]['id']} are ready with all 15 sensor names.")


def step_blynk(st):
    head(5, "Blynk (quick phone panel)")
    say("  1. https://blynk.cloud > Sign up (free) > Developer Zone > My Templates > New Template: name 'SARTHIK Room', ESP32, Wi-Fi\n"
        "  2. In the template > Datastreams > add Virtual Pins: V0 online, V1 present, V2 temperature, V3 alerts (Integer),\n"
        "     V10 'Open door' (Integer 0-1) > Save\n"
        "  3. Devices > New Device > From template 'SARTHIK Room' > copy BLYNK_AUTH_TOKEN (32 characters)")
    tok = ask("Paste the BLYNK_AUTH_TOKEN", r"[\w-]{32}", optional=True, hint="It is 32 characters long.")
    if not tok:
        say("   Skipped.")
        return
    c, t = http("GET", f"https://blynk.cloud/external/api/isHardwareConnected?token={tok}")
    if c != 200:
        raise SystemExit("\n   Blynk does not know this token. Copy it again from Devices > your device > Device info.")
    st["blynk"] = tok
    say("   OK: Blynk token works.")


def step_telegram(st):
    head(6, "Telegram (instant alerts + /status and /open from your phone)")
    say("  1. In Telegram search  @BotFather  (blue tick) > Start > send  /newbot\n"
        "  2. Name: SARTHIK Room   Username: something ending in bot, e.g. sarthik_room_alerts_bot\n"
        "  3. BotFather answers with a token like 123456789:AAH...  copy it")
    tok = ask("Paste the bot token", r"\d{6,12}:[\w-]{30,}", optional=True, hint="It looks like 123456789:AAH... with a colon.")
    if not tok:
        say("   Skipped.")
        return
    c, t = http("GET", f"https://api.telegram.org/bot{tok}/getMe")
    if c != 200:
        raise SystemExit("\n   Telegram does not know this token. Copy it again from @BotFather.")
    bot = jload(t)["result"]["username"]
    say(f"\n  4. Now open Telegram, search  @{bot} , press START (or send /start).")
    chat = ""
    for _ in range(12):
        input("     Done? Press Enter here... ")
        c, t = http("GET", f"https://api.telegram.org/bot{tok}/getUpdates")
        res = jload(t).get("result", [])
        msgs = [u.get("message", {}) for u in res if u.get("message")]
        if msgs:
            chat = str(msgs[-1]["chat"]["id"])
            break
        say("     I don't see your /start yet. Send /start to the bot, wait 3 seconds, then press Enter.")
    if not chat:
        raise SystemExit("\n   No message arrived. Run Setup again.")
    http("POST", f"https://api.telegram.org/bot{tok}/sendMessage", {"chat_id": chat, "text": "SARTHIK Room is linked to this chat. Try /status later."})
    st["tg"] = {"token": tok, "chat": chat, "bot": bot}
    say(f"   OK: found your chat number automatically ({chat}). Check Telegram: a test message arrived.")


def step_whatsapp(st):
    head(7, "WhatsApp (important messages to up to 5 family numbers, free test number)")
    if not yes("Set up WhatsApp now? It takes about 15 minutes in Meta's website"):
        say("   Skipped. Run Setup again any time to add it.")
        return
    say("  Follow STAGE3_SETUP_GUIDE.txt part 7 (Meta app, test number, add family numbers, permanent token, template).")
    tok = ask("Paste the PERMANENT token (starts with EAA)", r"EA[\w]{50,}", secret=True)
    pid = ask("Paste the Phone number ID (15-16 digits, NOT the phone number)", r"\d{10,20}")
    c, t = http("GET", f"https://graph.facebook.com/v21.0/{pid}?fields=display_phone_number", headers={"Authorization": "Bearer " + tok})
    if c != 200:
        raise SystemExit(f"\n   Meta refused ({c}): check the token and the Phone number ID. {t[:160]}")
    tpl = ask("Template name (room_update once approved; hello_world for the very first test)", r"[a-z0-9_]{3,60}", default="hello_world")
    st["wa"] = {"token": tok, "phone_id": pid, "template": tpl}
    say("   OK: WhatsApp token works.")


def fill_define(text, name, value):
    return re.sub(r'(#define\s+' + name + r'\s+)"[^"]*"', lambda m: m.group(1) + json.dumps(value), text)


def step_files(st):
    head(8, "Writing all settings files for you")
    if BOARD_SECRETS.exists():
        s = BOARD_SECRETS.read_text(encoding="utf-8")
        vals = {"FB_API_KEY": st["fb"]["apiKey"], "FB_PROJECT_ID": st["fb"]["projectId"],
                "FB_DEVICE_EMAIL": st["acc"]["device"]["email"], "FB_DEVICE_PASS": st["acc"]["device"]["pass"],
                "TS_WRITE_KEY_A": (st.get("ts") or [{}, {}])[0].get("write", ""), "TS_WRITE_KEY_B": (st.get("ts") or [{}, {}])[1].get("write", ""),
                "BLYNK_TOKEN": st.get("blynk", ""), "TG_BOT_TOKEN": (st.get("tg") or {}).get("token", ""), "TG_CHAT_ID": (st.get("tg") or {}).get("chat", "")}
        for k, v in vals.items():
            s = fill_define(s, k, v)
        BOARD_SECRETS.write_text(s, encoding="utf-8")
        say(f"   Door board: {BOARD_SECRETS.relative_to(ROOT)} filled.")
    st.setdefault("hook", secrets.token_hex(16))
    notify = st.get("notify")
    if notify is None:
        notify = ask("Whose arrival should go to WhatsApp? Door names, comma separated (e.g. rozy)", r"[a-z0-9_,\s]*", optional=True)
    notify = ",".join(x.strip() for x in notify.split(",") if x.strip())
    st["notify"] = notify
    report_to = st.get("report_to") or ask("E-mail for daily/weekly reports", r"[^@\s]+@[^@\s]+\.[^@\s]+", default=st["acc"]["admin"])
    st["report_to"] = report_to
    code = {"FB_API_KEY": st["fb"]["apiKey"], "FB_PROJECT_ID": st["fb"]["projectId"], "REPORTER_EMAIL": st["acc"]["reporter"]["email"],
            "REPORTER_PASS": st["acc"]["reporter"]["pass"], "REPORT_EMAIL": report_to, "TG_BOT_TOKEN": (st.get("tg") or {}).get("token", ""),
            "TG_CHAT_ID": (st.get("tg") or {}).get("chat", ""), "WA_TOKEN": (st.get("wa") or {}).get("token", ""),
            "WA_PHONE_ID": (st.get("wa") or {}).get("phone_id", ""), "WA_TEMPLATE": (st.get("wa") or {}).get("template", "room_update"),
            "NOTIFY_PEOPLE": notify, "HOOK_KEY": st["hook"], "SENTRY_DSN": st.get("sentry", "")}
    st["sheet_code"] = base64.b64encode(json.dumps(code).encode()).decode()
    st["files_v"] = 2
    say("   Google Sheet settings code: ready (next step).")


def step_sheet(st):
    if st.get("sheet_url"):
        head(9, "Google Sheet: install the NEW script version (keeps the same web address)")
        say("  1. Open your SARTHIK Room Sheet > Extensions > Apps Script > click inside Code.gs > Ctrl+A > Delete\n"
            f"  2. Notepad: open {ROOT / '3_Google_Sheet' / 'Code.gs'} > Ctrl+A > Ctrl+C > paste into Apps Script > Save (disk icon)\n"
            "  3. Deploy > Manage deployments > pencil (Edit) > Version: New version > Deploy\n"
            "  4. Back in the Sheet press F5 > menu SARTHIK Room > 1. Enter settings > paste the settings code > OK\n"
            "  5. Menu SARTHIK Room > 3. Test everything now > Allow (Google asks once more: it may now read monitor e-mails)")
        (ROOT / "tools" / "sheet_settings_code.txt").write_text(st["sheet_code"], encoding="utf-8")
        say("\n  The settings code is in tools\\sheet_settings_code.txt (open it in Notepad, Ctrl+A, Ctrl+C).")
        for _ in range(20):
            input("     When steps 1-5 are done, press Enter here... ")
            c, t = http("GET", st["sheet_url"] + "?action=uptime")
            if c == 200 and "door_online" in t:
                say("   OK: the Sheet runs the new version.")
                st["sheet_v"] = 2
                return
            say("     The Sheet still runs the OLD version: do step 3 (New version > Deploy) and press Enter again.")
        raise SystemExit("\n   The new Sheet version is not live yet. Run Setup again later.")
    head(9, "Google Sheet (reports, e-mail, WhatsApp - runs 24/7 inside Google)")
    say("  1. https://sheets.new  (makes a new Google Sheet) > name it  SARTHIK Room\n"
        "  2. Extensions > Apps Script > delete everything in Code.gs\n"
        f"  3. Open {ROOT / '3_Google_Sheet' / 'Code.gs'} in Notepad > Ctrl+A > Ctrl+C > paste into Apps Script > Save (disk icon)\n"
        "  4. Deploy > New deployment > gear > Web app > Execute as: Me, Who has access: Anyone > Deploy\n"
        "     > Authorize access > choose your Google account > Advanced > Go to SARTHIK Room (unsafe) > Allow\n"
        "     > copy the 'Web app URL' (ends with /exec)")
    copied = copy_to_clipboard(st["sheet_code"])
    (ROOT / "tools" / "sheet_settings_code.txt").write_text(st["sheet_code"], encoding="utf-8")
    say("  5. Back in the SHEET: reload the page > menu SARTHIK Room > 1. Enter settings > open tools\\sheet_settings_code.txt in\n"
        "     Notepad > Ctrl+A > Ctrl+C > paste into the box > OK" +
        "\n     then menu SARTHIK Room > 2. Start automatic jobs > Allow, and 3. Test everything now")
    url = ask("Paste the Web app URL here", r"https://script\.google\.com/macros/s/[\w-]+/exec")
    c, t = http("GET", url + "?key=" + st["hook"] + "&action=ping")
    if c != 200 or not jload(t).get("ok"):
        raise SystemExit("\n   The Sheet does not answer yet. Did you do step 5 (Enter settings) and choose 'Anyone' in step 4?\n"
                         "   Then run Setup again (finished steps are skipped).")
    st["sheet_url"] = url
    st["sheet_v"] = 2
    say("   OK: the Sheet answers.")


def step_laptop_and_n8n(st):
    head(10, "Linking the laptop website and n8n")
    N8N_ENV.parent.mkdir(parents=True, exist_ok=True)
    N8N_ENV.write_text(f"SR_SHEET_URL={st['sheet_url']}\nSR_SHEET_KEY={st['hook']}\n", encoding="utf-8")
    say("   n8n settings written.")
    hook = st["sheet_url"] + "?key=" + st["hook"]
    door_key = ""
    if BOARD_SECRETS.exists():
        m = re.search(r'#define\s+DOOR_API_KEY\s+"([^"]*)"', BOARD_SECRETS.read_text(encoding="utf-8"))
        door_key = m.group(1) if m else ""
    found = find_laptop_sites()
    for f in found:
        stop = f.parent.parent / "tools" / "stop.ps1"
        if WINDOWS and stop.exists():
            subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(stop)])
        bak = f.with_name("settings_backup_before_stage3_fix.json")
        if not bak.exists():
            shutil.copy2(f, bak)
        cfg = jload(f.read_text(encoding="utf-8"))
        cfg.update({"webhook_url": hook, "webhook_events": "alerts"})
        if re.fullmatch(r"[A-Za-z0-9]{8,64}", door_key):
            cfg["door_key"] = door_key
        f.write_text(json.dumps(cfg, indent=1), encoding="utf-8")
        say(f"   Laptop website at {f.parent.parent}: door key now matches the board, CCTV alerts linked (backup: {bak.name}).")
    st["link_v"] = 2
    if found:
        say("   Start the laptop website again with the Desktop icon 'SARTHIK Room'.")
        return
    if LAPTOP_SETTINGS.exists():
        stop = ROOT / "2_Website_v4" / "tools" / "stop.ps1"
        if WINDOWS and stop.exists():
            subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(stop)])
        cfg = jload(LAPTOP_SETTINGS.read_text(encoding="utf-8"))
        cfg.update({"webhook_url": hook, "webhook_events": "alerts"})
        LAPTOP_SETTINGS.write_text(json.dumps(cfg, indent=1), encoding="utf-8")
        say("   Laptop website: CCTV alerts now also go to the cloud (restart it with the Desktop icon).")
    else:
        st["laptop_hook_todo"] = hook
        say("   Laptop website not set up in this folder yet: in its Control page > Integrations > Webhook, paste:\n   " + hook)


def find_laptop_sites():
    """Every installed laptop website (2_Website_v4\\data\\settings.json) in the usual places."""
    home = Path.home()
    roots = [ROOT, Path("C:/"), home, home / "Desktop", home / "Downloads", home / "Documents", home / "OneDrive" / "Desktop"]
    out, seen = [], set()
    for r in roots:
        try:
            cands = list(r.glob("2_Website_v4/data/settings.json")) + list(r.glob("*/2_Website_v4/data/settings.json"))
        except Exception:
            continue
        for f in cands:
            try:
                k = str(f.resolve()).lower()
            except Exception:
                continue
            if f.exists() and k not in seen:
                seen.add(k); out.append(f)
    return out


def step_sentry(st):
    head(11, "Sentry (tells you about website and Sheet errors - free Developer plan)")
    say("  1. https://sentry.io/signup/ > sign up (free) > when asked for a platform choose  Browser JavaScript > Create Project\n"
        "  2. It shows a DSN like  https://abc123...@o456.ingest.us.sentry.io/789  (or later: Settings > Projects > your project\n"
        "     > Client Keys (DSN)) > copy it. A DSN is not a password: it only lets apps SEND errors to you.\n"
        "  Sentry e-mails you about errors; the Google Sheet forwards those e-mails to Telegram.")
    dsn = ask("Paste the Sentry DSN", r"https://[0-9a-f]{16,64}@[\w.-]+/\d+", optional=True)
    if dsn:
        st["sentry"] = dsn
        cfgp = CLOUD / "public" / "config.js"
        if cfgp.exists() and st.get("fb"):
            cfgp.write_text("// Written by Setup Stage 3 (public web settings, not secrets: security comes from firestore.rules).\n"
                            f"window.SR_CONFIG = {json.dumps(dict(st['fb'], device='door-1', sentryDsn=dsn))};\n", encoding="utf-8")
        st["site_version"] = ""          # re-publish the website so it reports errors
        say("   OK: Sentry DSN saved (website + Sheet will report errors).")


def step_uptime(st):
    head(12, "UptimeRobot (checks every 5 min that the website and the door are up - free plan)")
    say("  1. https://uptimerobot.com > Sign up (free) > confirm the e-mail\n"
        "  2. Left menu: Integrations & API > API > 'Main API key' > Create / show > copy it\n"
        "  The program then makes 2 monitors for you. Alerts come by e-mail; the Google Sheet forwards them to Telegram.")
    key = ask("Paste the UptimeRobot Main API key", r"[\w-]{20,80}", secret=True, optional=True)
    if not key:
        return
    if not st.get("sheet_url"):
        raise SystemExit("\n   Set up the Google Sheet first (run Setup again).")
    c, t = http("POST", "https://api.uptimerobot.com/v2/getAlertContacts", {"api_key": key, "format": "json"}, form=True)
    j = jload(t)
    if c != 200 or j.get("stat") != "ok":
        raise SystemExit("\n   UptimeRobot did not accept the key. Copy the MAIN API key again (not the read-only one).")
    contacts = "-".join(f"{a['id']}_0_0" for a in j.get("alert_contacts", []))
    have = jload(http("POST", "https://api.uptimerobot.com/v2/getMonitors", {"api_key": key, "format": "json"}, form=True)[1]).get("monitors", [])
    names = {m.get("friendly_name") for m in have}
    monitors = [{"friendly_name": "SARTHIK Room website", "url": st.get("site") or f"https://{st['fb']['projectId']}.web.app", "type": "1"},
                {"friendly_name": "SARTHIK Room door board", "url": st["sheet_url"] + "?action=uptime", "type": "2",
                 "keyword_type": "2", "keyword_case_type": "0", "keyword_value": '"door_online":true'}]
    for mon in monitors:
        if mon["friendly_name"] in names:
            say(f"   (monitor '{mon['friendly_name']}' already exists)")
            continue
        data = dict(mon, api_key=key, format="json", interval="300")
        if contacts:
            data["alert_contacts"] = contacts
        c, t = http("POST", "https://api.uptimerobot.com/v2/newMonitor", data, form=True)
        if c != 200 or jload(t).get("stat") != "ok":
            raise SystemExit(f"\n   UptimeRobot refused to create '{mon['friendly_name']}': {t[:160]}")
        say(f"   OK: monitor '{mon['friendly_name']}' created (every 5 minutes).")
    st["uptime"] = True


def n8n_workflow(name, wid, every, minutes, action, url):
    rule = {"field": "minutes", "minutesInterval": every} if minutes else {"field": "hours", "hoursInterval": every}
    return {"id": wid, "name": name, "active": False, "settings": {"executionOrder": "v1", "saveDataSuccessExecution": "none"},
            "nodes": [{"parameters": {"rule": {"interval": [rule]}}, "id": wid + "-t", "name": "Timer",
                       "type": "n8n-nodes-base.scheduleTrigger", "typeVersion": 1.2, "position": [0, 0]},
                      {"parameters": {"url": url + "&action=" + action, "options": {"timeout": 90000}}, "id": wid + "-h",
                       "name": "Ask the Google Sheet", "type": "n8n-nodes-base.httpRequest", "typeVersion": 4.2, "position": [260, 0],
                       "retryOnFail": True, "maxTries": 3, "waitBetweenTries": 5000}],
            "connections": {"Timer": {"main": [[{"node": "Ask the Google Sheet", "type": "main", "index": 0}]]}}, "pinData": {}}


def step_n8n(st):
    head(13, "n8n: importing your 2 workflows")
    npx = shutil.which("npx")
    if not npx:
        raise SystemExit("\n   Node.js is missing (https://nodejs.org, LTS). Install it, then run Setup again.")
    import socket
    while True:
        with socket.socket() as so:
            busy = so.connect_ex(("127.0.0.1", 5678)) == 0
        if not busy:
            break
        input("   n8n is running. CLOSE its black window first (Start n8n.bat), then press Enter here... ")
    base = st["sheet_url"] + "?key=" + st["hook"]
    gen = N8N_DIR / "generated"
    gen.mkdir(parents=True, exist_ok=True)
    files = [("SARTHIK Room - weekly and monthly reports", "SRreports0000001", 1, False, "reports"),
             ("SARTHIK Room - door watchdog", "SRwatchdog000001", 10, True, "watchdog")]
    env = dict(os.environ, N8N_USER_FOLDER=str(N8N_DIR / "n8n_data"), N8N_DIAGNOSTICS_ENABLED="false")
    for name, wid, every, minutes, action in files:
        p = gen / (wid + ".json")
        p.write_text(json.dumps(n8n_workflow(name, wid, every, minutes, action, base), indent=1), encoding="utf-8")
        r = subprocess.run([npx, "-y", "n8n", "import:workflow", f"--input={p}"], env=env, shell=WINDOWS)
        if r.returncode != 0:
            raise SystemExit(f"\n   n8n could not import '{name}'. Close every n8n window and run Setup again.")
        say(f"   OK: imported '{name}'.")
    st["n8n_v"] = 2


STEPS = [("firebase", step_firebase), ("deploy", step_deploy), ("accounts", step_accounts), ("thingspeak", step_thingspeak),
         ("blynk", step_blynk), ("telegram", step_telegram), ("whatsapp", step_whatsapp),
         ("sentry", step_sentry), ("uptime", step_uptime), ("files", step_files), ("sheet", step_sheet), ("link", step_laptop_and_n8n),
         ("n8n", step_n8n)]


def main():
    say("SARTHIK Room - Stage 3 setup.  Your keys stay on THIS laptop (tools\\stage3_state.json). Never share that file.")
    st = load_state()
    have = {"thingspeak": lambda: st.get("ts"), "blynk": lambda: st.get("blynk"), "telegram": lambda: st.get("tg"),
            "whatsapp": lambda: st.get("wa"), "sentry": lambda: st.get("sentry"), "uptime": lambda: st.get("uptime"),
            "deploy": lambda: st.get("site_version") == SITE_VERSION, "n8n": lambda: st.get("n8n_v") == 2,
            "files": lambda: st.get("files_v") == 2, "sheet": lambda: st.get("sheet_v") == 2, "link": lambda: st.get("link_v") == 2}
    keys = ("ts", "blynk", "tg", "wa", "sentry")
    before = json.dumps({k: (st.get(k) or "") for k in keys}, sort_keys=True)
    changed = False
    for name, fn in STEPS:
        done = bool(st["done"].get(name)) and (name not in have or bool(have[name]()))
        if name in ("files", "sheet") and changed and st["done"].get(name):
            done = False
        if name in ("whatsapp", "sentry", "uptime", "thingspeak") and st["done"].get(name) and not have[name]():
            if not yes(f"'{name}' is not set up yet. Set it up now"):
                continue
        if done:
            say(f"   (step '{name}' already done - skipped)")
            continue
        fn(st)
        st["done"][name] = True
        if json.dumps({k: (st.get(k) or "") for k in keys}, sort_keys=True) != before:
            changed = True
    if st["done"].get("deploy") and st.get("site_version") != SITE_VERSION:
        step_deploy(st)                          # website settings changed during this run (e.g. Sentry): publish once more
        save_state({k: v for k, v in st.items() if k != "_tok"})
        save_state({k: v for k, v in st.items() if k != "_tok"})
    say("\n" + "=" * 64 + "\n ALL DONE\n" + "=" * 64)
    say(f"  Website (24/7, any phone): {st.get('site', 'https://' + st['fb']['projectId'] + '.web.app')}")
    say("  NOW, for safety: Firebase > Authentication > Settings > User actions > UNTICK 'Enable create (sign-up)' > Save")
    say("  Then upload the door board firmware again (secrets.h is up to date) and start n8n with 'Start n8n.bat'.")
    say("  In n8n open each of the 2 workflows and press  Publish  (top right) once.")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        say("\nStopped. Run Setup again to continue where you left off.")
    except SystemExit as e:
        say(str(e) if e.code not in (None, 0) else "")
        say("\nNothing is lost: finished steps are remembered. Fix the item above and run Setup Stage 3 again.")
    if WINDOWS:
        input("\nPress Enter to close this window.")
