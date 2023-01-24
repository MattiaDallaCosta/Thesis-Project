#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTelegram2.h>
#include "esp_camera.h"
#include "img_converters.h"
#include "soc/soc.h"           // Brownout error fix
#include "soc/rtc_cntl_reg.h"  // Brownout error fix

#include <WiFiClientSecure.h>
WiFiClientSecure client;

extern "C" {
#include "brain.h"
#include "encoder.h"
}

uint8_t *raw;
uint8_t *sub;
uint8_t *jpg;
uint8_t EXT_RAM_ATTR saved[3*PIX_LEN/16];
area_t EXT_RAM_ATTR diffDims[20];
int len = 0;
uint8_t different = 0;

const char* ssid = "Mi 11";  // SSID WiFi network
const char* pass = "00000000";  // Password  WiFi network
const char* token = "5891807658:AAFgQDuxotPpZaP_a5bZ5FJj7XPrmKGByKo"; // token obtained from the botfather telegram bot (@BotFather)

// Check the userid with the help of bot @JsonDumpBot or @getidsbot (work also with groups)
// https://t.me/JsonDumpBot  or  https://t.me/getidsbot
int64_t userid = 1234567890;
uint8_t flashval = 1;
bool changingFlash = 0;

// Timezone definition to get properly time from NTP server
#define MYTZ "CET-1CEST,M3.5.0,M10.5.0/3"

AsyncTelegram2 myBot(client);

static camera_config_t camera_config = {
  .pin_pwdn = PWDN_GPIO_NUM,
  .pin_reset = RESET_GPIO_NUM,
  .pin_xclk = XCLK_GPIO_NUM,
  .pin_sscb_sda = SIOD_GPIO_NUM,
  .pin_sscb_scl = SIOC_GPIO_NUM,
  .pin_d7 = Y9_GPIO_NUM,
  .pin_d6 = Y8_GPIO_NUM,
  .pin_d5 = Y7_GPIO_NUM,
  .pin_d4 = Y6_GPIO_NUM,
  .pin_d3 = Y5_GPIO_NUM,
  .pin_d2 = Y4_GPIO_NUM,
  .pin_d1 = Y3_GPIO_NUM,
  .pin_d0 = Y2_GPIO_NUM,
  .pin_vsync = VSYNC_GPIO_NUM,
  .pin_href = HREF_GPIO_NUM,
  .pin_pclk = PCLK_GPIO_NUM,  
  .xclk_freq_hz = 20000000,        //XCLK 20MHz or 10MHz
  .ledc_timer = LEDC_TIMER_0,
  .ledc_channel = LEDC_CHANNEL_0,
  .pixel_format = PIXFORMAT_JPEG,  //YUV422,GRAYSCALE,RGB565,JPEG
  .frame_size = FRAMESIZE_QVGA,    //QQVGA-UXGA Do not use sizes above QVGA when not JPEG
  .jpeg_quality = 11,              //0-63 lower number means higher quality
  .fb_count = 1,                   //if more than one, i2s runs in continuous mode. Use only with JPEG
  .grab_mode = CAMERA_GRAB_WHEN_EMPTY
};


int lampChannel = 7;           // a free PWM channel (some channels used by camera)
const int pwmfreq = 50000;     // 50K pwm frequency
const int pwmresolution = 9;   // duty cycle bit range
const int pwmMax = pow(2,pwmresolution)-1;

// Lamp Control
#ifdef CAM
void setLamp(int newVal) {
    if (newVal != -1) {
        // Apply a logarithmic function to the scale.
        int brightness = round((pow(2,(1+(newVal*0.02)))-2)/6*pwmMax);
        ledcWrite(lampChannel, brightness);
        Serial.print("Lamp: ");
        Serial.print(newVal);
        Serial.print("%, pwm = ");
        Serial.println(brightness);
    }
}
#endif

static esp_err_t init_camera() {
  //initialize the camera
  Serial.print("Camera init... ");
  esp_err_t err = esp_camera_init(&camera_config);

  if (err != ESP_OK) {
    delay(100);  // need a delay here or the next serial o/p gets missed
    Serial.printf("\n\nCRITICAL FAILURE: Camera sensor failed to initialise.\n\n");
    Serial.printf("A full (hard, power off/on) reboot will probably be needed to recover from this.\n");
    return err;
  } else {
    Serial.println("succeeded");

    // Get a reference to the sensor
    sensor_t* s = esp_camera_sensor_get();

    // Dump camera module, warn for unsupported modules.
    switch (s->id.PID) {
      case OV9650_PID: Serial.println("WARNING: OV9650 camera module is not properly supported, will fallback to OV2640 operation"); break;
      case OV7725_PID: Serial.println("WARNING: OV7725 camera module is not properly supported, will fallback to OV2640 operation"); break;
      case OV2640_PID: Serial.println("OV2640 camera module detected"); break;
      case OV3660_PID: Serial.println("OV3660 camera module detected"); break;
      default: Serial.println("WARNING: Camera module is unknown and not properly supported, will fallback to OV2640 operation");
    }
  }
  return ESP_OK;
}

// void taskCheck(void * parameter) {
//   TBMessage msg;
//   //raw = (uint8_t*)ps_malloc(3*PIX_LEN);
//   setLamp(flashval);
//   camera_fb_t* fb = esp_camera_fb_get();
//   Serial.printf("first get\n");  
//   esp_camera_fb_return(fb);
//   fb = esp_camera_fb_get();
//   Serial.printf("second get\n"); 
//   setLamp(0);
//   Serial.printf("pre conversion\n");
//   fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, raw);
//   Serial.printf("post conversion\n");
//   esp_camera_fb_return(fb);
//   Serial.printf("pre sub\n");
//   subsample(raw,sub);
//   Serial.printf("post sub\n");

//   different = compare(sub, saved, diffDims);
//   //diffDims[0].x = 2;
//   //diffDims[0].y = 2;
//   //diffDims[0].h = 2;
//   //diffDims[0].w = 2;
//   //uint8_t different = 1;
//   Serial.printf("post compare: different = %i\n", different);
//   store(sub, saved);
//   //free(sub);
//   Serial.printf("%i\n", different);
//   if (different) {
//     Serial.printf("Images are different\n");
//     for (int i = 0; i < different; i++) {
//       Serial.printf("area #%i\nx: %i, y: %i, w: %i, h:%i\n", i, diffDims[i].x, diffDims[i].y, diffDims[i].w, diffDims[i].h);
//       enlargeAdjust(&diffDims[i]);
//       Serial.printf("\t¦\n\tV\nx: %i, y: %i, w: %i, h:%i\n", i, diffDims[i].x, diffDims[i].y, diffDims[i].w, diffDims[i].h);
//       jpg = (uint8_t*)ps_malloc(3*diffDims[i].w*diffDims[i].h);
//       len = encodeNsend(jpg, raw, diffDims[i]);
//       if (!len) continue;
//       myBot.sendPhoto(msg, jpg, len);
//       len = 0;
//       free(jpg);
//     }
//     Serial.printf("Post encodeNsend\n");
//   } else Serial.printf("Images are the same\n");
//   //free(raw);
// }
#ifdef CAM
void setFlash(TBMessage& msg) {
  Serial.println("Changing flash value");
  myBot.sendMessage(msg, "Which is the new intensity? [values beween 0 and 100]");
  changingFlash = true;
}
#endif

size_t sendPicture(TBMessage& msg) {
  // Take Picture with Camera;
  Serial.println("Camera capture requested");

  // Take picture with Camera and send to Telegram
  #ifdef CAM
  setLamp(flashval);
  #endif
  camera_fb_t* fb = esp_camera_fb_get();
  esp_camera_fb_return(fb);
  fb = esp_camera_fb_get();
  #ifdef CAM
  setLamp(0);
  #endif
  if (!fb) {
    Serial.println("Camera capture failed");
    return 0;
  }
  size_t len = fb->len;
  if (!myBot.sendPhoto(msg, fb->buf, fb->len)){
    len = 0;
    myBot.sendMessage(msg, "Error! Picture not sent.");
  }
  // Clear buffer
  esp_camera_fb_return(fb);
  fb = NULL;
  return len;
}

void setup() {  
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);       // disable brownout detector

  #ifdef CAM
  pinMode(LAMP_PIN, OUTPUT);                       // set the lamp pin as output 
  ledcSetup(lampChannel, pwmfreq, pwmresolution);  // configure LED PWM channel
  setLamp(0);                                      // set default value  
  ledcAttachPin(LAMP_PIN, lampChannel);            // attach the GPIO pin to the channel
  #endif

  Serial.begin(115200);
  Serial.println();
  // xTaskCreate(taskCheck, "taskCheck", 10000, NULL, 1, NULL);

  // Start WiFi connection
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println(WiFi.localIP());

  // Sync time with NTP
  configTzTime(MYTZ, "time.google.com", "time.windows.com", "pool.ntp.org");
  client.setCACert(telegram_cert);

  // Set the Telegram bot properies
  myBot.setUpdateTime(1000);
  myBot.setTelegramToken(token);

  // Check if all things are ok
  Serial.print("\nTest Telegram connection... ");
  myBot.begin() ? Serial.println("OK") : Serial.println("NOK");

  // Send a welcome message to user when ready
  char welcome_msg[64];
  snprintf(welcome_msg, 64, "BOT @%s online.\nTry with /takePhoto command.", myBot.getBotName());
  myBot.sendTo(userid, welcome_msg);

  // Init the camera module (accordind the camera_config_t defined)
  init_camera();

  raw = (uint8_t*)ps_malloc(3*PIX_LEN);
  sub = (uint8_t*)ps_malloc(3*PIX_LEN/16);
  #ifdef CAM
  setLamp(flashval);
  #endif
  camera_fb_t* fb = esp_camera_fb_get();
  Serial.printf("taken photo\n");
  esp_camera_fb_return(fb);
  fb = esp_camera_fb_get();
  #ifdef CAM
  setLamp(0);
  #endif
  fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, raw);
  esp_camera_fb_return(fb);
  Serial.printf("post jpg to rgb\n");
  subsample(raw, sub);
  store(sub, saved);
  Serial.printf("post store\n");
  free(raw);
  free(sub);
  Serial.printf("--- end of configuration ---\n");
}

void loop() {
  TBMessage msg;
  Serial.printf("sending photo\n");

  raw = (uint8_t*)ps_malloc(3*PIX_LEN);
  #ifdef CAM
  setLamp(flashval);
  #endif
  camera_fb_t* fb = esp_camera_fb_get();
  Serial.printf("first get\n");  
  esp_camera_fb_return(fb);
  fb = esp_camera_fb_get();
  Serial.printf("second get\n"); 
  #ifdef CAM
  setLamp(0);
  #endif
  Serial.printf("pre conversion\n");
  fmt2rgb888(fb->buf, fb->len, PIXFORMAT_JPEG, raw);
  Serial.printf("post conversion\n");
  esp_camera_fb_return(fb);
  Serial.printf("pre sub\n");
  subsample(raw,sub);
  Serial.printf("post sub\n");
  different = compare(sub, saved, diffDims);
  //different = 0;
  //diffDims[0].x = 2;
  //diffDims[0].y = 2;
  //diffDims[0].h = 2;
  //diffDims[0].w = 2;
  //uint8_t different = 1;
  Serial.printf("post compare: different = %i\n", different);
  store(sub, saved);
  free(sub);
  Serial.printf("%i\n", different);
  if (different) {
    Serial.printf("Images are different\n");
    for (int i = 0; i < different; i++) {
      Serial.printf("area #%i\nx: %i, y: %i, w: %i, h:%i\n", i, diffDims[i].x, diffDims[i].y, diffDims[i].w, diffDims[i].h);
      enlargeAdjust(&diffDims[i]);
      Serial.printf("\t¦\n\tV\nx: %i, y: %i, w: %i, h:%i\n", i, diffDims[i].x, diffDims[i].y, diffDims[i].w, diffDims[i].h);
      jpg = (uint8_t*)ps_malloc(3*diffDims[i].w*diffDims[i].h);
      len = encodeNsend(jpg, raw, diffDims[i]);
      if (len) {
        myBot.sendPhoto(msg, jpg, len);
        len = 0;
      }
      free(jpg);
    }
    Serial.printf("Post encodeNsend\n");
  } else Serial.printf("Images are the same\n");
  free(raw);
  sleep(1);
}

// void loop() {
//   // A variable to store telegram message data
//   TBMessage msg;
//   // if there is an incoming message...
//   if (myBot.getNewMessage(msg)) {
//     Serial.print("New message from chat_id: ");
//     Serial.println(msg.sender.id);
//     MessageType msgType = msg.messageType;
//     // Received a text message
//     if (msgType == MessageText) {
//       if (changingFlash) {
//         int8_t newval = atoi(msg.text.c_str());
//         if(newval >= 0 && newval <= 100) {
//           flashval = newval;
//           Serial.printf("Flash intensity set to %i\n", flashval);
//           String replyStr = "Flash intensity set to ";
//           replyStr += std::to_string(flashval).c_str();
//           myBot.sendMessage(msg, replyStr);      
//         } else {
//           myBot.sendMessage(msg, "Invalid flash value try values between 0 and 100");
//           Serial.printf("invalid flash value\n");
//         }
//         changingFlash = 0;
//       } else {
//         if (msg.text.equalsIgnoreCase("/takePhoto")) {
//           Serial.println("\nSending Photo from CAM");
//           //if (sendPicture(msg))
//           //    Serial.println("Picture sent successfull");
//           //else myBot.sendMessage(msg, "Error, picture not sent.");
//           taskCheck(NULL);
//         } else if (msg.text.equalsIgnoreCase("/changeFlash")) {
//           Serial.println("\nchanging flash intensity");
//           setFlash(msg);
//         } else {
//           Serial.print("\nText message received: ");
//           Serial.println(msg.text);
//           String replyStr = "Message received:\n";
//           replyStr += msg.text;
//           replyStr += "\nTry with /takePhoto to take a picture\n";
//           replyStr += "Try with /changeFlash to change the flash brightness";
//           myBot.sendMessage(msg, replyStr);
//         }
//       }     
//     }
//   }
// }  