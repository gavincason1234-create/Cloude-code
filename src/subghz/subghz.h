#pragma once
#include <Arduino.h>
#include "cc1101.h"

#define SUBGHZ_MAX_RAW   4096

enum class SubGhzState {
    IDLE,
    CAPTURING,
    CAPTURED,
    TRANSMITTING,
};

struct SubGhzRawSignal {
    uint32_t freq_hz;
    CC1101Preset preset;
    int32_t  timings[SUBGHZ_MAX_RAW]; // µs, positive=high, negative=low
    int      count;
    char     name[32];
};

class SubGhz {
public:
    SubGhz(CC1101& radio);

    bool  begin();
    bool  radioOk() const { return _radioOk; }

    // Capture one raw burst; blocks until silence or timeout_ms
    bool  captureRaw(SubGhzRawSignal& sig, uint32_t freq_hz,
                     CC1101Preset preset = CC1101Preset::OOK_650_ASYNC,
                     uint32_t timeout_ms = 5000);

    // Replay a previously captured signal
    void  transmitRaw(const SubGhzRawSignal& sig);

    // Frequency scanner: prints to Serial what it hears on each band
    void  scanBands();

    int8_t  readRSSI() { return _radio.readRSSI(); }

    // .sub file format (Flipper-compatible)
    bool  saveSignal(const SubGhzRawSignal& sig, const char* path);
    bool  loadSignal(SubGhzRawSignal& sig, const char* path);

    SubGhzState state() const { return _state; }
    void        stopCapture();

private:
    CC1101&    _radio;
    bool       _radioOk = false;
    SubGhzState _state  = SubGhzState::IDLE;

    const char* _presetName(CC1101Preset p);
    bool        _parsePreset(const char* s, CC1101Preset& out);
};
