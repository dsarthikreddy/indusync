"""Outbox: sends things OUT of the website in a background thread (never slows a page).

Phone   ntfy.sh topic (same free app the door board uses). Manual messages from the Control page,
        and (setting "phone_alerts") CCTV / cross-check alerts. Door-board alerts are already sent
        by the board itself, so they are NOT sent twice.
Webhook Stage 3: one JSON POST per event to a URL (n8n, Make, Zapier, your own server). From n8n you can
        forward to Google Sheets, Telegram, WhatsApp, e-mail and Firebase without changing this code.
        Event JSON: {id, ts, type, title, severity, source, device, person_id, name, detail, site}
Backups Daily copy of the database + accounts + settings into  backups\\  (outside the website), 14 kept.
"""
import json
import queue
import shutil
import threading
import time
import urllib.request
from datetime import datetime
from pathlib import Path

PHONE_SOURCES = ("cctv", "crosscheck")


class Outbox:
    def __init__(self, get_cfg, log, site="SARTHIK Room"):
        self.get_cfg = get_cfg
        self.log = log
        self.site = site
        self.q = queue.Queue(maxsize=500)
        self.sent = {"phone": 0, "webhook": 0, "failed": 0}
        self.last_error = ""
        self._last_phone = {}
        threading.Thread(target=self._run, daemon=True, name="outbox").start()

    # called by the database for every new event
    def on_event(self, ev):
        cfg = self.get_cfg()
        if cfg.get("webhook_url") and (cfg.get("webhook_events") == "all" or ev["severity"] in ("alert", "warn")):
            self._put(("webhook", dict(ev, site=self.site)))
        if cfg.get("phone_alerts") and ev["severity"] == "alert" and ev["source"] in PHONE_SOURCES:
            key = ev["type"] + str(ev.get("person_id"))
            if time.time() - self._last_phone.get(key, 0) > 60:          # same alert at most once a minute
                self._last_phone[key] = time.time()
                self._put(("phone", {"title": "SARTHIK Room: " + ev["title"],
                                     "text": "%s%s" % (ev.get("name") or "", (" - " + ev["detail"]) if ev.get("detail") else ""),
                                     "prio": "high"}))

    def phone(self, title, text, prio="default"):
        return self._put(("phone", {"title": title, "text": text, "prio": prio}))

    def _put(self, item):
        try:
            self.q.put_nowait(item)
            return True
        except queue.Full:
            return False

    def _run(self):
        while True:
            kind, data = self.q.get()
            try:
                if kind == "phone":
                    self._ntfy(data)
                else:
                    self._webhook(data)
                self.sent[kind] += 1
            except Exception as e:
                self.sent["failed"] += 1
                self.last_error = "%s: %s" % (kind, type(e).__name__)
                self.log("warn", "Could not send to %s (%s)" % ("the phone" if kind == "phone" else "the webhook", type(e).__name__))

    def _ntfy(self, d):
        topic = self.get_cfg().get("ntfy_topic", "")
        if not topic:
            return
        title = d["title"].encode("ascii", "ignore").decode()[:120]
        req = urllib.request.Request("https://ntfy.sh/" + topic, data=d["text"][:1000].encode("utf-8"), method="POST",
                                     headers={"Title": title, "Priority": d.get("prio", "default"), "Tags": "house",
                                              "User-Agent": "SARTHIK-Room"})
        urllib.request.urlopen(req, timeout=10).read(200)

    def _webhook(self, ev):
        url = self.get_cfg().get("webhook_url", "")
        if not url:
            return
        req = urllib.request.Request(url, data=json.dumps(ev).encode("utf-8"), method="POST",
                                     headers={"Content-Type": "application/json", "User-Agent": "SARTHIK-Room"})
        urllib.request.urlopen(req, timeout=10).read(200)


class Backups:
    KEEP = 14

    def __init__(self, base, data, db, log):
        self.dir = Path(base) / "backups"            # NOT inside a web-served folder
        self.data = Path(data)
        self.db = db
        self.log = log
        self.lock = threading.Lock()
        self.dir.mkdir(parents=True, exist_ok=True)
        threading.Thread(target=self._daily, daemon=True, name="backup").start()

    def list(self):
        out = []
        for d in sorted(self.dir.glob("backup_*"), reverse=True):
            size = sum(f.stat().st_size for f in d.glob("*") if f.is_file())
            out.append({"name": d.name, "size": size, "t": d.stat().st_mtime})
        return out

    def run(self, why="manual"):
        with self.lock:
            name = "backup_" + datetime.now().strftime("%Y%m%d_%H%M%S")
            d = self.dir / name
            d.mkdir()
            self.db.backup_to(d / "classync.db")
            for f in ("users.json", "settings.json", "requests.json"):
                if (self.data / f).exists():
                    shutil.copy2(self.data / f, d / f)
            unk = self.data / "unknown_faces"
            if unk.exists():
                shutil.make_archive(str(d / "unknown_faces"), "zip", str(unk))
            for old in sorted(self.dir.glob("backup_*"))[:-self.KEEP]:
                shutil.rmtree(old, ignore_errors=True)
            self.log("info", "Backup saved: %s (%s)" % (name, why))
            return name

    def _daily(self):
        time.sleep(60)
        while True:
            try:
                last = self.list()
                if not last or time.time() - last[0]["t"] > 24 * 3600:
                    self.run("daily")
            except Exception as e:
                self.log("warn", "Daily backup failed: %s" % type(e).__name__)
            time.sleep(3600)
