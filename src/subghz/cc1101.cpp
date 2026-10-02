#include "cc1101.h"
#include "../../include/config.h"

// Preset register tables (Flipper Zero compatible values)
static const CC1101RegPair PRESET_OOK650[] = {
    {CC1101_IOCFG0,    0x0D},  // GDO0: sync signal (active while in RX/TX sync)
    {CC1101_IOCFG2,    0x0D},  // GDO2: serial data
    {CC1101_MDMCFG4,   0x07},  // Chanbw 650 kHz
    {CC1101_MDMCFG3,   0x32},  // Drate ~4795 baud
    {CC1101_MDMCFG2,   0x30},  // OOK/ASK, no preamble/sync
    {CC1101_MDMCFG1,   0x23},
    {CC1101_MDMCFG0,   0xF8},
    {CC1101_AGCCTRL2,  0xDC},
    {CC1101_AGCCTRL1,  0x00},
    {CC1101_AGCCTRL0,  0x92},
    {CC1101_MCSM2,     0x07},
    {CC1101_MCSM1,     0x00},
    {CC1101_MCSM0,     0x18},
    {CC1101_FOCCFG,    0x14},
    {CC1101_BSCFG,     0x00},
    {CC1101_FREND1,    0xB6},
    {CC1101_FREND0,    0x17},
    {CC1101_PKTCTRL0,  0x32},  // async serial mode, infinite packet
};

static const CC1101RegPair PRESET_OOK270[] = {
    {CC1101_IOCFG0,    0x0D},
    {CC1101_IOCFG2,    0x0D},
    {CC1101_MDMCFG4,   0x07},  // Chanbw 270 kHz
    {CC1101_MDMCFG3,   0x32},
    {CC1101_MDMCFG2,   0x30},  // OOK
    {CC1101_MDMCFG1,   0x23},
    {CC1101_MDMCFG0,   0xF8},
    {CC1101_AGCCTRL2,  0xDC},
    {CC1101_AGCCTRL1,  0x00},
    {CC1101_AGCCTRL0,  0x92},
    {CC1101_MCSM1,     0x00},
    {CC1101_MCSM0,     0x18},
    {CC1101_FOCCFG,    0x14},
    {CC1101_FREND0,    0x17},
    {CC1101_PKTCTRL0,  0x32},
};

static const CC1101RegPair PRESET_FSK2DEV238[] = {
    {CC1101_IOCFG0,    0x0D},
    {CC1101_IOCFG2,    0x0D},
    {CC1101_MDMCFG4,   0xCA},
    {CC1101_MDMCFG3,   0x83},
    {CC1101_MDMCFG2,   0x00},  // 2-FSK, no sync
    {CC1101_MDMCFG1,   0x22},
    {CC1101_MDMCFG0,   0xF8},
    {CC1101_DEVIATN,   0x47},  // 238 kHz deviation
    {CC1101_AGCCTRL2,  0xDC},
    {CC1101_AGCCTRL1,  0x00},
    {CC1101_AGCCTRL0,  0x92},
    {CC1101_MCSM1,     0x00},
    {CC1101_MCSM0,     0x18},
    {CC1101_FOCCFG,    0x14},
    {CC1101_FREND0,    0x10},
    {CC1101_PKTCTRL0,  0x32},
};

static const CC1101RegPair PRESET_FSK2DEV476[] = {
    {CC1101_IOCFG0,    0x0D},
    {CC1101_IOCFG2,    0x0D},
    {CC1101_MDMCFG4,   0xCA},
    {CC1101_MDMCFG3,   0x83},
    {CC1101_MDMCFG2,   0x00},
    {CC1101_MDMCFG1,   0x22},
    {CC1101_MDMCFG0,   0xF8},
    {CC1101_DEVIATN,   0x57},  // 476 kHz deviation
    {CC1101_AGCCTRL2,  0xDC},
    {CC1101_AGCCTRL1,  0x00},
    {CC1101_AGCCTRL0,  0x92},
    {CC1101_MCSM1,     0x00},
    {CC1101_MCSM0,     0x18},
    {CC1101_FOCCFG,    0x14},
    {CC1101_FREND0,    0x10},
    {CC1101_PKTCTRL0,  0x32},
};

// ─────────────────────────────────────────────────────────────────────────────

CC1101::CC1101(uint8_t cs, uint8_t gdo0, uint8_t gdo2)
    : _cs(cs), _gdo0(gdo0), _gdo2(gdo2) {}

void CC1101::_select()   { digitalWrite(_cs, LOW);  }
void CC1101::_deselect() { digitalWrite(_cs, HIGH); }

bool CC1101::begin() {
    pinMode(_cs, OUTPUT);
    _deselect();
    if (_gdo0 != 255) pinMode(_gdo0, INPUT);
    if (_gdo2 != 255) pinMode(_gdo2, INPUT);

    SPI.begin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, _cs);
    SPI.setFrequency(5000000);

    // Power-on reset sequence per datasheet
    _deselect();
    delayMicroseconds(5);
    _select();
    delayMicroseconds(10);
    _deselect();
    delayMicroseconds(41);
    _select();

    // Wait for MISO to go low (chip ready)
    unsigned long t = millis();
    while (digitalRead(CC1101_MISO) && (millis() - t < 200));

    strobe(CC1101_SRES);
    delay(2);

    if (!checkPresence()) return false;

    // Default register init
    writeReg(CC1101_FSCTRL1,  0x06);
    writeReg(CC1101_FSCTRL0,  0x00);
    writeReg(CC1101_FSCAL3,   0xE9);
    writeReg(CC1101_FSCAL2,   0x2A);
    writeReg(CC1101_FSCAL1,   0x00);
    writeReg(CC1101_FSCAL0,   0x1F);
    writeReg(CC1101_TEST2,    0x81);
    writeReg(CC1101_TEST1,    0x35);
    writeReg(CC1101_TEST0,    0x09);
    writeReg(CC1101_PKTCTRL1, 0x04);
    writeReg(CC1101_ADDR,     0x00);
    writeReg(CC1101_PKTLEN,   0xFF);

    setFrequency(_freq_hz);
    applyPreset(CC1101Preset::OOK_650_ASYNC);
    idle();
    return true;
}

bool CC1101::checkPresence() {
    uint8_t partnum = readStatusReg(CC1101_PARTNUM);
    uint8_t version = readStatusReg(CC1101_VERSION);
    // CC1101 partnum=0x00, version=0x14
    return (partnum == 0x00 && version == 0x14);
}

void CC1101::reset() {
    strobe(CC1101_SRES);
    delay(2);
}

void CC1101::setFrequency(uint32_t hz) {
    _freq_hz = hz;
    // FREQ[23:0] = hz / Fxosc * 2^16
    uint32_t reg = (uint32_t)(((uint64_t)hz << 16) / SUBGHZ_XOSC_HZ);
    idle();
    writeReg(CC1101_FREQ2, (reg >> 16) & 0xFF);
    writeReg(CC1101_FREQ1, (reg >>  8) & 0xFF);
    writeReg(CC1101_FREQ0, (reg >>  0) & 0xFF);
    strobe(CC1101_SCAL);
    delay(2);
}

void CC1101::_applyRegList(const CC1101RegPair* list, size_t count) {
    for (size_t i = 0; i < count; i++) {
        writeReg(list[i].reg, list[i].val);
    }
}

void CC1101::applyPreset(CC1101Preset preset) {
    switch (preset) {
        case CC1101Preset::OOK_650_ASYNC:
            _applyRegList(PRESET_OOK650, sizeof(PRESET_OOK650) / sizeof(CC1101RegPair));
            break;
        case CC1101Preset::OOK_270_ASYNC:
            _applyRegList(PRESET_OOK270, sizeof(PRESET_OOK270) / sizeof(CC1101RegPair));
            break;
        case CC1101Preset::FSK_2DEV238_ASYNC:
            _applyRegList(PRESET_FSK2DEV238, sizeof(PRESET_FSK2DEV238) / sizeof(CC1101RegPair));
            break;
        case CC1101Preset::FSK_2DEV476_ASYNC:
            _applyRegList(PRESET_FSK2DEV476, sizeof(PRESET_FSK2DEV476) / sizeof(CC1101RegPair));
            break;
        default:
            break;
    }
}

void CC1101::idle()    { strobe(CC1101_SIDLE); delayMicroseconds(100); }
void CC1101::startRX() { strobe(CC1101_SRX);   delayMicroseconds(100); }
void CC1101::startTX() { strobe(CC1101_STX);   delayMicroseconds(100); }
void CC1101::flushRX() { idle(); strobe(CC1101_SFRX); }
void CC1101::flushTX() { idle(); strobe(CC1101_SFTX); }

void CC1101::writeReg(uint8_t addr, uint8_t val) {
    _select();
    while (digitalRead(CC1101_MISO));
    SPI.transfer(addr & 0x3F);
    SPI.transfer(val);
    _deselect();
}

uint8_t CC1101::readReg(uint8_t addr) {
    _select();
    while (digitalRead(CC1101_MISO));
    SPI.transfer(CC1101_READ_SINGLE | (addr & 0x3F));
    uint8_t val = SPI.transfer(0x00);
    _deselect();
    return val;
}

uint8_t CC1101::readStatusReg(uint8_t addr) {
    _select();
    while (digitalRead(CC1101_MISO));
    SPI.transfer(CC1101_READ_BURST | addr);
    uint8_t val = SPI.transfer(0x00);
    _deselect();
    return val;
}

void CC1101::strobe(uint8_t cmd) {
    _select();
    while (digitalRead(CC1101_MISO));
    SPI.transfer(cmd);
    _deselect();
}

void CC1101::writeFIFO(const uint8_t* data, uint8_t len) {
    _select();
    while (digitalRead(CC1101_MISO));
    SPI.transfer(CC1101_WRITE_BURST | CC1101_TXFIFO);
    for (uint8_t i = 0; i < len; i++) SPI.transfer(data[i]);
    _deselect();
}

uint8_t CC1101::readFIFO(uint8_t* buf, uint8_t maxLen) {
    uint8_t avail = rxBytesAvail() & 0x7F;
    uint8_t n = min(avail, maxLen);
    _select();
    while (digitalRead(CC1101_MISO));
    SPI.transfer(CC1101_READ_BURST | CC1101_RXFIFO);
    for (uint8_t i = 0; i < n; i++) buf[i] = SPI.transfer(0x00);
    _deselect();
    return n;
}

uint8_t CC1101::txBytesInFIFO() { return readStatusReg(CC1101_TXBYTES) & 0x7F; }
uint8_t CC1101::rxBytesAvail()  { return readStatusReg(CC1101_RXBYTES) & 0x7F; }

int8_t CC1101::readRSSI() {
    uint8_t raw = readStatusReg(CC1101_RSSI);
    int8_t rssi;
    if (raw >= 128) {
        rssi = (int8_t)((raw - 256) / 2) - 74;
    } else {
        rssi = (int8_t)(raw / 2) - 74;
    }
    return rssi;
}
