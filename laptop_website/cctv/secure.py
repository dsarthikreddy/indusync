"""Security helpers for SARTHIK Room (all in memory, microseconds per request).

SecLog      security events -> data/security.log (rotating 1 MB x 5) + the System logs page.
            Never logs passwords, cookies, tokens or photo contents.
RateLimit   sliding-window limits per (client, bucket). Normal use never reaches them.
Suspicion   repeated refused requests (401/403/404 on private paths, 429) from one internet client
            -> that client is paused for 10 minutes. This laptop is never paused.
"""
import logging
import re
import threading
import time
from collections import defaultdict, deque
from logging.handlers import RotatingFileHandler

_SAFE = re.compile(r"[^\w .:/@#=+,()\-]")


def clean(text, n=160):
    """Makes any text safe for one log line (no line breaks, no control characters)."""
    return _SAFE.sub("?", str(text))[:n]


class SecLog:
    def __init__(self, path, also=None):
        self.log = logging.getLogger("sarthik.security")
        self.log.setLevel(logging.INFO)
        self.log.propagate = False
        if not self.log.handlers:
            h = RotatingFileHandler(str(path), maxBytes=1_000_000, backupCount=5, encoding="utf-8")
            h.setFormatter(logging.Formatter("%(asctime)s %(levelname)s %(message)s"))
            self.log.addHandler(h)
        self.also = also

    def __call__(self, event, ip="", user="", detail="", level="info"):
        line = "%s ip=%s user=%s %s" % (clean(event, 40), clean(ip, 60), clean(user, 30), clean(detail))
        try:
            (self.log.warning if level in ("warn", "alert") else self.log.info)(line)
        except Exception:
            pass
        if self.also:
            try:
                self.also("warn" if level in ("warn", "alert") else "info", "security: " + line)
            except Exception:
                pass


class RateLimit:
    def __init__(self):
        self.lock = threading.Lock()
        self.hits = defaultdict(deque)
        self._last_clean = time.time()

    def allow(self, key, limit, per):
        now = time.time()
        with self.lock:
            q = self.hits[key]
            while q and now - q[0] > per:
                q.popleft()
            if len(q) >= limit:
                return False
            q.append(now)
            if now - self._last_clean > 300:                  # forget idle clients
                self._last_clean = now
                for k in [k for k, v in self.hits.items() if not v or now - v[-1] > 900]:
                    del self.hits[k]
            return True


class Suspicion:
    WINDOW = 300
    LIMIT = 40
    PAUSE = 600

    def __init__(self, on_block=None):
        self.lock = threading.Lock()
        self.bad = defaultdict(deque)
        self.blocked = {}
        self.on_block = on_block

    def is_blocked(self, ip):
        until = self.blocked.get(ip)
        if until and until > time.time():
            return True
        if until:
            self.blocked.pop(ip, None)
        return False

    def note(self, ip, status, path):
        if status not in (401, 403, 404, 405, 429):
            return
        now = time.time()
        with self.lock:
            q = self.bad[ip]
            q.append(now)
            while q and now - q[0] > self.WINDOW:
                q.popleft()
            if len(q) >= self.LIMIT and ip not in self.blocked:
                self.blocked[ip] = now + self.PAUSE
                q.clear()
                if self.on_block:
                    self.on_block(ip, path)
