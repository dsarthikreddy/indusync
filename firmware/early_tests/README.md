# Early Stage-1 test sketches (history; the real firmware is ../door_board_v8_2)
- rfid_test.ino: RC522 wiring + prints card UIDs.     - camera_id_test.ino: identifies the sensor (result: GC2145, PID 0x2145).
- outputs_test.ino / diagnostic_test.ino: OLED, LEDs, buzzer, servo with ESP32Servo (found: ESP32Servo conflicted, so the
  final firmware drives the servo with built-in LEDC: ledcSetup + ledcAttachPin on core 2.0.17).
- outputs_test_v2.ino: LEDC servo test (uses the core 3.x ledcAttach API).  - cam_face_test.ino: CameraWebServer face test (RGB565 fix).
