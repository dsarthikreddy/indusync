"""SARTHIK Room - CCTV dashboard with AI people/object counting.

Start with:  Start CCTV.bat   (or: python -m cctv.main)
Stop with:   close the black window (or press Ctrl+C in it)
"""
import os

os.environ.setdefault("OPENCV_VIDEOIO_MSMF_ENABLE_HW_TRANSFORMS", "0")
os.environ.setdefault("OPENCV_LOG_LEVEL", "ERROR")

import asyncio
import ipaddress
import json
import re
import secrets as pysecrets
import shutil
import socket
import subprocess
import sys
import threading
import time
import webbrowser
from collections import defaultdict, deque
from contextlib import asynccontextmanager
from html import escape
from pathlib import Path
from urllib.parse import urlparse

import uvicorn
from fastapi import FastAPI, HTTPException, Request
from fastapi.exceptions import RequestValidationError
from starlette.exceptions import HTTPException as StarletteHTTPException
from fastapi.responses import (FileResponse, HTMLResponse, JSONResponse, PlainTextResponse,
                               RedirectResponse, Response, StreamingResponse)
from starlette.datastructures import MutableHeaders

from .alerts import Alerts
from .db import Store, TITLES
from .facewatch import FaceWatch
from .hub import Hub
from .secure import RateLimit, SecLog, Suspicion
from .auth import AuthStore
from .camera import CameraService
from .detector import Detector
from .esp32link import Esp32Link
from .settings_store import BASE, DATA, SettingsStore

VERSION = "4.0"
WEB = Path(__file__).resolve().parent / "web"
COOKIE = "classync_session"
SNAP_RE = re.compile(r"^snap_\d{8}_\d{6}\.jpg$")

store = SettingsStore()
auth = AuthStore(DATA)
alerts = Alerts()
camera = CameraService(BASE)
db = Store(DATA / "classync.db")
hub = Hub(db, alerts)
seclog = SecLog(DATA / "security.log", also=hub.log)
limiter = RateLimit()
suspicion = Suspicion(on_block=lambda ip, path: (seclog("client_paused_10min", ip, detail=path, level="alert"),
                                                 alerts.add("alert", "Many refused requests from %s - paused for 10 minutes" % ip,
                                                            key="susp" + ip, cooldown=600)))
esp = Esp32Link(alerts, on_door=lambda r, c: on_door_radar(r, c), hub=hub, db=db)
_occupied = {"value": None}


def on_detection(people, counts):
    cfg = store.get()
    if people > cfg["max_occupancy"]:
        alerts.add("alert", f"Over capacity: {people} people (limit {cfg['max_occupancy']})", key="cap", cooldown=120)
    if esp.connected:
        checked = esp.checked_in()
        if people > checked:
            alerts.add("warn", f"Camera sees {people} people but only {checked} checked in at the door "
                               "(possible tailgating)", key="tailgate", cooldown=120)
    occ = people > 0
    if _occupied["value"] is not None and occ != _occupied["value"]:
        alerts.add("info", "Room is now occupied" if occ else "Room is now empty", key=f"occ{occ}", cooldown=5)
    _occupied["value"] = occ


def notice(level, msg):
    alerts.add(level, msg, key=msg[:60], cooldown=60)


def on_door_radar(radar, counts):
    """The door board's Wi-Fi radar said something; compare it with the camera."""
    if radar.get("state") == "person_at_door" and detector.state == "running" and camera.state == "live":
        if detector.people == 0:
            alerts.add("info", "Someone is at the door, but nobody is inside the room yet",
                       key="radar_outside", cooldown=120)


detector = Detector(BASE, camera, store, on_result=on_detection, on_notice=notice)
facewatch = FaceWatch(BASE, camera, db, store.get, on_seen=hub.on_seen, on_left=hub.on_left,
                      on_unknown=hub.on_unknown, on_notice=notice)
camera.overlay_fn = lambda frame: facewatch.draw(detector.draw(frame))
camera.on_notice = notice

# ============================ who is visiting? ============================
TS_V4 = ipaddress.ip_network("100.64.0.0/10")
TS_V6 = ipaddress.ip_network("fd7a:115c:a1e0::/48")


def _ip(value):
    try:
        return ipaddress.ip_address((value or "").strip())
    except ValueError:
        return None


def client_info(scope):
    """Works out who the visitor really is.

    Tailscale Funnel / Cloudflare deliver internet visitors to this laptop, so they arrive from
    127.0.0.1. We only trust their forwarding headers when the connection itself comes from this
    laptop (nobody else can fake that), and such visitors are NEVER treated as 'on the laptop'."""
    peer = (scope.get("client") or ("", 0))[0]
    headers = {k.decode("latin-1").lower(): v.decode("latin-1") for k, v in scope.get("headers", [])}
    peer_ip = _ip(peer)
    proxied = bool(peer_ip and peer_ip.is_loopback and
                   ("x-forwarded-for" in headers or "cf-connecting-ip" in headers))
    if proxied:
        real = headers.get("cf-connecting-ip") or headers.get("x-forwarded-for", "").split(",")[-1]
        ip = real.strip()
        public = headers.get("tailscale-funnel-request") == "?1" or "cf-connecting-ip" in headers
        if not public:                       # proxied but not from our Tailscale network = public too
            a = _ip(ip)
            public = not (a is not None and (a in TS_V4 if a.version == 4 else a in TS_V6))
        https = headers.get("x-forwarded-proto", "") == "https"
        host = headers.get("x-forwarded-host") or headers.get("host", "")
    else:
        ip, public, https, host = peer, False, False, headers.get("host", "")
    local = (not proxied) and bool(peer_ip and peer_ip.is_loopback)
    return {"ip": ip, "proxied": proxied, "public": public, "https": https, "host": host,
            "local": local, "headers": headers}


def ip_allowed(host):
    a = _ip(host)
    if a is None:
        return False
    if a.is_loopback:
        return True
    cfg = store.get()
    if cfg["allow_tailscale"] and (a in TS_V4 if a.version == 4 else a in TS_V6):
        return True
    if cfg["allow_lan"] and a.is_private and not a.is_link_local:
        return True
    return False


SECURITY_HEADERS = {
    "x-frame-options": "DENY",
    "x-content-type-options": "nosniff",
    "referrer-policy": "no-referrer",
    "permissions-policy": "camera=(), microphone=(), geolocation=()",
    "cross-origin-opener-policy": "same-origin",
    "content-security-policy": ("default-src 'self'; img-src 'self' data: blob:; style-src 'self' 'unsafe-inline'; "
                                "script-src 'self'; connect-src 'self'; frame-ancestors 'none'; "
                                "base-uri 'none'; form-action 'self'; object-src 'none'"),
}

PRIVATE_PAGE = """<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>SARTHIK Room</title>
<meta name="robots" content="noindex"><style>body{margin:0;min-height:100vh;display:grid;place-items:center;
background:#070b10;color:#e8eef4;font-family:system-ui,Segoe UI,Arial,sans-serif;text-align:center;padding:24px}
.c{max-width:460px}h1{font-size:22px}p{color:#8a9aab;line-height:1.6}</style></head><body><div class="c">
<svg width="56" height="56" viewBox="0 0 64 64"><circle cx="32" cy="32" r="14" fill="none" stroke="#22d3ee" stroke-width="6"/>
<circle cx="32" cy="32" r="5" fill="#22d3ee"/></svg><h1>SARTHIK Room</h1>
<p>This monitor is private right now. The owner has switched off the public link.</p></div></body></html>"""


QUIET_POSTS = ("/api/login", "/api/logout", "/api/v4/messages", "/api/request")
PAGE_CSP = ("default-src 'self'; img-src 'self' data: blob:; style-src 'self' 'unsafe-inline'; "
            "script-src 'self' 'nonce-%s'; connect-src 'self'; frame-ancestors 'none'; base-uri 'none'; "
            "form-action 'self'; object-src 'none'")
_PAGES = {}


class GuardMiddleware:
    """1) Decides who may connect: this laptop, your Wi-Fi, your Tailscale devices, and - only when
          you switch the public link ON - visitors from the internet (who still need an approved account).
       2) Button presses need a special header and must come from this website (stops CSRF).
       3) Adds browser security headers to every answer."""

    def __init__(self, inner):
        self.inner = inner

    async def __call__(self, scope, receive, send):
        if scope["type"] != "http":
            return await self.inner(scope, receive, send)
        path = scope.get("path", "")
        clean = re.sub(r"/{2,}", "/", path)
        if len(clean) > 1:
            clean = clean.rstrip("/") or "/"
        if clean != path:
            scope = dict(scope)
            scope["path"] = clean
            scope["raw_path"] = clean.encode("latin-1", "ignore")
            path = clean
        info = client_info(scope)
        ck = info["headers"].get("cookie", "")
        if COOKIE + "=" in ck:
            scope["_cookie_token"] = ck.split(COOKIE + "=", 1)[1].split(";", 1)[0].strip()
        scope.setdefault("state", {})["client"] = info
        if info["public"]:
            if not store.get()["public_access"]:
                if path.startswith(("/api/", "/files/")):
                    resp = JSONResponse({"error": "This monitor is private right now."}, status_code=403)
                else:
                    resp = HTMLResponse(PRIVATE_PAGE, status_code=403)
                return await resp(scope, receive, send)
        elif not ip_allowed(info["ip"]):
            return await PlainTextResponse("Access denied from this network.", status_code=403)(scope, receive, send)

        ip = info["ip"]
        watch = not info["local"]
        if watch and suspicion.is_blocked(ip):
            return await JSONResponse({"error": "Too many refused requests. Try again in 10 minutes."},
                                      status_code=429)(scope, receive, send)
        method = scope["method"]
        if method not in ("GET", "HEAD"):
            h = info["headers"]
            if h.get("x-classync") != "1" or (h.get("origin") and urlparse(h.get("origin")).netloc != info["host"]):
                seclog("csrf_blocked", ip, detail="%s %s" % (method, path), level="warn")
                if watch:
                    suspicion.note(ip, 403, path)
                return await JSONResponse({"error": "Blocked request."}, status_code=403)(scope, receive, send)
            if watch and not limiter.allow(("post", ip), 180, 60):
                return await JSONResponse({"error": "Too many requests. Wait a minute."}, status_code=429)(scope, receive, send)

        https = info["https"]

        async def send_with_headers(message):
            if message["type"] == "http.response.start":
                status = message.get("status", 200)
                if watch and status >= 400:
                    suspicion.note(ip, status, path)
                if method not in ("GET", "HEAD") and status < 400 and path not in QUIET_POSTS:
                    who = auth.session_user(scope.get("_cookie_token")) if scope.get("_cookie_token") else None
                    seclog("action", ip, who["name"] if who else "", "%s %s" % (method, path))
                hdrs = MutableHeaders(scope=message)
                for k, v in SECURITY_HEADERS.items():
                    if k not in hdrs:
                        hdrs[k] = v
                if "cache-control" not in hdrs:
                    hdrs["cache-control"] = "no-store"
                if https:
                    hdrs["strict-transport-security"] = "max-age=31536000"
            await send(message)

        await self.inner(scope, receive, send_with_headers)


# ============================ app ============================
@asynccontextmanager
async def lifespan(_app):
    esp.apply(store.get())
    task = asyncio.create_task(watchdog())
    task2 = asyncio.create_task(hub_ticker())
    hub.log("info", "Website started (v%s)" % VERSION)
    threading.Thread(target=detector.preload, daemon=True, name="preload-ai").start()      # Start becomes instant
    threading.Thread(target=facewatch.preload, daemon=True, name="preload-face").start()
    yield
    task.cancel()
    task2.cancel()
    await asyncio.to_thread(_stop_all)
    await asyncio.to_thread(esp.stop)


app = FastAPI(docs_url=None, redoc_url=None, openapi_url=None, lifespan=lifespan, redirect_slashes=False)
app.add_middleware(GuardMiddleware)


def cinfo(request):
    return request.scope.get("state", {}).get("client") or client_info(request.scope)


def lock_key(request):
    """Key used for the wrong-password lock. Only the real laptop is exempt."""
    info = cinfo(request)
    if info["local"]:
        return "127.0.0.1"
    return ("fwd:" + info["ip"]) if info["proxied"] else info["ip"]


def current(request):
    return auth.session_user(request.cookies.get(COOKIE))


def need_user(request):
    u = current(request)
    if not u:
        raise HTTPException(401, "Please sign in.")
    return u


def need_admin(request):
    u = need_user(request)
    if u["role"] != "admin":
        raise HTTPException(403, "Only an admin can do this.")
    return u


def set_login_cookie(resp, request, token, remember):
    secure = cinfo(request)["https"] or request.url.scheme == "https"
    resp.set_cookie(COOKIE, token, max_age=30 * 24 * 3600 if remember else None, httponly=True,
                    samesite="lax", secure=secure, path="/")


async def body(request):
    try:
        data = await request.json()
    except Exception:
        raise HTTPException(400, "Bad request.")
    if not isinstance(data, dict):
        raise HTTPException(400, "Bad request.")
    return data


@app.exception_handler(StarletteHTTPException)
async def http_error(request, exc):
    code = exc.status_code
    msg = exc.detail if isinstance(exc.detail, str) and code != 500 else ERROR_WORDS.get(code, "Request failed.")   # 500 = never show internals
    if code in (404, 405) and not request.url.path.startswith(("/api/", "/files/")):
        return HTMLResponse(ERROR_PAGE % (code, ERROR_WORDS[code]), status_code=code)
    if code == 403 and request.url.path.startswith("/api/"):
        seclog("forbidden", cinfo(request)["ip"], detail=request.url.path, level="warn")
    return JSONResponse({"error": msg}, status_code=code)


@app.exception_handler(RequestValidationError)
async def bad_request(request, exc):
    return JSONResponse({"error": ERROR_WORDS[400]}, status_code=400)


@app.exception_handler(Exception)
async def server_error(request, exc):
    hub.log("warn", "internal error on %s: %s" % (request.url.path, type(exc).__name__))
    return JSONResponse({"error": ERROR_WORDS[500]}, status_code=500)


def page(name):
    """Serves a page with a fresh random nonce: only OUR scripts may run (injected ones are blocked)."""
    p = WEB / name
    mt = p.stat().st_mtime
    c = _PAGES.get(name)
    if not c or c[0] != mt:
        c = _PAGES[name] = (mt, p.read_text(encoding="utf-8"))
    n = pysecrets.token_urlsafe(16)
    resp = HTMLResponse(c[1].replace("<script>", '<script nonce="%s">' % n))
    resp.headers["content-security-policy"] = PAGE_CSP % n
    return resp


ERROR_PAGE = """<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SARTHIK Room</title><style>body{margin:0;min-height:100vh;display:grid;place-items:center;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Inter,Roboto,sans-serif;
background:#f5f5f7;color:#1d1d1f}@media(prefers-color-scheme:dark){body{background:#000;color:#f5f5f7}}main{text-align:center;padding:24px}
h1{font-size:56px;margin:0;font-weight:700;letter-spacing:-.03em}p{color:#86868b;font-size:17px}a{color:#0071e3;text-decoration:none}</style></head>
<body><main><h1>%d</h1><p>%s</p><p><a href="/">Back to SARTHIK Room</a></p></main></body></html>"""
ERROR_WORDS = {400: "This request could not be understood.", 401: "Please sign in.", 403: "You do not have access to this.",
               404: "This page does not exist.", 405: "This action is not allowed here.", 413: "That file is too large.",
               429: "Too many requests. Please wait a minute.", 500: "Something went wrong. Please try again."}


# ============================ pages ============================
def welcome_html():
    cfg = store.get()
    html = (WEB / "welcome.html").read_text(encoding="utf-8")
    code = cfg["google_verification"]
    meta = f'<meta name="google-site-verification" content="{escape(code)}">' if code else ""
    html = html.replace("<!--GSV-->", meta)
    return html.replace("{{REQUESTS}}", "1" if cfg["allow_requests"] else "0")


@app.api_route("/", methods=["GET", "HEAD"])
async def index(request: Request):
    info = cinfo(request)
    if not auth.has_users():
        if info["local"]:
            return RedirectResponse("/setup", 303)
        return HTMLResponse(PRIVATE_PAGE.replace("The owner has switched off the public link.",
                                                 "This monitor is being set up."), 503)
    if current(request):
        return page("index.html")
    return HTMLResponse(welcome_html())


@app.api_route("/login", methods=["GET", "HEAD"])
async def login_page(request: Request):
    if not auth.has_users():
        return RedirectResponse("/", 303)
    if current(request):
        return RedirectResponse("/", 303)
    return page("login.html")


@app.api_route("/request", methods=["GET", "HEAD"])
async def request_page(request: Request):
    if current(request):
        return RedirectResponse("/", 303)
    if not store.get()["allow_requests"]:
        return RedirectResponse("/login", 303)
    return page("request.html")


@app.api_route("/setup", methods=["GET", "HEAD"])
async def setup_page(request: Request):
    if auth.has_users():
        return RedirectResponse("/login", 303)
    if not cinfo(request)["local"]:
        return HTMLResponse("<h2 style='font-family:sans-serif'>First-time setup must be done on the laptop "
                            "itself: open http://localhost:" + str(store.get()["port"]) + "</h2>", 403)
    return page("setup.html")


@app.api_route("/favicon.ico", methods=["GET", "HEAD"])
async def favicon():
    svg = ("<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64'><rect width='64' height='64' rx='14' "
           "fill='#0b1016'/><circle cx='32' cy='32' r='14' fill='none' stroke='#22d3ee' stroke-width='6'/>"
           "<circle cx='32' cy='32' r='5' fill='#22d3ee'/></svg>")
    return Response(svg, media_type="image/svg+xml", headers={"cache-control": "public, max-age=86400"})


@app.api_route("/robots.txt", methods=["GET", "HEAD"])
async def robots(request: Request):
    host = cinfo(request)["host"]
    text = "User-agent: *\nAllow: /$\nAllow: /login\nAllow: /request\nDisallow: /api/\nDisallow: /files/\n"
    if cinfo(request)["https"] and host:
        text += f"Sitemap: https://{host}/sitemap.xml\n"
    return PlainTextResponse(text)


@app.api_route("/sitemap.xml", methods=["GET", "HEAD"])
async def sitemap(request: Request):
    info = cinfo(request)
    base = f"https://{info['host']}" if info["https"] else f"http://{info['host']}"
    urls = "".join(f"<url><loc>{escape(base + p)}</loc></url>" for p in ("/", "/login", "/request"))
    xml = ('<?xml version="1.0" encoding="UTF-8"?><urlset xmlns="http://www.sitemaps.org/schemas/sitemap/0.9">'
           + urls + "</urlset>")
    return Response(xml, media_type="application/xml")


# ============================ account API ============================
@app.post("/api/setup")
async def api_setup(request: Request):
    if auth.has_users():
        raise HTTPException(403, "Setup is already done.")
    if not cinfo(request)["local"]:
        raise HTTPException(403, "First-time setup must be done on the laptop itself.")
    d = await body(request)
    try:
        auth.create_user(str(d.get("username", "")).strip(), str(d.get("password", "")), "admin")
    except ValueError as e:
        raise HTTPException(400, str(e))
    name = str(d["username"]).strip()
    token = auth.create_session(name, True, request.headers.get("user-agent", ""))
    resp = JSONResponse({"ok": True})
    set_login_cookie(resp, request, token, True)
    return resp


@app.post("/api/login")
async def api_login(request: Request):
    d = await body(request)
    username = str(d.get("username", "")).strip()
    password = str(d.get("password", ""))
    remember = bool(d.get("remember", True))
    ip = lock_key(request)
    wait = auth.locked_seconds(ip, username)
    if wait:
        raise HTTPException(429, f"Too many wrong attempts. Try again in {wait // 60 + 1} min.")
    real = next((u["name"] for u in auth.list_users() if u["name"].lower() == username.lower()), username)
    if not await asyncio.to_thread(auth.verify_password, real, password):
        auth.record_fail(ip, username)
        locked = auth.locked_seconds(ip, username)
        seclog("login_locked" if locked else "login_failed", cinfo(request)["ip"], username[:20], level="warn")
        await asyncio.sleep(0.8)
        if auth.is_pending(username):
            raise HTTPException(403, "Your access request is waiting for the owner's approval.")
        raise HTTPException(401, "Wrong username or password.")
    auth.clear_fails(ip, username)
    auth.end_session(request.cookies.get(COOKIE))            # new login = brand-new session id
    token = auth.create_session(real, remember, request.headers.get("user-agent", ""))
    seclog("login_ok", cinfo(request)["ip"], real)
    resp = JSONResponse({"ok": True})
    set_login_cookie(resp, request, token, remember)
    return resp


@app.post("/api/logout")
async def api_logout(request: Request):
    u = current(request)
    auth.end_session(request.cookies.get(COOKIE))
    seclog("logout", cinfo(request)["ip"], u["name"] if u else "")
    resp = JSONResponse({"ok": True})
    resp.delete_cookie(COOKIE, path="/")
    return resp


_request_times = defaultdict(deque)


@app.post("/api/request")
async def api_request(request: Request):
    if not store.get()["allow_requests"]:
        raise HTTPException(403, "The owner is not accepting new requests right now.")
    ip = lock_key(request)
    now = time.time()
    q = _request_times[ip]
    while q and now - q[0] > 3600:
        q.popleft()
    if len(q) >= 3:
        raise HTTPException(429, "Too many requests from this device. Try again in an hour.")
    d = await body(request)
    try:
        auth.add_request(str(d.get("username", "")).strip(), str(d.get("password", "")),
                         d.get("full_name", ""), d.get("note", ""), ip)
    except ValueError as e:
        raise HTTPException(400, str(e))
    q.append(now)
    alerts.add("info", f"New access request from {str(d.get('full_name', ''))[:40]} "
                       f"({str(d.get('username', ''))[:20]}) - approve it in People & sharing",
               key="req" + str(d.get("username", "")), cooldown=5)
    return {"ok": True}


@app.post("/api/password")
async def api_password(request: Request):
    u = need_user(request)
    d = await body(request)
    if not auth.verify_password(u["name"], str(d.get("old", ""))):
        raise HTTPException(400, "Current password is wrong.")
    try:
        auth.set_password(u["name"], str(d.get("new", "")), keep_token=request.cookies.get(COOKIE))
    except ValueError as e:
        raise HTTPException(400, str(e))
    return {"ok": True, "msg": "Password changed. Other devices were signed out."}


@app.get("/api/users")
async def api_users(request: Request):
    need_admin(request)
    return {"users": auth.list_users(), "requests": auth.list_requests()}


@app.post("/api/users")
async def api_users_add(request: Request):
    need_admin(request)
    d = await body(request)
    password = str(d.get("password") or "") or pysecrets.token_urlsafe(9)
    try:
        auth.create_user(str(d.get("username", "")).strip(), password, str(d.get("role", "viewer")))
    except ValueError as e:
        raise HTTPException(400, str(e))
    return {"ok": True, "password": password}


@app.post("/api/users/delete")
async def api_users_delete(request: Request):
    u = need_admin(request)
    d = await body(request)
    name = str(d.get("username", ""))
    if name == u["name"]:
        raise HTTPException(400, "You cannot delete yourself.")
    if auth.role_of(name) == "admin" and auth.admin_count() <= 1:
        raise HTTPException(400, "Keep at least one admin.")
    try:
        auth.delete_user(name)
    except ValueError as e:
        raise HTTPException(400, str(e))
    return {"ok": True}


@app.post("/api/requests/approve")
async def api_approve(request: Request):
    u = need_admin(request)
    d = await body(request)
    try:
        auth.approve_request(str(d.get("username", "")), str(d.get("role", "viewer")))
    except ValueError as e:
        raise HTTPException(400, str(e))
    alerts.add("ok", f"{u['name']} approved {d.get('username')}", key="appr" + str(d.get("username")), cooldown=3)
    return {"ok": True}


@app.post("/api/requests/deny")
async def api_deny(request: Request):
    need_admin(request)
    d = await body(request)
    try:
        auth.deny_request(str(d.get("username", "")))
    except ValueError as e:
        raise HTTPException(400, str(e))
    return {"ok": True}


# ============================ camera API ============================
def can_control(u):
    return u["role"] == "admin" or store.get()["viewers_can_start"]


@app.post("/api/camera/start")
async def api_start(request: Request):
    u = need_user(request)
    if not can_control(u):
        raise HTTPException(403, "Only the admin can start the camera.")
    camera.start(store.get())
    detector.start()
    facewatch.start()
    camera.last_viewer_time = time.time()
    alerts.add("info", f"Monitoring started by {u['name']}", key="start", cooldown=3)
    return {"ok": True}


@app.post("/api/camera/stop")
async def api_stop(request: Request):
    u = need_user(request)
    if not can_control(u):
        raise HTTPException(403, "Only the admin can stop the camera.")
    await asyncio.to_thread(_stop_all)
    alerts.add("info", f"Monitoring stopped by {u['name']}", key="stop", cooldown=3)
    return {"ok": True}


def _stop_all():
    facewatch.stop()
    detector.stop()
    camera.stop()


@app.get("/api/stream")
async def api_stream(request: Request, q: str = "high"):
    need_user(request)
    q = "low" if q == "low" else "high"
    if not camera.running:
        raise HTTPException(409, "Camera is off.")

    async def frames():
        last = -1
        camera.viewers[q] += 1
        try:
            while camera.running:
                if await request.is_disconnected():
                    break
                fid, jpg = camera.get_jpeg(q)
                if jpg is None or fid == last:
                    await asyncio.sleep(0.02)
                    continue
                last = fid
                camera.last_viewer_time = time.time()
                yield (b"--frame\r\nContent-Type: image/jpeg\r\nContent-Length: " +
                       str(len(jpg)).encode() + b"\r\n\r\n" + jpg + b"\r\n")
        finally:
            camera.viewers[q] -= 1
            camera.last_viewer_time = time.time()

    return StreamingResponse(frames(), media_type="multipart/x-mixed-replace; boundary=frame",
                             headers={"X-Accel-Buffering": "no"})


@app.post("/api/snapshot")
async def api_snapshot(request: Request):
    need_user(request)
    name = await asyncio.to_thread(camera.snapshot)
    if not name:
        raise HTTPException(409, "Camera is off.")
    return {"ok": True, "file": name}


@app.get("/files/snapshots/{name}")
async def get_snapshot(name: str, request: Request):
    need_user(request)
    if not SNAP_RE.match(name):
        raise HTTPException(404, "Not found.")
    path = BASE / "snapshots" / name
    if not path.exists():
        raise HTTPException(404, "Not found.")
    return FileResponse(path, media_type="image/jpeg", filename=name)


@app.post("/api/record")
async def api_record(request: Request):
    need_admin(request)
    d = await body(request)
    on = bool(d.get("on"))
    ok = camera.set_recording(on)
    if on and not ok:
        raise HTTPException(409, "Start the camera first.")
    alerts.add("info", "Recording started (saved on the laptop only)" if on else "Recording stopped",
               key=f"rec{on}", cooldown=3)
    return {"ok": True, "file": camera.record_file if on else ""}


@app.get("/api/cameras")
async def api_cameras(request: Request):
    need_admin(request)
    if camera.running:
        raise HTTPException(409, "Stop the camera first, then search again.")
    try:
        found = await asyncio.to_thread(CameraService.scan)
    except Exception:
        found = []
    return {"cameras": found}


@app.get("/api/log.csv")
async def api_log(request: Request):
    need_admin(request)
    path = DATA / "headcount_log.csv"
    if not path.exists():
        raise HTTPException(404, "No log yet. It starts once the camera runs for 30 seconds.")
    return FileResponse(path, media_type="text/csv", filename="headcount_log.csv")


# ============================ settings + status ============================
@app.get("/api/settings")
async def api_settings(request: Request):
    need_admin(request)
    return store.get()


@app.post("/api/settings")
async def api_settings_save(request: Request):
    u = need_user(request)
    d = await body(request)
    if u["role"] != "admin":
        raise HTTPException(403, "Only an admin can change settings.")
    for k in ("port", "settings_version"):
        d.pop(k, None)
    for k in [k for k, v in d.items() if v is None]:        # ignore empty boxes instead of failing
        d.pop(k)
    gv = d.get("google_verification")
    if isinstance(gv, str):                                  # accept the whole <meta ...> tag too
        m = re.search(r"""content\s*=\s*["']([^"']+)["']""", gv)
        if m:
            d["google_verification"] = m.group(1)
    try:
        changed = store.update(d)
    except (ValueError, TypeError) as e:
        raise HTTPException(400, str(e))
    cfg = store.get()
    if camera.running and changed & {"camera_index", "resolution", "fps", "jpeg_quality"}:
        await asyncio.to_thread(camera.stop)
        camera.start(cfg)
    if changed & {"esp32_link", "esp32_port", "esp32_mode", "esp32_host"}:
        await asyncio.to_thread(esp.apply, cfg)
    if "public_access" in changed:
        alerts.add("info", "Public link switched " + ("ON" if cfg["public_access"] else "OFF"), key="pub", cooldown=2)
    return {"ok": True, "changed": sorted(changed)}


def addresses():
    ips = set()
    try:
        for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
            ips.add(info[4][0])
    except OSError:
        pass
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ips.add(s.getsockname()[0])
        s.close()
    except OSError:
        pass
    lan, ts = [], []
    for ip in sorted(ips):
        a = _ip(ip)
        if a is None or a.is_loopback:
            continue
        if a in TS_V4:
            ts.append(ip)
        elif a.is_private:
            lan.append(ip)
    return lan, ts


_ts_cache = {"t": 0.0, "data": {"installed": False, "url": "", "funnel": False, "online": False}}
_ts_lock = threading.Lock()


def tailscale_info():
    """Reads this laptop's public Tailscale address (cached for 60 s)."""
    with _ts_lock:
        if time.time() - _ts_cache["t"] < 60:
            return dict(_ts_cache["data"])
        _ts_cache["t"] = time.time()
    exe = shutil.which("tailscale")
    if not exe and os.name == "nt":
        p = r"C:\Program Files\Tailscale\tailscale.exe"
        exe = p if os.path.exists(p) else None
    info = {"installed": bool(exe), "url": "", "funnel": False, "online": False}
    if exe:
        flags = 0x08000000 if os.name == "nt" else 0          # no black window flashing
        try:
            out = subprocess.run([exe, "status", "--json"], capture_output=True, text=True, timeout=5,
                                 creationflags=flags)
            me = (json.loads(out.stdout or "{}").get("Self") or {})
            dns = (me.get("DNSName") or "").rstrip(".")
            info["url"] = f"https://{dns}" if dns else ""
            info["online"] = bool(me.get("Online"))
        except Exception:
            pass
        try:
            out = subprocess.run([exe, "serve", "status", "--json"], capture_output=True, text=True, timeout=5,
                                 creationflags=flags)
            d = json.loads(out.stdout or "{}")
            info["funnel"] = any(bool(v) for v in (d.get("AllowFunnel") or {}).values())
        except Exception:
            pass
    with _ts_lock:
        _ts_cache["data"] = info
    return dict(info)


@app.get("/api/status")
async def api_status(request: Request):
    u = need_user(request)
    cfg = store.get()
    ai = detector.snapshot_state()
    objects = sorted(({"name": k, "count": v} for k, v in ai["counts"].items() if k != "person"),
                     key=lambda o: (-o["count"], o["name"]))
    info = cinfo(request)
    data = {
        "version": VERSION,
        "time": time.time(),
        "user": {"name": u["name"], "role": u["role"]},
        "via_public": info["public"],
        "can_control": can_control(u),
        "camera": {
            "state": camera.state, "error": camera.error, "detail": camera.detail,
            "starting_for": round(time.time() - camera.start_click, 1) if camera.state == "starting" else 0,
            "index": camera.index_used, "width": camera.width, "height": camera.height,
            "fps": round(camera.fps_actual, 1), "viewers": camera.total_viewers(), "since": camera.started_at,
            "recording": camera.recording, "record_file": camera.record_file,
        },
        "ai": {
            "enabled": cfg["detection_enabled"], "state": ai["state"], "error": ai["error"],
            "model": ai["model"], "ms": ai["ms"], "people": ai["people"], "objects": objects,
            "peak_today": ai["peak_today"], "history": ai["history"], "max_occupancy": cfg["max_occupancy"],
        },
        "view": {"show_boxes": cfg["show_boxes"], "resolution": cfg["resolution"], "fps_set": cfg["fps"]},
        "door": esp.state(),
        "faces": facewatch.snapshot(),
        "alerts": alerts.recent(20),
    }
    if u["role"] == "admin":
        lan, ts = addresses()
        port = cfg["port"]
        data["access"] = {
            "local": f"http://localhost:{port}",
            "lan": [f"http://{ip}:{port}" for ip in lan] if cfg["allow_lan"] else [],
            "tailscale": [f"http://{ip}:{port}" for ip in ts] if cfg["allow_tailscale"] else [],
        }
        data["public"] = {"enabled": cfg["public_access"], "requests_open": cfg["allow_requests"],
                          "tailscale": await asyncio.to_thread(tailscale_info)}
        data["requests_pending"] = len(auth.list_requests())
    resp = JSONResponse(data)
    if u.get("renewed") and u.get("remember"):
        set_login_cookie(resp, request, request.cookies.get(COOKIE), True)
    return resp


# ============================ v3: one website for the whole CLASSYNC Stage 1 ============================
import platform as _platform
from datetime import datetime as _dt

MAX_PHOTO = 12 * 1024 * 1024
UNK_RE = re.compile(r"^unknown_\d{8}_\d{6}\.jpg$")
ID_RE = re.compile(r"^[a-z0-9_]{2,30}$")
_started = time.time()


def _today():
    return _dt.now().strftime("%Y-%m-%d")


def _dir_size(p):
    total = 0
    try:
        for f in Path(p).rglob("*"):
            if f.is_file():
                total += f.stat().st_size
    except OSError:
        pass
    return total


@app.get("/api/v3/overview")
async def v3_overview(request: Request):
    need_user(request)
    day = _today()
    faces = facewatch.snapshot()
    door = esp.state()
    rows, visitors = db.attendance(day)
    present = sorted(r["person_id"] for r in rows if (r["entries"] or 0) > 0)
    people = db.people_list()
    _, r2 = db.r2_list(1)
    return {
        "time": time.time(), "day": day,
        "registered": len(people), "with_laptop_face": len(db.face_counts()),
        "present_today": present, "visitors_today": len(visitors),
        "in_room_now": faces["present"], "faces_now": len(faces["faces"]), "unknown_now": faces["unknown_now"],
        "people_now": detector.people if detector.state == "running" else None,
        "alerts_today": db.count("day=? AND severity='alert'", (day,)),
        "warnings_today": db.count("day=? AND severity='warn'", (day,)),
        "mismatches_today": db.count("day=? AND type IN ('mismatch_door_cctv','face_mismatch','no_door_auth')", (day,)),
        "unknown_today": db.count("day=? AND type IN ('cctv_unknown','unknown_card')", (day,)),
        "verified_today": db.count("day=? AND type='cross_verified'", (day,)),
        "events_total": db.count(),
        "camera": camera.state, "face_state": faces["state"], "ai_state": detector.state,
        "board": {"enabled": door["enabled"], "connected": door["connected"], "error": door["error"],
                  "v8": bool(door.get("v8"))},
        "r2": r2,
        "recent": db.events(limit=12),
    }


@app.get("/api/v3/events")
async def v3_events(request: Request, day: str = "", person: str = "", severity: str = "", type: str = "",
                    source: str = "", q: str = "", limit: int = 200, before: int = 0):
    need_user(request)
    return {"events": db.events(day=day or None, person=person or None,
                                types=[t for t in type.split(",") if t] or None,
                                severity=[s for s in severity.split(",") if s] or None,
                                source=source or None, search=q.strip()[:60] or None,
                                limit=limit, before_id=before or None),
            "titles": TITLES}


@app.get("/api/v3/events.csv")
async def v3_events_csv(request: Request, day: str = "", person: str = "", severity: str = "", type: str = "",
                        source: str = "", q: str = ""):
    need_user(request)
    rows = db.events(day=day or None, person=person or None, types=[t for t in type.split(",") if t] or None,
                     severity=[s for s in severity.split(",") if s] or None, source=source or None,
                     search=q.strip()[:60] or None, limit=5000)
    name = "classync_events_%s.csv" % (day or "all")
    return Response(db.export_csv(rows), media_type="text/csv",
                    headers={"content-disposition": 'attachment; filename="%s"' % name})


@app.get("/api/v3/attendance")
async def v3_attendance(request: Request, day: str = ""):
    need_user(request)
    day = day or _today()
    rows, visitors = db.attendance(day)
    by = {r["person_id"]: r for r in rows}
    out = []
    for p in db.people_list():
        r = by.get(p["id"], {})
        out.append({"id": p["id"], "name": p["name"], "present": (r.get("entries") or 0) > 0,
                    "first_entry": r.get("first_entry"), "last_seen": r.get("last_seen"),
                    "entries": r.get("entries") or 0, "verified": r.get("verified") or 0,
                    "cctv": r.get("cctv") or 0, "mismatches": r.get("mismatches") or 0, "methods": r.get("methods") or ""})
    out.sort(key=lambda x: (not x["present"], x["name"].lower()))
    return {"day": day, "people": out, "visitors": visitors, "days": db.days()}


@app.get("/api/v3/people")
async def v3_people(request: Request):
    need_user(request)
    stats = db.person_stats()
    fc = db.face_counts()
    present = {p["id"] for p in facewatch.snapshot()["present"]}
    out = []
    for p in db.people_list():
        s = stats.get(p["id"], {})
        out.append({**p, "laptop_faces": fc.get(p["id"], 0), "in_room": p["id"] in present,
                    "first_seen": s.get("first_seen"), "last_seen": s.get("last_seen"),
                    "appearances": s.get("appearances") or 0, "auth_entries": s.get("auth_entries") or 0,
                    "cctv_observations": s.get("cctv_observations") or 0, "mismatches": s.get("mismatches") or 0,
                    "verified": s.get("verified") or 0, "sources": s.get("sources") or "", "devices": s.get("devices") or ""})
    return {"people": out, "unknown_total": db.unknown_total()}


@app.post("/api/v3/people/add")
async def v3_people_add(request: Request):
    need_admin(request)
    d = await body(request)
    name = str(d.get("name", "")).strip()[:40]
    pid = str(d.get("id") or re.sub(r"[^a-z0-9_]", "", name.lower().replace(" ", "_")))[:30]
    if not name or not ID_RE.match(pid):
        raise HTTPException(400, "Type a name (letters/numbers). For door-board people use EXACTLY the name on the board.")
    if not db.add_person(pid, name):
        raise HTTPException(409, "A person with id '%s' already exists." % pid)
    facewatch.reload()
    hub.log("info", "Person added on the website: %s (%s)" % (name, pid))
    return {"ok": True, "id": pid}


@app.post("/api/v3/people/delete")
async def v3_people_delete(request: Request):
    need_admin(request)
    pid = str((await body(request)).get("id", ""))
    p = db.person(pid)
    if not p:
        raise HTTPException(404, "No such person.")
    db.delete_person(pid)
    facewatch.reload()
    hub.log("info", "Laptop face data removed for %s" % pid)
    return {"ok": True, "kept_board_person": p.get("board_index") is not None}


@app.post("/api/v3/people/clearfaces")
async def v3_people_clearfaces(request: Request):
    need_admin(request)
    pid = str((await body(request)).get("id", ""))
    db.clear_faces(pid)
    facewatch.reload()
    hub.log("info", "Laptop face samples cleared for %s" % pid)
    return {"ok": True}


@app.post("/api/v3/faces/enroll")
async def v3_faces_enroll(request: Request):
    need_admin(request)
    pid = str((await body(request)).get("id", ""))
    if not db.person(pid):
        raise HTTPException(404, "No such person.")
    ok, msg = facewatch.enroll_from_camera(pid)
    if not ok:
        raise HTTPException(409, msg)
    return {"ok": True}


@app.post("/api/v3/faces/cancel")
async def v3_faces_cancel(request: Request):
    need_admin(request)
    facewatch.cancel_enroll()
    return {"ok": True}


@app.post("/api/v3/faces/photo")
async def v3_faces_photo(request: Request, id: str = ""):
    u = need_admin(request)
    if not ID_RE.match(id or "") or not db.person(id):
        raise HTTPException(404, "No such person.")
    if not limiter.allow(("upload", u["name"]), 20, 60):
        raise HTTPException(429, "Too many photos at once. Wait a minute.")
    try:
        size = int(request.headers.get("content-length") or 0)
    except ValueError:
        size = 0
    if size > MAX_PHOTO:
        raise HTTPException(413, "That photo is too large (max 12 MB).")
    data = await request.body()
    if not data or len(data) > MAX_PHOTO:
        raise HTTPException(413 if data else 400, "Send one photo (JPG, PNG or WebP, up to 12 MB).")
    if not (data[:3] == b"\xff\xd8\xff" or data[:8] == b"\x89PNG\r\n\x1a\n" or (data[:4] == b"RIFF" and data[8:12] == b"WEBP")):
        seclog("upload_refused", cinfo(request)["ip"], u["name"], "not a JPG/PNG/WebP", level="warn")
        raise HTTPException(400, "Only JPG, PNG or WebP photos are accepted.")
    try:
        ok, msg = await asyncio.to_thread(facewatch.enroll_image, id, data)
    except Exception:
        raise HTTPException(500, "The photo could not be read. Try another photo.")
    if not ok:
        raise HTTPException(400, msg)
    db.add_event("face_registered", "website", person_id=id, detail="photo", device="laptop")
    return {"ok": True, "msg": msg}


@app.get("/api/v3/faces/status")
async def v3_faces_status(request: Request):
    need_user(request)
    snap = facewatch.snapshot()
    job = snap.get("job")
    if job and job.get("status") == "done" and not job.get("logged"):
        facewatch.job["logged"] = True
        db.add_event("face_registered", "website", person_id=job["pid"], detail="%d camera samples" % job["got"], device="laptop")
    return snap


@app.get("/api/v3/unknown")
async def v3_unknown(request: Request):
    need_user(request)
    files = sorted(facewatch.unknown_dir.glob("unknown_*.jpg"), reverse=True)[:60]
    return {"photos": [{"name": f.name, "t": f.stat().st_mtime} for f in files]}


@app.get("/files/unknown/{name}")
async def v3_unknown_file(request: Request, name: str):
    need_user(request)
    if not UNK_RE.match(name):
        raise HTTPException(404, "Not found")
    p = facewatch.unknown_dir / name
    if not p.exists():
        raise HTTPException(404, "Not found")
    return FileResponse(str(p), media_type="image/jpeg")


@app.get("/api/v3/devices")
async def v3_devices(request: Request):
    need_user(request)
    door = esp.state()
    ai = detector.snapshot_state()
    fw = facewatch.snapshot()
    du = shutil.disk_usage(str(BASE))
    return {
        "board": {"enabled": door["enabled"], "mode": door["mode"], "address": door["port"], "connected": door["connected"],
                  "error": door["error"], "v8": door.get("v8") or {}, "checked_in": door["checked_in"]},
        "laptop": {"version": VERSION, "python": _platform.python_version(), "system": _platform.platform(terse=True),
                   "cpus": os.cpu_count(), "uptime": time.time() - _started,
                   "camera": {"state": camera.state, "fps": round(camera.fps_actual, 1), "width": camera.width,
                              "height": camera.height, "viewers": camera.total_viewers()},
                   "people_ai": {"state": ai["state"], "model": ai["model"], "ms": ai["ms"]},
                   "face_ai": {"state": fw["state"], "ms": fw["ms"], "error": fw["error"], "registered": fw["registered_people"]},
                   "disk_free": du.free, "disk_total": du.total},
    }


@app.get("/api/v3/storage")
async def v3_storage(request: Request):
    need_user(request)
    du = shutil.disk_usage(str(BASE))
    dbfile = DATA / "classync.db"
    sizes = await asyncio.to_thread(lambda: {
        "database": sum(f.stat().st_size for f in DATA.glob("classync.db*") if f.is_file()),
        "snapshots": _dir_size(BASE / "snapshots"), "recordings": _dir_size(BASE / "recordings"),
        "unknown_faces": _dir_size(facewatch.unknown_dir)})
    v8 = esp.state().get("v8") or {}
    h = v8.get("health") or {}
    return {"laptop": {"disk_free": du.free, "disk_total": du.total, "sizes": sizes, "records": db.sizes(),
                       "database_file": str(dbfile)},
            "board": {"storage_total": h.get("storage_total"), "storage_used": h.get("storage_used"),
                      "nvs_used": h.get("nvs_used"), "nvs_total": h.get("nvs_total"),
                      "events_total": (v8.get("counts") or {}).get("events_total")}}


@app.get("/api/v3/logs")
async def v3_logs(request: Request):
    need_user(request)
    return {"logs": hub.logs(400), "alerts": alerts.recent(50)}


@app.get("/api/v3/r2")
async def v3_r2(request: Request):
    need_user(request)
    rows, tot = db.r2_list(300)
    v8 = esp.state().get("v8") or {}
    return {"visits": rows, "totals": tot, "board_r2": v8.get("radar_r2") or {}}


async def hub_ticker():
    while True:
        await asyncio.sleep(1.0)
        try:
            fw = facewatch.snapshot()
            hub.face_running = fw["state"] == "running"
            await asyncio.to_thread(hub.tick, detector.people if detector.state == "running" else 0,
                                    len([f for f in fw["faces"] if not f.get("far")]), camera.state == "live")
        except Exception as e:
            hub.log("warn", "cross-check tick problem: %s" % e)


# ============================ v4: control centre, chat, phone, trend, backups ============================
import urllib.parse as _uparse
import urllib.request as _ureq
from .integrations import Backups, Outbox

outbox = Outbox(store.get, hub.log)
db.on_event = outbox.on_event
backups = Backups(BASE, DATA, db, hub.log)
NAME_ARG = re.compile(r"^[a-z0-9_]{2,30}$")
DOOR_EXACT = ("opendoor", "reset", "alerts on", "alerts off", "testalert", "cancelcard", "restart")
DOOR_PREFIX = ("learncard ", "addphone ", "removephone ", "forget ")
CHAT_RE = re.compile(r"^(all|[A-Za-z0-9_]{3,20})$")


def _door_cmd_ok(cmd):
    if cmd in DOOR_EXACT:
        return True
    return any(cmd.startswith(p) and NAME_ARG.match(cmd[len(p):]) for p in DOOR_PREFIX)


def _send_door(cmd):
    cfg = store.get()
    host = cfg.get("esp32_host", "")
    if not cfg.get("esp32_link") or cfg.get("esp32_mode") != "wifi" or not host:
        raise HTTPException(409, "Link the door board first: Settings > Door board link > Over Wi-Fi.")
    req = _ureq.Request("http://%s/api/cmd?c=%s" % (host, _uparse.quote(cmd)), data=b"", method="POST",
                        headers={"X-Key": cfg.get("door_key", ""), "User-Agent": "SARTHIK-Room"})
    try:
        with _ureq.urlopen(req, timeout=6) as r:
            return json.loads(r.read(2000).decode("utf-8", "ignore") or "{}")
    except _ureq.HTTPError as e:
        if e.code == 403:
            raise HTTPException(502, "The door board refused the key. Settings > Door key must match the board's secrets.h.")
        raise HTTPException(502, "The door board refused this command (old firmware? upload v8.1).")
    except Exception:
        raise HTTPException(504, "The door board did not answer. Is it on Wi-Fi?")


@app.post("/api/v4/door")
async def v4_door(request: Request):
    u = need_admin(request)
    if not limiter.allow(("door", u["name"]), 20, 60):
        raise HTTPException(429, "Too many door commands. Wait a minute.")
    cmd = " ".join(str((await body(request)).get("cmd", "")).lower().split())[:40]
    if not _door_cmd_ok(cmd):
        raise HTTPException(400, "That command is not allowed.")
    r = await asyncio.to_thread(_send_door, cmd)
    db.add_event("board_command", "website", name=u["name"], detail=cmd, device="laptop")
    seclog("door_command", cinfo(request)["ip"], u["name"], cmd)
    return {"ok": bool(r.get("ok", True)), "cmd": cmd}


@app.post("/api/v4/notify")
async def v4_notify(request: Request):
    u = need_admin(request)
    if not limiter.allow(("notify", u["name"]), 10, 60):
        raise HTTPException(429, "Too many phone messages. Wait a minute.")
    d = await body(request)
    text = " ".join(str(d.get("text", "")).split())[:500]
    if not text:
        raise HTTPException(400, "Type a message.")
    if not outbox.phone("SARTHIK Room - message from " + u["name"], text):
        raise HTTPException(503, "Busy, try again.")
    db.add_message(u["name"], "phone", text, via="phone")
    return {"ok": True}


@app.get("/api/v4/chat/users")
async def v4_chat_users(request: Request):
    u = need_user(request)
    return {"me": u["name"], "users": [x["name"] for x in auth.list_users() if x["name"] != u["name"]]}


@app.get("/api/v4/messages")
async def v4_messages(request: Request, chat: str = "all", after: int = 0):
    u = need_user(request)
    if not CHAT_RE.match(chat):
        raise HTTPException(400, "Bad chat.")
    if chat != "all" and not any(x["name"] == chat for x in auth.list_users()):
        raise HTTPException(404, "No such person.")
    seen = int(request.query_params.get("seen", "0") or 0) if request.query_params.get("seen", "0").isdigit() else 0
    return {"messages": db.messages(u["name"], chat, after=max(0, after)), "unread": db.unread_counts(u["name"], seen),
            "last": db.last_message_id(), "me": u["name"]}


@app.post("/api/v4/messages")
async def v4_send_message(request: Request):
    u = need_user(request)
    if not limiter.allow(("msg", u["name"]), 30, 60):
        raise HTTPException(429, "Too many messages. Wait a minute.")
    d = await body(request)
    to = str(d.get("to", "all"))
    text = str(d.get("text", "")).strip()[:1000]
    if not text or not CHAT_RE.match(to) or (to != "all" and not any(x["name"] == to for x in auth.list_users())):
        raise HTTPException(400, "Type a message and choose who gets it.")
    mid = db.add_message(u["name"], to, text)
    return {"ok": True, "id": mid}


@app.get("/api/v4/trend")
async def v4_trend(request: Request):
    need_user(request)
    return {"days": db.trend(7)}


@app.get("/api/v4/control")
async def v4_control(request: Request):
    need_admin(request)
    cfg = store.get()
    return {"ntfy_topic": cfg.get("ntfy_topic"), "phone_alerts": cfg.get("phone_alerts"), "webhook_url": cfg.get("webhook_url"),
            "webhook_events": cfg.get("webhook_events"), "door_key_set": bool(cfg.get("door_key")),
            "outbox": {"sent": outbox.sent, "last_error": outbox.last_error}, "backups": backups.list()[:10],
            "security_log": str(DATA / "security.log")}


@app.post("/api/v4/control")
async def v4_control_save(request: Request):
    need_admin(request)
    d = await body(request)
    allowed = {k: d[k] for k in ("ntfy_topic", "phone_alerts", "webhook_url", "webhook_events", "door_key") if k in d}
    if "door_key" in allowed and not allowed["door_key"]:
        allowed.pop("door_key")
    try:
        store.update(allowed)
    except ValueError as e:
        raise HTTPException(400, str(e))
    return {"ok": True}


@app.post("/api/v4/backup")
async def v4_backup(request: Request):
    need_admin(request)
    if not limiter.allow(("backup", "all"), 3, 300):
        raise HTTPException(429, "A backup was just made. Wait a few minutes.")
    name = await asyncio.to_thread(backups.run, "manual")
    return {"ok": True, "name": name}


@app.post("/api/v4/test-phone")
async def v4_test_phone(request: Request):
    need_admin(request)
    if not limiter.allow(("notify", "test"), 5, 60):
        raise HTTPException(429, "Wait a minute.")
    outbox.phone("SARTHIK Room test", "If you can read this, the website reaches your phone.")
    return {"ok": True}


# ============================ background jobs ============================
async def watchdog():
    while True:
        await asyncio.sleep(15)
        try:
            cfg = store.get()
            mins = cfg["auto_stop_minutes"]
            if camera.running and mins > 0 and camera.total_viewers() == 0 and \
                    time.time() - camera.last_viewer_time > mins * 60:
                await asyncio.to_thread(_stop_all)
                alerts.add("info", f"Camera auto-stopped: nobody watched for {mins} min", key="autostop", cooldown=5)
            if camera.state == "error" and detector.state not in ("off",):
                await asyncio.to_thread(detector.stop)
                alerts.add("alert", "Camera problem: " + camera.error, key="camerr", cooldown=60)
        except Exception:
            pass


# ============================ run ============================
def run():
    os.chdir(BASE)
    cfg = store.get()
    port = int(cfg["port"])
    lan, ts = addresses()
    line = "=" * 64
    print(line)
    print("  SARTHIK ROOM  v" + VERSION)
    print(line)
    print("  Camera is OFF until someone presses Start on the dashboard.")
    print("")
    print("  Open on this laptop :  http://localhost:%d" % port)
    if cfg["allow_lan"]:
        for ip in lan:
            print("  Phone on same Wi-Fi :  http://%s:%d" % (ip, port))
    print("  Public website link :  " + ("ON (see People & sharing on the dashboard)" if cfg["public_access"]
                                         else "OFF (switch it on in Settings)"))
    print("")
    print("  To STOP the server: close this window (or press Ctrl+C).")
    print(line)
    sys.stdout.flush()
    if not os.environ.get("CCTV_NO_BROWSER"):
        threading.Timer(2.0, lambda: webbrowser.open(f"http://localhost:{port}")).start()
    try:
        # proxy_headers=False: our GuardMiddleware reads the forwarding headers itself, safely
        uvicorn.run(app, host="0.0.0.0", port=port, log_level="warning", access_log=False,
                    timeout_graceful_shutdown=3, proxy_headers=False)
    except (OSError, SystemExit):
        print("\nThe server stopped. If it did not start at all, port %d may already be in use:" % port)
        print("is the CCTV already running in another window?")


if __name__ == "__main__":
    run()
