//Sketch for LOLIN ESP32 to pull data from iot-calendar-server
//Adapted from https://github.com/kristiantm/eink-family-calendar-esp32
//Earlier alternate screen layouts, etc. in history of nano-test-local sketch

#include "config.h"

#include <ArduinoJson.h> // https://github.com/bblanchon/ArduinoJson needs version v6 or above
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h> // Needs to be from the ESP32 platform version 3.2.0 or later, as the previous has problems with http-redirect
#include <GxEPD2_3C.h>
#define ENABLE_GxEPD2_GFX 0
#include <Fonts/IOTLight16pt7b.h>
#include <Fonts/IOTBold16pt7b.h>
#include <Fonts/IOTRegular21pt7b.h>
#include <Fonts/IOTLight48pt7b.h>
#include <Fonts/IOTBold108pt7b.h>
#include <Fonts/IOTSymbols16pt7b.h>

// Mapping of Waveshare ESP32 Driver Board - 3C is tri-color displays, BW is black and white
GxEPD2_3C<GxEPD2_750c_Z08, GxEPD2_750c_Z08::HEIGHT/2> display(GxEPD2_750c_Z08(/*CS=*/ 15, /*DC=*/ 27, /*RST=*/ 26, /*BUSY=*/ 25));
//GxEPD2_BW<GxEPD2_750_T7, GxEPD2_750_T7::HEIGHT/2> display(GxEPD2_750_T7(/*CS=*/15, /*DC=*/ 27, /*RST=*/ 26, /*BUSY=*/ 25));

float battLevel;
HTTPClient http;
struct tm timeinfo;
int todInSec;

// Main flow of the program. It is designed to boot up, pull the info and refresh the screen, and then go back into deep sleep.
void setup() {

  // Initialize board
  Serial.begin(115200);

  //Get battery level in mV
  //https://www.youtube.com/watch?v=yZjpYmWVLh8&t=88s
    //battLevel = (analogRead(35) / 7.445) / 4.096;
  battLevel = readBatteryVoltage();
  //TODO a conversion similar to this one, after testing real-world voltages
  // if (voltage >= 4.1) percentage = 100;
  // else if (voltage >= 3.9) percentage = 75;
  // else if (voltage >= 3.7) percentage = 50;
  // else if (voltage >= 3.6) percentage = 25;
  // else if (voltage <= 3.5) percentage = 0;

  //Initialize and clear display
  display.init(115200);
  SPI.end(); // release standard SPI pins, e.g. SCK(18), MISO(19), MOSI(23), SS(5)
  SPI.begin(13, 12, 14, 15); // Map and init SPI pins SCK(13), MISO(12), MOSI(14), SS(15) - adjusted to the recommended PIN settings from Waveshare - note that this is not the default for most screens
  display.setRotation(3);
  display.setTextWrap(false);
  displayClear();

  //Start wifi
  for(int attempts=0; attempts<3; attempts++) {
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
      break; //leave attempts loop
    } else {
      // #ifdef NETWORK2_SSID
      //   Serial.print(F("\nConnecting to WiFi SSID "));
      //   Serial.println(NETWORK2_SSID);
      //   WiFi.begin(NETWORK2_SSID, NETWORK2_PASS);
      //   int timeout = 0;
      //   while(WiFi.status()!=WL_CONNECTED && timeout<15) {
      //     timeout++; delay(1000);
      //   }
      //   if(WiFi.status()==WL_CONNECTED){ //did it work?
      //     //Serial.print(millis());
      //     Serial.println(F("Connected!"));
      //     //Serial.print(F("SSID: ")); Serial.println(WiFi.SSID());
      //     Serial.print(F("Signal strength (RSSI): ")); Serial.print(WiFi.RSSI()); Serial.println(F(" dBm"));
      //     Serial.print(F("Local IP: ")); Serial.println(WiFi.localIP());
      //     break; //leave attempts loop
      //   }
      // #endif
    }
  }
  if(WiFi.status()!=WL_CONNECTED) {
    Serial.println(F("Wasn't able to connect."));
    displayError(F("Couldn't connect to WiFi."));
    //Close unneeded things
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return;
  }

  // Get time from timeserver - used when going into deep sleep again to ensure that we wake at the right hour
  configTime(TZ_OFFSET_SEC, DST_OFFSET_SEC, NTP_HOST);
  
  //Convert to time of day in seconds (do it now so it can be included in the request and logged)
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
  }
  todInSec = timeinfo.tm_hour*60*60 + timeinfo.tm_min*60 + timeinfo.tm_sec;

  //Get data and attempt to parse it
  //This can fail two ways: httpReturnCode != 200, or parse fails
  //In either case, we will attempt to pull it anew
  int httpReturnCode;
  bool parseSuccess = false;
  // Allocate the JSON document
  // Use arduinojson.org/v6/assistant to compute the capacity.
  DynamicJsonDocument doc(8192);

  for(int attempts=0; attempts<3; attempts++) {
    Serial.print(F("\nConnecting to data source "));
    Serial.print(DATA_SRC);
    Serial.print(F(" at tod "));
    Serial.println(todInSec,DEC);
//     std::string targetURL(DATA_SRC);
//     std::string todInSecStr = std::to_string(todInSec);
    http.begin(String(DATA_SRC)+"&tod="+String(todInSec));
    httpReturnCode = http.GET();
    if(httpReturnCode==200) { //got data, let's try to parse
      DeserializationError error = deserializeJson(doc, http.getStream());
      if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
      } else {
        parseSuccess = true;
        break; //leave attempts loop
      }
    }
  }
  if(httpReturnCode!=200) {
    Serial.println(F("Wasn't able to connect to host."));
    displayError(F("Couldn't connect to data host.")); //TODO could display code
    //Close unneeded things
    http.end();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return;
  }
  if(!parseSuccess) {
    displayError(F("Couldn't process data."));
    //Close unneeded things
    http.end();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return;
  }

  //If we reach this point, we've got good, parsed data

  //Close unneeded things
  http.end();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);

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
        //date is left-aligned in right half; day/month right-aligned in left half
        //entire line is centered; add up width of all components
        cw = 0; //center width
        display.setFont(&IOTLight48pt7b);
        display.getTextBounds(day["weekdayShort"].as<String>(),0,0,&tbx,&tby,&tbw,&tbh);
        uint16_t ww = tbw; //weekday width
        display.getTextBounds(day["monthShort"].as<String>(),0,0,&tbx,&tby,&tbw,&tbh);
        uint16_t mw = tbw; //month width
        cw += (mw>ww? mw: ww);
        display.setFont(&IOTBold108pt7b);
        display.getTextBounds(day["date"].as<String>(),0,0,&tbx,&tby,&tbw,&tbh);
        cw += 30 + tbw;
        
        //now render, using calculated center width
        #ifdef HORIZ_OFFSET_PX
          x = ((display.width()-cw)/2)+HORIZ_OFFSET_PX;
        #else
          x = (display.width()-cw)/2;
        #endif
        y += 155; //new line

        display.setTextColor(GxEPD_BLACK);
        #ifdef SUNDAY_IN_RED
          if(SUNDAY_IN_RED && day["weekdayShort"]=="Sun") display.setTextColor(GxEPD_RED);
        #endif

        display.setFont(&IOTLight48pt7b);
        //render weekday
        #ifdef HORIZ_OFFSET_PX
          x = ((display.width()-cw)/2)+HORIZ_OFFSET_PX;
        #else
          x = (display.width()-cw)/2;
        #endif
        if(ww<mw) x += mw-ww;
        display.setCursor(x, y - 72);
        display.print(day["weekdayShort"].as<String>());
        //render month
        #ifdef HORIZ_OFFSET_PX
          x = ((display.width()-cw)/2)+HORIZ_OFFSET_PX;
        #else
          x = (display.width()-cw)/2;
        #endif
        if(mw<ww) x += ww-mw;
        x += 4; //why do we need?
        display.setCursor(x, y);
        display.print(day["monthShort"].as<String>());

        display.setFont(&IOTBold108pt7b);
        #ifdef HORIZ_OFFSET_PX
          x = ((display.width()-cw)/2)+HORIZ_OFFSET_PX;
        #else
          x = (display.width()-cw)/2;
        #endif
        x += (mw>ww? mw: ww) + 30;
        display.setCursor(x, y-256); //the -256 became necessary somewhere between 72pt and 108pt
        display.print(day["date"].as<String>());

        display.setTextColor(GxEPD_BLACK);

        y += 12 + 4; //padding

        //sun/moon
        //entire line is centered; add up width of all components
        cw = 0; //center width

        display.setFont(&IOTLight16pt7b);
        //sunset, moonset (or moonfixed), and em dashes/slashes
        display.getTextBounds(day["sky"]["sunset"].as<String>(),0,0,&tbx,&tby,&tbw,&tbh); cw += tbw;
        cw += 16; //em dash with padding either side
        if(!day["sky"]["moonfixed"]) {
          display.getTextBounds(day["sky"]["moonset"].as<String>(),0,0,&tbx,&tby,&tbw,&tbh); cw += tbw;
          if(!day["sky"]["upfirst"]) {
            display.getTextBounds("/",0,0,&tbx,&tby,&tbw,&tbh); cw += tbw + 5; //3px left padding, 2px right padding
          } else {
            cw += 16; //em dash with padding either side
          }
        } else {
          display.getTextBounds(day["sky"]["moonfixed"].as<String>(),0,0,&tbx,&tby,&tbw,&tbh); cw += tbw;
        }

        display.setFont(&IOTBold16pt7b);
        //sunrise, moonrise
        display.getTextBounds(day["sky"]["sunrise"].as<String>(),0,0,&tbx,&tby,&tbw,&tbh); cw += tbw;
        if(!day["sky"]["moonfixed"]) {
          display.getTextBounds(day["sky"]["moonrise"].as<String>(),0,0,&tbx,&tby,&tbw,&tbh); cw += tbw;
        }

        cw += 22 + 19; //sun and moon icons
        cw += 10 + 30 + 10; //gaps between

        //now render, using calculated center width
        y += 16*2; //new line
        #ifdef HORIZ_OFFSET_PX
          x = ((display.width()-cw)/2)+HORIZ_OFFSET_PX;
        #else
          x = (display.width()-cw)/2;
        #endif

        display.setFont(&IOTSymbols16pt7b);
        display.setCursor(x, y);
        display.print("9"); //sun
        x += 22 + 10; //sun icon width + gap between

        display.setFont(&IOTBold16pt7b);
        display.setCursor(x, y);
        display.print(day["sky"]["sunrise"].as<String>());
        display.getTextBounds(day["sky"]["sunrise"].as<String>(),0,0,&tbx,&tby,&tbw,&tbh);
        x += tbw;

        display.setFont(&IOTLight16pt7b);
        x += 3; //em dash left padding
        display.setCursor(x, y); display.print("-");
        display.setCursor(x+4, y); display.print("-"); //poor man's em dash
        x += 13; //em dash width + right padding
        display.setCursor(x, y);
        display.print(day["sky"]["sunset"].as<String>());
        display.getTextBounds(day["sky"]["sunset"].as<String>(),0,0,&tbx,&tby,&tbw,&tbh);
        x += tbw + 30; //gap between

        display.setFont(&IOTSymbols16pt7b);
        itoa(day["sky"]["moonphase"].as<int>(),buf,10); //int to char buffer
        display.setCursor(x, y);
        display.print(buf);
        x += 19 + 10; //moon icon width + gap between

        if(day["sky"]["moonfixed"]) {
          display.setFont(&IOTLight16pt7b);
          display.setCursor(x, y);
          display.print(day["sky"]["moonfixed"].as<String>());
        } else {
          if(!day["sky"]["upfirst"]) { //moonrise then moonset (below)
            display.setFont(&IOTBold16pt7b);
            display.setCursor(x, y);
            display.print(day["sky"]["moonrise"].as<String>());
            display.getTextBounds(day["sky"]["moonrise"].as<String>(),0,0,&tbx,&tby,&tbw,&tbh);
            x += tbw;
            display.setFont(&IOTLight16pt7b);
            x += 3;
            display.setCursor(x, y); display.print("-");
            display.setCursor(x+4, y); display.print("-"); //poor man's em dash
            x += 13; //guessed em width
          }
          display.setFont(&IOTLight16pt7b);
          display.setCursor(x, y);
          display.print(day["sky"]["moonset"].as<String>());
          display.getTextBounds(day["sky"]["moonset"].as<String>(),0,0,&tbx,&tby,&tbw,&tbh);
          x += tbw;
          if(day["sky"]["upfirst"]) { //moonset (above) then moonrise
            display.setFont(&IOTLight16pt7b);
            display.setCursor(x+3, y); //3px left padding
            display.print("/");
            display.getTextBounds("/",0,0,&tbx,&tby,&tbw,&tbh);
            x += tbw + 5; //3px left padding, 2px right padding
            display.setFont(&IOTBold16pt7b);
            display.setCursor(x, y);
            display.print(day["sky"]["moonrise"].as<String>());
            display.getTextBounds(day["sky"]["moonrise"].as<String>(),0,0,&tbx,&tby,&tbw,&tbh);
            x += tbw;
          }
        }

      } else { //not today
        //render smaller date header
        y += 12; //padding

        if(day["weekdayShort"]=="Sun") display.setTextColor(GxEPD_RED);
        cw = 0;
        display.setFont(&IOTRegular21pt7b);
        display.getTextBounds(day["weekdayShort"].as<String>(),0,0,&tbx,&tby,&tbw,&tbh);
        y += 21*2; //new line
        cw += tbw + 12;
        display.getTextBounds(day["monthShort"].as<String>(),0,0,&tbx,&tby,&tbw,&tbh);
        cw += tbw + 12;
        //display.setFont(&IOTBold21pt7b);
        display.getTextBounds(day["date"].as<String>(),0,0,&tbx,&tby,&tbw,&tbh);
        cw += tbw;
        #ifdef HORIZ_OFFSET_PX
          x = ((display.width()-cw)/2)+HORIZ_OFFSET_PX;
        #else
          x = (display.width()-cw)/2;
        #endif
        //display.setFont(&IOTRegular21pt7b);
        display.setCursor(x, y);
        display.print(day["weekdayShort"].as<String>());
        display.getTextBounds(day["weekdayShort"].as<String>(),0,0,&tbx,&tby,&tbw,&tbh);
        x += tbw + 12;
        display.setCursor(x, y);
        display.print(day["monthShort"].as<String>());
        display.getTextBounds(day["monthShort"].as<String>(),0,0,&tbx,&tby,&tbw,&tbh);
        x += tbw + 12;
        //display.setFont(&IOTBold21pt7b);
        display.setCursor(x, y);
        display.print(day["date"].as<String>());
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
        display.print(w["shortForecast"].as<String>());
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
          display.getTextBounds(e["timestart"].as<String>(),0,0,&tbx,&tby,&tbw,&tbh);
          display.setCursor(x, y);
          display.print(e["timestart"].as<String>());
          x += tbw;
          display.setFont(&IOTLight16pt7b); //leave the rest non-bolded
          if(e.containsKey("timeend")) {
            x += 3;
            display.setCursor(x, y); display.print("-");
            display.setCursor(x+4, y); display.print("-"); //poor man's em dash
            x += 13; //guessed em width
            display.getTextBounds(e["timeend"].as<String>(),0,0,&tbx,&tby,&tbw,&tbh);
            display.setCursor(x, y);
            display.print(e["timeend"].as<String>());
            x += tbw;
          }
          x += 15; //gap between
        }
        display.getTextBounds(e["summary"].as<String>(),0,0,&tbx,&tby,&tbw,&tbh);
        display.setCursor(x, y);
        display.print(e["summary"].as<String>());
        x += tbw + 15;
        display.setFont(&IOTLight16pt7b); //leave the rest non-bolded
        if(e["allday"] && e["dend"]!=e["dstart"]) {
          display.getTextBounds("(thru",0,0,&tbx,&tby,&tbw,&tbh);
          display.setCursor(x, y);
          display.print("(thru");
          x += tbw + 10; //gap between
          display.getTextBounds(e["dendShort"].as<String>(),0,0,&tbx,&tby,&tbw,&tbh);
          display.setCursor(x, y);
          display.print(e["dendShort"].as<String>());
          x += tbw;
          display.print(")");
        }
      }
    } //end for each day

    //battery level
    y += 12; //padding
    x = 10; //left padding
    y += 16*2; //new line
    display.setFont(&IOTLight16pt7b);
    display.setCursor(x, y);
    display.print("Battery Voltage");
    display.getTextBounds("Battery Voltage",0,0,&tbx,&tby,&tbw,&tbh);
    x += tbw + 10; //gap between
    display.setFont(&IOTLight16pt7b);
    display.setCursor(x, y);
    //itoa(battLevel,buf,10); //int to char buffer
    display.print(battLevel);
    //display.getTextBounds(buf,0,0,&tbx,&tby,&tbw,&tbh);
    //x += tbw + 5; //gap between
    //display.print("mV");

    //displayBatteryBar();

  } while (display.nextPage());
  //display.hibernate();
  display.powerOff();

  //loop() will sleep

} //end setup

void displayError(String msg) {
  // display on e-ink
  
  //vars for calculating text bounds and setting draw origin
  int16_t tbx, tby; uint16_t tbw, tbh, cw; uint16_t x, y;
  
  display.setFullWindow();
  display.firstPage();
  do
  {
    display.fillScreen(GxEPD_WHITE);
  
    display.setTextColor(GxEPD_BLACK);
    display.setFont(&IOTLight16pt7b);
    display.getTextBounds(msg, 0, 0, &tbx, &tby, &tbw, &tbh);
    #ifdef HORIZ_OFFSET_PX
      display.setCursor(((display.width() - tbw) / 2) - tbx + HORIZ_OFFSET_PX, ((display.height() - tbh) / 2) - tby);
    #else
      display.setCursor(((display.width() - tbw) / 2) - tbx, ((display.height() - tbh) / 2) - tby);
    #endif
    display.print(msg);

    //displayBatteryBar();

  } while (display.nextPage());
  display.hibernate();
}
void displayClear() {
  displayError("");
}

void displayBatteryBar() {
  //as part of regular display cycle
  //vars for calculating text bounds and setting draw origin
  int16_t tbx, tby; uint16_t tbw, tbh, cw; uint16_t x, y;
  display.setFont(&IOTBold16pt7b);
  if(battLevel<3550) {
    display.fillRect(0, display.height()-48, display.width(), 48, (battLevel<200?GxEPD_BLACK:GxEPD_RED));
    display.getTextBounds((battLevel<200?"No battery":"Low battery"),0,0,&tbx,&tby,&tbw,&tbh);
    cw = tbw;
    y += display.height()-16;
    #ifdef HORIZ_OFFSET_PX
      x = ((display.width()-cw)/2)+HORIZ_OFFSET_PX;
    #else
      x = (display.width()-cw)/2;
    #endif
    display.setTextColor(GxEPD_WHITE);
    display.setCursor(x, y);
    display.print((battLevel<200?"No battery":"Low battery"));
  }
}

void loop() {
  //All the magic happens in setup()
  //Once that's done (whether successful or not), go to sleep

  // If battery is too low (see getBattery code), enter deepSleep and do not wake up
  if(battLevel == 0) {
    esp_deep_sleep_start();
  }

  if(todInSec>43200) { //we're early: it's PM - example: (86400*2)-80852 = 91948
    esp_sleep_enable_timer_wakeup((86400*2 - todInSec) * 1000000ULL);  
  } else { //we're late: it's AM - example: 86400-3600 = 82800
    esp_sleep_enable_timer_wakeup((86400 - todInSec) * 1000000ULL);  
  }
  //TODO maybe consider letting the webserver tell us how long to sleep
  
  Serial.flush(); 
  esp_deep_sleep_start();
}

float readBatteryVoltage() {
  return analogRead(35) / 4096.0 * 7.445;
}