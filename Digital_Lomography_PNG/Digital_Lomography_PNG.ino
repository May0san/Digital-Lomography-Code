/*********ESP32
  Rui Santos
  Photo script from https://RandomNerdTutorials.com/esp32-cam-take-photo-save-microsd-card
  
  IMPORTANT!!! 
   - Select Board "XIAO_ESP32S3"
   - Depends on PNGenc and FileConfig. Can be installed in the library manager.
   
*********/
/*
 * Based on script from Markus Opitz's Outdoor Camera at www.instructables.com
 *
 * This is a version of that code which instead produces a PNG, and has fewer artifacts as a result.
 * It doesn't support the larger frame sizes unfortunately, which I suspect is due to memory limitations.
 * It also supports changing the camera settings via config file and delays before taking a photo in order
 * to produce a clearer photo, without a tint.
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
#include <PNGenc.h>       // Install this library
#include <FileConfig.h>   // Install this library
#include <EEPROM.h>            // read and write from flash memory

PNGENC png; // static instance of the PNG encoder class
FileConfig cfg;

// File instance
static File pngfile;

#define CAMERA_MODEL_XIAO_ESP32S3 // Has PSRAM

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#endif

// define the number of bytes you want to access
#define EEPROM_SIZE 4

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

// Callback functions to access a file on the SD card

void *myOpen(const char *filename) {
 // Serial.printf("Attempting to open %s\n", filename);
  pngfile = SD.open(filename, "w+r"); // This option is needed to both read and write
  return &pngfile;
}

void myClose(PNGFILE *handle) {
  File *f = (File *)handle->fHandle;
  f->flush();
  f->close();
}
int32_t myRead(PNGFILE *pFile, uint8_t *buffer, int32_t length) {
//  File *f = (File *)pFile->fHandle;
//  return f->read(buffer, length);
    int32_t iBytesRead;
    iBytesRead = length;
    File *f = static_cast<File *>(pFile->fHandle);
//    Serial.printf("Read: requested size = %d\n", length);
    iBytesRead = (int32_t)f->read(buffer, iBytesRead);
    pFile->iPos = f->position();
//    Serial.printf("Read %d bytes\n", iBytesRead);
//    Serial.printf("New Pos: %d\n", pFile->iPos);
    return iBytesRead;
}

int32_t myWrite(PNGFILE *handle, uint8_t *buffer, int32_t length) {
  File *f = (File *)handle->fHandle;
  handle->iPos += length;
//  Serial.printf("Write %d bytes, new pos = %d\n", length, handle->iPos);
  return f->write(buffer, length);
}
int32_t mySeek(PNGFILE *handle, int32_t position) {
  File *f = (File *)handle->fHandle;
  f->seek(position);
  handle->iPos = (int)f->position();
//  Serial.printf("Seek: %d\n", (int)position);
  return handle->iPos;
}

uint pictureNumber = 0;
int indicatorLED = 44;

void setup() {
 
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println("setup");
  
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
  config.pixel_format = PIXFORMAT_RGB565;
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

  delay(50);

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

  delay(50);

  // LED on
  digitalWrite(indicatorLED, HIGH);//45? originally 44

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
          } else if (string == "framesize_vga" || string == "VGA" || string == "640x480") {
            frame_size = FRAMESIZE_VGA;
          } else if (string == "framesize_cif" || string == "cif" || string == "400x296") {
            frame_size = FRAMESIZE_CIF;
          } else if (string == "framesize_svga" || string == "svga" || string == "800x600") {
            frame_size = FRAMESIZE_SVGA;
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

  delay(50);

  if(psramFound()){
    config.frame_size = frame_size; // FRAMESIZE_ + QVGA|CIF|VGA|SVGA|XGA|SXGA|UXGA
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_QVGA;
    config.fb_count = 1;
  }
  
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
  delay(10);

  Serial.println("Applied settings:");
  Serial.println("frame_size="+String((framesize_t)frame_size) + " (enum value)");
  Serial.println("hmirror="+String(hmirror));
  Serial.println("vflip="+String(vflip));
  Serial.println("brightness="+String(brightness));
  Serial.println("contrast="+String(contrast));
  Serial.println("saturation="+String(saturation));
  Serial.println("special_effect="+String(special_effect));
  Serial.println("whitebal="+String(whitebal));
  Serial.println("awb_gain="+String(awb_gain));
  Serial.println("wb_mode="+String(wb_mode));
  Serial.println("exposure_ctrl="+String(exposure_ctrl));
  Serial.println("aec2="+String(aec2));
  Serial.println("ae_level="+String(ae_level));
  Serial.println("bpc="+String(bpc));
  Serial.println("wpc="+String(wpc));
  Serial.println("raw_gma="+String(raw_gma));
  Serial.println("lenc="+String(lenc));

  camera_fb_t * fb = NULL;

  // Warm-up loop to discard first few frames
  for (int i = 0; i < 12; i++) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Failed to capture frame");
      continue;
    }
    esp_camera_fb_return(fb);
  }
  // Take Picture with Camera, store as raw pixels in RGB565
  // Here I take the picture I will use:
  fb = esp_camera_fb_get();

  delay(100);
  if(!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  // MODIFY THE PIXELS OF fb->buf HERE
  
  // initialize EEPROM with predefined size
  EEPROM.begin(EEPROM_SIZE);
  pictureNumber = EEPROM.read(3) << 24;
  pictureNumber += EEPROM.read(2) << 16;
  pictureNumber += EEPROM.read(1) << 8;
  pictureNumber += EEPROM.read(0);
  pictureNumber ++;

  // Path where new picture will be saved in SD Card
  String path = "/outdoor" + String(pictureNumber) +".png";

  // fs::FS &fs = SD; 
  Serial.printf("Picture file name: %s\n", path.c_str());
  
  // convert RGB565 raw data to PNG
  int rc;
  unsigned long dataSize;
  uint8_t * imgOut;
  // unsigned long allocLen = (fb->width*fb->height*24)/2;
  // imgOut = (uint8_t *) ps_malloc(allocLen);
  uint16_t sourcePixLineBuffer[fb->width];
  uint8_t * convertedPixLineBuffer;
  convertedPixLineBuffer = (uint8_t *) ps_malloc(fb->width*3*sizeof(uint8_t));

  uint x, y;

  rc = png.open(path.c_str(), myOpen, myClose, myRead, myWrite, mySeek);


  if (rc == PNG_SUCCESS){
    rc = png.encodeBegin(fb->width, fb->height, PNG_PIXEL_TRUECOLOR, 24, NULL, 7);
    if (rc == PNG_SUCCESS){
      for (y = 0; y < fb->height*2 && rc == PNG_SUCCESS; y++){
        for (x = 0; x < fb->width*2; x++){
          if (x%2 == 0) {sourcePixLineBuffer[x/2] = 0;}
          sourcePixLineBuffer[x/2] |= (fb->buf[(y*fb->width*2)+x]) << ((1-(x%2))*8);
        }
        rc = png.addRGB565Line(sourcePixLineBuffer,convertedPixLineBuffer);
      }
      Serial.println("scan finished before "+ String(y)+ " on image height ");
      dataSize = png.close();
    } else {
      Serial.println("png.encodeBegin failed");
      png.close();
    }
  } else {
    Serial.println("png.open failed");
  }
  Serial.println("PNG result: "+String(rc));
  Serial.println("With error: "+String(png.getLastError()));
  
  Serial.println(dataSize);
  Serial.printf("Saved file to path: %s\n", path.c_str());
  EEPROM.write(0, pictureNumber & 0xff);
  EEPROM.write(1, (pictureNumber >> 8) & 0xff);
  EEPROM.write(2, (pictureNumber >> 16) & 0xff);
  EEPROM.write(3, (pictureNumber >> 24) & 0xff);
  // for (uint8_t i = 0; i < 4; i++){
  //   EEPROM.write(i, 0); //set EEPROM-pictureNumber back to 0
  // }
  EEPROM.commit();
  delay(100);
  digitalWrite(indicatorLED, LOW);
}

void loop() {
  digitalWrite(indicatorLED, LOW);
}
