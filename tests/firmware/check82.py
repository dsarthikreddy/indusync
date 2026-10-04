import json, glob
ok = True; n = 0
def chk(name, c, x=""):
    global ok, n; n += 1
    if not c: print("  FAIL  " + name + "  -> " + str(x)[:200]); ok = False
L = lambda m: json.load(open(f"o82_{m}.json"))
f = L("flow"); docs = [json.load(open(p)) for p in glob.glob("doc_*.json")]
chk("events all uploaded + cursor saved", f["cloudseq"] == f["seq"] == f["nvs"], f)
ev = [d["fields"] for d in docs if "seq" in d["fields"]]
chk("valid Firestore docs", len(docs) >= 4 and all("fields" in d for d in docs))
chk("fake pass = alert", any(e["type"]["stringValue"] == "fake_pass" and e["severity"]["stringValue"] == "alert" for e in ev))
chk("quotes/commas exact", any(e["detail"]["stringValue"] == 'comma, "quote"' for e in ev))
st = [d["fields"] for d in docs if "sensors" in d["fields"]]
chk("status doc 15 sensors", st and len(st[0]["sensors"]["arrayValue"]["values"]) == 15)
chk("Telegram alert", any("FAKE" in json.load(open(p))["text"].upper() for p in glob.glob("tg_*.json")))
ts = open("ts_urls.txt").read().split("\n"); chk("ThingSpeak A+B", "TSKEYA" in ts[0] and "TSKEYB" in ts[1], ts)
o = L("outage"); chk("outage: no loss, backoff, catch-up", o["during"] < o["seq"] and o["backoff_ms"] >= 10000 and o["backoff_ms"] <= 60000 and o["after"] == o["seq"], o)
chk("bad login message", "login refused" in L("badlogin")["err"])
chk("rules refusal message", "not allowed" in L("forbid")["err"])
c = L("cmd"); chk("commands: once, done, dangerous refused", c == {"ran_first": 1, "ran_again": 0, "done_ok": 1, "dangerous_ran": 0, "dangerous_refused": 1}, c)
t = L("telegram"); chk("Telegram: stranger ignored, owner served", t["stranger_opened"] == 0 and t["replies_to_stranger"] == 0 and t["owner_got_status"] == 1, t)
b = L("blynk"); chk("Blynk button opens once", b["door_from_button"] == 1 and b["button_reset"], b)
s = L("sensors")
chk("sensors: builtin ok, PIR, ADS 28.5, no_data, fault, off", s["chip_temp"][0] == "ok" and s["motion_pir"] == ["ok", 1.0] and abs(s["temperature"][1] - 28.5) < .01
    and s["gas"][0] == "no_data" and s["humidity"][0] == "fault" and s["flame"][0] == "off", s)
d = json.load(open("door_sensors.json")); chk("local API: 15 sensors, version 8.2", len(d["sensors"]) == 15 and d["version"] == "8.2.5")
print(f"V8.2 checks: {n} groups, {'ALL PASS' if ok else 'FAILED'}  (covering the 28 individual checks)")
