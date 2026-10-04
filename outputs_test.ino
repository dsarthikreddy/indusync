// STAGE 1 - TEST 2: OLED + green LED + red LED + buzzer + servo door
// After uploading: OLED shows "OLED OK!", then it repeats:
//   green LED + short beep + servo to 90 (open)
//   red LED  + long beep  + servo to 0  (locked)

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>

#define OLED_SDA  41
#define OLED_SCL  42
#define SERVO_PIN 2
#define GREEN_LED 1
#define RED_LED   3
#define BUZZER    46

Adafruit_SSD1306 display(128, 64, &Wire, -1);
Servo door;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== OUTPUT TEST ===");

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  Wire.begin(OLED_SDA, OLED_SCL);
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println("OLED OK!");
    display.setTextSize(1);
    display.setCursor(0, 30);
    display.println("Stage 1 output test");
    display.display();
    Serial.println("OLED OK");
  } else {
    Serial.println("OLED NOT FOUND -> check VCC, GND, SDA=41, SCL=42");
  }

  ESP32PWM::allocateTimer(0);
  door.setPeriodHertz(50);
  door.attach(SERVO_PIN, 500, 2400);
  door.write(0);
}

void loop() {
  Serial.println("GREEN LED + short beep + door OPEN (90 deg)");
  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(BUZZER, HIGH);
  delay(150);
  digitalWrite(BUZZER, LOW);
  door.write(90);
  delay(2000);
  digitalWrite(GREEN_LED, LOW);

  Serial.println("RED LED + long beep + door LOCKED (0 deg)");
  digitalWrite(RED_LED, HIGH);
  digitalWrite(BUZZER, HIGH);
  delay(800);
  digitalWrite(BUZZER, LOW);
  door.write(0);
  delay(2000);
  digitalWrite(RED_LED, LOW);
}
