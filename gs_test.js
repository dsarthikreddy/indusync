const vm = require("vm"), fs = require("fs");
function world(opts = {}) {
  const props = {}, cache = {}, sent = {tg: [], wa: [], mail: []}, sheets = {}, fsdb = {};
  const now = Date.now();
  const ev = (id, mins, type, sev, person, name, detail, source) => fsdb["events/" + id] = {time: new Date(now - mins * 60e3), type, severity: sev, person, name, detail, source: source || "door", device: "door-1"};
  ev("door-1_1", 50, "entry_card_face", "ok", "sarthikreddy", "sarthikreddy", "match 0.8");
  ev("door-1_2", 40, "entry_phone", "ok", "rozy", "rozy", "-48 dBm");
  ev("door-1_3", 30, "fake_pass", "alert", "", "visitor", "signature wrong");
  ev("door-1_4", 20, "entry_visitor_qr", "ok", "", "RAHUL", "pass X");
  fsdb["members/owner@x.com"] = {role: "admin", name: "Owner", whatsapp: "919876543210"};
  fsdb["members/rozy@x.com"] = {role: "viewer", name: "Rozy", whatsapp: "919811111111"};
  fsdb["devices/door-1"] = {seen: new Date(now - 60e3), queue: 0};
  const toFs = v => v instanceof Date ? {timestampValue: v.toISOString()} : typeof v === "number" ? {integerValue: String(v)} : {stringValue: String(v)};
  const fromFsF = f => { const o = {}; for (const k in f) { const x = f[k]; o[k] = "timestampValue" in x ? new Date(x.timestampValue) : "integerValue" in x ? +x.integerValue : "doubleValue" in x ? x.doubleValue : x.stringValue; } return o; };
  const docJ = (p) => ({name: "projects/p/databases/(default)/documents/" + p, fields: Object.fromEntries(Object.entries(fsdb[p]).map(([k, v]) => [k, toFs(v)]))});
  const resp = (code, body) => ({getResponseCode: () => code, getContentText: () => typeof body === "string" ? body : JSON.stringify(body)});
  const UrlFetchApp = { fetch(url, o = {}) {
    if (url.includes("identitytoolkit")) { const b = JSON.parse(o.payload); return b.password === "Rep#Pass123" ? resp(200, {idToken: "TOK"}) : resp(400, {error: {}}); }
    if (url.includes("firestore.googleapis.com")) {
      if (opts.fbDown) return resp(503, "down");
      if (o.headers.Authorization !== "Bearer TOK") return resp(401, "");
      const path = url.split("/documents")[1];
      if (path === ":runQuery") { const q = JSON.parse(o.payload).structuredQuery, f = q.where.compositeFilter.filters;
        const from = new Date(f[0].fieldFilter.value.timestampValue), to = new Date(f[1].fieldFilter.value.timestampValue);
        return resp(200, Object.keys(fsdb).filter(k => k.startsWith("events/") && fsdb[k].time > from && fsdb[k].time < to).sort((a, b) => fsdb[a].time - fsdb[b].time).map(k => ({document: docJ(k)}))); }
      if (path.startsWith("/members?")) return resp(200, {documents: Object.keys(fsdb).filter(k => k.startsWith("members/")).map(docJ)});
      const p = path.slice(1);
      if ((o.method || "get") === "get") return fsdb[p] ? resp(200, docJ(p)) : resp(404, "");
      if (o.method === "patch") { fsdb[p] = fromFsF(JSON.parse(o.payload).fields); return resp(200, {}); }
    }
    if (url.includes("api.telegram.org")) { sent.tg.push(JSON.parse(o.payload).text); return resp(200, {ok: true}); }
    if (url.includes("ingest")) { sent.sentry = (sent.sentry || []).concat([o.payload]); return resp(200, '{}'); }
    if (url.includes("graph.facebook.com")) { const b = JSON.parse(o.payload); sent.wa.push(b); return resp(200, {}); }
    return resp(404, ""); } };
  const sheet = name => { const rows = []; return {appendRow: r => rows.push(r), setFrozenRows() {}, getLastRow: () => rows.length,
    getRange: (r, c, n, m) => ({setValues: v => v.forEach(x => rows.push(x))}), getDataRange: () => ({getValues: () => rows}), rows}; };
  const ctx = {
    console, JSON, Date, Math, Number, String, Object, Array, Error, RegExp,
    PropertiesService: {getScriptProperties: () => ({getProperty: k => props[k] || null, setProperty: (k, v) => { props[k] = v; }})},
    CacheService: {getScriptCache: () => ({get: k => cache[k] || null, put: (k, v) => { cache[k] = v; }})},
    Session: {getScriptTimeZone: () => "Asia/Kolkata"},
    Utilities: {formatDate: (d, tz, f) => f === "yyyy-MM-dd" ? new Date(d.getTime() + 19800e3).toISOString().slice(0, 10) : f === "HH:mm" ? new Date(d.getTime() + 19800e3).toISOString().slice(11, 16) : new Date(d).toISOString(), getUuid: () => "abcd-1234-ef56-7890"},
    UrlFetchApp, MailApp: {sendEmail: m => sent.mail.push(m)},
    GmailApp: {getUserLabelByName: () => null, createLabel: n => ({n}), search: q => (opts.mails || []).filter(m => !m.done).map(m => ({getMessages: () => [{getFrom: () => m.from}], getFirstMessageSubject: () => m.subject, addLabel: () => { m.done = true; }}))},
    LockService: {getScriptLock: () => ({tryLock: () => true, releaseLock() {}})},
    SpreadsheetApp: {getActiveSpreadsheet: () => ({getSheetByName: n => sheets[n], insertSheet: n => (sheets[n] = sheet(n))}), getUi: () => { throw new Error("no ui in test"); }},
    ContentService: {createTextOutput: t => ({setMimeType: () => ({text: t})}), MimeType: {JSON: "json"}},
    ScriptApp: {getProjectTriggers: () => [], deleteTrigger() {}, newTrigger: () => { const b = {timeBased: () => b, everyMinutes: () => b, atHour: () => b, everyDays: () => b, create: () => b}; return b; }},
  };
  vm.createContext(ctx); vm.runInContext(fs.readFileSync("/home/claude/stage3free/sheet/Code.gs", "utf8"), ctx);
  Object.assign(props, {FB_API_KEY: "AIzaFAKE", FB_PROJECT_ID: "p", REPORTER_EMAIL: "sheet@x.com", REPORTER_PASS: "Rep#Pass123", REPORT_EMAIL: "owner@x.com",
    TG_BOT_TOKEN: "1234567:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", TG_CHAT_ID: "987654321", WA_TOKEN: "EAA" + "x".repeat(60), WA_PHONE_ID: "123456789012345", WA_TEMPLATE: "room_update",
    NOTIFY_PEOPLE: "rozy", HOOK_KEY: "SECRETHOOK", CURSOR: new Date(now - 3600e3).toISOString()});
  return {ctx, props, sent, sheets, fsdb};
}
let ok = true, n = 0; const chk = (name, c, x = "") => { n++; console.log(`  ${c ? "PASS" : "FAIL"}  ${name}${c ? "" : "  -> " + JSON.stringify(x).slice(0, 250)}`); ok = ok && !!c; };
// 1) routing
let w = world(); const r = w.ctx.route();
chk("route: 4 new events handled", r === "routed 4", r);
chk("route: Events sheet gets one row per event", w.sheets.Events.rows.length === 5, w.sheets.Events.rows.length);
chk("route: Rozy's arrival -> WhatsApp to the family", w.sent.wa.filter(m => JSON.stringify(m).includes("rozy arrived")).length === 2, w.sent.wa);
chk("route: sarthikreddy NOT announced (not in NOTIFY_PEOPLE)", !w.sent.wa.some(m => JSON.stringify(m).includes("sarthikreddy arrived")));
chk("route: fake pass -> security alert e-mail", w.sent.mail.some(m => /security alert/i.test(m.subject)));
chk("route (safety net): entries also e-mailed", w.sent.mail.some(m => /sarthikreddy entered/.test(m.subject)) && w.sent.mail.some(m => /rozy entered/.test(m.subject)), w.sent.mail.map(m => m.subject));
chk("route: fake pass -> WhatsApp to ADMIN only", w.sent.wa.filter(m => JSON.stringify(m).includes("Security alert")).map(m => m.to).join() === "919876543210", w.sent.wa.map(m => m.to));
chk("route: WhatsApp uses the official template format", w.sent.wa.every(m => m.messaging_product === "whatsapp" && m.type === "template" && m.template.name === "room_update"));
chk("route: Telegram NOT used for door events (the board sends those itself)", w.sent.tg.length === 0, w.sent.tg);
chk("route: heartbeat written for the Health page", w.fsdb["devices/reporter"] && w.fsdb["devices/reporter"].last_route === 4);
const r2 = w.ctx.route(); chk("route again: nothing processed twice", r2 === "routed 0" && w.sent.wa.length === 3, [r2, w.sent.wa.length]);
// 2) daily report
w.ctx.dailyReport();
const d = w.sheets.Daily.rows.slice(1);
chk("daily: 2 people in the Daily sheet", d.length === 2 && d.map(x => x[1]).sort().join() === "rozy,sarthikreddy", d);
chk("daily: report saved in Firebase", Object.keys(w.fsdb).some(k => k.startsWith("reports/")) );
const rep = w.fsdb[Object.keys(w.fsdb).find(k => k.startsWith("reports/"))];
chk("daily: counts 1 visitor + 1 alert", rep.visitors === 1 && rep.alerts === 1, rep);
chk("daily: e-mail + Telegram summary sent", w.sent.mail.some(m => /daily report/.test(m.subject)) && w.sent.tg.some(t => /2 present/.test(t)), w.sent.tg);
// 3) laptop webhook
const good = w.ctx.doPost({parameter: {key: "SECRETHOOK"}, postData: {contents: JSON.stringify({id: 77, ts: Date.now() / 1000, type: "cctv_unknown", title: "UNKNOWN face on CCTV", severity: "alert", source: "cctv", device: "laptop-cam", name: "unknown person", detail: "photo unknown_1.jpg"})}});
chk("webhook: laptop CCTV alert stored in Firebase", JSON.parse(good.text).ok && w.fsdb["events/laptop_77"] && w.fsdb["events/laptop_77"].type === "cctv_unknown");
chk("webhook: unknown face -> Telegram at once", w.sent.tg.some(t => /CCTV ALERT/.test(t)));
chk("webhook: laptop heartbeat for Health", !!w.fsdb["devices/laptop"]);
const bad = w.ctx.doPost({parameter: {key: "WRONG"}, postData: {contents: "{}"}});
chk("webhook: WRONG key refused, nothing stored", JSON.parse(bad.text).ok === false && Object.keys(w.fsdb).filter(k => k.startsWith("events/laptop")).length === 1);
const junk = w.ctx.doPost({parameter: {key: "SECRETHOOK"}, postData: {contents: "not json"}});
chk("webhook: broken data -> polite error, no crash", JSON.parse(junk.text).ok === false);
// 4) n8n endpoints
const h = JSON.parse(w.ctx.doGet({parameter: {key: "SECRETHOOK", action: "health"}}).text);
chk("n8n health: door online, n8n time recorded", h.ok && h.door_online === true && w.fsdb["devices/reporter"].n8n, h);
const wk = JSON.parse(w.ctx.doGet({parameter: {key: "SECRETHOOK", action: "weekly"}}).text);
chk("n8n weekly report from the Daily sheet", wk.ok && /Weekly report: 1 days/.test(wk.text), wk);
chk("n8n with wrong key refused", JSON.parse(w.ctx.doGet({parameter: {key: "nope", action: "weekly"}}).text).ok === false);
// 5) failures
let w2 = world({fbDown: true}); const rf = w2.ctx.route();
chk("Firebase down: no crash, error logged, cursor kept", /error/.test(rf) && w2.sheets.Log.rows.length === 2 && w2.props.CURSOR, rf);
let w3 = world(); w3.props.REPORTER_PASS = "wrong-pass"; const rl = w3.ctx.route();
chk("wrong Sheet login: clear message in Log", /login failed/.test(rl), rl);
let w4 = world(); w4.props.WA_TOKEN = ""; w4.ctx.route();
chk("WhatsApp not set up: everything else still works", w4.sent.wa.length === 0 && w4.sent.mail.some(m => /security alert/i.test(m.subject)));
let w5 = world(); w5.props.WA_TEMPLATE = "hello_world"; w5.ctx.route();
chk("hello_world test template has no parameters (Meta requires that)", w5.sent.wa.every(m => !m.template.components && m.template.language.code === "en_US"));
const nt = JSON.parse(w.ctx.doGet({parameter: {key: "SECRETHOOK", action: "notify", text: "Door board offline for 12 min", important: "1"}}).text);
chk("n8n notify -> Telegram + e-mail", nt.ok && w.sent.tg.some(t => /n8n: Door board offline/.test(t)) && w.sent.mail.some(m => /offline/.test(m.subject)), nt);
let w6 = world(); for (const k of Object.keys(w6.props)) delete w6.props[k];
w6.ctx.Utilities.base64Decode = b => Buffer.from(b, "base64"); w6.ctx.Utilities.newBlob = b => ({getDataAsString: () => Buffer.from(b).toString("utf8")});
const code = Buffer.from(JSON.stringify({FB_API_KEY: "AIza" + "B".repeat(35), FB_PROJECT_ID: "sarthik-room-1", REPORTER_EMAIL: "sheet.reporter@sarthik.room", REPORTER_PASS: "Gen-Pass_123456789",
  REPORT_EMAIL: "me@gmail.com", TG_BOT_TOKEN: "", TG_CHAT_ID: "", WA_TOKEN: "", WA_PHONE_ID: "", WA_TEMPLATE: "room_update", NOTIFY_PEOPLE: "rozy", HOOK_KEY: "a1b2c3d4e5f6a7b8c9d0e1f2"})).toString("base64");
const imp = w6.ctx.importSettings(code);
chk("one settings code saves all settings", imp.ok && w6.props.FB_PROJECT_ID === "sarthik-room-1" && w6.props.HOOK_KEY === "a1b2c3d4e5f6a7b8c9d0e1f2", imp);
chk("wrong / damaged code refused politely", w6.ctx.importSettings("hello").ok === false && w6.ctx.importSettings(Buffer.from(JSON.stringify({FB_API_KEY: "bad"})).toString("base64")).ok === false);
let w7 = world(); const tgBefore = w7.sent.tg.length;
const dup = JSON.parse(w7.ctx.doPost({parameter: {key: "SECRETHOOK"}, postData: {contents: JSON.stringify({id: 90, ts: Date.now() / 1000, type: "fake_pass", severity: "alert", source: "door", device: "door-1", name: "visitor"})}}).text);
chk("door alert copied by the laptop: ignored (no duplicate, no 2nd Telegram)", dup.ok && dup.skipped && !w7.fsdb["events/laptop_90"] && w7.sent.tg.length === tgBefore);
let w8 = world();
const up = JSON.parse(w8.ctx.doGet({parameter: {action: "uptime"}}).text);
chk("public uptime check: no key needed, shows ONLY online/offline", up.ok && up.door_online === true && Object.keys(up).length === 2, up);
w8.fsdb["devices/door-1"].seen = new Date(Date.now() - 20 * 60e3);
const wd1 = JSON.parse(w8.ctx.doGet({parameter: {key: "SECRETHOOK", action: "watchdog"}}).text);
const wd2 = JSON.parse(w8.ctx.doGet({parameter: {key: "SECRETHOOK", action: "watchdog"}}).text);
w8.fsdb["devices/door-1"].seen = new Date();
const wd3 = JSON.parse(w8.ctx.doGet({parameter: {key: "SECRETHOOK", action: "watchdog"}}).text);
chk("watchdog: offline told ONCE, then 'back online' once", wd1.sent === "offline" && wd2.sent === "" && wd3.sent === "back online", [wd1, wd2, wd3]);
const rp1 = JSON.parse(w8.ctx.doGet({parameter: {key: "SECRETHOOK", action: "reports"}}).text), rp2 = JSON.parse(w8.ctx.doGet({parameter: {key: "SECRETHOOK", action: "reports"}}).text);
chk("reports: weekly + monthly sent once, not repeated an hour later", rp1.sent.length === 2 && rp2.sent.length === 0, [rp1, rp2]);
w8.props.LAST_WEEKLY = new Date(Date.now() - 9 * 864e5).toISOString();
const rp3 = JSON.parse(w8.ctx.doGet({parameter: {key: "SECRETHOOK", action: "reports"}}).text);
chk("reports: laptop off 9 days -> missed weekly sent (catch-up)", rp3.sent.length === 1 && /Weekly/.test(rp3.sent[0]), rp3);
w8.props.SENTRY_DSN = "https://0123456789abcdef0123456789abcdef@o12345.ingest.sentry.io/4501234";
w8.props.REPORTER_PASS = "wrong"; w8.ctx.CacheService.getScriptCache().put("fbtok", "", 1);
w8.ctx.route();
chk("Sheet errors reported to Sentry (envelope format)", (w8.sent.sentry || []).length >= 1 && /"type":"event"/.test(w8.sent.sentry[0]), w8.sent.sentry);
let w9 = world(); const dr = w9.ctx.dailyReport({triggerUid: "1", 'time-driven': 1, year: 2026});
chk("daily report started by the timer (event object) works", /present/.test(dr) && !/error/.test(dr) && w9.sheets.Daily.rows.length >= 2, dr);
let wa = world(); const rows0 = (wa.sheets.Events || {rows: []}).rows.length;
const post = ev => JSON.parse(wa.ctx.doPost({parameter: {key: "SECRETHOOK"}, postData: {contents: JSON.stringify(ev)}}).text);
const b1 = post({via: "board", id: "door-1_1", seq: 1, ts: Date.now() / 1000 - 3000, type: "entry_card_face", severity: "ok", person: "sarthikreddy", name: "sarthikreddy", detail: "match 0.8", device: "door-1", source: "door"});
chk("board entry -> Sheet row at once", b1.ok && wa.sheets.Events.rows.some(r => r[2] === "entry_card_face" && r[4] === "sarthikreddy"), wa.sheets.Events.rows);
chk("board entry -> e-mail at once", wa.sent.mail.some(m => /sarthikreddy entered/.test(m.subject)), wa.sent.mail.map(m => m.subject));
chk("board entry -> NO second Telegram from the Sheet (the board already sent it)", wa.sent.tg.length === 0, wa.sent.tg);
chk("same board event sent twice (retry) -> handled once", post({via: "board", id: "door-1_1", type: "entry_card_face", severity: "ok"}).dup === true);
const evRows = wa.sheets.Events.rows.length, mails = wa.sent.mail.length;
wa.ctx.route();
chk("5-minute route afterwards: that entry NOT added or e-mailed again", wa.sheets.Events.rows.filter(r => r[2] === "entry_card_face" && r[4] === "sarthikreddy").length === 1, wa.sheets.Events.rows.length);
chk("route still handles events the board could not send instantly", wa.sheets.Events.rows.some(r => r[2] === "fake_pass") && wa.sheets.Events.rows.some(r => r[4] === "rozy"));
const lp = ev => post(Object.assign({id: Math.floor(Math.random() * 1e6), ts: Date.now() / 1000, device: "laptop-cam", source: "cctv"}, ev));
const tg0 = wa.sent.tg.length;
lp({type: "cctv_seen", severity: "info", name: "goutam"}); lp({type: "cctv_seen", severity: "info", name: "goutam"});
chk("laptop face recognised -> Telegram once per 30 min (no spam)", wa.sent.tg.filter(t => /CCTV recognised goutam/.test(t)).length === 1, wa.sent.tg.slice(tg0));
lp({type: "cross_verified", severity: "ok", name: "goutam"});
chk("laptop confirms the door entry -> Telegram + e-mail", wa.sent.tg.some(t => /CCTV confirmed the door entry of goutam/.test(t)) && wa.sent.mail.some(m => /CCTV confirmed/.test(m.subject)));
const skip = lp({type: "cctv_left", severity: "info", name: "goutam"});
chk("unimportant laptop events (left the camera view) dropped", skip.skipped === "not needed");
wa.props.MAIL_D = wa.ctx.Utilities.formatDate(new Date(), "", "yyyy-MM-dd"); wa.props.MAIL_N = "70"; const m0 = wa.sent.mail.length;
post({via: "board", id: "door-1_99", ts: Date.now() / 1000, type: "entry_phone", severity: "ok", person: "goutam", name: "goutam"});
post({via: "board", id: "door-1_100", ts: Date.now() / 1000, type: "fake_pass", severity: "alert", name: "visitor"});
chk("Google's daily e-mail limit respected: entries pause at 70, alerts still go", wa.sent.mail.length === m0 + 1 && /security alert/.test(wa.sent.mail[wa.sent.mail.length - 1].subject), wa.sent.mail.slice(m0).map(m => m.subject));
console.log(`\nSHEET SCRIPT: ${n} checks, ${ok ? "ALL PASS" : "SOMETHING FAILED"}`);
