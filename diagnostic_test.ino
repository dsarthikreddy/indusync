// STAGE 1 - DIAGNOSTIC TEST
// Shows exactly where the program stops. No wiring changes needed.
// Upload -> open Serial Monitor (115200) -> press EN/RST -> read the STEP lines.

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>

#define USE_SERVO true     // change to false to skip the servo part completely

#define OLED_SDA  41
#define OLED_SCL  42
#define SERVO_PIN 2
#define GREEN_LED 1
#define RED_LED   3
#define BUZZER    46

Adafruit_SSD1306 display(128, 64, &Wire, -1);
Servo door;
int counter = 0;

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== DIAGNOSTIC TEST ===");
  Serial.println("STEP 1: program started");

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  Serial.println("STEP 2: LED and buzzer pins ready");

  Wire.begin(OLED_SDA, OLED_SCL);
  bool oled = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  Serial.println(oled ? "STEP 3: OLED OK" : "STEP 3: OLED NOT FOUND");
  if (oled) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println("DIAG TEST");
    display.display();
  }

  if (USE_SERVO) {
    Serial.println("STEP 4: starting servo on pin 2...");
    Serial.flush();
    ESP32PWM::allocateTimer(0);
    door.setPeriodHertz(50);
    door.attach(SERVO_PIN, 500, 2400);
    Serial.println("STEP 5: servo attached");
    Serial.flush();
    door.write(0);
    Serial.println("STEP 6: servo set to 0 degrees");
  } else {
    Serial.println("STEP 4-6: servo SKIPPED");
  }

  Serial.println("STEP 7: setup finished, loop starting");
  Serial.flush();
}

void loop() {
  counter++;
  Serial.printf("Alive %d\n", counter);

  bool greenTurn = (counter % 2 == 1);
  digitalWrite(GREEN_LED, greenTurn ? HIGH : LOW);
  digitalWrite(RED_LED, greenTurn ? LOW : HIGH);

  digitalWrite(BUZZER, HIGH);
  delay(100);
  digitalWrite(BUZZER, LOW);

  if (USE_SERVO) door.write(greenTurn ? 90 : 0);
  delay(1400);
}
