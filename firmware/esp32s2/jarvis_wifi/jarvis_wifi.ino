/*
 * JARVIS WiFi — ESP32-S2 Firmware
 * For use with the Flipper Zero Wi-Fi Developer Board (ESP32-S2)
 *
 * Board: "Adafruit Feather ESP32-S2" or "ESP32-S2 Dev Module"
 * Flash:  4MB, SPIFFS partition scheme
 * Upload speed: 921600
 *
 * Required libraries (via Arduino Library Manager):
 *   - ArduinoJson (Benoit Blanchon)
 *   - WebServer (built-in ESP32 Arduino core)
 */

#include "CommandHandler.h"

void setup() {
    handler.begin();
}

void loop() {
    handler.loop();
}
