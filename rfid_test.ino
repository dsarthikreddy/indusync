// STAGE 1 - TEST 1: RC522 RFID reader
// Checks the wiring and prints each card's UID.
// Open Serial Monitor at 115200 baud after uploading.

#include <SPI.h>
#include <MFRC522.h>

#define RFID_SS   21
#define RFID_SCK  47
#define RFID_MOSI 48
#define RFID_MISO 14
#define RFID_RST  255   // RC522 RST pin is wired to 3.3V, not to a GPIO

MFRC522 rfid(RFID_SS, RFID_RST);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== RC522 TEST ===");

  SPI.begin(RFID_SCK, RFID_MISO, RFID_MOSI, RFID_SS);
  rfid.PCD_Init();

  byte v = rfid.PCD_ReadRegister(MFRC522::VersionReg);
  Serial.printf("RC522 version: 0x%02X\n", v);
  if (v == 0x00 || v == 0xFF) {
    Serial.println("RC522 NOT FOUND -> check the 7 wires and 3.3V");
  } else {
    Serial.println("RC522 OK -> tap your card or tag on the reader");
  }
}

void loop() {
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    delay(50);
    return;
  }

  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uid += "0";
    uid += String(rfid.uid.uidByte[i], HEX);
    if (i < rfid.uid.size - 1) uid += " ";
  }
  uid.toUpperCase();

  Serial.println("Card UID: " + uid + "   <- write this down exactly");
  rfid.PICC_HaltA();
  delay(1000);
}
