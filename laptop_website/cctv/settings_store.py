"""Settings that are saved permanently in data/settings.json."""
import json
import os
import re
import threading
from pathlib import Path

BASE = Path(__file__).resolve().parent.parent
DATA = BASE / "data"

SETTINGS_VERSION = 3

DEFAULTS = {
    "settings_version": SETTINGS_VERSION,
    "camera_index": 0,            # 0 = first camera. External USB webcam is often 1 on laptops
    "resolution": "1280x720",
    "fps": 12,
    "jpeg_quality": 75,
    "detection_enabled": True,
    "model": "auto",              # auto = best model your laptop can run smoothly
    "confidence": 0.45,
    "detect_every_ms": 900,
    "show_boxes": True,
    "face_enabled": True,         # laptop face RECOGNITION (second check next to the door board)
    "face_threshold": 0.40,       # how similar a face must be to count as that person (0.30 - 0.70)
    "face_interval": 0.5,         # seconds between face checks (slower laptops wait longer automatically)
    "door_key": "ChangeMeDoorKey123",            # must match DOOR_API_KEY in the door board's secrets.h
    "ntfy_topic": "change-me-topic",  # your phone notification channel (ntfy app)
    "phone_alerts": True,         # laptop also sends CCTV / cross-check alerts to the phone
    "webhook_url": "",            # Stage 3: n8n / Google Sheets / Telegram / WhatsApp / email (empty = off)
    "webhook_events": "alerts",   # "alerts" = only warnings and alerts, "all" = every event
    "max_occupancy": 40,
    "auto_stop_minutes": 0,       # 0 = keep running until you press Stop
    "allow_lan": True,            # phones on the same Wi-Fi
    "allow_tailscale": True,      # phones/people you invited through Tailscale
    "viewers_can_start": False,
    "log_counts": True,           # numbers only (people/objects), never pictures
    "esp32_link": False,
    "esp32_mode": "wifi",         # "wifi" = read the door board over Wi-Fi, "usb" = USB cable
    "esp32_host": "",             # the door board's address, e.g. 192.168.0.12
    "esp32_port": "COM4",
    "public_access": False,       # public website link (Tailscale Funnel) - OFF until you switch it on
    "allow_requests": True,       # visitors may ask for access; you approve or deny
    "google_verification": "",    # optional Google Search Console code
    "port": 8000,
}

# key -> (type, allowed values or (min, max))
SCHEMA = {
    "camera_index": (int, (0, 9)),
    "resolution": (str, ["640x480", "1280x720", "1920x1080"]),
    "fps": (int, (1, 30)),
    "jpeg_quality": (int, (40, 95)),
    "detection_enabled": (bool, None),
    "model": (str, ["auto", "yolo11n.pt", "yolo11s.pt", "yolo11m.pt"]),
    "confidence": (float, (0.2, 0.9)),
    "detect_every_ms": (int, (200, 5000)),
    "show_boxes": (bool, None),
    "face_enabled": (bool, None),
    "face_threshold": (float, (0.30, 0.70)),
    "face_interval": (float, (0.15, 2.0)),
    "door_key": (str, re.compile(r"^[A-Za-z0-9]{8,64}$")),
    "ntfy_topic": (str, re.compile(r"^[A-Za-z0-9_-]{6,64}$")),
    "phone_alerts": (bool, None),
    "webhook_url": (str, re.compile(r"^(|https?://[A-Za-z0-9._~:/?#\[\]@!$&()*+,;=%-]{4,300})$")),
    "webhook_events": (str, ["alerts", "all"]),
    "max_occupancy": (int, (1, 500)),
    "auto_stop_minutes": (int, (0, 720)),
    "allow_lan": (bool, None),
    "allow_tailscale": (bool, None),
    "viewers_can_start": (bool, None),
    "log_counts": (bool, None),
    "esp32_link": (bool, None),
    "esp32_port": (str, re.compile(r"^(COM\d{1,3}|/dev/[\w.\-]+)$")),
    "esp32_mode": (str, ["wifi", "usb"]),
    "esp32_host": (str, re.compile(r"^(|[A-Za-z0-9.\-]{1,60}(:\d{1,5})?)$")),
    "public_access": (bool, None),
    "allow_requests": (bool, None),
    "google_verification": (str, re.compile(r"^[A-Za-z0-9_\-]{0,100}$")),
}


class SettingsStore:
    def __init__(self):
        self._lock = threading.Lock()
        DATA.mkdir(parents=True, exist_ok=True)
        self._path = DATA / "settings.json"
        self._cfg = dict(DEFAULTS)
        if self._path.exists():
            try:
                saved = json.loads(self._path.read_text(encoding="utf-8"))
                for k, v in saved.items():
                    if k in DEFAULTS:
                        self._cfg[k] = v
                if saved.get("settings_version", 1) < 3 and not saved.get("esp32_mode"):
                    self._cfg["esp32_mode"] = "usb" if saved.get("esp32_link") else "wifi"
                    self._cfg["settings_version"] = SETTINGS_VERSION
                if saved.get("settings_version", 1) < 2:
                    # upgrade from version 1: smoother defaults for everyday laptops
                    self._cfg["model"] = "auto"
                    self._cfg["detect_every_ms"] = max(700, int(self._cfg.get("detect_every_ms", 700)))
                    self._cfg["settings_version"] = SETTINGS_VERSION
            except Exception:
                pass
        self._save()

    def get(self):
        with self._lock:
            return dict(self._cfg)

    def _save(self):
        tmp = self._path.with_suffix(".tmp")
        tmp.write_text(json.dumps(self._cfg, indent=2), encoding="utf-8")
        os.replace(tmp, self._path)

    def update(self, data):
        """Validates and saves. Returns the set of changed keys. Raises ValueError."""
        clean = {}
        for k, v in data.items():
            if k not in SCHEMA:
                raise ValueError(f"Unknown setting: {k}")
            typ, rule = SCHEMA[k]
            if typ is bool:
                if not isinstance(v, bool):
                    raise ValueError(f"{k} must be on/off")
            elif typ is int:
                if isinstance(v, bool):
                    raise ValueError(f"{k} must be a number")
                v = int(v)
                lo, hi = rule
                if not lo <= v <= hi:
                    raise ValueError(f"{k} must be between {lo} and {hi}")
            elif typ is float:
                v = float(v)
                lo, hi = rule
                if not lo <= v <= hi:
                    raise ValueError(f"{k} must be between {lo} and {hi}")
            elif typ is str:
                v = str(v).strip()
                if isinstance(rule, list) and v not in rule:
                    raise ValueError(f"{k} must be one of {rule}")
                if hasattr(rule, "match") and not rule.match(v):
                    raise ValueError(f"{k} has a wrong format")
            clean[k] = v
        with self._lock:
            changed = {k for k, v in clean.items() if self._cfg.get(k) != v}
            self._cfg.update(clean)
            self._save()
        return changed
