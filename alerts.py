"""Short list of recent alerts shown on the dashboard."""
import threading
import time
from collections import deque


class Alerts:
    def __init__(self):
        self._items = deque(maxlen=60)
        self._last = {}
        self._lock = threading.Lock()

    def add(self, level, msg, key=None, cooldown=60):
        """level: info / ok / warn / alert. Same key is not repeated within cooldown seconds."""
        k = key or msg
        now = time.time()
        with self._lock:
            if k in self._last and now - self._last[k] < cooldown:
                return
            self._last[k] = now
            self._items.appendleft({"t": now, "level": level, "msg": msg})

    def recent(self, n=20):
        with self._lock:
            return list(self._items)[:n]
