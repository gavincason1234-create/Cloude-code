#pragma once
#include <Arduino.h>

// BLE tools are only compiled when CONFIG_BT_ENABLED=1 (ESP32-S3 / original ESP32).
// On ESP32-S2, these functions are no-ops so the rest of the firmware compiles clean.

enum class BleSpamType {
    APPLE_PROXIMITY,    // triggers "Connect to Apple device" popup on iPhones
    SAMSUNG_FAST_PAIR,  // Samsung Fast Pair notification spam
    WINDOWS_SWIFT_PAIR, // Windows Swift Pair popup
    GENERIC_ADV,        // generic BLE advertisement flood
};

class BleTools {
public:
    bool  begin();

    // Spam BLE advertisements of the given type; runs until stop() is called
    void  startSpam(BleSpamType type);
    void  stop();
    bool  isRunning() const { return _running; }

    // BLE device scanner — prints found devices to Serial
    void  scanDevices(uint32_t duration_ms = 5000);

    // Spoof as a specific device name
    void  advertiseAs(const char* name);

private:
    bool _running  = false;
    bool _bleReady = false;
};
