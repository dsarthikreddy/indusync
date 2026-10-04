// SARTHIK Room - 24/7 cloud website (Firebase free plan). Works with the laptop OFF.
// Security is enforced by firestore.rules; this page only shows what the rules allow.
(function () {
"use strict";
const C = window.SR_CONFIG || {}, DEV = C.device || "door-1";
const $ = id => document.getElementById(id);
const E = s => String(s == null ? "" : s).replace(/[&<>"']/g, c => ({"&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;"}[c]));
let auth, db, me = null, role = "", page = "home", unsubs = [], device = null, events = [], cfg = {};
const TABS = [["home", "Home"], ["board", "Board"], ["att", "Attendance"], ["sensors", "Sensors"], ["control", "Control", "admin"], ["sec", "Security"],
  ["chat", "Messages"], ["health", "Health"], ["members", "Members", "admin"]];
const TITLES = {entry_card_face: "Entered (card + face)", entry_phone: "Entered (phone key)", entry_visitor_qr: "Visitor entered (QR)",
  unknown_card: "Unknown card", card_locked: "Locked card tapped", face_mismatch: "Face did not match card", security_lock: "Card locked: 3 wrong faces",
  no_face: "No face seen", fake_pass: "FAKE visitor pass", pass_reused: "Used pass shown again", proxy_blocked: "Proxy enrolment blocked",
  face_enrolled: "Face saved on door", phone_paired: "Phone paired", phone_refused: "Phone refused", board_started: "Door board started",
  card_registered: "New card registered", door_opened_remote: "Door opened remotely", board_command: "Board command",
  cctv_unknown: "UNKNOWN face on CCTV", mismatch_door_cctv: "Door person is not CCTV person", no_door_auth: "In room without door check-in",
  cross_verified: "Confirmed by CCTV", multiple_people: "More people than entries"};
const HELP = {
  home: "Today at a glance. 'Present today' counts people who entered with card + face or phone key. Everything here comes from the door board through Firebase, so it works 24/7 even when the laptop is off.",
  board: "A live copy of the door board. The black box shows exactly what the OLED screen shows; below it are the latest lines of the Arduino Serial Monitor. They update a few seconds after something happens at the door (about every 30 s otherwise).",
  att: "Pick a day to see who came, first and last entry, and how (card + face or phone key). 'Download CSV' opens in Excel.",
  sensors: "The 15 sensor slots of the board. ok = good reading, off = not connected yet, no_data = connected but not answering (check wires), fault = value impossible (sensor or setting wrong). Graphs come from ThingSpeak.",
  control: "Buttons that reach the door from anywhere. The board checks for new commands every 10 seconds, then shows 'done'. Only admins see this page.",
  sec: "Alerts (red) and warnings (orange): unknown cards, wrong faces, fake visitor passes and CCTV problems. Telegram gets the red ones instantly.",
  chat: "Family chat. 'Everyone' is seen by all members; pick a person for a private chat only the two of you can read.",
  health: "Is every part working? Green = working. Run the security check after any change: every line must say 'blocked'.",
  members: "Who may sign in. Viewer = family (can look), Admin = can control the door. Also create the same e-mail in Firebase > Authentication > Users."};
function toast(m, ms) { const t = $("toast"); t.textContent = m; t.classList.remove("hidden"); clearTimeout(toast.h); toast.h = setTimeout(() => t.classList.add("hidden"), ms || 3200); }
const ts = v => v && v.toDate ? v.toDate() : (v ? new Date(v) : null);
const T = v => { const d = ts(v); return d ? d.toLocaleString([], {day: "2-digit", month: "short", hour: "2-digit", minute: "2-digit"}) : "–"; };
const sev = s => `<span class="sev ${E(s)}">${E(s)}</span>`;
const kpi = (v, l, c) => `<div class="kpi ${c || ""}"><b>${v == null ? "–" : v}</b><span>${l}</span></div>`;
const isAdmin = () => role === "admin";
const dayKey = d => d.toISOString().slice(0, 10);

// ---------------- start ----------------
function start() {
  const th = (() => { try { return localStorage.getItem("sr-theme"); } catch (_) { return null; } })();
  document.documentElement.dataset.theme = th || (matchMedia && matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light");
  $("themeBtn").onclick = () => { const t = document.documentElement.dataset.theme === "dark" ? "light" : "dark"; document.documentElement.dataset.theme = t; try { localStorage.setItem("sr-theme", t); } catch (_) {} };
  if (!C.apiKey || !C.projectId || !window.firebase) { $("setupMissing").classList.remove("hidden"); return; }
  firebase.initializeApp({apiKey: C.apiKey, authDomain: C.authDomain || C.projectId + ".firebaseapp.com", projectId: C.projectId, appId: C.appId});
  auth = firebase.auth(); db = firebase.firestore();
  try { db.enablePersistence({synchronizeTabs: true}).catch(() => {}); } catch (_) {}      // instant start from the phone's cache
  sentryInit();
  window.addEventListener("offline", () => toast("No internet on this phone. Showing the last saved data.", 5000));
  window.addEventListener("online", () => toast("Back online.", 2500));
  document.addEventListener("visibilitychange", () => {                                     // hidden tab: stop live updates (saves data + battery)
    if (!me) return;
    if (document.hidden) { visibleTimer = setTimeout(() => { unsubGlobal(); stopAll(); paused = true; }, 120000); }
    else { clearTimeout(visibleTimer); if (paused) { paused = false; watchGlobal(); show(page); } }
  });
  if ("serviceWorker" in navigator) navigator.serviceWorker.register("sw.js").catch(() => {});
  $("loginForm").onsubmit = async ev => {
    ev.preventDefault(); $("lErr").textContent = ""; $("lGo").disabled = true;
    try { await auth.signInWithEmailAndPassword($("lEmail").value.trim(), $("lPass").value); }
    catch (e) { $("lErr").textContent = /too-many/.test(e.code) ? "Too many attempts. Wait a few minutes." : "Wrong e-mail or password."; }
    $("lGo").disabled = false;
  };
  $("outBtn").onclick = () => auth.signOut();
  auth.onAuthStateChanged(async u => {
    stopAll(); unsubGlobal();
    if (!u) { me = null; $("app").classList.add("hidden"); $("login").classList.remove("hidden"); return; }
    try {
      const m = await db.collection("members").doc(u.email.toLowerCase()).get();
      if (!m.exists || !["admin", "viewer"].includes(m.data().role)) throw new Error("not a member");
      me = u.email.toLowerCase(); role = m.data().role;
    } catch (e) { toast("This account is not on the family list. Ask the owner to add you.", 6000); await auth.signOut(); return; }
    $("login").classList.add("hidden"); $("app").classList.remove("hidden");
    $("who").textContent = me + " · " + role;
    buildTabs(); watchGlobal(); show(location.hash.slice(1) || "home");
  });
}
let globalUnsubs = [], visibleTimer = null, paused = false;
// tiny Sentry reporter (official envelope endpoint, no extra library): only runs if a DSN is configured
function sentryInit() {
  const m = /^https:\/\/([0-9a-f]+)@([\w.-]+)\/(\d+)$/.exec(C.sentryDsn || "");
  if (!m) return;
  let sent = 0;
  const send = (msg, stack) => {
    if (sent++ > 5) return;
    const id = Array.from(crypto.getRandomValues(new Uint8Array(16)), b => b.toString(16).padStart(2, "0")).join("");
    const ev = {event_id: id, timestamp: Date.now() / 1000, platform: "javascript", level: "error", logger: "cloud-website",
      message: {formatted: String(msg).slice(0, 500)}, extra: {stack: String(stack || "").slice(0, 2000), page}, request: {url: location.origin + location.pathname}, tags: {part: "cloud-website"}};
    const body = JSON.stringify({event_id: id, sent_at: new Date().toISOString()}) + "\n" + JSON.stringify({type: "event"}) + "\n" + JSON.stringify(ev);
    try { fetch(`https://${m[2]}/api/${m[3]}/envelope/?sentry_key=${m[1]}&sentry_version=7`, {method: "POST", body, keepalive: true, headers: {"Content-Type": "text/plain;charset=UTF-8"}}); } catch (_) {}
  };
  window.addEventListener("error", e => send(e.message, e.error && e.error.stack));
  window.addEventListener("unhandledrejection", e => send("Unhandled: " + (e.reason && e.reason.message || e.reason), e.reason && e.reason.stack));
}
function unsubGlobal() { globalUnsubs.forEach(f => { try { f(); } catch (_) {} }); globalUnsubs = []; }
function stopAll() { unsubs.forEach(f => { try { f(); } catch (_) {} }); unsubs = []; }
function watchGlobal() {
  globalUnsubs.push(db.collection("devices").doc(DEV).onSnapshot(d => { device = d.exists ? d.data() : null; addConsole(device); pill(); if (["home", "sensors", "health", "board"].includes(page)) render(); }, () => {}));
  globalUnsubs.push(db.collection("events").orderBy("time", "desc").limit(150).onSnapshot(s => { events = s.docs.map(d => Object.assign({id: d.id}, d.data())); if (["home", "sec", "att"].includes(page)) render(); }, e => toast("Events: " + e.code)));
  db.collection("config").doc("site").get().then(d => { cfg = d.exists ? d.data() : {}; }).catch(() => {});
  if (!watchGlobal.timer) watchGlobal.timer = setInterval(pill, 10000);
}
let conLines = [], conTotal = -1, conPaused = false;
function addConsole(d) {
  if (!d || !Array.isArray(d.console)) return;
  const tot = Number(d.console_total || 0);
  if (tot === conTotal) return;
  const fresh = conTotal < 0 || tot < conTotal ? d.console : d.console.slice(Math.max(0, d.console.length - (tot - conTotal)));
  conLines = conLines.concat(fresh.map(t => ({t, at: ts(d.seen) || new Date()}))).slice(-400);
  conTotal = tot;
}
function online() { const s = device && ts(device.seen); return s && Date.now() - s.getTime() < 3 * 60 * 1000; }
function ago() { const s = device && ts(device.seen); if (!s) return "never"; const x = Math.max(0, Math.round((Date.now() - s) / 1000)); return x < 90 ? x + " s ago" : x < 5400 ? Math.round(x / 60) + " min ago" : T(device.seen); }
function pill() { const p = $("pill"); if (!p) return; const on = online(); p.className = "pill " + (on ? "on" : "off"); $("pillText").textContent = (on ? "door online" : (device ? "door offline" : "no data yet")) + (device ? " · " + ago() : ""); }
function buildTabs() {
  $("tabs").innerHTML = TABS.filter(t => !t[2] || isAdmin()).map(([k, l]) => `<button data-p="${k}">${l}</button>`).join("");
  $("tabs").onclick = e => { const b = e.target.closest("button[data-p]"); if (b) show(b.dataset.p); };
}
function show(p) {
  if (!TABS.some(t => t[0] === p && (!t[2] || isAdmin()))) p = "home";
  page = p; stopAll(); try { history.replaceState(null, "", "#" + p); } catch (_) {}
  document.querySelectorAll("#tabs button").forEach(b => b.classList.toggle("on", b.dataset.p === p));
  render();
}
function helpBox() { return HELP[page] ? `<details class="help"><summary>What is this page?</summary><p>${E(HELP[page])}</p></details>` : ""; }
function render(force) { if (page === "board" && conPaused && !force) return; try { (PAGES[page] || PAGES.home)(); const pg = $("page"), h = pg.querySelector("h2.ph"); if (h && !pg.querySelector(".help")) h.parentElement.insertAdjacentHTML("afterend", helpBox()); } catch (e) { $("page").innerHTML = `<div class="banner bad">Problem showing this page: ${E(e.message)}</div>`; } }

// ---------------- pages ----------------
const today = () => { const d = new Date(); d.setHours(0, 0, 0, 0); return d; };
function attendanceFrom(list, from, to) {
  const by = {};
  for (const e of list) {
    const t = ts(e.time); if (!t || t < from || t >= to || !e.person) continue;
    if (!/^entry_(card_face|phone)$/.test(e.type)) continue;
    const r = by[e.person] || (by[e.person] = {person: e.person, first: t, last: t, n: 0, how: new Set()});
    r.n++; if (t < r.first) r.first = t; if (t > r.last) r.last = t; r.how.add(e.type === "entry_phone" ? "phone key" : "card + face");
  }
  return Object.values(by).sort((a, b) => a.first - b.first);
}
const PAGES = {
  board() {
    const o = (device && device.oled) || [], f = $("cFind") ? $("cFind").value : "";
    $("page").innerHTML = `<div><h2 class="ph">Door board</h2><div class="sub">${online() ? '<span class="dot g"></span>online' : '<span class="dot r"></span>offline'} · version ${E(device ? device.version : "–")} · updated ${ago()}</div></div>
      <div class="grid2"><div class="card"><h3>OLED screen (live copy)</h3><div class="oled" aria-label="OLED screen"><div class="obar">CLASSYNC 4.0 <span>P:${E(device ? device.present : 0)}/${E(device ? device.students : 0)}</span></div>
        <div class="obig">${E(o[0] || "–")}</div>${[1, 2, 3, 4].map(i => `<div class="oline">${E(o[i] || "")}</div>`).join("")}</div></div>
      <div class="card"><h3>Board at a glance</h3><table><tbody><tr><td>Wi-Fi signal</td><td>${E(device && device.wifi_rssi ? device.wifi_rssi + " dBm" : "–")}</td></tr>
        <tr><td>Free memory</td><td>${device && device.heap ? Math.round(device.heap / 1024) + " KB" : "–"}</td></tr><tr><td>Running for</td><td>${device && device.uptime ? Math.floor(device.uptime / 3600) + " h " + Math.floor(device.uptime % 3600 / 60) + " min" : "–"}</td></tr>
        <tr><td>Events waiting to upload</td><td>${E(device ? device.queue : "–")}</td></tr><tr><td>Camera</td><td>${device ? (device.camera_ok ? "OK" : "Problem") : "–"}</td></tr></tbody></table></div></div>
      <div class="card"><h3>Serial Monitor (latest lines)</h3><div class="row"><input id="cFind" placeholder="Filter, e.g. ATTENDANCE" value="${E(f)}" style="flex:1;min-width:160px">
        <button class="btn sm" id="cPause">${conPaused ? "Resume" : "Pause"}</button><button class="btn sm" id="cSave">Download</button></div>
        <pre class="console" id="cOut">${conLines.filter(l => !f || l.t.toLowerCase().includes(f.toLowerCase())).slice(-250).map(l => E(l.t)).join("\n") || "Waiting for the board…"}</pre></div>`;
    const out = $("cOut"); out.scrollTop = 1e9;
    $("cFind").oninput = () => { const v = $("cFind").value; render(true); const i = $("cFind"); i.focus(); i.setSelectionRange(v.length, v.length); };
    $("cPause").onclick = () => { conPaused = !conPaused; render(true); };
    $("cSave").onclick = () => { const a = document.createElement("a"); a.href = URL.createObjectURL(new Blob([conLines.map(l => l.t).join("\r\n")], {type: "text/plain"})); a.download = "door_board_serial.txt"; a.click(); };
    if (conPaused) out.classList.add("paused");
  },
  home() {
    const t0 = today(), todayEv = events.filter(e => ts(e.time) >= t0), att = attendanceFrom(events, t0, new Date(t0.getTime() + 864e5));
    const alerts = todayEv.filter(e => e.severity === "alert").length, warns = todayEv.filter(e => e.severity === "warn").length;
    const s = (device && device.sensors) || [], temp = s.find(x => x.type === "temperature" && x.status === "ok") || s.find(x => x.type === "chip_temp" && x.status === "ok");
    $("page").innerHTML = `<div><h2 class="ph">${greet()}</h2><div class="sub">${new Date().toLocaleDateString([], {weekday: "long", day: "numeric", month: "long"})} · ${online() ? '<span class="dot g"></span>door online' : '<span class="dot r"></span>door offline'} · works 24/7, laptop not needed</div></div>
      <div class="kpis">${kpi(att.length + (device ? " / " + device.students : ""), "Present today", "ok")}${kpi(device ? device.present : null, "Checked in on the door")}
      ${kpi(todayEv.filter(e => e.type === "entry_visitor_qr").length, "Visitors today")}${kpi(alerts, "Alerts today", alerts ? "bad" : "")}${kpi(warns, "Warnings today", warns ? "warn" : "")}
      ${kpi(temp ? temp.value.toFixed(1) + "°" : null, temp ? (temp.type === "temperature" ? "Room temperature" : "Board temperature") : "Temperature")}
      ${kpi(s.filter(x => x.status === "ok").length + " / 15", "Sensors working")}${kpi(device ? device.queue : null, "Events waiting to upload")}</div>
      <div class="grid2"><div class="card"><h3>Present today</h3>${att.length ? `<table><tbody>${att.map(r => `<tr><td><b>${E(r.person)}</b></td><td>${r.first.toLocaleTimeString([], {hour: "2-digit", minute: "2-digit"})}</td><td class="muted">${E([...r.how].join(", "))}</td></tr>`).join("")}</tbody></table>` : '<div class="empty">Nobody yet today.</div>'}</div>
      <div class="card"><h3>Latest events</h3>${evTable(events.slice(0, 10))}</div></div>`;
  },
  att() {
    $("page").innerHTML = `<div><h2 class="ph">Attendance</h2><div class="sub">From the door board (card + face or phone key).</div></div>
      <div class="card row"><input type="date" id="aDay" value="${dayKey(new Date(Date.now() - new Date().getTimezoneOffset() * 6e4))}"><button class="btn sm" id="aCsv">Download CSV</button><span class="muted" id="aSum"></span></div>
      <div class="card tw" id="aBody"></div>`;
    const load = async () => {
      const d = new Date($("aDay").value + "T00:00:00"), e = new Date(d.getTime() + 864e5);
      let list = events.filter(x => ts(x.time) >= d && ts(x.time) < e);
      if (!list.length || d < today()) {
        const s = await db.collection("events").where("time", ">=", d).where("time", "<", e).orderBy("time").limit(500).get();
        list = s.docs.map(x => x.data());
      }
      const att = attendanceFrom(list, d, e), vis = list.filter(x => x.type === "entry_visitor_qr");
      $("aSum").textContent = `${att.length} present · ${vis.length} visitor(s)`;
      $("aBody").innerHTML = (att.length ? `<table><thead><tr><th>Person</th><th>First entry</th><th>Last entry</th><th>Entries</th><th>How</th></tr></thead><tbody>${att.map(r => `<tr><td><b>${E(r.person)}</b></td><td>${r.first.toLocaleTimeString()}</td><td>${r.last.toLocaleTimeString()}</td><td>${r.n}</td><td>${E([...r.how].join(", "))}</td></tr>`).join("")}</tbody></table>` : '<div class="empty">No entries on this day.</div>') +
        (vis.length ? `<h3 style="margin-top:16px">Visitors</h3><table><tbody>${vis.map(v => `<tr><td>${E(v.name)}</td><td>${T(v.time)}</td><td class="muted">${E(v.detail)}</td></tr>`).join("")}</tbody></table>` : "");
      $("aCsv").onclick = () => csv(`attendance_${$("aDay").value}.csv`, [["person", "first_entry", "last_entry", "entries", "how"]].concat(att.map(r => [r.person, r.first.toLocaleString(), r.last.toLocaleString(), r.n, [...r.how].join(" / ")])));
    };
    $("aDay").onchange = () => load().catch(e => toast(e.message)); load().catch(e => toast(e.message));
  },
  sensors() {
    const s = (device && device.sensors) || [];
    $("page").innerHTML = `<div><h2 class="ph">Sensors</h2><div class="sub">All 15 slots · live from the door board · history from ThingSpeak</div></div>
      <div class="sensors">${s.length ? s.map((x, i) => `<div class="sensor"><div class="t">${E(x.id)} · ${E(String(x.type).replace(/_/g, " "))}</div><b>${x.status === "ok" ? E(Number(x.value).toFixed(x.unit === "on/off" ? 0 : 1)) : "–"}</b> <span class="muted">${x.status === "ok" ? E(x.unit) : ""}</span><div>${sev(x.status)}</div></div>`).join("") : '<div class="empty">No data from the door board yet.</div>'}</div>
      <div class="grid2" id="charts"></div>`;
    charts(s).catch(() => {});
  },
  control() {
    if (!isAdmin()) return PAGES.home();
    $("page").innerHTML = `<div><h2 class="ph">Control</h2><div class="sub">Works from anywhere. The door board carries it out within about 10 seconds.</div></div>
      <div class="banner ${online() ? "" : "warn"}">${online() ? '<span class="dot g"></span>Door board online.' : "The door board is offline right now: the command will run when it comes back."}</div>
      <div class="ctl"><div class="card"><h3>Door</h3><p>Opens for 3 seconds, like a valid entry.</p><button class="btn primary" data-cmd="opendoor">Open door</button></div>
      <div class="card"><h3>New day</h3><p>Clears today's attendance. Faces and cards stay.</p><button class="btn" data-cmd="reset" data-ask="Start a new day?">Start new day</button></div>
      <div class="card"><h3>Add a card</h3><p>Choose the person, press Start, then tap the new card within 60 s.</p><input id="cWho" placeholder="person, e.g. rozy" maxlength="30"><button class="btn primary" id="cLearn">Start</button></div>
      <div class="card"><h3>Door alerts</h3><p>Board alerts to your phone.</p><div class="row"><button class="btn" data-cmd="alerts on">On</button><button class="btn" data-cmd="alerts off">Off</button><button class="btn" data-cmd="testalert">Test</button></div></div>
      <div class="card"><h3>Restart</h3><p>Safe restart; nothing is lost.</p><button class="btn" data-cmd="restart" data-ask="Restart the door board?">Restart board</button></div></div>
      <div class="card"><h3>Last command</h3><div id="cLast" class="muted">–</div></div>`;
    $("page").onclick = e => { const b = e.target.closest("[data-cmd]"); if (b && (!b.dataset.ask || confirm(b.dataset.ask))) sendCmd(b.dataset.cmd); };
    $("cLearn").onclick = () => { const w = $("cWho").value.trim().toLowerCase().replace(/[^a-z0-9_]/g, ""); if (w.length < 2) return toast("Type the person's name."); sendCmd("learncard " + w); };
    unsubs.push(db.collection("commands").doc(DEV).onSnapshot(d => { const c = d.exists ? d.data() : null;
      $("cLast").innerHTML = c ? `<b>${E(c.cmd)}</b> by ${E(c.by)} · ${T(c.at)} · ${c.done_id === c.id ? sev(c.result === "done" ? "ok" : "alert").replace(/>ok<|>alert</, ">" + E(c.result) + "<") : '<span class="sev info">waiting for the board</span>'}` : "No command yet."; }, () => {}));
  },
  sec() {
    const list = events.filter(e => e.severity === "alert" || e.severity === "warn");
    $("page").innerHTML = `<div><h2 class="ph">Security</h2><div class="sub">Alerts and warnings from the door and the CCTV cross-check.</div></div>
      <div class="card tw">${evTable(list, true)}</div>`;
  },
  chat() {
    $("page").innerHTML = `<div><h2 class="ph">Messages</h2><div class="sub">Family chat, available 24/7 from any phone.</div></div>
      <div class="card chat"><div class="row"><select id="mTo"><option value="all">Everyone</option></select></div><div class="msgs" id="mList"></div>
      <div class="compose"><textarea id="mText" maxlength="1000" placeholder="Message" aria-label="Message"></textarea><button class="btn primary" id="mSend">Send</button></div></div>`;
    const msgs = {}, draw = () => { const to = $("mTo").value, list = Object.values(msgs).filter(m => to === "all" ? m.to === "all" : (m.to !== "all" && (m.from === to || m.to === to)))
      .sort((a, b) => (ts(a.at) || 0) - (ts(b.at) || 0));
      $("mList").innerHTML = list.length ? list.map(m => `<div class="bub ${m.from === me ? "me" : ""}">${m.from === me ? "" : "<b>" + E(m.from.split("@")[0]) + "</b><br>"}${E(m.text)}<small>${T(m.at)}</small></div>`).join("") : '<div class="empty">No messages yet.</div>';
      $("mList").scrollTop = 1e9; };
    const add = s => { s.docChanges().forEach(c => { if (c.type !== "removed") msgs[c.doc.id] = c.doc.data(); }); const people = new Set(); Object.values(msgs).forEach(m => { if (m.from !== me) people.add(m.from); if (m.to !== "all" && m.to !== me) people.add(m.to); });
      const cur = $("mTo").value; $("mTo").innerHTML = '<option value="all">Everyone</option>' + [...people].map(p => `<option value="${E(p)}">${E(p)}</option>`).join(""); $("mTo").value = cur; draw(); };
    const col = db.collection("messages");
    unsubs.push(col.where("to", "==", "all").orderBy("at", "desc").limit(100).onSnapshot(add, e => toast("Messages: " + e.code)));
    unsubs.push(col.where("to", "==", me).orderBy("at", "desc").limit(100).onSnapshot(add, () => {}));
    unsubs.push(col.where("from", "==", me).orderBy("at", "desc").limit(100).onSnapshot(add, () => {}));
    $("mTo").onchange = draw;
    const send = async () => { const t = $("mText").value.trim(); if (!t) return; $("mSend").disabled = true;
      try { await col.add({from: me, to: $("mTo").value, text: t, at: firebase.firestore.FieldValue.serverTimestamp()}); $("mText").value = ""; } catch (e) { toast("Not sent: " + e.code); }
      $("mSend").disabled = false; };
    $("mSend").onclick = send; $("mText").onkeydown = e => { if (e.key === "Enter" && !e.shiftKey) { e.preventDefault(); send(); } };
  },
  health() {
    const d = device || {}, sv = d.services || {};
    const svc = (n, v) => `<tr><td>${n}</td><td>${!v ? '<span class="sev off">not set up</span>' : v === "ok" ? '<span class="sev ok">working</span>' : v === "off" ? '<span class="sev off">off</span>' : /^working/.test(v) ? `<span class="sev warn">${E(v)}</span>` : /memory/.test(v) ? `<span class="sev info">${E(v)}</span>` : `<span class="sev alert">${E(v)}</span>`}</td></tr>`;
    $("page").innerHTML = `<div><h2 class="ph">Health</h2><div class="sub">Every part of SARTHIK Room at a glance.</div></div>
      <div class="kpis">${kpi(online() ? "Online" : "Offline", "Door board", online() ? "ok" : "bad")}${kpi(T(d.seen), "Last heartbeat")}${kpi(d.wifi_rssi ? d.wifi_rssi + " dBm" : null, "Wi-Fi signal")}
      ${kpi(d.heap ? Math.round(d.heap / 1024) + " KB" : null, "Board free memory")}${kpi(d.queue, "Events waiting", d.queue > 20 ? "warn" : "")}${kpi(d.camera_ok == null ? null : d.camera_ok ? "OK" : "Problem", "Door camera", d.camera_ok === false ? "bad" : "")}</div>
      <div class="grid2"><div class="card"><h3>Cloud links (reported by the board)</h3><table><tbody>${svc("Firebase", sv.firebase)}${svc("ThingSpeak", sv.thingspeak)}${svc("Blynk", sv.blynk)}${svc("Telegram", sv.telegram)}${svc("Google Sheet (instant rows + e-mail)", sv.sheet)}</tbody></table></div>
      <div class="card"><h3>Other parts</h3><table><tbody id="hOther"><tr><td colspan="2" class="muted">checking…</td></tr></tbody></table></div></div>
      <div class="card"><h3>Security self-check</h3><p class="muted">Tries things your account must NOT be able to do, against the real database. Every line should say "blocked".</p>
      <button class="btn" id="hSelf">Run security check</button><table><tbody id="hSelfOut"></tbody></table></div>
      <div class="card"><h3>Diagnostics</h3><p class="muted">Copies a short status summary (no passwords or keys) to send when asking for help.</p><button class="btn" id="hDiag">Copy diagnostics</button></div>`;
    Promise.all(["reporter", "laptop"].map(id => db.collection("devices").doc(id).get().then(x => [id, x.exists ? x.data() : null]).catch(() => [id, null]))).then(r => {
      const row = (n, x, note) => { const s = x && ts(x.seen), fresh = s && Date.now() - s < 36 * 3600e3;
        return `<tr><td>${n}</td><td>${x ? (fresh ? '<span class="sev ok">working</span> ' : '<span class="sev warn">quiet</span> ') + T(x.seen) : '<span class="sev off">not set up</span>'} <span class="muted small">${note}</span></td></tr>`; };
      const o = Object.fromEntries(r);
      $("hOther").innerHTML = row("Google Sheet script (reports, e-mail, WhatsApp)", o.reporter, "runs every 5 min") + row("Laptop CCTV", o.laptop, "only while the laptop is on") +
        `<tr><td>n8n (weekly/monthly + watchdog)</td><td>${o.reporter && o.reporter.n8n ? T(o.reporter.n8n) : '<span class="sev off">not run yet</span>'} <span class="muted small">laptop only</span></td></tr>` +
        `<tr><td>Sentry (error alerts)</td><td>${C.sentryDsn ? '<span class="sev ok">on</span>' : '<span class="sev off">not set up</span>'} <span class="muted small">e-mail + Telegram</span></td></tr>` +
        `<tr><td>UptimeRobot (uptime alerts)</td><td><span class="sev info">checks every 5 min</span> <span class="muted small">see uptimerobot.com</span></td></tr>`;
    });
    $("hSelf").onclick = selfCheck;
    $("hDiag").onclick = () => { const x = Object.assign({}, device || {}); delete x.console; const t = JSON.stringify({site: location.host, version: x.version, seen: String(ts(x.seen)), online: online(),
      services: x.services, queue: x.queue, heap: x.heap, wifi: x.wifi_rssi, camera: x.camera_ok, sensors_ok: (x.sensors || []).filter(s => s.status === "ok").length, oled: x.oled}, null, 1);
      (navigator.clipboard ? navigator.clipboard.writeText(t) : Promise.reject()).then(() => toast("Copied."), () => toast("Copy not allowed here.")); };
  },
  members() {
    if (!isAdmin()) return PAGES.home();
    $("page").innerHTML = `<div><h2 class="ph">Members</h2><div class="sub">Who may sign in. Also create the same e-mail in Firebase &rsaquo; Authentication &rsaquo; Add user.</div></div>
      <div class="card row"><input id="nEmail" type="email" placeholder="e-mail" style="flex:1;min-width:200px"><input id="nName" placeholder="name" maxlength="40">
      <select id="nRole"><option value="viewer">Viewer (family)</option><option value="admin">Admin</option><option value="device">Door board</option><option value="reporter">Sheet script</option></select>
      <input id="nWa" placeholder="WhatsApp e.g. 919876543210" maxlength="15"><button class="btn primary" id="nAdd">Add</button></div>
      <div class="card tw" id="mBody"></div>`;
    const load = () => db.collection("members").get().then(s => { $("mBody").innerHTML = `<table><thead><tr><th>E-mail</th><th>Name</th><th>Role</th><th>WhatsApp</th><th></th></tr></thead><tbody>${s.docs.map(d => `<tr><td>${E(d.id)}</td><td>${E(d.data().name)}</td><td>${E(d.data().role)}</td><td>${E(d.data().whatsapp || "")}</td><td>${d.id === me ? '<span class="muted">you</span>' : `<button class="btn sm danger" data-del="${E(d.id)}">Remove</button>`}</td></tr>`).join("")}</tbody></table>`; }).catch(e => toast(e.code));
    $("nAdd").onclick = async () => { const em = $("nEmail").value.trim().toLowerCase(), wa = $("nWa").value.replace(/\D/g, "");
      if (!/^[^@\s]+@[^@\s]+\.[^@\s]+$/.test(em)) return toast("Type a valid e-mail.");
      const doc = {role: $("nRole").value, name: $("nName").value.trim().slice(0, 40) || em.split("@")[0]}; if (wa) doc.whatsapp = wa;
      try { await db.collection("members").doc(em).set(doc); toast("Added. Now create the login in Firebase > Authentication."); load(); } catch (e) { toast("Not added: " + e.code); } };
    $("mBody").onclick = async e => { const b = e.target.closest("[data-del]"); if (b && confirm("Remove " + b.dataset.del + "?")) { try { await db.collection("members").doc(b.dataset.del).delete(); load(); } catch (x) { toast(x.code); } } };
    load();
  }
};
function greet() { const h = new Date().getHours(); return (h < 12 ? "Good morning" : h < 17 ? "Good afternoon" : "Good evening"); }
function evTable(list, full) {
  return list.length ? `<table><tbody>${list.slice(0, full ? 150 : 10).map(e => `<tr><td class="muted" style="white-space:nowrap">${T(e.time)}</td><td>${sev(e.severity || "info")}</td><td>${E(TITLES[e.type] || String(e.type).replace(/_/g, " "))}</td><td><b>${E(e.person || e.name || "")}</b></td>${full ? `<td class="muted">${E(e.detail)}</td><td class="muted">${E(e.source)}</td>` : ""}</tr>`).join("")}</tbody></table>` : '<div class="empty">Nothing yet.</div>';
}
async function sendCmd(cmd) {
  const id = "c" + Date.now().toString(36);
  try { await db.collection("commands").doc(DEV).set({cmd, id, by: me, at: firebase.firestore.FieldValue.serverTimestamp()}); toast("Sent: " + cmd + ". Waiting for the board…"); }
  catch (e) { toast("Not sent: " + e.code); }
}
function csv(name, rows) {
  const t = rows.map(r => r.map(x => `"${String(x).replace(/"/g, '""')}"`).join(",")).join("\r\n"), a = document.createElement("a");
  a.href = URL.createObjectURL(new Blob([t], {type: "text/csv"})); a.download = name; a.click(); setTimeout(() => URL.revokeObjectURL(a.href), 2000);
}
async function charts(s) {
  if (!cfg.ts_channel_a) { $("charts").innerHTML = '<div class="card"><div class="empty">History graphs appear after ThingSpeak is set up (Setup Stage 3.bat).</div></div>'; return; }
  const out = [];
  for (const [ch, key, first] of [[cfg.ts_channel_a, cfg.ts_read_a, 0], [cfg.ts_channel_b, cfg.ts_read_b, 8]]) {
    if (!ch) continue;
    const r = await fetch(`https://api.thingspeak.com/channels/${encodeURIComponent(ch)}/feeds.json?results=120${key ? "&api_key=" + encodeURIComponent(key) : ""}`);
    if (!r.ok) continue;
    const j = await r.json();
    for (let f = 1; f <= 8; f++) {
      const slot = s[first + f - 1]; if (!slot || slot.status === "off") continue;
      const pts = (j.feeds || []).map(x => [new Date(x.created_at), parseFloat(x["field" + f])]).filter(p => !isNaN(p[1]));
      if (pts.length > 1) out.push(`<div class="card"><h3>${E(String(slot.type).replace(/_/g, " "))} (${E(slot.unit)})</h3>${svgLine(pts)}</div>`);
    }
  }
  $("charts").innerHTML = out.join("") || '<div class="card"><div class="empty">No history yet. ThingSpeak fills up every 40 seconds.</div></div>';
}
function svgLine(pts) {
  const W = 600, H = 160, xs = pts.map(p => p[0].getTime()), ys = pts.map(p => p[1]);
  const x0 = Math.min(...xs), x1 = Math.max(...xs), y0 = Math.min(...ys), y1 = Math.max(...ys) || y0 + 1, dy = (y1 - y0) || 1;
  const P = pts.map(p => `${(30 + (p[0] - x0) / ((x1 - x0) || 1) * (W - 40)).toFixed(1)},${(H - 20 - (p[1] - y0) / dy * (H - 40)).toFixed(1)}`).join(" ");
  return `<svg class="chart" viewBox="0 0 ${W} ${H}" preserveAspectRatio="none" role="img" aria-label="history graph"><polyline fill="none" stroke="var(--accent)" stroke-width="2" points="${P}"/>
    <text x="2" y="14">${y1.toFixed(1)}</text><text x="2" y="${H - 22}">${y0.toFixed(1)}</text><text x="30" y="${H - 4}">${new Date(x0).toLocaleTimeString([], {hour: "2-digit", minute: "2-digit"})}</text><text x="${W - 60}" y="${H - 4}">now</text></svg>`;
}
// Runs forbidden actions as YOU against the real database. Each one must be refused.
async function selfCheck() {
  const out = $("hSelfOut"); out.innerHTML = "";
  const tests = [
    ["Change your own role (" + role + " to " + (role === "admin" ? "viewer" : "admin") + ")", () => db.collection("members").doc(me).update({role: role === "admin" ? "viewer" : "admin"})],
    ["Delete a door event", () => events[0] ? db.collection("events").doc(events[0].id).delete() : Promise.reject({code: "permission-denied"})],
    ["Edit a door event", () => events[0] ? db.collection("events").doc(events[0].id).update({detail: "changed"}) : Promise.reject({code: "permission-denied"})],
    ["Create a fake door event", () => db.collection("events").doc("fake_test").set({type: "entry_card_face", device: "door-1", seq: 1, severity: "ok", name: "x", detail: "x", source: "door"})],
    ["Send a message as someone else", () => db.collection("messages").add({from: "someone@else.com", to: "all", text: "x", at: firebase.firestore.FieldValue.serverTimestamp()})],
    ["Read a hidden collection", () => db.collection("private").get()],
  ];
  if (!isAdmin()) tests.push(["Send a door command (viewer)", () => db.collection("commands").doc(DEV).set({cmd: "opendoor", id: "x", by: me})],
                             ["Read the members list (viewer)", () => db.collection("members").get()]);
  let good = 0;
  for (const [name, fn] of tests) {
    let blocked;
    try { await fn(); blocked = false; } catch (e) { blocked = /permission|denied|insufficient/i.test(String(e.code || e.message)); }
    if (blocked) good++;
    out.insertAdjacentHTML("beforeend", `<tr><td>${E(name)}</td><td>${blocked ? '<span class="sev ok">blocked</span>' : '<span class="sev alert">ALLOWED - check firestore.rules</span>'}</td></tr>`);
  }
  out.insertAdjacentHTML("beforeend", `<tr><td><b>Result</b></td><td><b>${good} of ${tests.length} blocked</b></td></tr>`);
}
window.addEventListener("hashchange", () => { if (me) show(location.hash.slice(1)); });
if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", start); else start();
})();
