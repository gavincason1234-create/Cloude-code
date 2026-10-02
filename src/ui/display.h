#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "../../include/config.h"

class Display {
public:
    bool begin();

    void clear();
    void show();   // push buffer to screen

    // Text helpers
    void print(int x, int y, const char* text,
               uint8_t size = 1, bool invert = false);
    void printCenter(int y, const char* text, uint8_t size = 1);

    // Status bar (top 8 px)
    void drawStatusBar(const char* left, const char* right);

    // Horizontal separator line
    void drawHLine(int y);

    // Fill a rectangle
    void fillRect(int x, int y, int w, int h);

    // Get raw Adafruit display for custom drawing
    Adafruit_SSD1306& raw() { return _dsp; }

    int width()  const { return OLED_WIDTH;  }
    int height() const { return OLED_HEIGHT; }
    bool ok()    const { return _ok; }

private:
    Adafruit_SSD1306 _dsp{OLED_WIDTH, OLED_HEIGHT, &Wire, -1};
    bool             _ok = false;
};
