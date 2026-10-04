"""The CROSS-CHECK ENGINE: door board (ESP32) authentication + laptop CCTV recognition.

The door board is the primary security (card + face, phone key, QR pass). The CCTV is a SECOND,
independent check - never a single point of failure: if the camera is off, the door still works
and every door event is still stored; the cross-check simply waits until the camera is on.

Rules (every result becomes a permanent event):
  door entry for P, CCTV then sees P             -> cross_verified        (ok)
  door entry for P, CCTV sees ANOTHER known Q    -> mismatch_door_cctv    (alert)  (card/phone/face of P, body of Q)
  door entry for P, CCTV sees nobody known       -> not_verified          (info)   (face not visible / camera angle)
  CCTV sees known Q with NO door entry today     -> no_door_auth          (warn)   (only when the door board is linked)
  CCTV sees an unregistered face                 -> cctv_unknown          (alert)  (photo saved as evidence)
  more faces/people than door entries            -> multiple_people       (warn)   (tailgating)
  person seen / left / came back                 -> cctv_seen / cctv_left / cctv_reentered
"""
import threading
import time
from collections import deque
from datetime import datetime

from .db import AUTH_TYPES, TITLES

VERIFY_WINDOW = 90.0         # after a door entry, the CCTV has 90 s to see that person
REENTRY_WINDOW = 4 * 3600    # coming back within 4 hours = "re-entered"
MULTI_COOLDOWN = 300.0


class Hub:
    def __init__(self, db, alerts):
        self.db = db
        self.alerts = alerts
        self.lock = threading.RLock()
        self.syslog = deque(maxlen=800)
        self.pending = {}            # person id -> {"t", "how", "others": set()}
        self.auth_today = set()
        self._auth_day = ""
        self._no_auth_warned = {}    # person id -> day already warned
        self._last_multi = 0.0
        self.board_linked = False
        self.camera_on = False
        self.face_running = False
        self._refresh_auth(force=True)

    # ---------- system log ----------
    def log(self, level, msg):
        self.syslog.appendleft({"t": time.time(), "level": level, "msg": str(msg)[:300]})

    def logs(self, n=300):
        return list(self.syslog)[:n]

    def _alert(self, level, msg, key, cooldown=30):
        try:
            self.alerts.add(level, msg, key=key, cooldown=cooldown)
        except Exception:
            pass

    def _refresh_auth(self, force=False):
        day = datetime.now().strftime("%Y-%m-%d")
        if force or day != self._auth_day:
            rows = self.db.events(day=day, types=AUTH_TYPES, limit=5000)
            with self.lock:
                self._auth_day = day
                self.auth_today = {r["person_id"] for r in rows if r["person_id"]}

    # ---------- door board ----------
    def sync_board_people(self, people):
        for p in people or []:
            try:
                pid = str(p.get("id", "")).strip()[:40]
                if pid:
                    self.db.upsert_board_person(pid, int(p.get("index", -1)), str(p.get("card", ""))[:20],
                                                bool(p.get("face_saved")))
            except Exception:
                continue

    def ingest_board_events(self, device, events):
        """Copies new door events into permanent memory. Returns how many were new."""
        new = 0
        now = time.time()
        for e in events or []:
            try:
                seq = int(e.get("seq", 0))
                typ = str(e.get("type", "event"))[:40]
                t = float(e.get("time") or 0)
                received_now = t < 1.5e9            # board clock not set yet: use the arrival time
                ts = now if received_now else t
                person = str(e.get("person") or "").strip() or None
                name = str(e.get("name") or "").strip() or person
                ext = "%s:%d:%d:%d" % (device, seq, int(t), int(e.get("uptime", 0) or 0))
                rid = self.db.add_event(typ, "door", person_id=person, name=name, detail=str(e.get("detail", ""))[:120],
                                        ts=ts, device=device, ext=ext)
                if rid is None:
                    continue
                new += 1
                if typ in AUTH_TYPES and person:
                    with self.lock:
                        self.auth_today.add(person)
                    if now - ts < VERIFY_WINDOW:     # only fresh entries are cross-checked
                        with self.lock:
                            self.pending[person] = {"t": ts, "how": typ, "others": set()}
                level = {"alert": "alert", "warn": "warn"}.get(self._sev(typ))
                if level:
                    self._alert(level, "Door: %s%s" % (TITLES.get(typ, typ), (" (" + name + ")") if name else ""),
                                key="door_" + typ + (name or ""), cooldown=20)
            except Exception as ex:
                self.log("warn", "Could not read a door event: %s" % ex)
        if new:
            self.log("info", "Copied %d new door event(s) into permanent memory" % new)
        return new

    @staticmethod
    def _sev(typ):
        from .db import SEVERITY
        return SEVERITY.get(typ, "info")

    def ingest_r2(self, visits):
        n = self.db.add_r2(visits)
        if n:
            self.log("info", "Copied %d Wi-Fi radar (R2) visit(s)" % n)
        return n

    def board_state(self, connected, device):
        if connected != self.board_linked:
            self.board_linked = connected
            self.db.add_event("board_online" if connected else "board_offline", "system", detail=device or "", device=device or "door")
            self.log("info" if connected else "warn", "Door board %s" % ("connected" if connected else "NOT reachable"))

    # ---------- CCTV ----------
    def camera_state(self, on):
        if on != self.camera_on:
            self.camera_on = on
            self.db.add_event("camera_started" if on else "camera_stopped", "system", device="laptop")

    def on_seen(self, pid, ts, last_left):
        self._refresh_auth()
        if last_left and ts - last_left < REENTRY_WINDOW:
            self.db.add_event("cctv_reentered", "cctv", person_id=pid,
                              detail="back after %s" % _dur(ts - last_left), ts=ts, device="laptop-cam")
        self.db.add_event("cctv_seen", "cctv", person_id=pid, detail="recognised by face", ts=ts, device="laptop-cam")
        with self.lock:
            mine = self.pending.pop(pid, None)
            others = [(p, v) for p, v in self.pending.items() if ts - v["t"] <= VERIFY_WINDOW]
        if mine:
            self.db.add_event("cross_verified", "crosscheck", person_id=pid,
                              detail="door (%s) and CCTV agree" % _how(mine["how"]), ts=ts, device="laptop-cam")
            return
        # someone else just authenticated at the door, but the camera sees THIS person instead
        if others and pid not in self.auth_today:
            for door_pid, v in others:
                if pid in v["others"]:
                    continue
                v["others"].add(pid)
                self.db.add_event("mismatch_door_cctv", "crosscheck", person_id=door_pid,
                                  detail="door: %s (%s) but CCTV sees %s" % (door_pid, _how(v["how"]), pid), ts=ts,
                                  device="laptop-cam")
                self._alert("alert", "MISMATCH: door entry was %s, but the camera sees %s" % (door_pid, pid),
                            key="mm" + door_pid + pid, cooldown=60)
        # a known person in the room who never checked in at the door today
        if self.board_linked and pid not in self.auth_today:
            day = datetime.now().strftime("%Y-%m-%d")
            if self._no_auth_warned.get(pid) != day:
                self._no_auth_warned[pid] = day
                self.db.add_event("no_door_auth", "crosscheck", person_id=pid,
                                  detail="seen in the room without a door check-in today", ts=ts, device="laptop-cam")
                self._alert("warn", "%s is in the room but did not check in at the door today" % pid,
                            key="noauth" + pid, cooldown=600)

    def on_left(self, pid, start, end):
        self.db.add_event("cctv_left", "cctv", person_id=pid, detail="was visible for %s" % _dur(end - start),
                          ts=end, device="laptop-cam")

    def on_unknown(self, ts, photo, count):
        self.db.add_event("cctv_unknown", "cctv", name="unknown person",
                          detail=("photo " + photo) if photo else "%d unknown face(s)" % count, ts=ts, device="laptop-cam")
        self._alert("alert", "UNKNOWN face seen by the CCTV camera", key="unknownface", cooldown=60)

    def tick(self, people_count, faces_now, camera_live):
        """Called about once a second from the main program."""
        now = time.time()
        self._refresh_auth()
        self.camera_state(camera_live)
        with self.lock:
            expired = [(p, v) for p, v in self.pending.items() if now - v["t"] > VERIFY_WINDOW]
            for p, _ in expired:
                self.pending.pop(p, None)
        for p, v in expired:
            if camera_live:
                self.db.add_event("not_verified", "crosscheck", person_id=p,
                                  detail="door entry (%s) but the CCTV did not see this face" % _how(v["how"]), device="laptop-cam")
        # more people / faces than door entries -> possible tailgating
        if camera_live and self.board_linked and now - self._last_multi > MULTI_COOLDOWN:
            with self.lock:
                fresh = sum(1 for v in self.pending.values() if now - v["t"] < VERIFY_WINDOW)
            if fresh >= 1 and faces_now > fresh:
                self._last_multi = now
                self.db.add_event("multiple_people", "crosscheck",
                                  detail="%d faces right after %d door entry" % (faces_now, fresh), device="laptop-cam")
                self._alert("warn", "Possible tailgating: %d faces after %d door entry" % (faces_now, fresh), key="multi", cooldown=120)


def _how(t):
    return {"entry_card_face": "card + face", "entry_phone": "phone key"}.get(t, t)


def _dur(s):
    s = int(max(0, s))
    if s < 60:
        return "%d s" % s
    if s < 3600:
        return "%d min %d s" % (s // 60, s % 60)
    return "%d h %d min" % (s // 3600, (s % 3600) // 60)
