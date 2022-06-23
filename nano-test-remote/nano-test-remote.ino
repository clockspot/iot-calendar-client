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
#include "GxEPD2_display_selection_new_style.h"

#include "config.h"

WiFiSSLClient sslClient;

void setup() {
  //Serial.begin(9600);
  //while(!Serial); //only works on 33 IoT
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
  //     Serial.print(day["sky"]["sunrise"].as<char*>());
  //     Serial.print(F("  Sunset "));
  //     Serial.print(day["sky"]["sunset"].as<char*>());
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

        //aproach A:
        // //the date should be centered, and day/month rendered to its sides
        // //date:  x = (display width - date width)/2
        // //day:   x = date x - day width - 15
        // //month: x = date x + date width + 15
        // display.setFont(&IOTBold72pt7b);
        // display.getTextBounds(day["date"].as<char*>(),0,0,&tbx,&tby,&cw,&tbh); //sets cw
        // y += tbh + 21*2; //new line
        // x = (display.width()-cw)/2;
        // display.setCursor(x, y);
        // display.print(day["date"].as<char*>());

        // display.setFont(&IOTLight36pt7b);
        // //render weekday
        // display.getTextBounds(day["weekdayShort"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh); //sets tbw
        // display.setCursor(x-tbw-20, y);
        // display.print(day["weekdayShort"].as<char*>());
        // //render month
        // display.setCursor(x+cw+20, y);
        // display.print(day["monthShort"].as<char*>());

        //approach B:
        //date is left-aligned in right half; day/month right-aligned in left half (ish)
        //date:  x = (display width/2) + 0, y = n
        //day:   x = (display width/2) - 30 - day width, y = n + day height + 12(?)
        //month: x = (display width/2) - 30 - month width, y = n
        display.setFont(&IOTBold108pt7b);
        if(day["weekdayShort"]=="Sun") display.setTextColor(GxEPD_RED);
        display.getTextBounds(day["date"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        // Serial.println(tbw,DEC);
        // Serial.println(tbh,DEC);
        y += 155; //new line - these used to be based on tbh, which is why they're here, after the first getTextBounds of the line
        x = (display.width()/2) + 0;
        // Serial.println(x,DEC);
        // Serial.println(y,DEC);
        display.setCursor(x, y-256); //the -256 became necessary somewhere between 72pt and 108pt
        display.print(day["date"].as<char*>());

        display.setFont(&IOTLight48pt7b);
        //render weekday
        display.getTextBounds(day["weekdayShort"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        x = (display.width()/2) - 30 - tbw;
        // Serial.println(F("now"));
        // Serial.println(x,DEC);
        // Serial.println(y,DEC);
        display.setCursor(x, y - 72);
        display.print(day["weekdayShort"].as<char*>());
        //render month
        display.getTextBounds(day["monthShort"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        x = (display.width()/2) - 30 - tbw;
        display.setCursor(x, y);
        display.print(day["monthShort"].as<char*>());

        display.setTextColor(GxEPD_BLACK);

        y += 12 + 4; //padding
        //render sunrise and sunset
        //entire line is centered; add up width of all components
        cw = 0; //center width
        display.setFont(&IOTLight16pt7b);
        display.getTextBounds("SunriseSunset",0,0,&tbx,&tby,&tbw,&tbh);
        cw += tbw + 10 + 10; //gaps between
        //uncomment if you want dawn/dusk (1 of 3)
        // display.getTextBounds(day["sky"]["dawn"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        // cw += tbw + 13; //poor man's em dash width
        // display.getTextBounds(day["sky"]["dusk"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        // cw += tbw + 13; //poor man's em dash width
        display.setFont(&IOTBold16pt7b);
        display.getTextBounds(day["sky"]["sunrise"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        cw += tbw;
        display.getTextBounds(day["sky"]["sunset"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        cw += tbw + 20; //gap between
        //now render, using calculated center width
        y += 16*2; //new line
        x = (display.width()-cw)/2;
        display.setFont(&IOTLight16pt7b);
        display.setCursor(x, y);
        display.print("Sunrise");
        display.getTextBounds("Sunrise",0,0,&tbx,&tby,&tbw,&tbh);
        x += tbw + 10;
        //uncomment if you want dawn/dusk (2 of 3)
        // display.setCursor(x, y); 
        // display.print(day["sky"]["dawn"].as<char*>());
        // display.getTextBounds(day["sky"]["dawn"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        // x += tbw;
        // display.setCursor(x, y); display.print("-");
        // display.setCursor(x+4, y); display.print("-"); //poor man's em dash
        // x += 13; //guessed em width
        display.setFont(&IOTBold16pt7b);
        display.setCursor(x, y);
        display.print(day["sky"]["sunrise"].as<char*>());
        display.getTextBounds(day["sky"]["sunrise"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        x += tbw;
        x += 20; //gap between halves
        display.setFont(&IOTLight16pt7b);
        display.setCursor(x, y);
        display.print("Sunset");
        display.getTextBounds("Sunset",0,0,&tbx,&tby,&tbw,&tbh);
        x += tbw + 10;
        display.setFont(&IOTBold16pt7b);
        display.setCursor(x, y);
        display.print(day["sky"]["sunset"].as<char*>());
        //uncomment if you want dawn/dusk (3 of 3)
        // display.getTextBounds(day["sky"]["sunset"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        // x += tbw;
        // display.setFont(&IOTLight16pt7b);
        // display.setCursor(x, y); display.print("-");
        // display.setCursor(x+4, y); display.print("-"); //poor man's em dash
        // x += 13; //guessed em width
        // display.setCursor(x, y);
        // display.print(day["sky"]["dusk"].as<char*>());

        //render moonrise and moonset
        //entire line is centered; add up width of all components
        cw = 0; //center width
        if(day["sky"]["moonfixed"]) {
          display.setFont(&IOTLight16pt7b);
          display.getTextBounds("Moon",0,0,&tbx,&tby,&tbw,&tbh);
          cw += tbw + 10; //gap between
          display.setFont(&IOTBold16pt7b);
          display.getTextBounds(day["sky"]["moonfixed"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
          cw += tbw + 20; //gap after
        } else {
          display.setFont(&IOTLight16pt7b);
          display.getTextBounds("MoonriseMoonset",0,0,&tbx,&tby,&tbw,&tbh);
          cw += tbw + 10 + 10; //gaps between
          display.setFont(&IOTBold16pt7b);
          display.getTextBounds(day["sky"]["moonrise"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
          cw += tbw + 20; //gap after
          display.getTextBounds(day["sky"]["moonset"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
          cw += tbw + 20; //gap after
        }
        display.setFont(&IOTLight16pt7b);
        display.getTextBounds("Phase",0,0,&tbx,&tby,&tbw,&tbh);
        cw += tbw + 10; //gap between
        display.setFont(&IOTBold16pt7b);
        display.getTextBounds(day["sky"]["moonphase"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        cw += tbw;

        //now render, using calculated center width
        y += 16*2; //new line
        x = (display.width()-cw)/2;
        if(day["sky"]["moonfixed"]) {
          display.setFont(&IOTLight16pt7b);
          display.setCursor(x, y);
          display.print("Moon");
          display.getTextBounds("Moon",0,0,&tbx,&tby,&tbw,&tbh);
          x += tbw + 10;
          display.setFont(&IOTBold16pt7b);
          display.setCursor(x, y);
          display.print(day["sky"]["moonfixed"].as<char*>());
          display.getTextBounds(day["sky"]["moonfixed"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
          x += tbw + 20;
        } else {
          if(!day["sky"]["upfirst"]) { //moonrise then moonset (below)
            display.setFont(&IOTLight16pt7b);
            display.setCursor(x, y);
            display.print("Moonrise");
            display.getTextBounds("Moonrise",0,0,&tbx,&tby,&tbw,&tbh);
            x += tbw + 10;
            display.setFont(&IOTBold16pt7b);
            display.setCursor(x, y);
            display.print(day["sky"]["moonrise"].as<char*>());
            display.getTextBounds(day["sky"]["moonrise"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
            x += tbw + 20;
          }
          display.setFont(&IOTLight16pt7b);
          display.setCursor(x, y);
          display.print("Moonset");
          display.getTextBounds("Moonset",0,0,&tbx,&tby,&tbw,&tbh);
          x += tbw + 10;
          display.setFont(&IOTBold16pt7b);
          display.setCursor(x, y);
          display.print(day["sky"]["moonset"].as<char*>());
          display.getTextBounds(day["sky"]["moonset"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
          x += tbw + 20;
          if(day["sky"]["upfirst"]) { //moonset (above) then moonrise
            display.setFont(&IOTLight16pt7b);
            display.setCursor(x, y);
            display.print("Moonrise");
            display.getTextBounds("Moonrise",0,0,&tbx,&tby,&tbw,&tbh);
            x += tbw + 10;
            display.setFont(&IOTBold16pt7b);
            display.setCursor(x, y);
            display.print(day["sky"]["moonrise"].as<char*>());
            display.getTextBounds(day["sky"]["moonrise"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
            x += tbw + 20;
          }
        }
        display.setFont(&IOTLight16pt7b);
        display.setCursor(x, y);
        display.print("Phase");
        display.getTextBounds("Phase",0,0,&tbx,&tby,&tbw,&tbh);
        x += tbw + 10;
        display.setFont(&IOTBold16pt7b);
        display.setCursor(x, y);
        display.print(day["sky"]["moonphase"].as<char*>());
        
      } else {
        //render relative date, centered
        y += 12; //padding
        // cw = 0;
        // display.setFont(&IOTRegular21pt7b);
        // display.getTextBounds(day["weekdayRelative"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        // y += 21*2; //new line
        // cw += tbw + 10;
        // display.getTextBounds(day["dateShort"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        // cw += tbw;
        // x = (display.width()-cw)/2;
        // display.setCursor(x, y);
        // display.print(day["weekdayRelative"].as<char*>());
        // display.getTextBounds(day["weekdayRelative"].as<char*>(),0,0,&tbx,&tby,&tbw,&tbh);
        // x += tbw + 10;
        // display.setCursor(x, y);
        // display.print(day["dateShort"].as<char*>());

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
          display.setCursor(x, y);
          display.print("/");
          x += tbw;
          itoa(w["precipChance"].as<int>(),buf,10); //int to char buffer
          display.getTextBounds(buf,0,0,&tbx,&tby,&tbw,&tbh);
          display.setCursor(x, y);
          display.print(buf);
          x += tbw;
          display.getTextBounds("%",0,0,&tbx,&tby,&tbw,&tbh);
          display.setCursor(x, y);
          display.print("%");
          x += tbw;
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