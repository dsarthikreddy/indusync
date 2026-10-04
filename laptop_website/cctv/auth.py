"""Accounts and logins.

- Passwords are never stored: only a salted PBKDF2-SHA256 hash (240,000 rounds).
- A login gives a random 256-bit token kept in a secure browser cookie.
  Only a SHA-256 of that token is saved on the laptop, so even data/sessions.json
  cannot be used to log in.
- "Keep me signed in" lasts 30 days and renews itself every day you use it,
  so you stay signed in permanently as long as you open the dashboard at least once a month.
- 5 wrong passwords in 10 minutes -> the guessing device is locked for 10 minutes
  (the laptop itself is never locked, so the owner can always get in).
"""
import hashlib
import hmac
import json
import os
import re
import secrets
import threading
import time
from collections import defaultdict, deque

SESSION_SECONDS = 30 * 24 * 3600
RENEW_AFTER = 24 * 3600
PBKDF2_ROUNDS = 240_000
USERNAME_RE = re.compile(r"^[A-Za-z0-9_]{3,20}$")
MAX_FAILS = 5
FAIL_WINDOW = 600
LOCK_SECONDS = 600
GLOBAL_MAX_FAILS = 40          # many devices guessing together -> pause internet logins
GLOBAL_PAUSE = 300
MAX_PENDING = 30


IDLE_REMEMBER = 14 * 24 * 3600     # a remembered login that is not used for 14 days ends
IDLE_SHORT = 2 * 3600              # a normal login ends after 2 hours without use
SEEN_WRITE = 300                   # write "last used" at most every 5 minutes (keeps pages fast)


def _hash_password(password, salt):            # old format (v3 and earlier), still accepted
    return hashlib.pbkdf2_hmac("sha256", password.encode("utf-8"), salt, PBKDF2_ROUNDS).hex()


def _hash_scrypt(password, salt):              # v4: memory-hard scrypt (built into Python)
    return hashlib.scrypt(password.encode("utf-8"), salt=salt, n=2 ** 15, r=8, p=1,
                          maxmem=64 * 1024 * 1024, dklen=32).hex()


def _calc(u, password):
    salt = bytes.fromhex(u["salt"])
    return _hash_scrypt(password, salt) if u.get("algo") == "scrypt" else _hash_password(password, salt)


def _token_id(token):
    return hashlib.sha256(token.encode("utf-8")).hexdigest()


class AuthStore:
    def __init__(self, data_dir):
        self._dir = data_dir
        self._dir.mkdir(parents=True, exist_ok=True)
        self._users_path = self._dir / "users.json"
        self._requests_path = self._dir / "requests.json"
        self._sessions_path = self._dir / "sessions.json"
        self._lock = threading.Lock()
        self._users = self._load(self._users_path)
        self._sessions = self._load(self._sessions_path)
        self._requests = self._load(self._requests_path)
        self._global_fails = deque()
        self._global_pause_until = 0.0
        self._fails = defaultdict(deque)
        self._locked_until = {}
        self._cleanup_sessions()

    # ---------- files ----------
    @staticmethod
    def _load(path):
        if path.exists():
            try:
                return json.loads(path.read_text(encoding="utf-8"))
            except Exception:
                return {}
        return {}

    @staticmethod
    def _write(path, data):
        tmp = path.with_suffix(".tmp")
        tmp.write_text(json.dumps(data, indent=2), encoding="utf-8")
        os.replace(tmp, path)

    def _cleanup_sessions(self):
        now = time.time()
        with self._lock:
            dead = [k for k, s in self._sessions.items() if s.get("expires", 0) < now]
            for k in dead:
                del self._sessions[k]
            if dead:
                self._write(self._sessions_path, self._sessions)

    # ---------- users ----------
    def has_users(self):
        with self._lock:
            return len(self._users) > 0

    def list_users(self):
        with self._lock:
            return [{"name": n, "role": u["role"], "created": u.get("created", 0)}
                    for n, u in sorted(self._users.items())]

    def admin_count(self):
        with self._lock:
            return sum(1 for u in self._users.values() if u["role"] == "admin")

    @staticmethod
    def check_new_credentials(username, password):
        if not USERNAME_RE.match(username or ""):
            return "Username: 3-20 letters, numbers or _ (no spaces)."
        if not password or len(password) < 8:
            return "Password must be at least 8 characters."
        if len(password) > 128:
            return "Password is too long."
        return None

    def create_user(self, username, password, role):
        err = self.check_new_credentials(username, password)
        if err:
            raise ValueError(err)
        if role not in ("admin", "viewer"):
            raise ValueError("Role must be admin or viewer.")
        with self._lock:
            if any(n.lower() == username.lower() for n in self._users):
                raise ValueError("That username already exists.")
            salt = secrets.token_bytes(16)
            self._users[username] = {
                "salt": salt.hex(),
                "hash": _hash_scrypt(password, salt), "algo": "scrypt",
                "role": role,
                "created": time.time(),
            }
            self._write(self._users_path, self._users)

    def delete_user(self, username):
        with self._lock:
            if username not in self._users:
                raise ValueError("No such user.")
            del self._users[username]
            self._write(self._users_path, self._users)
            self._sessions = {k: s for k, s in self._sessions.items() if s["user"] != username}
            self._write(self._sessions_path, self._sessions)

    def set_password(self, username, new_password, keep_token=None):
        err = self.check_new_credentials(username, new_password)
        if err:
            raise ValueError(err)
        with self._lock:
            salt = secrets.token_bytes(16)
            self._users[username]["salt"] = salt.hex()
            self._users[username]["hash"] = _hash_scrypt(new_password, salt)
            self._users[username]["algo"] = "scrypt"
            self._write(self._users_path, self._users)
            keep = _token_id(keep_token) if keep_token else None
            self._sessions = {k: s for k, s in self._sessions.items()
                              if s["user"] != username or k == keep}
            self._write(self._sessions_path, self._sessions)

    def verify_password(self, username, password):
        with self._lock:
            u = self._users.get(username)
        if not u:
            _hash_scrypt(password or "", b"0" * 16)        # same time whether user exists or not
            return False
        ok = hmac.compare_digest(_calc(u, password or ""), u["hash"])
        if ok and u.get("algo") != "scrypt":              # upgrade an old password hash once
            with self._lock:
                salt = secrets.token_bytes(16)
                u.update(salt=salt.hex(), hash=_hash_scrypt(password, salt), algo="scrypt")
                self._write(self._users_path, self._users)
        return ok

    def role_of(self, username):
        with self._lock:
            u = self._users.get(username)
            return u["role"] if u else None

    # ---------- brute-force protection ----------
    @staticmethod
    def _exempt(ip):
        return ip in ("127.0.0.1", "::1")

    def locked_seconds(self, ip, username=None):
        if self._exempt(ip):
            return 0
        now = time.time()
        until = max(self._locked_until.get(ip, 0), self._global_pause_until)
        left = until - now
        return int(left) + 1 if left > 0 else 0

    def record_fail(self, ip, username=None):
        if self._exempt(ip):
            return
        now = time.time()
        self._global_fails.append(now)
        while self._global_fails and now - self._global_fails[0] > FAIL_WINDOW:
            self._global_fails.popleft()
        if len(self._global_fails) >= GLOBAL_MAX_FAILS:
            self._global_pause_until = now + GLOBAL_PAUSE
            self._global_fails.clear()
        q = self._fails[ip]
        q.append(now)
        while q and now - q[0] > FAIL_WINDOW:
            q.popleft()
        if len(q) >= MAX_FAILS:
            self._locked_until[ip] = now + LOCK_SECONDS
            q.clear()

    def clear_fails(self, ip, username=None):
        self._fails.pop(ip, None)

    # ---------- sessions ----------
    def create_session(self, username, remember, device=""):
        token = secrets.token_urlsafe(32)
        now = time.time()
        with self._lock:
            self._sessions[_token_id(token)] = {
                "user": username,
                "remember": bool(remember),
                "created": now,
                "renewed": now,
                "seen": now,
                "expires": now + (SESSION_SECONDS if remember else 12 * 3600),
                "device": device[:80],
            }
            self._write(self._sessions_path, self._sessions)
        return token

    def session_user(self, token):
        """Returns dict(name, role, renewed) or None. Renews remembered sessions once a day."""
        if not token or len(token) > 100:
            return None
        tid = _token_id(token)
        now = time.time()
        with self._lock:
            s = self._sessions.get(tid)
            if not s:
                return None
            idle = IDLE_REMEMBER if s["remember"] else IDLE_SHORT
            if s["expires"] < now or s["user"] not in self._users or now - s.get("seen", s["created"]) > idle:
                self._sessions.pop(tid, None)
                self._write(self._sessions_path, self._sessions)
                return None
            renewed = False
            if now - s.get("seen", 0) > SEEN_WRITE:
                s["seen"] = now
                self._write(self._sessions_path, self._sessions)
            if s["remember"] and now - s.get("renewed", 0) > RENEW_AFTER:
                s["renewed"] = now
                s["expires"] = now + SESSION_SECONDS
                self._write(self._sessions_path, self._sessions)
                renewed = True
            return {"name": s["user"], "role": self._users[s["user"]]["role"],
                    "renewed": renewed, "remember": s["remember"]}

    def end_session(self, token):
        if not token:
            return
        with self._lock:
            if self._sessions.pop(_token_id(token), None) is not None:
                self._write(self._sessions_path, self._sessions)

    # ---------- access requests (visitors ask, the admin approves) ----------
    def add_request(self, username, password, full_name, note, ip):
        err = self.check_new_credentials(username, password)
        if err:
            raise ValueError(err)
        full_name = " ".join(str(full_name or "").split())[:40]
        note = " ".join(str(note or "").split())[:160]
        if len(full_name) < 2:
            raise ValueError("Please type your full name.")
        with self._lock:
            taken = any(n.lower() == username.lower() for n in list(self._users) + list(self._requests))
            if taken:
                raise ValueError("That username is taken. Choose another one.")
            if len(self._requests) >= MAX_PENDING:
                raise ValueError("Too many requests are waiting. Please try again later.")
            salt = secrets.token_bytes(16)
            self._requests[username] = {
                "salt": salt.hex(), "hash": _hash_scrypt(password, salt), "algo": "scrypt",
                "full_name": full_name, "note": note, "ip": ip, "time": time.time(),
            }
            self._write(self._requests_path, self._requests)

    def list_requests(self):
        with self._lock:
            return [{"username": n, "full_name": r["full_name"], "note": r["note"], "time": r["time"]}
                    for n, r in sorted(self._requests.items(), key=lambda kv: kv[1]["time"])]

    def is_pending(self, username):
        with self._lock:
            return any(n.lower() == (username or "").lower() for n in self._requests)

    def approve_request(self, username, role="viewer"):
        if role not in ("admin", "viewer"):
            raise ValueError("Role must be admin or viewer.")
        with self._lock:
            r = self._requests.pop(username, None)
            if r is None:
                raise ValueError("No such request.")
            if any(n.lower() == username.lower() for n in self._users):
                self._write(self._requests_path, self._requests)
                raise ValueError("That username already exists.")
            self._users[username] = {"salt": r["salt"], "hash": r["hash"], "algo": r.get("algo", "pbkdf2"), "role": role,
                                     "created": time.time(), "full_name": r["full_name"]}
            self._write(self._users_path, self._users)
            self._write(self._requests_path, self._requests)

    def deny_request(self, username):
        with self._lock:
            if self._requests.pop(username, None) is None:
                raise ValueError("No such request.")
            self._write(self._requests_path, self._requests)
