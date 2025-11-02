#include <Arduino.h>
#include <XPT2046_Touchscreen.h>
#include "User_Setup.h"
#include <SPI.h>
#include <time.h>
#include <WiFi.h>
#include "pitches.h"
#include <TFT_eSPI.h>
#include <SD.h>

// Touchscreen pins
#define XPT2046_IRQ  36   // T_IRQ
#define XPT2046_MOSI 32   // T_DIN
#define XPT2046_MISO 39   // T_OUT
#define XPT2046_CLK  25   // T_CLK
#define XPT2046_CS   33   // T_CS
#define LED_BL       21   // Backlight
#define SD_CS        5    // SD Card reader

// Globals for the touchscreen
TFT_eSPI tft = TFT_eSPI();
SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define FONT_BIG 6
#define FONT_MED 4
#define FONT_SMALL 2
#define FONT_SIZE 2

// Defint the buttons
int buttonCoord[5][4] = {
  { 30, 200, 20, TFT_WHITE },     // Brighter
  { 270, 200, 20, TFT_DARKGREY }, // Dimmer
  { 140, 190, 40, TFT_RED  },     // Alarm
  { 95, 125, 55, 165 },           // Alarm Hours
  { 170, 125, 55, 165  }          // Alarm Minutes
};
bool buttonState[3] = { false, false, false };
#define DEBOUNCE_TIME 200
#define LONG_PRESS    1000
int deBounce = 0;
int pressStart = 0;

#define BRIGHTER   0
#define DIMMER     1
#define ALARM      2
#define ALARM_HH   3
#define ALARM_MM   4

// Globals
struct tm alarmTime;
bool alarmOn = false;
int screenBrightness = 128;
#define MIN_BRIGHT  1
#define MAX_BRIGHT  255
#define BRIGHT_STEP 10

struct Config {
  String ssidName;
  String ssidPwd;
  int TZoffset;
  bool DSTFlag;
};

Config readConfig (const char* filename) {
  Config config;
  File file = SD.open(filename);
  
  if (!file) {
    Serial.println("Config file not found");
    return config;
  }
  
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();  // Remove whitespace
    
    if (line.startsWith("ssid:")) {
      config.ssidName = line.substring(5);
    } else if (line.startsWith("password:")) {
      config.ssidPwd = line.substring(9);
    } else if (line.startsWith("tz:")) {
      config.TZoffset = line.substring(3).toInt();
    } else if (line.startsWith("dst:")) {
      config.DSTFlag = line.substring(4).toInt();
    } else {
      Serial.printf("Read line: %s\n", line);
    }
  }
  
  file.close();
  return config;
}

// A set of nice public NTP server s
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.apple.com";
const char* ntpServer3 = "time.nist.gov";

// GMT-7 in AZ
const long  gmtOffset_sec = -7 * 3600;
// No DST in AZ; should be 3600 for any other state
const int   daylightOffset_sec = 0;

void drawButtonRect(int myButton) {
  if (myButton != ALARM) {
    tft.fillRoundRect(buttonCoord[myButton][0], buttonCoord[myButton][1], buttonCoord[myButton][2], buttonCoord[myButton][2], 3, buttonCoord[myButton][3]);
  } else {
    // Special handling for ALARM - fill if true
    if (buttonState[ALARM]) {
      tft.fillRoundRect(buttonCoord[myButton][0], buttonCoord[myButton][1], buttonCoord[myButton][2], buttonCoord[myButton][2], 3, buttonCoord[myButton][3]);
    } else {
      tft.fillRoundRect(buttonCoord[myButton][0], buttonCoord[myButton][1], buttonCoord[myButton][2], buttonCoord[myButton][2], 3, TFT_BLACK);
      tft.drawRoundRect(buttonCoord[myButton][0], buttonCoord[myButton][1], buttonCoord[myButton][2], buttonCoord[myButton][2], 3, buttonCoord[myButton][3]);
    }
  }
}

void printLocalTime() {
  static bool lastAlarm = false;
  // Set X and Y coordinates for center of display
  int centerX = SCREEN_WIDTH / 2;
  int centerY = SCREEN_HEIGHT / 4;
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    return;
  }
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  char myDate[16];
  strftime(myDate, 16, "%a %D", &timeinfo);
  tft.drawCentreString(myDate, centerX, centerY, FONT_MED);

  char myTime[12];
  strftime(myTime, 12, "%T", &timeinfo);
  tft.drawCentreString(myTime, centerX, 10, FONT_BIG);

  char myAlarm[12];
  strftime(myAlarm, 12, "%H:%M", &alarmTime);
  if (!alarmOn) tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawCentreString(myAlarm, centerX, centerY*2, FONT_BIG);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
}

bool buttonPressed(int myButton, int x, int y, int pressure) {
  if ((x > buttonCoord[myButton][0]) && (x < (buttonCoord[myButton][0] + buttonCoord[myButton][2])) && (y > buttonCoord[myButton][1]) && (y < (buttonCoord[myButton][1] + buttonCoord[myButton][2]))) {
    return true;
  } else {
    return false;
  }
}

void setBackLight(int brightness) {
  // Set brightness (0-255, where 255 is brightest)
  analogWrite(LED_BL, brightness);
  Serial.printf("Brightness set to %d\n", brightness);
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(100);

  if (!SD.begin(SD_CS)) {
    Serial.println("SD failed!");
    return;
  }

  Config mySettings = readConfig("/settings.txt");

  // Start the SPI for the touchscreen and init the touchscreen
  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  touchscreen.begin(touchscreenSPI);
  // Set the Touchscreen rotation in landscape mode
  // Note: in some displays, the touchscreen might be upside down, so you might need to set the rotation to 3: touchscreen.setRotation(3);
  touchscreen.setRotation(1);

  // Start the tft display
  tft.init();
  // Set the TFT display rotation in landscape mode
  tft.setRotation(1);

  // Clear the screen before writing to it
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  
  // Set X and Y coordinates for center of display
  int centerX = SCREEN_WIDTH / 2;
  int centerY = SCREEN_HEIGHT / 2;

  // Do WiFi first
  tft.print("\nStarting Wifi");
  WiFi.begin(mySettings.ssidName, mySettings.ssidPwd);
  while (WiFi.status() != WL_CONNECTED) {
    tft.print(".");
    delay(500);
  }

  tft.printf("\nConnected to SSID %s\n", mySettings.ssidName);
  long offsetSec = mySettings.TZoffset * 3600;
  long dstOffset = mySettings.DSTFlag ? 3600 : 0;
  tft.printf("Setting TZ offset to %d\n", offsetSec);
  tft.printf("Setting DST offset to %d\n", dstOffset);
  configTime(offsetSec, dstOffset, ntpServer1, ntpServer2, ntpServer3);
  delay(1000);

  // Wait for time synchronization
  tft.print("Getting time");
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    delay(500);
    tft.print(".");
  }  
  tft.println("Done!");
  
  // Disconnect WiFi as it's no longer needed
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  tft.println("Wifi turned off.");
  tft.println("Press screen to begin");

  // Setup for dimming
  pinMode(LED_BL, OUTPUT);

  // Set default alarm to 6:00am
  alarmTime.tm_hour = 6;
  alarmTime.tm_min = 0;
}

void loop() {
  static bool screenReady = false;
  int x, y, z;
  bool beingPressed = false;
  beingPressed = touchscreen.tirqTouched() && touchscreen.touched();
  // Checks if Touchscreen was touched
  if (beingPressed && (millis()-deBounce > DEBOUNCE_TIME)) {    
    // Set debounce
    deBounce = millis();
    // Set the start for the long press
    if (pressStart == 0) pressStart = millis();
    // Get Touchscreen points
    TS_Point p = touchscreen.getPoint();
    // Calibrate Touchscreen points with map function to the correct width and height
    x = map(p.x, 200, 3700, 1, SCREEN_WIDTH);
    y = map(p.y, 240, 3800, 1, SCREEN_HEIGHT);
    z = p.z;
    // Clear TFT screen
    if (!screenReady) {
      tft.fillScreen(TFT_BLACK);
      // Draw buttons
      drawButtonRect(BRIGHTER);
      drawButtonRect(DIMMER);
      drawButtonRect(ALARM);
      
      // Lastly, draw time
      printLocalTime();
      screenReady = true;
      delay(100);
    }
    
    // We need to see what was touched
    if (buttonPressed(BRIGHTER,x,y,z)) {
      screenBrightness += BRIGHT_STEP;
      if (screenBrightness > MAX_BRIGHT) screenBrightness = MAX_BRIGHT;
      setBackLight(screenBrightness);
    } else if (buttonPressed(DIMMER,x,y,z)) {
      screenBrightness -= BRIGHT_STEP;
      if (screenBrightness < MIN_BRIGHT) screenBrightness = MIN_BRIGHT;
      setBackLight(screenBrightness);
    } else if (buttonPressed(ALARM,x,y,z)) {
      // Just toggle
      buttonState[ALARM] = !buttonState[ALARM];
      drawButtonRect(ALARM);
      alarmOn = buttonState[ALARM];
      if (alarmOn) {
        tone(ALARM_PIN, NOTE_C3, 200);
        tone(ALARM_PIN, NOTE_C5, 100);
      } else {
        tone(ALARM_PIN, NOTE_C7, 200);
        tone(ALARM_PIN, NOTE_C5, 100);
      }
    } else if ((buttonPressed(ALARM_HH,x,y,z)) && !alarmOn) {
      // Only change if alarm is off
      alarmTime.tm_hour++;
      if (alarmTime.tm_hour > 23) alarmTime.tm_hour = 0;
    } else if ((buttonPressed(ALARM_MM,x,y,z)) && !alarmOn) {
      // only change if alarm is off
      alarmTime.tm_min++;
      if (alarmTime.tm_min > 59) alarmTime.tm_min = 0;
    } else {
      Serial.printf("Pressed %d,%d,%d\n", x,y,z);
    }
  }

  if (screenReady) printLocalTime();
  delay(100);
}