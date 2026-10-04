"""Permanent memory of the website: one SQLite file (data/classync.db). Built into Python, free.

Tables
  people   one row per registered person (the SAME id as on the door board, e.g. "sarthikreddy")
  faces    laptop face samples (512 numbers each) per person
  events   EVERY important event: door board, CCTV recognition, cross-check, system
  r2       Wi-Fi radar (R2) visits read from the door board's storage
  kv       small settings (e.g. last board event already copied)
"""
import json
import sqlite3
import threading
import time
from datetime import datetime

import numpy as np

SEVERITY = {
    # door board (ESP32)
    "entry_card_face": "ok", "entry_phone": "ok", "entry_visitor_qr": "ok",
    "unknown_card": "warn", "card_locked": "warn", "face_mismatch": "warn", "no_face": "info",
    "pass_reused": "warn", "security_lock": "alert", "fake_pass": "alert", "proxy_blocked": "alert",
    "phone_refused": "alert", "face_enrolled": "info", "phone_paired": "info", "board_started": "info",
    # laptop CCTV + cross-check
    "cctv_seen": "info", "cctv_left": "info", "cctv_reentered": "info", "cctv_unknown": "alert",
    "cross_verified": "ok", "mismatch_door_cctv": "alert", "no_door_auth": "warn",
    "not_verified": "info", "multiple_people": "warn", "face_registered": "info",
    "board_online": "info", "board_offline": "warn", "camera_started": "info", "camera_stopped": "info",
}
TITLES = {
    "entry_card_face": "Entered with card + face", "entry_phone": "Entered with phone key",
    "entry_visitor_qr": "Visitor entered with QR pass", "unknown_card": "Unknown RFID card",
    "card_locked": "Locked card tapped", "face_mismatch": "Face did not match the card",
    "no_face": "Card tapped, no face seen", "pass_reused": "Used visitor pass shown again",
    "security_lock": "Card locked: 3 wrong faces", "fake_pass": "FAKE visitor pass",
    "proxy_blocked": "Proxy enrolment blocked", "phone_refused": "Phone refused (no pairing code)",
    "face_enrolled": "Face saved on the door board", "phone_paired": "Phone key paired",
    "board_started": "Door board started", "cctv_seen": "Seen by CCTV", "cctv_left": "Left the CCTV view",
    "cctv_reentered": "Came back (re-entered)", "cctv_unknown": "UNKNOWN face on CCTV",
    "cross_verified": "Door entry confirmed by CCTV", "mismatch_door_cctv": "Door person is NOT the CCTV person",
    "no_door_auth": "Known person in room without door check-in", "not_verified": "Door entry not seen by CCTV",
    "multiple_people": "More people than door entries", "face_registered": "Face registered on the laptop",
    "board_online": "Door board connected", "board_offline": "Door board not reachable",
    "camera_started": "CCTV camera started", "camera_stopped": "CCTV camera stopped",
}
AUTH_TYPES = ("entry_card_face", "entry_phone")
MISMATCH_TYPES = ("face_mismatch", "mismatch_door_cctv", "no_door_auth", "security_lock", "proxy_blocked")


def day_of(ts):
    return datetime.fromtimestamp(ts).strftime("%Y-%m-%d")


class Store:
    def __init__(self, path):
        self.path = str(path)
        self.lock = threading.RLock()
        self.db = sqlite3.connect(self.path, check_same_thread=False, timeout=10)
        self.on_event = None          # called with every NEW event (integrations: webhook / phone)
        self.db.row_factory = sqlite3.Row
        with self.lock:
            c = self.db
            c.execute("PRAGMA journal_mode=WAL")
            c.execute("PRAGMA synchronous=NORMAL")
            c.executescript("""
            CREATE TABLE IF NOT EXISTS people(
              id TEXT PRIMARY KEY, name TEXT NOT NULL, source TEXT NOT NULL DEFAULT 'laptop',
              board_index INTEGER, card TEXT, face_on_board INTEGER DEFAULT 0, created REAL);
            CREATE TABLE IF NOT EXISTS faces(
              id INTEGER PRIMARY KEY AUTOINCREMENT, person_id TEXT NOT NULL, emb BLOB NOT NULL,
              created REAL, how TEXT);
            CREATE TABLE IF NOT EXISTS events(
              id INTEGER PRIMARY KEY AUTOINCREMENT, ts REAL NOT NULL, day TEXT NOT NULL, source TEXT NOT NULL,
              device TEXT, type TEXT NOT NULL, severity TEXT NOT NULL, person_id TEXT, name TEXT, detail TEXT,
              ext TEXT UNIQUE);
            CREATE INDEX IF NOT EXISTS ev_day ON events(day);
            CREATE INDEX IF NOT EXISTS ev_person ON events(person_id);
            CREATE INDEX IF NOT EXISTS ev_type ON events(type);
            CREATE TABLE IF NOT EXISTS r2(number INTEGER PRIMARY KEY, ts REAL, kind TEXT, seconds INTEGER);
            CREATE TABLE IF NOT EXISTS kv(k TEXT PRIMARY KEY, v TEXT);
            CREATE TABLE IF NOT EXISTS messages(
              id INTEGER PRIMARY KEY AUTOINCREMENT, ts REAL NOT NULL, sender TEXT NOT NULL,
              recipient TEXT NOT NULL, text TEXT NOT NULL, via TEXT DEFAULT 'web');
            CREATE INDEX IF NOT EXISTS msg_pair ON messages(sender, recipient);
            """)
            c.commit()

    # ---------- small settings ----------
    def kv_get(self, k, default=None):
        with self.lock:
            r = self.db.execute("SELECT v FROM kv WHERE k=?", (k,)).fetchone()
        return json.loads(r["v"]) if r else default

    def kv_set(self, k, v):
        with self.lock:
            self.db.execute("INSERT INTO kv(k,v) VALUES(?,?) ON CONFLICT(k) DO UPDATE SET v=excluded.v", (k, json.dumps(v)))
            self.db.commit()

    # ---------- events ----------
    def add_event(self, type_, source, person_id=None, name=None, detail="", ts=None, device="laptop", ext=None):
        ts = float(ts or time.time())
        sev = SEVERITY.get(type_, "info")
        with self.lock:
            try:
                cur = self.db.execute(
                    "INSERT INTO events(ts,day,source,device,type,severity,person_id,name,detail,ext) VALUES(?,?,?,?,?,?,?,?,?,?)",
                    (ts, day_of(ts), source, device, type_, sev, person_id or None, name or person_id or None,
                     (detail or "")[:200], ext))
                self.db.commit()
                rid = cur.lastrowid
            except sqlite3.IntegrityError:          # already stored (same board event)
                return None
        if self.on_event:
            try:
                self.on_event({"id": rid, "ts": ts, "type": type_, "title": TITLES.get(type_, type_), "severity": sev,
                               "source": source, "device": device, "person_id": person_id, "name": name or person_id,
                               "detail": (detail or "")[:200]})
            except Exception:
                pass
        return rid

    def events(self, day=None, person=None, types=None, severity=None, source=None, search=None,
               limit=200, before_id=None):
        q, a = "SELECT * FROM events WHERE 1=1", []
        if day:
            q += " AND day=?"; a.append(day)
        if person:
            q += " AND person_id=?"; a.append(person)
        if types:
            q += " AND type IN (%s)" % ",".join("?" * len(types)); a += list(types)
        if severity:
            q += " AND severity IN (%s)" % ",".join("?" * len(severity)); a += list(severity)
        if source:
            q += " AND source=?"; a.append(source)
        if search:
            q += " AND (name LIKE ? OR detail LIKE ? OR type LIKE ?)"; a += ["%" + search + "%"] * 3
        if before_id:
            q += " AND id<?"; a.append(int(before_id))
        q += " ORDER BY id DESC LIMIT ?"; a.append(int(max(1, min(limit, 5000))))
        with self.lock:
            rows = self.db.execute(q, a).fetchall()
        return [self._ev(r) for r in rows]

    @staticmethod
    def _ev(r):
        d = dict(r)
        d["title"] = TITLES.get(d["type"], d["type"].replace("_", " "))
        d.pop("ext", None)
        return d

    def count(self, where="1=1", args=()):
        with self.lock:
            return self.db.execute("SELECT COUNT(*) FROM events WHERE " + where, args).fetchone()[0]

    # ---------- people ----------
    def upsert_board_person(self, pid, index, card, face_on_board):
        with self.lock:
            r = self.db.execute("SELECT id FROM people WHERE id=?", (pid,)).fetchone()
            if r:
                self.db.execute("UPDATE people SET board_index=?, card=?, face_on_board=?, "
                                "source=CASE WHEN source='laptop' THEN 'both' ELSE source END WHERE id=?",
                                (index, card, int(bool(face_on_board)), pid))
            else:
                self.db.execute("INSERT INTO people(id,name,source,board_index,card,face_on_board,created) VALUES(?,?,?,?,?,?,?)",
                                (pid, pid, "board", index, card, int(bool(face_on_board)), time.time()))
            self.db.commit()

    def add_person(self, pid, name):
        with self.lock:
            if self.db.execute("SELECT 1 FROM people WHERE id=?", (pid,)).fetchone():
                return False
            self.db.execute("INSERT INTO people(id,name,source,created) VALUES(?,?,?,?)", (pid, name, "laptop", time.time()))
            self.db.commit()
            return True

    def rename_person(self, pid, name):
        with self.lock:
            self.db.execute("UPDATE people SET name=? WHERE id=?", (name, pid))
            self.db.commit()

    def delete_person(self, pid):
        with self.lock:
            self.db.execute("DELETE FROM faces WHERE person_id=?", (pid,))
            self.db.execute("DELETE FROM people WHERE id=? AND board_index IS NULL", (pid,))
            self.db.commit()

    def person(self, pid):
        with self.lock:
            r = self.db.execute("SELECT * FROM people WHERE id=?", (pid,)).fetchone()
        return dict(r) if r else None

    def people_list(self):
        with self.lock:
            return [dict(r) for r in self.db.execute("SELECT * FROM people ORDER BY name COLLATE NOCASE").fetchall()]

    # ---------- faces ----------
    def add_face(self, pid, emb, how):
        with self.lock:
            self.db.execute("INSERT INTO faces(person_id,emb,created,how) VALUES(?,?,?,?)",
                            (pid, np.asarray(emb, dtype=np.float32).tobytes(), time.time(), how))
            self.db.commit()

    def clear_faces(self, pid):
        with self.lock:
            self.db.execute("DELETE FROM faces WHERE person_id=?", (pid,))
            self.db.commit()

    def all_faces(self):
        with self.lock:
            rows = self.db.execute("SELECT person_id, emb FROM faces").fetchall()
        return [(r["person_id"], np.frombuffer(r["emb"], dtype=np.float32)) for r in rows]

    def face_counts(self):
        with self.lock:
            return {r[0]: r[1] for r in self.db.execute("SELECT person_id, COUNT(*) FROM faces GROUP BY person_id")}

    # ---------- statistics per person ----------
    def person_stats(self):
        with self.lock:
            rows = self.db.execute(f"""
              SELECT person_id,
                MIN(ts) AS first_seen, MAX(ts) AS last_seen,
                SUM(type IN ('cctv_seen','entry_card_face','entry_phone')) AS appearances,
                SUM(type IN {AUTH_TYPES}) AS auth_entries,
                SUM(type='cctv_seen') AS cctv_observations,
                SUM(type IN {MISMATCH_TYPES}) AS mismatches,
                SUM(type='cross_verified') AS verified,
                GROUP_CONCAT(DISTINCT source) AS sources,
                GROUP_CONCAT(DISTINCT device) AS devices
              FROM events WHERE person_id IS NOT NULL GROUP BY person_id""").fetchall()
        return {r["person_id"]: dict(r) for r in rows}

    def unknown_total(self):
        return self.count("type IN ('cctv_unknown','unknown_card')")

    # ---------- attendance for one day ----------
    def attendance(self, day):
        with self.lock:
            rows = self.db.execute(f"""
              SELECT person_id,
                MIN(CASE WHEN type IN {AUTH_TYPES} THEN ts END) AS first_entry,
                MAX(ts) AS last_seen,
                SUM(type IN {AUTH_TYPES}) AS entries,
                SUM(type='cross_verified') AS verified,
                SUM(type='cctv_seen') AS cctv,
                SUM(type IN {MISMATCH_TYPES}) AS mismatches,
                GROUP_CONCAT(DISTINCT CASE WHEN type='entry_card_face' THEN 'card+face'
                                           WHEN type='entry_phone' THEN 'phone key' END) AS methods
              FROM events WHERE day=? AND person_id IS NOT NULL GROUP BY person_id""", (day,)).fetchall()
            visitors = self.db.execute("SELECT ts, name, detail FROM events WHERE day=? AND type='entry_visitor_qr' ORDER BY ts",
                                       (day,)).fetchall()
        return [dict(r) for r in rows], [dict(v) for v in visitors]

    def days(self, limit=60):
        with self.lock:
            return [r[0] for r in self.db.execute("SELECT DISTINCT day FROM events ORDER BY day DESC LIMIT ?", (limit,))]

    # ---------- R2 radar visits ----------
    def add_r2(self, visits):
        n = 0
        with self.lock:
            for v in visits:
                try:
                    num = int(v.get("number", 0))
                    if num <= 0:
                        continue
                    t = float(v.get("time") or 0)
                    cur = self.db.execute("INSERT OR IGNORE INTO r2(number,ts,kind,seconds) VALUES(?,?,?,?)",
                                          (num, t if t > 1e9 else None, str(v.get("kind", ""))[:20], int(v.get("seconds") or 0)))
                    n += cur.rowcount
                except (TypeError, ValueError):
                    continue
            self.db.commit()
        return n

    def r2_list(self, limit=200):
        with self.lock:
            rows = self.db.execute("SELECT * FROM r2 ORDER BY number DESC LIMIT ?", (limit,)).fetchall()
            tot = self.db.execute("SELECT COUNT(*), SUM(kind='passed'), SUM(kind='stopped'), SUM(kind='waited'), "
                                  "MAX(number) FROM r2").fetchone()
        return [dict(r) for r in rows], {"visits": tot[0] or 0, "passed": tot[1] or 0, "stopped": tot[2] or 0,
                                         "waited": tot[3] or 0, "last_number": tot[4] or 0}

    # ---------- storage ----------
    def sizes(self):
        with self.lock:
            ev = self.db.execute("SELECT COUNT(*) FROM events").fetchone()[0]
            fa = self.db.execute("SELECT COUNT(*) FROM faces").fetchone()[0]
            pe = self.db.execute("SELECT COUNT(*) FROM people").fetchone()[0]
            r2 = self.db.execute("SELECT COUNT(*) FROM r2").fetchone()[0]
            first = self.db.execute("SELECT MIN(ts) FROM events").fetchone()[0]
        return {"events": ev, "faces": fa, "people": pe, "r2": r2, "since": first}

    def export_csv(self, rows):
        import csv
        import io
        buf = io.StringIO()
        w = csv.writer(buf)
        w.writerow(["id", "date_time", "source", "device", "type", "title", "severity", "person_id", "name", "detail"])
        for e in rows:
            w.writerow([e["id"], datetime.fromtimestamp(e["ts"]).strftime("%Y-%m-%d %H:%M:%S"), e["source"], e["device"],
                        e["type"], e["title"], e["severity"], e["person_id"] or "", e["name"] or "", e["detail"] or ""])
        return buf.getvalue()

    # ---------- 7-day trend for the dashboard chart ----------
    def trend(self, days=7):
        with self.lock:
            rows = self.db.execute(f"""SELECT day, SUM(type IN {AUTH_TYPES}) AS entries, SUM(severity='alert') AS alerts,
                SUM(type='cross_verified') AS verified, SUM(type IN ('cctv_unknown','unknown_card')) AS unknown,
                SUM(type='entry_visitor_qr') AS visitors FROM events GROUP BY day ORDER BY day DESC LIMIT ?""", (days,)).fetchall()
        return [dict(r) for r in reversed(rows)]

    # ---------- messages (chat between website users) ----------
    def add_message(self, sender, recipient, text, via="web"):
        with self.lock:
            cur = self.db.execute("INSERT INTO messages(ts,sender,recipient,text,via) VALUES(?,?,?,?,?)",
                                  (time.time(), sender, recipient, text, via))
            self.db.commit()
            return cur.lastrowid

    def messages(self, me, other, after=0, limit=200):
        """other = 'all' (family room) or a username (private chat between me and other)."""
        with self.lock:
            if other == "all":
                rows = self.db.execute("SELECT * FROM messages WHERE recipient='all' AND id>? ORDER BY id DESC LIMIT ?",
                                       (int(after), limit)).fetchall()
            else:
                rows = self.db.execute("SELECT * FROM messages WHERE id>? AND ((sender=? AND recipient=?) OR (sender=? AND recipient=?)) "
                                       "ORDER BY id DESC LIMIT ?", (int(after), me, other, other, me, limit)).fetchall()
        return [dict(r) for r in reversed(rows)]

    def unread_counts(self, me, since):
        with self.lock:
            rows = self.db.execute("SELECT CASE WHEN recipient='all' THEN 'all' ELSE sender END AS chat, COUNT(*) FROM messages "
                                   "WHERE id>? AND sender<>? AND (recipient='all' OR recipient=?) GROUP BY chat", (int(since), me, me)).fetchall()
        return {r[0]: r[1] for r in rows}

    def last_message_id(self):
        with self.lock:
            return self.db.execute("SELECT COALESCE(MAX(id),0) FROM messages").fetchone()[0]

    def backup_to(self, path):
        """Consistent copy of the live database (safe while the website runs)."""
        dst = sqlite3.connect(str(path))
        with self.lock:
            self.db.backup(dst)
        dst.close()
