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
 * This is a version of that code which instead produces a dithered PNG, and has fewer artifacts as a result.
 * It doesn't support the larger frame sizes unfortunately, which I suspect is due to memory limitations.
 * It also supports changing the camera settings via config file and delays before taking a photo in order
 * to produce a clearer photo, without a tint.
 *
 * Dither settings can also be modified via config file.
 * You can specify palette colors using hex, or a path to a .hex file on the SD card (plaintext with
 * hex numbers separated by newlines).
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
#include <PNGenc.h>         // Install this library
#include <FileConfig.h>     // Install this library
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

// Chromacity weight mode (how it handles monochromatic vs highly chromatic palettes)
enum {
  CHROM_EXPONENTIAL, // dynamic
  CHROM_FACTORIAL, // dynamic, alternative
  CHROM_WEIGHTED, // ideal for monochromatic palettes
  CHROM_DISABLED // ideal for highly chromatic palettes
};

uint pictureNumber = 0;
int indicatorLED = 44;
int palette_len = 3;
float weight_r = 0.2627;
float weight_g = 0.678;
float weight_b = 0.0593;
float weight_coeff_r = 1;
float weight_coeff_g = 1;
float weight_coeff_b = 1;
uint8_t* palette = (uint8_t *) ps_malloc(768);
int chromacity_mode = CHROM_EXPONENTIAL;
bool dithering_enabled = true;
bool brightness_mapping = false;
uint8_t darkest = 255;
uint8_t brightest = 0;

/**
  accepts length 3 array rgb888 as pixel color and array of triples rgb888 array palette
  returns rgb888 triple from the palette
*/
void nearestColor(uint8_t result[3], int16_t error[3], int16_t rawcolor[3], uint8_t palSize, uint8_t palette[]) {
  int nearest_dist = 0x7fffffff;
  uint8_t nearest_col[3] = {255, 0, 0};
  for (int i = 0; i < palSize; i++) {
    float r = palette[i*3+2];
    float g = palette[i*3+1];
    float b = palette[i*3];
    float raw_r = rawcolor[0];
    float raw_g = rawcolor[1];
    float raw_b = rawcolor[2];
    // Serial.println("brightest:"+String(brightest));
    // Serial.println("darkest:"+String(darkest));
    if (brightness_mapping) {
      r /= min(max(brightest-darkest,1),255)/(float)255;
      g /= min(max(brightest-darkest,1),255)/(float)255;
      b /= min(max(brightest-darkest,1),255)/(float)255;
      r -= darkest;
      g -= darkest;
      b -= darkest;
    }
    float dr = raw_r - r;
    float dg = raw_g - g;
    float db = raw_b - b;
    if (chromacity_mode != CHROM_DISABLED) {
      dr *= weight_r;
      dg *= weight_g;
      db *= weight_b;
    }
    int dist = (dr*dr)+(dg*dg)+(db*db);
    // Serial.println(String(dist)+" "+String(nearest_dist));
    if (dist < nearest_dist) {
      error[0] = dr;
      error[1] = dg;
      error[2] = db;
      // Serial.println("hewwo "+String(r)+" "+String(g)+" "+String(b)+", "+String(dist)+" replaces "+String(nearest_col[0])+" "+String(nearest_col[1])+" "+String(nearest_col[2])+" "+String(nearest_dist));
      nearest_dist = dist;
      nearest_col[0] = r;
      nearest_col[1] = g;
      nearest_col[2] = b;
    }
  }
  result[0] = nearest_col[0];
  result[1] = nearest_col[1];
  result[2] = nearest_col[2];
}

void addToPaletteFromHex(const char* hex, uint8_t value_offsets[3]) {
  uint32_t c = strtoul(hex, nullptr, 16);
  uint8_t r = (c >> 16) & 0xff;
  uint8_t g = (c >> 8) & 0xff;
  uint8_t b = c & 0xff;
  palette[palette_len*3] = b;
  palette[palette_len*3+1] = g;
  palette[palette_len*3+2] = r;
  palette_len ++;

  int gray_level = min(min(r,g),b);
  value_offsets[0] = r - gray_level;
  value_offsets[1] = g - gray_level;
  value_offsets[2] = b - gray_level;

  int brightness = (r + ((int)g + b)) / 3;
  if (brightness > brightest) {
    brightest = brightness;
  }
  if (brightness < darkest) {
    darkest = brightness;
  }
}

void rgb585to888(int16_t col888[3], uint16_t col565) {
  int16_t b = col565 & 0x1f;
  int16_t g = (col565 >> 5) & 0x3f;
  int16_t r = (col565 >> 11) & 0x1f;
  col888[0] = (r * 527 + 23) >> 6;
  col888[1] = (g * 259 + 33) >> 6;
  col888[2] = (b * 527 + 23) >> 6;
}

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
  uint8_t default_palette[9] = {0,0,0,127,127,127,255,255,255};
  for (int i = 0; i < sizeof(default_palette); i++) {
    palette[i] = default_palette[i];
  }

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
  int maxSectionLength = 128; // max 128 palette colors
  bool ignoreCase = true;
  bool ignoreError = true;
  int palIndex = 0;
  bool replaceDefaultPalette = false;
  File file;
  int sum_r = 0;
  int sum_g = 0;
  int sum_b = 0;

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
      } else if (cfg.sectionIs("dither_palette")) {
        if (!replaceDefaultPalette) {
          replaceDefaultPalette = true;
          palette_len = 0;
        }
        if (cfg.nameIs("path")) {
          char* path = cfg.copyValue();
          file = fs.open(String(path));
          if (file) {
            String hex = "";
            while (file.available()) {
              char nextByte = file.read();
              if (nextByte == '\n') {
                uint8_t v[3];
                addToPaletteFromHex(hex.c_str(), v);

                sum_r += v[0]/255;
                sum_g += v[1]/255;
                sum_b += v[2]/255;

                hex = "";
              } else {
                hex = hex + nextByte;
              }
            }
          } else {
            delay(100);
            Serial.println("Failed to open .hex palette " + String(path));
          }
          file.close();
        } else if (cfg.nameIs("chromacity_mode")) {
          char* s = cfg.copyValue();
          if (s == "exponential") {
            chromacity_mode = CHROM_EXPONENTIAL;
          }
          if (s == "factorial") {
            chromacity_mode = CHROM_FACTORIAL;
          }
          if (s == "weighted") {
            chromacity_mode = CHROM_WEIGHTED;
          }
          if (s == "disabled") {
            chromacity_mode = CHROM_DISABLED;
          }
        } else if (cfg.nameIs("map_brightness")) {
          brightness_mapping = cfg.getBooleanValue();
        } else if (cfg.nameIs("dithering_enabled")) {
          dithering_enabled = cfg.getBooleanValue();
        } else {
          char* s = cfg.copyValue();
          uint8_t v[3];
          addToPaletteFromHex(s, v);

          sum_r += v[0]/255;
          sum_g += v[1]/255;
          sum_b += v[2]/255;
        }
      }
    }
  }
  cfg.end();
  if (replaceDefaultPalette) {
    weight_coeff_r = min((float)1,max((float)0,1 - (sum_r / (float)palette_len)));
    weight_coeff_g = min((float)1,max((float)0,1 - (sum_g / (float)palette_len)));
    weight_coeff_b = min((float)1,max((float)0,1 - (sum_b / (float)palette_len)));
    if (chromacity_mode == CHROM_EXPONENTIAL) {
      weight_r = pow(weight_r, weight_coeff_r);
      weight_g = pow(weight_g, weight_coeff_g);
      weight_b = pow(weight_b, weight_coeff_b);
    }
    if (chromacity_mode == CHROM_FACTORIAL) {
      weight_r *= weight_coeff_r;
      weight_g *= weight_coeff_g;
      weight_b *= weight_coeff_b;
    }
  }
  for (int i = palette_len*3; i < 768; i+=3) {
    palette[i] = palette[palette_len*3-3];
    palette[i+1] = palette[palette_len*3-2];
    palette[i+2] = palette[palette_len*3-1];
  }

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

  // Serial.println("darkest="+String(darkest));
  // Serial.println("brightest="+String(brightest));

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
  // Take Picture with Camera, store as raw pixels in RGB565
  // Here I take the picture I will use:
  camera_fb_t *fb = esp_camera_fb_get();

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
  unsigned long allocLen = (fb->width*fb->height*24)/2;
  // imgOut = (uint8_t *) ps_malloc(allocLen);
  uint16_t sourcePixLineBuffer[fb->width];
  int16_t * currentPixLineBuffer;
  uint8_t * ditheredPixLineBuffer;
  int16_t * nextPixLineBuffer;
  currentPixLineBuffer = (int16_t *) ps_malloc(fb->width*3*sizeof(int16_t));
  ditheredPixLineBuffer = (uint8_t *) ps_malloc(fb->width*3*sizeof(uint8_t));
  nextPixLineBuffer = (int16_t *) ps_malloc(fb->width*3*sizeof(int16_t));
  for (int i = 0; i < fb->width * 3; i++) {
    currentPixLineBuffer[i] = 0;
  }

  uint x, y;

  rc = png.open(path.c_str(), myOpen, myClose, myRead, myWrite, mySeek);

  Serial.println("palette_len = "+String(palette_len));

  if (rc == PNG_SUCCESS){
    rc = png.encodeBegin(fb->width, fb->height, PNG_PIXEL_TRUECOLOR, 24, NULL, 7);
    if (rc == PNG_SUCCESS){
      for (y = 0; y < fb->height && rc == PNG_SUCCESS; y++){
        for (int i = 0; i < fb->width * 3; i++) {
          nextPixLineBuffer[i] = 0;
        }
        for (x = 0; x < fb->width; x++){

          uint16_t pix585 = (fb->buf[(y*fb->width*2)+(x*2)+1]) | (fb->buf[(y*fb->width*2)+x*2]) << 8;
          int16_t currentPix[3];
          rgb585to888(currentPix, pix585);

           for (uint8_t i = 0; i < 3; i++) {
            currentPix[i] += currentPixLineBuffer[(x*3)+i];
           }
          uint8_t quantPix[3];
          int16_t pixError[3];
          nearestColor(quantPix, pixError, currentPix, palette_len, palette);
          if (dithering_enabled) {
            for (uint8_t i = 0; i < 3; i++){
              if (x > 0) {
                nextPixLineBuffer[((x-1)*3) + i] += (pixError[i] * 3) >> 4; // 3/16 of error
              }
              nextPixLineBuffer[(x*3) + i] += (pixError[i] * 5) >> 4; // 5/16
              if (x < fb->width-1) {
                nextPixLineBuffer[((x+1)*3) + i] += pixError[i] >> 4; // 1/16
                currentPixLineBuffer[((x+1)*3) + i] += (pixError[i] * 7) >> 4; // 7/16
              }
            }
          }

          ditheredPixLineBuffer[x*3] = quantPix[0];
          ditheredPixLineBuffer[(x*3)+1] = quantPix[1];
          ditheredPixLineBuffer[(x*3)+2] = quantPix[2];
          
        }
        rc = png.addLine(ditheredPixLineBuffer);
        currentPixLineBuffer = nextPixLineBuffer;
        nextPixLineBuffer = (int16_t *) ps_malloc(fb->width*3*sizeof(int16_t));
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
