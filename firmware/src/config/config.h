#pragma once

#include <Arduino.h>

// Display configuration from build flags (platformio.ini)
#ifndef DISPLAY_WIDTH
#define DISPLAY_WIDTH 320
#endif
#ifndef DISPLAY_HEIGHT
#define DISPLAY_HEIGHT 240
#endif
#ifndef DISPLAY_ROTATION
#define DISPLAY_ROTATION 1
#endif

// Pin defaults for ESP32-S3-DevKitC-1 + ST7789 320x240
#ifndef PIN_LCD_MOSI
#define PIN_LCD_MOSI 11
#endif
#ifndef PIN_LCD_SCK
#define PIN_LCD_SCK 12
#endif
#ifndef PIN_LCD_CS
#define PIN_LCD_CS 10
#endif
#ifndef PIN_LCD_DC
#define PIN_LCD_DC 13
#endif
#ifndef PIN_LCD_RST
#define PIN_LCD_RST 14
#endif
#ifndef PIN_LCD_BL
#define PIN_LCD_BL 3
#endif

#ifndef BLE_DEVICE_NAME
#define BLE_DEVICE_NAME "CodexPet"
#endif

namespace Config {
    constexpr int DisplayWidth = DISPLAY_WIDTH;
    constexpr int DisplayHeight = DISPLAY_HEIGHT;
    constexpr int DisplayRotation = DISPLAY_ROTATION;
    constexpr const char* BleDeviceName = BLE_DEVICE_NAME;
}
