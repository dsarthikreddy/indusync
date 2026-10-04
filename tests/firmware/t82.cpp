#include "stubs82.h"
#include "v82.h"
void dumpJson(const char* f, const std::string& s);
// ---------- pretend internet ----------
std::map<std::string, std::string> fsDocs; std::vector<std::string> calls, tgSent, tsUrls, sheetPosts; bool blMissV3 = false;
const size_t TOKLEN = 1043; int fbDown = 0, loginBad = 0, forbid = 0, v10 = 1; std::string cmdDoc;
int v9Http(const char* method, const String& url, const String& body, String& reply, const char* auth, int) {
  std::string u = url.s, m = method; calls.push_back(m + " " + u);
  if (u.find("identitytoolkit") != std::string::npos || u.find("securetoken") != std::string::npos) {
    if (loginBad) { reply = String("{\"error\":{\"message\":\"INVALID_LOGIN_CREDENTIALS\"}}"); return 400; }
    reply = String("{\n  \"kind\": \"identitytoolkit#VerifyPasswordResponse\",\n  \"idToken\": \"" + std::string(TOKLEN, 'x') + "\",\n  \"refreshToken\": \"REF1\",\n  \"expiresIn\": \"3600\"\n}"); return 200; }
  if (u.find("firestore.googleapis.com") != std::string::npos) {
    if (fbDown > 0) { fbDown--; return -1; }
    if (forbid) { reply = String("{\"error\":{\"code\":403}}"); return 403; }
    if (std::string(auth ? auth : "").size() != TOKLEN) return 401;
    std::string path = u.substr(u.find("/documents/") + 11); path = path.substr(0, path.find('?'));
    if (m == "PATCH") { if (path.rfind("commands/", 0) == 0) fsDocs[path + "#done"] = body.s; else fsDocs[path] = body.s; reply = String("{}"); return 200; }
    if (m == "GET") { if (cmdDoc.empty()) return 404; reply = String(cmdDoc); return 200; }
  }
  if (u.find("script.google.com") != std::string::npos) { sheetPosts.push_back(body.s); return 302; }
  if (u.find("api.thingspeak.com") != std::string::npos) { tsUrls.push_back(u); reply = String("17"); return 200; }
  if (u.find("blynk.cloud") != std::string::npos && blMissV3 && (u.find("&V3=") != std::string::npos)) { reply = String("{\"error\":{\"message\":\"Requested dataStream doesn't exist in the template.\"}}"); return 400; }
  if (u.find("blynk.cloud") != std::string::npos) { if (u.find("batch") != std::string::npos) dumpJson("blynk_url.txt", u); if (u.find("get?") != std::string::npos) { if (v10 < 0) { reply = String("{\"error\":{\"message\":\"Requested pin doesn't have any value yet.\"}}"); return 400; } reply = String(std::to_string(v10)); v10 = 0; } else reply = String(""); return 200; }
  if (u.find("api.telegram.org") != std::string::npos) {
    if (u.find("sendMessage") != std::string::npos) { tgSent.push_back(body.s); reply = String("{\"ok\":true}"); return 200; }
    reply = String("{\"ok\":true,\"result\":[{\"update_id\":500,\"message\":{\"message_id\":1,\"from\":{\"id\":111},\"chat\":{\"id\":111,\"type\":\"private\"},\"text\":\"/open\"}},"
                   "{\"update_id\":501,\"message\":{\"message_id\":2,\"from\":{\"id\":1234567890},\"chat\":{\"id\":1234567890,\"type\":\"private\"},\"text\":\"/status\"}}]}");
    return 200; }
  return 404;
}
void drain() { V8Event e; while (v8EvQueue && xQueueReceive(v8EvQueue, &e, 0)) v8SaveEvent(e); }
void spin(int n, int stepMs = 1000) { for (int i = 0; i < n; i++) { g_ms += stepMs; drain(); v9CloudLoop(); } }
void dumpJson(const char* f, const std::string& s) { FILE* o = fopen(f, "w"); fputs(s.c_str(), o); fclose(o); }
int main(int argc, char** argv) {
  std::string mode = argc > 1 ? argv[1] : "";
  v8LoadCards(); v8Setup(); v8WifiOK = true; v9Setup();
  if (mode == "flow") {
    v8Event(V8_ENTRY_FACE, 0, "sarthikreddy", "match 0.82");
    v8Event(V8_QR_FAKE, -1, "visitor", "signature wrong");                 // alert -> Telegram
    v8Event(V8_ENTRY_FACE, 1, "goutam \"g\", ok", "comma, \"quote\"");
    drain();
    v9Slots[8].enabled = true; g_digital[38] = 1;                 // a sensor of channel B (PIR) is connected
    for (int i = 0; i < 40; i++) v9SensorsLoop();
    spin(400);
    printf("{\"cloudseq\":%u,\"seq\":%u,\"docs\":%zu,\"tg\":%zu,\"ts\":%zu,\"fbok\":%u,\"nvs\":%u}\n", (unsigned)v9CloudSeq, (unsigned)v8Seq,
           fsDocs.size(), tgSent.size(), tsUrls.size(), (unsigned)v9Fb.ok, (unsigned)v8Store.getUInt("cloudseq", 0));
    int k = 0; for (auto& d : fsDocs) dumpJson(("doc_" + std::to_string(k++) + ".json").c_str(), d.second);
    for (size_t i = 0; i < tgSent.size(); i++) dumpJson(("tg_" + std::to_string(i) + ".json").c_str(), tgSent[i]);
    dumpJson("ts_urls.txt", tsUrls.empty() ? "" : tsUrls[0] + "\n" + tsUrls[1]);
  } else if (mode == "outage") {                // Firebase down for a while: nothing lost, retries back off, then catches up
    fbDown = 6;
    for (int i = 0; i < 5; i++) v8Event(V8_ENTRY_FACE, 0, "sarthikreddy", "x");
    drain(); spin(30);
    unsigned during = v9CloudSeq; unsigned long back = v9Fb.backoff;
    spin(700);
    printf("{\"during\":%u,\"backoff_ms\":%lu,\"after\":%u,\"seq\":%u,\"failed\":%u}\n", during, back, (unsigned)v9CloudSeq, (unsigned)v8Seq, (unsigned)v9Fb.fail);
  } else if (mode == "badlogin") {
    loginBad = 1; v8Event(V8_ENTRY_FACE, 0, "s", ""); drain(); spin(60);
    printf("{\"err\":\"%s\",\"cloudseq\":%u,\"door_events\":%u}\n", v9Fb.err, (unsigned)v9CloudSeq, (unsigned)v8Seq);
  } else if (mode == "forbid") {
    forbid = 1; v8Event(V8_ENTRY_FACE, 0, "s", ""); drain(); spin(30);
    printf("{\"err\":\"%s\",\"cloudseq\":%u}\n", v9Fb.err, (unsigned)v9CloudSeq);
  } else if (mode == "cmd") {
    v10 = 0;   // Blynk button not pressed in this test
    cmdDoc = "{\n  \"name\": \"projects/p/databases/(default)/documents/commands/door-1\",\n  \"fields\": {\n    \"cmd\": {\n      \"stringValue\": \"OpenDoor\"\n    },\n    \"id\": {\n      \"stringValue\": \"c-1001\"\n    },\n    \"by\": {\n      \"stringValue\": \"admin@x\"\n    }\n  }\n}";
    spin(60); int ran1 = 0; V8Cmd m; while (xQueueReceive(v8CmdQueue, &m, 0)) { ran1++; if (std::string(m.text) != "opendoor") ran1 = -100; }
    spin(60); int ran2 = 0; while (xQueueReceive(v8CmdQueue, &m, 0)) ran2++;                       // same id again: must NOT run twice
    std::string done = fsDocs["commands/door-1#done"];
    cmdDoc = "{\"fields\":{\"cmd\":{\"stringValue\":\"clear\"},\"id\":{\"stringValue\":\"c-1002\"}}}";
    spin(60); int ran3 = 0; while (xQueueReceive(v8CmdQueue, &m, 0)) { ran3++; printf("queued: %s\n", m.text); }
    std::string done2 = fsDocs["commands/door-1#done"];
    printf("{\"ran_first\":%d,\"ran_again\":%d,\"done_ok\":%d,\"dangerous_ran\":%d,\"dangerous_refused\":%d}\n", ran1, ran2,
           (int)(done.find("c-1001") != std::string::npos && done.find("\"done\"") != std::string::npos), ran3, (int)(done2.find("refused") != std::string::npos));
  } else if (mode == "telegram") {
    v10 = 0;   // Blynk button not pressed in this test
    v9Tg.nextTry = 0; spin(40, 1000);
    int opened = 0; V8Cmd m; while (xQueueReceive(v8CmdQueue, &m, 0)) opened++;
    std::string all; for (auto& t : tgSent) all += t;
    printf("{\"stranger_opened\":%d,\"owner_got_status\":%d,\"offset\":%ld,\"replies_to_stranger\":%d}\n", opened,
           (int)(all.find("SARTHIK Room door") != std::string::npos), v9TgOffset, (int)(all.find("\"chat_id\":\"111\"") != std::string::npos));
  } else if (mode == "blynk") {
    spin(400);
    int opened = 0; V8Cmd m; while (xQueueReceive(v8CmdQueue, &m, 0)) opened++;
    printf("{\"blynk_ok\":%u,\"door_from_button\":%d,\"button_reset\":%d}\n", (unsigned)v9Bl.ok, opened, v10 == 0);
  } else if (mode == "mirror") {

    v9OledSet("PRESENT", "sarthikreddy", "Attendance marked", "Match 0.81", "Door OPEN");
    unsigned long before = v9Fb.ok;
    spin(12);                                                         // OLED changed -> status sent within ~8-12 s
    std::string st; for (auto& d : fsDocs) if (d.first == "devices/door-1") st = d.second;
    printf("{\"pushed_fast\":%d,\"has_oled\":%d,\"blynk_pins\":0}\n", (int)(v9Fb.ok > before && st.find("Attendance marked") != std::string::npos),
           (int)(st.find("\"oled\"") != std::string::npos));
    dumpJson("mirror_doc.json", st);
  } else if (mode == "instant") {
    v8Event(V8_ENTRY_FACE, 0, "sarthikreddy", "match 0.82");
    v8Event(V8_ENTRY_PHONE, 1, "goutam", "-48 dBm");
    drain(); spin(6, 1000);
    std::string tg; for (auto& t : tgSent) tg += t;
    int firstOk = sheetPosts.size() >= 2 && sheetPosts[0].find("\"via\":\"board\"") != std::string::npos;
    printf("{\"tg_entry\":%d,\"tg_phone\":%d,\"sheet_rows\":%zu,\"sheet_ok\":%d,\"sheet_seq_saved\":%u,\"seq\":%u}\n",
           (int)(tg.find("ENTRY: sarthikreddy (card + face)") != std::string::npos), (int)(tg.find("ENTRY: goutam (phone key)") != std::string::npos),
           sheetPosts.size(), firstOk, (unsigned)v8Store.getUInt("sheetseq", 0), (unsigned)v8Seq);
    dumpJson("sheet_post.json", sheetPosts.empty() ? "{}" : sheetPosts[0]);
  } else if (mode == "blynkmiss") {
    blMissV3 = true; v10 = 1;
    spin(60);
    int opened = 0; V8Cmd m; while (xQueueReceive(v8CmdQueue, &m, 0)) opened++;
    std::string lastBatch; for (auto& c : calls) if (c.find("batch/update") != std::string::npos) lastBatch = c;
    printf("{\"ok\":%u,\"msg\":\"%s\",\"opened\":%d,\"mask\":%u}\n", (unsigned)v9Bl.ok, v9Bl.err, opened, (unsigned)v9BlMask);
    v8Event(V8_ENTRY_FACE, 0, "x", ""); drain(); g_ms += 300000; v9Bl.nextTry = 0; size_t nb = calls.size(); spin(40);
    std::string after; for (size_t i = nb; i < calls.size(); i++) if (calls[i].find("batch/update") != std::string::npos) after = calls[i];
    printf("{\"next_batch_has_V3\":%d,\"next_batch_has_V2\":%d}\n", (int)(after.find("&V3=") != std::string::npos), (int)(after.find("&V2=") != std::string::npos));
  } else if (mode == "blynknovalue") {
    v10 = -1; spin(60);
    int setZero = 0; for (auto& c : calls) if (c.find("update?token=") != std::string::npos && c.find("&V10=0") != std::string::npos) setZero++;
    printf("{\"button_kept\":%d,\"msg\":\"%s\",\"ok\":%u,\"v10_set_to_0\":%d}\n", (int)v9BlButton, v9Bl.err, (unsigned)v9Bl.ok, setZero);
  } else if (mode == "sensors") {
    v9Slots[8].enabled = true; g_digital[38] = 1;                                // PIR wired and moving
    v9Slots[4].enabled = true; Wire.addrOk = 0x48; Wire.raw = 285;              // temperature on ADS1115: 285 * 0.1 = 28.5 C
    v9Slots[12].enabled = true;                                                 // gas sensor on 0x49 but NOT connected
    v9Slots[11].enabled = true; v9Slots[11].maxV = 5;                           // voltage reads 0x49 -> missing too
    v9Slots[5].enabled = true; v9Slots[5].maxV = 5;                              // humidity: 285*0.03 = 8.55 > 5 -> fault
    for (int r = 0; r < 4; r++) { g_ms += 20000; for (int i = 0; i < 20; i++) v9SensorsLoop(); }
    std::string out = "{";
    for (int i = 0; i < V9_SLOTS; i++) { char b[120]; snprintf(b, 120, "%s\"%s\":[\"%s\",%.2f]", i ? "," : "", v9Slots[i].type, v9StatusWord(v9Slots[i].status), v9Slots[i].value); out += b; }
    printf("%s}\n", out.c_str());
    v9Bl.on = true; v9Bad(v9Bl, "Blynk: {\"error\":{\"message\":\"Invalid token.\"}}");
    dumpJson("status_doc.json", v9StatusDoc().s); dumpJson("door_sensors.json", v8DoorJson().s);
  }
  return 0;
}
