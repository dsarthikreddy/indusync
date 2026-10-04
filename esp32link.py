"""Optional link to the CLASSYNC door board (ESP32-S3).

Two ways to connect:
  Wi-Fi (recommended): reads http://<board address>/api/door a few times a second.
      Nothing is plugged in, the Arduino Serial Monitor can stay open, and the
      Wi-Fi door radar (who is standing at the door) is included.
  USB cable: reads the messages the board prints, like the Serial Monitor.

It only READS the messages the board already prints (the same ones you see in the
Arduino Serial Monitor). Nothing on the board is changed.
Used for: 'Checked in today' and the tailgating check (camera sees more people than checked in).
Note: only one program can use the COM port, so close the Arduino Serial Monitor first.
"""
import json
import re
import threading
import time
import urllib.error
import urllib.request
from collections import deque

try:
    import serial
except Exception:  # pragma: no cover
    serial = None

RE_ATT = re.compile(r">>> ATTENDANCE MARKED(?: \(phone key\))?: (\S+)")
RE_PHONE = re.compile(r">>> PHONE KEY ENTRY: (\S+)")
RE_VISIT = re.compile(r">>> VISITOR ENTRY: (\S+) \(pass (\w+)\)")
RESET_MARKERS = ("New day started", "===== CLASSYNC")
EVENTS = [
    (">>> ATTENDANCE MARKED", "ok"),
    (">>> PHONE KEY ENTRY", "ok"),
    (">>> VISITOR ENTRY", "ok"),
    ("already marked today", "info"),
    (">>> ACCESS DENIED", "warn"),
    (">>> REJECTED", "warn"),
    (">>> BLOCKED", "warn"),
    (">>> CARD LOCKED", "warn"),
    (">>> PASS ALREADY USED", "warn"),
    (">>> QR IGNORED", "info"),
    (">>> SECURITY", "alert"),
    (">>> NO FACE", "info"),
]


class Esp32Link:
    def __init__(self, alerts, on_door=None, hub=None, db=None):
        self.alerts = alerts
        self.hub = hub
        self.db = db
        self.v8 = {}                 # door board v8: people, Bluetooth, RFID, camera, health, R2 radar
        self._next_events = 0.0
        self._next_r2 = 0.0
        self.on_door = on_door
        self.mode = "usb"
        self.host = ""
        self.radar = {}
        self.door_log = []
        self.counts = {}
        self._last_radar_state = ""
        self._last_wait_count = None
        self.lock = threading.Lock()
        self._stop = threading.Event()
        self._thread = None
        self.enabled = False
        self.port = ""
        self.connected = False
        self.error = ""
        self.names = set()          # everyone who got in today (students, staff phones, visitor passes)
        self.events = deque(maxlen=40)

    def apply(self, cfg):
        """Start, stop or restart according to settings."""
        want = bool(cfg["esp32_link"])
        mode = cfg.get("esp32_mode", "usb")
        port = cfg["esp32_port"]
        host = (cfg.get("esp32_host") or "").strip()
        same = (want == self.enabled and mode == self.mode and port == self.port and host == self.host
                and (self._thread is not None) == want)
        if same:
            return
        self.stop()
        self.enabled, self.mode, self.port, self.host = want, mode, port, host
        if not want:
            return
        if mode == "wifi":
            if not host:
                self.error = "Type the door board's address (shown in its Serial Monitor at start-up)."
                return
            target = self._run_wifi
        else:
            if serial is None:
                self.error = "pyserial is not installed (run Install.bat again)."
                return
            target = self._run
        self._stop.clear()
        self._thread = threading.Thread(target=target, daemon=True, name="esp32link")
        self._thread.start()

    def stop(self):
        self._stop.set()
        th = self._thread
        if th is not None:
            th.join(timeout=4)
        self._thread = None
        self.connected = False

    def checked_in(self):
        with self.lock:
            return len(self.names)

    def state(self):
        with self.lock:
            return {
                "enabled": self.enabled,
                "mode": self.mode,
                "port": self.port if self.mode == "usb" else self.host,
                "connected": self.connected,
                "error": self.error,
                "checked_in": len(self.names),
                "names": sorted(self.names),
                "events": list(self.events)[:15],
                "radar": dict(self.radar),
                "counts": dict(self.counts),
                "door_log": list(self.door_log)[:12],
                "v8": dict(self.v8),
            }

    # ---------- Wi-Fi mode ----------
    def _run_wifi(self):
        url = "http://%s/api/door" % self.host
        misses = 0
        while not self._stop.is_set():
            try:
                req = urllib.request.Request(url, headers={"User-Agent": "CLASSYNC-CCTV"})
                with urllib.request.urlopen(req, timeout=4) as r:
                    if int(r.headers.get("Content-Length") or 0) > 200000:
                        raise ValueError("reply too big")
                    data = json.loads(r.read(200000).decode("utf-8", "ignore"))
                if not isinstance(data, dict) or data.get("device") != "CLASSYNC-DOOR":
                    raise ValueError("that address is not a CLASSYNC door board")
                self._absorb(data)
                self.connected = True
                self.error = ""
                misses = 0
                self._sync_v8(data)
            except Exception as e:
                misses += 1
                if misses >= 3:
                    self.connected = False
                    if self.hub is not None and misses == 15:      # about 30 s without answer
                        self.hub.board_state(False, self.v8.get("device_id", "door"))
                    msg = str(e)
                    if "timed out" in msg or "Errno" in msg or "urlopen" in msg:
                        self.error = ("Cannot reach the door board at %s. Is it powered on and on the "
                                      "same Wi-Fi? Check the address in its Serial Monitor." % self.host)
                    else:
                        self.error = "Door link problem: " + msg[:120]
                self._stop.wait(2)
                continue
            self._stop.wait(1.0)

    def _get(self, path, limit=400000):
        req = urllib.request.Request("http://%s%s" % (self.host, path), headers={"User-Agent": "CLASSYNC-CCTV"})
        with urllib.request.urlopen(req, timeout=5) as r:
            return json.loads(r.read(limit).decode("utf-8", "ignore"))

    def _sync_v8(self, data):
        """Door board v8: keep its status, copy new events and R2 radar visits into permanent memory."""
        if str(data.get("version", "")).split(".")[0] not in ("8", "9"):
            return
        dev = str(data.get("device_id") or "door-1")[:30]
        with self.lock:
            self.v8 = {k: data.get(k) for k in ("version", "device_id", "uptime", "time", "counts", "people", "ble",
                                                "rfid", "camera", "health", "radar_r2")}
            self.v8["seen"] = time.time()
        if self.hub is None or self.db is None:
            return
        self.hub.board_state(True, dev)
        self.hub.sync_board_people(data.get("people"))
        now = time.time()
        total = int((data.get("counts") or {}).get("events_total", 0) or 0)
        key = "board_seq:" + dev
        cursor = int(self.db.kv_get(key, 0) or 0)
        if total < cursor:                       # the board's memory was cleared: start again from 1
            cursor = 0
        if total > cursor and now >= self._next_events:
            self._next_events = now + 1.5
            for _ in range(6):                   # up to 360 events per round, oldest first
                page = self._get("/api/events?since=%d" % cursor)
                evs = page.get("events") or []
                if not evs:
                    break
                self.hub.ingest_board_events(dev, evs)
                cursor = max(cursor, max(int(e.get("seq", 0)) for e in evs))
                self.db.kv_set(key, cursor)
                if cursor >= total or self._stop.is_set():
                    break
        r2 = data.get("radar_r2") or {}
        if int(r2.get("visits", 0) or 0) > 0 and now >= self._next_r2:
            self._next_r2 = now + 60
            _, tot = self.db.r2_list(1)
            page = self._get("/api/r2?since=%d" % int(tot.get("last_number", 0)))
            self.hub.ingest_r2(page.get("visits") or [])

    def _absorb(self, data):
        """Takes the board's status and turns it into dashboard information."""
        radar = data.get("radar") or {}
        counts = data.get("counts") or {}
        log = data.get("door_log") or []
        names = set()
        for row in (data.get("attendance") or []):
            if isinstance(row, dict) and row.get("present") and row.get("name"):
                names.add(str(row["name"])[:20].lower())
        with self.lock:
            self.radar = {
                "state": str(radar.get("state", "off"))[:20],
                "score": float(radar.get("score", 0) or 0),
                "trigger": float(radar.get("trigger", 0) or 0),
                "at_door_seconds": int(radar.get("at_door_seconds", 0) or 0),
                "waiting": bool(radar.get("waiting")),
                "packets": int(radar.get("packets", 0) or 0),
            }
            self.counts = {k: int(v or 0) for k, v in counts.items() if isinstance(k, str)}
            self.door_log = [{
                "when": int(e.get("when", 0) or 0),
                "uptime": int(e.get("uptime", 0) or 0),
                "seconds": int(e.get("seconds", 0) or 0),
                "kind": str(e.get("kind", ""))[:30],
            } for e in log[:15] if isinstance(e, dict)]
            self.names = names
            state = self.radar["state"]
            waits = self.counts.get("waited")

        if state != self._last_radar_state:
            self._last_radar_state = state
            if state == "person_at_door":
                self.events.appendleft({"t": time.time(), "level": "info", "msg": "Wi-Fi radar: someone is at the door"})
                self.alerts.add("info", "Someone is at the door (Wi-Fi radar)", key="radar_here", cooldown=20)
        if waits is not None:
            if self._last_wait_count is not None and waits > self._last_wait_count:
                self.events.appendleft({"t": time.time(), "level": "alert",
                                        "msg": "Someone waited a full minute at the door"})
                self.alerts.add("alert", "Someone has been waiting at your door for 1 minute",
                                key="radar_wait", cooldown=30)
            self._last_wait_count = waits
        if self.on_door:
            try:
                self.on_door(self.radar, self.counts)
            except Exception:
                pass

    # ---------- reading thread ----------
    def _run(self):
        while not self._stop.is_set():
            ser = None
            try:
                ser = serial.Serial()
                ser.port = self.port
                ser.baudrate = 115200
                ser.timeout = 1
                ser.dtr = False          # do not reset the board when connecting
                ser.rts = False
                ser.open()
                self.connected, self.error = True, ""
                while not self._stop.is_set():
                    raw = ser.readline()
                    if raw:
                        self.handle_line(raw.decode("utf-8", "ignore").strip())
            except Exception as e:
                self.connected = False
                msg = str(e)
                if "Access is denied" in msg or "PermissionError" in msg or "busy" in msg.lower():
                    self.error = f"{self.port} is busy. Close the Arduino Serial Monitor (only one program can use it)."
                elif "could not open port" in msg or "FileNotFoundError" in msg or "No such file" in msg:
                    self.error = f"Door board not found on {self.port}. Check the USB cable and the port name."
                else:
                    self.error = "Door link problem: " + msg[:120]
                self._stop.wait(5)
            finally:
                if ser is not None:
                    try:
                        ser.close()
                    except Exception:
                        pass
                self.connected = False

    def handle_line(self, line):
        if not line:
            return
        with self.lock:
            if any(m in line for m in RESET_MARKERS):
                self.names.clear()
                self.events.appendleft({"t": time.time(), "level": "info",
                                        "msg": "Door board restarted / new day: check-ins reset"})
                return
            m = RE_ATT.search(line)
            if m:
                self.names.add(m.group(1).lower())
            m = RE_PHONE.search(line)
            if m:
                self.names.add(m.group(1).lower())
            m = RE_VISIT.search(line)
            if m:
                self.names.add("visitor:" + m.group(2))
            for marker, level in EVENTS:
                if marker in line:
                    text = line.replace(">>> ", "").strip()
                    self.events.appendleft({"t": time.time(), "level": level, "msg": text[:110]})
                    if level == "alert":
                        self.alerts.add("alert", "Door: " + text[:100], key="door:" + text[:40], cooldown=30)
                    break
