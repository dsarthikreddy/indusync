# Pin map — ESP32-S3-N16R8 CAM board (do not change)
| Function | GPIO | Notes |
|---|---|---|
| OLED SDA / SCL (I2C, 0x3C) | 41 / 42 | shared I2C bus for ADS1115 (0x48, 0x49) and future I2C sensors |
| Servo (door) | 2 | LEDC channel 2, 50 Hz, 14-bit |
| Green LED / Red LED | 1 / 3 | |
| Buzzer | 46 | |
| RC522 SS / SCK / MOSI / MISO | 21 / 47 / 48 / 14 | RST wired to 3.3 V (RFID_RST 255) |
| Camera XCLK, SIOD, SIOC | 15, 4, 5 | ESP32-S3-EYE layout |
| Camera Y9..Y2 | 16, 17, 18, 12, 10, 8, 9, 11 | |
| Camera VSYNC / HREF / PCLK | 6 / 7 / 13 | |
| FREE (digital only) | 38, 39, 40 | slots S09 PIR, S14 door contact, S15 flame |
| Do not use | 0, 45 (strapping), 19/20 (USB), 35-37 (OPI PSRAM), 43/44 (UART0) | |
I2C addresses: OLED 0x3C, ADS1115 #1 0x48 (ADDR->GND), ADS1115 #2 0x49 (ADDR->3.3 V).
