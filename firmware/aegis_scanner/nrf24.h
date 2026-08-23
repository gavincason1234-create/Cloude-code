/*
 * Minimal nRF24L01+ driver — carrier detection only.
 *
 * Deliberately not RF24.h. Everything needed for a spectrum sweep is four
 * registers, and a self-contained ~100 lines avoids pinning a library version
 * across three board variants. This driver cannot transmit or receive packets,
 * only measure energy, which is exactly the scope this project wants.
 *
 * The technique: park the receiver on one 1 MHz channel, listen briefly, then
 * read the Received Power Detector bit. RPD latches high when the channel was
 * above roughly -64 dBm. Sampling it many times per channel turns a one-bit
 * comparator into a usable occupancy percentage.
 */
#pragma once

#include <Arduino.h>
#include <SPI.h>

class Nrf24Sweeper {
public:
  Nrf24Sweeper(uint8_t cePin, uint8_t csnPin) : ce_(cePin), csn_(csnPin) {}

  /** @return false if no module answers on SPI. */
  bool begin() {
    pinMode(ce_, OUTPUT);
    pinMode(csn_, OUTPUT);
    digitalWrite(ce_, LOW);
    digitalWrite(csn_, HIGH);

    SPI.begin();
    delay(5);

    // Probe before configuring: a floating bus reads back 0x00 or 0xFF, while
    // a real module returns the value we just wrote.
    writeRegister(REG_CONFIG, 0x0C);
    if (readRegister(REG_CONFIG) != 0x0C) return false;

    writeRegister(REG_EN_AA, 0x00);        // no auto-ack
    writeRegister(REG_RF_SETUP, 0x0F);     // 2 Mbps wideband, max gain
    writeRegister(REG_CONFIG, 0x0F);       // PWR_UP | PRIM_RX
    delay(2);                              // power-up settling

    return true;
  }

  void end() {
    digitalWrite(ce_, LOW);
    writeRegister(REG_CONFIG, 0x0C);       // power down
  }

  /**
   * Sample one channel.
   *
   * @param channel 0-125, where the centre frequency is 2400 + channel MHz.
   * @param samples how many RPD reads to take.
   * @return number of samples that saw carrier (0..samples).
   */
  uint16_t sampleChannel(uint8_t channel, uint16_t samples) {
    writeRegister(REG_RF_CH, channel);

    uint16_t hits = 0;
    for (uint16_t i = 0; i < samples; i++) {
      digitalWrite(ce_, HIGH);
      // Datasheet: 130 µs to settle into RX, then the RPD needs ~40 µs of
      // dwell before its reading is meaningful.
      delayMicroseconds(220);
      digitalWrite(ce_, LOW);

      if (readRegister(REG_RPD) & 0x01) hits++;
    }
    return hits;
  }

private:
  static const uint8_t REG_CONFIG = 0x00;
  static const uint8_t REG_EN_AA = 0x01;
  static const uint8_t REG_RF_CH = 0x05;
  static const uint8_t REG_RF_SETUP = 0x06;
  static const uint8_t REG_RPD = 0x09;

  static const uint8_t CMD_R_REGISTER = 0x00;
  static const uint8_t CMD_W_REGISTER = 0x20;

  // 10 MHz is well inside the nRF24's limit and safe on breadboard wiring.
  SPISettings settings_{10000000, MSBFIRST, SPI_MODE0};

  uint8_t ce_;
  uint8_t csn_;

  uint8_t readRegister(uint8_t reg) {
    SPI.beginTransaction(settings_);
    digitalWrite(csn_, LOW);
    SPI.transfer(CMD_R_REGISTER | (reg & 0x1F));
    const uint8_t value = SPI.transfer(0xFF);
    digitalWrite(csn_, HIGH);
    SPI.endTransaction();
    return value;
  }

  void writeRegister(uint8_t reg, uint8_t value) {
    SPI.beginTransaction(settings_);
    digitalWrite(csn_, LOW);
    SPI.transfer(CMD_W_REGISTER | (reg & 0x1F));
    SPI.transfer(value);
    digitalWrite(csn_, HIGH);
    SPI.endTransaction();
  }
};
