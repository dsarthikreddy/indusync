// STAGE 1 - CAMERA ID TEST
// Confirms exactly which camera sensor chip is on your board.
// Run this BEFORE anything else - it tells us which code path to use.

#include "esp_camera.h"

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

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== CAMERA ID TEST ===");

  if (!psramFound()) {
    Serial.println("WARNING: PSRAM not found. Go to Tools > PSRAM > OPI PSRAM");
  }

  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_7;
  c.ledc_timer   = LEDC_TIMER_3;
  c.pin_d0 = CAM_Y2;  c.pin_d1 = CAM_Y3;  c.pin_d2 = CAM_Y4;  c.pin_d3 = CAM_Y5;
  c.pin_d4 = CAM_Y6;  c.pin_d5 = CAM_Y7;  c.pin_d6 = CAM_Y8;  c.pin_d7 = CAM_Y9;
  c.pin_xclk  = CAM_XCLK;
  c.pin_pclk  = CAM_PCLK;
  c.pin_vsync = CAM_VSYNC;
  c.pin_href  = CAM_HREF;
  c.pin_sccb_sda = CAM_SIOD;
  c.pin_sccb_scl = CAM_SIOC;
  c.pin_pwdn  = -1;
  c.pin_reset = -1;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_RGB565;
  c.frame_size   = FRAMESIZE_QVGA;
  c.jpeg_quality = 12;
  c.fb_count     = 1;
  c.fb_location  = CAMERA_FB_IN_PSRAM;
  c.grab_mode    = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    Serial.printf("Camera init FAILED, error 0x%x\n", err);
    Serial.println("Check the ribbon cable is fully seated in the connector.");
    return;
  }

  sensor_t* s = esp_camera_sensor_get();
  uint16_t pid = s->id.PID;

  Serial.println("Camera initialized OK.");
  Serial.printf("Sensor PID: 0x%04X\n", pid);
  Serial.println("--------------------------------------------------");

  switch (pid) {
    case 0x2642: Serial.println("RESULT: This is an OV2640 sensor."); break;
    case 0x3660: Serial.println("RESULT: This is an OV3660 sensor."); break;
    case 0x5640: Serial.println("RESULT: This is an OV5640 sensor."); break;
    case 0x2145: Serial.println("RESULT: This is a GC2145 sensor (matches RHYX M21-45 label)."); break;
    default:     Serial.printf("RESULT: Unknown sensor, PID 0x%04X. Note this down.\n", pid); break;
  }
  Serial.println("--------------------------------------------------");

  camera_fb_t* fb = esp_camera_fb_get();
  if (fb) {
    Serial.printf("Test capture OK: %d x %d, %d bytes\n", fb->width, fb->height, fb->len);
    esp_camera_fb_return(fb);
  } else {
    Serial.println("Test capture FAILED - camera detected but can't grab a frame.");
  }
}

void loop() {
  delay(5000);
  Serial.println("(camera_id_test still running - check the RESULT line above)");
}
