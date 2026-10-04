// STAGE 1 - OUTPUT TEST v2
// Servo now uses the ESP32's built-in PWM (no ESP32Servo library).
// Upload -> Serial Monitor (115200) -> press EN/RST.
// NOTE: ledcAttach(pin, freq, bits) is the esp32 core 3.x API. On core 2.0.17 use ledcSetup + ledcAttachPin (see the main firmware).

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define OLED_SDA  41
#define OLED_SCL  42
#define SERVO_PIN 2
#define GREEN_LED 1
#define RED_LED   3
#define BUZZER    46

Adafruit_SSD1306 display(128, 64, &Wire, -1);

// Servo on built-in PWM: 50 Hz, 14-bit (0-16383 = 0-20 ms)
void servoWrite(int angle) {
  int pulseUs = map(angle, 0, 180, 500, 2400);
  uint32_t duty = (uint32_t)pulseUs * 16384UL / 20000UL;
  ledcWrite(SERVO_PIN, duty);
}

void beep(int ms) {
  digitalWrite(BUZZER, HIGH);
  delay(ms);
  digitalWrite(BUZZER, LOW);
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== OUTPUT TEST v2 ===");

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  Serial.println("STEP 1: LED and buzzer pins ready");

  Wire.begin(OLED_SDA, OLED_SCL);
  bool oled = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (oled) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println("OLED OK!");
    display.setTextSize(1);
    display.setCursor(0, 30);
    display.println("Output test v2");
    display.display();
  }
  Serial.println(oled ? "STEP 2: OLED OK" : "STEP 2: OLED NOT FOUND");

  Serial.println("STEP 3: starting servo on pin 2 (built-in PWM)");
  Serial.flush();
  bool ok = ledcAttach(SERVO_PIN, 50, 14);
  Serial.println(ok ? "STEP 4: servo PWM OK" : "STEP 4: servo PWM FAILED");
  servoWrite(0);
  Serial.println("STEP 5: setup finished");
}

void loop() {
  Serial.println("GREEN LED + short beep + door OPEN (90 deg)");
  digitalWrite(GREEN_LED, HIGH);
  beep(150);
  servoWrite(90);
  delay(2000);
  digitalWrite(GREEN_LED, LOW);

  Serial.println("RED LED + long beep + door LOCKED (0 deg)");
  digitalWrite(RED_LED, HIGH);
  beep(800);
  servoWrite(0);
  delay(2000);
  digitalWrite(RED_LED, LOW);
}
