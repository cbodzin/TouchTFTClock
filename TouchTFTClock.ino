#include <Arduino.h>
#include <XPT2046_Touchscreen.h>
#include "User_Setup.h"
#include <SPI.h>
#include <time.h>
#include "WiFi.h"


/*  Install the "TFT_eSPI" library by Bodmer to interface with the TFT Display - https://github.com/Bodmer/TFT_eSPI
    *** IMPORTANT: User_Setup.h available on the internet will probably NOT work with the examples available at Random Nerd Tutorials ***
    *** YOU MUST USE THE User_Setup.h FILE PROVIDED IN THE LINK BELOW IN ORDER TO USE THE EXAMPLES FROM RANDOM NERD TUTORIALS ***
    FULL INSTRUCTIONS AVAILABLE ON HOW CONFIGURE THE LIBRARY: https://RandomNerdTutorials.com/cyd/ or https://RandomNerdTutorials.com/esp32-tft/   */
#include <TFT_eSPI.h>

// Install the "XPT2046_Touchscreen" library by Paul Stoffregen to use the Touchscreen - https://github.com/PaulStoffregen/XPT2046_Touchscreen
// Note: this library doesn't require further configuration

TFT_eSPI tft = TFT_eSPI();

// Touchscreen pins
#define XPT2046_IRQ 36   // T_IRQ
#define XPT2046_MOSI 32  // T_DIN
#define XPT2046_MISO 39  // T_OUT
#define XPT2046_CLK 25   // T_CLK
#define XPT2046_CS 33    // T_CS

SPIClass touchscreenSPI = SPIClass(VSPI);
XPT2046_Touchscreen touchscreen(XPT2046_CS, XPT2046_IRQ);

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define FONT_BIG 6
#define FONT_MED 4
#define FONT_SMALL 2

// Touchscreen coordinates: (x, y) and pressure (z)
int x, y, z;

#include "Wireless_Config.h"

// A set of nice public NTP server s
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.apple.com";
const char* ntpServer3 = "time.nist.gov";

// GMT-7 in AZ
const long  gmtOffset_sec = -7 * 3600;
// No DST in AZ; should be 3600 for any other state
const int   daylightOffset_sec = 0;

// Print Touchscreen info about X, Y and Pressure (Z) on the TFT Display
void printTouchToDisplay(int touchX, int touchY, int touchZ) {
  tft.setTextColor(TFT_YELLOW, TFT_BLACK, true);

  int centerX = SCREEN_WIDTH / 2;
  int textY = 80;
 
  String tempText = "X = " + String(touchX);
  tft.drawCentreString(tempText, centerX, textY, FONT_SMALL);

  textY += 20;
  tempText = "Y = " + String(touchY);
  tft.drawCentreString(tempText, centerX, textY, FONT_SMALL);

  textY += 20;
  tempText = "Pressure = " + String(touchZ);
  tft.drawCentreString(tempText, centerX, textY, FONT_SMALL);
}

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
  tft.drawCentreString(myDate, centerX, centerY*3, FONT_MED);

  char myTime[12];
  strftime(myTime, 12, "%T", &timeinfo);
  tft.drawCentreString(myTime, centerX, 10, FONT_BIG);

}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(100);
  delay(2000);

  // Do WiFi first
  Serial.println();
  Serial.println();
  Serial.print("Starting Wifi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }

  Serial.println("Connected");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer1, ntpServer2, ntpServer3);
  delay(1000);

  // Wait for time synchronization
  Serial.print("Getting time");
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    delay(1000);
    Serial.print(".");
  }  
  Serial.println("Done");
  //disconnect WiFi as it's no longer needed
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("Wifi Off");
  delay(2000);

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

  tft.drawCentreString("Hello World!", centerX, 30, FONT_MED);
  tft.drawCentreString("Press to start", centerX, 120, FONT_MED);

}

void loop() {
  static bool touched = false;
  // Checks if Touchscreen was touched, and prints X, Y and Pressure (Z) info on the TFT display and Serial Monitor
  if (touchscreen.tirqTouched() && touchscreen.touched()) {
    // Get Touchscreen points
    TS_Point p = touchscreen.getPoint();
    // Calibrate Touchscreen points with map function to the correct width and height
    x = map(p.x, 200, 3700, 1, SCREEN_WIDTH);
    y = map(p.y, 240, 3800, 1, SCREEN_HEIGHT);
    z = p.z;
    // Clear TFT screen
    if (!touched) tft.fillScreen(TFT_BLACK);
    printTouchToDisplay(x, y, z);
    delay(100);
    touched = true;
  }
  if (touched) printLocalTime();
  delay(100);
}