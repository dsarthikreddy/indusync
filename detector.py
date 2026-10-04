"""AI counting of people and objects (Ultralytics YOLO11), fully on the laptop - nothing is uploaded.

Accuracy features:
- Median of the last 5 checks (about 2.5 s), so one bad frame never changes the count.
- Confidence threshold (default 45%) removes weak guesses.
- Model choice: auto (default), yolo11n (fastest), yolo11s (balanced), yolo11m (most accurate).
  "auto" starts with the balanced model and switches to the fast one if this laptop is too slow,
  so the video and dashboard always stay smooth.
- The AI may use only half of the CPU cores, the rest stays free for video and the website.
"""
import os
import csv
import threading
import time
from collections import Counter, deque
from datetime import datetime

import cv2
import numpy as np

PERSON = "person"


class Detector:
    def __init__(self, base, camera, store, on_result=None, on_notice=None):
        self.base = base
        self.on_notice = on_notice
        self._auto_pick = "yolo11s.pt"
        self._recent_ms = deque(maxlen=6)
        self.camera = camera
        self.store = store
        self.on_result = on_result
        self.lock = threading.Lock()
        self._stop = threading.Event()
        self._thread = None
        self.state = "off"           # off / loading / running / paused / error
        self.error = ""
        self._model = None
        self._model_name = None
        self._load_lock = threading.Lock()
        self.dets = []               # (name, confidence, (x1, y1, x2, y2))
        self.dets_time = 0.0
        self.counts = {}             # smoothed count per object type
        self.people = 0
        self.infer_ms = 0
        self._window = deque(maxlen=5)
        self.history = deque(maxlen=360)      # (time, people) every 10 s -> last hour
        self._last_hist = 0.0
        self._last_log = 0.0
        self.peak_today = 0
        self._peak_day = datetime.now().date()

    # ---------- control ----------
    def start(self):
        with self.lock:
            if self._thread is not None and self._thread.is_alive():
                return
            self._stop.clear()
            self._thread = threading.Thread(target=self._run, daemon=True, name="detector")
            self._thread.start()

    def stop(self):
        self._stop.set()
        th = self._thread
        if th is not None:
            th.join(timeout=10)
        with self.lock:
            self._thread = None
            self.state = "off"
            self.error = ""
            self.dets = []
            self.counts = {}
            self.people = 0
            self._window.clear()

    def snapshot_state(self):
        with self.lock:
            return {
                "state": self.state,
                "error": self.error,
                "model": self._model_name or self.store.get()["model"],
                "ms": self.infer_ms,
                "people": self.people,
                "counts": dict(self.counts),
                "peak_today": self.peak_today,
                "history": list(self.history),
            }

    def preload(self):
        """Loads the AI model in the background when the website starts, so Start is instant."""
        try:
            wanted = self._wanted_model(self.store.get())
            with self._load_lock:
                if self._model is None:
                    self._model, self._model_name = self._load(wanted), wanted
        except Exception:
            pass                              # the camera start will try again and show the error

    # ---------- AI thread ----------
    def _wanted_model(self, cfg):
        return self._auto_pick if cfg["model"] == "auto" else cfg["model"]

    def _load(self, name):
        try:
            import torch
            torch.set_num_threads(max(1, min(4, (os.cpu_count() or 2) // 2)))
        except Exception:
            pass
        try:
            from ultralytics import YOLO
        except Exception as e:  # pragma: no cover
            raise RuntimeError("AI engine is not installed. Close this window and run Install.bat again.") from e
        model = YOLO(str(self.base / name))           # downloads once into the app folder
        model.predict(np.zeros((480, 640, 3), np.uint8), verbose=False)   # warm-up
        return model

    def _run(self):
        last_frame_time = 0.0
        while not self._stop.is_set():
            cfg = self.store.get()
            if not cfg["detection_enabled"]:
                with self.lock:
                    self.state = "paused"
                    self.dets, self.counts, self.people = [], {}, 0
                    self._window.clear()
                self._stop.wait(0.5)
                continue

            wanted = self._wanted_model(cfg)
            if self._model is None or self._model_name != wanted:
                with self.lock:
                    self.state = "loading"
                    self.error = ""
                try:
                    with self._load_lock:                         # the preload may already have done it
                        if self._model is None or self._model_name != wanted:
                            self._model, self._model_name = self._load(wanted), wanted
                    self._window.clear()
                    self._recent_ms.clear()
                except Exception as e:
                    with self.lock:
                        self.state = "error"
                        self.error = ("Could not load the AI model (" + str(e)[:150] + "). "
                                      "The first start needs internet once to download it.")
                    self._stop.wait(10)
                    continue

            frame, ftime = self.camera.get_frame()
            if frame is None or ftime == last_frame_time:
                self._stop.wait(0.05)
                continue
            last_frame_time = ftime

            t0 = time.time()
            try:
                r = self._model.predict(frame, conf=float(cfg["confidence"]), imgsz=640, verbose=False)[0]
            except Exception as e:
                with self.lock:
                    self.state = "error"
                    self.error = ("AI error: " + str(e))[:200]
                self._stop.wait(2)
                continue

            names = r.names
            dets = []
            if r.boxes is not None and len(r.boxes):
                for c, p, b in zip(r.boxes.cls.tolist(), r.boxes.conf.tolist(), r.boxes.xyxy.tolist()):
                    dets.append((names[int(c)], float(p), tuple(int(v) for v in b)))
            ms = int((time.time() - t0) * 1000)
            self._recent_ms.append(ms)
            if (cfg["model"] == "auto" and self._auto_pick != "yolo11n.pt" and len(self._recent_ms) == 6
                    and sum(self._recent_ms) / 6 > 450):
                self._auto_pick = "yolo11n.pt"          # laptop too slow for the bigger model
                if self.on_notice:
                    self.on_notice("info", "AI switched to the fast model so the video stays smooth on this laptop.")

            self._window.append(Counter(d[0] for d in dets))
            smoothed = {}
            for name in set().union(*self._window):
                vals = sorted(c.get(name, 0) for c in self._window)
                m = vals[len(vals) // 2]                  # median: ignores one-frame mistakes
                if m > 0:
                    smoothed[name] = m
            people = smoothed.get(PERSON, 0)
            now = time.time()
            with self.lock:
                self.dets = dets
                self.dets_time = now
                self.counts = smoothed
                self.people = people
                self.infer_ms = ms
                self.state = "running"
                self.error = ""
            self._bookkeeping(now, people, smoothed, cfg)
            if self.on_result is not None:
                try:
                    self.on_result(people, smoothed)
                except Exception:
                    pass

            wait = cfg["detect_every_ms"] / 1000.0 - (time.time() - t0)
            if wait > 0:
                self._stop.wait(wait)

    def _bookkeeping(self, now, people, counts, cfg):
        today = datetime.now().date()
        with self.lock:
            if today != self._peak_day:
                self._peak_day = today
                self.peak_today = 0
            self.peak_today = max(self.peak_today, people)
            if now - self._last_hist >= 10:
                self.history.append((int(now), people))
                self._last_hist = now
        if cfg["log_counts"] and now - self._last_log >= 30:
            self._last_log = now
            path = self.base / "data" / "headcount_log.csv"
            new = not path.exists()
            try:
                with open(path, "a", newline="", encoding="utf-8") as f:
                    w = csv.writer(f)
                    if new:
                        w.writerow(["time", "people", "objects"])
                    objs = "; ".join(f"{k}:{v}" for k, v in sorted(counts.items()) if k != PERSON)
                    w.writerow([datetime.now().strftime("%Y-%m-%d %H:%M:%S"), people, objs])
            except OSError:
                pass

    # ---------- drawing (called by the camera thread for every picture) ----------
    def draw(self, frame):
        cfg = self.store.get()
        out = frame.copy()
        h, w = out.shape[:2]
        s = max(0.45, w / 1280.0 * 0.6)
        thick = 1 if w < 1000 else 2
        with self.lock:
            dets = self.dets
            age = time.time() - self.dets_time
            people = self.people
            running = self.state == "running"

        if cfg["show_boxes"] and cfg["detection_enabled"] and age < 2.0:
            for name, conf, (x1, y1, x2, y2) in dets:
                color = (238, 211, 34) if name == PERSON else (11, 158, 245)   # cyan people, amber objects
                cv2.rectangle(out, (x1, y1), (x2, y2), color, 2)
                label = f"{name} {int(conf * 100)}%"
                (tw, th), _ = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, s, thick)
                ty = max(y1, th + 8)
                cv2.rectangle(out, (x1, ty - th - 8), (x1 + tw + 8, ty), color, -1)
                cv2.putText(out, label, (x1 + 4, ty - 5), cv2.FONT_HERSHEY_SIMPLEX, s, (15, 15, 15), thick, cv2.LINE_AA)

        # CCTV-style top bar: date, time and live people count
        bar = int(34 * s / 0.6)
        roi = out[0:bar, 0:w]
        dark = np.zeros_like(roi)
        cv2.addWeighted(dark, 0.55, roi, 0.45, 0, dst=roi)
        text = "CLASSYNC ROOM   " + datetime.now().strftime("%Y-%m-%d  %H:%M:%S")
        cv2.putText(out, text, (10, int(bar * 0.7)), cv2.FONT_HERSHEY_SIMPLEX, s, (240, 240, 240), thick, cv2.LINE_AA)
        if cfg["detection_enabled"] and running:
            ptxt = f"PEOPLE: {people}"
            (tw, _), _ = cv2.getTextSize(ptxt, cv2.FONT_HERSHEY_SIMPLEX, s, thick + 1)
            cv2.putText(out, ptxt, (w - tw - 12, int(bar * 0.7)), cv2.FONT_HERSHEY_SIMPLEX, s,
                        (238, 211, 34), thick + 1, cv2.LINE_AA)
        return out
