"""USB webcam service.

- The webcam is opened ONLY when someone presses Start, and fully released on Stop.
- Fast start on Windows: DirectShow first (about 1 s). If the chosen camera number does not
  answer, the other numbers are tried automatically. The slow Media Foundation method is
  used only as the very last option, with its slow start-up switched off.
- Pictures are compressed only while someone is watching (saves a lot of CPU).
- Two streams: HD for laptops, "data saver" (640 px, fewer frames) for phones / internet.
"""
import os

os.environ.setdefault("OPENCV_VIDEOIO_MSMF_ENABLE_HW_TRANSFORMS", "0")   # removes 10-30 s MSMF start delay
os.environ.setdefault("OPENCV_LOG_LEVEL", "ERROR")

import threading
import time
from datetime import datetime

import cv2
import numpy as np

LOW_WIDTH = 640
LOW_QUALITY = 55
LOW_MAX_FPS = 10


def _keep_awake(on):
    """Stops Windows from sleeping while the camera runs (does nothing on other systems)."""
    if os.name != "nt":
        return
    try:
        import ctypes
        ES_CONTINUOUS, ES_SYSTEM_REQUIRED = 0x80000000, 0x00000001
        flags = ES_CONTINUOUS | ES_SYSTEM_REQUIRED if on else ES_CONTINUOUS
        ctypes.windll.kernel32.SetThreadExecutionState(flags)
    except Exception:
        pass


def _release(cap):
    try:
        cap.release()
    except Exception:          # some webcam drivers throw here - never let that crash the app
        pass


class CameraService:
    def __init__(self, base):
        self.base = base
        self.lock = threading.Lock()
        self._rec_lock = threading.Lock()
        self._stop = threading.Event()
        self._thread = None
        self.running = False
        self.state = "off"          # off / starting / live / error
        self.error = ""
        self.detail = ""            # what the camera is doing right now (shown while starting)
        self.index_used = None
        self.frame = None           # latest clean picture (for AI, recording, snapshots)
        self.frame_time = 0.0
        self._jpeg = {"high": None, "low": None}
        self._fid = {"high": 0, "low": 0}
        self.viewers = {"high": 0, "low": 0}
        self.width = 0
        self.height = 0
        self.fps_actual = 0.0
        self.last_viewer_time = time.time()
        self.started_at = 0.0
        self.start_click = 0.0
        self.overlay_fn = None      # draws boxes + timestamp
        self.on_notice = None       # callback(level, message)
        self.recording = False
        self.record_file = ""
        self._writer = None
        self._rec_fps = 15
        self._quality = 75

    # ---------- public ----------
    def total_viewers(self):
        return self.viewers["high"] + self.viewers["low"]

    def start(self, cfg):
        with self.lock:
            if self.running:
                return
            self.running = True
            self.state = "starting"
            self.error = ""
            self.detail = "Opening the webcam..."
            self.start_click = time.time()
            self._stop.clear()
            self._thread = threading.Thread(target=self._run, args=(dict(cfg),), daemon=True, name="camera")
            self._thread.start()

    def stop(self):
        with self.lock:
            th = self._thread
            self._stop.set()
        if th is not None:
            th.join(timeout=8)
        self.set_recording(False)
        with self.lock:
            self._thread = None
            self.running = False
            self.state = "off"
            self.error = ""
            self.detail = ""
            self._jpeg = {"high": None, "low": None}
            self.frame = None
            self.fps_actual = 0.0

    def get_jpeg(self, q):
        with self.lock:
            return self._fid[q], self._jpeg[q]

    def get_frame(self):
        with self.lock:
            return self.frame, self.frame_time

    def snapshot(self):
        with self.lock:
            frame = self.frame
        if frame is None:
            return None
        shown = self._overlay(frame)
        ok, buf = cv2.imencode(".jpg", shown, [cv2.IMWRITE_JPEG_QUALITY, 90])
        if not ok:
            return None
        folder = self.base / "snapshots"
        folder.mkdir(exist_ok=True)
        name = datetime.now().strftime("snap_%Y%m%d_%H%M%S.jpg")
        (folder / name).write_bytes(buf.tobytes())
        return name

    def set_recording(self, on):
        with self._rec_lock:
            if on and self._writer is None:
                with self.lock:
                    frame = self.frame
                if not self.running or frame is None:
                    return False
                h, w = frame.shape[:2]
                folder = self.base / "recordings"
                folder.mkdir(exist_ok=True)
                name = datetime.now().strftime("rec_%Y%m%d_%H%M%S.mp4")
                wr = cv2.VideoWriter(str(folder / name), cv2.VideoWriter_fourcc(*"mp4v"),
                                     float(self._rec_fps), (w, h))
                if not wr.isOpened():
                    return False
                self._writer = wr
                self.recording = True
                self.record_file = name
                return True
            if not on and self._writer is not None:
                try:
                    self._writer.release()
                except Exception:
                    pass
                self._writer = None
                self.recording = False
            return self.recording == on

    @staticmethod
    def scan(max_index=4):
        """Lists cameras that really give a picture. Never crashes, even with odd drivers."""
        found = []
        backend = cv2.CAP_DSHOW if os.name == "nt" else cv2.CAP_ANY
        for i in range(max_index):
            cap = None
            try:
                cap = cv2.VideoCapture(i, backend)
                if cap.isOpened():
                    ok, fr = cap.read()
                    if ok and fr is not None and fr.size:
                        found.append({"index": i, "size": f"{fr.shape[1]}x{fr.shape[0]}"})
            except Exception:
                pass
            finally:
                if cap is not None:
                    _release(cap)
        return found

    # ---------- helpers ----------
    def _overlay(self, frame):
        if self.overlay_fn is None:
            return frame
        try:
            return self.overlay_fn(frame)
        except Exception:
            return frame

    def _notice(self, level, msg):
        if self.on_notice:
            try:
                self.on_notice(level, msg)
            except Exception:
                pass

    def _try_open(self, idx, backend, cfg, label):
        if self._stop.is_set():
            return None
        with self.lock:
            self.detail = f"Trying camera {idx} ({label})..."
        cap = None
        try:
            cap = cv2.VideoCapture(idx, backend)
            if not cap.isOpened():
                _release(cap)
                return None
            w, h = map(int, cfg["resolution"].split("x"))
            cap.set(cv2.CAP_PROP_FOURCC, cv2.VideoWriter_fourcc(*"MJPG"))   # USB webcams: fast HD
            cap.set(cv2.CAP_PROP_FRAME_WIDTH, w)
            cap.set(cv2.CAP_PROP_FRAME_HEIGHT, h)
            cap.set(cv2.CAP_PROP_FPS, int(cfg["fps"]))
            try:
                cap.set(cv2.CAP_PROP_BUFFERSIZE, 1)
            except Exception:
                pass
            for _ in range(25):
                if self._stop.is_set():
                    break
                ok, frame = cap.read()
                if ok and frame is not None and frame.size:
                    return cap
                time.sleep(0.04)
        except Exception:
            pass
        if cap is not None:
            _release(cap)
        return None

    def _open(self, cfg):
        fake = os.environ.get("CCTV_FAKE_CAMERA")
        if fake:
            return _FakeCamera(fake, cfg), int(cfg["camera_index"])
        want = int(cfg["camera_index"])
        if os.name != "nt":
            return self._try_open(want, cv2.CAP_ANY, cfg, "default"), want
        order = [want] + [i for i in range(4) if i != want]
        for i in order:
            cap = self._try_open(i, cv2.CAP_DSHOW, cfg, "fast mode")
            if cap is not None:
                return cap, i
        cap = self._try_open(want, cv2.CAP_MSMF, cfg, "compatibility mode")
        return cap, want

    def _fail(self, msg):
        self.set_recording(False)
        with self.lock:
            self.state = "error"
            self.error = msg
            self.detail = ""
            self.running = False
            self._jpeg = {"high": None, "low": None}
            self.frame = None
            self.fps_actual = 0.0

    # ---------- camera thread ----------
    def _run(self, cfg):
        cap, used = self._open(cfg)
        if self._stop.is_set():
            if cap is not None:
                _release(cap)
            return
        if cap is None:
            self._fail("No webcam answered. Check the USB cable, close Zoom/Teams/Camera app, "
                       "then press Try again.")
            return
        want = int(cfg["camera_index"])
        if used != want:
            self._notice("warn", f"Camera {want} did not answer, so camera {used} is used. "
                                 f"If this is the wrong camera, change 'Camera number' in Settings.")
        interval = 1.0 / max(1, int(cfg["fps"]))
        self._quality = int(cfg["jpeg_quality"])
        self._rec_fps = int(cfg["fps"])
        fails = 0
        next_t = 0.0
        last_low = 0.0
        count, t0 = 0, time.time()
        with self.lock:
            self.state = "live"
            self.detail = ""
            self.index_used = used
            self.started_at = time.time()
        _keep_awake(True)
        try:
            while not self._stop.is_set():
                ok, frame = cap.read()
                if not ok or frame is None:
                    fails += 1
                    if fails > 60:
                        self._fail("The webcam stopped sending pictures (USB unplugged?). Press Try again.")
                        return
                    time.sleep(0.05)
                    continue
                fails = 0
                now = time.time()
                if now < next_t:
                    continue
                next_t = next_t + interval if next_t else now + interval
                if next_t < now:
                    next_t = now + interval
                with self.lock:
                    self.frame = frame
                    self.frame_time = now
                    self.height, self.width = frame.shape[:2]
                    want_high = self.viewers["high"] > 0
                    want_low = self.viewers["low"] > 0 and now - last_low >= 1.0 / LOW_MAX_FPS
                with self._rec_lock:
                    if self._writer is not None:
                        self._writer.write(frame)
                if want_high or want_low:
                    shown = self._overlay(frame)
                    if want_high:
                        ok2, buf = cv2.imencode(".jpg", shown, [cv2.IMWRITE_JPEG_QUALITY, self._quality])
                        if ok2:
                            with self.lock:
                                self._jpeg["high"] = buf.tobytes()
                                self._fid["high"] += 1
                    if want_low:
                        last_low = now
                        h, w = shown.shape[:2]
                        small = shown if w <= LOW_WIDTH else cv2.resize(
                            shown, (LOW_WIDTH, int(h * LOW_WIDTH / w)), interpolation=cv2.INTER_AREA)
                        ok3, buf = cv2.imencode(".jpg", small, [cv2.IMWRITE_JPEG_QUALITY, LOW_QUALITY])
                        if ok3:
                            with self.lock:
                                self._jpeg["low"] = buf.tobytes()
                                self._fid["low"] += 1
                count += 1
                if now - t0 >= 2.0:
                    self.fps_actual = count / (now - t0)
                    count, t0 = 0, now
        finally:
            _keep_awake(False)
            _release(cap)


class _FakeCamera:
    """Test camera (only when the CCTV_FAKE_CAMERA variable is set): shows pictures like a video."""

    def __init__(self, spec, cfg):
        w, h = map(int, cfg["resolution"].split("x"))
        self.frames = []
        for p in spec.split(";"):
            img = cv2.imread(p)
            if img is not None:
                self.frames.append(cv2.resize(img, (w, h)))
        if not self.frames:
            self.frames = [np.full((h, w, 3), 60, np.uint8)]
        self.fps = max(1, int(cfg["fps"]))
        self.hold = float(os.environ.get("CCTV_FAKE_HOLD", "4"))
        self.t0 = time.time()

    def read(self):
        time.sleep(1.0 / self.fps)
        i = int((time.time() - self.t0) / self.hold) % len(self.frames)
        return True, self.frames[i].copy()

    def release(self):
        pass
