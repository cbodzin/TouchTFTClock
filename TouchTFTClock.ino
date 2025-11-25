#include <Arduino.h>
#include <XPT2046_Touchscreen.h>
#include "User_Setup.h"
#include <SPI.h>
#include <time.h>
#include <WiFi.h>
#include "pitches.h"
#include <TFT_eSPI.h>
#include <SD.h>
#include <SPIFFS.h>
#include "icons.h"

// Touchscreen pins
#define XPT2046_IRQ  36   // T_IRQ
#define XPT2046_MOSI 32   // T_DIN
#define XPT2046_MISO 39   // T_OUT
#define XPT2046_CLK  25   // T_CLK
#define XPT2046_CS   33   // T_CS
#define LED_BL       21   // Backlight
#define SD_CS        5    // SD Card reader
#define LDR_PIN      34   // The light dependent resistor

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
#define DARK_FONT   20
#define DARK_COLOR  TFT_LIGHTGREY
#define LIGHT_FONT  128
#define LIGHT_COLOR TFT_WHITE
#define MY_REALLYDARK 0x4A49
const uint16_t MY_DARKBLUE = tft.color565(15, 15, 40);

// Defint the buttons
int buttonCoord[8][4] = {
  { 30, 200, 32, TFT_WHITE },      // Brighter
  { 268, 200, 32, MY_REALLYDARK }, // Dimmer
  { 140, 182, 48, TFT_RED  },      // Alarm
  { 95, 125, 55, 165 },            // Alarm Hours
  { 170, 125, 55, 165 },           // Alarm Minutes
  { 30, 160, 32, TFT_YELLOW },    // Auto-brightness
  { 0, 320, 90, 0 },               // Clock display (toggle size and seconds)
  { 268, 160, 32, TFT_WHITE }  // Big Clock mode
};

bool buttonState[8] = { false, false, false, false, false, false, false, true };
#define DEBOUNCE_TIME 200
#define LONG_PRESS    1000
#define DIM_INTERVAL  500
#define TONE_INTERVAL 300
int deBounce = 0;
int pressStart = 0;
int lastDimCheck = 0;
int lastBrightness = 0;
int curTextColor = TFT_WHITE;
bool ringAlarm = false;
bool alreadyRang = false;
int lastTone = 0;
bool showBig = false;

#define BRIGHTER   0
#define DIMMER     1
#define ALARM      2
#define ALARM_HH   3
#define ALARM_MM   4
#define AUTODIM    5
#define CLOCK_AREA 6
#define BIG_CLOCK  7

// Globals
struct tm alarmTime;
bool alarmOn = false;
bool showSec = false;
int nightHour = 21;
int dayHour = 8;
int screenBrightness = 128;
#define MIN_BRIGHT  1
#define MAX_BRIGHT  255
#define BRIGHT_STEP 10

struct Config {
  String ssidName;
  String ssidPwd;
  int TZoffset;
  bool DSTFlag;
  int nightHour;
  int dayHour;
};

// A set of nice public NTP server s
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.apple.com";
const char* ntpServer3 = "time.nist.gov";

Config mySettings;

void syncWithNTP() {
  WiFi.begin(mySettings.ssidName, mySettings.ssidPwd);
  while (WiFi.status() != WL_CONNECTED) {
    delay(10);
  }

  long offsetSec = mySettings.TZoffset * 3600;
  long dstOffset = mySettings.DSTFlag ? 3600 : 0;
  configTime(offsetSec, dstOffset, ntpServer1, ntpServer2, ntpServer3);
  delay(1000);

  // Wait for time synchronization
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    delay(50);
  }  
  
  // Disconnect WiFi as it's no longer needed
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

void soundAlarm() {
  int toneLength = 5;
  int myTone = NOTE_G4;
  // First see if we have been ringing for a while
  struct tm curTimeInfo;
  if(getLocalTime(&curTimeInfo)) {
    // Add time to the toneLength
    int secondsPassed = curTimeInfo.tm_sec / 10 ;
    toneLength = 5 + (secondsPassed * 5);
  }

  switch (toneLength) {
    case 5:
    case 10:
      myTone = NOTE_G4;
      break;
    case 15:
    case 20:
      myTone = NOTE_A5;
      break;
    case 25:
      myTone = NOTE_B6;
      break;
    case 30:
    default:
      myTone = NOTE_C7;
  }

  if ((millis() - lastTone) > TONE_INTERVAL) {
    lastTone = millis();
    tone(ALARM_PIN, myTone, toneLength);
  }
}

void writeAlarm() {
  if (!SPIFFS.begin()) return;
  // First delete the existing file
  File file = SPIFFS.open("/alarmset.txt", FILE_WRITE);
  if (!file) {
    Serial.println("Couldn't write alarm file.");
    return;
  }

  // Reset it
  file.seek(0);
  file.printf("hour:%d\nmin:%d\n", alarmTime.tm_hour, alarmTime.tm_min);
  file.print("state:");
  file.println(alarmOn ? "on" : "off");
  file.close();
}

bool readAlarm(const char* filename) {
  if (!SPIFFS.begin()) return false;
  File file = SPIFFS.open(filename);
  if (!file) {
    tft.println("Couldn't read alarm file");
    return false;
  }
  tft.println("Reading alarm file");
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();  // Remove whitespace
    if (line.startsWith("hour:")) {
      alarmTime.tm_hour = line.substring(5).toInt();
    } else if (line.startsWith("min:")) {
      alarmTime.tm_min = line.substring(4).toInt();
    } else if (line.startsWith("state:")) {
      alarmOn = (line.substring(6) == "on");
    }
  }

  file.close();
  buttonState[ALARM] = alarmOn;
  return true;
}

Config readConfig (const char* filename) {
  Config config;
  Serial.println("Starting open");
  File file = SD.open(filename);
  File file2 = SD.open("/alarm.txt");

  if (!file) {
    Serial.println("Config file not found");
    return config;
  }
  tft.println("Reading configuration file...");
  
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
    } else if (line.startsWith("night:")) {
      config.nightHour = line.substring(6).toInt();
    } else if (line.startsWith("day:")) {
      config.dayHour = line.substring(4).toInt();
    } else {
      Serial.printf("Read line: %s\n", line);
    }
  }

  tft.println("Reading alarm file");
  while (file2.available()) {
    String line = file.readStringUntil('\n');
    line.trim();  // Remove whitespace
    if (line.startsWith("hour:")) {
      alarmTime.tm_hour = line.substring(5).toInt();
      tft.printf("Hour:%d\n", alarmTime.tm_hour);
    } else if (line.startsWith("min:")) {
      alarmTime.tm_min = line.substring(4).toInt();
      tft.printf("Min:%d\n", alarmTime.tm_hour);
    } else if (line.startsWith("state:")) {
      alarmOn = (line.substring(6) == "on");
      tft.printf("State:%d\n", alarmOn);
    }
  }

  file.close();
  file2.close();
  return config;
}

void drawButtonRect(int myButton) {
/*
#define BRIGHTER   0
#define DIMMER     1
#define ALARM      2
#define ALARM_HH   3
#define ALARM_MM   4
#define AUTODIM    5
#define CLOCK_AREA 6
#define BIG_CLOCK  7

*/
  switch (myButton) {
    case BRIGHTER:
      tft.drawBitmap(buttonCoord[myButton][0], buttonCoord[myButton][1], brightness_up_bits, buttonCoord[myButton][2], buttonCoord[myButton][2], buttonCoord[myButton][3], TFT_BLACK);
      break;
    case DIMMER:
      tft.drawBitmap(buttonCoord[myButton][0], buttonCoord[myButton][1], brightness_down_bits, buttonCoord[myButton][2], buttonCoord[myButton][2], buttonCoord[myButton][3], TFT_BLACK);
      break;
    case ALARM:
      tft.drawBitmap(buttonCoord[myButton][0], buttonCoord[myButton][1], bellIcon48, buttonCoord[myButton][2], buttonCoord[myButton][2], (buttonState[myButton] ? TFT_RED : TFT_DARKGREY), TFT_BLACK);
      break;
    case AUTODIM:
      tft.drawBitmap(buttonCoord[myButton][0], buttonCoord[myButton][1], lightbulb_bits, buttonCoord[myButton][2], buttonCoord[myButton][2], (buttonState[myButton] ? TFT_YELLOW : TFT_DARKGREY), TFT_BLACK);
      break;
    case BIG_CLOCK:
      tft.drawBitmap(buttonCoord[myButton][0], buttonCoord[myButton][1], fontsize_up_bits, buttonCoord[myButton][2], buttonCoord[myButton][2], buttonCoord[myButton][3], TFT_BLACK);
  } 
}

void printLocalTime() {
  static bool lastAlarm = false;
  static bool alreadySynced = false;

  // Set X and Y coordinates for center of display
  int centerX = SCREEN_WIDTH / 2;
  int centerY = SCREEN_HEIGHT / 5;
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    return;
  }

  // Check if we are at the alarm time
  if (alarmOn && !alreadyRang) {
    if ((timeinfo.tm_hour == alarmTime.tm_hour) && (timeinfo.tm_min == alarmTime.tm_min)) {
      if (!alreadyRang) {
        ringAlarm = true;
      } else {
        alreadyRang = false;
      }
    }
  } 

  // Reset alreadyRang if needed
  if (alreadyRang && (timeinfo.tm_hour == alarmTime.tm_hour) && (timeinfo.tm_min == (alarmTime.tm_min+1))) {
    // One minute past alarm
    alreadyRang = false;
  }

  // Special handling for alarms at :59
  if (alreadyRang && (timeinfo.tm_hour == alarmTime.tm_hour-1) && (timeinfo.tm_min == 0) && (alarmTime.tm_min == 59)) {
    alreadyRang = false;
  }

  // Really special handling for alarms at 23:59
  if (alreadyRang && (timeinfo.tm_hour == 0) && (alarmTime.tm_hour == 23) && (timeinfo.tm_min == 0) && (alarmTime.tm_min == 59)) {
    alreadyRang = false;
  }
  
  // Check if we're supposed to set Big Clock on or off 
  if (!showBig && (timeinfo.tm_hour == nightHour) && (timeinfo.tm_min == 0) && (timeinfo.tm_sec == 0)) {
    showBig = true;
    tft.fillScreen(TFT_BLACK);
  }
  if (showBig && (timeinfo.tm_hour == dayHour) && (timeinfo.tm_min == 0) && (timeinfo.tm_sec == 0)) {
    showBig = false;
    tft.setTextSize(1);
    tft.fillScreen(TFT_BLACK);
    // Draw buttons
    drawButtonRect(BRIGHTER);
    drawButtonRect(DIMMER);
    drawButtonRect(ALARM);
    drawButtonRect(AUTODIM);
    drawButtonRect(BIG_CLOCK);
  }

  tft.setTextColor(curTextColor, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);

  // Are we in BIG CLOCK mode?  If so just show the time
  if (showBig) {
    char myTime[6];
    strftime(myTime, 6, "%H:%M", &timeinfo);
    tft.setTextSize(2);
    tft.drawString(myTime, centerX, centerY, 7);
    if (alarmOn) {
      char myAlarm[6];
      strftime(myAlarm, 6, "%H:%M", &alarmTime);
      tft.setTextSize(1);
      tft.drawString(myAlarm, centerX, 220, 2);
    }
    return;
  }

  // Did we change from not showing seconds to showing seconds?
  if (showSec != buttonState[CLOCK_AREA]) {
    tft.fillRect(0,0,320,90, TFT_BLACK);
    showSec = buttonState[CLOCK_AREA];
  }

  if (showSec) {
    char myTime[9];
    strftime(myTime, 9, "%H:%M:%S", &timeinfo);
    tft.setTextSize(3);
    tft.drawString(myTime, centerX, 10, 4);
    tft.setTextSize(1);
  } else {
    char myTime[6];
    strftime(myTime, 6, "%H:%M", &timeinfo);
    tft.drawString(myTime, centerX, 10, 8);
  }

  char myDate[16];
  strftime(myDate, 16, "%a %D", &timeinfo);
  tft.drawString(myDate, centerX, centerY*2, 4);

  char myAlarm[6];
  strftime(myAlarm, 6, "%H:%M", &alarmTime);
  if (!alarmOn) tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawCentreString(myAlarm, centerX, ( centerY*3)-10, FONT_BIG);
  tft.setTextColor(curTextColor, TFT_BLACK);

  // Lastly, if this is the magic hour of 12:13am then sync with NTP
  if ((timeinfo.tm_hour == 0) && (timeinfo.tm_min == 13) && (!alreadySynced)) {
    syncWithNTP();
    alreadySynced = true;
  } else if (timeinfo.tm_min != 13) {
    alreadySynced = false;
  }
}

bool buttonPressed(int myButton, int x, int y, int pressure) {
  bool retVal;
  if ((x > buttonCoord[myButton][0]) && (x < (buttonCoord[myButton][0] + buttonCoord[myButton][2])) && (y > buttonCoord[myButton][1]) && (y < (buttonCoord[myButton][1] + buttonCoord[myButton][2]))) {
    retVal = true;
  } else {
    // Special handling for CLOCK_AREA
    if (myButton == CLOCK_AREA) {
      // Anywhere on the top section
      if ((y > 0) && (y < buttonCoord[CLOCK_AREA][2])) retVal = true;
    } else {
      retVal = false;
    }
  }

  // Make a chirp
  if (retVal) tone(ALARM_PIN, NOTE_C6, 3);
  return retVal;
}

void setBackLight(int brightness) {
  // Set brightness (0-255, where 255 is brightest)
  analogWrite(LED_BL, brightness);

  // Check if we're really dark and use dark font
  if (brightness < DARK_FONT) {
    // Dim the brightness button
    curTextColor = DARK_COLOR;
    buttonCoord[BRIGHTER][3] == TFT_DARKGREY;
    if (!showBig) drawButtonRect(BRIGHTER);
  }
  if (brightness > LIGHT_FONT) {
    curTextColor = LIGHT_COLOR;
    buttonCoord[BRIGHTER][3] == TFT_WHITE;
    if (!showBig) drawButtonRect(BRIGHTER);
  }

}

void doAutoDim() {
  // First read the sensor
  int ldrValue = analogRead(LDR_PIN);
  int brightness = map(ldrValue, 0, 1000, 255, 1);

  // If it is really dark and it is night then go to Big Clock Mode
  if (brightness < 1) brightness = 1;
  if (brightness > 255) brightness = 255;
  setBackLight(brightness);
  screenBrightness = brightness;
}


/*

  Setup routine
  
*/
void setup() {
  Serial.begin(115200);
  while (!Serial) delay(100);

  if (!SD.begin(SD_CS)) {
    Serial.println("SD failed!");
    return;
  }

  mySettings = readConfig("/settings.txt");

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
  tft.setTextColor(curTextColor, TFT_BLACK);
  
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

  // Did we read night and day settings?
  if (mySettings.nightHour) nightHour = mySettings.nightHour;
  tft.printf("Setting night mode hour to %d\n", nightHour);
  if (mySettings.dayHour) dayHour = mySettings.dayHour;
  tft.printf("Setting day mode hour to %d\n", dayHour);

  // Setup for dimming
  pinMode(LED_BL, OUTPUT);
  pinMode(LDR_PIN, INPUT);
  tft.println("Backlight and LDR initialized.");

  // Set default alarm to 6:00am
  if (readAlarm("/alarmset.txt")) {
    tft.printf("Setting alarm to %d:%02d\n", alarmTime.tm_hour, alarmTime.tm_min);
    tft.println(alarmOn ? "Alarm turned on." : "Alarm turned off");
  } else {
    alarmTime.tm_hour = 6;
    alarmTime.tm_min = 0;
    alarmOn = false;
    tft.println("Defaulting to 6:00am alarm, currently off");
  }

  delay(5000);
  // Clear TFT screen
  tft.fillScreen(TFT_BLACK);
  // Draw buttons
  drawButtonRect(BRIGHTER);
  drawButtonRect(DIMMER);
  drawButtonRect(ALARM);
  drawButtonRect(AUTODIM);
  drawButtonRect(BIG_CLOCK);
  
}

void loop() {
  int x, y, z;
  bool beingPressed = false;
  beingPressed = touchscreen.tirqTouched() && touchscreen.touched();

  // First things first, turn off the alarm if it is ringing
  if (beingPressed) {
    // Dont' let it get turned on again for the rest of the minute
    if (ringAlarm) alreadyRang = true;
    ringAlarm = false;
  }

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
    
    // We need to see what was touched
    if (showBig) {
      // Any press while showing big reverts to normal
      showBig = false;
      tft.setTextSize(1);
      tft.fillScreen(TFT_BLACK);
      // Draw buttons
      drawButtonRect(BRIGHTER);
      drawButtonRect(DIMMER);
      drawButtonRect(ALARM);
      drawButtonRect(AUTODIM);
      drawButtonRect(BIG_CLOCK);
      Serial.println("Reseting BIG mode");
    } else if (buttonPressed(BRIGHTER,x,y,z)) {
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
      // Write the time and on/off state to file
      writeAlarm();
      if (alarmOn) {
        tone(ALARM_PIN, NOTE_C3, 100);
        tone(ALARM_PIN, NOTE_C5, 50);
      } else {
        tone(ALARM_PIN, NOTE_C7, 100);
        tone(ALARM_PIN, NOTE_C5, 50);
      }
    } else if ((buttonPressed(ALARM_HH,x,y,z)) && !alarmOn) {
      // Only change if alarm is off
      alarmTime.tm_hour++;
      if (alarmTime.tm_hour > 23) alarmTime.tm_hour = 0;
    } else if ((buttonPressed(ALARM_MM,x,y,z)) && !alarmOn) {
      // only change if alarm is off
      alarmTime.tm_min++;
      if (alarmTime.tm_min > 59) alarmTime.tm_min = 0;
    } else if (buttonPressed(AUTODIM,x,y,z)) {
      buttonState[AUTODIM] = !buttonState[AUTODIM];
      drawButtonRect(AUTODIM);
    } else if (buttonPressed(CLOCK_AREA,x,y,z)) {
      // Twiddle the state of displaying "%H:%M" and "%H:%M:%S"
      buttonState[CLOCK_AREA] = !buttonState[CLOCK_AREA];
    } else if (buttonPressed(BIG_CLOCK,x,y,z)) {
      showBig = true;
      tft.fillScreen(TFT_BLACK);
    } else {
      Serial.printf("Pressed %d,%d,%d\n", x,y,z);
    }
  }

  printLocalTime();
  delay(100);

  // Should we be ringing?
  if (ringAlarm) soundAlarm();

  if ((buttonState[AUTODIM]) && (millis() - lastDimCheck > DIM_INTERVAL)) {
    lastDimCheck = millis();
    doAutoDim();
  } 
}