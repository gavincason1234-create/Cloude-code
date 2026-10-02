#include "display.h"

bool Display::begin() {
    Wire.begin(OLED_SDA, OLED_SCL);
    _ok = _dsp.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
    if (!_ok) {
        Serial.println("[Display] SSD1306 not found — check I2C wiring");
        return false;
    }
    _dsp.setTextColor(SSD1306_WHITE);
    _dsp.clearDisplay();
    _dsp.display();
    Serial.println("[Display] SSD1306 OK");
    return true;
}

void Display::clear() {
    _dsp.clearDisplay();
}

void Display::show() {
    _dsp.display();
}

void Display::print(int x, int y, const char* text, uint8_t size, bool invert) {
    _dsp.setTextSize(size);
    _dsp.setTextColor(invert ? SSD1306_BLACK : SSD1306_WHITE);
    _dsp.setCursor(x, y);
    _dsp.print(text);
    _dsp.setTextColor(SSD1306_WHITE);
}

void Display::printCenter(int y, const char* text, uint8_t size) {
    int16_t x1, y1;
    uint16_t w, h;
    _dsp.setTextSize(size);
    _dsp.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
    int cx = (OLED_WIDTH - (int)w) / 2;
    print(cx < 0 ? 0 : cx, y, text, size);
}

void Display::drawStatusBar(const char* left, const char* right) {
    _dsp.fillRect(0, 0, OLED_WIDTH, 9, SSD1306_WHITE);
    _dsp.setTextColor(SSD1306_BLACK);
    _dsp.setTextSize(1);
    _dsp.setCursor(2, 1);
    _dsp.print(left);

    // Right-align the right string
    int16_t x1, y1; uint16_t w, h;
    _dsp.getTextBounds(right, 0, 0, &x1, &y1, &w, &h);
    _dsp.setCursor(OLED_WIDTH - w - 2, 1);
    _dsp.print(right);

    _dsp.setTextColor(SSD1306_WHITE);
}

void Display::drawHLine(int y) {
    _dsp.drawFastHLine(0, y, OLED_WIDTH, SSD1306_WHITE);
}

void Display::fillRect(int x, int y, int w, int h) {
    _dsp.fillRect(x, y, w, h, SSD1306_WHITE);
}
