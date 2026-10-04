const {JSDOM, VirtualConsole} = require("jsdom"); const fs = require("fs");
const PUB = "/home/claude/stage3free/firebase/public/";
const TS = d => ({toDate: () => d});
function makeFirebase(role, store) {
  const listeners = [];
  const fire = () => listeners.forEach(l => l());
  const docRef = (col, id) => ({ id,
    get: async () => { if (col === "private") throw {code: "permission-denied"}; const v = store[col] && store[col][id]; return {exists: !!v, id, data: () => v}; },
    set: async v => { if (col === "members" && id === store.me) throw {code: "permission-denied"}; if (col === "events") throw {code: "permission-denied"};
      if (col === "commands" && role !== "admin") throw {code: "permission-denied"}; (store[col] = store[col] || {})[id] = v; fire(); },
    update: async () => { throw {code: "permission-denied"}; }, delete: async () => { if (col !== "members") throw {code: "permission-denied"}; delete store[col][id]; fire(); },
    onSnapshot: (ok) => { const f = () => { const v = store[col] && store[col][id]; ok({exists: !!v, data: () => v}); }; listeners.push(f); f(); return () => {}; } });
  const query = (col, filters = []) => ({
    where: (f, op, v) => query(col, filters.concat([[f, op, v]])), orderBy: () => query(col, filters), limit: () => query(col, filters),
    _docs: () => Object.entries(store[col] || {}).filter(([id, d]) => filters.every(([f, op, v]) => op === "==" ? d[f] === v : op === ">=" ? d[f].toDate() >= v : d[f].toDate() < v)).map(([id, d]) => ({id, data: () => d})),
    get: async function () { if (col === "private" || (col === "members" && role !== "admin")) throw {code: "permission-denied"}; return {docs: this._docs()}; },
    onSnapshot: function (ok) { const f = () => { const docs = this._docs(); ok({docs, docChanges: () => docs.map(d => ({type: "added", doc: d}))}); }; listeners.push(f); f(); return () => {}; },
    add: async v => { if (col === "messages" && v.from !== store.me) throw {code: "permission-denied"}; const id = "m" + Math.random(); (store[col] = store[col] || {})[id] = Object.assign({}, v, {at: TS(new Date())}); fire(); return {id}; },
    doc: id => docRef(col, id) });
  let authCb = null, user = null;
  const fb = { initializeApp() {}, apps: [],
    auth: () => ({ signInWithEmailAndPassword: async (e, p) => { if (p !== "Good#Pass123") throw {code: "auth/wrong-password"}; user = {email: e}; authCb(user); },
                   onAuthStateChanged: cb => { authCb = cb; cb(null); }, signOut: async () => { user = null; authCb(null); } }),
    firestore: () => ({ collection: c => query(c) }) };
  fb.firestore.FieldValue = {serverTimestamp: () => TS(new Date())};
  return fb;
}
async function run(role) {
  const now = new Date(), me = role + "@family.com";
  const store = { me,
    members: {[me]: {role, name: role}, "rozy@family.com": {role: "viewer", name: "Rozy"}},
    devices: {"door-1": {seen: TS(now), version: "8.2.1", uptime: 7300, oled: ["PRESENT", "sarthikreddy", "Attendance marked", "Match score: 0.81", "Door OPEN - go in"], console: ["Card tapped: [4E 09 5E F8]", ">>> ATTENDANCE MARKED: sarthikreddy (present today: 1/3)", "<script>window.__xss=1</script>"], console_total: 57, present: 2, students: 3, visitors: 1, wifi_rssi: -55, heap: 150000, queue: 0, camera_ok: true,
      services: {firebase: "ok", thingspeak: "ok", blynk: "working, but add in Blynk: V3", telegram: "chat not found (send /start)", sheet: "ok"},
      sensors: Array.from({length: 15}, (_, i) => ({id: "S" + String(i + 1).padStart(2, "0"), type: ["chip_temp", "occupancy", "wifi_signal", "free_memory", "temperature"][i] || "slot" + i, unit: "C",
        status: i < 5 ? "ok" : i === 6 ? "fault" : "off", value: 20 + i}))},
      reporter: {seen: TS(now), n8n: TS(now)}},
    events: {
      "door-1_1": {time: TS(new Date(now - 3600e3)), type: "entry_card_face", severity: "ok", person: "sarthikreddy", name: "sarthikreddy", detail: "match 0.82", source: "door"},
      "door-1_2": {time: TS(new Date(now - 1800e3)), type: "fake_pass", severity: "alert", person: "", name: "<img src=x onerror=window.__xss=1>", detail: "<script>window.__xss=1</script>", source: "door"},
      "door-1_3": {time: TS(new Date(now - 600e3)), type: "entry_phone", severity: "ok", person: "rozy", name: "rozy", detail: "-50 dBm", source: "door"}},
    config: {site: {ts_channel_a: "123", ts_read_a: "RKEY"}},
    messages: {m1: {from: "rozy@family.com", to: "all", text: "<b>hi</b> from mom", at: TS(now)}} };
  const errors = [], vc = new VirtualConsole();
  vc.on("jsdomError", e => { if (!/navigation|scrollTo|test crash/i.test(e.message)) errors.push(e.message); });
  vc.on("error", (...a) => errors.push(a.join(" ")));
  const html = fs.readFileSync(PUB + "index.html", "utf8").replace(/<script src="https:[^"]+"><\/script>/g, "").replace('<script src="config.js"></script>', "").replace('<script src="app.js" defer></script>', "");
  const dom = new JSDOM(html, {runScripts: "outside-only", pretendToBeVisual: true, virtualConsole: vc, url: "https://sarthik-room.web.app/"});
  const w = dom.window, d = w.document, sleep = ms => new Promise(r => setTimeout(r, ms)), res = [];
  w.firebase = makeFirebase(role, store); w.SR_CONFIG = {apiKey: "AIzaX", projectId: "p", appId: "a", device: "door-1", sentryDsn: "https://0123456789abcdef0123456789abcdef@o1.ingest.us.sentry.io/42"};
  w.matchMedia = () => ({matches: false}); w.confirm = () => true; w.HTMLElement.prototype.scrollIntoView = () => {};
  w.__sentry = []; w.fetch = async (u, o) => { if (String(u).includes("ingest")) { w.__sentry.push(o.body); return {ok: true}; } return {ok: true, json: async () => ({feeds: Array.from({length: 30}, (_, i) => ({created_at: new Date(now - (30 - i) * 40e3).toISOString(), field1: String(40 + Math.sin(i)), field5: String(28 + i / 10)}))})}; };
  w.URL.createObjectURL = () => "blob:x"; w.addEventListener("error", e => { if (!/test crash/.test(e.message)) errors.push("window: " + e.message); });
  w.eval(fs.readFileSync(PUB + "app.js", "utf8"));
  const ok = (n, c, x = "") => { res.push(!!c); console.log(`  ${c ? "PASS" : "FAIL"}  [${role}] ${n}${c ? "" : "  -> " + x}`); };
  await sleep(100);
  ok("login screen shown first", !d.getElementById("login").classList.contains("hidden"));
  d.getElementById("lEmail").value = me; d.getElementById("lPass").value = "wrong";
  d.getElementById("loginForm").dispatchEvent(new w.Event("submit")); await sleep(100);
  ok("wrong password -> friendly message", /Wrong e-mail or password/.test(d.getElementById("lErr").textContent));
  d.getElementById("lPass").value = "Good#Pass123"; d.getElementById("loginForm").dispatchEvent(new w.Event("submit")); await sleep(200);
  ok("signed in", !d.getElementById("app").classList.contains("hidden"));
  const tabs = [...d.querySelectorAll("#tabs button")].map(b => b.dataset.p);
  ok("admin pages only for admins", role === "admin" ? tabs.includes("control") && tabs.includes("members") : !tabs.includes("control") && !tabs.includes("members"), tabs.join(","));
  ok("door online pill", /door online/.test(d.getElementById("pillText").textContent));
  for (const t of tabs) { d.querySelector(`#tabs button[data-p="${t}"]`).click(); await sleep(150);
    ok(`page ${t} opens with content`, d.getElementById("page").textContent.trim().length > 30 && !/Problem showing/.test(d.getElementById("page").textContent), d.getElementById("page").textContent.slice(0, 80)); }
  d.querySelector('#tabs button[data-p="home"]').click(); await sleep(100);
  ok("home: present today lists both people", /sarthikreddy/.test(d.getElementById("page").textContent) && /rozy/.test(d.getElementById("page").textContent));
  d.querySelector('#tabs button[data-p="sec"]').click(); await sleep(100);
  ok("XSS in event name/detail shown as text, never runs", w.__xss === undefined && !d.querySelector("#page img") && /onerror/.test(d.getElementById("page").textContent));
  d.querySelector('#tabs button[data-p="sensors"]').click(); await sleep(300);
  ok("15 sensor tiles + fault/off states", d.querySelectorAll(".sensor").length === 15 && /fault/.test(d.getElementById("page").textContent));
  ok("history graphs drawn from ThingSpeak", d.querySelectorAll("svg.chart polyline").length >= 1, d.getElementById("charts").textContent.slice(0, 80));
  d.querySelector('#tabs button[data-p="chat"]').click(); await sleep(150);
  ok("chat shows mom's message as text", /<b>hi<\/b> from mom/.test(d.getElementById("mList").textContent) && !d.querySelector("#mList b + b"));
  d.getElementById("mText").value = "Dinner at 8"; d.getElementById("mSend").click(); await sleep(150);
  ok("send chat message", Object.values(store.messages).some(m => m.text === "Dinner at 8" && m.from === me));
  d.querySelector('#tabs button[data-p="health"]').click(); await sleep(200);
  ok("health shows the Telegram problem in plain words", /chat not found/.test(d.getElementById("page").textContent));
  ok("health shows the instant Sheet link + Blynk hint in orange", /Google Sheet \(instant/.test(d.getElementById("page").textContent) && !!d.querySelector(".sev.warn"));
  d.getElementById("hSelf").click(); await sleep(400);
  const selfTxt = d.getElementById("hSelfOut").textContent;
  ok("security self-check: everything blocked", /(\d+) of \1 blocked/.test(selfTxt) && !/ALLOWED/.test(selfTxt), selfTxt.slice(-80));
  if (role === "admin") {
    d.querySelector('#tabs button[data-p="control"]').click(); await sleep(150);
    d.getElementById("cWho").value = "Rozy"; d.getElementById("cLearn").click(); await sleep(150);
    ok("control: Add card for rozy -> command written for the board", store.commands && store.commands["door-1"].cmd === "learncard rozy" && store.commands["door-1"].by === me);
    d.querySelector('[data-cmd="opendoor"]').click(); await sleep(150);
    ok("control: Open door command", store.commands["door-1"].cmd === "opendoor");
    store.commands["door-1"].done_id = store.commands["door-1"].id; store.commands["door-1"].result = "done"; store.devices["door-1"].seen = TS(new Date()); w.document.dispatchEvent(new w.Event("x"));
    d.querySelector('#tabs button[data-p="control"]').click(); await sleep(150);
    ok("control: shows the board finished it", /done/.test(d.getElementById("cLast").textContent), d.getElementById("cLast").textContent);
    d.querySelector('#tabs button[data-p="members"]').click(); await sleep(150);
    d.getElementById("nEmail").value = "Mom@Family.com"; d.getElementById("nName").value = "Mom"; d.getElementById("nWa").value = "+91 98765 43210"; d.getElementById("nAdd").click(); await sleep(150);
    ok("members: add mom (e-mail lower-cased, WhatsApp digits only)", store.members["mom@family.com"] && store.members["mom@family.com"].whatsapp === "919876543210");
  }
  d.querySelector('#tabs button[data-p="board"]').click(); await sleep(150);
  const bt = d.getElementById("page").textContent;
  ok("Board page: OLED mirror shows the door screen", d.querySelector(".oled .obig").textContent === "PRESENT" && /Attendance marked/.test(d.querySelector(".oled").textContent));
  ok("Board page: Serial Monitor lines, script text shown as text", /ATTENDANCE MARKED: sarthikreddy/.test(d.getElementById("cOut").textContent) && w.__xss === undefined && /<script>/.test(d.getElementById("cOut").textContent));
  d.getElementById("cFind").value = "ATTENDANCE"; d.getElementById("cFind").dispatchEvent(new w.Event("input")); await sleep(50);
  ok("Board page: filter works", d.getElementById("cOut").textContent.trim().split("\n").length === 1, d.getElementById("cOut").textContent);
  ok("every page has a beginner explanation", !!d.querySelector("details.help"));
  ok("header shows how fresh the data is", /s ago|min ago/.test(d.getElementById("pillText").textContent), d.getElementById("pillText").textContent);
  w.dispatchEvent(new w.ErrorEvent("error", {message: "test crash", error: new Error("test crash")})); await sleep(50);
  ok("website errors are sent to Sentry (envelope)", w.__sentry.length === 1 && /"type":"event"/.test(w.__sentry[0]) && /test crash/.test(w.__sentry[0]));
  d.getElementById("outBtn").click(); await sleep(100);
  ok("sign out returns to login", !d.getElementById("login").classList.contains("hidden"));
  console.log(`  JS errors: ${errors.length ? errors.join(" | ") : "none"}`);
  return {pass: res.filter(Boolean).length, total: res.length, errors: errors.length};
}
(async () => { const a = await run("admin"), v = await run("viewer");
  console.log(`\nCLOUD WEBSITE: ${a.pass + v.pass}/${a.total + v.total} pass, JS errors ${a.errors + v.errors}`); process.exit(0); })();
