//Working sketch for Arduino Nano 33 IoT to pull data from iot-calendar-server
//(done while waiting for my ESP32 to turn up in the mail!)

//sources:
//wifinina: https://github.com/clockspot/arduino-ledclock
//json: https://arduinojson.org/v6/example/http-client/

#include <SPI.h>
#include <WiFiNINA.h>
#include <ArduinoJson.h> //bblanchon

#include <GxEPD2_3C.h>
#include <Fonts/IOTLight16pt7b.h>
#include <Fonts/IOTBold16pt7b.h>
#include <Fonts/IOTRegular21pt7b.h>
#include <Fonts/IOTLight48pt7b.h>
#include <Fonts/IOTBold108pt7b.h>
#include <Fonts/IOTSymbols16pt7b.h>
#include "GxEPD2_display_selection_new_style.h"

#include "config.h"

WiFiSSLClient sslClient;

void setup() {
  Serial.begin(9600);
  //while(!Serial); //only works on 33 IoT - uncomment before flight
  delay(1000);

  //Clear display
  displayClear();

  //Check status of wifi module up front
  if(WiFi.status()==WL_NO_MODULE){ Serial.println(F("Communication with WiFi module failed!")); return; }
  else if(WiFi.firmwareVersion()<WIFI_FIRMWARE_LATEST_VERSION) Serial.println(F("Please upgrade the firmware"));

  //Start wifi
  Serial.print(F("\nConnecting to WiFi SSID "));
  Serial.println(NETWORK_SSID);
  WiFi.begin(NETWORK_SSID, NETWORK_PASS); //WPA - hangs while connecting
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

  //Connect to data host
  Serial.print(F("\nConnecting to data host "));
  Serial.println(DATA_HOST);
  sslClient.setTimeout(15000);
  if(!sslClient.connect(DATA_HOST, 443)) {
    Serial.println(F("Wasn't able to connect to host."));
    displayError(F("Couldn't connect to data host."));
    return;
  }

  Serial.println("Connected!");

  //Make an HTTP request
  Serial.print(F("GET "));
  Serial.print(DATA_PATH);
  Serial.println(F(" HTTP/1.1"));
  sslClient.print(F("GET "));
  sslClient.print(DATA_PATH);
  sslClient.println(F(" HTTP/1.1"));

  sslClient.print(F("Host: "));
  sslClient.println(DATA_HOST);

  sslClient.println(F("Connection: close"));

  //the rest adapted from https://arduinojson.org/v6/example/http-sslClient/
  if (sslClient.println() == 0) {
    Serial.println(F("Failed to send request"));
    displayError(F("Failed to send data request."));
    sslClient.stop();
    return;
  }

  // Check HTTP status
  char status[32] = {0};
  sslClient.readBytesUntil('\r', status, sizeof(status));
  if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
    Serial.print(F("Unexpected response: "));
    displayError(F("Data error. Please restart."));
    Serial.println(status);
    sslClient.stop();
    return;
  }

  // Skip HTTP headers
  char endOfHeaders[] = "\r\n\r\n";
  if (!sslClient.find(endOfHeaders)) {
    Serial.println(F("Invalid response"));
    displayError(F("Data error. Please restart."));
    sslClient.stop();
    return;
  }

  // Allocate the JSON document
  // Use arduinojson.org/v6/assistant to compute the capacity.
  DynamicJsonDocument doc(8192);
  
  // Parse JSON object
  DeserializationError error = deserializeJson(doc, sslClient);
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    displayError(F("Couldn't process data. Please restart."));
    sslClient.stop();
    return;
  }

  // Disconnect
  sslClient.stop();

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

  } while (display.nextPage());
  display.hibernate();
  
} //end fn setup

void displayError(String msg) {
  // display on e-ink
  display.init(115200);
  display.setRotation(1);
  display.setTextWrap(false);
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

void loop() {
  // code for inspecting the HTTP response directly by reading from sslClient - last seen in commit 12eada4
}