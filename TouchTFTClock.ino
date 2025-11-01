#include <Arduino.h>
#include <XPT2046_Touchscreen.h>
#include "User_Setup.h"
#include <SPI.h>
#include <time.h>
#include <WiFi.h>
#include "pitches.h"
#include <TFT_eSPI.h>


// Touchscreen pins
#define XPT2046_IRQ  36   // T_IRQ
#define XPT2046_MOSI 32   // T_DIN
#define XPT2046_MISO 39   // T_OUT
#define XPT2046_CLK  25   // T_CLK
#define XPT2046_CS   33   // T_CS
#define LED_BL       21   // Backlight

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
int buttonCoord[3][4] = {
  { 20, 190, TFT_WHITE, TFT_WHITE }, // Brighter
  { 260, 190, TFT_DARKGREY, TFT_DARKGREY }, // Dimmer
  { 140, 190, TFT_BLUE, TFT_RED } // Alarm
};
bool buttonState[3] = { false, false, false };
int myWidth = 40;
int myHeight = 40;
int myRadius = 3;

#define BRIGHTER   0
#define DIMMER     1
#define ALARM      2
#define MAX_BUTTON 3

// Globals
int alarmTime;
bool alarmOn = false;
int screenBrightness = 128;
#define MIN_BRIGHT  1
#define MAX_BRIGHT  255
#define BRIGHT_STEP 10

// // Buttons
// // Button position and size
// #define FRAME_X 80
// #define FRAME_Y 80
// #define FRAME_W 160
// #define FRAME_H 80

// // Red zone size
// #define REDBUTTON_X FRAME_X
// #define REDBUTTON_Y FRAME_Y
// #define REDBUTTON_W (FRAME_W / 2)
// #define REDBUTTON_H FRAME_H

// // Green zone size
// #define GREENBUTTON_X (REDBUTTON_X + REDBUTTON_W)
// #define GREENBUTTON_Y FRAME_Y
// #define GREENBUTTON_W (FRAME_W / 2)
// #define GREENBUTTON_H FRAME_H

// Stores current button state
// bool buttonState = false;

// Touchscreen coordinates: (x, y) and pressure (z)
// int x, y, z;

// Need your SSID and Password defined in here
#include "Wireless_Config.h"

// A set of nice public NTP server s
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.apple.com";
const char* ntpServer3 = "time.nist.gov";

// GMT-7 in AZ
const long  gmtOffset_sec = -7 * 3600;
// No DST in AZ; should be 3600 for any other state
const int   daylightOffset_sec = 0;

// // Buttons
// // Draw button frame
// void drawFrame() {
//   tft.drawRect(FRAME_X, FRAME_Y, FRAME_W, FRAME_H, TFT_BLACK);
// }

// // Draw a red button
// void drawRedButton() {
//   tft.fillRect(REDBUTTON_X, REDBUTTON_Y, REDBUTTON_W, REDBUTTON_H, TFT_RED);
//   tft.fillRect(GREENBUTTON_X, GREENBUTTON_Y, GREENBUTTON_W, GREENBUTTON_H, TFT_WHITE);
//   drawFrame();
//   //tft.setTextColor(TFT_BLACK);
//   //tft.setTextSize(FONT_SIZE);
//   //tft.setTextDatum(MC_DATUM);
//   //tft.drawString("ON", GREENBUTTON_X + (GREENBUTTON_W / 2), GREENBUTTON_Y + (GREENBUTTON_H / 2));
//   buttonState = false;
// }

// // Draw a green button
// void drawGreenButton() {
//   tft.fillRect(GREENBUTTON_X, GREENBUTTON_Y, GREENBUTTON_W, GREENBUTTON_H, TFT_GREEN);
//   tft.fillRect(REDBUTTON_X, REDBUTTON_Y, REDBUTTON_W, REDBUTTON_H, TFT_WHITE);
//   drawFrame();
//   //tft.setTextColor(TFT_BLACK);
//   //tft.setTextSize(FONT_SIZE);
//   //tft.setTextDatum(MC_DATUM);
//   //tft.drawString("OFF", REDBUTTON_X + (REDBUTTON_W / 2) + 1, REDBUTTON_Y + (REDBUTTON_H / 2));
//   buttonState = true;
// }

void drawButtonRect(int myButton) {
  // If button is flase then use the unpressed color (2) otherwise use pressed (3)
  int myColor = buttonState[myButton] ? 3 : 2;
  tft.drawRoundRect(buttonCoord[myButton][0], buttonCoord[myButton][1], myWidth, myHeight, myRadius, myColor);
}

// Print Touchscreen info about X, Y and Pressure (Z) on the TFT Display
// void printTouchToDisplay(int touchX, int touchY, int touchZ) {
  // tft.setTextColor(TFT_YELLOW, TFT_BLACK, true);

  // int centerX = SCREEN_WIDTH / 2;
  // int textY = 80;
 
  // String tempText = "X = " + String(touchX);
  // tft.drawCentreString(tempText, centerX, textY, FONT_SMALL);

  // textY += 20;
  // tempText = "Y = " + String(touchY);
  // tft.drawCentreString(tempText, centerX, textY, FONT_SMALL);

  // textY += 20;
  // tempText = "Pressure = " + String(touchZ);
  // tft.drawCentreString(tempText, centerX, textY, FONT_SMALL);

  // Play a quick tone
//   tone(ALARM_PIN, NOTE_C3, 100);
//   tone(ALARM_PIN, NOTE_C5, 100);
// }

void printLocalTime() {
  // Set X and Y coordinates for center of display
  int centerX = SCREEN_WIDTH / 2;
  int centerY = SCREEN_HEIGHT / 4;
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    return;
  }
  tft.setTextColor(TFT_CYAN, TFT_BLACK);

  char myDate[16];
  strftime(myDate, 16, "%a %D", &timeinfo);
  tft.drawCentreString(myDate, centerX, centerY*2, FONT_MED);

  char myTime[12];
  strftime(myTime, 12, "%T", &timeinfo);
  tft.drawCentreString(myTime, centerX, 10, FONT_BIG);

}

bool buttonPressed(int myButton, int x, int y, int pressure) {
  if ((x > buttonCoord[myButton][0]) && (x < (buttonCoord[myButton][0] + myWidth)) && (y > buttonCoord[myButton][1]) && (y < (buttonCoord[myButton][1] + myHeight))) {
    return true;
  } else {
    return false;
  }
}

void setBackLight(int brightness) {
  // Set brightness (0-255, where 255 is brightest)
  analogWrite(LED_BL, brightness);
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(100);

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
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    tft.print(".");
    delay(500);
  }

  tft.println("Connected");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2, ntpServer3);
  delay(1000);

  // Wait for time synchronization
  tft.print("Getting time");
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    delay(500);
    tft.print(".");
  }  
  tft.println("Done");
  
  // Disconnect WiFi as it's no longer needed
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  tft.println("Wifi Off");

  tft.println("Press screen to begin");

  // Setup for dimming
  pinMode(LED_BL, OUTPUT);
}

void loop() {
  static bool touched = false;
  int x, y, z;
  // Checks if Touchscreen was touched
  // if (touchscreen.tirqTouched() && touchscreen.touched()) {
  if (touchscreen.touched()) {  
    Serial.println("PRESSED!!!!!!");
    // Get Touchscreen points
    TS_Point p = touchscreen.getPoint();
    // Calibrate Touchscreen points with map function to the correct width and height
    x = map(p.x, 200, 3700, 1, SCREEN_WIDTH);
    y = map(p.y, 240, 3800, 1, SCREEN_HEIGHT);
    z = p.z;
    // Clear TFT screen
    if (!touched) {
      tft.fillScreen(TFT_BLACK);
      // Draw buttons
      drawButtonRect(BRIGHTER);
      drawButtonRect(DIMMER);
      drawButtonRect(ALARM);
      // Lastly, draw time
      printLocalTime();
      touched = true;
      delay(100);
    }
    
    // We need to see what was touched
    if (buttonPressed[BRIGHTER],x,y,z) {
      screenBrightness += BRIGHT_STEP;
      if (screenBrightness > MAX_BRIGHT) screenBrightness = MAX_BRIGHT;
      setBackLight(screenBrightness);
    } else if (buttonPressed(DIMMER,x,y,z)) {
      screenBrightness -= BRIGHT_STEP;
      if (screenBrightness > MIN_BRIGHT) screenBrightness = MIN_BRIGHT;
      setBackLight(screenBrightness);
    } else if (buttonPressed(ALARM,x,y,z)) {
      // Just toggle
      buttonState[ALARM] = !buttonState[ALARM];
      drawButtonRect(ALARM);
    } else {
      Serial.printf("Pressed %d,%d,%d\n", x,y,z);
    }
  }
  Serial.printf("Pressed %d,%d,%d\n", x,y,z);
  if (touched){printLocalTime();
        drawButtonRect(BRIGHTER);
      drawButtonRect(DIMMER);
      drawButtonRect(ALARM);}
  delay(100);
}