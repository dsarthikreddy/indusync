"""Laptop face RECOGNITION (not just detection), fully on this laptop - nothing is uploaded.

Two small standard AI models (InsightFace, free for academic / non-commercial use):
  scrfd_500m_kps.onnx  - finds faces and 5 face points (eyes, nose, mouth corners)
  w600k_mbf.onnx       - ArcFace MobileFaceNet: turns a face into 512 numbers ("embedding")
Two photos of the same person give embeddings that point the same way (cosine similarity high);
different people give low similarity. A person is recognised when the best similarity to their
registered photos is above the threshold (default 0.40) AND clearly better than the next person.
"""
import threading
from pathlib import Path

import cv2
import numpy as np

try:
    import onnxruntime as ort
except Exception:  # pragma: no cover
    ort = None

MODELS = Path(__file__).resolve().parent / "models"
DET_MODEL = MODELS / "scrfd_500m_kps.onnx"
REC_MODEL = MODELS / "w600k_mbf.onnx"

# where the 5 face points must land in the 112x112 picture ArcFace expects
ARCFACE_DST = np.array([[38.2946, 51.6963], [73.5318, 51.5014], [56.0252, 71.7366],
                        [41.5493, 92.3655], [70.7299, 92.2041]], dtype=np.float32)


def _nms(dets, thresh):
    x1, y1, x2, y2, scores = dets[:, 0], dets[:, 1], dets[:, 2], dets[:, 3], dets[:, 4]
    areas = (x2 - x1 + 1) * (y2 - y1 + 1)
    order = scores.argsort()[::-1]
    keep = []
    while order.size > 0:
        i = order[0]
        keep.append(i)
        xx1 = np.maximum(x1[i], x1[order[1:]])
        yy1 = np.maximum(y1[i], y1[order[1:]])
        xx2 = np.minimum(x2[i], x2[order[1:]])
        yy2 = np.minimum(y2[i], y2[order[1:]])
        w = np.maximum(0.0, xx2 - xx1 + 1)
        h = np.maximum(0.0, yy2 - yy1 + 1)
        ovr = (w * h) / (areas[i] + areas[order[1:]] - w * h)
        order = order[np.where(ovr <= thresh)[0] + 1]
    return keep


class FaceEngine:
    def __init__(self, threads=2):
        if ort is None:
            raise RuntimeError("onnxruntime is not installed (run Install.bat again)")
        if not DET_MODEL.exists() or not REC_MODEL.exists():
            raise RuntimeError("face models missing in cctv\\models (unzip the full folder again)")
        so = ort.SessionOptions()
        so.intra_op_num_threads = max(1, threads)
        so.log_severity_level = 3
        prov = ["CPUExecutionProvider"]
        self.det = ort.InferenceSession(str(DET_MODEL), sess_options=so, providers=prov)
        self.rec = ort.InferenceSession(str(REC_MODEL), sess_options=so, providers=prov)
        self.det_in = self.det.get_inputs()[0].name
        self.rec_in = self.rec.get_inputs()[0].name
        self.det_size = (640, 640)
        self._centers = {}
        self.lock = threading.Lock()

    # ---------- find faces ----------
    def detect(self, img, thresh=0.5, max_faces=10):
        """Returns a list of (box[x1,y1,x2,y2], score, kps[5x2]) in the image's own pixels."""
        h, w = img.shape[:2]
        if h < 20 or w < 20:
            return []
        dw, dh = self.det_size
        scale = min(dw / w, dh / h)
        nw, nh = int(w * scale), int(h * scale)
        canvas = np.zeros((dh, dw, 3), dtype=np.uint8)
        canvas[:nh, :nw] = cv2.resize(img, (nw, nh))
        blob = cv2.dnn.blobFromImage(canvas, 1.0 / 128, (dw, dh), (127.5, 127.5, 127.5), swapRB=True)
        with self.lock:
            outs = self.det.run(None, {self.det_in: blob})
        outs = [o[0] if o.ndim == 3 else o for o in outs]
        scores_l, boxes_l, kps_l = [], [], []
        for idx, stride in enumerate((8, 16, 32)):
            scores = outs[idx].reshape(-1)
            bbox = outs[idx + 3].reshape(-1, 4) * stride
            kps = outs[idx + 6].reshape(-1, 10) * stride
            fh, fw = dh // stride, dw // stride
            key = (fh, fw, stride)
            if key not in self._centers:
                yy, xx = np.mgrid[:fh, :fw]
                c = (np.stack([xx, yy], axis=-1).astype(np.float32) * stride).reshape(-1, 2)
                self._centers[key] = np.repeat(c, 2, axis=0)        # 2 anchors per place
            centers = self._centers[key]
            pos = np.where(scores >= thresh)[0]
            if pos.size == 0:
                continue
            c = centers[pos]
            b = bbox[pos]
            boxes = np.stack([c[:, 0] - b[:, 0], c[:, 1] - b[:, 1], c[:, 0] + b[:, 2], c[:, 1] + b[:, 3]], axis=-1)
            k = kps[pos].reshape(-1, 5, 2) + c[:, None, :]
            scores_l.append(scores[pos])
            boxes_l.append(boxes)
            kps_l.append(k)
        if not scores_l:
            return []
        scores = np.concatenate(scores_l)
        boxes = np.concatenate(boxes_l) / scale
        kpss = np.concatenate(kps_l) / scale
        dets = np.hstack([boxes, scores[:, None]]).astype(np.float32)
        keep = _nms(dets, 0.4)[:max_faces]
        res = []
        for i in keep:
            x1, y1, x2, y2 = dets[i, :4]
            res.append(([max(0, x1), max(0, y1), min(w, x2), min(h, y2)], float(dets[i, 4]), kpss[i]))
        return res

    # ---------- turn a face into 512 numbers ----------
    def embed(self, img, kps):
        M, _ = cv2.estimateAffinePartial2D(np.asarray(kps, dtype=np.float32), ARCFACE_DST, method=cv2.LMEDS)
        if M is None:
            return None
        face = cv2.warpAffine(img, M, (112, 112), borderValue=0.0)
        blob = cv2.dnn.blobFromImage(face, 1.0 / 127.5, (112, 112), (127.5, 127.5, 127.5), swapRB=True)
        with self.lock:
            v = self.rec.run(None, {self.rec_in: blob})[0][0]
        n = np.linalg.norm(v)
        return (v / n).astype(np.float32) if n > 0 else None

    @staticmethod
    def quality(box, img_shape):
        """Bigger, sharper-in-frame faces are better samples (0..1)."""
        x1, y1, x2, y2 = box
        side = min(x2 - x1, y2 - y1)
        return float(max(0.0, min(1.0, (side - 40) / 120.0)))


class FaceGallery:
    """Registered people: person id -> list of embeddings. Matching = best cosine similarity."""

    def __init__(self):
        self.lock = threading.Lock()
        self.ids = []          # one row per stored embedding
        self.mat = np.zeros((0, 512), dtype=np.float32)

    def load(self, items):
        """items: iterable of (person_id, embedding ndarray)"""
        ids, vecs = [], []
        for pid, v in items:
            ids.append(pid)
            vecs.append(v)
        with self.lock:
            self.ids = ids
            self.mat = np.vstack(vecs).astype(np.float32) if vecs else np.zeros((0, 512), dtype=np.float32)

    def people(self):
        with self.lock:
            return sorted(set(self.ids))

    def match(self, emb, threshold=0.40, margin=0.05):
        """Returns (person_id or None, best_similarity, second_best_other_person)."""
        with self.lock:
            if self.mat.shape[0] == 0 or emb is None:
                return None, 0.0, 0.0
            sims = self.mat @ emb
            ids = self.ids
        best = {}
        for pid, s in zip(ids, sims):
            if s > best.get(pid, -1.0):
                best[pid] = float(s)
        ranked = sorted(best.items(), key=lambda kv: kv[1], reverse=True)
        top_id, top = ranked[0]
        second = ranked[1][1] if len(ranked) > 1 else 0.0
        if top >= threshold and top - second >= margin:
            return top_id, top, second
        return None, top, second
