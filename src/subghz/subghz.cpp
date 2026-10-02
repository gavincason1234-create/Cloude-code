#include "subghz.h"
#include "../../include/config.h"
#include <FS.h>
#include <SD.h>
#include <SPIFFS.h>

SubGhz::SubGhz(CC1101& radio) : _radio(radio) {}

bool SubGhz::begin() {
    _radioOk = _radio.begin();
    if (!_radioOk) {
        Serial.println("[SubGHz] CC1101 not found — check wiring");
    } else {
        Serial.println("[SubGHz] CC1101 OK");
    }
    return _radioOk;
}

// ─── Capture ─────────────────────────────────────────────────────────────────

bool SubGhz::captureRaw(SubGhzRawSignal& sig, uint32_t freq_hz,
                        CC1101Preset preset, uint32_t timeout_ms) {
    if (!_radioOk) return false;

    sig.freq_hz = freq_hz;
    sig.preset  = preset;
    sig.count   = 0;
    snprintf(sig.name, sizeof(sig.name), "capture_%lu", millis());

    _radio.idle();
    _radio.setFrequency(freq_hz);
    _radio.applyPreset(preset);
    _radio.startRX();

    _state = SubGhzState::CAPTURING;

    uint8_t gdo0 = _radio.gdo0Pin();
    unsigned long deadline = millis() + timeout_ms;

    // Wait for first edge (signal start)
    while (digitalRead(gdo0) == LOW && millis() < deadline);
    if (millis() >= deadline) {
        _radio.idle();
        _state = SubGhzState::IDLE;
        return false;
    }

    uint32_t t = micros();
    uint8_t  last = 1;
    uint32_t silence_us = 0;
    const uint32_t MAX_SILENCE_US = 15000; // 15 ms gap = end of burst

    while (sig.count < SUBGHZ_MAX_RAW && millis() < deadline) {
        uint8_t cur = digitalRead(gdo0);

        if (cur != last) {
            uint32_t now = micros();
            int32_t  dur = (int32_t)(now - t);
            if (dur > 50) { // ignore glitches < 50 µs
                sig.timings[sig.count++] = last ? dur : -dur;
            }
            t = now;
            last = cur;
            silence_us = 0;
        } else if (cur == 0) {
            silence_us = (uint32_t)(micros() - t);
            if (silence_us >= MAX_SILENCE_US && sig.count > 4) break;
        }

        if (sig.count == 0 && millis() >= deadline) break;
    }

    _radio.idle();
    _state = (sig.count > 4) ? SubGhzState::CAPTURED : SubGhzState::IDLE;
    Serial.printf("[SubGHz] Captured %d samples @ %lu Hz\n", sig.count, freq_hz);
    return sig.count > 4;
}

void SubGhz::stopCapture() {
    _radio.idle();
    _state = SubGhzState::IDLE;
}

// ─── Transmit ─────────────────────────────────────────────────────────────────

void SubGhz::transmitRaw(const SubGhzRawSignal& sig) {
    if (!_radioOk || sig.count == 0) return;

    _radio.idle();
    _radio.setFrequency(sig.freq_hz);
    _radio.applyPreset(sig.preset);

    uint8_t gdo0 = _radio.gdo0Pin();
    // Switch GDO0 to output (async TX mode uses GDO0 as serial data in)
    pinMode(gdo0, OUTPUT);

    _radio.startTX();
    _state = SubGhzState::TRANSMITTING;

    for (int i = 0; i < sig.count; i++) {
        int32_t t = sig.timings[i];
        if (t > 0) {
            digitalWrite(gdo0, HIGH);
            delayMicroseconds((uint32_t)t);
        } else {
            digitalWrite(gdo0, LOW);
            delayMicroseconds((uint32_t)(-t));
        }
    }

    digitalWrite(gdo0, LOW);
    _radio.idle();
    pinMode(gdo0, INPUT);
    _state = SubGhzState::IDLE;
    Serial.printf("[SubGHz] TX done: %d samples\n", sig.count);
}

// ─── Band scanner ─────────────────────────────────────────────────────────────

static const uint32_t SCAN_FREQS[] = {
    300000000UL, 315000000UL, 318000000UL,
    390000000UL, 418000000UL, 433920000UL,
    438000000UL, 868350000UL, 915000000UL,
};

void SubGhz::scanBands() {
    if (!_radioOk) return;
    Serial.println("[SubGHz] === Band Scan ===");
    for (uint32_t f : SCAN_FREQS) {
        _radio.idle();
        _radio.setFrequency(f);
        _radio.applyPreset(CC1101Preset::OOK_650_ASYNC);
        _radio.startRX();
        delay(50);
        int8_t rssi = _radio.readRSSI();
        Serial.printf("  %9lu Hz  RSSI: %d dBm %s\n",
                      f, rssi, (rssi > -90) ? "<<" : "");
    }
    _radio.idle();
    Serial.println("[SubGHz] === End Scan ===");
}

// ─── .sub file I/O ───────────────────────────────────────────────────────────

const char* SubGhz::_presetName(CC1101Preset p) {
    switch (p) {
        case CC1101Preset::OOK_650_ASYNC:    return "FuriHalSubGhzPresetOok650Async";
        case CC1101Preset::OOK_270_ASYNC:    return "FuriHalSubGhzPresetOok270Async";
        case CC1101Preset::FSK_2DEV238_ASYNC: return "FuriHalSubGhzPreset2FSKDev238Async";
        case CC1101Preset::FSK_2DEV476_ASYNC: return "FuriHalSubGhzPreset2FSKDev476Async";
        default:                              return "FuriHalSubGhzPresetOok650Async";
    }
}

bool SubGhz::_parsePreset(const char* s, CC1101Preset& out) {
    if (strstr(s, "Ook650"))    { out = CC1101Preset::OOK_650_ASYNC;     return true; }
    if (strstr(s, "Ook270"))    { out = CC1101Preset::OOK_270_ASYNC;     return true; }
    if (strstr(s, "2FSKDev238")) { out = CC1101Preset::FSK_2DEV238_ASYNC; return true; }
    if (strstr(s, "2FSKDev476")) { out = CC1101Preset::FSK_2DEV476_ASYNC; return true; }
    out = CC1101Preset::OOK_650_ASYNC;
    return false;
}

bool SubGhz::saveSignal(const SubGhzRawSignal& sig, const char* path) {
#if STORAGE_USE_SD
    if (!SD.begin(SD_CS)) {
        Serial.println("[SubGHz] SD init failed");
        return false;
    }
    File f = SD.open(path, FILE_WRITE);
#else
    SPIFFS.begin(true);
    File f = SPIFFS.open(path, FILE_WRITE);
#endif
    if (!f) {
        Serial.printf("[SubGHz] Cannot open %s for write\n", path);
        return false;
    }

    f.printf("Filetype: Flipper SubGhz RAW File\n");
    f.printf("Version: 1\n");
    f.printf("Frequency: %lu\n", sig.freq_hz);
    f.printf("Preset: %s\n", _presetName(sig.preset));
    f.printf("Protocol: RAW\n");
    f.print("RAW_Data:");
    for (int i = 0; i < sig.count; i++) {
        f.printf(" %ld", (long)sig.timings[i]);
    }
    f.println();
    f.close();
    Serial.printf("[SubGHz] Saved %d samples to %s\n", sig.count, path);
    return true;
}

bool SubGhz::loadSignal(SubGhzRawSignal& sig, const char* path) {
#if STORAGE_USE_SD
    if (!SD.begin(SD_CS)) return false;
    File f = SD.open(path, FILE_READ);
#else
    SPIFFS.begin(true);
    File f = SPIFFS.open(path, FILE_READ);
#endif
    if (!f) {
        Serial.printf("[SubGHz] Cannot open %s\n", path);
        return false;
    }

    sig.count = 0;
    char line[256];
    while (f.available()) {
        int len = f.readBytesUntil('\n', line, sizeof(line) - 1);
        line[len] = '\0';

        if (strncmp(line, "Frequency:", 10) == 0)
            sig.freq_hz = strtoul(line + 11, nullptr, 10);
        else if (strncmp(line, "Preset:", 7) == 0)
            _parsePreset(line + 8, sig.preset);
        else if (strncmp(line, "RAW_Data:", 9) == 0) {
            char* tok = strtok(line + 10, " \r\n");
            while (tok && sig.count < SUBGHZ_MAX_RAW) {
                sig.timings[sig.count++] = (int32_t)strtol(tok, nullptr, 10);
                tok = strtok(nullptr, " \r\n");
            }
        }
    }
    f.close();

    strncpy(sig.name, path, sizeof(sig.name));
    Serial.printf("[SubGHz] Loaded %d samples from %s\n", sig.count, path);
    return sig.count > 0;
}
