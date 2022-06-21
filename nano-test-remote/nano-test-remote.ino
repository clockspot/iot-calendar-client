//Working sketch for Arduino Nano 33 IoT to pull data from iot-calendar-server
//(done while waiting for my ESP32 to turn up in the mail!)

//sources:
//wifinina: https://github.com/clockspot/arduino-ledclock
//json: https://arduinojson.org/v6/example/http-client/

#include <SPI.h>
#include <WiFiNINA.h>
#include <ArduinoJson.h> //bblanchon

#include <GxEPD2_3C.h>
#include <Fonts/IOTLight14pt7b.h>
#include <Fonts/IOTBold14pt7b.h>
#include <Fonts/IOTRegular21pt7b.h>
#include <Fonts/IOTLight36pt7b.h>
#include <Fonts/IOTBold72pt7b.h>
#include "GxEPD2_display_selection_new_style.h"

#include "config.h"

WiFiSSLClient sslClient;

void setup() {
  //Serial.begin(9600);
  //while(!Serial); //only works on 33 IoT
  delay(1000);

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
    return;
  }

  //Connect to data host
  Serial.print(F("\nConnecting to data host "));
  Serial.println(DATA_HOST);
  sslClient.setTimeout(15000);
  if(!sslClient.connect(DATA_HOST, 443)) {
    Serial.println(F("Wasn't able to connect to host."));
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
    sslClient.stop();
    return;
  }

  // Check HTTP status
  char status[32] = {0};
  sslClient.readBytesUntil('\r', status, sizeof(status));
  if (strcmp(status, "HTTP/1.1 200 OK") != 0) {
    Serial.print(F("Unexpected response: "));
    Serial.println(status);
    sslClient.stop();
    return;
  }

  // Skip HTTP headers
  char endOfHeaders[] = "\r\n\r\n";
  if (!sslClient.find(endOfHeaders)) {
    Serial.println(F("Invalid response"));
    sslClient.stop();
    return;
  }

  // Allocate the JSON document
  // Use arduinojson.org/v6/assistant to compute the capacity.
  DynamicJsonDocument doc(4096);
  
  // Parse JSON object
  DeserializationError error = deserializeJson(doc, sslClient);
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    sslClient.stop();
    return;
  }

  // Disconnect
  sslClient.stop();

  // // display on serial
  // // Extract values - expecting an array of days (or empty array on server error)
  // // cf. "Pretend display" in iot-calendar-server/docroot/index.php
  // Serial.println();
  // for (JsonObject day : doc.as<JsonArray>()) {
  //   //header
  //   Serial.print(day["weekdayRelative"].as<char*>());
  //   if(day["weekdayRelative"]=="Today") {
  //     Serial.print(F(" - "));
  //     Serial.print(day["weekdayShort"].as<char*>());
  //     Serial.print(F(" "));
  //     Serial.print(day["date"].as<char*>());
  //     Serial.print(F(" "));
  //     Serial.print(day["monthShort"].as<char*>());
  //     Serial.println();
  //     Serial.print(F("Sunrise "));
  //     Serial.print(day["sun"]["sunrise"].as<char*>());
  //     Serial.print(F("  Sunset "));
  //     Serial.print(day["sun"]["sunset"].as<char*>());
  //   }
  //   Serial.println();
  //   //weather
  //   for (JsonObject w : day["weather"].as<JsonArray>()) {
  //     if(w["isDaytime"]) Serial.print(F("High "));
  //     else Serial.print(F("Low "));
  //     Serial.print(w["temperature"].as<int>());
  //     Serial.print(F("º "));
  //     Serial.print(w["shortForecast"].as<char*>());
  //     Serial.println();
  //   }
  //   // events
  //   for (JsonObject e : day["events"].as<JsonArray>()) {
  //     if(e["style"]=="red") Serial.print(F(" ▪ "));
  //     else Serial.print(F(" • "));
  //     if(!e["allday"]) { Serial.print(e["timestart"].as<char*>()); Serial.print(F(" ")); }
  //     Serial.print(e["summary"].as<char*>());
  //     if(e["allday"] && e["dend"]!=e["dstart"]) { Serial.print(F(" (thru ")); Serial.print(e["dendShort"].as<char*>()); Serial.print(F(")")); }
  //     Serial.println();
  //   }
  //   //end of day
  //   Serial.println();
  // }
  // Serial.println(F("Done!"));

  // display on e-ink
  display.init(115200);
  display.setRotation(1);
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
        //the date should be centered, and day/month rendered to its sides
        //date:  x = (display width - date width)/2
        //day:   x = date x - day width - 15
        //month: x = date x + date width + 15
        display.setFont(&IOTBold72pt7b);
        display.getTextBounds(day["date"].as<char*>(),0,0,&tbx,&tby,&cw,&tbh); //sets cw
        y += tbh + 21; //new line
        x = (display.width()-cw)/2;
        display.setCursor(x, y);
        display.print(day["date"].as<char*>());

        display.setFont(&IOTLight36pt7b);
        //render weekday
        display.getTextBounds(day["weekdayShort"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh); //sets tbw
        display.setCursor(x-tbw-20, y);
        display.print(day["weekdayShort"].as<char*>());
        //render month
        display.setCursor(x+cw+20, y);
        display.print(day["monthShort"].as<char*>());

        //render sunrise and sunset
        //entire line is centered; add up width of all components
        cw = 0;
        display.setFont(&IOTLight14pt7b);
        display.getTextBounds("SunriseSunset",0,0,&tbx,&tby,&tbw,&tbh);
        y += tbh + 36; //new line
        cw += tbw + 10 + 10; //gaps between
        display.setFont(&IOTBold14pt7b);
        display.getTextBounds(day["sun"]["sunrise"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        cw += tbw;
        display.getTextBounds(day["sun"]["sunset"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        cw += tbw + 20; //gap between
        //now render, using calculated center width
        x = (display.width()-cw)/2;
        display.setFont(&IOTLight14pt7b);
        display.getTextBounds("Sunrise",0,0,&tbx,&tby,&tbw,&tbh);
        display.setCursor(x, y);
        display.print("Sunrise");
        x += tbw + 10;
        display.setFont(&IOTBold14pt7b);
        display.getTextBounds(day["sun"]["sunrise"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        display.setCursor(x, y);
        display.print(day["sun"]["sunrise"].as<char*>());
        x += tbw + 20;
        display.setFont(&IOTLight14pt7b);
        display.getTextBounds("Sunset",0,0,&tbx,&tby,&tbw,&tbh);
        display.setCursor(x, y);
        display.print("Sunset");
        x += tbw + 10;
        display.setFont(&IOTBold14pt7b);
        display.setCursor(x, y);
        display.print(day["sun"]["sunset"].as<char*>());
      } else {
        //render relative date, centered
        y += 24; //padding
        display.setFont(&IOTRegular21pt7b);
        display.getTextBounds(day["weekdayRelative"].as<char*>(),0,0,&tbx,&tby,&cw,&tbh); //sets cw
        y += tbh + 14; //new line
        x = (display.width()-cw)/2;
        display.setCursor(x, y);
        display.print(day["weekdayRelative"].as<char*>());
      }
      y += 12; //padding
      //render weather
      for (JsonObject w : day["weather"].as<JsonArray>()) {
        x = 10; //left padding
        display.setFont(&IOTBold14pt7b);
        if(w["isDaytime"]) {
          display.getTextBounds("High",0,0,&tbx,&tby,&tbw,&tbh);
          y += tbh + 14; //new line
          display.setCursor(x, y);
          display.print("High");
          x += tbw + 10; //gap between
        } else {
          display.getTextBounds("Low",0,0,&tbx,&tby,&tbw,&tbh);
          y += tbh + 14; //new line
          display.setCursor(x, y);
          display.print("Low");
          x += tbw + 10; //gap between
        }
        itoa(w["temperature"].as<int>(),buf,10); //int to char buffer
        display.getTextBounds(buf,0,0,&tbx,&tby,&tbw,&tbh);
        display.setCursor(x, y);
        display.print(buf);
        x += tbw;
        display.setCursor(x, y);
        //display.print("º");
        x += 15; //gap between
        display.setFont(&IOTLight14pt7b);
        display.setCursor(x, y);
        display.print(w["shortForecast"].as<char*>());
      }
      y += 12; //padding
      //render events
      for (JsonObject e : day["events"].as<JsonArray>()) {
        x = 20; //left padding
        if(e["style"]=="red") display.setTextColor(GxEPD_RED);
        else display.setTextColor(GxEPD_BLACK);
        display.setFont(&IOTBold14pt7b);
        display.getTextBounds("X",0,0,&tbx,&tby,&tbw,&tbh);
        y += tbh + 14; //new line
        display.setCursor(x, y);
        display.print("-");
        x += 15; //gap between
        if(!e["allday"]) {
          display.getTextBounds(e["timestart"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
          display.setCursor(x, y);
          display.print(e["timestart"].as<char*>());
          x += tbw + 15; //gap between
          display.setFont(&IOTLight14pt7b); //leave the rest non-bolded
        }
        display.getTextBounds(e["summary"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        display.setCursor(x, y);
        display.print(e["summary"].as<char*>());
        x += tbw + 15;
        display.setFont(&IOTLight14pt7b); //leave the rest non-bolded
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

void loop() {
  // To inspect the HTTP response directly, uncomment this code, and in setup(), comment out "Check HTTP Status" and everything afterward

  // // if there are incoming bytes available
  // // from the server, read them and print them:
  // while (sslClient.available()) {
  //   char c = sslClient.read();
  //   Serial.write(c);
  // }

  // // if the server's disconnected, stop the client:
  // if (!sslClient.connected()) {
  //   Serial.println();
  //   Serial.println("disconnecting from server.");
  //   sslClient.stop();

  //   // do nothing forevermore:
  //   while (true);
  // }
}