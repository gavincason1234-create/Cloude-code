#pragma once
#include <Arduino.h>
#include <SPI.h>

// ─── CC1101 register addresses ───────────────────────────────────────────────
#define CC1101_IOCFG2    0x00
#define CC1101_IOCFG1    0x01
#define CC1101_IOCFG0    0x02
#define CC1101_FIFOTHR   0x03
#define CC1101_SYNC1     0x04
#define CC1101_SYNC0     0x05
#define CC1101_PKTLEN    0x06
#define CC1101_PKTCTRL1  0x07
#define CC1101_PKTCTRL0  0x08
#define CC1101_ADDR      0x09
#define CC1101_CHANNR    0x0A
#define CC1101_FSCTRL1   0x0B
#define CC1101_FSCTRL0   0x0C
#define CC1101_FREQ2     0x0D
#define CC1101_FREQ1     0x0E
#define CC1101_FREQ0     0x0F
#define CC1101_MDMCFG4   0x10
#define CC1101_MDMCFG3   0x11
#define CC1101_MDMCFG2   0x12
#define CC1101_MDMCFG1   0x13
#define CC1101_MDMCFG0   0x14
#define CC1101_DEVIATN   0x15
#define CC1101_MCSM2     0x16
#define CC1101_MCSM1     0x17
#define CC1101_MCSM0     0x18
#define CC1101_FOCCFG    0x19
#define CC1101_BSCFG     0x1A
#define CC1101_AGCCTRL2  0x1B
#define CC1101_AGCCTRL1  0x1C
#define CC1101_AGCCTRL0  0x1D
#define CC1101_FREND1    0x21
#define CC1101_FREND0    0x22
#define CC1101_FSCAL3    0x23
#define CC1101_FSCAL2    0x24
#define CC1101_FSCAL1    0x25
#define CC1101_FSCAL0    0x26
#define CC1101_TEST2     0x2C
#define CC1101_TEST1     0x2D
#define CC1101_TEST0     0x2E

// Status registers (read via 0xC0 | addr)
#define CC1101_PARTNUM   0x30
#define CC1101_VERSION   0x31
#define CC1101_RSSI      0x34
#define CC1101_MARCSTATE 0x35
#define CC1101_TXBYTES   0x3A
#define CC1101_RXBYTES   0x3B

// Command strobes
#define CC1101_SRES      0x30
#define CC1101_SFSTXON   0x31
#define CC1101_SXOFF     0x32
#define CC1101_SCAL      0x33
#define CC1101_SRX       0x34
#define CC1101_STX       0x35
#define CC1101_SIDLE     0x36
#define CC1101_SPWD      0x39
#define CC1101_SFRX      0x3A
#define CC1101_SFTX      0x3B
#define CC1101_SNOP      0x3D

// Multi-byte access flags
#define CC1101_WRITE_BURST   0x40
#define CC1101_READ_SINGLE   0x80
#define CC1101_READ_BURST    0xC0

// TX/RX FIFO
#define CC1101_TXFIFO    0x3F
#define CC1101_RXFIFO    0x3F

// MARCSTATE values
#define CC1101_MARCSTATE_IDLE     0x01
#define CC1101_MARCSTATE_RX       0x0D
#define CC1101_MARCSTATE_TX       0x13

enum class CC1101Preset {
    OOK_650_ASYNC,    // most remotes, garage doors, keyfobs
    OOK_270_ASYNC,    // some older 433 MHz remotes
    FSK_2DEV238_ASYNC,
    FSK_2DEV476_ASYNC,
    RAW,
};

struct CC1101RegPair {
    uint8_t reg;
    uint8_t val;
};

class CC1101 {
public:
    CC1101(uint8_t cs, uint8_t gdo0, uint8_t gdo2 = 255);

    bool     begin();
    void     reset();
    bool     checkPresence();        // returns true if chip responds

    void     setFrequency(uint32_t hz);
    uint32_t getFrequency() const { return _freq_hz; }

    void     applyPreset(CC1101Preset preset);
    void     idle();
    void     startRX();
    void     startTX();
    void     flushRX();
    void     flushTX();

    // Raw register access
    void     writeReg(uint8_t addr, uint8_t val);
    uint8_t  readReg(uint8_t addr);
    uint8_t  readStatusReg(uint8_t addr);
    void     strobe(uint8_t cmd);

    // FIFO
    void     writeFIFO(const uint8_t* data, uint8_t len);
    uint8_t  readFIFO(uint8_t* buf, uint8_t maxLen);
    uint8_t  txBytesInFIFO();
    uint8_t  rxBytesAvail();

    // RSSI in dBm
    int8_t   readRSSI();

    uint8_t  gdo0Pin() const { return _gdo0; }
    uint8_t  gdo2Pin() const { return _gdo2; }

private:
    uint8_t  _cs;
    uint8_t  _gdo0;
    uint8_t  _gdo2;
    uint32_t _freq_hz = 433920000UL;

    void     _select();
    void     _deselect();
    void     _applyRegList(const CC1101RegPair* list, size_t count);
};
