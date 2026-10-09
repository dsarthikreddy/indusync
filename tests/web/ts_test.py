import json, builtins, importlib.util
from pathlib import Path
spec = importlib.util.spec_from_file_location("wz", "/tmp/wz2/pkg/tools/setup_stage3.py"); wz = importlib.util.module_from_spec(spec); spec.loader.exec_module(wz)
st = {"done": {}, "fb": {"apiKey": "AIzaFAKE-FAKEfakeFAKEfakeFAKEfake00000", "projectId": "sarthik-room"}, "acc": {"admin": "owner@gmail.com"}}
EXIST = [{"id": 10, "name": "SARTHIK Room sensors A (1-8)", "api_keys": [{"api_key": "WA1", "write_flag": True}, {"api_key": "RA1", "write_flag": False}]},
         {"id": 11, "name": "SARTHIK Room sensors B (9-15)", "api_keys": [{"api_key": "WB1", "write_flag": True}, {"api_key": "RB1", "write_flag": False}]},
         {"id": 12, "name": "SARTHIK Room sensors A (1-8)", "api_keys": [{"api_key": "WA2", "write_flag": True}, {"api_key": "RA2", "write_flag": False}]},
         {"id": 13, "name": "SARTHIK Room sensors B (9-15)", "api_keys": [{"api_key": "WB2", "write_flag": True}, {"api_key": "RB2", "write_flag": False}]}]
CHANNEL_DETAILS = {}
LOG, DOCS = [], {}
def fake(method, url, data=None, headers=None, form=False, timeout=25):
    LOG.append((method, url.split("?")[0]))
    if "signInWithPassword" in url: return (200, json.dumps({"idToken": "T"})) if data["password"] == "RightPassword1" else (400, '{"error":{"message":"INVALID_LOGIN_CREDENTIALS"}}')
    if url.startswith("https://api.thingspeak.com/channels.json") and method == "GET": return 200, json.dumps(EXIST)
    if url.startswith("https://api.thingspeak.com/channels/") and method == "GET":
        ch_id = int(url.split("/channels/")[1].split(".json")[0])
        return 200, json.dumps(CHANNEL_DETAILS.get(ch_id, {}))
    if method == "PUT": return 200, "{}"
    if method == "POST" and "channels.json" in url: return 500, "must not create"
    if "firestore" in url: DOCS.update(data["fields"]); return 200, "{}"
    return 404, ""
wz.http = fake
ans = ["ABCD1234EFGH5678", "WrongOne123", "RightPassword1"]       # hidden attempt fails -> visible attempt works
builtins.input = lambda p="": ans.pop(0); wz.getpass.getpass = lambda p="": ans.pop(0)
wz.step_thingspeak(st)
ok = lambda n, c: print(("PASS  " if c else "FAIL  ") + n)
ok("password checked BEFORE anything is created", LOG[0][1].endswith("signInWithPassword") and LOG[1][1].endswith("signInWithPassword"))
ok("visible-typing retry works after a failed hidden attempt", st["_tok"] == "T")
ok("existing channels re-used, none created, duplicates oldest-first", st["ts"][0]["id"] == 10 and st["ts"][1]["id"] == 11 and not any(m == "POST" and u.endswith("channels.json") for m, u in LOG))
ok("website gets READ keys only", DOCS["ts_read_a"]["stringValue"] == "RA1" and "WA1" not in json.dumps(DOCS))
ok("write keys kept for the board", st["ts"][0]["write"] == "WA1" and st["ts"][1]["write"] == "WB1")
LOG.clear(); DOCS.clear(); CHANNEL_DETAILS.clear()
st2 = {"done": {}, "fb": {"apiKey": "AIzaFAKE-FAKEfakeFAKEfakeFAKEfake00000", "projectId": "sarthik-room"}, "acc": {"admin": "owner@gmail.com"}}
EXIST[:] = [{"id": 10, "name": "SARTHIK Room sensors A (1-8)", "api_keys": [{"api_key": "WA1", "write_flag": True}, {"api_key": "RA1", "write_flag": False}]},
            {"id": 11, "name": "SARTHIK Room sensors B (9-15)"}]
CHANNEL_DETAILS[11] = {"api_keys": [{"api_key": "WB1F", "write_flag": True}, {"api_key": "RB1F", "write_flag": False}]}
ans[:] = ["ABCD1234EFGH5678", "RightPassword1"]
wz.step_thingspeak(st2)
ok("missing api_keys in list -> falls back to channel details API", any(m == "GET" and u.endswith("/channels/11.json") for m, u in LOG))
ok("fallback keys are used and saved", st2["ts"][1]["write"] == "WB1F" and st2["ts"][1]["read"] == "RB1F" and DOCS["ts_read_b"]["stringValue"] == "RB1F")
