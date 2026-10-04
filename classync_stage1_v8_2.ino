/*
  CLASSYNC 4.0 - STAGE 1 (VERSION 8.1)
  RFID card + ON-BOARD face recognition (Edge AI) + attendance + servo door
  + VISITOR QR PASSES (one-time use, signed, fully offline)
  + HANDS-FREE PHONE KEY (Bluetooth LE, bonded + encrypted, near-door only)
  Needs: esp32 board package version 2.0.17 (NOT 3.x)
  No Wi-Fi and no laptop needed: everything runs on the ESP32-S3.

  FEATURES
  - Faces saved PERMANENTLY with their cards (survive unplugging and re-uploading)
  - Enrollment takes 3 photos and averages them for better accuracy
  - One face can only belong to ONE card (blocks proxy enrollment)
  - Card locked for 30 s after 3 wrong faces in a row
  - Match   -> BOTH LEDs ON + double chirp + door opens instantly
  - Warning -> ONLY red LED blinks + loud pulsing alarm
  - Visitors: show a QR pass (made by make_pass.py) to the camera.
      real pass, first time -> door opens, pass is marked USED forever
      used pass            -> PASS USED alarm
      fake / edited pass   -> FAKE PASS alarm
  - Phone key (for teachers, room owners, surgeons, people with disabilities):
      the admin pairs a phone ONCE (code shown on OLED). After that the phone
      reconnects by itself like headphones - no code, no app, no buttons.
      The door opens only when the phone is within ~30 cm of the board.
      Only phones approved by the admin can open. Lost phone? removephone <name>.

  HOW IT WORKS
  - Tap a registered card the FIRST time -> the board saves that person's face.
  - Tap the same card again later         -> the board checks the face.

  SERIAL MONITOR COMMANDS (type, then press Enter)
    help            -> show all commands
    list            -> which cards have a face saved
    report          -> today's attendance (present / absent)
    reset           -> start a new day (clears attendance, keeps faces)
    forget <name>   -> delete ONE student's face (e.g. forget goutam)
    clear           -> delete ALL saved faces
    visitors        -> visitors let in since power-on
    clearpasses     -> forget used passes (old passes work again, for testing)
    addphone <name> -> allow ONE new phone for 60 s (admin, one time per phone)
    phones          -> paired phones and their live signal
    removephone <n> -> remove a phone (lost / stolen / left the job)
    resetphones     -> delete ALL phone keys
    blecal          -> measure 30 cm distance automatically (saved)
    bleopen <dBm>   -> set opening level by hand, e.g. bleopen -50 (saved)
    blelive         -> print live signal strength every second (on/off)
    log             -> last 20 door openings (card+face, QR, phone)

  NEEDS LIBRARY: NimBLE-Arduino by h2zero, version 1.4.3 (NOT 2.x)

  THIS SKETCH FOLDER NEEDS 3 FILES:
    classync_stage1_v8_1.ino (this file = v6 door system + website link)
    partitions.csv           (same 6 MB layout as v7/v8: keeps faces, phones, passes, R2 data)
    quirc.h                  (QR decoder)
    quirc_lib.c              (QR decoder)
*/

#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#include <vector>
#include <list>
#include <string>
#include <math.h>
#include "esp_camera.h"
#include "img_converters.h"
#include "human_face_detect_msr01.hpp"
#include "human_face_detect_mnp01.hpp"
#include "face_recognition_tool.hpp"
#include "face_recognition_112_v1_s8.hpp"
#include <string.h>
#include "mbedtls/md.h"
#include "quirc.h"
#include "freertos/queue.h"
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include "nimble/nimble/host/include/host/ble_hs.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "esp_heap_caps.h"
#include "mbedtls/platform.h"
#include <LittleFS.h>
#include <time.h>
#include "nvs.h"
#include "esp_coexist.h"

#include <Wire.h>
#include <SPI.h>
#include <Preferences.h>
#include <MFRC522.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ================= 1. YOUR STUDENTS (card UID -> name) =================
// UIDs come from the Serial Monitor when you tap a card. Names: no spaces.
struct Student { const char* uid; const char* name; };
Student students[] = {
  { "4E 09 5E F8", "sarthikreddy" },
  { "86 E7 01 02", "goutam" },
  { "PASTE_ROZY_CARD_UID_HERE", "rozy" },
};
#define NUM_STUDENTS ((int)(sizeof(students) / sizeof(students[0])))

// ================= 2. SETTINGS =================
#define MIN_SIMILARITY    0.55   // face must score at least this to be accepted
#define ENROLL_SAMPLES    3      // photos taken during enrollment
#define MAX_WRONG_TRIES   3      // wrong faces in a row before the card is locked
#define LOCK_TIME_MS      30000  // how long a card stays locked
#define DOOR_LOCKED       0      // servo angle when locked
#define DOOR_OPEN         90     // servo angle when open
#define DOOR_OPEN_MS      3000   // door stays open 3 seconds

// Alarm lengths (number of red-LED + buzzer pulses)
#define PULSES_UNKNOWN_CARD  8
#define PULSES_WRONG_FACE    8
#define PULSES_CARD_LOCKED   6
#define PULSES_SECURITY      16
#define PULSES_NO_FACE       3
#define PULSES_FAKE_PASS     12
#define PULSES_USED_PASS     8
#define PULSES_OTHER_QR      3

// ================= 2b. VISITOR QR PASS SETTINGS =================
// QR_SECRET (visitor-pass signing key) lives in secrets.h - never in this file
#define QR_SIG_LEN       12
#define QR_NAME_MAX      12
#define QR_ID_LEN        6

enum QrResult { QR_NONE = 0, QR_VALID, QR_NOT_CLASSYNC, QR_FAKE };
#define QR_SCAN_EVERY_MS     250    // look for a QR code 4 times per second
#define QR_REPEAT_IGNORE_MS  6000   // same QR held in front of camera: ignore for 6 s
#define VISITOR_LOG_SIZE     10

// ================= 2c. PHONE KEY (Bluetooth) SETTINGS =================
#define BLE_DEVICE_NAME          "CLASSYNC-DOOR"  // name shown in the phone's Bluetooth list
#define MAX_PHONES               3        // phones that can be paired (library limit)
#define BLE_MAX_LINKS            3        // phones connected at the same time
#define BLE_UNLOCK_RSSI_DEFAULT  -50      // open when smoothed signal is stronger (about 30 cm; tune with blecal)
#define BLE_REARM_GAP            10       // signal must drop this many dB (move away) before next opening
#define BLE_NEAR_SAMPLES         3        // strong readings in a row needed (~0.75 s)
#define BLE_POLL_MS              250      // read signal strength 4 times per second
#define BLE_COOLDOWN_MS          10000    // same phone cannot open again within 10 s
#define BLE_PAIR_WINDOW_MS       60000    // pairing mode stays open for 60 s
#define BLE_AUTH_TIMEOUT_MS      12000    // unknown devices are dropped after 12 s
#define BLE_ALARM_GAP_MS         30000    // Bluetooth security alarm at most once every 30 s
#define PULSES_BLE_DENIED        4
#define ACCESS_LOG_SIZE          20

// ================= v8.1: WEBSITE LINK SETTINGS (already filled in) =================
#include "secrets.h"                           // Wi-Fi passwords, phone channel, door key, cloud keys (NOT shared)
#ifndef QR_SECRET
#error "Add to secrets.h:  #define QR_SECRET \"<same secret as SECRET_KEY in make_pass.py>\""
#endif
// ---- v8.2: sensor slots + cloud timing (free-plan safe) ----
#define V9_SLOTS         15
#define V9_CMD_POLL_MS   15000    // website buttons checked every 10 s  (~17k Firestore reads/day incl. rule checks, of 50k free)
#define V9_STATUS_MS     30000    // heartbeat + sensors + OLED + Serial lines every 30 s (~3k writes/day of 20k free)
#define V9_TS_MS         20000    // ThingSpeak: channels A and B take turns -> each every 40 s (free minimum 15 s)
#define V9_BLYNK_MS      240000   // Blynk every 4 min: 6 values + button (~76k of 100k messages/month)
#define V9_CON_LINES     12       // Serial Monitor lines kept for the website
#define V9_CON_W         120
#define V9_TG_POLL_MS    30000    // Telegram /status /open checked every 15 s
enum V9If : uint8_t { V9_NONE, V9_BUILTIN, V9_DIGITAL, V9_ADS1115, V9_I2C, V9_UART, V9_SPI };
enum V9St : uint8_t { V9_OFF, V9_OK, V9_FAULT, V9_NODATA };
struct V9Slot {
  const char* id; const char* type; const char* unit; uint8_t iface; int8_t pin; uint8_t addr; uint8_t ch;
  uint32_t everyMs; bool enabled; float minV, maxV, scale, offset;
  float value; uint8_t status; uint32_t lastRead; uint16_t fails; uint32_t at; bool ready;
};
struct V9Svc;
void v9OnEvent(uint8_t type, const char* name, const char* detail);
void v9OledSet(const String& s, const String& a, const String& b, const String& c, const String& d);
#ifndef V9_HOST_TEST
// Serial Monitor copy for the website: every Serial line still goes to the USB Serial Monitor exactly as before,
// and the last 40 lines are also kept (safe from both CPU cores) so the website can show them.
template <class S> class V9Tee : public Stream {
 public:
  S& real;
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  char cur[V9_CON_W]; uint8_t curLen = 0;
  char lines[V9_CON_LINES][V9_CON_W];
  volatile uint32_t total = 0;
  V9Tee(S& r) : real(r) {}
  void begin(unsigned long baud) { real.begin(baud); }
  size_t write(uint8_t c) override { take(c); return real.write(c); }
  size_t write(const uint8_t* b, size_t n) override { for (size_t i = 0; i < n; i++) take(b[i]); return real.write(b, n); }
  int available() override { return real.available(); }
  int read() override { return real.read(); }
  int peek() override { return real.peek(); }
  void flush() override { real.flush(); }
  operator bool() { return true; }
  void take(uint8_t c) {
    portENTER_CRITICAL(&mux);
    if (c == '\n') { cur[curLen] = 0; memcpy(lines[total % V9_CON_LINES], cur, curLen + 1); total++; curLen = 0; }
    else if (c != '\r' && curLen < V9_CON_W - 1) cur[curLen++] = (c >= 32 && c < 127) ? c : '?';   // plain text only
    portEXIT_CRITICAL(&mux);
  }
  void copyLine(uint32_t k, char* out) { portENTER_CRITICAL(&mux); memcpy(out, lines[k % V9_CON_LINES], V9_CON_W); portEXIT_CRITICAL(&mux); out[V9_CON_W - 1] = 0; }
};
V9Tee<decltype(Serial)> v9Console(Serial);
#define Serial v9Console
#endif
extern V9Slot v9Slots[V9_SLOTS];
const char* v9StatusWord(uint8_t s);
#define TZ_OFFSET_SEC  19800                          // India = UTC + 5 h 30 m
#define DEVICE_ID      "door-1"
enum V8Type : uint8_t { V8_ENTRY_FACE = 1, V8_ENTRY_PHONE, V8_ENTRY_QR, V8_UNKNOWN_CARD, V8_CARD_LOCKED,
                        V8_FACE_MISMATCH, V8_SECURITY_LOCK, V8_NO_FACE, V8_QR_FAKE, V8_QR_USED,
                        V8_PROXY_BLOCKED, V8_ENROLLED, V8_PHONE_PAIRED, V8_BLE_REFUSED, V8_BOOT,
                        V8_REMOTE_OPEN, V8_CARD_LEARNED, V8_COMMAND };
struct V8Event {
  uint32_t seq;
  uint32_t when;          // real time (0 if the clock was not set yet)
  uint32_t up;            // seconds since power-on
  uint8_t type;
  int8_t person;          // index in students[], or -1
  char name[20];
  char detail[28];
};
struct V8Stat {           // per student, kept forever
  uint32_t first;
  uint32_t last;
  uint16_t entries;       // authenticated entries (card+face or phone)
  uint16_t rejects;       // wrong face on this person's card
  uint16_t locks;         // security locks of this card
  uint16_t noface;        // card tapped but no face seen
};
void v8Event(uint8_t type, int person, const char* name, const char* detail);
void v8LoadCards();
int v8LearnCard(const String& uid);
bool v8QrRead(struct quirc* q, const uint8_t* rgb, int w, int h, char* out, int outLen, bool all);

// ================= 3. PINS (same wiring you already have) =================
#define OLED_SDA   41
#define OLED_SCL   42
#define SERVO_PIN  2
#define GREEN_LED  1
#define RED_LED    3
#define BUZZER     46

#define RFID_SS    21
#define RFID_SCK   47
#define RFID_MOSI  48
#define RFID_MISO  14
#define RFID_RST   255   // RC522 RST is wired to 3.3V

#define SERVO_CH   2     // PWM channel for the servo

// Camera pins of this board (ESP32-S3-EYE layout). Do NOT change.
#define CAM_XCLK  15
#define CAM_SIOD  4
#define CAM_SIOC  5
#define CAM_Y9    16
#define CAM_Y8    17
#define CAM_Y7    18
#define CAM_Y6    12
#define CAM_Y5    10
#define CAM_Y4    8
#define CAM_Y3    9
#define CAM_Y2    11
#define CAM_VSYNC 6
#define CAM_HREF  7
#define CAM_PCLK  13

#define RGB_BUF_SIZE (240 * 240 * 3)
#define MAX_FACE_IDS 128

// ================= objects and state =================
MFRC522 rfid(RFID_SS, RFID_RST);
Adafruit_SSD1306 display(128, 64, &Wire, -1);
Preferences faceStore;                 // permanent face memory
FaceRecognition112V1S8 recognizer;

uint8_t* rgbBuf = NULL;
int frameW = 240;
int frameH = 240;
bool oledOK = false;
bool cameraOK = false;
int presentCount = 0;
unsigned long lastIdleDraw = 0;

bool hasFace[NUM_STUDENTS];
bool markedPresent[NUM_STUDENTS];
int wrongTries[NUM_STUDENTS];
unsigned long lockedUntil[NUM_STUDENTS];
int idToStudent[MAX_FACE_IDS];         // face ID -> student index

// Visitor QR passes
Preferences passStore;                 // remembers used pass IDs forever
struct quirc* qrReader = NULL;
unsigned long lastQrScan = 0;
unsigned long lastQrTime = 0;
char lastQrText[64] = "";
int visitorCount = 0;
String visitorLog[VISITOR_LOG_SIZE];

// Phone key (Bluetooth)
struct PhoneSession {            // a phone that is connected right now
  bool active;
  bool authorized;               // encrypted with a key made WITH the OLED code
  bool needId;                   // still reading the phone's permanent ID
  uint16_t conn;
  unsigned long idAt;
  char name[13];
  float ema;                     // smoothed signal strength
  int8_t lastRssi;
  int nearCount;
  bool armed;                    // false after opening, until the phone moves away
  unsigned long connectedAt;
  unsigned long lastUnlock;
};
enum BleEventType : uint8_t {
  BLE_EV_CONNECT = 1, BLE_EV_AUTH, BLE_EV_DISCONNECT, BLE_EV_PASSKEY, BLE_EV_PAIR_REJECT
};
struct BleEvent {                // message from the Bluetooth task to loop()
  uint8_t type;
  uint16_t conn;
  uint8_t encrypted;
  uint8_t authenticated;
  uint8_t bonded;
  uint32_t passkey;
};
struct AccessEntry {             // one door opening, for the audit log
  unsigned long t;
  char method[11];
  char name[13];
};

Preferences phoneStore;
PhoneSession sessions[BLE_MAX_LINKS];
NimBLEServer* bleServer = NULL;
QueueHandle_t bleQueue = NULL;
bool bleOK = false;
volatile bool pairWindowOpen = false;
unsigned long pairUntil = 0;
unsigned long lastPairDraw = 0;
char pairName[13] = "";
uint32_t pairPasskey = 0;
int bleUnlockRssi = BLE_UNLOCK_RSSI_DEFAULT;
bool bleLive = false;
unsigned long lastBlePoll = 0;
unsigned long lastLivePrint = 0;
unsigned long lastBleAlarm = 0;
AccessEntry accessLog[ACCESS_LOG_SIZE];
int accessCount = 0;

// HID report map: a "remote" with one volume key that is NEVER pressed.
// Being a HID device makes Android and iPhone reconnect automatically in the background.
static const uint8_t hidReportMap[] = {
  0x05, 0x0C,   // Usage Page (Consumer)
  0x09, 0x01,   // Usage (Consumer Control)
  0xA1, 0x01,   // Collection (Application)
  0x85, 0x01,   //   Report ID (1)
  0x15, 0x00,   //   Logical Minimum (0)
  0x25, 0x01,   //   Logical Maximum (1)
  0x75, 0x01,   //   Report Size (1)
  0x95, 0x01,   //   Report Count (1)
  0x09, 0xE9,   //   Usage (Volume Increment)
  0x81, 0x02,   //   Input (Data, Variable, Absolute)
  0x95, 0x07,   //   Report Count (7) - padding
  0x81, 0x03,   //   Input (Constant)
  0xC0          // End Collection
};

// Header stored in front of each saved face
struct EmbHeader {
  uint16_t n;        // number of values
  uint8_t ndim;      // number of shape dimensions
  uint8_t pad;
  int16_t dims[4];   // shape
};

// ================= outputs: servo, LEDs, buzzer =================
void servoWrite(int angle) {
  int pulseUs = map(angle, 0, 180, 500, 2400);
  uint32_t duty = (uint32_t)pulseUs * 16384UL / 20000UL;
  ledcWrite(SERVO_CH, duty);
}

void allLedsOff() {
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, LOW);
}

void beep(int ms) {
  digitalWrite(BUZZER, HIGH);
  delay(ms);
  digitalWrite(BUZZER, LOW);
}

// Warning: ONLY the red LED blinks together with a loud pulsing buzzer
void warningAlarm(int pulses) {
  digitalWrite(GREEN_LED, LOW);
  for (int i = 0; i < pulses; i++) {
    digitalWrite(RED_LED, HIGH);
    digitalWrite(BUZZER, HIGH);
    delay(150);
    digitalWrite(RED_LED, LOW);
    digitalWrite(BUZZER, LOW);
    delay(90);
  }
  allLedsOff();
}

// Small attention chirp (before a face check or enrollment photo)
void chirp() {
  beep(60);
}

// Success: door opens instantly, BOTH LEDs on, double chirp
void successSignalAndOpenDoor() {
  servoWrite(DOOR_OPEN);               // door first, so it opens with no delay
  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(RED_LED, HIGH);
  beep(80);
  delay(60);
  beep(80);
  delay(DOOR_OPEN_MS - 220);
  servoWrite(DOOR_LOCKED);
  allLedsOff();
}

// Enrollment saved: green LED blinks twice with chirps
void savedSignal() {
  for (int i = 0; i < 2; i++) {
    digitalWrite(GREEN_LED, HIGH);
    beep(90);
    delay(120);
    digitalWrite(GREEN_LED, LOW);
    delay(120);
  }
}

// ================= OLED =================
String fit(const String& s) {
  if (s.length() > 21) return s.substring(0, 21);
  return s;
}

int countFacesSaved() {
  int c = 0;
  for (int i = 0; i < NUM_STUDENTS; i++) {
    if (hasFace[i]) c++;
  }
  return c;
}

// Layout: header bar, big status word, then 4 detail lines
void screen(const String& status, const String& l1, const String& l2, const String& l3, const String& l4) {
  v9OledSet(status, l1, l2, l3, l4);   // v8.2.1: OLED mirror on the website
  Serial.println("[OLED] " + status + " | " + l1 + " | " + l2 + " | " + l3 + " | " + l4);
  if (!oledOK) return;

  display.clearDisplay();

  // Header bar (inverted): title + present count
  display.fillRect(0, 0, 128, 10, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 1);
  display.print("CLASSYNC 4.0");
  String counts = "P:" + String(presentCount) + "/" + String(NUM_STUDENTS);
  display.setCursor(126 - counts.length() * 6, 1);
  display.print(counts);

  // Big status word (max 10 characters)
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(0, 12);
  display.print(status.length() > 10 ? status.substring(0, 10) : status);

  // Four detail lines
  display.setTextSize(1);
  display.setCursor(0, 30);
  display.print(fit(l1));
  display.setCursor(0, 39);
  display.print(fit(l2));
  display.setCursor(0, 48);
  display.print(fit(l3));
  display.setCursor(0, 57);
  display.print(fit(l4));

  display.display();
}

void standby() {
  unsigned long mins = millis() / 60000UL;
  (void)mins;
  screen("READY",
         "Card, QR or Phone",
         "Faces " + String(countFacesSaved()) + "/" + String(NUM_STUDENTS) +
           "  Phones " + String(countPhones()) + "/" + String(MAX_PHONES),
         "Visitors today: " + String(visitorCount),
         "Door LOCKED  Linked:" + String(countLinkedPhones()));
  lastIdleDraw = millis();
}

// ================= RFID helpers =================
String readUID() {
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) uid += " ";
  }
  uid.toUpperCase();
  return uid;
}

int findStudent(const String& uid) {
  for (int i = 0; i < NUM_STUDENTS; i++) {
    if (uid == students[i].uid) return i;
  }
  return -1;
}

int findStudentByName(const String& name) {
  for (int i = 0; i < NUM_STUDENTS; i++) {
    if (name.equalsIgnoreCase(students[i].name)) return i;
  }
  return -1;
}

// Memory key for a card: UID without spaces (max 15 characters)
String keyFor(const String& uid) {
  String k = uid;
  k.replace(" ", "");
  if (k.length() > 15) k = k.substring(0, 15);
  return k;
}

void mapFaceId(int faceId, int studentIdx) {
  if (faceId >= 0 && faceId < MAX_FACE_IDS) idToStudent[faceId] = studentIdx;
}

int studentForFaceId(int faceId) {
  if (faceId < 0 || faceId >= MAX_FACE_IDS) return -1;
  return idToStudent[faceId];
}

// ================= permanent face memory =================
// Saves one face (already normalized, values between -1 and 1)
bool saveFaceToMemory(const String& key, const float* emb, int n, const std::vector<int>& shape) {
  size_t bytes = sizeof(EmbHeader) + (size_t)n * sizeof(int16_t);
  uint8_t* buf = (uint8_t*)malloc(bytes);
  if (!buf) return false;

  EmbHeader* h = (EmbHeader*)buf;
  h->n = (uint16_t)n;
  h->ndim = (uint8_t)((shape.size() < 4) ? shape.size() : 4);
  h->pad = 0;
  for (int d = 0; d < 4; d++) h->dims[d] = (d < h->ndim) ? (int16_t)shape[d] : 0;

  int16_t* v = (int16_t*)(buf + sizeof(EmbHeader));
  for (int j = 0; j < n; j++) {
    float x = emb[j];
    if (x > 1.0f) x = 1.0f;
    if (x < -1.0f) x = -1.0f;
    v[j] = (int16_t)(x * 32767.0f + (x >= 0 ? 0.5f : -0.5f));
  }

  size_t written = faceStore.putBytes(key.c_str(), buf, bytes);
  free(buf);
  return written == bytes;
}

// Loads every saved face into the recognizer at start-up
int loadFacesFromMemory() {
  int loaded = 0;
  for (int i = 0; i < NUM_STUDENTS; i++) {
    String key = keyFor(students[i].uid);
    size_t len = faceStore.getBytesLength(key.c_str());
    if (len < sizeof(EmbHeader)) continue;

    uint8_t* buf = (uint8_t*)malloc(len);
    if (!buf) continue;
    faceStore.getBytes(key.c_str(), buf, len);

    EmbHeader* h = (EmbHeader*)buf;
    int n = h->n;
    if (n <= 0 || len != sizeof(EmbHeader) + (size_t)n * sizeof(int16_t)) {
      Serial.printf("  %s: saved face is damaged, please re-enroll\n", students[i].name);
      free(buf);
      continue;
    }

    // Kept for as long as the board runs (the recognizer uses it)
    float* f = (float*)ps_malloc(n * sizeof(float));
    if (!f) {
      free(buf);
      continue;
    }
    int16_t* v = (int16_t*)(buf + sizeof(EmbHeader));
    for (int j = 0; j < n; j++) f[j] = v[j] / 32767.0f;

    std::vector<int> shape;
    for (int d = 0; d < h->ndim && d < 4; d++) shape.push_back(h->dims[d]);
    free(buf);

    dl::Tensor<float> t;
    t.set_element(f).set_shape(shape).set_auto_free(false);
    int id = recognizer.enroll_id(t, std::string(students[i].name), false);
    if (id >= 0) {
      mapFaceId(id, i);
      hasFace[i] = true;
      loaded++;
      Serial.printf("  %s: face loaded\n", students[i].name);
    } else {
      Serial.printf("  %s: could not load saved face, please re-enroll\n", students[i].name);
    }
  }
  return loaded;
}

// ================= camera + face AI =================
bool initCamera() {
  camera_config_t config = {};
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = CAM_Y2;
  config.pin_d1 = CAM_Y3;
  config.pin_d2 = CAM_Y4;
  config.pin_d3 = CAM_Y5;
  config.pin_d4 = CAM_Y6;
  config.pin_d5 = CAM_Y7;
  config.pin_d6 = CAM_Y8;
  config.pin_d7 = CAM_Y9;
  config.pin_xclk = CAM_XCLK;
  config.pin_pclk = CAM_PCLK;
  config.pin_vsync = CAM_VSYNC;
  config.pin_href = CAM_HREF;
  config.pin_sscb_sda = CAM_SIOD;
  config.pin_sscb_scl = CAM_SIOC;
  config.pin_pwdn = -1;
  config.pin_reset = -1;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_RGB565;   // GC2145 camera
  config.frame_size = FRAMESIZE_240X240;
  config.jpeg_quality = 12;
  config.fb_count = 2;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init FAILED, error 0x%x\n", err);
    return false;
  }
  sensor_t* s = esp_camera_sensor_get();
  Serial.printf("Camera OK, sensor PID 0x%04X\n", s->id.PID);
  return true;
}

// Takes one photo and looks for a face. The photo stays in rgbBuf.
bool captureFace(std::vector<int>& landmarks) {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return false;
  if ((int)(fb->width * fb->height * 3) > RGB_BUF_SIZE) {
    esp_camera_fb_return(fb);
    return false;
  }
  frameW = fb->width;
  frameH = fb->height;
  bool ok = fmt2rgb888(fb->buf, fb->len, fb->format, rgbBuf);
  esp_camera_fb_return(fb);
  if (!ok) return false;

  static HumanFaceDetectMSR01 s1(0.1F, 0.5F, 10, 0.2F);
  static HumanFaceDetectMNP01 s2(0.5F, 0.3F, 5);
  std::list<dl::detect::result_t>& candidates = s1.infer(rgbBuf, { frameH, frameW, 3 });
  std::list<dl::detect::result_t>& results = s2.infer(rgbBuf, { frameH, frameW, 3 }, candidates);
  if (results.empty()) return false;

  landmarks = results.front().keypoint;
  return true;
}

// ================= enrollment (first tap of a card) =================
void enrollFace(int idx) {
  const char* name = students[idx].name;
  String key = keyFor(students[idx].uid);
  screen("ENROLL", String("New: ") + name, "Look at the camera", "30-50 cm, good light", "Taking 3 photos...");
  chirp();
  delay(1000);

  int ids[ENROLL_SAMPLES];
  int got = 0;
  bool checkedDuplicate = false;
  unsigned long start = millis();

  while (got < ENROLL_SAMPLES && millis() - start < 15000) {
    std::vector<int> landmarks;
    if (!captureFace(landmarks)) {
      delay(10);
      continue;
    }

    dl::Tensor<uint8_t> tensor;
    tensor.set_element(rgbBuf).set_shape({ frameH, frameW, 3 }).set_auto_free(false);

    // Block proxy: this face must not already belong to another student
    if (!checkedDuplicate) {
      checkedDuplicate = true;
      if (recognizer.get_enrolled_id_num() > 0) {
        face_info_t r = recognizer.recognize(tensor, landmarks);
        int owner = studentForFaceId(r.id);
        if (owner >= 0 && owner != idx && r.similarity >= MIN_SIMILARITY) {
          v8Event(V8_PROXY_BLOCKED, idx, students[idx].name, (String("face of ") + students[owner].name).c_str());
          Serial.printf(">>> BLOCKED: this face is already registered to %s (similarity %.2f)\n",
                        students[owner].name, r.similarity);
          screen("BLOCKED", "Face already used by", students[owner].name,
                 "One face = one card", "Door LOCKED");
          warningAlarm(PULSES_WRONG_FACE);
          return;
        }
      }
    }

    int id = recognizer.enroll_id(tensor, landmarks, std::string(name), false);
    if (id < 0) continue;
    mapFaceId(id, idx);
    ids[got] = id;
    got++;

    screen("ENROLL", String("New: ") + name,
           "Photo " + String(got) + " of " + String(ENROLL_SAMPLES) + " taken",
           "Move head slightly", "Keep looking at cam");
    chirp();
    delay(600);
  }

  if (got < ENROLL_SAMPLES) {
    screen("NO FACE", name, "Face not seen clearly", "Face the camera", "Tap card again");
    warningAlarm(PULSES_NO_FACE);
    return;
  }

  // Average the photos into one strong face template
  dl::Tensor<float>& first = recognizer.get_face_emb(ids[0]);
  int n = first.get_size();
  std::vector<int> shape = first.shape;
  float* avg = (float*)ps_malloc(n * sizeof(float));
  if (!avg) {
    screen("SAVE FAIL", name, "Out of memory", "Restart the board", "Tap card again");
    warningAlarm(PULSES_NO_FACE);
    return;
  }
  for (int j = 0; j < n; j++) avg[j] = 0.0f;

  for (int k = 0; k < got; k++) {
    dl::Tensor<float>& e = recognizer.get_face_emb(ids[k]);
    if (e.get_size() != n) {
      free(avg);
      screen("SAVE FAIL", name, "Photo data mismatch", "", "Tap card again");
      warningAlarm(PULSES_NO_FACE);
      return;
    }
    for (int j = 0; j < n; j++) avg[j] += e.element[j];
  }

  float norm = 0.0f;
  for (int j = 0; j < n; j++) norm += avg[j] * avg[j];
  norm = sqrtf(norm);
  if (norm > 0.0f) {
    for (int j = 0; j < n; j++) avg[j] /= norm;
  }

  bool saved = saveFaceToMemory(key, avg, n, shape);
  free(avg);

  if (!saved) {
    Serial.println(">>> SAVE FAILED: memory may be full. Use 'forget <name>' or 'clear'.");
    screen("SAVE FAIL", name, "Memory may be full", "Type: forget <name>", "Tap card again");
    warningAlarm(PULSES_NO_FACE);
    return;
  }

  hasFace[idx] = true;
  Serial.printf(">>> FACE SAVED PERMANENTLY: %s\n", name);
  v8Event(V8_ENROLLED, idx, name, "");
  screen("SAVED", name, "Face linked to card", "Saved permanently", "Tap again to enter");
  savedSignal();
  delay(800);
}

// ================= verification (later taps) =================
// 1 = match, 0 = wrong face, -1 = no face seen. bestSim returns the best score seen.
int verifyFace(int idx, float& bestSim) {
  int facesSeen = 0;
  bestSim = 0.0f;
  unsigned long start = millis();
  while (millis() - start < 6000) {
    std::vector<int> landmarks;
    if (!captureFace(landmarks)) {
      delay(10);
      continue;
    }
    facesSeen++;

    dl::Tensor<uint8_t> tensor;
    tensor.set_element(rgbBuf).set_shape({ frameH, frameW, 3 }).set_auto_free(false);
    face_info_t r = recognizer.recognize(tensor, landmarks);
    int owner = studentForFaceId(r.id);
    if (owner == idx && r.similarity > bestSim) bestSim = r.similarity;
    Serial.printf("   face check %d: looks like %s, similarity %.2f (need %.2f)\n",
                  facesSeen, owner >= 0 ? students[owner].name : "nobody known",
                  r.similarity, MIN_SIMILARITY);

    if (owner == idx && r.similarity >= MIN_SIMILARITY) return 1;
    if (facesSeen >= 3) return 0;
  }
  return (facesSeen == 0) ? -1 : 0;
}

void grantAccess(int idx, float sim) {
  const char* name = students[idx].name;
  String line3 = "Match score: " + String(sim, 2);
  if (!markedPresent[idx]) {
    markedPresent[idx] = true;
    presentCount++;
    Serial.printf(">>> ATTENDANCE MARKED: %s (present today: %d/%d)\n", name, presentCount, NUM_STUDENTS);
    screen("PRESENT", name, "Attendance marked", line3, "Door OPEN - go in");
  } else {
    Serial.printf(">>> %s already marked today, door opened\n", name);
    screen("WELCOME", name, "Already marked today", line3, "Door OPEN - go in");
  }
  logAccess("CARD+FACE", name);
  v8Event(V8_ENTRY_FACE, idx, name, ("match " + String(sim, 2)).c_str());
  successSignalAndOpenDoor();
}

// ================= visitor QR passes =================
// Finds and decodes one QR code in a grayscale image already loaded into quirc.
// Returns true and copies the text into out (zero-terminated).
bool qrReadText(struct quirc* q, char* out, int outLen) {
  static struct quirc_code code;   // static: too big for the stack
  static struct quirc_data data;
  int count = quirc_count(q);
  for (int i = 0; i < count; i++) {
    quirc_extract(q, i, &code);
    quirc_decode_error_t err = quirc_decode(&code, &data);
    if (err == QUIRC_ERROR_DATA_ECC) {       // maybe the camera image is mirrored
      quirc_flip(&code);
      err = quirc_decode(&code, &data);
    }
    if (err == QUIRC_SUCCESS && data.payload_len > 0 && data.payload_len < outLen) {
      memcpy(out, data.payload, data.payload_len);
      out[data.payload_len] = 0;
      return true;
    }
  }
  return false;
}

// Checks a pass text "CSY:NAME:ID:SIGNATURE". Fills name and id when the format is right.
QrResult qrCheckPass(const char* text, char* name, char* id) {
  name[0] = 0;
  id[0] = 0;
  if (strncmp(text, "CSY:", 4) != 0) return QR_NOT_CLASSYNC;

  const char* p1 = text + 4;                 // start of name
  const char* c1 = strchr(p1, ':');
  if (!c1) return QR_NOT_CLASSYNC;
  const char* p2 = c1 + 1;                   // start of pass ID
  const char* c2 = strchr(p2, ':');
  if (!c2) return QR_NOT_CLASSYNC;
  const char* sig = c2 + 1;                  // start of signature

  int nameLen = (int)(c1 - p1);
  int idLen = (int)(c2 - p2);
  if (nameLen < 1 || nameLen > QR_NAME_MAX) return QR_FAKE;
  if (idLen != QR_ID_LEN) return QR_FAKE;
  if ((int)strlen(sig) != QR_SIG_LEN) return QR_FAKE;

  memcpy(name, p1, nameLen);
  name[nameLen] = 0;
  memcpy(id, p2, idLen);
  id[idLen] = 0;

  // Signature = first 12 hex characters (capitals) of HMAC-SHA256(secret, "CSY:NAME:ID")
  int msgLen = (int)(c2 - text);
  unsigned char mac[32];
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (mbedtls_md_hmac(info, (const unsigned char*)QR_SECRET, strlen(QR_SECRET),
                      (const unsigned char*)text, msgLen, mac) != 0) {
    return QR_FAKE;
  }
  char expected[QR_SIG_LEN + 1];
  const char* hexd = "0123456789ABCDEF";
  for (int i = 0; i < QR_SIG_LEN / 2; i++) {
    expected[2 * i] = hexd[mac[i] >> 4];
    expected[2 * i + 1] = hexd[mac[i] & 0x0F];
  }
  expected[QR_SIG_LEN] = 0;

  // Compare every character (no early exit)
  unsigned char diff = 0;
  for (int i = 0; i < QR_SIG_LEN; i++) diff |= (unsigned char)(expected[i] ^ sig[i]);
  return (diff == 0) ? QR_VALID : QR_FAKE;
}

// RGB888 photo (from fmt2rgb888) -> grayscale for quirc
void qrLoadGray(struct quirc* q, const uint8_t* rgb, int w, int h) {
  int qw = 0, qh = 0;
  uint8_t* gray = quirc_begin(q, &qw, &qh);
  int n = w * h;
  if (qw * qh < n) n = qw * qh;
  for (int i = 0; i < n; i++) {
    const uint8_t* px = rgb + 3 * i;
    gray[i] = (uint8_t)((px[0] + 2 * px[1] + px[2]) >> 2);
  }
  quirc_end(q);
}

#define V8_QR_ALLOC ps_malloc     // QR work pictures live in PSRAM
// ===== v8 QR reader: v6's way first, then sharper versions of the same picture =====
// Pass 0 is exactly v6. If it finds nothing, the SAME photo is tried again with stronger contrast,
// then zoomed into the centre (small / far codes), so codes on phone screens and paper read easier.
static uint8_t* v8QrGray = NULL;     // 240x240 grey copy (in PSRAM)
static uint8_t* v8QrTmp = NULL;

static void v8QrStretch(const uint8_t* src, uint8_t* dst, int n) {
  uint32_t hist[256] = {0};
  for (int i = 0; i < n; i++) hist[src[i]]++;
  int lo = 0, hi = 255; uint32_t acc = 0, cut = n / 100;          // ignore the darkest / brightest 1 %
  while (lo < 255 && (acc += hist[lo]) <= cut) lo++;
  acc = 0;
  while (hi > 0 && (acc += hist[hi]) <= cut) hi--;
  if (hi - lo < 8) { memcpy(dst, src, n); return; }
  uint8_t lut[256];
  for (int v = 0; v < 256; v++) { int o = (v - lo) * 255 / (hi - lo); lut[v] = o < 0 ? 0 : (o > 255 ? 255 : o); }
  for (int i = 0; i < n; i++) dst[i] = lut[src[i]];
}

static void v8QrZoom(const uint8_t* src, uint8_t* dst, int w, int h, int num, int den) {  // centre crop, scale num/den
  int cw = w * den / num, ch = h * den / num, x0 = (w - cw) / 2, y0 = (h - ch) / 2;
  for (int y = 0; y < h; y++) {
    int sy256 = y * ch * 256 / h; int sy = sy256 >> 8, fy = sy256 & 255; int sy1 = sy + 1 < ch ? sy + 1 : sy;
    for (int x = 0; x < w; x++) {
      int sx256 = x * cw * 256 / w; int sx = sx256 >> 8, fx = sx256 & 255; int sx1 = sx + 1 < cw ? sx + 1 : sx;
      const uint8_t* r0 = src + (y0 + sy) * w + x0; const uint8_t* r1 = src + (y0 + sy1) * w + x0;
      int top = r0[sx] * (256 - fx) + r0[sx1] * fx, bot = r1[sx] * (256 - fx) + r1[sx1] * fx;
      dst[y * w + x] = (uint8_t)((top * (256 - fy) + bot * fy) >> 16);
    }
  }
}


static void v8QrFlatten(const uint8_t* src, uint8_t* dst, int w, int h, int r) {   // remove glare / shadows
  static uint32_t* integ = NULL;
  if (!integ) integ = (uint32_t*)V8_QR_ALLOC((w + 1) * (h + 1) * sizeof(uint32_t));
  if (!integ) { memcpy(dst, src, w * h); return; }
  for (int x = 0; x <= w; x++) integ[x] = 0;
  for (int y = 1; y <= h; y++) {
    uint32_t row = 0; integ[y * (w + 1)] = 0;
    for (int x = 1; x <= w; x++) { row += src[(y - 1) * w + x - 1]; integ[y * (w + 1) + x] = integ[(y - 1) * (w + 1) + x] + row; }
  }
  for (int y = 0; y < h; y++) {
    int y0 = y - r < 0 ? 0 : y - r, y1 = y + r + 1 > h ? h : y + r + 1;
    for (int x = 0; x < w; x++) {
      int x0 = x - r < 0 ? 0 : x - r, x1 = x + r + 1 > w ? w : x + r + 1;
      uint32_t sum = integ[y1 * (w + 1) + x1] - integ[y0 * (w + 1) + x1] - integ[y1 * (w + 1) + x0] + integ[y0 * (w + 1) + x0];
      int mean = sum / ((y1 - y0) * (x1 - x0));
      int v = src[y * w + x] * 128 / (mean + 1);                 // pixel compared with its surroundings
      v = 128 + (v - 128) * 2;                                    // and made twice as strong
      dst[y * w + x] = v < 0 ? 0 : (v > 255 ? 255 : v);
    }
  }
}

static bool v8QrTry(struct quirc* q, const uint8_t* gray, int n, char* out, int outLen) {
  int qw = 0, qh = 0;
  uint8_t* buf = quirc_begin(q, &qw, &qh);
  if (qw * qh < n) n = qw * qh;
  memcpy(buf, gray, n);
  quirc_end(q);
  return qrReadText(q, out, outLen);
}

// The ESP32 is busy, so each scan tries v6's way + ONE extra version (they take turns).
// A code held in front of the camera for ~1.5 s gets every version tried.
#define V8_QR_VARIANTS 6
static uint8_t v8QrTurn = 0;

static bool v8QrVariant(struct quirc* q, const uint8_t* rgb, int w, int h, int k, char* out, int outLen) {
  int n = w * h;
  if (!v8QrGray) { v8QrGray = (uint8_t*)V8_QR_ALLOC(n); v8QrTmp = (uint8_t*)V8_QR_ALLOC(n); }
  if (!v8QrGray || !v8QrTmp) return false;
  for (int i = 0; i < n; i++) { const uint8_t* px = rgb + 3 * i; v8QrGray[i] = (uint8_t)((px[0] + 2 * px[1] + px[2]) >> 2); }
  switch (k) {
    case 0: v8QrStretch(v8QrGray, v8QrTmp, n); break;                                        // stronger contrast
    case 1: v8QrFlatten(v8QrGray, v8QrTmp, w, h, 14); break;                                // remove glare (phone screens)
    case 2: v8QrStretch(v8QrGray, v8QrTmp, n); v8QrZoom(v8QrTmp, v8QrGray, w, h, 2, 1); memcpy(v8QrTmp, v8QrGray, n); break;  // small / far
    case 3: v8QrFlatten(v8QrGray, v8QrTmp, w, h, 14); v8QrZoom(v8QrTmp, v8QrGray, w, h, 2, 1); memcpy(v8QrTmp, v8QrGray, n); break;  // far phone
    case 4: v8QrStretch(v8QrGray, v8QrTmp, n); v8QrZoom(v8QrTmp, v8QrGray, w, h, 3, 2); memcpy(v8QrTmp, v8QrGray, n); break;  // medium
    default: v8QrFlatten(v8QrGray, v8QrTmp, w, h, 14); v8QrZoom(v8QrTmp, v8QrGray, w, h, 3, 2); memcpy(v8QrTmp, v8QrGray, n); break;
  }
  return v8QrTry(q, v8QrTmp, n, out, outLen);
}

// One scan: exactly v6 first; if nothing, one extra version (the next in turn). all=true tries every version (tests).
bool v8QrRead(struct quirc* q, const uint8_t* rgb, int w, int h, char* out, int outLen, bool all) {
  qrLoadGray(q, rgb, w, h);
  if (qrReadText(q, out, outLen)) return true;
  if (all) { for (int k = 0; k < V8_QR_VARIANTS; k++) if (v8QrVariant(q, rgb, w, h, k, out, outLen)) return true; return false; }
  int k = v8QrTurn; v8QrTurn = (v8QrTurn + 1) % V8_QR_VARIANTS;
  return v8QrVariant(q, rgb, w, h, k, out, outLen);
}

// Remember a visitor in the session log
void logVisitor(const char* name, const char* id) {
  visitorLog[visitorCount % VISITOR_LOG_SIZE] = String(name) + " (pass " + id + ")";
  visitorCount++;
}

// Takes one photo, looks for a QR pass and acts on it
void scanVisitorPass() {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return;
  if (fb->width != 240 || fb->height != 240) {
    esp_camera_fb_return(fb);
    return;
  }
  bool ok = fmt2rgb888(fb->buf, fb->len, fb->format, rgbBuf);
  esp_camera_fb_return(fb);
  if (!ok) return;

  static char text[96];
  if (!v8QrRead(qrReader, rgbBuf, 240, 240, text, sizeof(text), false)) return;   // v8.1: v6 pass + one sharper version

  // Same QR still in front of the camera: ignore it for a few seconds
  if (strcmp(text, lastQrText) == 0 && millis() - lastQrTime < QR_REPEAT_IGNORE_MS) return;
  strncpy(lastQrText, text, sizeof(lastQrText) - 1);
  lastQrText[sizeof(lastQrText) - 1] = 0;

  Serial.println("QR seen: " + String(text));
  char name[QR_NAME_MAX + 1];
  char id[QR_ID_LEN + 1];
  QrResult r = qrCheckPass(text, name, id);

  if (r == QR_NOT_CLASSYNC) {
    Serial.println(">>> QR IGNORED: not a CLASSYNC pass");
    screen("UNKNOWN QR", "Not a CLASSYNC pass", "Use a visitor pass", "made by make_pass.py", "Door LOCKED");
    warningAlarm(PULSES_OTHER_QR);
  } else if (r == QR_FAKE) {
    Serial.println(">>> SECURITY ALERT: FAKE or edited visitor pass");
    v8Event(V8_QR_FAKE, -1, "visitor", "signature wrong");
    screen("FAKE PASS", "Signature is wrong", "Pass not issued by", "CLASSYNC admin", "Door LOCKED");
    warningAlarm(PULSES_FAKE_PASS);
  } else {
    String key = String("p") + id;
    if (passStore.getString(key.c_str(), "").length() > 0) {
      Serial.printf(">>> PASS ALREADY USED: %s (pass %s)\n", name, id);
      v8Event(V8_QR_USED, -1, name, (String("pass ") + id).c_str());
      screen("PASS USED", name, "Pass " + String(id), "One-time pass expired", "Door LOCKED");
      warningAlarm(PULSES_USED_PASS);
    } else {
      passStore.putString(key.c_str(), name);   // this pass never works again
      logVisitor(name, id);
      Serial.printf(">>> VISITOR ENTRY: %s (pass %s), visitors today: %d\n", name, id, visitorCount);
      screen("VISITOR", name, "Pass " + String(id) + " accepted", "Pass now used up", "Door OPEN - go in");
      logAccess("QR PASS", name);
      v8Event(V8_ENTRY_QR, -1, name, (String("pass ") + id).c_str());
      successSignalAndOpenDoor();
    }
  }

  lastQrTime = millis();   // ignore window starts after the door has closed
  delay(300);
  standby();
}

// ================= audit log (all door openings) =================
void logAccess(const char* method, const char* name) {
  AccessEntry& e = accessLog[accessCount % ACCESS_LOG_SIZE];
  e.t = millis();
  strlcpy(e.method, method, sizeof(e.method));
  strlcpy(e.name, name, sizeof(e.name));
  accessCount++;
}

void printAccessLog() {
  Serial.println("---- Access log (time since power-on) ----");
  int shown = (accessCount < ACCESS_LOG_SIZE) ? accessCount : ACCESS_LOG_SIZE;
  for (int i = 0; i < shown; i++) {
    AccessEntry& e = accessLog[(accessCount - shown + i) % ACCESS_LOG_SIZE];
    unsigned long s = e.t / 1000;
    Serial.printf("  %02lu:%02lu:%02lu  %-10s %s\n", s / 3600, (s / 60) % 60, s % 60, e.method, e.name);
  }
  Serial.printf("  Total door openings: %d\n", accessCount);
  Serial.println("------------------------------------------");
}

// ================= phone key: Bluetooth callbacks =================
// These run inside the Bluetooth task. They only send short messages to loop(),
// so the OLED, servo and buzzer are always controlled from one place.
class DoorBleCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* s, ble_gap_conn_desc* desc) override {
    BleEvent ev = {};
    ev.type = BLE_EV_CONNECT;
    ev.conn = desc->conn_handle;
    if (bleQueue) xQueueSend(bleQueue, &ev, 0);
    NimBLEDevice::startSecurity(desc->conn_handle);        // paired phone: switch on encryption now
    if (s->getConnectedCount() < BLE_MAX_LINKS) NimBLEDevice::startAdvertising();  // room for more phones
  }

  void onDisconnect(NimBLEServer* s, ble_gap_conn_desc* desc) override {
    BleEvent ev = {};
    ev.type = BLE_EV_DISCONNECT;
    ev.conn = desc->conn_handle;
    if (bleQueue) xQueueSend(bleQueue, &ev, 0);
  }

  // The 6-digit code the phone user must type. A NEW random code every time,
  // shown on the OLED ONLY while the admin has opened pairing mode.
  uint32_t onPassKeyRequest() override {
    uint32_t key = 100000 + (esp_random() % 900000);
    BleEvent ev = {};
    ev.type = pairWindowOpen ? BLE_EV_PASSKEY : BLE_EV_PAIR_REJECT;
    ev.passkey = key;
    if (bleQueue) xQueueSend(bleQueue, &ev, 0);
    return key;
  }

  void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
    BleEvent ev = {};
    ev.type = BLE_EV_AUTH;
    ev.conn = desc->conn_handle;
    ev.encrypted = desc->sec_state.encrypted;
    ev.authenticated = desc->sec_state.authenticated;
    ev.bonded = desc->sec_state.bonded;
    if (bleQueue) xQueueSend(bleQueue, &ev, 0);
  }
};

// Never throw out an approved phone to make room for a new pairing
int doorStoreStatus(struct ble_store_status_event* event, void* arg) {
  if (event->event_code == BLE_STORE_EVENT_OVERFLOW) return BLE_HS_ENOMEM;
  return 0;
}

// ================= phone key: names of paired phones (saved permanently) =================
// The Bluetooth library keeps the secret keys of paired phones (that IS the approved list).
// Here we only keep a friendly name for each phone, stored under its permanent ID.
String addrKey(const NimBLEAddress& a) {
  String k = String(a.toString().c_str());
  k.replace(":", "");
  return "k" + k;                      // e.g. k1a2b3c4d5e6f (13 characters)
}

String nameForAddr(const NimBLEAddress& a) {
  return phoneStore.getString(addrKey(a).c_str(), "");
}

int countPhones() {
  return NimBLEDevice::getNumBonds();
}

int countLinkedPhones() {
  int c = 0;
  for (int i = 0; i < BLE_MAX_LINKS; i++) {
    if (sessions[i].active && sessions[i].authorized && !sessions[i].needId) c++;
  }
  return c;
}

bool phoneNameTaken(const String& name) {
  int n = NimBLEDevice::getNumBonds();
  for (int b = 0; b < n; b++) {
    if (name.equalsIgnoreCase(nameForAddr(NimBLEDevice::getBondedAddress(b)))) return true;
  }
  return false;
}

String autoPhoneName() {
  for (int k = 1; k <= 9; k++) {
    String nm = "PHONE" + String(k);
    if (!phoneNameTaken(nm)) return nm;
  }
  return "PHONE";
}

// ================= phone key: connections =================
int findSessionByConn(uint16_t conn) {
  for (int i = 0; i < BLE_MAX_LINKS; i++) {
    if (sessions[i].active && sessions[i].conn == conn) return i;
  }
  return -1;
}

int newSession(uint16_t conn) {
  for (int i = 0; i < BLE_MAX_LINKS; i++) {
    if (!sessions[i].active) {
      sessions[i] = PhoneSession();
      sessions[i].active = true;
      sessions[i].conn = conn;
      sessions[i].ema = -100.0f;
      sessions[i].connectedAt = millis();
      return i;
    }
  }
  return -1;
}

void dropConnection(int s, uint16_t conn) {
  if (bleServer) bleServer->disconnect(conn);
  if (s >= 0) sessions[s].active = false;
}

// Security alarms are limited to one every 30 s, so a phone retrying in the
// background can never make the buzzer ring again and again.
bool bleAlarmAllowed() {
  if (lastBleAlarm != 0 && millis() - lastBleAlarm < BLE_ALARM_GAP_MS) return false;
  lastBleAlarm = millis();
  return true;
}

// ================= phone key: pairing mode (admin, from the laptop) =================
void closePairWindow() {
  pairWindowOpen = false;
  pairPasskey = 0;
}

void drawPairScreen() {
  long left = ((long)(pairUntil - millis()) + 999) / 1000;
  if (left < 0) left = 0;
  if (pairPasskey == 0) {
    screen("PAIR PHONE", "For: " + String(pairName), "On phone: Bluetooth",
           "Pair " + String(BLE_DEVICE_NAME), "Time left: " + String(left) + " s");
  } else {
    screen("PAIR PHONE", "For: " + String(pairName), "CODE: " + String(pairPasskey),
           "Type code on phone", "Time left: " + String(left) + " s");
  }
  lastPairDraw = millis();
}

void openPairWindow(String name) {
  name.trim();
  String clean = "";
  for (unsigned int i = 0; i < name.length() && clean.length() < 12; i++) {
    char c = name[i];
    if (isalnum((unsigned char)c)) clean += c;
  }
  if (!bleOK) {
    Serial.println("Phone key is OFF (Bluetooth did not start). Check the start-up messages.");
    return;
  }
  if (clean.length() == 0) {
    Serial.println("Give the phone a name (letters/numbers), e.g.  addphone teacher");
    return;
  }
  if (phoneNameTaken(clean)) {
    Serial.println("A phone called '" + clean + "' already exists. Use: removephone " + clean);
    return;
  }
  if (countPhones() >= MAX_PHONES) {
    Serial.println("Phone list is FULL (" + String(MAX_PHONES) + "). Remove one first: removephone <name>");
    screen("LIST FULL", "3 phones already", "paired. Remove one", "with: removephone", "Door LOCKED");
    warningAlarm(PULSES_NO_FACE);
    standby();
    return;
  }
  strlcpy(pairName, clean.c_str(), sizeof(pairName));
  pairPasskey = 0;
  pairUntil = millis() + BLE_PAIR_WINDOW_MS;
  pairWindowOpen = true;
  Serial.println(">>> PAIRING MODE OPEN for 60 s, phone name: " + clean);
  Serial.println("    On the phone: Settings > Connected devices > Pair new device > " BLE_DEVICE_NAME);
  Serial.println("    Then type the 6-digit CODE shown on the OLED.");
  drawPairScreen();
  chirp();
}

// ================= phone key: hands-free opening =================
void phoneUnlock(int s) {
  PhoneSession& ss = sessions[s];
  const char* name = ss.name;
  String sig = "Signal: " + String((int)ss.ema) + " dBm";

  int st = findStudentByName(String(name));   // phone named like a student -> attendance too
  if (st >= 0 && !markedPresent[st]) {
    markedPresent[st] = true;
    presentCount++;
    Serial.printf(">>> ATTENDANCE MARKED (phone key): %s (present today: %d/%d)\n", name, presentCount, NUM_STUDENTS);
    screen("PRESENT", name, "Phone key accepted", sig, "Door OPEN - go in");
  } else {
    screen("PHONE KEY", name, "Hands-free unlock", sig, "Door OPEN - go in");
  }
  Serial.printf(">>> PHONE KEY ENTRY: %s (signal %d dBm, opens at %d)\n", name, (int)ss.ema, bleUnlockRssi);
  logAccess("PHONE KEY", name);
  v8Event(V8_ENTRY_PHONE, st, name, (String((int)ss.ema) + " dBm").c_str());
  successSignalAndOpenDoor();
  standby();
}

// Reads the phone's PERMANENT ID (only reliable a moment after encryption starts)
// and gives the phone its name. Returns true when done.
bool identifyPhone(int s) {
  PhoneSession& ss = sessions[s];
  ble_gap_conn_desc d;
  if (ble_gap_conn_find(ss.conn, &d) != 0) {
    ss.active = false;
    return true;
  }
  NimBLEAddress id(d.peer_id_addr);
  if (!NimBLEDevice::isBonded(id)) {
    if (millis() - ss.connectedAt > 10000) {
      Serial.println(">>> BLE: phone key could not be saved (list full?) -> disconnected");
      dropConnection(s, ss.conn);
      return true;
    }
    ss.idAt = millis() + 500;          // keys still being saved: check again shortly
    return false;
  }

  String nm = nameForAddr(id);
  bool newlyPaired = false;
  if (nm.length() == 0) {
    nm = pairWindowOpen ? String(pairName) : autoPhoneName();
    phoneStore.putString(addrKey(id).c_str(), nm);
    newlyPaired = pairWindowOpen;
    if (pairWindowOpen) closePairWindow();
  }
  strlcpy(ss.name, nm.c_str(), sizeof(ss.name));
  ss.needId = false;

  if (newlyPaired) {
    ss.armed = false;                  // first opening needs a fresh approach
    Serial.printf(">>> PHONE PAIRED PERMANENTLY: %s [%s]\n", ss.name, id.toString().c_str());
    v8Event(V8_PHONE_PAIRED, findStudentByName(String(ss.name)), ss.name, "");
    Serial.println("    It reconnects by itself from now on, like headphones.");
    screen("PAIRED", ss.name, "Saved permanently", "Step back, then hold", "phone near the door");
    savedSignal();
    delay(1500);
    standby();
  } else {
    Serial.printf("BLE: %s's phone linked (encrypted). Hold it within 30 cm to open.\n", ss.name);
  }
  return true;
}

// ================= phone key: messages from the Bluetooth task =================
void bleHandleEvent(const BleEvent& ev) {
  if (ev.type == BLE_EV_CONNECT) {
    if (newSession(ev.conn) < 0) dropConnection(-1, ev.conn);

  } else if (ev.type == BLE_EV_DISCONNECT) {
    int s = findSessionByConn(ev.conn);
    if (s >= 0) {
      if (sessions[s].authorized && !sessions[s].needId) {
        Serial.printf("BLE: %s's phone went out of range\n", sessions[s].name);
      }
      sessions[s].active = false;
    }

  } else if (ev.type == BLE_EV_PASSKEY) {
    pairPasskey = ev.passkey;
    Serial.printf(">>> PAIRING CODE for the phone: %06lu (also on the OLED)\n", (unsigned long)ev.passkey);
    drawPairScreen();
    chirp();

  } else if (ev.type == BLE_EV_PAIR_REJECT) {
    Serial.println(">>> A phone asked to pair, but pairing mode is CLOSED -> refused.");
    Serial.println("    If it is YOUR phone: on the phone 'Forget' CLASSYNC-DOOR, then type addphone <name> here.");
    screen("NOT PAIRED", "Phone is not paired", "Admin on laptop:", "addphone <name>", "Door LOCKED");
    if (bleAlarmAllowed()) warningAlarm(PULSES_BLE_DENIED);
    delay(800);
    standby();

  } else if (ev.type == BLE_EV_AUTH) {
    int s = findSessionByConn(ev.conn);
    if (s < 0) s = newSession(ev.conn);
    if (s < 0) {
      dropConnection(-1, ev.conn);
      return;
    }

    if (!ev.encrypted) {
      Serial.println(">>> BLE: pairing/encryption failed -> disconnected");
      if (pairWindowOpen) {
        screen("PAIR FAIL", "Wrong or no code", "Try again: tap", String(BLE_DEVICE_NAME), "on the phone");
        warningAlarm(PULSES_NO_FACE);
        pairPasskey = 0;
        drawPairScreen();
      }
      dropConnection(s, ev.conn);
      return;
    }

    if (!(ev.bonded && ev.authenticated)) {
      // Linked WITHOUT the OLED code (no protection against copying): never allowed
      Serial.println(">>> SECURITY: phone linked without the pairing code -> refused");
      v8Event(V8_BLE_REFUSED, -1, "phone", "no pairing code");
      ble_gap_conn_desc d;
      if (ble_gap_conn_find(ev.conn, &d) == 0) NimBLEDevice::deleteBond(NimBLEAddress(d.peer_id_addr));
      dropConnection(s, ev.conn);
      screen("BLE DENIED", "Phone not approved", "Admin on laptop:", "addphone <name>", "Door LOCKED");
      if (bleAlarmAllowed()) warningAlarm(PULSES_BLE_DENIED);
      standby();
      return;
    }

    // Encrypted with a key that was created WITH the OLED code: an approved phone.
    sessions[s].authorized = true;
    sessions[s].needId = true;
    sessions[s].idAt = millis() + 800;   // give the library time to finish saving
    sessions[s].armed = true;
    sessions[s].ema = -100.0f;
    sessions[s].nearCount = 0;
    sessions[s].lastUnlock = 0;
    strlcpy(sessions[s].name, "phone", sizeof(sessions[s].name));
  }
}

// ================= phone key: start-up =================
void bleSetup() {
  bleQueue = xQueueCreate(16, sizeof(BleEvent));
  for (int i = 0; i < BLE_MAX_LINKS; i++) sessions[i].active = false;
  if (bleQueue == NULL) {
    Serial.println("Phone key: not enough memory -> OFF");
    return;
  }

  NimBLEDevice::init(BLE_DEVICE_NAME);
  ble_hs_cfg.store_status_cb = doorStoreStatus;             // never evict an approved phone
  NimBLEDevice::setSecurityAuth(true, true, true);          // bonding + MITM protection + LE Secure Connections
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);   // code shown on OLED, typed on the phone
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

  // Names + settings. First start of v6: clear the old v5 pairings (they were saved wrongly).
  phoneStore.begin("classyncph6", false);
  if (!phoneStore.getBool("v6", false)) {
    NimBLEDevice::deleteAllBonds();
    Preferences oldStore;
    oldStore.begin("classyncble", false);
    oldStore.clear();
    oldStore.end();
    phoneStore.putBool("v6", true);
    Serial.println("Phone key: one-time upgrade done, old pairings cleared. Pair your phone again once.");
  }
  bleUnlockRssi = phoneStore.getInt("thr", BLE_UNLOCK_RSSI_DEFAULT);

  bleServer = NimBLEDevice::createServer();
  bleServer->setCallbacks(new DoorBleCallbacks());

  // Appear as a Bluetooth "remote control" so phones reconnect by themselves (no app needed).
  NimBLEHIDDevice* hid = new NimBLEHIDDevice(bleServer);
  hid->manufacturer(std::string("CLASSYNC"));
  hid->pnp(0x02, 0xE502, 0xA111, 0x0210);
  hid->hidInfo(0x00, 0x01);
  hid->inputReport(1);                                      // never sends anything
  hid->reportMap((uint8_t*)hidReportMap, sizeof(hidReportMap));
  hid->startServices();
  hid->setBatteryLevel(100);

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->setAppearance(0x03C0);                               // generic HID device
  adv->addServiceUUID(hid->hidService()->getUUID());
  adv->setName(BLE_DEVICE_NAME);
  adv->setMinInterval(48);                                  // 30 ms: phones reconnect quickly
  adv->setMaxInterval(80);                                  // 50 ms
  bleOK = adv->start();

  Serial.printf("Phone key (Bluetooth): %s, name %s, %d of %d phones paired, opens at %d dBm\n",
                bleOK ? "ON" : "FAILED", BLE_DEVICE_NAME, countPhones(), MAX_PHONES, bleUnlockRssi);
  int n = NimBLEDevice::getNumBonds();
  for (int b = 0; b < n; b++) {
    NimBLEAddress a = NimBLEDevice::getBondedAddress(b);
    String nm = nameForAddr(a);
    Serial.printf("  phone %d: %s [%s]\n", b + 1, nm.length() ? nm.c_str() : "(no name yet)", a.toString().c_str());
  }
}

// ================= phone key: runs every loop() =================
void bleLoop() {
  if (!bleOK) return;

  BleEvent ev;
  while (xQueueReceive(bleQueue, &ev, 0) == pdTRUE) bleHandleEvent(ev);

  // Pairing mode countdown
  if (pairWindowOpen) {
    if ((long)(millis() - pairUntil) >= 0) {
      closePairWindow();
      Serial.println(">>> Pairing mode closed (60 s passed). No phone was added.");
      screen("TIMEOUT", "Pairing closed", "No phone added", "Try again: addphone", "Door LOCKED");
      delay(1500);
      standby();
    } else if (millis() - lastPairDraw >= 1000) {
      drawPairScreen();
    }
  }

  if (millis() - lastBlePoll < BLE_POLL_MS) return;
  lastBlePoll = millis();

  for (int i = 0; i < BLE_MAX_LINKS; i++) {
    PhoneSession& ss = sessions[i];
    if (!ss.active) continue;

    // Devices that never prove they are approved are dropped (keeps the door free for real phones)
    if (!ss.authorized) {
      unsigned long limit = pairWindowOpen ? BLE_PAIR_WINDOW_MS : BLE_AUTH_TIMEOUT_MS;
      if (millis() - ss.connectedAt > limit) {
        Serial.println("BLE: device did not prove it is approved -> dropped");
        dropConnection(i, ss.conn);
      }
      continue;
    }

    if (ss.needId) {
      if ((long)(millis() - ss.idAt) >= 0) identifyPhone(i);
      continue;
    }

    int8_t rssi = 0;
    if (ble_gap_conn_rssi(ss.conn, &rssi) != 0) continue;
    if (rssi >= 0 || rssi < -110) continue;               // invalid reading
    ss.lastRssi = rssi;
    ss.ema = (ss.ema <= -99.0f) ? rssi : (ss.ema * 0.65f + rssi * 0.35f);

    if (ss.ema >= bleUnlockRssi) ss.nearCount++;
    else ss.nearCount = 0;

    if (!ss.armed && ss.ema <= bleUnlockRssi - BLE_REARM_GAP) {
      ss.armed = true;
      Serial.printf("BLE: %s moved away -> ready to open again\n", ss.name);
    }

    bool cooledDown = (ss.lastUnlock == 0) || (millis() - ss.lastUnlock > BLE_COOLDOWN_MS);
    if (ss.armed && cooledDown && ss.nearCount >= BLE_NEAR_SAMPLES) {
      ss.armed = false;
      ss.nearCount = 0;
      ss.lastUnlock = millis();
      phoneUnlock(i);
      return;
    }
  }

  if (bleLive && millis() - lastLivePrint >= 1000) {
    lastLivePrint = millis();
    bool any = false;
    for (int i = 0; i < BLE_MAX_LINKS; i++) {
      PhoneSession& ss = sessions[i];
      if (!ss.active || !ss.authorized || ss.needId) continue;
      any = true;
      Serial.printf("  LIVE %-12s now %4d dBm  smooth %4d dBm  opens at %d  %s\n",
                    ss.name, ss.lastRssi, (int)ss.ema, bleUnlockRssi,
                    ss.armed ? "(ready)" : "(move away to re-arm)");
    }
    if (!any) Serial.println("  LIVE: no paired phone connected (Bluetooth ON? in range?)");
  }
}

// ================= phone key: Serial commands =================
void listPhones() {
  Serial.println("---- Phone keys ----");
  int n = NimBLEDevice::getNumBonds();
  if (n == 0) Serial.println("  (none yet - add one with: addphone <name>)");
  for (int b = 0; b < n; b++) {
    NimBLEAddress a = NimBLEDevice::getBondedAddress(b);
    String nm = nameForAddr(a);
    String state = "not connected";
    for (int k = 0; k < BLE_MAX_LINKS; k++) {
      PhoneSession& ss = sessions[k];
      if (ss.active && ss.authorized && !ss.needId && nm.equalsIgnoreCase(ss.name)) {
        state = "connected, signal " + String((int)ss.ema) + " dBm";
      }
    }
    Serial.printf("  %d. %-12s [%s]  %s\n", b + 1, nm.length() ? nm.c_str() : "(no name)",
                  a.toString().c_str(), state.c_str());
  }
  Serial.printf("  Opens when signal is stronger than %d dBm. Re-measure: blecal\n", bleUnlockRssi);
  Serial.println("--------------------");
}

void removePhone(String name) {
  name.trim();
  int n = NimBLEDevice::getNumBonds();
  for (int b = 0; b < n; b++) {
    NimBLEAddress a = NimBLEDevice::getBondedAddress(b);
    String nm = nameForAddr(a);
    if (nm.length() > 0 && name.equalsIgnoreCase(nm)) {
      for (int k = 0; k < BLE_MAX_LINKS; k++) {
        if (sessions[k].active && nm.equalsIgnoreCase(sessions[k].name)) dropConnection(k, sessions[k].conn);
      }
      NimBLEDevice::deleteBond(a);
      phoneStore.remove(addrKey(a).c_str());
      Serial.printf(">>> Removed %s's phone. It can no longer open the door.\n", nm.c_str());
      Serial.println("    (On that phone you can also 'Forget' CLASSYNC-DOOR in Bluetooth settings.)");
      standby();
      return;
    }
  }
  Serial.println("No phone called '" + name + "'. Type 'phones' to see the list.");
}

void resetPhones() {
  for (int k = 0; k < BLE_MAX_LINKS; k++) {
    if (sessions[k].active) dropConnection(k, sessions[k].conn);
  }
  NimBLEDevice::deleteAllBonds();
  int thr = bleUnlockRssi;
  phoneStore.clear();
  phoneStore.putBool("v6", true);
  phoneStore.putInt("thr", thr);
  Serial.println(">>> ALL phone keys deleted. On each phone 'Forget' CLASSYNC-DOOR, then pair again.");
  standby();
}

void setBleThreshold(String v) {
  v.trim();
  int t = v.toInt();
  if (t > -20 || t < -100) {
    Serial.println("Use a value between -100 and -20, e.g.  bleopen -50  (closer = bigger number like -45)");
    return;
  }
  bleUnlockRssi = t;
  phoneStore.putInt("thr", t);
  Serial.printf("Door now opens when phone signal is stronger than %d dBm (saved).\n", t);
}

// Measures the phone's signal at the distance you choose (30 cm) and saves it.
void calibrateBle() {
  int s = -1;
  for (int i = 0; i < BLE_MAX_LINKS; i++) {
    PhoneSession& ss = sessions[i];
    if (ss.active && ss.authorized && !ss.needId && (s < 0 || ss.ema > sessions[s].ema)) s = i;
  }
  if (s < 0) {
    Serial.println("No paired phone is connected. Turn phone Bluetooth ON, wait 5 s, try blecal again.");
    return;
  }
  Serial.printf(">>> CALIBRATION with %s's phone: hold it 30 cm from the TOP edge of the board.\n", sessions[s].name);
  for (int c = 5; c >= 1; c--) {
    screen("CALIBRATE", "Hold phone 30 cm", "from board top edge", "Starting in " + String(c) + " s", "");
    delay(1000);
  }
  screen("MEASURING", "Keep phone still", "at 30 cm", "Please wait 5 s", "");
  long sum = 0;
  int count = 0;
  unsigned long start = millis();
  while (millis() - start < 5000) {
    int8_t r = 0;
    if (ble_gap_conn_rssi(sessions[s].conn, &r) == 0 && r < 0 && r > -110) {
      sum += r;
      count++;
    }
    delay(100);
  }
  if (count < 10) {
    Serial.println("Calibration failed: phone signal lost. Keep Bluetooth ON and try again.");
    screen("CAL FAILED", "Phone signal lost", "Keep Bluetooth ON", "Type blecal again", "Door LOCKED");
    warningAlarm(PULSES_NO_FACE);
    standby();
    return;
  }
  int t = (int)lround((double)sum / count);
  bleUnlockRssi = t;
  phoneStore.putInt("thr", t);
  for (int i = 0; i < BLE_MAX_LINKS; i++) {
    sessions[i].armed = false;              // move away first, then test
    sessions[i].nearCount = 0;
  }
  Serial.printf(">>> SAVED: door opens only when a phone is within about 30 cm (signal %d dBm).\n", t);
  Serial.println("    Now move the phone away (1 m), then bring it close again to test.");
  screen("CALIBRATED", "Opens within ~30 cm", "Level: " + String(t) + " dBm", "Move away, then", "bring phone close");
  savedSignal();
  delay(2000);
  standby();
}

// =====================================================================================
// v8.1: WEBSITE LINK THAT NEVER SLOWS THE DOOR
// Everything above is the v6 door system. This part only ADDS, and it is built so the door
// loop runs exactly as fast as in v6:
//  - Wi-Fi, the website API, saving events to storage and phone alerts all run on the OTHER
//    CPU core (core 0) in their own task. loop() only copies an event into memory (microseconds).
//  - Bluetooth gets radio priority over Wi-Fi (phone key reads its signal like before).
//  - "wifi off" switches Wi-Fi off completely: the board then behaves exactly like v6.
//  - The website can control the board (open door, new day, pair phone, learn a new card ...).
//    Commands are carried out by loop() itself, through the SAME code as the Serial commands.
// =====================================================================================

#define V8_EV_RING        40
#define V8_EV_FILE        "/classync_events.csv"
#define V8_EV_OLD         "/classync_events_old.csv"
#define V8_EV_ROTATE      4000000           // 4 MB, then the file becomes ..._old (about 60,000 events)
#define V8_R2_FILE        "/events.csv"     // written by the standalone Wi-Fi radar R2 (read-only here)
#define V8_RETRY_MS       20000
#define V8_LEARN_MS       60000             // "learncard": time to tap the new card

Preferences v8Store;
WiFiServer v8Server(80);
V8Event v8Ring[V8_EV_RING];
uint16_t v8RingHead = 0, v8RingCount = 0;
volatile uint32_t v8Seq = 0;
V8Stat v8Stat[NUM_STUDENTS];
volatile uint32_t v8UnknownCards = 0, v8VisitorsTotal = 0, v8PhoneTotal = 0, v8Boots = 0;
volatile bool v8FsOK = false, v8WifiOK = false, v8TimeOK = false, v8AlertsOn = true, v8ServerOn = false;
volatile bool v8NetOff = false, v8StatsDirty = false;
volatile uint8_t v8Net = 0;                    // 1 = first network, 2 = backup
volatile int v8PhonesPaired = 0;               // read in loop (Bluetooth stack is not touched from core 0)
volatile uint8_t v8RfidVer = 0;
volatile int v8LearnIdx = -1;
unsigned long v8LearnUntil = 0, v8LastSlow = 0;
portMUX_TYPE v8Mux = portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t v8EvQueue = NULL;                // loop -> network core: events to save
QueueHandle_t v8CmdQueue = NULL;               // network core -> loop: commands from the website
struct V8Cmd { char text[48]; };
char v8AlertTitle[48] = "", v8AlertMsg[120] = "", v8AlertPrio[10] = "high";
volatile bool v8AlertPending = false;
volatile uint32_t v8CmdsDone = 0;

const char* v8TypeWord(uint8_t t) {
  switch (t) {
    case V8_ENTRY_FACE:     return "entry_card_face";
    case V8_ENTRY_PHONE:    return "entry_phone";
    case V8_ENTRY_QR:       return "entry_visitor_qr";
    case V8_UNKNOWN_CARD:   return "unknown_card";
    case V8_CARD_LOCKED:    return "card_locked";
    case V8_FACE_MISMATCH:  return "face_mismatch";
    case V8_SECURITY_LOCK:  return "security_lock";
    case V8_NO_FACE:        return "no_face";
    case V8_QR_FAKE:        return "fake_pass";
    case V8_QR_USED:        return "pass_reused";
    case V8_PROXY_BLOCKED:  return "proxy_blocked";
    case V8_ENROLLED:       return "face_enrolled";
    case V8_PHONE_PAIRED:   return "phone_paired";
    case V8_BLE_REFUSED:    return "phone_refused";
    case V8_BOOT:           return "board_started";
    case V8_REMOTE_OPEN:    return "door_opened_remote";
    case V8_CARD_LEARNED:   return "card_registered";
    case V8_COMMAND:        return "board_command";
    default:                return "event";
  }
}

String v8Json(const char* s) {        // safe text for JSON
  String o = "";
  for (const char* p = s; *p; p++) {
    char c = *p;
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}

String v8Csv(const char* s) {         // no commas or line breaks inside one CSV field
  String o = "";
  for (const char* p = s; *p; p++) o += (*p == ',' || *p == '\n' || *p == '\r') ? ' ' : *p;
  return o;
}

String v8Clock(uint32_t when) {
  if (when < 1600000000UL) return "unknown";
  time_t t = (time_t)when;
  struct tm lt;
  localtime_r(&t, &lt);
  char b[24];
  strftime(b, sizeof(b), "%Y-%m-%d %H:%M:%S", &lt);
  return String(b);
}

uint32_t v8NowReal() {
  time_t t = time(NULL);
  return (t > 1600000000L) ? (uint32_t)t : 0;
}

void v8QueueAlert(const char* title, const String& msg, const char* prio) {
  portENTER_CRITICAL(&v8Mux);
  strlcpy(v8AlertTitle, title, sizeof(v8AlertTitle));
  strlcpy(v8AlertMsg, msg.c_str(), sizeof(v8AlertMsg));
  strlcpy(v8AlertPrio, prio, sizeof(v8AlertPrio));
  v8AlertPending = true;
  portEXIT_CRITICAL(&v8Mux);
}

// ---- the one function every door event goes through (called from loop: memory only, no waiting) ----
void v8Event(uint8_t type, int person, const char* name, const char* detail) {
  if (person >= NUM_STUDENTS) person = -1;
  V8Event e;
  e.when = v8NowReal();
  e.up = millis() / 1000UL;
  e.type = type;
  e.person = (int8_t)person;
  strlcpy(e.name, name ? name : "", sizeof(e.name));
  strlcpy(e.detail, detail ? detail : "", sizeof(e.detail));
  portENTER_CRITICAL(&v8Mux);
  e.seq = ++v8Seq;
  v8Ring[v8RingHead] = e;
  v8RingHead = (v8RingHead + 1) % V8_EV_RING;
  if (v8RingCount < V8_EV_RING) v8RingCount++;
  if (person >= 0) {
    V8Stat& s = v8Stat[person];
    if (e.when && (s.first == 0 || e.when < s.first)) s.first = e.when;
    if (e.when) s.last = e.when;
    if (type == V8_ENTRY_FACE || type == V8_ENTRY_PHONE) s.entries++;
    if (type == V8_FACE_MISMATCH) s.rejects++;
    if (type == V8_SECURITY_LOCK) s.locks++;
    if (type == V8_NO_FACE) s.noface++;
  }
  if (type == V8_UNKNOWN_CARD) v8UnknownCards++;
  if (type == V8_ENTRY_QR) v8VisitorsTotal++;
  if (type == V8_ENTRY_PHONE) v8PhoneTotal++;
  v8StatsDirty = true;
  portEXIT_CRITICAL(&v8Mux);
  if (v8EvQueue) xQueueSend(v8EvQueue, &e, 0);          // saved to storage by the network core
  v9OnEvent(type, e.name, e.detail);                     // v8.2: Telegram alert queue + Blynk counters

  if (type == V8_SECURITY_LOCK) v8QueueAlert("CLASSYNC security alert", String("3 wrong faces on ") + e.name + "'s card. Card locked.", "urgent");
  else if (type == V8_QR_FAKE) v8QueueAlert("CLASSYNC security alert", "A FAKE visitor pass was shown at the door.", "urgent");
  else if (type == V8_PROXY_BLOCKED) v8QueueAlert("CLASSYNC security alert", String("Proxy blocked: ") + e.detail + " tried to enroll on " + e.name + "'s card.", "high");
  else if (type == V8_BLE_REFUSED) v8QueueAlert("CLASSYNC security alert", "A phone tried to link without the pairing code.", "high");
  else if (type == V8_UNKNOWN_CARD) v8QueueAlert("CLASSYNC: unknown card", String("An unknown card was tapped (") + e.detail + ").", "default");
}

// ---- new card for a person ("learncard rozy", then tap the card) ----
void v8LoadCards() {                  // called in setup BEFORE the saved faces are loaded
  Preferences p;
  if (!p.begin("classyncv8", true)) return;
  for (int i = 0; i < NUM_STUDENTS; i++) {
    String k = "card" + String(i);
    String u = p.getString(k.c_str(), "");
    if (u.length() >= 8) {
      students[i].uid = strdup(u.c_str());
      Serial.printf("Card for %s loaded from memory: %s\n", students[i].name, u.c_str());
    }
  }
  p.end();
}

void v8StartLearn(const String& who) {
  int idx = findStudentByName(who);
  if (idx < 0) {
    Serial.println("No student called '" + who + "'. Type 'list' to see names.");
    return;
  }
  v8LearnIdx = idx;
  v8LearnUntil = millis() + V8_LEARN_MS;
  Serial.printf(">>> TAP THE NEW CARD FOR %s within 60 s (then the face is saved right away)\n", students[idx].name);
  screen("NEW CARD", students[idx].name, "Tap the new card", "within 60 seconds", "then look at camera");
  chirp();
}

int v8LearnCard(const String& uid) {  // loop: unknown card tapped while learning -> it becomes that person's card
  if (v8LearnIdx < 0) return -1;
  if ((long)(millis() - v8LearnUntil) >= 0) {
    v8LearnIdx = -1;
    return -1;
  }
  int idx = v8LearnIdx;
  v8LearnIdx = -1;
  if (findStudent(uid) >= 0) return -1;            // card already belongs to someone
  students[idx].uid = strdup(uid.c_str());
  v8Store.putString(("card" + String(idx)).c_str(), uid);
  Serial.printf(">>> CARD SAVED PERMANENTLY for %s: [%s]. Now look at the camera to save the face.\n", students[idx].name, uid.c_str());
  v8Event(V8_CARD_LEARNED, idx, students[idx].name, uid.c_str());
  screen("CARD SAVED", students[idx].name, "Card linked", "Now look at the", "camera (face)");
  savedSignal();
  delay(700);
  return idx;
}

void v8OpenDoorRemote(const char* by) {
  Serial.printf(">>> DOOR OPENED FROM THE WEBSITE (%s)\n", by);
  logAccess("WEBSITE", by);
  v8Event(V8_REMOTE_OPEN, -1, by, "website");
  screen("OPEN", "Opened remotely", "from the website", "", "Door OPEN - go in");
  successSignalAndOpenDoor();
  standby();
}

// ---- phone notification (runs on the network core only) ----
bool v8SendAlert(const char* title, const char* message, const char* priority) {
  if (!v8WifiOK) return false;
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(5000);
  if (!client.connect("ntfy.sh", 443)) {
    Serial.println("!! Phone alert failed: could not reach ntfy.sh");
    return false;
  }
  String msg = message;
  String req = String("POST /") + NTFY_TOPIC + " HTTP/1.1\r\n"
               "Host: ntfy.sh\r\nUser-Agent: CLASSYNC-Door\r\n"
               "Title: " + title + "\r\nPriority: " + priority + "\r\nTags: rotating_light\r\n"
               "Content-Type: text/plain\r\nContent-Length: " + String(msg.length()) + "\r\n"
               "Connection: close\r\n\r\n" + msg;
  client.print(req);
  unsigned long start = millis();
  bool ok = false;
  while (client.connected() && millis() - start < 5000) {
    String line = client.readStringUntil('\n');
    if (line.startsWith("HTTP/1.1 200")) { ok = true; break; }
    if (line == "\r") break;
  }
  client.stop();
  Serial.println(ok ? String(">>> PHONE ALERT SENT: ") + title : String("!! Phone alert not confirmed by ntfy.sh"));
  return ok;
}

// ---- R2 Wi-Fi radar data already stored on this board (read-only) ----
uint32_t v8R2Count(const char* key) {
  Preferences p;
  if (!p.begin("doorradar", true)) return 0;
  uint32_t v = p.getUInt(key, 0);
  p.end();
  return v;
}

// ---- status page that the website reads (built on the network core) ----
String v8DoorJson() {
  V8Stat st[NUM_STUDENTS];
  portENTER_CRITICAL(&v8Mux);
  memcpy(st, v8Stat, sizeof(st));
  portEXIT_CRITICAL(&v8Mux);
  String j = "{";
  j += "\"device\":\"CLASSYNC-DOOR\",\"version\":\"8.2.5\",\"device_id\":\"" DEVICE_ID "\",";
  j += "\"uptime\":" + String(millis() / 1000UL) + ",\"time\":" + String(v8NowReal()) + ",";
  j += "\"counts\":{\"present_today\":" + String(presentCount) + ",\"visitors_today\":" + String(visitorCount) +
       ",\"openings_since_start\":" + String(accessCount) + ",\"events_total\":" + String(v8Seq) +
       ",\"unknown_cards_total\":" + String(v8UnknownCards) + ",\"visitors_total\":" + String(v8VisitorsTotal) +
       ",\"phone_entries_total\":" + String(v8PhoneTotal) + "},";
  j += "\"attendance\":[";
  for (int i = 0; i < NUM_STUDENTS; i++) {
    if (i) j += ",";
    j += "{\"name\":\"" + v8Json(students[i].name) + "\",\"present\":" + String(markedPresent[i] ? "true" : "false") + "}";
  }
  j += "],\"people\":[";
  for (int i = 0; i < NUM_STUDENTS; i++) {
    if (i) j += ",";
    String uid = students[i].uid;
    String masked = (uid.length() >= 5 && !uid.startsWith("PASTE")) ? uid.substring(0, 5) + " ** **" : String("not set");
    V8Stat& s = st[i];
    j += "{\"id\":\"" + v8Json(students[i].name) + "\",\"index\":" + String(i) + ",\"card\":\"" + masked + "\"" +
         ",\"card_set\":" + String(uid.startsWith("PASTE") ? "false" : "true") +
         ",\"face_saved\":" + String(hasFace[i] ? "true" : "false") +
         ",\"present\":" + String(markedPresent[i] ? "true" : "false") +
         ",\"locked\":" + String(lockedUntil[i] > millis() ? "true" : "false") +
         ",\"first\":" + String(s.first) + ",\"last\":" + String(s.last) +
         ",\"entries\":" + String(s.entries) + ",\"rejects\":" + String(s.rejects) +
         ",\"locks\":" + String(s.locks) + ",\"noface\":" + String(s.noface) + "}";
  }
  j += "],";
  int connected = 0;
  String phones = "";
  for (int i = 0; i < BLE_MAX_LINKS; i++) {
    PhoneSession& ss = sessions[i];
    if (!ss.active) continue;
    connected++;
    if (phones.length()) phones += ",";
    char nm[sizeof(ss.name)];
    strlcpy(nm, ss.name, sizeof(nm));
    phones += "{\"name\":\"" + v8Json(nm) + "\",\"approved\":" + String(ss.authorized ? "true" : "false") +
              ",\"signal\":" + String((int)ss.ema) + ",\"last\":" + String((int)ss.lastRssi) +
              ",\"ready\":" + String(ss.armed ? "true" : "false") + "}";
  }
  j += "\"ble\":{\"on\":" + String(bleOK ? "true" : "false") + ",\"paired\":" + String(v8PhonesPaired) +
       ",\"max\":" + String(MAX_PHONES) + ",\"connected\":" + String(connected) + ",\"phones\":[" + phones + "]" +
       ",\"unlock_dbm\":" + String(bleUnlockRssi) + ",\"pairing\":" + String(pairWindowOpen ? "true" : "false") + "},";
  uint8_t ver = v8RfidVer;
  j += "\"rfid\":{\"ok\":" + String((ver == 0x00 || ver == 0xFF) ? "false" : "true") + ",\"version\":\"0x" + String(ver, HEX) + "\"" +
       ",\"learning\":\"" + String(v8LearnIdx >= 0 ? students[v8LearnIdx].name : "") + "\"},";
  j += "\"camera\":{\"ok\":" + String(cameraOK ? "true" : "false") + ",\"faces_saved\":" + String(countFacesSaved()) +
       ",\"students\":" + String(NUM_STUDENTS) + "},";
  size_t fsT = v8FsOK ? LittleFS.totalBytes() : 0, fsU = v8FsOK ? LittleFS.usedBytes() : 0;
  nvs_stats_t ns;
  bool nvsOK = (nvs_get_stats(NULL, &ns) == ESP_OK);
  j += "\"health\":{\"heap_free\":" + String(ESP.getFreeHeap()) + ",\"heap_min\":" + String(ESP.getMinFreeHeap()) +
       ",\"psram_free\":" + String(ESP.getFreePsram()) + ",\"psram_total\":" + String(ESP.getPsramSize()) +
       ",\"wifi_rssi\":" + String(v8WifiOK ? WiFi.RSSI() : 0) + ",\"wifi\":\"" + String(v8Net == 1 ? NET1_SSID : v8Net == 2 ? NET2_SSID : "-") + "\"" +
       ",\"ip\":\"" + (v8WifiOK ? WiFi.localIP().toString() : String("-")) + "\",\"clock_set\":" + String(v8TimeOK ? "true" : "false") +
       ",\"storage_total\":" + String(fsT) + ",\"storage_used\":" + String(fsU) +
       ",\"nvs_used\":" + String(nvsOK ? ns.used_entries : 0) + ",\"nvs_total\":" + String(nvsOK ? ns.total_entries : 0) +
       ",\"boots\":" + String(v8Boots) + ",\"cpu_mhz\":" + String(getCpuFrequencyMhz()) +
       ",\"flash_app_total\":" + String(ESP.getFreeSketchSpace() + ESP.getSketchSize()) + ",\"flash_app_used\":" + String(ESP.getSketchSize()) +
       ",\"alerts_on\":" + String(v8AlertsOn ? "true" : "false") + ",\"commands_done\":" + String(v8CmdsDone) + "},";
  j += "\"radar_r2\":{\"note\":\"stored visits from the standalone Wi-Fi radar (R2)\",\"visits\":" + String(v8R2Count("seq")) +
       ",\"passed\":" + String(v8R2Count("pass")) + ",\"stopped\":" + String(v8R2Count("stop")) +
       ",\"waited\":" + String(v8R2Count("wait")) + ",\"file\":" + String(v8FsOK && LittleFS.exists(V8_R2_FILE) ? "true" : "false") + "},";
  j += "\"sensors\":[";
  for (int i = 0; i < V9_SLOTS; i++) {
    V9Slot& s = v9Slots[i];
    if (i) j += ",";
    j += String("{\"id\":\"") + s.id + "\",\"type\":\"" + s.type + "\",\"unit\":\"" + s.unit + "\",\"status\":\"" + v9StatusWord(s.status) + "\"" +
         (s.status == V9_OK ? ",\"value\":" + String(s.value, 2) : String("")) + "}";
  }
  j += "],";
  j += "\"door_log\":[]}";
  return j;
}

String v8EventJson(const V8Event& e) {
  return String("{\"seq\":") + e.seq + ",\"time\":" + e.when + ",\"uptime\":" + e.up +
         ",\"type\":\"" + v8TypeWord(e.type) + "\",\"person\":\"" + (e.person >= 0 ? v8Json(students[e.person].name) : String("")) +
         "\",\"name\":\"" + v8Json(e.name) + "\",\"detail\":\"" + v8Json(e.detail) + "\"}";
}

// events with seq > since (oldest first, at most 60 per request)
String v8EventsJson(uint32_t since) {
  static V8Event copy[V8_EV_RING];
  uint16_t cnt, head;
  uint32_t last;
  portENTER_CRITICAL(&v8Mux);
  memcpy(copy, v8Ring, sizeof(copy));
  cnt = v8RingCount;
  head = v8RingHead;
  last = v8Seq;
  portEXIT_CRITICAL(&v8Mux);
  String j = "{\"device\":\"CLASSYNC-DOOR\",\"device_id\":\"" DEVICE_ID "\",\"last\":" + String(last) + ",\"events\":[";
  int n = 0;
  uint32_t oldestInRing = cnt ? copy[(head - cnt + V8_EV_RING) % V8_EV_RING].seq : last + 1;
  if (since + 1 >= oldestInRing) {                       // everything needed is in fast memory
    for (int i = 0; i < cnt && n < 60; i++) {
      V8Event& e = copy[(head - cnt + i + V8_EV_RING) % V8_EV_RING];
      if (e.seq <= since) continue;
      if (n++) j += ",";
      j += v8EventJson(e);
    }
  } else if (v8FsOK) {                                   // older: read them back from the permanent file
    File f = LittleFS.open(V8_EV_FILE, "r");
    while (f && f.available() && n < 60) {
      String line = f.readStringUntil('\n');
      uint32_t seq = (uint32_t)line.toInt();
      if (seq == 0 || seq <= since) continue;
      String c[8];
      int start = 0;
      for (int k = 0; k < 8; k++) {
        int comma = line.indexOf(',', start);
        c[k] = (comma < 0 || k == 7) ? line.substring(start) : line.substring(start, comma);
        c[k].trim();
        if (comma < 0) break;
        start = comma + 1;
      }
      if (n++) j += ",";
      j += "{\"seq\":" + c[0] + ",\"time\":" + c[1] + ",\"uptime\":" + c[3] + ",\"type\":\"" + v8Json(c[4].c_str()) +
           "\",\"person\":\"" + (c[5] == "-" ? String("") : v8Json(c[5].c_str())) + "\",\"name\":\"" + v8Json(c[6].c_str()) +
           "\",\"detail\":\"" + v8Json(c[7].c_str()) + "\"}";
    }
    if (f) f.close();
  }
  j += "]}";
  return j;
}

// R2 radar visits stored on this board: number,unix_time,date_time,uptime_s,kind,seconds,signal_dbm[,how]
String v8R2Json(uint32_t since) {
  String j = "{\"device\":\"CLASSYNC-DOOR\",\"visits\":[";
  int n = 0;
  if (v8FsOK && LittleFS.exists(V8_R2_FILE)) {
    File f = LittleFS.open(V8_R2_FILE, "r");
    while (f && f.available() && n < 100) {
      String line = f.readStringUntil('\n');
      uint32_t num = (uint32_t)line.toInt();
      if (num == 0 || num <= since) continue;
      String c[7];
      int start = 0;
      for (int k = 0; k < 7; k++) {
        int comma = line.indexOf(',', start);
        c[k] = comma < 0 ? line.substring(start) : line.substring(start, comma);
        c[k].trim();
        if (comma < 0) break;
        start = comma + 1;
      }
      if (n++) j += ",";
      j += "{\"number\":" + c[0] + ",\"time\":" + (c[1].length() ? c[1] : String("0")) + ",\"kind\":\"" + v8Json(c[4].c_str()) +
           "\",\"seconds\":" + (c[5].length() ? c[5] : String("0")) + "}";
    }
    if (f) f.close();
  }
  j += "]}";
  return j;
}

// ---- commands from the website: only this list is accepted ----
bool v8CommandAllowed(const String& c) {
  const char* exact[] = {"opendoor", "reset", "alerts on", "alerts off", "testalert", "cancelcard", "restart", "blelive"};
  for (const char* e : exact) if (c == e) return true;
  const char* prefix[] = {"addphone ", "removephone ", "learncard ", "forget "};
  for (const char* p : prefix) if (c.startsWith(p) && c.length() > strlen(p) && c.length() < 40) return true;
  return false;
}

String v8UrlDecode(const String& s) {
  String o = "";
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '+') o += ' ';
    else if (c == '%' && i + 2 < s.length()) { o += (char)strtol(s.substring(i + 1, i + 3).c_str(), NULL, 16); i += 2; }
    else o += c;
  }
  return o;
}

void v8Send(WiFiClient& c, int code, const char* type, const String& body) {
  c.print(String("HTTP/1.1 ") + code + (code == 200 ? " OK" : code == 403 ? " Forbidden" : " Bad Request") +
          "\r\nContent-Type: " + type + "\r\nCache-Control: no-store\r\nContent-Length: " + String(body.length()) +
          "\r\nConnection: close\r\n\r\n");
  c.print(body);
}

void v8ServerLoop() {                 // network core only
  if (!v8ServerOn) return;
  WiFiClient c = v8Server.available();
  if (!c) return;
  c.setTimeout(2);
  unsigned long t0 = millis();
  String line = "", key = "";
  while (c.connected() && millis() - t0 < 1500) {
    if (!c.available()) { vTaskDelay(2); continue; }
    line = c.readStringUntil('\n');
    break;
  }
  if (line.length() == 0) { c.stop(); return; }
  while (c.connected() && millis() - t0 < 2000) {         // headers (the website sends X-Key)
    if (!c.available()) { vTaskDelay(2); continue; }
    String h = c.readStringUntil('\n');
    if (h.length() <= 1) break;
    if (h.startsWith("X-Key:") || h.startsWith("x-key:")) { key = h.substring(6); key.trim(); }
  }
  uint32_t since = 0;
  int q = line.indexOf("since=");
  if (q > 0) since = (uint32_t)line.substring(q + 6).toInt();
  if (line.startsWith("GET /api/door")) v8Send(c, 200, "application/json", v8DoorJson());
  else if (line.startsWith("GET /api/events")) v8Send(c, 200, "application/json", v8EventsJson(since));
  else if (line.startsWith("GET /api/r2")) v8Send(c, 200, "application/json", v8R2Json(since));
  else if (line.startsWith("POST /api/cmd?c=")) {
    String cmd = line.substring(16);
    int sp = cmd.indexOf(' ');
    if (sp > 0) cmd = cmd.substring(0, sp);
    cmd = v8UrlDecode(cmd);
    cmd.trim();
    cmd.toLowerCase();
    if (key != DOOR_API_KEY) v8Send(c, 403, "application/json", "{\"ok\":false,\"error\":\"wrong door key\"}");
    else if (!v8CommandAllowed(cmd)) v8Send(c, 400, "application/json", "{\"ok\":false,\"error\":\"command not allowed\"}");
    else {
      V8Cmd m;
      strlcpy(m.text, cmd.c_str(), sizeof(m.text));
      bool ok = v8CmdQueue && xQueueSend(v8CmdQueue, &m, 0) == pdTRUE;
      v8Send(c, ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"busy, try again\"}");
    }
  } else if (line.startsWith("GET / ")) {
    String h = "<!doctype html><meta name=viewport content='width=device-width'><body style='font-family:sans-serif;padding:16px'>"
               "<h2>CLASSYNC door board v8.1</h2><p>Open the SARTHIK Room website to see and control everything.</p>"
               "<p>Present today: " + String(presentCount) + "/" + String(NUM_STUDENTS) + " &middot; events saved: " + String(v8Seq) + "</p></body>";
    v8Send(c, 200, "text/html", h);
  } else {
    c.print("HTTP/1.1 404 Not Found\r\nContent-Length: 10\r\nConnection: close\r\n\r\nNot found\n");
  }
  c.stop();
}

// ---- saving events to the 9.8 MB storage (network core) ----
void v8SaveEvent(const V8Event& e) {
  if (!v8FsOK) return;
  File chk = LittleFS.open(V8_EV_FILE, "r");
  size_t sz = chk ? chk.size() : 0;
  bool fresh = !chk;
  if (chk) chk.close();
  if (sz > V8_EV_ROTATE) {
    LittleFS.remove(V8_EV_OLD);
    LittleFS.rename(V8_EV_FILE, V8_EV_OLD);
    fresh = true;
  }
  File f = LittleFS.open(V8_EV_FILE, FILE_APPEND);
  if (!f) return;
  if (fresh) f.println("seq,unix_time,date_time,uptime_s,type,person,name,detail");
  f.printf("%lu,%lu,%s,%lu,%s,%s,%s,%s\n", (unsigned long)e.seq, (unsigned long)e.when, v8Clock(e.when).c_str(),
           (unsigned long)e.up, v8TypeWord(e.type), e.person >= 0 ? students[e.person].name : "-",
           v8Csv(e.name).c_str(), v8Csv(e.detail).c_str());
  f.close();
}

void v8SaveStats() {
  V8Stat st[NUM_STUDENTS];
  uint32_t seq, unk, vis, pho;
  portENTER_CRITICAL(&v8Mux);
  memcpy(st, v8Stat, sizeof(st));
  seq = v8Seq; unk = v8UnknownCards; vis = v8VisitorsTotal; pho = v8PhoneTotal;
  v8StatsDirty = false;
  portEXIT_CRITICAL(&v8Mux);
  v8Store.putBytes("stat", st, sizeof(st));
  v8Store.putUInt("seq", seq);
  v8Store.putUInt("unk", unk);
  v8Store.putUInt("vis", vis);
  v8Store.putUInt("pho", pho);
}

// ---- Wi-Fi in the background (network core) ----
void v8Begin(uint8_t which) {
  v8Net = which;
  WiFi.disconnect();
  WiFi.begin(which == 1 ? NET1_SSID : NET2_SSID, which == 1 ? NET1_PASS : NET2_PASS);
}

void v8WifiUp() {
  v8WifiOK = true;
  esp_coex_preference_set(ESP_COEX_PREFER_BT);        // phone key keeps the radio when both need it
  Serial.printf("Wi-Fi OK: %s. This board is at http://%s  (type this in the website settings)\n",
                v8Net == 1 ? NET1_SSID : NET2_SSID, WiFi.localIP().toString().c_str());
  configTime(TZ_OFFSET_SEC, 0, "pool.ntp.org", "time.google.com");
  if (!v8ServerOn) { v8Server.begin(); v8ServerOn = true; }
}

void v8NetTask(void*) {
  unsigned long lastTry = 0, lastClock = 0, lastStats = 0;
  bool radioOn = false;
  for (;;) {
    // 1) save events (always, also with Wi-Fi off)
    V8Event e;
    while (v8EvQueue && xQueueReceive(v8EvQueue, &e, 0) == pdTRUE) v8SaveEvent(e);
    if (v8StatsDirty && millis() - lastStats > 2000) { lastStats = millis(); v8SaveStats(); }

    // 2) Wi-Fi on/off switch
    if (v8NetOff) {
      if (radioOn) {
        if (v8ServerOn) { v8Server.end(); v8ServerOn = false; }
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        radioOn = false;
        v8WifiOK = false;
        v8Net = 0;
        Serial.println("Wi-Fi is OFF: the door works exactly like v6. Type 'wifi on' to link the website again.");
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (!radioOn) {
      WiFi.mode(WIFI_STA);
      WiFi.setSleep(bleOK ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);   // with Bluetooth on this is required (v7 proof)
      esp_coex_preference_set(ESP_COEX_PREFER_BT);
      radioOn = true;
      v8Begin(1);
      lastTry = millis();
      Serial.printf("Wi-Fi: connecting to \"%s\" in the background ...\n", NET1_SSID);
    }

    // 3) keep Wi-Fi connected, try the backup network if the first one does not answer
    bool up = (WiFi.status() == WL_CONNECTED);
    if (up && !v8WifiOK) v8WifiUp();
    if (!up && v8WifiOK) { v8WifiOK = false; Serial.println("Wi-Fi: connection lost, reconnecting in the background..."); lastTry = millis(); }
    if (!up && millis() - lastTry > V8_RETRY_MS) {
      v8Begin(v8Net == 1 ? 2 : 1);
      lastTry = millis();
    }
    if (v8WifiOK && !v8TimeOK) {
      if (time(NULL) > 1600000000L) { v8TimeOK = true; Serial.println("Clock set from the internet."); }
      else if (millis() - lastClock > 60000) { configTime(TZ_OFFSET_SEC, 0, "pool.ntp.org", "time.google.com"); lastClock = millis(); }
    }

    // 4) website requests and phone alerts
    v8ServerLoop();
    if (v8AlertPending && v8WifiOK) {
      char t[48], m[120], p[10];
      portENTER_CRITICAL(&v8Mux);
      strlcpy(t, v8AlertTitle, sizeof(t)); strlcpy(m, v8AlertMsg, sizeof(m)); strlcpy(p, v8AlertPrio, sizeof(p));
      v8AlertPending = false;
      portEXIT_CRITICAL(&v8Mux);
      if (v8AlertsOn) v8SendAlert(t, m, p);
    }
    v9CloudLoop();     // v8.2: Firebase / ThingSpeak / Blynk / Telegram, one short step at a time
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void v8Setup() {
  v8Store.begin("classyncv8", false);
  v8Seq = v8Store.getUInt("seq", 0);
  if (v8Store.getBytesLength("stat") == sizeof(v8Stat)) v8Store.getBytes("stat", v8Stat, sizeof(v8Stat));
  else memset(v8Stat, 0, sizeof(v8Stat));
  v8UnknownCards = v8Store.getUInt("unk", 0);
  v8VisitorsTotal = v8Store.getUInt("vis", 0);
  v8PhoneTotal = v8Store.getUInt("pho", 0);
  v8Boots = v8Store.getUInt("boot", 0) + 1;
  v8Store.putUInt("boot", v8Boots);
  v8AlertsOn = v8Store.getBool("alerts", true);
  v8NetOff = v8Store.getBool("netoff", false);
  v8PhonesPaired = bleOK ? countPhones() : 0;
  v8RfidVer = rfid.PCD_ReadRegister(MFRC522::VersionReg);

  v8FsOK = LittleFS.begin(false, "/littlefs", 5, "spiffs");
  if (!v8FsOK) {
    Serial.println("Storage: preparing the 9.8 MB event storage for the first time (about 20 s)...");
    v8FsOK = LittleFS.begin(true, "/littlefs", 5, "spiffs");
  }
  Serial.printf("Event history: %s, %lu events saved so far. Wi-Fi radar (R2) visits stored: %lu\n",
                v8FsOK ? "OK" : "NOT available", (unsigned long)v8Seq, (unsigned long)v8R2Count("seq"));
  if (v8NetOff) Serial.println("Wi-Fi is switched OFF (type 'wifi on' to link the website).");

  v8EvQueue = xQueueCreate(64, sizeof(V8Event));
  v8CmdQueue = xQueueCreate(8, sizeof(V8Cmd));
  // Network work runs on core 0 (loop() runs on core 1), low priority: the door is never slowed down.
  xTaskCreatePinnedToCore(v8NetTask, "v8net", 12288, NULL, 1, NULL, 0);
  v8Event(V8_BOOT, -1, "board", (String("boot ") + v8Boots).c_str());
}

void handleCommandText(String cmd);

void v8Loop() {                       // core 1, first line of loop(): only quick work
  V8Cmd m;
  if (v8CmdQueue && xQueueReceive(v8CmdQueue, &m, 0) == pdTRUE) {
    String c = m.text;
    Serial.println("Website command: " + c);
    v8CmdsDone++;
    v8Event(V8_COMMAND, -1, "website", m.text);
    handleCommandText(c);
  }
  if (v8LearnIdx >= 0 && (long)(millis() - v8LearnUntil) >= 0) {
    Serial.println(">>> No new card was tapped in 60 s. Type learncard <name> to try again.");
    v8LearnIdx = -1;
    standby();
  }
  if (millis() - v8LastSlow > 2000) {    // things only loop may touch (SPI reader, Bluetooth stack)
    v8LastSlow = millis();
    v8RfidVer = rfid.PCD_ReadRegister(MFRC522::VersionReg);
    if (bleOK) v8PhonesPaired = countPhones();
  }
}

void v8PrintEvents() {
  Serial.println("---- Last door events (kept permanently) ----");
  if (v8RingCount == 0) Serial.println("  (none since power-on; older ones are in the website)");
  for (int i = 0; i < v8RingCount; i++) {
    V8Event& e = v8Ring[(v8RingHead - v8RingCount + i + V8_EV_RING) % V8_EV_RING];
    Serial.printf("  #%-5lu %-19s %-17s %-14s %s\n", (unsigned long)e.seq, v8Clock(e.when).c_str(), v8TypeWord(e.type), e.name, e.detail);
  }
  Serial.printf("  Events saved in total: %lu\n", (unsigned long)v8Seq);
}

void v8PrintStats() {
  Serial.println("---- People statistics (kept permanently) ----");
  for (int i = 0; i < NUM_STUDENTS; i++) {
    V8Stat& s = v8Stat[i];
    Serial.printf("  %-14s card %-12s entries %-4u wrong faces %-3u locks %-3u no-face %-3u first %s last %s\n",
                  students[i].name, String(students[i].uid).startsWith("PASTE") ? "NOT SET" : students[i].uid,
                  s.entries, s.rejects, s.locks, s.noface, v8Clock(s.first).c_str(), v8Clock(s.last).c_str());
  }
  Serial.printf("  Unknown cards: %lu   Visitor passes used: %lu   Phone-key entries: %lu\n",
                (unsigned long)v8UnknownCards, (unsigned long)v8VisitorsTotal, (unsigned long)v8PhoneTotal);
}

void v8PrintWifi() {
  Serial.println("---- Wi-Fi + website link ----");
  if (v8NetOff) Serial.println("  Wi-Fi     : switched OFF (door works exactly like v6). Type 'wifi on'.");
  else {
    Serial.printf("  Wi-Fi     : %s", v8WifiOK ? (v8Net == 1 ? NET1_SSID : NET2_SSID) : "NOT connected (retrying)");
    if (v8WifiOK) Serial.printf(", signal %d dBm, board address http://%s", WiFi.RSSI(), WiFi.localIP().toString().c_str());
    Serial.println();
  }
  Serial.printf("  Clock     : %s\n", v8TimeOK ? v8Clock(v8NowReal()).c_str() : "not set yet (needs internet)");
  Serial.printf("  Storage   : %s, %lu of %lu KB used\n", v8FsOK ? "OK" : "NOT available",
                (unsigned long)(v8FsOK ? LittleFS.usedBytes() / 1024 : 0), (unsigned long)(v8FsOK ? LittleFS.totalBytes() / 1024 : 0));
  Serial.printf("  Events    : %lu saved   Phone alerts: %s\n", (unsigned long)v8Seq, v8AlertsOn ? "ON" : "OFF");
  Serial.printf("  Door key  : %s   (type the same key in the website settings)\n", DOOR_API_KEY);
  Serial.printf("  R2 radar  : %lu visits stored (%lu passed, %lu stopped, %lu waited)\n", (unsigned long)v8R2Count("seq"),
                (unsigned long)v8R2Count("pass"), (unsigned long)v8R2Count("stop"), (unsigned long)v8R2Count("wait"));
}

// =====================================================================================
// v8.2 STAGE 3: 15 SENSOR SLOTS + CLOUD (Firebase, ThingSpeak, Blynk, Telegram)
// Everything here is ADDED to v8.1. The door (card, face, QR, phone key) never waits for
// the internet: sensors are read in short steps on the door core, all cloud traffic runs
// on the network core, with retries that back off (5 s ... 5 min) when a service is down.
// A service whose key in secrets.h is empty is simply switched off.
// =====================================================================================

// ---------------- sensor slots ----------------
// Free pins on this board: GPIO 38, 39, 40 (digital only). Analog sensors use an ADS1115
// on the OLED's I2C bus (SDA 41 / SCL 42): address 0x48..0x4B, channel 0..3.
// To add a sensor: set enabled = true and fill pin (digital) or i2c address + channel.
V9Slot v9Slots[V9_SLOTS] = {
//  id          type            unit     iface          pin  addr  ch  every_ms enabled min    max     scale  offset
  {"S01", "chip_temp",     "C",     V9_BUILTIN,    -1,  0,    0,  10000, true,  -20,  110,    1,     0},
  {"S02", "occupancy",     "people",V9_BUILTIN,    -1,  0,    1,  10000, true,  0,    500,    1,     0},
  {"S03", "wifi_signal",   "dBm",   V9_BUILTIN,    -1,  0,    2,  30000, true,  -100, 0,      1,     0},
  {"S04", "free_memory",   "KB",    V9_BUILTIN,    -1,  0,    3,  30000, true,  0,    8192,   1,     0},
  {"S05", "temperature",   "C",     V9_ADS1115,    -1,  0x48, 0,  10000, false, -20,  80,     0.1,   0},
  {"S06", "humidity",      "%",     V9_ADS1115,    -1,  0x48, 1,  10000, false, 0,    100,    0.03,  0},
  {"S07", "air_quality",   "ppm",   V9_ADS1115,    -1,  0x48, 2,  10000, false, 0,    5000,   0.2,   0},
  {"S08", "light",         "lux",   V9_ADS1115,    -1,  0x48, 3,  5000,  false, 0,    100000, 1,     0},
  {"S09", "motion_pir",    "on/off",V9_DIGITAL,    38,  0,    0,  1000,  false, 0,    1,      1,     0},
  {"S10", "sound",         "level", V9_ADS1115,    -1,  0x49, 0,  5000,  false, 0,    32767,  1,     0},
  {"S11", "current",       "A",     V9_ADS1115,    -1,  0x49, 1,  5000,  false, -30,  30,     0.001, 0},
  {"S12", "voltage",       "V",     V9_ADS1115,    -1,  0x49, 2,  5000,  false, 0,    300,    0.01,  0},
  {"S13", "gas",           "ppm",   V9_ADS1115,    -1,  0x49, 3,  10000, false, 0,    10000,  0.3,   0},
  {"S14", "door_contact",  "open",  V9_DIGITAL,    39,  0,    0,  1000,  false, 0,    1,      1,     0},
  {"S15", "flame",         "on/off",V9_DIGITAL,    40,  0,    0,  1000,  false, 0,    1,      1,     0},
};
const char* v9StatusWord(uint8_t s) { return s == V9_OK ? "ok" : s == V9_FAULT ? "fault" : s == V9_NODATA ? "no_data" : "off"; }

// one ADS1115 single-shot read (+-4.096 V range). Returns false if the chip does not answer.
bool v9AdsRead(uint8_t addr, uint8_t ch, int16_t& raw) {
  uint16_t cfg = 0x8000 | ((0x04 + (ch & 3)) << 12) | 0x0200 | 0x0100 | 0x0080 | 0x0003;
  Wire.beginTransmission(addr); Wire.write(0x01); Wire.write(cfg >> 8); Wire.write(cfg & 0xFF);
  if (Wire.endTransmission() != 0) return false;
  delay(9);                                                   // 128 samples/s conversion
  Wire.beginTransmission(addr); Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((int)addr, 2) != 2) return false;
  raw = (int16_t)((Wire.read() << 8) | Wire.read());
  return true;
}

// reads at most ONE due sensor per call (a few ms), so the door loop never slows down
void v9SensorsLoop() {
  static uint8_t next = 0;
  for (int k = 0; k < V9_SLOTS; k++) {
    V9Slot& s = v9Slots[next];
    next = (next + 1) % V9_SLOTS;
    if (!s.enabled) { s.status = V9_OFF; continue; }
    if (s.lastRead && millis() - s.lastRead < s.everyMs) continue;
    s.lastRead = millis();
    float v = NAN;
    if (s.iface == V9_BUILTIN) {
      if (s.ch == 0) v = temperatureRead();
      else if (s.ch == 1) v = presentCount;
      else if (s.ch == 2) v = v8WifiOK ? WiFi.RSSI() : NAN;
      else v = ESP.getFreeHeap() / 1024.0f;
    } else if (s.iface == V9_DIGITAL && s.pin >= 0) {
      if (!s.ready) { pinMode(s.pin, INPUT_PULLDOWN); s.ready = true; }
      v = digitalRead(s.pin);
    } else if (s.iface == V9_ADS1115 && s.addr) {
      int16_t raw;
      if (v9AdsRead(s.addr, s.ch, raw)) v = raw;
    }
    if (isnan(v)) { s.status = V9_NODATA; s.fails++; }
    else {
      v = v * s.scale + s.offset;
      if (v < s.minV || v > s.maxV) { s.status = V9_FAULT; s.fails++; }
      else { s.value = v; s.status = V9_OK; s.fails = 0; s.at = v8NowReal(); }
    }
    return;
  }
}

// ---------------- cloud settings + state ----------------
#ifndef FB_API_KEY
#define FB_API_KEY ""
#endif
#ifndef FB_PROJECT_ID
#define FB_PROJECT_ID ""
#endif
#ifndef FB_DEVICE_EMAIL
#define FB_DEVICE_EMAIL ""
#endif
#ifndef FB_DEVICE_PASS
#define FB_DEVICE_PASS ""
#endif
#ifndef TS_WRITE_KEY_A
#define TS_WRITE_KEY_A ""
#endif
#ifndef TS_WRITE_KEY_B
#define TS_WRITE_KEY_B ""
#endif
#ifndef BLYNK_TOKEN
#define BLYNK_TOKEN ""
#endif
#ifndef TG_BOT_TOKEN
#define TG_BOT_TOKEN ""
#endif
#ifndef TG_CHAT_ID
#define TG_CHAT_ID ""
#endif
#ifndef SHEET_HOOK
#define SHEET_HOOK ""          // Google Sheet web address + ?key=... (instant rows + e-mail); "" = off
#endif

struct V9Svc { const char* name; bool on; uint32_t ok, fail; unsigned long nextTry, backoff, lastOk; char err[64]; };
int v9LastCode = 0;                       // last HTTP result (for clear error messages)
#ifndef V9_HOST_TEST
// Secure (HTTPS) connections need 2 x 16 KB buffers. The board package puts them in the small internal RAM
// (only ~20-70 KB free here, shared with Wi-Fi, Bluetooth and the camera) -> connections failed.
// This moves every big secure-connection buffer into the 8 MB PSRAM; small ones stay internal (fast).
static void* v9TlsCalloc(size_t n, size_t size) {
  size_t total = n * size;
  void* p = NULL;
  if (total >= 1024) p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!p) p = heap_caps_calloc(n, size, MALLOC_CAP_8BIT);
  return p;
}
static void v9TlsFree(void* p) { heap_caps_free(p); }
#endif
V9Svc v9Fb = {"firebase"}, v9Ts = {"thingspeak"}, v9Bl = {"blynk"}, v9Tg = {"telegram"}, v9Sh = {"sheet"};
uint32_t v9SheetSeq = 0;                 // last event sent straight to the Google Sheet (kept in NVS)
uint8_t v9BlMask = 0x3F;                 // Blynk pins V0..V5 that exist in your Blynk template (learned automatically)
bool v9BlButton = true;                  // V10 exists
unsigned long v9BlProbeAt = 0;
String v9IdToken = "", v9Refresh = "";
unsigned long v9TokenUntil = 0;
uint32_t v9CloudSeq = 0;           // last event copied to Firebase (kept in NVS)
String v9LastCmdId = "";
long v9TgOffset = 0;
char v9TgQueue[8][160];
volatile uint8_t v9TgHead = 0, v9TgTail = 0;

bool v9Ready(V9Svc& s) { return s.on && v8WifiOK && millis() >= s.nextTry; }
void v9Good(V9Svc& s) { s.ok++; s.backoff = 0; s.lastOk = millis(); s.err[0] = 0; }
void v9Bad(V9Svc& s, const char* why) {
  if (v9LastCode == -100) { s.nextTry = millis() + 2000; strlcpy(s.err, "waiting for free memory", sizeof(s.err)); return; }
  s.fail++;
  s.backoff = s.backoff ? min(s.backoff * 2, 60000UL) : 5000UL;           // 5 s, 10 s ... max 5 min
  s.nextTry = millis() + s.backoff;
  if (v9LastCode != 0 && v9LastCode != 200) snprintf(s.err, sizeof(s.err), "%s (%d)", why, v9LastCode);
  else strlcpy(s.err, why, sizeof(s.err));
  for (char* q = s.err; *q; q++) if (*q == '"' || *q == '\\' || (unsigned char)*q < 32) *q = ' ';   // keeps the status JSON valid
}

// small JSON helpers (Firestore REST + Telegram answers)
String v9Str(const String& j, const char* key, int from = 0) {          // "key": "value"  or  "key": {"stringValue": "value"}
  String k = String("\"") + key + "\"";
  int p = j.indexOf(k, from);
  if (p < 0) return "";
  p += k.length();
  int sv = j.indexOf("\"stringValue\"", p);
  int colon = j.indexOf(':', p);
  if (colon < 0) return "";
  int q1 = j.indexOf('"', colon + 1);
  if (sv >= 0 && sv < p + 40) q1 = j.indexOf('"', j.indexOf(':', sv) + 1);
  else { int brace = j.indexOf('{', colon); if (brace >= 0 && brace < q1) return ""; }
  if (q1 < 0) return "";
  String out = "";
  for (int i = q1 + 1; i < (int)j.length() && i < q1 + 4000; i++) {   // Firebase login tokens are ~1000 characters
    char c = j[i];
    if (c == '\\' && i + 1 < (int)j.length()) { out += j[++i]; continue; }
    if (c == '"') return out;
    out += c;
  }
  return "";
}
long v9Num(const String& j, const char* key, int from = 0) {
  int p = j.indexOf(String("\"") + key + "\"", from);
  if (p < 0) return -1;
  p = j.indexOf(':', p);
  while (p >= 0 && p + 1 < (int)j.length() && (j[p + 1] == ' ' || j[p + 1] == '"')) p++;
  return p < 0 ? -1 : j.substring(p + 1).toInt();
}
String v9Digits(const String& j, const char* key, int from = 0) {   // number as text (no 32-bit overflow)
  int p = j.indexOf(String("\"") + key + "\"", from);
  if (p < 0) return "";
  p = j.indexOf(':', p) + 1;
  while (p > 0 && p < (int)j.length() && (j[p] == ' ' || j[p] == '"')) p++;
  String out = "";
  while (p < (int)j.length() && (isdigit((unsigned char)j[p]) || (j[p] == '-' && out.length() == 0))) out += j[p++];
  return out;
}
String v9Iso(uint32_t t) {
  if (t < 1600000000UL) t = v8NowReal();
  if (t < 1600000000UL) return "";
  time_t tt = t; struct tm g; gmtime_r(&tt, &g);
  char b[24]; strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%SZ", &g);
  return b;
}
const char* v9Severity(const char* type) {
  static const char* alerts[] = {"security_lock", "fake_pass", "proxy_blocked", "phone_refused", NULL};
  static const char* warns[] = {"unknown_card", "card_locked", "face_mismatch", "pass_reused", NULL};
  for (int i = 0; alerts[i]; i++) if (!strcmp(type, alerts[i])) return "alert";
  for (int i = 0; warns[i]; i++) if (!strcmp(type, warns[i])) return "warn";
  return strncmp(type, "entry", 5) == 0 ? "ok" : "info";
}

// Firestore document for one door event (same fields as the website and the Sheet use)
String v9EventDoc(uint32_t seq, uint32_t when, const char* type, const char* person, const char* name, const char* detail) {
  String iso = v9Iso(when);
  String j = "{\"fields\":{";
  j += "\"device\":{\"stringValue\":\"" DEVICE_ID "\"},";
  j += "\"seq\":{\"integerValue\":\"" + String(seq) + "\"},";
  if (iso.length()) j += "\"time\":{\"timestampValue\":\"" + iso + "\"},";
  j += "\"type\":{\"stringValue\":\"" + v8Json(type) + "\"},";
  j += "\"severity\":{\"stringValue\":\"" + String(v9Severity(type)) + "\"},";
  j += "\"person\":{\"stringValue\":\"" + v8Json(person) + "\"},";
  j += "\"name\":{\"stringValue\":\"" + v8Json(name) + "\"},";
  j += "\"detail\":{\"stringValue\":\"" + v8Json(detail) + "\"},";
  j += "\"source\":{\"stringValue\":\"door\"}}}";
  return j;
}

// status document: heartbeat + all 15 sensor slots
// ---- website mirror: OLED screen + Serial Monitor lines ----
String v9Oled[5];
volatile bool v9OledDirty = false;
uint32_t v9ConSent = 0;
void v9OledSet(const String& s, const String& a, const String& b, const String& c, const String& d) {
  if (s == v9Oled[0] && a == v9Oled[1] && b == v9Oled[2] && c == v9Oled[3] && d == v9Oled[4]) return;
  v9Oled[0] = s; v9Oled[1] = a; v9Oled[2] = b; v9Oled[3] = c; v9Oled[4] = d;
  v9OledDirty = true;
}
String v9ConsoleJson() {                 // new Serial Monitor lines since the last upload (max V9_CON_LINES)
  String j = "";
#ifndef V9_HOST_TEST
  uint32_t total = v9Console.total;
  if (total - v9ConSent > V9_CON_LINES) v9ConSent = total - V9_CON_LINES;
  int n = 0;
  for (uint32_t k = v9ConSent; k < total; k++) {
    char line[V9_CON_W];
    v9Console.copyLine(k, line);
    if (n++) j += ",";
    j += "{\"stringValue\":\"" + v8Json(line) + "\"}";
  }
  v9ConSent = total;
#endif
  return j;
}
String v9StatusDoc() {
  String j = "{\"fields\":{";
  j += "\"device\":{\"stringValue\":\"" DEVICE_ID "\"},\"version\":{\"stringValue\":\"8.2.5\"},";
  String iso = v9Iso(0);
  if (iso.length()) j += "\"seen\":{\"timestampValue\":\"" + iso + "\"},";
  j += "\"present\":{\"integerValue\":\"" + String(presentCount) + "\"},";
  j += "\"students\":{\"integerValue\":\"" + String(NUM_STUDENTS) + "\"},";
  j += "\"visitors\":{\"integerValue\":\"" + String(visitorCount) + "\"},";
  j += "\"wifi_rssi\":{\"integerValue\":\"" + String(v8WifiOK ? WiFi.RSSI() : 0) + "\"},";
  j += "\"heap\":{\"integerValue\":\"" + String(ESP.getFreeHeap()) + "\"},";
  j += "\"uptime\":{\"integerValue\":\"" + String(millis() / 1000UL) + "\"},";
  j += "\"events\":{\"integerValue\":\"" + String(v8Seq) + "\"},";
  j += "\"queue\":{\"integerValue\":\"" + String(v8Seq > v9CloudSeq ? v8Seq - v9CloudSeq : 0) + "\"},";
  j += "\"camera_ok\":{\"booleanValue\":" + String(cameraOK ? "true" : "false") + "},";
  j += "\"phone_key\":{\"booleanValue\":" + String(bleOK ? "true" : "false") + "},";
  j += "\"services\":{\"mapValue\":{\"fields\":{";
  V9Svc* sv[] = {&v9Fb, &v9Ts, &v9Bl, &v9Tg, &v9Sh};
  for (int i = 0; i < 5; i++) {
    if (i) j += ",";
    j += String("\"") + sv[i]->name + "\":{\"stringValue\":\"" + v8Json(!sv[i]->on ? "off" : sv[i]->err[0] ? sv[i]->err : "ok") + "\"}";
  }
  j += "}}},";
  j += "\"oled\":{\"arrayValue\":{\"values\":[";
  for (int i = 0; i < 5; i++) j += String(i ? "," : "") + "{\"stringValue\":\"" + v8Json(v9Oled[i].c_str()) + "\"}";
  j += "]}},\"console\":{\"arrayValue\":{\"values\":[" + v9ConsoleJson() + "]}},";
#ifndef V9_HOST_TEST
  j += "\"console_total\":{\"integerValue\":\"" + String(v9Console.total) + "\"},";
#endif
  j += "\"sensors\":{\"arrayValue\":{\"values\":[";
  for (int i = 0; i < V9_SLOTS; i++) {
    V9Slot& s = v9Slots[i];
    if (i) j += ",";
    j += "{\"mapValue\":{\"fields\":{\"id\":{\"stringValue\":\"" + String(s.id) + "\"},\"type\":{\"stringValue\":\"" + String(s.type) +
         "\"},\"unit\":{\"stringValue\":\"" + String(s.unit) + "\"},\"status\":{\"stringValue\":\"" + String(v9StatusWord(s.status)) + "\"}";
    if (s.status == V9_OK) j += ",\"value\":{\"doubleValue\":" + String(s.value, 2) + "}";
    j += "}}}";
  }
  j += "]}}}}";
  return j;
}

// one HTTPS request. Connections are opened per request (keeps free memory high for the camera + BLE).
int v9Http(const char* method, const String& url, const String& body, String& reply, const char* auth = NULL, int timeout = 15000) {
  reply = "";
  if (heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 10000 ||
      heap_caps_get_largest_free_block(MALLOC_CAP_DMA) < 6000) { v9LastCode = -100; return -100; }   // try again in 2 s
  WiFiClientSecure c;
  c.setInsecure();
  c.setTimeout(timeout / 1000);
  HTTPClient h;
  h.setTimeout(timeout);
  h.setConnectTimeout(timeout);
  if (!h.begin(c, url)) return -1;
  if (body.length()) h.addHeader("Content-Type", "application/json");
  if (auth) h.addHeader("Authorization", String("Bearer ") + auth);
  h.setFollowRedirects(strcmp(method, "GET") ? HTTPC_DISABLE_FOLLOW_REDIRECTS : HTTPC_STRICT_FOLLOW_REDIRECTS);   // POST: 302 = done
  int code = h.sendRequest(method, (uint8_t*)body.c_str(), body.length());
  reply = code > 0 ? h.getString() : String("");
  v9LastCode = code;
  if (code <= 0 || code >= 400) {
    String host = url.substring(8, url.indexOf('/', 8));
    Serial.printf("CLOUD fail: %s code %d, free %u KB, biggest block %u KB, wifi %d dBm %s\n", host.c_str(), code,
                  (unsigned)(ESP.getFreeHeap() / 1024), (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024),
                  v8WifiOK ? WiFi.RSSI() : 0, reply.substring(0, 60).c_str());
  }
  if (reply.length() > 6000) reply = reply.substring(0, 6000);
  h.end();
  return code;
}

// ---------------- Firebase (Firestore) ----------------
String v9DocUrl(const String& path) {
  return String("https://firestore.googleapis.com/v1/projects/") + FB_PROJECT_ID + "/databases/(default)/documents/" + path;
}
bool v9FbToken() {
  if (v9IdToken.length() && millis() < v9TokenUntil) return true;
  String reply, url, body;
  if (v9Refresh.length()) {
    url = String("https://securetoken.googleapis.com/v1/token?key=") + FB_API_KEY;
    body = "{\"grant_type\":\"refresh_token\",\"refresh_token\":\"" + v9Refresh + "\"}";
  } else {
    url = String("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=") + FB_API_KEY;
    body = String("{\"email\":\"") + FB_DEVICE_EMAIL + "\",\"password\":\"" + FB_DEVICE_PASS + "\",\"returnSecureToken\":true}";
  }
  int code = v9Http("POST", url, body, reply);
  if (code != 200) {
    v9Refresh = "";
    v9Bad(v9Fb, code == 400 ? "device login refused (check email/password)" : "login: no answer");
    return false;
  }
  String tok = v9Str(reply, "idToken");
  if (!tok.length()) tok = v9Str(reply, "id_token");
  String ref = v9Str(reply, "refreshToken");
  if (!ref.length()) ref = v9Str(reply, "refresh_token");
  if (!tok.length()) { v9Bad(v9Fb, "login: bad answer"); return false; }
  v9IdToken = tok;
  if (ref.length()) v9Refresh = ref;
  v9TokenUntil = millis() + 50UL * 60UL * 1000UL;                     // tokens live 60 min; renew after 50
  return true;
}

// copies up to 10 new events per round (oldest first). A repeated upload of the same event
// is harmless: its document id is fixed (door-1_<seq>), so Firebase keeps only one copy.
void v9FbEvents() {
  if (v8Seq <= v9CloudSeq || !v9FbToken()) return;
  String page = v8EventsJson(v9CloudSeq);
  int sent = 0, pos = 0;
  while (sent < 10) {
    int s = page.indexOf("{\"seq\":", pos);
    if (s < 0) break;
    int e = page.indexOf('}', s);
    String one = page.substring(s, e + 1);
    pos = e + 1;
    uint32_t seq = (uint32_t)v9Num(one, "seq");
    if (seq <= v9CloudSeq) continue;
    String doc = v9EventDoc(seq, (uint32_t)v9Num(one, "time"), v9Str(one, "type").c_str(), v9Str(one, "person").c_str(),
                            v9Str(one, "name").c_str(), v9Str(one, "detail").c_str());
    String reply;
    int code = v9Http("PATCH", v9DocUrl(String("events/" DEVICE_ID "_") + seq), doc, reply, v9IdToken.c_str());
    if (code == 401 || code == 403) { v9IdToken = ""; v9Bad(v9Fb, code == 403 ? "not allowed (rules / members list)" : "login expired"); return; }
    if (code != 200) { v9Bad(v9Fb, "upload failed"); return; }
    v9CloudSeq = seq;
    v8Store.putUInt("cloudseq", v9CloudSeq);
    sent++;
  }
  if (sent) v9Good(v9Fb);
}

void v9FbStatus() {
  if (!v9FbToken()) return;
  String reply;
  int code = v9Http("PATCH", v9DocUrl("devices/" DEVICE_ID), v9StatusDoc(), reply, v9IdToken.c_str());
  if (code == 200) v9Good(v9Fb);
  else { if (code == 401) v9IdToken = ""; v9Bad(v9Fb, code == 403 ? "not allowed (rules / members list)" : "status failed"); }
}

// website buttons: the website writes commands/door-1 {cmd, id}; the board runs it once
void v9FbCommands() {
  if (!v9FbToken()) return;
  String reply;
  int code = v9Http("GET", v9DocUrl("commands/" DEVICE_ID), "", reply, v9IdToken.c_str());
  if (code == 404) { v9Good(v9Fb); return; }                              // no command yet
  if (code != 200) { if (code == 401) v9IdToken = ""; v9Bad(v9Fb, "commands failed"); return; }
  String id = v9Str(reply, "id"), cmd = v9Str(reply, "cmd"), done = v9Str(reply, "done_id");
  if (!id.length() || id == done || id == v9LastCmdId) return;
  v9LastCmdId = id;
  cmd.toLowerCase();
  bool ok = v8CommandAllowed(cmd);
  if (ok) {
    V8Cmd m;
    strlcpy(m.text, cmd.c_str(), sizeof(m.text));
    ok = v8CmdQueue && xQueueSend(v8CmdQueue, &m, 0) == pdTRUE;
  }
  String upd = "{\"fields\":{\"done_id\":{\"stringValue\":\"" + v8Json(id.c_str()) + "\"},\"result\":{\"stringValue\":\"" +
               String(ok ? "done" : "refused") + "\"}" + (v9Iso(0).length() ? ",\"done_at\":{\"timestampValue\":\"" + v9Iso(0) + "\"}" : String("")) + "}}";
  v9Http("PATCH", v9DocUrl("commands/" DEVICE_ID) + "?updateMask.fieldPaths=done_id&updateMask.fieldPaths=result&updateMask.fieldPaths=done_at",
         upd, reply, v9IdToken.c_str());
  Serial.printf(">>> CLOUD COMMAND %s: %s\n", ok ? "accepted" : "REFUSED", cmd.c_str());
}

// ---------------- ThingSpeak (graphs of all sensors) ----------------
String v9TsUrl(int ch) {                       // ch 0 = sensors 1-8, ch 1 = sensors 9-15
  String u = String("https://api.thingspeak.com/update?api_key=") + (ch == 0 ? TS_WRITE_KEY_A : TS_WRITE_KEY_B);
  int n = 0;
  for (int i = ch * 8; i < min(V9_SLOTS, ch * 8 + 8); i++) {
    V9Slot& s = v9Slots[i];
    if (s.status != V9_OK) continue;
    u += "&field" + String(i - ch * 8 + 1) + "=" + String(s.value, 2);
    n++;
  }
  if (ch == 1) { u += "&field8=" + String(visitorCount); n++; }      // B field 8: visitors today
  return n ? u : String("");
}
void v9TsSend(int ch) {
  if (!strlen(ch == 0 ? TS_WRITE_KEY_A : TS_WRITE_KEY_B)) return;
  String url = v9TsUrl(ch), reply;
  if (!url.length()) return;
  int code = v9Http("GET", url, "", reply);
  if (code == 200 && reply.toInt() > 0) v9Good(v9Ts);
  else v9Bad(v9Ts, code == 200 ? "too fast or bad key" : "no answer");
}

// ---------------- Blynk (quick phone panel) ----------------
// V0 online, V1 present, V2 room temp (or chip temp), V3 alerts, V4 Wi-Fi dBm, V5 visitors.  V10 = button (1 = open door)
String v9LastEvent = "";
uint16_t v9OpenAlerts = 0;
String v9BlVal(int pin) {
  switch (pin) {
    case 0: return "1";
    case 1: return String(presentCount);
    case 2: return String(v9Slots[4].status == V9_OK ? v9Slots[4].value : v9Slots[0].value, 1);
    case 3: return String(v9OpenAlerts);
    case 4: return String(v8WifiOK ? WiFi.RSSI() : 0);
    default: return String(visitorCount);
  }
}
String v9BlMissing() {
  String m = "";
  for (int p = 0; p < 6; p++) if (!(v9BlMask & (1 << p))) m += String(m.length() ? "," : "") + "V" + p;
  if (!v9BlButton) m += String(m.length() ? "," : "") + "V10";
  return m;
}
void v9BlynkSync() {
  String reply, base = String("https://blynk.cloud/external/api/");
  if (v9BlProbeAt && millis() - v9BlProbeAt > 10UL * 60UL * 1000UL) { v9BlMask = 0x3F; v9BlButton = true; v9BlProbeAt = 0; }   // re-check every 10 min
  String url = base + "batch/update?token=" + BLYNK_TOKEN;
  for (int p = 0; p < 6; p++) if (v9BlMask & (1 << p)) url += "&V" + String(p) + "=" + v9BlVal(p);
  int code = v9BlMask ? v9Http("GET", url, "", reply) : 200;
  if (code == 400 && reply.indexOf("doesn't exist") >= 0) {         // a pin is missing in the Blynk template: find which
    for (int p = 0; p < 6; p++) {
      if (!(v9BlMask & (1 << p))) continue;
      int c = v9Http("GET", base + "update?token=" + BLYNK_TOKEN + "&V" + String(p) + "=" + v9BlVal(p), "", reply);
      if (c == 400) v9BlMask &= ~(1 << p);
      else if (c != 200) { v9Bad(v9Bl, "no answer"); return; }
    }
    v9BlProbeAt = millis();
    code = 200;
  }
  if (code != 200) { v9Bad(v9Bl, code == 400 ? (String("Blynk: ") + reply.substring(0, 36)).c_str() : "no answer"); return; }
  if (v9BlButton) {
    code = v9Http("GET", base + "get?token=" + BLYNK_TOKEN + "&V10", "", reply);
    if (code == 400 && reply.indexOf("any value") >= 0) {              // V10 exists, never pressed yet: give it 0 once
      v9Http("GET", base + "update?token=" + BLYNK_TOKEN + "&V10=0", "", reply);
      code = 200; reply = "0";
    }
    if (code == 400) { v9BlButton = false; v9BlProbeAt = millis(); }
    else if (code == 200 && reply.toInt() == 1) {
      V8Cmd m; strlcpy(m.text, "opendoor", sizeof(m.text));
      if (v8CmdQueue) xQueueSend(v8CmdQueue, &m, 0);
      v9Http("GET", base + "update?token=" + BLYNK_TOKEN + "&V10=0", "", reply);
      Serial.println(">>> BLYNK: open door button");
    }
  }
  v9Good(v9Bl);
  String miss = v9BlMissing();
  if (miss.length()) snprintf(v9Bl.err, sizeof(v9Bl.err), "working, but add in Blynk: %s", miss.c_str());
}

// ---------------- Telegram (instant technical alerts + /status) ----------------
void v9TgQueueMsg(const String& text) {
  if (!strlen(TG_BOT_TOKEN)) return;
  uint8_t nh = (v9TgHead + 1) % 8;
  if (nh == v9TgTail) return;                                         // full: drop (rate limit)
  strlcpy(v9TgQueue[v9TgHead], text.c_str(), sizeof(v9TgQueue[0]));
  v9TgHead = nh;
}
bool v9TgSend(const char* chat, const String& text) {
  String body = String("{\"chat_id\":\"") + chat + "\",\"text\":\"" + v8Json(text.c_str()) + "\"}", reply;
  int code = v9Http("POST", String("https://api.telegram.org/bot") + TG_BOT_TOKEN + "/sendMessage", body, reply);
  if (code == 200) { v9Good(v9Tg); return true; }
  v9Bad(v9Tg, code == 401 ? "bad bot token" : code == 400 ? "chat not found (send /start)" : "no answer");
  return false;
}
String v9StatusText() {
  String t = String("SARTHIK Room door\nPresent: ") + presentCount + "/" + NUM_STUDENTS + "  Visitors: " + visitorCount +
             "\nWi-Fi " + (v8WifiOK ? WiFi.RSSI() : 0) + " dBm  Events " + v8Seq + "\n";
  for (int i = 0; i < V9_SLOTS; i++)
    if (v9Slots[i].status == V9_OK) t += String(v9Slots[i].type) + ": " + String(v9Slots[i].value, 1) + " " + v9Slots[i].unit + "\n";
  return t;
}
void v9TgPoll() {
  String reply;
  int code = v9Http("GET", String("https://api.telegram.org/bot") + TG_BOT_TOKEN + "/getUpdates?timeout=0&limit=5&offset=" + String(v9TgOffset), "", reply);
  if (code != 200) { v9Bad(v9Tg, code == 401 ? "bad bot token" : "no answer"); return; }
  v9Good(v9Tg);
  int pos = 0;
  while (true) {
    int u = reply.indexOf("\"update_id\"", pos);
    if (u < 0) break;
    long id = v9Num(reply, "update_id", u);
    int next = reply.indexOf("\"update_id\"", u + 10);
    String one = reply.substring(u, next < 0 ? reply.length() : next);
    pos = u + 10;
    v9TgOffset = id + 1;
    String chat = v9Digits(one, "id", one.indexOf("\"chat\""));
    String text = v9Str(one, "text");
    if (chat != TG_CHAT_ID) continue;                                  // only YOUR chat may talk to the board
    if (text.startsWith("/status") || text.startsWith("/start")) v9TgSend(TG_CHAT_ID, v9StatusText());
    else if (text.startsWith("/open")) {
      V8Cmd m; strlcpy(m.text, "opendoor", sizeof(m.text));
      if (v8CmdQueue) xQueueSend(v8CmdQueue, &m, 0);
      v9TgSend(TG_CHAT_ID, "Door opening.");
    } else if (text.startsWith("/help")) v9TgSend(TG_CHAT_ID, "/status - room now\n/open - open the door\n/help - this list");
  }
}

// called by v8Event (door core) for every event: only short work here
void v9OnEvent(uint8_t type, const char* name, const char* detail) {
  const char* w = v8TypeWord(type);
  v9LastEvent = String(w) + " " + name;
  const char* sev = v9Severity(w);
  if (!strncmp(w, "entry", 5)) {
    const char* how = !strcmp(w, "entry_phone") ? "phone key" : !strcmp(w, "entry_visitor_qr") ? "visitor QR pass" : "card + face";
    v9TgQueueMsg(String("ENTRY: ") + name + " (" + how + ") at " + v8Clock(v8NowReal()) + (detail[0] ? String("\n") + detail : String("")));
  }
  if (!strcmp(sev, "alert") || type == V8_UNKNOWN_CARD) {
    v9OpenAlerts++;
    v9TgQueueMsg(String("SECURITY ALERT\n") + w + ": " + name + (detail[0] ? String(" (") + detail + ")" : String("")) +
                 "\nDevice: " DEVICE_ID "\nTime: " + v8Clock(v8NowReal()));
  }
}

// ---------------- Google Sheet (instant rows + e-mail; the Sheet also collects everything from Firebase every 5 min) ----
void v9SheetEvents() {
  if (v8Seq <= v9SheetSeq) return;
  String page = v8EventsJson(v9SheetSeq);
  int pos = 0, sent = 0;
  while (sent < 3) {
    int s = page.indexOf("{\"seq\":", pos);
    if (s < 0) { v9SheetSeq = v8Seq; break; }
    int e = page.indexOf('}', s);
    String one = page.substring(s, e + 1);
    pos = e + 1;
    uint32_t seq = (uint32_t)v9Num(one, "seq");
    if (seq <= v9SheetSeq) continue;
    String type = v9Str(one, "type");
    const char* sev = v9Severity(type.c_str());
    if (!strcmp(sev, "info")) { v9SheetSeq = seq; continue; }          // only entries, warnings and alerts
    uint32_t evt = (uint32_t)v9Num(one, "time");
    if (evt > 1600000000UL && v8NowReal() > evt + 600) { v9SheetSeq = seq; continue; }   // older than 10 min: the Sheet's 1-minute job has it
    String body = String("{\"via\":\"board\",\"id\":\"" DEVICE_ID "_") + seq + "\",\"seq\":" + seq + ",\"ts\":" + String((uint32_t)v9Num(one, "time")) +
                  ",\"type\":\"" + v8Json(type.c_str()) + "\",\"severity\":\"" + sev + "\",\"person\":\"" + v8Json(v9Str(one, "person").c_str()) +
                  "\",\"name\":\"" + v8Json(v9Str(one, "name").c_str()) + "\",\"detail\":\"" + v8Json(v9Str(one, "detail").c_str()) +
                  "\",\"device\":\"" DEVICE_ID "\",\"source\":\"door\"}";
    String reply;
    int code = v9Http("POST", SHEET_HOOK, body, reply);
    if (code != 200 && code != 302) { v9Bad(v9Sh, "no answer"); return; }   // Google answers 302 after running the script
    v9SheetSeq = seq;
    sent++;
  }
  v8Store.putUInt("sheetseq", v9SheetSeq);
  v9Good(v9Sh);
}

// ---------------- the network-core step (called every 10 ms; does at most one thing) ----------------
void v9CloudLoop() {
  if (!v8WifiOK) return;
  static unsigned long tStatus = 0, tCmd = 0, tEv = 0, tTs = 0, tBl = 0, tTgPoll = 0;
  static uint8_t tsCh = 0;
  unsigned long now = millis();
  if (!tBl) tBl = now - V9_BLYNK_MS + 30000;          // first Blynk update 30 s after start
  if (v9TgHead != v9TgTail && v9Ready(v9Tg)) {                          // 1) urgent alerts first
    if (v9TgSend(TG_CHAT_ID, v9TgQueue[v9TgTail])) v9TgTail = (v9TgTail + 1) % 8;
    return;
  }
  if (v9Ready(v9Sh) && v8Seq > v9SheetSeq) { v9SheetEvents(); return; }          // instant Sheet row + e-mail
  if (v9Ready(v9Fb) && now - tEv > 3000 && v8Seq > v9CloudSeq) { tEv = now; v9FbEvents(); return; }
  if (v9Ready(v9Fb) && now - tCmd > V9_CMD_POLL_MS) { tCmd = now; v9FbCommands(); return; }
  if (v9Ready(v9Fb) && (now - tStatus > V9_STATUS_MS || (v9OledDirty && now - tStatus > 8000))) { tStatus = now; v9OledDirty = false; v9FbStatus(); return; }
  if (v9Ready(v9Ts) && now - tTs > V9_TS_MS) { tTs = now; v9TsSend(tsCh); tsCh ^= 1; return; }
  if (v9Ready(v9Bl) && now - tBl > V9_BLYNK_MS) { tBl = now; v9BlynkSync(); return; }
  if (v9Ready(v9Tg) && now - tTgPoll > V9_TG_POLL_MS) { tTgPoll = now; v9TgPoll(); return; }
}

void v9Setup() {
#ifndef V9_HOST_TEST
  if (psramFound()) mbedtls_platform_set_calloc_free(v9TlsCalloc, v9TlsFree);
#endif
  v9Fb.on = strlen(FB_API_KEY) && strlen(FB_PROJECT_ID) && strlen(FB_DEVICE_EMAIL);
  v9Ts.on = strlen(TS_WRITE_KEY_A) > 0;
  v9Bl.on = strlen(BLYNK_TOKEN) > 0;
  v9Tg.on = strlen(TG_BOT_TOKEN) && strlen(TG_CHAT_ID);
  v9Sh.on = strncmp(SHEET_HOOK, "https://script.google.com/", 26) == 0;
  if (v9Sh.on) {
    if (!v8Store.isKey("sheetseq")) v8Store.putUInt("sheetseq", v8Seq);   // first time: start from now (no flood of old events)
    v9SheetSeq = v8Store.getUInt("sheetseq", v8Seq);
    if (v9SheetSeq > v8Seq) v9SheetSeq = v8Seq;
  }
  v9CloudSeq = v8Store.getUInt("cloudseq", 0);
  if (v9CloudSeq > v8Seq) v9CloudSeq = 0;                              // board memory was cleared
  Serial.printf("Cloud: Firebase %s, ThingSpeak %s, Blynk %s, Telegram %s, Sheet %s. Sensors on: ",
                v9Fb.on ? "ON" : "off", v9Ts.on ? "ON" : "off", v9Bl.on ? "ON" : "off", v9Tg.on ? "ON" : "off", v9Sh.on ? "ON" : "off");
  for (int i = 0; i < V9_SLOTS; i++) if (v9Slots[i].enabled) Serial.printf("%s ", v9Slots[i].type);
  Serial.println();
}

void v9PrintCloud() {
  Serial.println("---- Cloud links (keys in secrets.h; empty key = off) ----");
  V9Svc* sv[] = {&v9Fb, &v9Ts, &v9Bl, &v9Tg, &v9Sh};
  for (int i = 0; i < 5; i++)
    Serial.printf("  %-10s %-4s ok %-6lu failed %-5lu %s\n", sv[i]->name, sv[i]->on ? "ON" : "off", (unsigned long)sv[i]->ok,
                  (unsigned long)sv[i]->fail, sv[i]->err[0] ? sv[i]->err : (sv[i]->ok ? "working" : ""));
  Serial.printf("  events waiting for Firebase: %lu\n", (unsigned long)(v8Seq > v9CloudSeq ? v8Seq - v9CloudSeq : 0));
}
void v9PrintSensors() {
  Serial.println("---- Sensor slots (edit the table in the v8.2 block to add sensors) ----");
  for (int i = 0; i < V9_SLOTS; i++) {
    V9Slot& s = v9Slots[i];
    Serial.printf("  %s %-13s %-7s %-6s", s.id, s.type, s.unit, v9StatusWord(s.status));
    if (s.status == V9_OK) Serial.printf(" %.2f", s.value);
    Serial.println();
  }
}

// ================= Serial Monitor commands =================
void printHelp() {
  Serial.println("---- Commands ----");
  Serial.println("  list           -> which cards have a face saved");
  Serial.println("  report         -> today's attendance");
  Serial.println("  reset          -> start a new day (keeps faces)");
  Serial.println("  forget <name>  -> delete one student's face");
  Serial.println("  clear          -> delete ALL saved faces");
  Serial.println("  visitors       -> visitors let in since power-on");
  Serial.println("  clearpasses    -> forget used passes (testing only)");
  Serial.println("  addphone <name>   -> allow one new phone for 60 s");
  Serial.println("  blecal            -> measure the 30 cm opening distance");
  Serial.println("  resetphones       -> delete ALL phone keys");
  Serial.println("  phones            -> paired phones + live signal");
  Serial.println("  removephone <name>-> remove a phone (lost/stolen)");
  Serial.println("  bleopen <dBm>     -> opening level by hand, e.g. bleopen -50");
  Serial.println("  blelive           -> live signal print on/off");
  Serial.println("  log               -> last 20 door openings");
  Serial.println("  events            -> last 40 door events (all are kept permanently)");
  Serial.println("  cloud             -> Firebase / ThingSpeak / Blynk / Telegram status");
  Serial.println("  sensors           -> the 15 sensor slots and their values");
  Serial.println("  stats             -> per-person statistics (kept permanently)");
  Serial.println("  wifi              -> Wi-Fi, board address + door key for the website");
  Serial.println("  wifi off | wifi on-> Wi-Fi completely off (exactly like v6) / on again");
  Serial.println("  learncard <n>     -> give <n> a NEW card: tap it within 60 s, then the face is saved");
  Serial.println("  opendoor          -> open the door once (also from the website)");
  Serial.println("  alerts on|off     -> phone notifications for security events");
  Serial.println("  testalert         -> send a test phone notification");
  Serial.println("  restart           -> restart the board");
  Serial.println("------------------");
}

void listStudents() {
  Serial.println("---- Registered students ----");
  for (int i = 0; i < NUM_STUDENTS; i++) {
    Serial.print("  ");
    Serial.print(students[i].name);
    Serial.print("  [");
    Serial.print(students[i].uid);
    Serial.print("]  -> ");
    Serial.println(hasFace[i] ? "face saved" : "NO FACE YET (tap card to enroll)");
  }
  Serial.println("-----------------------------");
}

void printReport() {
  Serial.println("---- Attendance report ----");
  for (int i = 0; i < NUM_STUDENTS; i++) {
    Serial.print("  ");
    Serial.print(students[i].name);
    Serial.println(markedPresent[i] ? "  PRESENT" : "  ABSENT");
  }
  Serial.printf("  Total present: %d of %d\n", presentCount, NUM_STUDENTS);
  Serial.println("---------------------------");
}

void handleSerialCommands() {
  if (!Serial.available()) return;
  handleCommandText(Serial.readStringUntil('\n'));
}

// v8.1: one place for all commands (Serial Monitor AND website)
void handleCommandText(String cmd) {
  cmd.trim();
  String lower = cmd;
  lower.toLowerCase();

  if (lower == "help") {
    printHelp();
  } else if (lower == "list") {
    listStudents();
  } else if (lower == "report") {
    printReport();
  } else if (lower == "reset") {
    for (int i = 0; i < NUM_STUDENTS; i++) markedPresent[i] = false;
    presentCount = 0;
    visitorCount = 0;
    Serial.println("New day started: attendance and visitor count cleared (faces kept).");
    standby();
  } else if (lower.startsWith("forget ")) {
    String name = cmd.substring(7);
    name.trim();
    int idx = findStudentByName(name);
    if (idx < 0) {
      Serial.println("No student called '" + name + "'. Type 'list' to see names.");
      return;
    }
    faceStore.remove(keyFor(students[idx].uid).c_str());
    Serial.printf("Deleted %s's face. Restarting...\n", students[idx].name);
    delay(500);
    ESP.restart();
  } else if (lower == "visitors") {
    Serial.println("---- Visitors since power-on ----");
    int shown = (visitorCount < VISITOR_LOG_SIZE) ? visitorCount : VISITOR_LOG_SIZE;
    for (int i = 0; i < shown; i++) {
      int slot = (visitorCount - shown + i) % VISITOR_LOG_SIZE;
      Serial.println("  " + visitorLog[slot]);
    }
    Serial.printf("  Total visitors: %d\n", visitorCount);
    Serial.println("--------------------------------");
  } else if (lower.startsWith("addphone ")) {
    openPairWindow(cmd.substring(9));
  } else if (lower == "addphone") {
    Serial.println("Type a name too, e.g.  addphone teacher");
  } else if (lower == "phones") {
    listPhones();
  } else if (lower.startsWith("removephone ")) {
    removePhone(cmd.substring(12));
  } else if (lower == "blecal") {
    calibrateBle();
  } else if (lower == "resetphones") {
    resetPhones();
  } else if (lower.startsWith("bleopen ")) {
    setBleThreshold(cmd.substring(8));
  } else if (lower == "blelive") {
    bleLive = !bleLive;
    Serial.println(bleLive ? "Live signal print ON (type blelive again to stop)" : "Live signal print OFF");
  } else if (lower == "cloud") {
    v9PrintCloud();
  } else if (lower == "sensors") {
    v9PrintSensors();
  } else if (lower == "events") {
    v8PrintEvents();
  } else if (lower == "stats") {
    v8PrintStats();
  } else if (lower == "wifi") {
    v8PrintWifi();
  } else if (lower == "wifi off" || lower == "wifi on") {
    v8NetOff = (lower == "wifi off");
    v8Store.putBool("netoff", v8NetOff);
    Serial.println(v8NetOff ? "Switching Wi-Fi OFF (door works exactly like v6)..." : "Switching Wi-Fi ON...");
  } else if (lower == "alerts on" || lower == "alerts off") {
    v8AlertsOn = lower.endsWith("on");
    v8Store.putBool("alerts", v8AlertsOn);
    Serial.println(v8AlertsOn ? "Phone alerts ON" : "Phone alerts OFF");
  } else if (lower == "testalert") {
    v8QueueAlert("CLASSYNC test", "If you can read this, door alerts reach your phone.", "default");
    Serial.println("Test alert queued (sent in the background).");
  } else if (lower == "opendoor") {
    v8OpenDoorRemote("website");
  } else if (lower.startsWith("learncard ")) {
    String who = cmd.substring(10);
    who.trim();
    v8StartLearn(who);
  } else if (lower == "cancelcard") {
    v8LearnIdx = -1;
    Serial.println("New-card mode cancelled.");
    standby();
  } else if (lower == "restart") {
    Serial.println("Restarting...");
    delay(300);
    ESP.restart();
  } else if (lower == "log") {
    printAccessLog();
  } else if (lower == "clearpasses") {
    passStore.clear();
    Serial.println("All used-pass records deleted. Old passes will work once more.");
  } else if (lower == "clear") {
    faceStore.clear();
    Serial.println("Deleted ALL saved faces. Restarting...");
    delay(500);
    ESP.restart();
  } else if (lower.length() > 0) {
    printHelp();
  }
}

// ================= setup =================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n===== CLASSYNC 4.0 v8.2.1: RFID + FACE + QR + PHONE KEY + WEBSITE + CLOUD + 15 SENSOR SLOTS =====");

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  allLedsOff();
  digitalWrite(BUZZER, LOW);

  for (int i = 0; i < MAX_FACE_IDS; i++) idToStudent[i] = -1;
  for (int i = 0; i < NUM_STUDENTS; i++) {
    hasFace[i] = false;
    markedPresent[i] = false;
    wrongTries[i] = 0;
    lockedUntil[i] = 0;
  }

  // 1) Servo door first, so it is locked immediately at power-up
  ledcSetup(SERVO_CH, 50, 14);
  ledcAttachPin(SERVO_PIN, SERVO_CH);
  servoWrite(DOOR_LOCKED);

  // 2) OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  oledOK = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  Serial.println(oledOK ? "OLED OK" : "OLED NOT FOUND -> check VCC/GND/SDA=41/SCL=42");
  screen("STARTING", "Camera...", "Face memory...", "RFID reader...", "Please wait");

  // 3) Camera
  if (!psramFound()) Serial.println("WARNING: PSRAM not found -> Tools > PSRAM > OPI PSRAM");
  rgbBuf = (uint8_t*)ps_malloc(RGB_BUF_SIZE);
  if (rgbBuf == NULL) Serial.println("ERROR: could not reserve memory for the photo");
  cameraOK = (rgbBuf != NULL) && initCamera();

  v8LoadCards();   // v8.1: cards added with "learncard" (before faces are loaded)

  // 4) Permanent face memory
  faceStore.begin("classyncface", false);
  int loaded = loadFacesFromMemory();
  Serial.printf("Faces loaded from permanent memory: %d of %d students\n", loaded, NUM_STUDENTS);

  // 5) Visitor QR pass reader
  passStore.begin("classyncpass", false);
  if (cameraOK) {
    qrReader = quirc_new();
    if (qrReader == NULL || quirc_resize(qrReader, 240, 240) < 0) {
      Serial.println("QR reader: not enough memory, visitor passes disabled");
      if (qrReader) quirc_destroy(qrReader);
      qrReader = NULL;
    } else {
      Serial.println("QR reader OK (visitor passes enabled)");
    }
  }

  // 6) Phone key (Bluetooth LE)
  bleSetup();

  // 7) RFID reader
  SPI.begin(RFID_SCK, RFID_MISO, RFID_MOSI, RFID_SS);
  rfid.PCD_Init();
  byte v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  bool rfidOK = !(v == 0x00 || v == 0xFF);
  Serial.printf("RC522 version 0x%02X %s\n", v, rfidOK ? "-> OK" : "-> NOT FOUND, check wiring");

  // 7) v8.1: website link on the second CPU core (never slows the door)
  v8Setup();
  v9Setup();          // v8.2 cloud + sensors

  listStudents();
  Serial.println("Ready. Tap a card. Type 'help' for commands.");

  if (!cameraOK) {
    screen("CAM ERROR", "Camera not working", "Reseat camera cable", "Check PSRAM setting", "See Serial Monitor");
  } else if (!rfidOK) {
    screen("RFID ERR", "RFID reader missing", "Check 7 RFID wires", "RST and 3.3V to +", "See Serial Monitor");
  } else {
    screen("READY", "Faces loaded: " + String(loaded) + "/" + String(NUM_STUDENTS),
           "Camera OK", "RFID OK", bleOK ? "Phone key: ON" : "Phone key: OFF");
    delay(1200);
    standby();
  }
}

// ================= loop =================
void loop() {
  v8Loop();    // v8.1: website commands + quick checks (all network work runs on the other core)
  v9SensorsLoop();   // v8.2: reads at most one due sensor (a few ms)
  handleSerialCommands();
  bleLoop();   // phone key: events, pairing mode, signal strength, hands-free opening

  // Refresh the idle screen every 10 s (updates uptime)
  if (!pairWindowOpen && millis() - lastIdleDraw > 10000) standby();

  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    // No card: look for a visitor QR pass instead
    if (qrReader != NULL && cameraOK && millis() - lastQrScan >= QR_SCAN_EVERY_MS) {
      lastQrScan = millis();
      scanVisitorPass();
    }
    delay(20);
    return;
  }

  String uid = readUID();
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  Serial.println("Card tapped: [" + uid + "]");

  int idx = findStudent(uid);
  if (idx < 0) idx = v8LearnCard(uid);   // v8.1: "learncard <name>" is waiting for a new card

  // Wrong card
  if (idx < 0) {
    Serial.println(">>> ACCESS DENIED: unknown card");
    v8Event(V8_UNKNOWN_CARD, -1, "unknown card", uid.c_str());
    screen("DENIED", "Unknown card", "UID: " + uid, "Not registered", "Door LOCKED");
    warningAlarm(PULSES_UNKNOWN_CARD);
    standby();
    return;
  }

  // Card locked after too many wrong faces
  if (lockedUntil[idx] > millis()) {
    unsigned long secs = (lockedUntil[idx] - millis()) / 1000 + 1;
    Serial.printf(">>> CARD LOCKED: %s (%lu s left)\n", students[idx].name, secs);
    v8Event(V8_CARD_LOCKED, idx, students[idx].name, (String(secs) + " s left").c_str());
    screen("LOCKED", students[idx].name, "Too many wrong faces",
           "Wait " + String(secs) + " seconds", "Door LOCKED");
    warningAlarm(PULSES_CARD_LOCKED);
    standby();
    return;
  }

  if (!cameraOK) {
    screen("CAM ERROR", "Camera not working", "Reseat camera cable", "", "Door LOCKED");
    warningAlarm(PULSES_NO_FACE);
    standby();
    return;
  }

  if (!hasFace[idx]) {
    // First time this card is used: save the face
    enrollFace(idx);
  } else {
    // Card already has a face: check it
    screen("CHECKING", students[idx].name, "Card OK", "Look at the camera", "Checking face...");
    chirp();
    delay(300);

    float sim = 0.0f;
    int result = verifyFace(idx, sim);

    if (result == 1) {
      wrongTries[idx] = 0;
      grantAccess(idx, sim);
    } else if (result == 0) {
      // Wrong face
      wrongTries[idx]++;
      v8Event(V8_FACE_MISMATCH, idx, students[idx].name, ("try " + String(wrongTries[idx]) + " of " + String(MAX_WRONG_TRIES)).c_str());
      Serial.printf(">>> REJECTED: face does not match %s (wrong try %d of %d)\n",
                    students[idx].name, wrongTries[idx], MAX_WRONG_TRIES);
      if (wrongTries[idx] >= MAX_WRONG_TRIES) {
        wrongTries[idx] = 0;
        lockedUntil[idx] = millis() + LOCK_TIME_MS;
        v8Event(V8_SECURITY_LOCK, idx, students[idx].name, "3 wrong faces");
        Serial.printf(">>> SECURITY ALERT: %s's card locked for %d seconds\n",
                      students[idx].name, LOCK_TIME_MS / 1000);
        screen("ALERT!", "Card: " + String(students[idx].name), "3 wrong faces in row",
               "Card LOCKED 30 s", "Door LOCKED");
        warningAlarm(PULSES_SECURITY);
      } else {
        screen("REJECTED", "Card: " + String(students[idx].name), "Face does NOT match",
               "Try " + String(wrongTries[idx]) + " of " + String(MAX_WRONG_TRIES),
               "Door LOCKED");
        warningAlarm(PULSES_WRONG_FACE);
      }
    } else {
      Serial.println(">>> NO FACE SEEN");
      v8Event(V8_NO_FACE, idx, students[idx].name, "");
      screen("NO FACE", students[idx].name, "Face not seen", "Face the camera", "Tap card again");
      warningAlarm(PULSES_NO_FACE);
    }
  }

  delay(500);
  standby();
}
