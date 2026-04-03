/*********ESP32
  Rui Santos
  Photo script from https://RandomNerdTutorials.com/esp32-cam-take-photo-save-microsd-card
  
  IMPORTANT!!! 
   - Select Board "XIAO_ESP32S3"
   - Depends on the FileConfig Library. This can be installed from the library manager.
   
*********/
/*
 * Skript used for Outdoor Camera
 * by Markus Opitz 2024 at www.instructables.com
 * 
 * Config parser and pre-capture delay added by Meivuu 2026
 */

/* XIAO ESP32S3 Sense supports microSD cards up to 32GB, format the microSD card to FAT32
 *
 */



// camera * * * * * * * * * * * * * * * * * * * *
#include "esp_camera.h"
// #include <WiFi.h>
#include "esp_timer.h"
#include "img_converters.h"
#include "Arduino.h"
#include "fb_gfx.h"
#include "soc/soc.h"             // disable brownout problems
#include "soc/rtc_cntl_reg.h"    // disable brownout problems
// #include "esp_http_server.h"
#include "sdkconfig.h"
#include "FS.h"                // SD Card ESP32
#include "SD.h"                // SD Card ESP32
#include "SPI.h"               // SD Card ESP32
#include "driver/rtc_io.h"
#include <FileConfig.h>     // Install this library
#include <EEPROM.h>            // read and write from flash memory

FileConfig cfg;

#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#endif

// define the number of bytes you want to access
#define EEPROM_SIZE 1

#if defined(CAMERA_MODEL_XIAO_ESP32S3)
 #define PWDN_GPIO_NUM     -1
 #define RESET_GPIO_NUM    -1
 #define XCLK_GPIO_NUM     10
 #define SIOD_GPIO_NUM     40
 #define SIOC_GPIO_NUM     39 

 #define Y9_GPIO_NUM       48
 #define Y8_GPIO_NUM       11
 #define Y7_GPIO_NUM       12
 #define Y6_GPIO_NUM       14
 #define Y5_GPIO_NUM       16
 #define Y4_GPIO_NUM       18
 #define Y3_GPIO_NUM       17
 #define Y2_GPIO_NUM       15
 #define VSYNC_GPIO_NUM    38
 #define HREF_GPIO_NUM     47
 #define PCLK_GPIO_NUM     13

#define LED_GPIO_NUM      21

#else
  #error "Camera model not selected"
#endif


int pictureNumber = 0;
int indicatorLED = 44;

void setup() {
 
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println("setup");
  // LED on
  digitalWrite(indicatorLED, HIGH);//45? originally 44
  
  //WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector

  //delay(500);
 //LED
 pinMode(indicatorLED, OUTPUT);

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;

  framesize_t frame_size = FRAMESIZE_VGA;
  bool hmirror = false;
  bool vflip = true;
  int brightness = 2;
  int contrast = 1;
  int saturation = 2;
  int special_effect = 0;
  int whitebal = 1;
  int awb_gain = 1;
  int wb_mode = 0;
  int exposure_ctrl = 1;
  int aec2 = 0;
  int ae_level = 2;
  int bpc = 0;
  int wpc = 1;
  int raw_gma = 1;
  int lenc = 1;

  //Serial.println("Starting SD Card");
    if(!SD.begin(21)){
        Serial.println("Card Mount Failed");
        return;
    }
  
  uint8_t cardType = SD.cardType();
  if(cardType == CARD_NONE){
        Serial.println("No SD card attached");
        return;
  }

// Read config.txt on SD card
  int maxLineLength = 30;
  int maxSectionLength = 20;
  bool ignoreCase = true;
  bool ignoreError = true;

  fs::FS &fs = SD;
  if (cfg.begin(fs, "/config.txt", maxLineLength, maxSectionLength, ignoreCase, ignoreError)){
    while (cfg.readNextSetting()) {
      if (cfg.sectionIs("camera_settings")) {
        if (cfg.nameIs("frame_size")) {
          String string = String(cfg.copyValue());
          string.toLowerCase();
          if (string == "framesize_qvga" || string == "qvga" || string == "320x240") {
            frame_size = FRAMESIZE_QVGA;
          } else if (string == "framesize_vga" || string == "vga" || string == "640x480") {
            frame_size = FRAMESIZE_VGA;
          } else if (string == "framesize_cif" || string == "cif" || string == "400x296") {
            frame_size = FRAMESIZE_CIF;
          } else if (string == "framesize_svga" || string == "svga" || string == "800x600") {
            frame_size = FRAMESIZE_SVGA;
          } else if (string == "framesize_sxga" || string == "sxga" || string == "1280x1024") {
            frame_size = FRAMESIZE_SXGA;
          } else if (string == "framesize_xga" || string == "xga" || string == "1024x768") {
            frame_size = FRAMESIZE_XGA;
          } else if (string == "framesize_uxga" || string == "uxga" || string == "1600x1200") {
            frame_size = FRAMESIZE_UXGA;
          }
        } else if (cfg.nameIs("hmirror")) {
          hmirror = cfg.getBooleanValue();
        } else if (cfg.nameIs("vflip")) {
          vflip = cfg.getBooleanValue();
        } else if (cfg.nameIs("brightness")) {
          int i = cfg.getIntValue();
          if (i >= -2 && i <= 2) {
            brightness = i;
          }
        } else if (cfg.nameIs("contrast")) {
          int i = cfg.getIntValue();
          if (i >= -2 && i <= 2) {
            contrast = i;
          }
        } else if (cfg.nameIs("saturation")) {
          int i = cfg.getIntValue();
          if (i >= -2 && i <= 2) {
            saturation = i;
          }
        } else if (cfg.nameIs("special_effect")) {
          int i = cfg.getIntValue();
          if (i >= 0 && i <= 6) {
            special_effect = i;
          }
        } else if (cfg.nameIs("whitebal")) {
          int i = cfg.getIntValue();
          if (i >= 0 && i <= 1) {
            whitebal = i;
          }
        } else if (cfg.nameIs("awb_gain")) {
          int i = cfg.getIntValue();
          if (i >= 0 && i <= 1) {
            awb_gain = i;
          }
        } else if (cfg.nameIs("wb_mode")) {
          int i = cfg.getIntValue();
          if (i >= 0 && i <= 4) {
            wb_mode = i;
          }
        } else if (cfg.nameIs("exposure_ctrl")) {
          int i = cfg.getIntValue();
          if (i >= 0 && i <= 1) {
            exposure_ctrl = i;
          }
        } else if (cfg.nameIs("aec2")) {
          int i = cfg.getIntValue();
          if (i >= 0 && i <= 1) {
            aec2 = i;
          }
        } else if (cfg.nameIs("ae_level")) {
          int i = cfg.getIntValue();
          if (i >= -2 && i <= 2) {
            ae_level = i;
          }
        } else if (cfg.nameIs("bpc")) {
          int i = cfg.getIntValue();
          if (i >= 0 && i <= 1) {
            bpc = i;
          }
        } else if (cfg.nameIs("wpc")) {
          int i = cfg.getIntValue();
          if (i >= 0 && i <= 1) {
            wpc = i;
          }
        } else if (cfg.nameIs("raw_gma")) {
          int i = cfg.getIntValue();
          if (i >= 0 && i <= 1) {
            raw_gma = i;
          }
        } else if (cfg.nameIs("lenc")) {
          int i = cfg.getIntValue();
          if (i >= 0 && i <= 1) {
            lenc = i;
          }
        }
      }
    }
  }
  cfg.end();

  if(psramFound()){
    config.frame_size = frame_size; // FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_XGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // delay(500);
  
  // Init Camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }
  
    sensor_t * s = esp_camera_sensor_get();  //change settings
  s->set_hmirror(s,hmirror);
  s->set_vflip(s,vflip);
  s->set_brightness(s, brightness);     // -2 to 2
  s->set_contrast(s, contrast);       // -2 to 2
  s->set_saturation(s, saturation);     // -2 to 2
  s->set_special_effect(s, special_effect); // 0 to 6 (0 - No Effect, 1 - Negative, 2 - Grayscale, 3 - Red Tint, 4 - Green Tint, 5 - Blue Tint, 6 - Sepia)
  s->set_whitebal(s, whitebal);       // 0 = disable , 1 = enable
  s->set_awb_gain(s, awb_gain);       // 0 = disable , 1 = enable
  s->set_wb_mode(s, wb_mode);        // 0 to 4 - if awb_gain enabled (0 - Auto, 1 - Sunny, 2 - Cloudy, 3 - Office, 4 - Home)
  s->set_exposure_ctrl(s, exposure_ctrl);  // 0 = disable , 1 = enable
  s->set_aec2(s, aec2);           // 0 = disable , 1 = enable
  s->set_ae_level(s, ae_level);       // -2 to 2
  s->set_bpc(s, bpc);            // 0 = disable , 1 = enable
  s->set_wpc(s, wpc);            // 0 = disable , 1 = enable
  s->set_raw_gma(s, raw_gma);        // 0 = disable , 1 = enable
  s->set_lenc(s, lenc);           // 0 = disable , 1 = enable

  digitalWrite(indicatorLED, LOW);
  // Warm-up loop to discard first few frames, resolve auto exposure and whitebalance
  for (int i = 0; i < 12; i++) {
    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Failed to capture frame");
      continue;
    }
    esp_camera_fb_return(fb);
  }
  digitalWrite(indicatorLED, HIGH);

  // Take Picture with Camera
  camera_fb_t * fb = esp_camera_fb_get();
  delay(100);
  if(!fb) {
    Serial.println("Camera capture failed");
    return;
  }
  // initialize EEPROM with predefined size
  EEPROM.begin(EEPROM_SIZE);
  pictureNumber = EEPROM.read(3) << 24;
  pictureNumber += EEPROM.read(2) << 16;
  pictureNumber += EEPROM.read(1) << 8;
  pictureNumber += EEPROM.read(0);
  pictureNumber ++;

  // Path where new picture will be saved in SD Card
  String path = "/outdoor" + String(pictureNumber) +".jpg";

  Serial.printf("Picture file name: %s\n", path.c_str());
  
  File file = fs.open(path.c_str(), FILE_WRITE);
  if(!file){
    Serial.println("Failed to open file in writing mode");
  } 
  else {
    file.write(fb->buf, fb->len); // payload (image), payload length
    Serial.printf("Saved file to path: %s\n", path.c_str());
    EEPROM.write(0, pictureNumber & 0xff);
    EEPROM.write(1, (pictureNumber >> 8) & 0xff);
    EEPROM.write(2, (pictureNumber >> 16) & 0xff);
    EEPROM.write(3, (pictureNumber >> 24) & 0xff);
    // for (uint8_t i = 0; i < 4; i++){
    //   EEPROM.write(i, 0); //set EEPROM-pictureNumber back to 0
    // }
    EEPROM.commit();
  }
  file.close();
  esp_camera_fb_return(fb); 
  delay(100);
  /*
  // Turns off the ESP32-CAM white on-board LED (flash) connected to GPIO 4
  pinMode(4, OUTPUT);
  digitalWrite(4, LOW);
  rtc_gpio_hold_en(GPIO_NUM_4);
  */
}

void loop() {
  digitalWrite(indicatorLED, LOW);
}
