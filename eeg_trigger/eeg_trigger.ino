
/*
 * Board: Adafruit ESP32 feather ("TTGO ESP32 rev1 (rev one) Dev Module WiFi & Bluetooth 4MB Flash"
 * https://www.aliexpress.com/item/TTGO-ESP32-rev1-rev-one-Dev-Module-WiFi-Bluetooth-4MB-Flash/32813561581.html?spm=a2g0s.9042311.0.0.13264c4dFOMf2I)
 *
 * https://learn.adafruit.com/adafruit-huzzah32-esp32-feather/pinouts
 *
 *  API:
 *  curl -X GET http://192.168.4.1/config
 *  curl -X POST -H "Content-Type: application/json" --data '{"ssid":"SSID","passwd":"PASSWORD"}' http://192.168.4.1/config
 *
 *  NOTE: SPFFS can't handle long filenames. Be sure to keep them short (maybe 32 chars?)
 *
 * LiIon Battery Charge Status:
 * 4.2V – 100%
 * 4.1V – 87%
 * 4.0V – 75%
 * 3.9V – 55%
 * 3.8V – 30%
 * 3.5V – <15% (minimum recommended)
 *
 */

// Current pinout (standard male db9 pin numbers, left to right, 1-5 in the top row, 6-9 in bottom)
//   -------------
//   \ 0 1 2 3 G /
//    \ 4 5 6 7 /
//      -------
// pins 1-4: bits 0-3
// pin 5: ground
// pins 6-9: bits 4-7

#include <FS.h>
// https://github.com/bblanchon/ArduinoJson
// see https://github.com/bblanchon/ArduinoJson/issues/578
#define ARDUINOJSON_USE_DOUBLE 0
// timestamps in milliseconds require more than 32 bits!
#define ARDUINOJSON_USE_LONG_LONG 1
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <Streaming.h>

#include <WiFi.h>
#include <WebServer.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <DNSServer.h>

// NTP modules
// TODO: consider using the esp32 native code (https://esp32.com/viewtopic.php?f=19&t=5188)
#include <NTPClient.h>
#include <TimeLib.h>

const char* g_hostname = "eeg";

// This is nominally 128 (assuming 10-bit with 11dB attenuation)
// But can be off, so calibrate for your board for accurate readings
const float BAT_SCALE = 139;

const long NTP_OFFSET = -8 * 60 * 60; // Pacific Standard Time (USA)
const long NTP_INTERVAL = 1 * 60 * 1000; // In milliseconds
static const char NTP_ADDRESS[] = "us.pool.ntp.org";

/*
 *  NOTE: ADC2 cannot be used when wifi is enabled. ADC2 pins are GPIO 0, 2, 4, 12-15, 25-27 = A0,A1,A5,A6,A8,A10,A11 on the feather
 *  GPIO #12 / A11 has a pull-down built in, so use only as output
 *  ADC1: GPIO 13, 32 - 39 = A2 (34), A3 (39), A4 (36), A7 (32), A9 (33), A12 (13)
 *  GPIO 13 / A12 is connected to the led; A13 (GPIO 35) is connected to the battery and thus not exposed on the feather.
 *  GPIO 34, 36, 39 are NOT output capable
 */
const byte LED_PIN = 13;
const byte BAT_PIN = A13;
// Digital pins are, in order from bits 0 to 7:
const byte DIG_PINS[] = {27, 33, 15, 32, 14, 26, 25, 21};
const byte DAC_PIN = 25;
const byte TOUCH_PIN = 12;

uint64_t g_unix_epoch_ms;
float g_bat_volt;
byte g_mac[6];

// configuration parameters
String g_ssid = "defaultSsid";
String g_passwd = "defaultPasswd";
long g_pulseMicros = 10 * 1000;
byte g_logicOn = 1;
byte g_analogOut = 1;
int g_touchThresh = 40;

//struct paramItem {
// String key;
// String val;
// String type;
//};
//paramItem g_params[] = {
// {"ssid", "myssid", "String"},
// {"passwd", "mypasswd", "String"},
// {"pulseMicros", "10000", "int"},
// {"logicOn", "1", "byte"},
// {"analogOutFlag", "1", "byte"}
//};

volatile bool g_isTouched = false;
volatile bool g_longTouch = false;
long g_touchTime = 0;

hw_timer_t * timer = NULL;
volatile SemaphoreHandle_t timerSemaphore;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR onTouchTimer(){
  // Increment the counter and set the time of ISR
  portENTER_CRITICAL_ISR(&timerMux);
  // If touch pad was touched...
  if (g_isTouched) {
    // and is still being touched...
    if (touchRead(TOUCH_PIN) <= g_touchThresh) {
      // and has been touched for at least 3 seconds...
      if ((millis()-g_touchTime) >= 3000) {
        g_longTouch = true;
      }
    } else {
      g_isTouched = false;
      g_longTouch = false;
    }
  }
  portEXIT_CRITICAL_ISR(&timerMux);
  // Give a semaphore that we can check in the loop
  xSemaphoreGiveFromISR(timerSemaphore, NULL);
  // It is safe to use digitalRead/Write here if you want to toggle an output
}

void touchISR() {
  // Called when touch pin value goes below treshold
  if (!g_isTouched) {
    g_touchTime = millis();
    g_isTouched = true;
  }
}

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, NTP_ADDRESS, NTP_OFFSET, NTP_INTERVAL);

std::unique_ptr<WebServer> server;
//WebServer server(80);

void setup() {
  Serial.begin(115200);
  delay(10);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  Serial << "Mounting FS... ";
  //SPIFFS.format(); // erase filesystem, for testing...
  if (!SPIFFS.begin()) {
    Serial << "failed to mount file system." << endl;
  } else {
    Serial << "successfully mounted." << endl;
  }
  // Load config values here
  if(loadConfig()) {
    Serial << "Config successfully loaded." << endl;
  } else {
    Serial << "Failed to load config-- initializing..." << endl;
    if(saveConfig()) {
      Serial << "Config initialized." << endl;
    } else {
      Serial << "FAILED to initialize config!" << endl;
    }
  }

  // Setup digital pulse output pins:
  for(byte i=0; i<8; i++)
    pinMode(DIG_PINS[i], OUTPUT);

  // Flash LED as well
  pinMode(LED_PIN, OUTPUT);

  // Connect to WiFi network
  Serial << endl << "Connecting to '" << g_ssid << "' with '" << g_passwd << "'..." << endl;

  WiFi.mode(WIFI_STA);
  WiFi.begin(g_ssid.c_str(), g_passwd.c_str());

  int ntries = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial << ".";
    if(ntries > 30) {
      Serial << "timeout. Going into AP mode..." << endl;
      if(configMode())
        Serial << "Config success!" << endl;
    }
    ntries++;
  }

  Serial << "Connected to " << g_ssid << " with IP address: " << WiFi.localIP() << endl;
  WiFi.macAddress(g_mac);
  Serial << "MAC address: " << g_mac[0] << "." << g_mac[1] << "." << g_mac[2] << "." << g_mac[3] << "." << g_mac[4] << "." << g_mac[5] << endl;

  if (MDNS.begin(g_hostname)) {
    Serial << "MDNS responder started" << endl;
  }

  // Start the server
  startServer();

  timeClient.begin();
  timeClient.update();
  setSyncProvider(getNtpTime);
  setSyncInterval(NTP_INTERVAL/1000+1);

  Serial << "Starting OTA..." << endl;
  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });
  ArduinoOTA.begin();

  /*
   * ADC settings.
   *   0dB attenuaton (ADC_ATTEN_0db) gives full-scale voltage 1.1V
   * 2.5dB attenuation (ADC_ATTEN_2_5db) gives full-scale voltage 1.5V
   *   6dB attenuation (ADC_ATTEN_6db) gives full-scale voltage 2.2V
   *  11dB attenuation (ADC_ATTEN_11db) gives full-scale voltage 3.9V (max voltage is limited by VDD_A)
   *  Arduino default is 11dB
   */
  analogSetWidth(10); // 9-12 bits
  //analogSetAttenuation(ADC_6db); // set for all all ADC pins (ADC_0db, ADC_2_5db, ADC_6db, ADC_11db)
  //analogSetPinAttenuation(A2, ADC_11db); // specific pin
  analogSetPinAttenuation(BAT_PIN, ADC_11db);

  /*
   * Harware timer to check touch pin status.
   * TODO: use a hardware time to manage TTL/analog output pulses.
   */
  // Create semaphore to inform us when the timer has fired
  timerSemaphore = xSemaphoreCreateBinary();

  // Use 1st timer of 4 (timer 0) and set prescaler to 80 (see ESP32 Technical Reference Manual).
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTouchTimer, true);
  // Set alarm to call onTimer function every half-second (third parameter=repeat)
  timerAlarmWrite(timer, 500000, true);
  timerAlarmEnable(timer); // Start the alarm
  // Attach a touch interrupt
  touchAttachInterrupt(TOUCH_PIN, touchISR, g_touchThresh);

  Serial.print("Trigger ready!");
}

void loop() {
  ArduinoOTA.handle();

  server->handleClient();

  if(g_longTouch) {
    // Go back into config mode
    Serial << "Touch detected-- entering AP config mode..." << endl;
    while(!configMode()) {
      Serial << "Config failed." << endl;
      delay(500);
    }
    Serial << "Config success!" << endl;
    Serial << "Connected to " << g_ssid << " with IP address: " << WiFi.localIP() << endl;
    WiFi.macAddress(g_mac);
    Serial << "MAC address: " << g_mac[0] << "." << g_mac[1] << "." << g_mac[2] << "." << g_mac[3] << "." << g_mac[4] << "." << g_mac[5] << endl;
    if (MDNS.begin(g_hostname))
      Serial << "MDNS responder started" << endl;
    // Start the server
    startServer();
  }

  heartbeat(1);
  delay(5);
}

inline void writeByte(byte b) {
  if(g_analogOut) {
    dacWrite(DAC_PIN, b);
  }else{
    ((b & 0x01)) ? digitalWrite(DIG_PINS[0], g_logicOn) : digitalWrite(DIG_PINS[0], !g_logicOn);
    ((b & 0x02)) ? digitalWrite(DIG_PINS[1], g_logicOn) : digitalWrite(DIG_PINS[1], !g_logicOn);
    ((b & 0x04)) ? digitalWrite(DIG_PINS[2], g_logicOn) : digitalWrite(DIG_PINS[2], !g_logicOn);
    ((b & 0x08)) ? digitalWrite(DIG_PINS[3], g_logicOn) : digitalWrite(DIG_PINS[3], !g_logicOn);
    ((b & 0x10)) ? digitalWrite(DIG_PINS[4], g_logicOn) : digitalWrite(DIG_PINS[4], !g_logicOn);
    ((b & 0x20)) ? digitalWrite(DIG_PINS[5], g_logicOn) : digitalWrite(DIG_PINS[5], !g_logicOn);
    ((b & 0x40)) ? digitalWrite(DIG_PINS[6], g_logicOn) : digitalWrite(DIG_PINS[6], !g_logicOn);
    ((b & 0x80)) ? digitalWrite(DIG_PINS[7], g_logicOn) : digitalWrite(DIG_PINS[7], !g_logicOn);
  }
}

int parseHex(char str[]){
  return (int) strtol(str, 0, 16);
  //byte tens = (hexValue[0] < '9') ? hexValue[0] - '0' : hexValue[0] - '7';
  //byte ones = (hexValue[1] < '9') ? hexValue[1] - '0' : hexValue[1] - '7';
  //byte number = (16 * tens) + ones;
}

void pulseOut(byte b) {
  digitalWrite(LED_PIN, HIGH);
  writeByte(b);
  // Simple delay. We could use a timer and mke this non-blocking, but
  // we don't want overlapping pulses, so blocking here is ok.
  delayMicroseconds(g_pulseMicros);
  writeByte(0);
  digitalWrite(LED_PIN, LOW);
}

int dst_hour(int y, int m, int d, int h) {
  // From https://forum.arduino.cc/index.php?topic=370460.0
  // remainder will identify which day of month is Sunday by subtracting x from the one
  // or two week window.  First two weeks for March and first week for November
  int yy = y - 2000;
  int x = (yy + yy/4 + 2) % 7;

  // DST begins on 2nd Sunday of March @ 2:00AM and ends on 1st Sunday of Nov @ 2:00AM
  if ((m>3 && m<11) || (m==3 && d>14-x) || (m==3 && d==14-x && h>=2) || (m==11 && d<7-x) || (m==11 && d==7-x && h<2))
    return(h+1);
  else
    return(h);
}

time_t getNtpTime() {
  timeClient.update();
  return(timeClient.getEpochTime());
}


void heartbeat(uint8_t num_blinks){
  // if num_blinks==0, the led will stay on.
  static uint32_t prev_millis;
  static uint32_t prev_blink_millis;
  static uint8_t led_state;
  uint32_t cur_millis = millis();
  const uint32_t flash_interval = 1500;
  if(num_blinks==0){
    led_state = prev_millis = prev_blink_millis = 0;
    digitalWrite(LED_PIN, HIGH);
  }
  if(led_state){
    if(cur_millis - prev_blink_millis > 80){
      // Toggle the led's state
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      led_state--;
      prev_blink_millis = cur_millis;
    }
  }else{
    if(cur_millis - prev_millis > flash_interval){
      digitalWrite(LED_PIN, HIGH);
      led_state = (num_blinks-1) * 2 + 1;
      prev_millis = cur_millis;
      prev_blink_millis = cur_millis;
    }
  }
}
