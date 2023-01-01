# iot-calendar-client

Arduino/ESP32 software for an e-ink calendar, intended for use with [iot-calendar-server](https://github.com/clockspot/iot-calendar-server).

<img src="https://user-images.githubusercontent.com/9385318/210183103-a624e4f4-9a1b-46c9-8aa5-d11bb0678466.jpg">

Forked from [kristiantm/eink-family-calendar-esp32](https://github.com/kristiantm/eink-family-calendar-esp32), which details the hardware specs and required libraries for this project.

Sketch for LOLIN D32 is in `/lolin32`.

Earlier test sketches written for Arduino Nano 33 IoT are in `/nano-test-local` (processes locally) and `/nano-test-remote` (relies on [iot-calendar-server](https://github.com/clockspot/iot-calendar-server)).

Once you have installed the Adafruit GFX Library, you will need to copy the IOT Symbols font from `/resources` into the library's Fonts folder (on macOS, `~/Documents/Arduino/libraries/Adafruit_GFX_Library/Fonts`). This draws the sun and moon icons. For the text fonts, you will need to edit the sketch to use the GFX library's [standard fonts](https://learn.adafruit.com/adafruit-gfx-graphics-library/using-fonts), or [ones of your own making](https://learn.adafruit.com/adafruit-gfx-graphics-library/using-fonts#adding-new-fonts-2002831). (The font I use, the wonderful [Metric](https://klim.co.nz/retail-fonts/metric/) from Klim Type Foundry, I am not licensed to share.)