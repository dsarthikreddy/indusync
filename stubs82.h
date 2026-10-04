#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdarg>
#include <string>
#include <vector>
#include <map>
#include <ctime>
#include <type_traits>
static unsigned long g_ms = 5000; static time_t g_time = 1790000000;
unsigned long millis() { return g_ms; }
void delay(unsigned long ms) { g_ms += ms; }
time_t fake_time(time_t* p) { return g_time + g_ms / 1000; }
#define time(x) fake_time(x)
struct String {
  std::string s;
  String() {}
  String(const char* c) : s(c ? c : "") {}
  String(const std::string& c) : s(c) {}
  String(char c) : s(1, c) {}
  template <typename T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0> String(T v) : s(std::to_string(v)) {}
  String(unsigned char v, int base) { char b[8]; snprintf(b, 8, base == 16 ? "%x" : "%u", v); s = b; }
  String(double v, int d) { char b[32]; snprintf(b, 32, "%.*f", d, v); s = b; }
  String(float v, int d) { char b[32]; snprintf(b, 32, "%.*f", d, v); s = b; }
  String operator+(const String& o) const { return String(s + o.s); }
  String operator+(const char* o) const { return String(s + o); }
  template <typename T, typename std::enable_if<std::is_integral<T>::value, int>::type = 0> String operator+(T v) const { return String(s + std::to_string(v)); }
  String& operator+=(const String& o) { s += o.s; return *this; }
  String& operator+=(char c) { s += c; return *this; }
  String& operator+=(const char* c) { s += c; return *this; }
  bool operator==(const char* o) const { return s == o; }
  bool operator!=(const char* o) const { return s != o; }
  void toLowerCase() { for (auto& c : s) c = tolower(c); }
  const char* c_str() const { return s.c_str(); }
  size_t length() const { return s.size(); }
  int indexOf(char c, int from = 0) const { size_t p = s.find(c, from); return p == std::string::npos ? -1 : (int)p; }
  int indexOf_old(const char* t) const { size_t p = s.find(t); return p == std::string::npos ? -1 : (int)p; }
  String substring(int a) const { return a >= (int)s.size() ? String("") : String(s.substr(a)); }
  String substring(int a, int b) const { return String(s.substr(a, b - a)); }
  void trim() { size_t a = s.find_first_not_of(" \r\n\t"), b = s.find_last_not_of(" \r\n\t"); s = a == std::string::npos ? "" : s.substr(a, b - a + 1); }
  bool startsWith(const char* p) const { return s.rfind(p, 0) == 0; }
  bool endsWith(const char* p) const { size_t n = strlen(p); return s.size() >= n && s.compare(s.size() - n, n, p) == 0; }
  long toInt() const { return atol(s.c_str()); }
  int indexOf(const String& t, int from = 0) const { size_t p = s.find(t.s, from); return p == std::string::npos ? -1 : (int)p; }
  int indexOf(const char* t, int from) const { size_t p = s.find(t, from); return p == std::string::npos ? -1 : (int)p; }
  char operator[](int i) const { return s[i]; }
  bool operator==(const String& o) const { return s == o.s; }
  bool operator!=(const String& o) const { return s != o.s; }
};
String operator+(const char* a, const String& b) { return String(std::string(a) + b.s); }
struct SerialT { void println(const String& x = String()) { lines.push_back(x.s); } void println(const char* x) { lines.push_back(x); }
  void printf(const char* f, ...) { char b[600]; va_list a; va_start(a, f); vsnprintf(b, 600, f, a); va_end(a); lines.push_back(b); }
  std::vector<std::string> lines; } Serial;
// ---- fake permanent memory ----
struct Preferences {
  static std::map<std::string, std::map<std::string, std::vector<uint8_t>>> all; std::string ns;
  bool begin(const char* n, bool ro = false) { ns = n; if (ro && !all.count(ns)) return false; all[ns]; return true; }
  void end() {}
  bool isKey(const char* k) { return all[ns].count(k) > 0; }
  template <typename T> void put(const char* k, T v) { auto& m = all[ns][k]; m.assign((uint8_t*)&v, (uint8_t*)&v + sizeof(T)); }
  template <typename T> T get(const char* k, T d) { auto& m = all[ns]; if (!m.count(k) || m[k].size() != sizeof(T)) return d; T v; memcpy(&v, m[k].data(), sizeof(T)); return v; }
  void putUInt(const char* k, uint32_t v) { put(k, v); } uint32_t getUInt(const char* k, uint32_t d) { return get(k, d); }
  void putBool(const char* k, bool v) { put(k, v); }
  void putString(const char* k, const String& v) { all[ns][k].assign(v.s.begin(), v.s.end()); }
  String getString(const char* k, const char* d) { auto& m = all[ns]; if (!m.count(k)) return String(d); return String(std::string(m[k].begin(), m[k].end())); } bool getBool(const char* k, bool d) { return get(k, d); }
  void putBytes(const char* k, const void* p, size_t l) { all[ns][k].assign((const uint8_t*)p, (const uint8_t*)p + l); }
  size_t getBytesLength(const char* k) { return all[ns].count(k) ? all[ns][k].size() : 0; }
  void getBytes(const char* k, void* p, size_t l) { memcpy(p, all[ns][k].data(), l); }
};
std::map<std::string, std::map<std::string, std::vector<uint8_t>>> Preferences::all;
// ---- fake 9.8 MB storage ----
static std::map<std::string, std::string> g_files;
#define FILE_APPEND "a"
struct File {
  std::string name; size_t pos = 0; bool ok = false; bool app = false;
  explicit operator bool() const { return ok; }
  bool available() { return pos < g_files[name].size(); }
  String readStringUntil(char c) { std::string& d = g_files[name]; size_t e = d.find(c, pos); std::string r = d.substr(pos, e == std::string::npos ? std::string::npos : e - pos); pos = e == std::string::npos ? d.size() : e + 1; return String(r); }
  void println(const char* x) { g_files[name] += std::string(x) + "\n"; }
  void printf(const char* f, ...) { char b[600]; va_list a; va_start(a, f); vsnprintf(b, 600, f, a); va_end(a); g_files[name] += b; }
  size_t size() { return g_files[name].size(); } void close() {}
};
struct FS {
  bool begin(bool, const char*, int, const char*) { return true; }
  bool exists(const char* n) { return g_files.count(n); }
  File open(const char* n, const char* m) { File f; f.name = n; if (std::string(m) == "r" && !g_files.count(n)) return f; if (!g_files.count(n)) g_files[n] = ""; f.ok = true; return f; }
  void remove(const char* n) { g_files.erase(n); }
  void rename(const char* a, const char* b) { g_files[b] = g_files[a]; g_files.erase(a); }
  size_t totalBytes() { return 9830400; } size_t usedBytes() { size_t t = 0; for (auto& f : g_files) t += f.second.size(); return t; }
} LittleFS;
// ---- other hardware stand-ins ----
struct IP { std::string a = "192.168.0.5"; String toString() { return String(a); } };
#define WL_CONNECTED 3
enum { WIFI_STA, WIFI_PS_MIN_MODEM, WIFI_PS_NONE, WIFI_OFF };
struct WiFiT { int st = 3; int status() { return st; } IP localIP() { return IP(); } int RSSI() { return -48; } void disconnect(bool = false) {} void begin(const char*, const char*) {} void mode(int) {} void setSleep(int) {} } WiFi;
#define HEX 16
struct WiFiClient { void setTimeout(int) {} explicit operator bool() const { return false; } bool connected() { return false; } int available() { return 0; }
  String readStringUntil(char) { return String(""); } void print(const String&) {} void print(const char*) {} void stop() {} };
enum { ESP_COEX_PREFER_BT = 1 }; void esp_coex_preference_set(int) {}
struct WiFiServer { WiFiServer(int) {} void begin() {} void end() {} WiFiClient available() { return WiFiClient(); } };
struct WiFiClientSecure { void setInsecure() {} void setTimeout(int) {} bool connect(const char*, int) { return false; } void print(const String&) {} bool connected() { return false; } String readStringUntil(char) { return String(""); } void stop() {} };
void localtime_r_dummy();
#include <cmath>
using std::isnan;
template <class A, class B> A min(A a, B b) { return a < (A)b ? a : (A)b; }
void v9CloudLoop(); void v9Setup(); void v9OnEvent(uint8_t, const char*, const char*);
float temperatureRead() { return 41.5f; }
int g_digital[64]; int digitalRead(int p) { return g_digital[p]; } void pinMode(int, int) {}
#define INPUT_PULLDOWN 9
struct WireT { int addrOk = 0; uint8_t last = 0; int16_t raw = 1234;
  void beginTransmission(uint8_t a) { last = a; } void write(uint8_t) {} int endTransmission() { return last == addrOk ? 0 : 2; }
  int requestFrom(int a, int n) { return a == addrOk ? n : 0; } int rd = 0; int read() { return (rd++ % 2 == 0) ? (raw >> 8) & 0xFF : raw & 0xFF; } } Wire;
struct ESPT { uint32_t getFreeHeap() { return 150000; } uint32_t getMinFreeHeap() { return 120000; } uint32_t getFreePsram() { return 7000000; } uint32_t getPsramSize() { return 8388608; } uint32_t getFreeSketchSpace() { return 6291456; } uint32_t getSketchSize() { return 3359497; } } ESP;
struct nvs_stats_t { size_t used_entries, free_entries, total_entries, namespace_count; };
int nvs_get_stats(const char*, nvs_stats_t* s) { s->used_entries = 120; s->total_entries = 630; return 0; }
#define ESP_OK 0
void configTime(long, int, const char*, const char*) {}
typedef unsigned char byte;
struct MFRC522 { enum { VersionReg = 0x37 }; byte PCD_ReadRegister(int) { return 0x82; } } rfid;
// ---- v6 globals the v8 block reads ----
struct Student { const char* uid; const char* name; };
// ---- FreeRTOS stand-ins (queues work for real, the network task is driven by the test) ----
#include <deque>
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x)
#define portEXIT_CRITICAL(x)
#define pdTRUE 1
struct Q { std::deque<std::vector<uint8_t>> d; size_t sz, cap; };
typedef Q* QueueHandle_t;
QueueHandle_t xQueueCreate(int n, size_t sz) { Q* q = new Q; q->sz = sz; q->cap = n; return q; }
int xQueueSend(QueueHandle_t q, const void* p, int) { if (q->d.size() >= q->cap) return 0; q->d.emplace_back((const uint8_t*)p, (const uint8_t*)p + q->sz); return 1; }
int xQueueReceive(QueueHandle_t q, void* p, int) { if (q->d.empty()) return 0; memcpy(p, q->d.front().data(), q->sz); q->d.pop_front(); return 1; }
void xTaskCreatePinnedToCore(void (*)(void*), const char*, int, void*, int, void*, int) {}
void vTaskDelay(int) {}
#define pdMS_TO_TICKS(x) (x)
std::vector<std::string> g_screens; int g_opened = 0, g_logged = 0;
void screen(const String& a, const String& b, const String& c, const String& d, const String& e) { g_screens.push_back(a.s + "|" + b.s); }
void chirp() {} void savedSignal() {} void standby() {} void successSignalAndOpenDoor() { g_opened++; }
void logAccess(const char*, const char*) { g_logged++; }
std::vector<std::string> g_cmds; void handleCommandText(String c) { g_cmds.push_back(c.s); }
Student students[] = { { "4E 09 5E F8", "sarthikreddy" }, { "86 E7 01 02", "goutam" }, { "PASTE_ROZY_CARD_UID_HERE", "rozy" } };
#define NUM_STUDENTS ((int)(sizeof(students) / sizeof(students[0])))
bool hasFace[3] = {true, true, false}, markedPresent[3] = {true, false, false}; unsigned long lockedUntil[3] = {0, 0, 0};
int presentCount = 1, visitorCount = 2, accessCount = 5; bool cameraOK = true, bleOK = true; int bleUnlockRssi = -45;
#define BLE_MAX_LINKS 3
#define MAX_PHONES 3
struct PhoneSession { bool active; char name[16]; bool authorized; float ema; int lastRssi; bool armed; } sessions[BLE_MAX_LINKS] = {{true, "sarthik", true, -52, -50, true}, {false, ""}, {false, ""}};
bool pairWindowOpen = false;
int getCpuFrequencyMhz() { return 240; }
int countPhones() { return 1; } int countFacesSaved() { return 2; }
int findStudent(const String& u) { for (int i = 0; i < 3; i++) if (u.s == students[i].uid) return i; return -1; }
int findStudentByName(const String& n) { for (int i = 0; i < 3; i++) if (n.s == students[i].name) return i; return -1; }
