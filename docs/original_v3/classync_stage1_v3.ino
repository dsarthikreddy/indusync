/*
  CLASSYNC 4.0 - STAGE 1 (VERSION 3)
  RFID card + ON-BOARD face recognition (Edge AI) + attendance + servo door
  Needs: esp32 board package version 2.0.17 (NOT 3.x)
  No Wi-Fi and no laptop needed: everything runs on the ESP32-S3.

  FEATURES
  - Faces saved PERMANENTLY with their cards (survive unplugging and re-uploading)
  - Enrollment takes 3 photos and averages them for better accuracy
  - One face can only belong to ONE card (blocks proxy enrollment)
  - Card locked for 30 s after 3 wrong faces in a row
  - Match   -> BOTH LEDs ON + double chirp + door opens instantly
  - Warning -> ONLY red LED blinks + loud pulsing alarm

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
  screen("SCAN CARD",
         "Tap card, look at cam",
         "Faces saved: " + String(countFacesSaved()) + "/" + String(NUM_STUDENTS),
         "Present today: " + String(presentCount) + "/" + String(NUM_STUDENTS),
         "Door LOCKED  Up:" + String(mins) + "m");
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
  successSignalAndOpenDoor();
}

// ================= Serial Monitor commands =================
void printHelp() {
  Serial.println("---- Commands ----");
  Serial.println("  list           -> which cards have a face saved");
  Serial.println("  report         -> today's attendance");
  Serial.println("  reset          -> start a new day (keeps faces)");
  Serial.println("  forget <name>  -> delete one student's face");
  Serial.println("  clear          -> delete ALL saved faces");
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
  String cmd = Serial.readStringUntil('\n');
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
    Serial.println("New day started: attendance cleared (faces kept).");
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
  Serial.println("\n===== CLASSYNC 4.0 v3: RFID + ON-BOARD FACE RECOGNITION =====");

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

  // 4) Permanent face memory
  faceStore.begin("classyncface", false);
  int loaded = loadFacesFromMemory();
  Serial.printf("Faces loaded from permanent memory: %d of %d students\n", loaded, NUM_STUDENTS);

  // 5) RFID reader
  SPI.begin(RFID_SCK, RFID_MISO, RFID_MOSI, RFID_SS);
  rfid.PCD_Init();
  byte v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  bool rfidOK = !(v == 0x00 || v == 0xFF);
  Serial.printf("RC522 version 0x%02X %s\n", v, rfidOK ? "-> OK" : "-> NOT FOUND, check wiring");

  listStudents();
  Serial.println("Ready. Tap a card. Type 'help' for commands.");

  if (!cameraOK) {
    screen("CAM ERROR", "Camera not working", "Reseat camera cable", "Check PSRAM setting", "See Serial Monitor");
  } else if (!rfidOK) {
    screen("RFID ERR", "RFID reader missing", "Check 7 RFID wires", "RST and 3.3V to +", "See Serial Monitor");
  } else {
    screen("READY", "Faces loaded: " + String(loaded) + "/" + String(NUM_STUDENTS),
           "Camera OK", "RFID OK", "System starting...");
    delay(1200);
    standby();
  }
}

// ================= loop =================
void loop() {
  handleSerialCommands();

  // Refresh the idle screen every 10 s (updates uptime)
  if (millis() - lastIdleDraw > 10000) standby();

  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    delay(30);
    return;
  }

  String uid = readUID();
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  Serial.println("Card tapped: [" + uid + "]");

  int idx = findStudent(uid);

  // Wrong card
  if (idx < 0) {
    Serial.println(">>> ACCESS DENIED: unknown card");
    screen("DENIED", "Unknown card", "UID: " + uid, "Not registered", "Door LOCKED");
    warningAlarm(PULSES_UNKNOWN_CARD);
    standby();
    return;
  }

  // Card locked after too many wrong faces
  if (lockedUntil[idx] > millis()) {
    unsigned long secs = (lockedUntil[idx] - millis()) / 1000 + 1;
    Serial.printf(">>> CARD LOCKED: %s (%lu s left)\n", students[idx].name, secs);
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
      Serial.printf(">>> REJECTED: face does not match %s (wrong try %d of %d)\n",
                    students[idx].name, wrongTries[idx], MAX_WRONG_TRIES);
      if (wrongTries[idx] >= MAX_WRONG_TRIES) {
        wrongTries[idx] = 0;
        lockedUntil[idx] = millis() + LOCK_TIME_MS;
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
      screen("NO FACE", students[idx].name, "Face not seen", "Face the camera", "Tap card again");
      warningAlarm(PULSES_NO_FACE);
    }
  }

  delay(500);
  standby();
}
