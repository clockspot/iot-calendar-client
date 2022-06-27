//Sketch for LOLIN ESP32 to pull data from iot-calendar-server
//Adapted from https://github.com/kristiantm/eink-family-calendar-esp32

//#include "time.h"

// const unsigned long UpdateInterval = (30L * 60L - 03) * 1000000L; // Update delay in microseconds, 13-secs is the time to update so compensate for that
// bool LargeIcon =  true;
// bool SmallIcon =  false;
// #define Large  6
// #define Small  4
// String time_str, Day_time_str; // strings to hold time and received weather data;

#define BATTERY_PIN 35

//#include <string>
#include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson needs version v6 or above
//#include "Wire.h"

#include <WiFi.h>
//#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h> // Needs to be from the ESP32 platform version 3.2.0 or later, as the previous has problems with http-redirect

//using WebServerClass = WebServer;
//#include <FS.h>
//#include <AutoConnect.h>
//#include <webconfig.h>

#include <GxEPD2_3C.h>
#define ENABLE_GxEPD2_GFX 0 //TODO what does this do
#include <Fonts/IOTLight16pt7b.h>
#include <Fonts/IOTBold16pt7b.h>
#include <Fonts/IOTRegular21pt7b.h>
#include <Fonts/IOTLight48pt7b.h>
#include <Fonts/IOTBold108pt7b.h>
#include <Fonts/IOTSymbols16pt7b.h>
//#include "GxEPD2_display_selection_new_style.h"

#include "config.h"

//WebServerClass  server;
//AutoConnect portal(server);
//AutoConnectConfig config;
//AutoConnectAux  elementsAux;
//AutoConnectAux  saveAux;

//int portalTimeoutTime = 60 * 5; // Seconds to wait before showing calendar

/* Mapping of Waveshare ESP32 Driver Board - 3C is tri-color displays, BW is black and white ***** */
GxEPD2_3C<GxEPD2_750c_Z08, GxEPD2_750c_Z08::HEIGHT/2> display(GxEPD2_750c_Z08(/*CS=*/ 15, /*DC=*/ 27, /*RST=*/ 26, /*BUSY=*/ 25));
//GxEPD2_BW<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT/2> display(GxEPD2_750_T7(/*CS=*/15, /*DC=*/ 27, /*RST=*/ 26, /*BUSY=*/ 25)); // GDEW075T7 800x480

/* Mapping of Generic ESP32 Driver Board - 3C is tri-color displays, BW is black and white ***** */
//GxEPD2_3C<GxEPD2_750c_Z08, GxEPD2_750c_Z08::HEIGHT/2> display(GxEPD2_750c_Z08(/*CS=*/ SS, /*DC=*/ 17, /*RST=*/ 16, /*BUSY=*/ 4));
//GxEPD2_BW<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT/2> display(GxEPD2_750_T7(/*CS=5*/ SS, /*DC=*/ 17, /*RST=*/ 16, /*BUSY=*/ 4)); // GDEW075T7 800x480

//int    wifi_signal, wifisection, displaysection, MoonDay, MoonMonth, MoonYear, start_time;

HTTPClient http;
//WiFiSSLClient sslClient;

float battLevel = -1; // Being set when reading battery level - used to avoid deep sleep when under 0%, and when drawing battery

// Main flow of the program. It is designed to boot up, pull the info and refresh the screen, and then go back into deep sleep.
void setup() {

  // Initialize board
  Serial.begin(115200);

  //Get battery level
  uint8_t battLevel = analogRead(BATTERY_PIN);

  //Initialize and clear display
  display.init(115200);
  display.setRotation(1);
  display.setTextWrap(false);
  displayClear();

  //Start wifi
  Serial.print(F("\nConnecting to WiFi SSID "));
  Serial.println(NETWORK_SSID);
  WiFi.begin(NETWORK_SSID, NETWORK_PASS);
  int timeout = 0;
  while(WiFi.status()!=WL_CONNECTED && timeout<15) {
    timeout++; delay(1000);
  }
  if(WiFi.status()==WL_CONNECTED){ //did it work?
    //Serial.print(millis());
    Serial.println(F("Connected!"));
    //Serial.print(F("SSID: ")); Serial.println(WiFi.SSID());
    Serial.print(F("Signal strength (RSSI): ")); Serial.print(WiFi.RSSI()); Serial.println(F(" dBm"));
    Serial.print(F("Local IP: ")); Serial.println(WiFi.localIP());
  } else {
    Serial.println(F("Wasn't able to connect."));
    displayError(F("Couldn't connect to WiFi."));
    return;
  }

  // Get time from timeserver - used when going into deep sleep again to ensure that we wake at the right hour
  configTime(TZ_OFFSET_SEC, DST_OFFSET_SEC, NTP_HOST);

  // Read battLevel and set battLevel variable
  //readBattery();

  Serial.print(F("\nConnecting to data source "));
  Serial.println(DATA_SRC);
  http.begin(DATA_SRC);
  int httpReturnCode = http.GET();
  if(httpReturnCode!=200) {
    Serial.println(F("Wasn't able to connect to host."));
    displayError(F("Couldn't connect to data host.")); //TODO could display code
    return;
  }

  // Allocate the JSON document
  // Use arduinojson.org/v6/assistant to compute the capacity.
  DynamicJsonDocument doc(8192);
  
  // Parse JSON object
  DeserializationError error = deserializeJson(doc, http.getStream());
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    displayError(F("Couldn't process data. Please restart."));
    http.end();
    return;
  }

  // Disconnect
  http.end();

  //disconnect WiFi as it's no longer needed
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

  // display on serial - last seen in commit 12eada4

  // display on e-ink
  display.init(115200);
  display.setRotation(3);
  display.setTextWrap(false);
  display.setTextColor(GxEPD_BLACK);

  //vars for calculating text bounds and setting draw origin
  int16_t tbx, tby; uint16_t tbw, tbh, cw; uint16_t x, y;
  char buf[30];

  display.setFullWindow();
  display.firstPage();
  do
  {
    y = 0; //top padding
    display.fillScreen(GxEPD_WHITE);

    //render here
    //TODO set top pad?
    for (JsonObject day : doc.as<JsonArray>()) {
      //header
      if(day["weekdayRelative"]=="Today") {
        //render big top date

        //approach A:
        //the date should be centered, and day/month rendered to its sides - last seen in commit 12eada4

        //approach B:
        //date is left-aligned in right half; day/month right-aligned in left half (ish) - last seen in commit 12eada4

        //approach C:
        //same as B, but entire line is centered rather than using a fixed middle
        //add up width of all components
        cw = 0; //center width
        display.setFont(&IOTLight48pt7b);
        display.getTextBounds(day["weekdayShort"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        uint16_t ww = tbw; //weekday width
        display.getTextBounds(day["monthShort"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        uint16_t mw = tbw; //month width
        cw += (mw>ww? mw: ww);
        display.setFont(&IOTBold108pt7b);
        display.getTextBounds(day["date"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        cw += 30 + tbw;
        
        //now render, using calculated center width
        x = (display.width()-cw)/2;
        y += 155; //new line

        if(day["weekdayShort"]=="Sun") display.setTextColor(GxEPD_RED);

        display.setFont(&IOTLight48pt7b);
        //render weekday
        x = (display.width()-cw)/2;
        if(ww<mw) x += mw-ww;
        display.setCursor(x, y - 72);
        display.print(day["weekdayShort"].as<char*>());
        //render month
        x = (display.width()-cw)/2;
        if(mw<ww) x += ww-mw;
        display.setCursor(x, y);
        display.print(day["monthShort"].as<char*>());

        display.setFont(&IOTBold108pt7b);
        x = (display.width()-cw)/2 + (mw>ww? mw: ww) + 30;
        display.setCursor(x, y-256); //the -256 became necessary somewhere between 72pt and 108pt
        display.print(day["date"].as<char*>());

        display.setTextColor(GxEPD_BLACK);

        y += 12 + 4; //padding

        //sun/moon, two-line version - last seen in commit 12eada4

        //sun/moon, one-line version
        //entire line is centered; add up width of all components
        cw = 0; //center width

        display.setFont(&IOTLight16pt7b);
        //sunset, moonset (or moonfixed), and em dashes/slashes
        display.getTextBounds(day["sky"]["sunset"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh); cw += tbw;
        cw += 16; //em dash with padding either side
        if(!day["sky"]["moonfixed"]) {
          display.getTextBounds(day["sky"]["moonset"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh); cw += tbw;
          if(!day["sky"]["upfirst"]) {
            display.getTextBounds("/",0,0,&tbx,&tby,&tbw,&tbh); cw += tbw + 5; //3px left padding, 2px right padding
          } else {
            cw += 16; //em dash with padding either side
          }
        } else {
          display.getTextBounds(day["sky"]["moonfixed"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh); cw += tbw;
        }

        display.setFont(&IOTBold16pt7b);
        //sunrise, moonrise
        display.getTextBounds(day["sky"]["sunrise"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh); cw += tbw;
        if(!day["sky"]["moonfixed"]) {
          display.getTextBounds(day["sky"]["moonrise"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh); cw += tbw;
        }

        cw += 22 + 19; //sun and moon icons
        cw += 10 + 30 + 10; //gaps between

        //now render, using calculated center width
        y += 16*2; //new line
        x = (display.width()-cw)/2;

        display.setFont(&IOTSymbols16pt7b);
        display.setCursor(x, y);
        display.print("9"); //sun
        x += 22 + 10; //sun icon width + gap between

        display.setFont(&IOTBold16pt7b);
        display.setCursor(x, y);
        display.print(day["sky"]["sunrise"].as<char*>());
        display.getTextBounds(day["sky"]["sunrise"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        x += tbw;

        display.setFont(&IOTLight16pt7b);
        x += 3; //em dash left padding
        display.setCursor(x, y); display.print("-");
        display.setCursor(x+4, y); display.print("-"); //poor man's em dash
        x += 13; //em dash width + right padding
        display.setCursor(x, y);
        display.print(day["sky"]["sunset"].as<char*>());
        display.getTextBounds(day["sky"]["sunset"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        x += tbw + 30; //gap between

        display.setFont(&IOTSymbols16pt7b);
        itoa(day["sky"]["moonphase"].as<int>(),buf,10); //int to char buffer
        display.setCursor(x, y);
        display.print(buf);
        x += 19 + 10; //moon icon width + gap between

        if(day["sky"]["moonfixed"]) {
          display.setFont(&IOTLight16pt7b);
          display.setCursor(x, y);
          display.print(day["sky"]["moonfixed"].as<char*>());
        } else {
          if(!day["sky"]["upfirst"]) { //moonrise then moonset (below)
            display.setFont(&IOTBold16pt7b);
            display.setCursor(x, y);
            display.print(day["sky"]["moonrise"].as<char*>());
            display.getTextBounds(day["sky"]["moonrise"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
            x += tbw;
            display.setFont(&IOTLight16pt7b);
            x += 3;
            display.setCursor(x, y); display.print("-");
            display.setCursor(x+4, y); display.print("-"); //poor man's em dash
            x += 13; //guessed em width
          }
          display.setFont(&IOTLight16pt7b);
          display.setCursor(x, y);
          display.print(day["sky"]["moonset"].as<char*>());
          display.getTextBounds(day["sky"]["moonset"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
          x += tbw;
          if(day["sky"]["upfirst"]) { //moonset (above) then moonrise
            display.setFont(&IOTLight16pt7b);
            display.setCursor(x+3, y); //3px left padding
            display.print("/");
            display.getTextBounds("/",0,0,&tbx,&tby,&tbw,&tbh);
            x += tbw + 5; //3px left padding, 2px right padding
            display.setFont(&IOTBold16pt7b);
            display.setCursor(x, y);
            display.print(day["sky"]["moonrise"].as<char*>());
            display.getTextBounds(day["sky"]["moonrise"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
            x += tbw;
          }
        }

      } else { //not today
        //render smaller date header
        y += 12; //padding

        //relative date - last seen in commit 12eada4

        //actual date
        if(day["weekdayShort"]=="Sun") display.setTextColor(GxEPD_RED);
        cw = 0;
        display.setFont(&IOTRegular21pt7b);
        display.getTextBounds(day["weekdayShort"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        y += 21*2; //new line
        cw += tbw + 12;
        display.getTextBounds(day["monthShort"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        cw += tbw + 12;
        //display.setFont(&IOTBold21pt7b);
        display.getTextBounds(day["date"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        cw += tbw;
        x = (display.width()-cw)/2;
        //display.setFont(&IOTRegular21pt7b);
        display.setCursor(x, y);
        display.print(day["weekdayShort"].as<char*>());
        display.getTextBounds(day["weekdayShort"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        x += tbw + 12;
        display.setCursor(x, y);
        display.print(day["monthShort"].as<char*>());
        display.getTextBounds(day["monthShort"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        x += tbw + 12;
        //display.setFont(&IOTBold21pt7b);
        display.setCursor(x, y);
        display.print(day["date"].as<char*>());
        display.setTextColor(GxEPD_BLACK);
      }

      y += 12; //padding
      //render weather
      for (JsonObject w : day["weather"].as<JsonArray>()) {
        x = 10; //left padding
        display.setFont(&IOTBold16pt7b);
        if(w["isDaytime"]) {
          display.getTextBounds("High",0,0,&tbx,&tby,&tbw,&tbh);
          y += 16*2; //new line
          display.setCursor(x, y);
          display.print("High");
          x += tbw + 10; //gap between
        } else {
          display.getTextBounds("Low",0,0,&tbx,&tby,&tbw,&tbh);
          y += 16*2; //new line
          display.setCursor(x, y);
          display.print("Low");
          x += tbw + 10; //gap between
        }
        itoa(w["temperature"].as<int>(),buf,10); //int to char buffer
        display.getTextBounds(buf,0,0,&tbx,&tby,&tbw,&tbh);
        display.setCursor(x, y);
        display.print(buf);
        x += tbw;
        display.setFont(&IOTLight16pt7b);
        //display precipitation if applicable
        if(w.containsKey("precipChance") && w["precipChance"]) {
          display.getTextBounds("/",0,0,&tbx,&tby,&tbw,&tbh);
          display.setCursor(x+3, y); //3px left padding
          display.print("/");
          x += tbw + 5; //3px left padding, 2px right padding
          itoa(w["precipChance"].as<int>(),buf,10); //int to char buffer
          display.getTextBounds(buf,0,0,&tbx,&tby,&tbw,&tbh);
          display.setCursor(x, y);
          display.print(buf);
          x += tbw;
          display.getTextBounds("%",0,0,&tbx,&tby,&tbw,&tbh);
          display.setCursor(x+3, y); //3px left padding
          display.print("%");
          x += tbw + 5; //3px left padding, 2px right padding
        }
        x += 15; //gap before forecast
        display.setCursor(x, y);
        display.print(w["shortForecast"].as<char*>());
      }
      y += 12; //padding
      //render events
      for (JsonObject e : day["events"].as<JsonArray>()) {
        x = 10; //left padding //formerly 20
        if(e["style"]=="red") display.setTextColor(GxEPD_RED);
        else display.setTextColor(GxEPD_BLACK);
        display.setFont(&IOTBold16pt7b);
        display.getTextBounds("X",0,0,&tbx,&tby,&tbw,&tbh);
        y += 16*2; //new line
        display.setCursor(x, y);
        //display.print("-");
        //x += 15; //gap between
        if(!e["allday"]) {
          display.getTextBounds(e["timestart"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
          display.setCursor(x, y);
          display.print(e["timestart"].as<char*>());
          x += tbw;
          display.setFont(&IOTLight16pt7b); //leave the rest non-bolded
          if(e.containsKey("timeend")) {
            x += 3;
            display.setCursor(x, y); display.print("-");
            display.setCursor(x+4, y); display.print("-"); //poor man's em dash
            x += 13; //guessed em width
            display.getTextBounds(e["timeend"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
            display.setCursor(x, y);
            display.print(e["timeend"].as<char*>());
            x += tbw;
          }
          x += 15; //gap between
        }
        display.getTextBounds(e["summary"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        display.setCursor(x, y);
        display.print(e["summary"].as<char*>());
        x += tbw + 15;
        display.setFont(&IOTLight16pt7b); //leave the rest non-bolded
        if(e["allday"] && e["dend"]!=e["dstart"]) {
          display.getTextBounds("(thru",0,0,&tbx,&tby,&tbw,&tbh);
          display.setCursor(x, y);
          display.print("(thru");
          x += tbw + 10; //gap between
          display.getTextBounds(e["dendShort"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
          display.setCursor(x, y);
          display.print(e["dendShort"].as<char*>());
          x += tbw;
          display.print(")");
        }
      }
    } //end for each day

    //battery level
    y += 12; //padding
    x = 10; //left padding
    y += 16*2; //new line
    display.setFont(&IOTBold16pt7b);
    display.setCursor(x, y);
    display.print("Battery");
    display.getTextBounds("Battery",0,0,&tbx,&tby,&tbw,&tbh);
    x += tbw + 10; //gap between
    display.setFont(&IOTLight16pt7b);
    display.setCursor(x, y);
    itoa(battLevel,buf,10); //int to char buffer
    display.print(buf);

  } while (display.nextPage());
  //display.hibernate();
  display.powerOff();

            // WiFi.mode(WIFI_STA);
            // WiFi.begin(ssid, password);
            // Serial.print("Connecting to WiFi ..");
            // while (WiFi.status() != WL_CONNECTED) {
            //   Serial.print('.');
            //   delay(1000);
            // }
            // Serial.println(WiFi.localIP());
  
  // //Initialize e-ink display
  // display.init(115200); // uses standard SPI pins, e.g. SCK(18), MISO(19), MOSI(23), SS(5)
  // SPI.end(); // release standard SPI pins, e.g. SCK(18), MISO(19), MOSI(23), SS(5)
  // SPI.begin(13, 12, 14, 15); // Map and init SPI pins SCK(13), MISO(12), MOSI(14), SS(15) - adjusted to the recommended PIN settings from Waveshare - note that this is not the default for most screens
  // //SPI.begin(18, 19, 23, 5); // 

  // WiFi.setHostname("EInkCalendar");
  // startWifiServer();

  // bool isConfigured = false;
  // bool isWebConnected = false;

  // isConfigured = loadConfig();
  // if(isConfigured) Serial.println("Configuration loaded"); else Serial.println("Configuration not loaded");

  // while(WiFi.status() != WL_CONNECTED) {
  //     delay(500);
  //     Serial.print(".");
  // }

  // isWebConnected = internetWorks();


  // if(isWebConnected) Serial.println("Internet connected"); else Serial.println("Internet not connected");
  
  // if((isConfigured) && (isWebConnected)) {

  //   Serial.println("Configuration exist and internet connection works - displaying calendar");

  //   // Get time from timeserver - used when going into deep sleep again to ensure that we wake at the right hour
  //   configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  //   // Read battLevel and set battLevel variable
  //   readBattery();

  //   //Get and draw calendar on display
  //   display.setRotation(calendarOrientation);
  //   displayCalendar(); // Main flow for drawing calendar

  //   delay(1000);

  //   // Turn off display before deep sleep
  //   display.powerOff();
  // }

  
  
  
  
  
  
  //deepSleepTillLater();

} //end setup

void displayError(String msg) {
  // display on e-ink
  display.setTextColor(GxEPD_BLACK);
  display.setFont(&IOTLight16pt7b);

  //vars for calculating text bounds and setting draw origin
  int16_t tbx, tby; uint16_t tbw, tbh;
  display.getTextBounds(msg, 0, 0, &tbx, &tby, &tbw, &tbh);

  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(((display.width() - tbw) / 2) - tbx, ((display.height() - tbh) / 2) - tby);
    display.print(msg);
  } while (display.nextPage());
  display.hibernate();
}
void displayClear() {
  displayError("");
}

// Not used, as we boot up from scratch every time we wake from deep sleep
void loop() {}

// bool internetWorks() {
//   HTTPClient http;
//   if (http.begin("api.weather.gov", 443)) {
//     http.end();
//     return true;
//   } else {
//     http.end();
//     return false;
//   }
// }


// // Main display code - assumes that the display has been initialized
// bool displayCalendar()
// {

//   // Getting calendar from your published google script
//   Serial.println("Getting calendar");
//   Serial.println(calendarRequest);

//   http.end();
//   http.setTimeout(20000);
//   http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
//   if (!http.begin(calendarRequest)) {
//     Serial.println("Cannot connect to google script");
//     return false;
//   } 

//   Serial.println("Connected to google script");
//   int returnCode = http.GET();
//   Serial.print("Returncode: "); Serial.println(returnCode);
//   String response = http.getString();
//   Serial.print("Response: "); Serial.println(response);

//   int indexFrom = 0;
//   int indexTo = 0;
//   int cutTo = 0;

//   String strBuffer = "";

//   int count = 0;
//   int line = 0;
//   struct calendarEntries calEnt[calEntryCount];

//   Serial.println("IntexFrom");  
//   indexFrom = response.lastIndexOf("\n") + 1;



//   // Fill calendarEntries with entries from the get-request
//   while (indexTo>=0 && line<calEntryCount) {
//     count++;
//     indexTo = response.indexOf(";",indexFrom);
//     cutTo = indexTo;

//     if(indexTo != -1) { 
//       strBuffer = response.substring(indexFrom, cutTo);
      
//       indexFrom = indexTo + 1;
      
//       Serial.println(strBuffer);

//       if(count == 1) {
//         // Set entry time
//         calEnt[line].calTime = strBuffer.substring(0,21); //Exclude end date and time to avoid clutter - Format is "Wed Feb 10 2020 10:00"

//       } else if(count == 2) {
//         // Set entry title
//         calEnt[line].calTitle = strBuffer;

//       } else {
//           count = 0;
//           line++;
//       }
//     }
//   }

//   struct tm timeinfo;
//   if(!getLocalTime(&timeinfo)){
//     Serial.println("Failed to obtain time");
//   }

//   String weatherIcon;

//   // Get weather info using the OWM API "onecall" which gives you current info, 3-hour forecast and 5-day forecast in one call. We only use the first day of the 5-day forecast
//   bool weatherSuccess = obtain_wx_data("onecall");
//   if(weatherSuccess) {
//     weatherIcon = WxForecast[0].Icon.substring(0,2);

//     /*Serial.println("Weatherinfo");
//     Serial.println(WxForecast[0].Icon);
//     Serial.println(WxForecast[0].High);
//     Serial.println(WxForecast[0].Winddir);
//     Serial.println(WxForecast[0].Windspeed);
//     Serial.println(WxForecast[0].Rainfall);*/
//   }  


//   // All data is now gathered and processed.
//   // Prepare to refresh the display with the calendar entries and weather info

//   // Turn off text-wrapping
//   display.setTextWrap(false);

//   display.setRotation(calendarOrientation);

//   // Clear the screen with white using full window mode. Not strictly nessecary, but as I selected to use partial window for the content, I decided to do a full refresh first.
//   display.setFullWindow();
//   display.firstPage();
//   do {
//     display.fillScreen(GxEPD_WHITE);
//   }   while(display.nextPage());

//   // Print the content on the screen - I use a partial window refresh for the entire width and height, as I find this makes a clearer picture
//   display.setPartialWindow(0, 0, display.width(), display.height());
//   display.firstPage();
//   do {
//     int x = calendarPosX;
//     int y = calendarPosY;

//     display.fillScreen(GxEPD_WHITE);
//     display.setTextColor(GxEPD_BLACK);

//     // Print mini-test in top in white (e.g. not visible) - avoids a graphical glitch I observed in all first-lines printed
//     display.setCursor(x, 0);
//     display.setTextColor(GxEPD_WHITE);
//     display.setFont(fontEntryTime);
//     display.print(weekday[timeinfo.tm_wday]);

//     // Print morning greeting (Happy X-day)
//     display.setCursor(x, y);
//     display.setTextColor(GxEPD_BLACK);
//     display.setFont(fontMainTitle);
//     display.print(weekday[timeinfo.tm_wday]);

//     // If fetching the weather was a succes, then print the weather
//     if(weatherSuccess) {
//       drawOWMIcon(weatherIcon);
//     }

//     // Draw battery level
//     if(battLevel >= 0) {
//       int batX = weatherPosX+50;
//       int batY = weatherPosY + 130;

//       display.drawRect(batX + 15, batY - 12, 19, 10, GxEPD_BLACK);
//       display.fillRect(batX + 34, batY - 10, 2, 5, GxEPD_BLACK);
//       display.fillRect(batX + 17, batY - 10, 15 * battLevel / 100.0, 6, GxEPD_BLACK);
//       Serial.println("Draw battery: " && 15 * battLevel / 100.0 && " - " && battLevel);
//     }


//     // Set position for the first calendar entry
//     y = y + 45;
    
//     // Print calendar entries from first [0] to the last fetched [line-1] - in case there is fewer events than the maximum allowed
//     for(int i=0;  i < line; i++) {

//       // Print event time
//       display.setCursor(x, y);
//       display.setFont(fontEntryTime);
//       display.print(calEnt[i].calTime);

//       // Print event title
//       display.setCursor(x, y + 30);
//       display.setFont(fontEntryTitle);
//       display.print(calEnt[i].calTitle);

//       // Prepare y-position for next event entry
//       y = y + calendarSpacing;

//     }

//   } while(display.nextPage());

//   return true;
// }

// Sleep until set wake-hour
void deepSleepTillLater() {
  // If battery is too low (see getBattery code), enter deepSleep and do not wake up
  if(battLevel == 0) {
    esp_deep_sleep_start();
  }

  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
  }

  int wakeInSec = 0;
  wakeInSec = timeinfo.tm_hour * 60 * 60 + timeinfo.tm_min + timeinfo.tm_sec; //time since midnight
  wakeInSec = (wakeInSec < 86400? 86400 - wakeInSec: 0); //time until midnight
  wakeInSec += 60; //time until 00:01:00
  if(wakeInSec >= 86400) wakeInSec -= 86400; //in case it's e.g. 00:00:30, only wait 30s, not 24h 30s!

  Serial.print("Wake in sec: ");
  Serial.println(wakeInSec);

  esp_sleep_enable_timer_wakeup(wakeInSec * 1000000ULL);
  Serial.flush(); 
  esp_deep_sleep_start();
}

// Get and decode weather data from OWM - credits to ESP32 Weather Station project on GIT, which I have heavily modified from - using onecall instead of forecast and current for instance. I have removed the code for weather and forecast types

// bool obtain_wx_data(const String& RequestType) {
//   const String units = (Units == "M" ? "metric" : "imperial");

//   HTTPClient http;
//   http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  
//   String weatherURL = String("https://") + OWMserver + "/data/2.5/" + RequestType + "?APPID=" + OWMapikey + "&mode=json&units=" + units + "&lang=" + Language + "&lat=" + Lattitude + "&lon=" + Longitude;

//   Serial.println(weatherURL);
//   http.begin(weatherURL.c_str());

//   int httpReturnCode = http.GET();

//   if(httpReturnCode > 0) {
//     if (!DecodeWeather(http.getStream(), RequestType)) return false;
//     http.end();
//     return true;
//   }

//   http.end();
//   return false;

// }

// // Get and decode weather data from OWM - credits to ESP32 Weather Station project on GIT, which I have heavily modified from - only using onecall instead of forecast and current for instance
// bool DecodeWeather(WiFiClient& json, String Type) {
//   Serial.print(F("\nCreating object...and "));
//   // allocate the JsonDocument
//   DynamicJsonDocument doc(35 * 1024);
//   // Deserialize the JSON document
//   DeserializationError error = deserializeJson(doc, json);
//   // Test if parsing succeeds.
//   if (error) {
//     Serial.print(F("deserializeJson() failed: "));
//     Serial.println(error.c_str());
//     return false;
//   }
//   // convert it to a JsonObject
//   JsonObject root = doc.as<JsonObject>();
//   Serial.println(" Decoding " + Type + " data");

//   if (Type == "onecall") {
//     // Selecting "daily" forecast for the next five days - alternatives are "forecast" for 3-hour forcasts, and "current" for the weather right now
//     JsonArray list                  = root["daily"];
//     int listLength = list.size();
//     Serial.println(listLength);

//     // Collecting the whole forecast - even though I am only using the first day list[0]
//     for (byte r = 0; r < listLength; r++) {
//       Serial.println("\nPeriod-" + String(r) + "--------------");
//       WxForecast[r].Dt                = list[r]["dt"].as<char*>();
//       WxForecast[r].Temperature       = list[r]["temp"]["day"].as<float>();              //Serial.println("Temp: "+String(WxForecast[r].Temperature));
//       WxForecast[r].Low               = list[r]["temp"]["min"].as<float>();          //Serial.println("TLow: "+String(WxForecast[r].Low));
//       WxForecast[r].High              = list[r]["temp"]["max"].as<float>();         // Serial.println("THig: "+String(WxForecast[r].High));
//       WxForecast[r].Pressure          = list[r]["pressure"].as<float>();          //Serial.println("Pres: "+String(WxForecast[r].Pressure));
//       WxForecast[r].Humidity          = list[r]["humidity"].as<float>();          //Serial.println("Humi: "+String(WxForecast[r].Humidity));
//       WxForecast[r].Icon              = list[r]["weather"][0]["icon"].as<char*>();       // Serial.println("Icon: "+String(WxForecast[r].Icon));
//       WxForecast[r].Description       = list[r]["weather"][0]["description"].as<char*>(); Serial.println("Desc: "+String(WxForecast[r].Description));
//       WxForecast[r].Cloudcover        = list[r]["clouds"].as<int>();              // Serial.println("CCov: "+String(WxForecast[r].Cloudcover)); // in % of cloud cover
//       WxForecast[r].Windspeed         = list[r]["wind_speed"].as<float>();             //Serial.println("WSpd: "+String(WxForecast[r].Windspeed));
//       WxForecast[r].Winddir           = list[r]["wind_deg"].as<float>();              // Serial.println("WDir: "+String(WxForecast[r].Winddir));
//       WxForecast[r].Rainfall          = list[r]["rain"].as<float>();               // Serial.println("Rain: "+String(WxForecast[r].Rainfall));
//     }
//   }

//   return true;
// }

// void readBattery() {
//   uint8_t percentage = 100;

//   //Adjust the pin below depending on what pin you measure your battery voltage on. 
//   //On LOLIN D32 boards this is build into pin 35 - for other ESP32 boards, you have to manually insert a voltage divider between the battery and an analogue pin
//   uint8_t batteryPin = 35;

//   // Set OHM values based on the resistors used in your voltage divider http://www.ohmslawcalculator.com/voltage-divider-calculator  
//   float R1 = 30;
//   float R2 = 100  ;

//   float voltage = analogRead(batteryPin) / 4096.0 * (1/(R1/(R1+R2)));
//   if (voltage > 1 ) { // Only display if there is a valid reading
//     Serial.println("Voltage = " + String(voltage));

//     if (voltage >= 4.1) percentage = 100;
//     else if (voltage >= 3.9) percentage = 75;
//     else if (voltage >= 3.7) percentage = 50;
//     else if (voltage >= 3.6) percentage = 25;
//     else if (voltage <= 3.5) percentage = 0;
//     Serial.println("battLevel = " + String(percentage));
//     battLevel = percentage;
//   }
// }
