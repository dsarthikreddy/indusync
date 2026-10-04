/**
 * SARTHIK Room - Google Sheet script (free, runs 24/7 inside Google, no laptop, no card).
 * Jobs:  every 5 min  -> new events into the "Events" sheet, security e-mail, WhatsApp notices
 *        every day 8 PM -> attendance report: "Daily" sheet + e-mail + WhatsApp + Telegram summary
 *        web address  -> receives laptop CCTV alerts (doPost) and n8n requests (doGet)
 * Setup: menu "SARTHIK Room" > "1. Enter settings", then "2. Start automatic jobs". Keys are kept in
 *        Script Properties (hidden), never in this code.
 */
var KEYS = [
  ['FB_API_KEY', 'Firebase Web API Key (starts with AIza)', /^AIza[\w-]{30,}$/],
  ['FB_PROJECT_ID', 'Firebase Project ID (e.g. sarthik-room-1234)', /^[a-z0-9-]{4,40}$/],
  ['REPORTER_EMAIL', 'E-mail of the "Sheet script" login you created in Firebase', /^[^@\s]+@[^@\s]+\.[^@\s]+$/],
  ['REPORTER_PASS', 'Password of that login', /^.{8,}$/],
  ['REPORT_EMAIL', 'Where reports go (your e-mail; several: comma separated)', /^[^@\s]+@[^@\s]+(,\s*[^@\s]+@[^@\s]+)*$/],
  ['TG_BOT_TOKEN', 'Telegram bot token (optional, from @BotFather)', /^(|\d{6,12}:[\w-]{30,})$/],
  ['TG_CHAT_ID', 'Telegram chat number (optional)', /^(|-?\d{5,15})$/],
  ['WA_TOKEN', 'WhatsApp permanent token (optional, starts with EAA)', /^(|EA[\w]{50,})$/],
  ['WA_PHONE_ID', 'WhatsApp Phone number ID (optional, 15-16 digits)', /^(|\d{10,20})$/],
  ['WA_TEMPLATE', 'WhatsApp template name (room_update, or hello_world for the first test)', /^[a-z0-9_]{3,60}$/],
  ['NOTIFY_PEOPLE', 'People whose arrival goes to WhatsApp (e.g. rozy,goutam; empty = nobody)', /^[a-z0-9_,\s]*$/],
  ['SENTRY_DSN', 'Sentry DSN (optional, https://...@...ingest...sentry.io/123)', /^(|https:\/\/[0-9a-f]{16,64}@[\w.-]+\/\d+)$/]
];
var DEVICE = 'door-1';

function P() { return PropertiesService.getScriptProperties(); }
function cfg(k) { return P().getProperty(k) || ''; }
function tz() { return Session.getScriptTimeZone() || 'Asia/Kolkata'; }
function fmt(d, f) { return Utilities.formatDate(d, tz(), f || 'dd MMM yyyy HH:mm'); }

// ---------------- menu + setup ----------------
function onOpen() {
  SpreadsheetApp.getUi().createMenu('SARTHIK Room')
    .addItem('1. Enter settings', 'setupSettings')
    .addItem('2. Start automatic jobs', 'setupJobs')
    .addItem('3. Test everything now', 'testAll')
    .addItem('Send today\'s report now', 'dailyReport')
    .addToUi();
}
function setupSettings() {
  var ui = SpreadsheetApp.getUi();
  var q = ui.prompt('SARTHIK Room settings', 'Paste the SETTINGS CODE shown by "Setup Stage 3" on your laptop (it is already copied: press Ctrl+V).\n' +
                    'Or press Cancel to type the settings one by one.', ui.ButtonSet.OK_CANCEL);
  if (q.getSelectedButton() === ui.Button.OK && q.getResponseText().trim()) {
    var res = importSettings(q.getResponseText().trim());
    ui.alert(res.ok ? 'Saved ' + res.saved + ' settings. Next: menu SARTHIK Room > 2. Start automatic jobs.' : 'That code did not work: ' + res.error + '\nCopy it again from the laptop window.');
    return;
  }
  for (var i = 0; i < KEYS.length; i++) {
    var k = KEYS[i], cur = cfg(k[0]);
    var shown = !cur ? '(empty)' : (/PASS|TOKEN|KEY/.test(k[0]) ? '(saved - leave empty to keep)' : cur);
    var r = ui.prompt('Setting ' + (i + 1) + ' of ' + KEYS.length, k[1] + '\nNow: ' + shown, ui.ButtonSet.OK_CANCEL);
    if (r.getSelectedButton() !== ui.Button.OK) return;
    var v = r.getResponseText().trim();
    if (!v && cur) continue;
    if (k[0] === 'WA_TEMPLATE' && !v) v = 'room_update';
    if (!k[2].test(v)) { ui.alert('That does not look right for: ' + k[1] + '\nPlease run "1. Enter settings" again.'); return; }
    P().setProperty(k[0], v);
  }
  if (!cfg('HOOK_KEY')) P().setProperty('HOOK_KEY', Utilities.getUuid().replace(/-/g, ''));
  ui.alert('Saved. Next: menu SARTHIK Room > 2. Start automatic jobs.');
}
// one code from the laptop setup program = all settings at once (no typing mistakes)
function importSettings(code) {
  try {
    var o = JSON.parse(Utilities.newBlob(Utilities.base64Decode(code.replace(/\s+/g, ''))).getDataAsString());
    var saved = 0, ok = {};
    KEYS.forEach(function (k) { ok[k[0]] = k[2]; });
    ok.HOOK_KEY = /^[A-Za-z0-9]{16,64}$/;
    for (var key in o) {
      if (!ok[key]) continue;
      var v = String(o[key] == null ? '' : o[key]).trim();
      if (!ok[key].test(v)) return {ok: false, error: key + ' looks wrong'};
      P().setProperty(key, v); saved++;
    }
    if (!cfg('HOOK_KEY')) P().setProperty('HOOK_KEY', Utilities.getUuid().replace(/-/g, ''));
    return {ok: saved > 0, saved: saved, error: saved ? '' : 'empty code'};
  } catch (e) { return {ok: false, error: 'not a settings code'}; }
}
function setupJobs() {
  ScriptApp.getProjectTriggers().forEach(function (t) { ScriptApp.deleteTrigger(t); });
  ScriptApp.newTrigger('route').timeBased().everyMinutes(1).create();       // safety net: at most 1 minute late
  ScriptApp.newTrigger('dailyReport').timeBased().atHour(20).everyDays(1).create();
  sheets_();
  var msg = 'Automatic jobs are ON (every 1 minute, and a report every day at 8 PM).';
  try { SpreadsheetApp.getUi().alert(msg); } catch (e) {}
  return msg;
}
function sheets_() {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  [['Events', ['Time', 'Device', 'Type', 'Severity', 'Person', 'Name', 'Detail', 'Source']],
   ['Daily', ['Date', 'Person', 'First entry', 'Last entry', 'Entries', 'How']],
   ['Summary', ['Period', 'Days', 'People present (avg)', 'Visitors', 'Alerts', 'Warnings', 'Made at']],
   ['Log', ['Time', 'What', 'Result']]].forEach(function (s) {
    var sh = ss.getSheetByName(s[0]);
    if (!sh) { sh = ss.insertSheet(s[0]); sh.appendRow(s[1]); sh.setFrozenRows(1); }
  });
  return ss;
}
function log_(what, result) {
  try { sheets_().getSheetByName('Log').appendRow([new Date(), what, String(result).slice(0, 300)]); } catch (e) {}
  if (/error|fail|refus|-> [45]\d\d/i.test(String(result))) sentry_(what + ': ' + result);
}
// Sentry error report without any library (official envelope endpoint). Only runs if SENTRY_DSN is set.
function sentry_(message) {
  var m = /^https:\/\/([0-9a-f]+)@([\w.-]+)\/(\d+)$/.exec(cfg('SENTRY_DSN'));
  if (!m) return 'off';
  try {
    var id = Utilities.getUuid().replace(/-/g, '');
    var ev = {event_id: id, timestamp: Date.now() / 1000, platform: 'javascript', level: 'error', logger: 'google-sheet',
              server_name: 'SARTHIK Room Sheet', message: {formatted: String(message).slice(0, 500)}, tags: {part: 'google-sheet'}};
    var body = JSON.stringify({event_id: id, sent_at: new Date().toISOString()}) + '\n' + JSON.stringify({type: 'event'}) + '\n' + JSON.stringify(ev);
    var r = UrlFetchApp.fetch('https://' + m[2] + '/api/' + m[3] + '/envelope/?sentry_key=' + m[1] + '&sentry_version=7',
      {method: 'post', contentType: 'text/plain;charset=UTF-8', payload: body, muteHttpExceptions: true});
    return r.getResponseCode() === 200 ? 'ok' : 'error ' + r.getResponseCode();
  } catch (e) { return 'error'; }
}

// ---------------- Firebase (REST) ----------------
function token_() {
  var c = CacheService.getScriptCache(), t = c.get('fbtok');
  if (t) return t;
  var r = UrlFetchApp.fetch('https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=' + cfg('FB_API_KEY'),
    {method: 'post', contentType: 'application/json', muteHttpExceptions: true,
     payload: JSON.stringify({email: cfg('REPORTER_EMAIL'), password: cfg('REPORTER_PASS'), returnSecureToken: true})});
  if (r.getResponseCode() !== 200) throw new Error('Firebase login failed (check REPORTER_EMAIL / REPORTER_PASS and Members role "reporter")');
  t = JSON.parse(r.getContentText()).idToken;
  c.put('fbtok', t, 3000);
  return t;
}
function fsBase_() { return 'https://firestore.googleapis.com/v1/projects/' + cfg('FB_PROJECT_ID') + '/databases/(default)/documents'; }
function fsCall_(method, path, body) {
  var o = {method: method, muteHttpExceptions: true, headers: {Authorization: 'Bearer ' + token_()}};
  if (body) { o.contentType = 'application/json'; o.payload = JSON.stringify(body); }
  var r = UrlFetchApp.fetch(fsBase_() + path, o), code = r.getResponseCode();
  if (code === 404) return null;
  if (code !== 200) throw new Error('Firebase ' + method + ' ' + path + ' -> ' + code);
  return JSON.parse(r.getContentText() || '{}');
}
function toFs_(v) {
  if (v === null || v === undefined) return {nullValue: null};
  if (v instanceof Date) return {timestampValue: v.toISOString()};
  if (typeof v === 'boolean') return {booleanValue: v};
  if (typeof v === 'number') return Number.isInteger(v) ? {integerValue: String(v)} : {doubleValue: v};
  if (Array.isArray(v)) return {arrayValue: {values: v.map(toFs_)}};
  if (typeof v === 'object') { var f = {}; for (var k in v) f[k] = toFs_(v[k]); return {mapValue: {fields: f}}; }
  return {stringValue: String(v)};
}
function fromFs_(x) {
  if (!x) return null;
  if ('stringValue' in x) return x.stringValue;
  if ('integerValue' in x) return Number(x.integerValue);
  if ('doubleValue' in x) return x.doubleValue;
  if ('booleanValue' in x) return x.booleanValue;
  if ('timestampValue' in x) return new Date(x.timestampValue);
  if ('mapValue' in x) { var o = {}, f = x.mapValue.fields || {}; for (var k in f) o[k] = fromFs_(f[k]); return o; }
  if ('arrayValue' in x) return (x.arrayValue.values || []).map(fromFs_);
  return null;
}
function doc_(d) { var o = {}, f = d.fields || {}; for (var k in f) o[k] = fromFs_(f[k]); o._id = d.name.split('/').pop(); return o; }
function fsSet_(path, obj) { var f = {}; for (var k in obj) f[k] = toFs_(obj[k]); return fsCall_('patch', path, {fields: f}); }
function eventsBetween_(from, to, limit) {
  var where = {compositeFilter: {op: 'AND', filters: [
    {fieldFilter: {field: {fieldPath: 'time'}, op: 'GREATER_THAN', value: {timestampValue: from.toISOString()}}},
    {fieldFilter: {field: {fieldPath: 'time'}, op: 'LESS_THAN', value: {timestampValue: to.toISOString()}}}]}};
  var rows = fsCall_('post', ':runQuery', {structuredQuery: {from: [{collectionId: 'events'}], where: where,
    orderBy: [{field: {fieldPath: 'time'}, direction: 'ASCENDING'}], limit: limit || 300}}) || [];
  return rows.filter(function (r) { return r.document; }).map(function (r) { return doc_(r.document); });
}
function members_() {
  var r = fsCall_('get', '/members?pageSize=100') || {};
  return (r.documents || []).map(function (d) { var o = doc_(d); o.email = o._id; return o; });
}

// ---------------- senders ----------------
function telegram_(text) {
  if (!cfg('TG_BOT_TOKEN') || !cfg('TG_CHAT_ID')) return 'off';
  var r = UrlFetchApp.fetch('https://api.telegram.org/bot' + cfg('TG_BOT_TOKEN') + '/sendMessage',
    {method: 'post', contentType: 'application/json', muteHttpExceptions: true, payload: JSON.stringify({chat_id: cfg('TG_CHAT_ID'), text: text.slice(0, 3500)})});
  return r.getResponseCode() === 200 ? 'ok' : 'error ' + r.getResponseCode();
}
function whatsapp_(number, text) {
  if (!cfg('WA_TOKEN') || !cfg('WA_PHONE_ID') || !number) return 'off';
  var tpl = cfg('WA_TEMPLATE') || 'room_update';
  var template = tpl === 'hello_world' ? {name: 'hello_world', language: {code: 'en_US'}}
    : {name: tpl, language: {code: 'en'}, components: [{type: 'body', parameters: [{type: 'text', text: text.replace(/\s+/g, ' ').slice(0, 900)}]}]};
  var r = UrlFetchApp.fetch('https://graph.facebook.com/v21.0/' + cfg('WA_PHONE_ID') + '/messages',
    {method: 'post', contentType: 'application/json', muteHttpExceptions: true, headers: {Authorization: 'Bearer ' + cfg('WA_TOKEN')},
     payload: JSON.stringify({messaging_product: 'whatsapp', to: String(number).replace(/\D/g, ''), type: 'template', template: template})});
  var code = r.getResponseCode();
  if (code !== 200) log_('WhatsApp to ' + number, code + ' ' + r.getContentText().slice(0, 200));
  return code === 200 ? 'ok' : 'error ' + code;
}
function email_(subject, html) {
  if (!cfg('REPORT_EMAIL')) return 'off';
  MailApp.sendEmail({to: cfg('REPORT_EMAIL'), subject: subject, htmlBody: html, name: 'SARTHIK Room'});
  return 'ok';
}
function esc_(s) { return String(s == null ? '' : s).replace(/[&<>"]/g, function (c) { return {'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;'}[c]; }); }

// ---------------- instant events (board + laptop) ----------------
function handled_(id) { return !!CacheService.getScriptCache().get('ev:' + id); }
function markHandled_(id) { CacheService.getScriptCache().put('ev:' + id, '1', 21600); }
// Google allows ~100 e-mails a day on a free account: entries stop at 70, alerts at 95 (daily report always goes)
function mailCapped_(subject, html, important) {
  var day = fmt(new Date(), 'yyyy-MM-dd');
  if (cfg('MAIL_D') !== day) { P().setProperty('MAIL_D', day); P().setProperty('MAIL_N', '0'); }
  var n = Number(cfg('MAIL_N') || 0);
  if (n >= (important ? 95 : 70)) return 'limit';
  P().setProperty('MAIL_N', String(n + 1));
  return email_(subject, html);
}
var MEMBERS_CACHE_ = null;
function waTo_(filter, text) {
  if (!cfg('WA_TOKEN')) return;
  MEMBERS_CACHE_ = MEMBERS_CACHE_ || members_();
  MEMBERS_CACHE_.filter(filter).forEach(function (m) { whatsapp_(m.whatsapp, text); });
}
function howText_(type) { return type === 'entry_phone' ? 'phone key' : type === 'entry_visitor_qr' ? 'visitor QR pass' : 'card + face'; }
function noticeEvent_(e) {                    // e-mail + WhatsApp for one event (Telegram is sent by the board itself)
  var who = e.name || e.person || 'someone';
  if (/^entry_/.test(e.type)) {
    mailCapped_('SARTHIK Room: ' + who + ' entered', '<p><b>' + esc_(who) + '</b> entered at ' + fmt(e.time, 'HH:mm') + ' (' + howText_(e.type) + ').</p>', false);
    var notify = cfg('NOTIFY_PEOPLE').toLowerCase().split(/[,\s]+/).filter(String);
    if (e.person && notify.indexOf(String(e.person).toLowerCase()) >= 0)
      waTo_(function (m) { return m.whatsapp && (m.role === 'admin' || m.role === 'viewer'); }, e.person + ' arrived at ' + fmt(e.time, 'HH:mm') + ' (' + howText_(e.type) + ')');
  } else if (e.severity === 'alert') {
    mailCapped_('SARTHIK Room: security alert - ' + e.type.replace(/_/g, ' '), '<p><b>' + esc_(e.type.replace(/_/g, ' ')) + '</b> ' + esc_(who) + ' - ' + esc_(e.detail) + ' (' + fmt(e.time) + ', ' + esc_(e.source) + ')</p>', true);
    waTo_(function (m) { return m.whatsapp && m.role === 'admin'; }, 'Security alert: ' + e.type.replace(/_/g, ' ') + ' ' + (e.name || ''));
  }
}
function appendEvent_(e) {
  sheets_().getSheetByName('Events').appendRow([e.time, e.device || '', e.type, e.severity, e.person || '', e.name || '', e.detail || '', e.source || '']);
}
// door board -> Sheet, seconds after the entry (the 5-minute route() then skips it: no duplicates)
function boardEvent_(ev) {
  var id = String(ev.id || '').replace(/[^\w-]/g, '').slice(0, 40);
  if (!id) return json_({ok: false, error: 'no id'});
  if (handled_(id)) return json_({ok: true, dup: true});
  var e = {device: String(ev.device || 'door-1').slice(0, 30), type: String(ev.type || '').slice(0, 40), severity: String(ev.severity || 'info').slice(0, 10),
           person: String(ev.person || '').slice(0, 40), name: String(ev.name || '').slice(0, 60), detail: String(ev.detail || '').slice(0, 200), source: 'door',
           time: new Date((Number(ev.ts) > 1600000000 ? Number(ev.ts) : Date.now() / 1000) * 1000)};
  appendEvent_(e);
  noticeEvent_(e);
  markHandled_(id);
  return json_({ok: true});
}
var CCTV_KEEP_ = ['cctv_unknown', 'mismatch_door_cctv', 'no_door_auth', 'multiple_people', 'not_verified', 'cross_verified', 'cctv_seen', 'face_registered'];

// ---------------- every 5 minutes: route new events ----------------
function route() {
  var lock = LockService.getScriptLock();
  if (!lock.tryLock(1000)) return 'busy';
  try {
    var since = new Date(cfg('CURSOR') || (Date.now() - 3600e3)), now = new Date();
    var evs = eventsBetween_(since, new Date(now.getTime() + 60e3), 200), all = evs;
    var sh = sheets_().getSheetByName('Events'), rows = [];
    evs = evs.filter(function (e) { return !handled_(e._id); });          // already done instantly
    evs.forEach(function (e) {
      rows.push([e.time, e.device || '', e.type, e.severity, e.person || '', e.name || '', e.detail || '', e.source || '']);
      markHandled_(e._id);
      noticeEvent_(e);                     // the same e-mail / WhatsApp as the instant path (safety net: max 1 minute late)
    });
    if (rows.length) sh.getRange(sh.getLastRow() + 1, 1, rows.length, rows[0].length).setValues(rows);
    if (all.length) P().setProperty('CURSOR', all[all.length - 1].time.toISOString());
    fsSet_('/devices/reporter', {seen: new Date(), last_route: evs.length, version: 'stage3-2'});
    return 'routed ' + evs.length;   // (events handled instantly are not counted again)
  } catch (err) { log_('route', err.message); return 'error: ' + err.message; }
  finally { lock.releaseLock(); }
}

// ---------------- every day 8 PM: attendance report ----------------
function summarise_(evs) {
  var by = {}, visitors = 0, alerts = 0, warns = 0;
  evs.forEach(function (e) {
    if (e.type === 'entry_visitor_qr') visitors++;
    if (e.severity === 'alert') alerts++; else if (e.severity === 'warn') warns++;
    if (!/^entry_(card_face|phone)$/.test(e.type) || !e.person) return;
    var r = by[e.person] || (by[e.person] = {person: e.person, first: e.time, last: e.time, n: 0, how: {}});
    r.n++; if (e.time < r.first) r.first = e.time; if (e.time > r.last) r.last = e.time;
    r.how[e.type === 'entry_phone' ? 'phone key' : 'card + face'] = 1;
  });
  return {people: Object.keys(by).map(function (k) { return by[k]; }), visitors: visitors, alerts: alerts, warns: warns};
}
function dailyReport(day) {
  try {
    var start = (typeof day === 'string' && day) ? new Date(day) : new Date(); start.setHours(0, 0, 0, 0);   // a timer passes an event object here, not a date
    var end = new Date(start.getTime() + 864e5), key = fmt(start, 'yyyy-MM-dd');
    var s = summarise_(eventsBetween_(start, end, 1000));
    var sh = sheets_().getSheetByName('Daily');
    s.people.forEach(function (r) { sh.appendRow([key, r.person, fmt(r.first, 'HH:mm'), fmt(r.last, 'HH:mm'), r.n, Object.keys(r.how).join(', ')]); });
    if (!s.people.length) sh.appendRow([key, '(nobody)', '', '', 0, '']);
    fsSet_('/reports/' + key, {day: key, present: s.people.map(function (r) { return r.person; }), visitors: s.visitors, alerts: s.alerts, warnings: s.warns, made: new Date()});
    var text = 'SARTHIK Room ' + key + ': ' + s.people.length + ' present (' + s.people.map(function (r) { return r.person; }).join(', ') +
               '), ' + s.visitors + ' visitor(s), ' + s.alerts + ' alert(s).';
    email_('SARTHIK Room daily report ' + key, '<h2>Daily report ' + key + '</h2><table border="1" cellpadding="6" style="border-collapse:collapse">' +
      '<tr><th>Person</th><th>First</th><th>Last</th><th>Entries</th><th>How</th></tr>' + s.people.map(function (r) {
      return '<tr><td>' + esc_(r.person) + '</td><td>' + fmt(r.first, 'HH:mm') + '</td><td>' + fmt(r.last, 'HH:mm') + '</td><td>' + r.n + '</td><td>' + esc_(Object.keys(r.how).join(', ')) + '</td></tr>'; }).join('') +
      '</table><p>Visitors: ' + s.visitors + ' &middot; Alerts: ' + s.alerts + ' &middot; Warnings: ' + s.warns + '</p>');
    telegram_(text);
    members_().filter(function (m) { return m.whatsapp && m.role === 'admin'; }).forEach(function (m) { whatsapp_(m.whatsapp, text); });
    log_('daily report ' + key, 'ok');
    return text;
  } catch (err) { log_('daily report', err.message); return 'error: ' + err.message; }
}
// weekly / monthly: from the Daily sheet (no database reads) - started by n8n on the laptop
function periodReport(days, label) {
  var rows = sheets_().getSheetByName('Daily').getDataRange().getValues().slice(1), since = new Date(Date.now() - days * 864e5);
  var byDay = {};
  rows.forEach(function (r) { var d = new Date(r[0]); if (d >= since && r[1] !== '(nobody)') (byDay[r[0]] = byDay[r[0]] || []).push(r[1]); });
  var keys = Object.keys(byDay), avg = keys.length ? (keys.reduce(function (a, k) { return a + byDay[k].length; }, 0) / keys.length).toFixed(1) : '0';
  sheets_().getSheetByName('Summary').appendRow([label, keys.length, avg, '', '', '', new Date()]);
  var text = 'SARTHIK Room ' + label + ' report: ' + keys.length + ' days with entries, ' + avg + ' people per day on average.';
  email_('SARTHIK Room ' + label + ' report', '<h2>' + esc_(label) + ' report</h2><p>' + esc_(text) + '</p><p>Details: see the Daily sheet.</p>');
  telegram_(text);
  return text;
}

// ---------------- web address: laptop CCTV webhook + n8n ----------------
function json_(o) { return ContentService.createTextOutput(JSON.stringify(o)).setMimeType(ContentService.MimeType.JSON); }
function doPost(e) {
  if (!e || !e.parameter || e.parameter.key !== cfg('HOOK_KEY') || !cfg('HOOK_KEY')) return json_({ok: false, error: 'wrong key'});
  try {
    var ev = JSON.parse(e.postData.contents);
    if (ev.via === 'board') return boardEvent_(ev);
    if (ev.source === 'door') return json_({ok: true, skipped: 'door events come straight from the board'});   // no duplicates
    if (CCTV_KEEP_.indexOf(String(ev.type)) < 0 && ev.severity !== 'alert' && ev.severity !== 'warn')
      return json_({ok: true, skipped: 'not needed'});                                     // e.g. person left the camera view
    var c = CacheService.getScriptCache(), who = String(ev.name || ev.person_id || 'someone');
    if (ev.type === 'cctv_seen') {                                                           // same person: at most every 30 minutes
      if (c.get('seen:' + who)) return json_({ok: true, skipped: 'seen recently'});
      c.put('seen:' + who, '1', 1800);
    }
    var id = 'laptop_' + String(ev.id || Date.now()).replace(/[^\w-]/g, '').slice(0, 40);
    fsSet_('/events/' + id, {device: String(ev.device || 'laptop').slice(0, 30), seq: Number(ev.id) || 0, time: new Date((ev.ts || Date.now() / 1000) * 1000),
      type: String(ev.type || 'event').slice(0, 40), severity: String(ev.severity || 'info').slice(0, 10), person: String(ev.person_id || '').slice(0, 40),
      name: String(ev.name || '').slice(0, 60), detail: String(ev.detail || '').slice(0, 200), source: String(ev.source || 'cctv').slice(0, 20)});
    fsSet_('/devices/laptop', {seen: new Date(), last: String(ev.type || '')});
    var le = {device: String(ev.device || 'laptop'), type: String(ev.type), severity: String(ev.severity || 'info'), person: String(ev.person_id || ''),
              name: who, detail: String(ev.detail || '').slice(0, 200), source: String(ev.source || 'cctv'), time: new Date()};
    appendEvent_(le);
    markHandled_(id);
    if (ev.severity === 'alert') {
      telegram_('CCTV ALERT\n' + (ev.title || ev.type) + (ev.name ? ': ' + ev.name : '') + '\nTime: ' + fmt(new Date()));
      noticeEvent_(le);
    } else if (ev.type === 'cross_verified' || ev.type === 'cctv_seen') {
      var txt = (ev.type === 'cross_verified' ? 'CCTV confirmed the door entry of ' : 'CCTV recognised ') + who + ' at ' + fmt(new Date(), 'HH:mm');
      telegram_(txt);
      mailCapped_('SARTHIK Room: ' + txt, '<p>' + esc_(txt) + '</p>', false);
    }
    return json_({ok: true});
  } catch (err) { log_('webhook', err.message); return json_({ok: false, error: 'bad event'}); }
}
function doorStatus_() {
  var d = fsCall_('get', '/devices/' + DEVICE), seen = d && d.fields && d.fields.seen ? fromFs_(d.fields.seen) : null;
  return {seen: seen, online: !!seen && Date.now() - seen.getTime() < 5 * 60e3, queue: d && d.fields.queue ? fromFs_(d.fields.queue) : null};
}
function doGet(e) {
  if (e && e.parameter && e.parameter.action === 'uptime') {          // public, for UptimeRobot: says only online / offline
    try { var u = doorStatus_(); return json_({ok: true, door_online: u.online}); } catch (err) { return json_({ok: false}); }
  }
  if (!e || !e.parameter || e.parameter.key !== cfg('HOOK_KEY') || !cfg('HOOK_KEY')) return json_({ok: false, error: 'wrong key'});
  var a = e.parameter.action;
  try {
    if (a === 'reports') {                                            // n8n every hour: sends what is due (catch-up if the laptop was off)
      var now = new Date(), sent = [], last = cfg('LAST_WEEKLY'), month = now.getFullYear() + '-' + (now.getMonth() + 1);
      if (!last || now - new Date(last) >= 7 * 864e5) { sent.push(periodReport(7, 'Weekly')); P().setProperty('LAST_WEEKLY', now.toISOString()); }
      if (cfg('LAST_MONTH') !== month) { sent.push(periodReport(30, 'Monthly')); P().setProperty('LAST_MONTH', month); }
      fsSet_('/devices/reporter', {seen: new Date(), n8n: new Date()});
      return json_({ok: true, sent: sent});
    }
    if (a === 'watchdog') {                                           // n8n every 10 min: tells you ONCE when the door goes offline / comes back
      var w = doorStatus_(), alerted = cfg('WATCH_ALERTED') === '1', said = '';
      if (!w.online && !alerted) { said = 'offline'; P().setProperty('WATCH_ALERTED', '1');
        var t = 'Door board is OFFLINE (last seen ' + (w.seen ? fmt(w.seen) : 'never') + '). Check power and Wi-Fi.';
        telegram_('n8n: ' + t); email_('SARTHIK Room: door board offline', '<p>' + esc_(t) + '</p>'); }
      else if (w.online && alerted) { said = 'back online'; P().setProperty('WATCH_ALERTED', '0'); telegram_('n8n: Door board is back online.'); }
      fsSet_('/devices/reporter', {seen: new Date(), n8n: new Date()});
      return json_({ok: true, door_online: w.online, sent: said});
    }
    if (a === 'weekly') return json_({ok: true, text: periodReport(7, 'Weekly')});
    if (a === 'monthly') return json_({ok: true, text: periodReport(30, 'Monthly')});
    if (a === 'health') {
      var d = fsCall_('get', '/devices/' + DEVICE), seen = d ? fromFs_(d.fields.seen) : null;
      fsSet_('/devices/reporter', {seen: new Date(), n8n: new Date()});
      return json_({ok: true, door_seen: seen, door_online: !!seen && Date.now() - seen.getTime() < 5 * 60e3, queue: d ? fromFs_(d.fields.queue) : null});
    }
    if (a === 'notify') {                         // n8n watchdog: one short message to Telegram (+ e-mail for 'important')
      var t = String(e.parameter.text || '').slice(0, 500);
      if (!t) return json_({ok: false, error: 'no text'});
      var res = {telegram: telegram_('n8n: ' + t)};
      if (e.parameter.important === '1') res.email = email_('SARTHIK Room: ' + t.slice(0, 80), '<p>' + esc_(t) + '</p>');
      return json_({ok: true, sent: res});
    }
    return json_({ok: true, pong: true});
  } catch (err) { return json_({ok: false, error: err.message}); }
}

// ---------------- 3. Test everything ----------------
function testAll() {
  var out = [];
  try { token_(); out.push('Firebase login: OK'); } catch (e) { out.push('Firebase login: ' + e.message); }
  try { var m = members_(); out.push('Members readable: OK (' + m.length + ')'); } catch (e) { out.push('Members: ' + e.message); }
  out.push('Telegram: ' + telegram_('SARTHIK Room test from the Google Sheet. It works.'));
  try { out.push('E-mail: ' + email_('SARTHIK Room test', '<p>The Google Sheet can send you e-mails.</p>')); } catch (e) { out.push('E-mail: ' + e.message); }
  var wa = 'off';
  try { var admins = members_().filter(function (x) { return x.whatsapp && x.role === 'admin'; }); if (admins.length) wa = whatsapp_(admins[0].whatsapp, 'Test from SARTHIK Room'); } catch (e) { wa = e.message; }
  out.push('WhatsApp: ' + wa);
  out.push('Sentry: ' + sentry_('SARTHIK Room test event from the Google Sheet (you can ignore this)'));
  out.push('Instant link from the door board: ' + (cfg('HOOK_KEY') ? 'ready' : 'missing'));
  log_('test', out.join(' | '));
  try { SpreadsheetApp.getUi().alert(out.join('\n')); } catch (e) {}
  return out;
}
