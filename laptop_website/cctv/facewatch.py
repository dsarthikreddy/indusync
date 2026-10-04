"""CCTV face RECOGNITION on the laptop webcam (second check next to the door board).

- Looks at the live picture about 3 times a second (only while the camera is ON).
- Recognises registered people by face (same person ids as the door board).
- A person must be recognised in 2 pictures within 3 s before they count (no 1-frame blips).
- A person "left" after not being seen for 20 s; coming back later = "re-entered".
- An unknown face must be seen in 3 pictures within 5 s; then a photo is saved as evidence.
- Registration: 5 samples from the live camera, or one or more uploaded photos.
  A face that already belongs to someone else is refused (one face = one person).
"""
import threading
import time
from collections import deque
from datetime import datetime
from pathlib import Path

import cv2
import numpy as np

from .faces import FaceEngine, FaceGallery

CONFIRM_HITS = 2
CONFIRM_WINDOW = 3.0
LEAVE_AFTER = 20.0
UNKNOWN_HITS = 3
UNKNOWN_WINDOW = 5.0
UNKNOWN_COOLDOWN = 60.0
MIN_FACE_PX = 48            # smaller faces are too far away to recognise reliably
ENROLL_MIN_PX = 80
ENROLL_SAMPLES = 5


class FaceWatch:
    def __init__(self, base, camera, db, get_cfg, on_seen=None, on_left=None, on_unknown=None, on_notice=None):
        self.base = Path(base)
        self.camera = camera
        self.db = db
        self.get_cfg = get_cfg
        self.on_seen, self.on_left, self.on_unknown, self.on_notice = on_seen, on_left, on_unknown, on_notice
        self.engine = None
        self.gallery = FaceGallery()
        self.lock = threading.Lock()
        self._stop = threading.Event()
        self._thread = None
        self.state = "off"           # off / loading / running / error
        self.error = ""
        self.ms = 0
        self.faces = []              # last result: dicts with box, id, name, score, known
        self.faces_time = 0.0
        self.tracks = {}             # person id -> {"start", "last", "hits": deque, "confirmed"}
        self.last_left = {}          # person id -> time they were last marked as left
        self._unknown_hits = deque()
        self._last_unknown_event = 0.0
        self.unknown_dir = self.base / "data" / "unknown_faces"
        self.unknown_dir.mkdir(parents=True, exist_ok=True)
        self.names = {}
        self.job = None              # enrolment from the camera
        self.processed = 0
        self.reload()

    # ---------- registered faces ----------
    def reload(self):
        self.gallery.load(self.db.all_faces())
        self.names = {p["id"]: p["name"] for p in self.db.people_list()}

    def preload(self):
        try:
            self._ensure_engine()
            if self.state == "loading":
                self.state = "off"
        except Exception as e:
            self.state, self.error = "error", str(e)

    def _ensure_engine(self):
        if self.engine is None:
            self.state = "loading"
            self.engine = FaceEngine(threads=2)

    # ---------- control (follows the camera) ----------
    def start(self):
        with self.lock:
            if self._thread is not None and self._thread.is_alive():
                return
            self._stop.clear()
            self._thread = threading.Thread(target=self._run, daemon=True, name="facewatch")
            self._thread.start()

    def stop(self):
        self._stop.set()
        th = self._thread
        if th is not None:
            th.join(timeout=8)
        self._thread = None
        now = time.time()
        for pid, tr in list(self.tracks.items()):
            if tr["confirmed"] and self.on_left:
                self._safe(self.on_left, pid, tr["start"], tr["last"])
            self.last_left[pid] = now
        with self.lock:
            self.tracks = {}
            self.faces = []
            if self.state != "error":
                self.state = "off"
            if self.job and self.job["status"] == "running":
                self.job.update(status="failed", msg="The camera was stopped.")

    def _safe(self, fn, *a):
        try:
            fn(*a)
        except Exception as e:  # never let a callback stop recognition
            if self.on_notice:
                self.on_notice("warn", "Face check problem: %s" % e)

    # ---------- main loop ----------
    def _run(self):
        try:
            self._ensure_engine()
        except Exception as e:
            self.state, self.error = "error", str(e)
            return
        self.state, self.error = "running", ""
        last_fid = None
        while not self._stop.is_set():
            cfg = self.get_cfg()
            if not cfg.get("face_enabled", True) or not self.camera.running:
                self._stop.wait(0.5)
                continue
            frame, ft = self.camera.get_frame()
            if frame is None or ft == last_fid:
                self._stop.wait(0.1)
                continue
            last_fid = ft
            t0 = time.time()
            try:
                self._process(frame, cfg)
            except Exception as e:
                self.error = "recognition error: %s" % e
            self.ms = int((time.time() - t0) * 1000)
            self.processed += 1
            # adaptive: a slow laptop checks less often (never more than ~30 % of one CPU), video stays smooth
            pause = max(float(cfg.get("face_interval", 0.5)), self.ms / 1000.0 * 2.5)
            self._stop.wait(max(0.05, pause - (time.time() - t0)))

    def _process(self, frame, cfg):
        now = time.time()
        thr = float(cfg.get("face_threshold", 0.40))
        dets = self.engine.detect(frame, thresh=0.55)
        result, unknown_here, seen_ids = [], [], set()
        for box, score, kps in dets:
            x1, y1, x2, y2 = box
            side = min(x2 - x1, y2 - y1)
            emb = self.engine.embed(frame, kps) if side >= MIN_FACE_PX * 0.6 else None
            pid, sim, _ = self.gallery.match(emb, threshold=thr) if emb is not None else (None, 0.0, 0.0)
            far = bool(side < MIN_FACE_PX)
            result.append({"box": [int(x1), int(y1), int(x2), int(y2)], "id": pid,
                           "name": self.names.get(pid, pid) if pid else ("too far" if far else "UNKNOWN"),
                           "score": round(float(sim), 2), "known": bool(pid), "far": far, "size": int(side)})
            if pid:
                seen_ids.add(pid)
            elif not far and emb is not None and score >= 0.6:
                unknown_here.append((box, emb))
        with self.lock:
            self.faces = result
            self.faces_time = now
            job = self.job
        if job and job["status"] == "running":
            self._enroll_step(frame, dets, now)
            return                                    # while registering, do not raise alerts

        # known people: confirm, keep alive, and notice leaving
        for pid in seen_ids:
            tr = self.tracks.get(pid)
            if tr is None:
                tr = self.tracks[pid] = {"start": now, "last": now, "hits": deque(), "confirmed": False}
            tr["last"] = now
            tr["hits"].append(now)
            while tr["hits"] and now - tr["hits"][0] > CONFIRM_WINDOW:
                tr["hits"].popleft()
            if not tr["confirmed"] and len(tr["hits"]) >= CONFIRM_HITS:
                tr["confirmed"] = True
                tr["start"] = tr["hits"][0]
                back = self.last_left.get(pid)
                if self.on_seen:
                    self._safe(self.on_seen, pid, tr["start"], back)
        for pid, tr in list(self.tracks.items()):
            if now - tr["last"] > LEAVE_AFTER:
                if tr["confirmed"] and self.on_left:
                    self._safe(self.on_left, pid, tr["start"], tr["last"])
                if tr["confirmed"]:
                    self.last_left[pid] = tr["last"]
                del self.tracks[pid]

        # unknown faces: need 3 sightings within 5 s, then one event per minute at most
        if unknown_here:
            self._unknown_hits.append(now)
        while self._unknown_hits and now - self._unknown_hits[0] > UNKNOWN_WINDOW:
            self._unknown_hits.popleft()
        if len(self._unknown_hits) >= UNKNOWN_HITS and now - self._last_unknown_event > UNKNOWN_COOLDOWN and unknown_here:
            self._last_unknown_event = now
            self._unknown_hits.clear()
            path = self._save_unknown(frame, unknown_here[0][0], now)
            if self.on_unknown:
                self._safe(self.on_unknown, now, path, len(unknown_here))

    def _save_unknown(self, frame, box, now):
        try:
            h, w = frame.shape[:2]
            x1, y1, x2, y2 = [int(v) for v in box]
            mx, my = int((x2 - x1) * 0.4), int((y2 - y1) * 0.4)
            crop = frame[max(0, y1 - my):min(h, y2 + my), max(0, x1 - mx):min(w, x2 + mx)]
            name = "unknown_%s.jpg" % datetime.fromtimestamp(now).strftime("%Y%m%d_%H%M%S")
            cv2.imwrite(str(self.unknown_dir / name), crop, [cv2.IMWRITE_JPEG_QUALITY, 85])
            files = sorted(self.unknown_dir.glob("unknown_*.jpg"))
            for old in files[:-500]:                      # keep the newest 500 photos
                old.unlink(missing_ok=True)
            return name
        except Exception:
            return ""

    # ---------- what is on screen now ----------
    def snapshot(self):
        with self.lock:
            age = time.time() - self.faces_time
            faces = list(self.faces) if age < 2.0 else []
            job = dict(self.job) if self.job else None
        if job:
            job.pop("embs", None)
        tracks = dict(self.tracks)
        present = sorted(pid for pid, tr in tracks.items() if tr["confirmed"])
        return {"state": self.state, "error": self.error, "ms": self.ms, "faces": faces,
                "known_now": [f for f in faces if f["known"]], "unknown_now": sum(1 for f in faces if not f["known"] and not f["far"]),
                "present": [{"id": p, "name": self.names.get(p, p), "since": tracks[p]["start"]} for p in present],
                "registered_people": len(self.gallery.people()), "job": job}

    def draw(self, frame):
        cfg = self.get_cfg()
        if not cfg.get("face_enabled", True) or not cfg.get("show_boxes", True):
            return frame
        with self.lock:
            faces = list(self.faces) if time.time() - self.faces_time < 1.5 else []
        h, w = frame.shape[:2]
        s = max(0.5, w / 1280.0 * 0.7)
        thick = 1 if w < 1000 else 2
        for f in faces:
            x1, y1, x2, y2 = f["box"]
            color = (94, 197, 34) if f["known"] else ((160, 160, 160) if f["far"] else (60, 60, 239))
            cv2.rectangle(frame, (x1, y1), (x2, y2), color, 2)
            label = ("%s %d%%" % (f["name"], int(f["score"] * 100))) if f["known"] else f["name"]
            (tw, th), _ = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, s, thick)
            ty = min(h - 2, y2 + th + 10)
            cv2.rectangle(frame, (x1, ty - th - 8), (x1 + tw + 8, ty), color, -1)
            cv2.putText(frame, label, (x1 + 4, ty - 5), cv2.FONT_HERSHEY_SIMPLEX, s, (255, 255, 255), thick, cv2.LINE_AA)
        return frame

    # ---------- registration ----------
    def enroll_from_camera(self, pid):
        if not self.camera.running:
            return False, "Start the camera first."
        with self.lock:
            if self.job and self.job["status"] == "running":
                return False, "Another registration is running."
            self.job = {"pid": pid, "name": self.names.get(pid, pid), "need": ENROLL_SAMPLES, "got": 0, "embs": [],
                        "status": "running", "msg": "Look at the camera. Move your head slightly between samples.",
                        "deadline": time.time() + 40, "last": 0.0}
        return True, "started"

    def cancel_enroll(self):
        with self.lock:
            if self.job and self.job["status"] == "running":
                self.job.update(status="cancelled", msg="Cancelled.")

    def _duplicate_of(self, emb, pid):
        other, sim, _ = self.gallery.match(emb, threshold=0.45, margin=0.0)
        if other and other != pid:
            return other, sim
        return None, sim

    def _enroll_step(self, frame, dets, now):
        job = self.job
        if now > job["deadline"]:
            job.update(status="failed", msg="Time is up: the face was not seen clearly. Try again in good light.")
            return
        big = [d for d in dets if min(d[0][2] - d[0][0], d[0][3] - d[0][1]) >= ENROLL_MIN_PX and d[1] >= 0.6]
        if len(dets) > 1:
            job["msg"] = "Only ONE person may be in front of the camera while registering."
            return
        if not big:
            job["msg"] = "Come closer (face bigger on screen) and look at the camera."
            return
        if now - job["last"] < 0.5:
            return
        emb = self.engine.embed(frame, big[0][2])
        if emb is None:
            return
        other, sim = self._duplicate_of(emb, job["pid"])
        if other:
            job.update(status="failed", msg="Refused: this face is already registered as %s (one face = one person)."
                       % self.names.get(other, other))
            return
        job["embs"].append(emb)
        job["got"] = len(job["embs"])
        job["last"] = now
        job["msg"] = "Sample %d of %d saved. Turn your head a little." % (job["got"], job["need"])
        if job["got"] >= job["need"]:
            for e in job["embs"]:
                self.db.add_face(job["pid"], e, "camera")
            self.reload()
            job.update(status="done", msg="Done: %s is registered with %d face samples." % (job["name"], job["got"]))

    def enroll_image(self, pid, data):
        img = cv2.imdecode(np.frombuffer(data, dtype=np.uint8), cv2.IMREAD_COLOR)
        if img is None:
            return False, "That file is not a picture (use JPG or PNG)."
        if max(img.shape[:2]) > 2000:
            k = 2000.0 / max(img.shape[:2])
            img = cv2.resize(img, None, fx=k, fy=k)
        self._ensure_engine()
        dets = self.engine.detect(img, thresh=0.55)
        if not dets:
            return False, "No face found in the photo. Use a clear, front-facing photo."
        box, score, kps = max(dets, key=lambda d: (d[0][2] - d[0][0]) * (d[0][3] - d[0][1]))
        if min(box[2] - box[0], box[3] - box[1]) < 60:
            return False, "The face in the photo is too small. Use a closer photo."
        emb = self.engine.embed(img, kps)
        if emb is None:
            return False, "Could not read the face. Try another photo."
        other, sim = self._duplicate_of(emb, pid)
        if other:
            return False, "Refused: this face is already registered as %s." % self.names.get(other, other)
        self.db.add_face(pid, emb, "photo")
        self.reload()
        return True, "Photo added for %s." % self.names.get(pid, pid)
