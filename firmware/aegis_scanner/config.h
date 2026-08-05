/*
 * AEGIS scanner firmware — build configuration.
 *
 * Every scan source is optional. Turn off what your stack does not have and
 * the code for it is not compiled in, so this same sketch builds for an
 * UNO R4 WiFi with a full stack or a bare Minima with one module on top.
 */
#pragma once

/* ------------------------------------------------------------- board ---- */

/*
 * The R4 WiFi carries an ESP32-S3 radio co-processor; the Minima has no radio
 * at all. ARDUINO_UNOWIFIR4 is defined by the core, so the default is correct
 * for whichever board is selected in the IDE.
 */
#if defined(ARDUINO_UNOWIFIR4)
  #define AEGIS_BOARD_NAME "uno_r4_wifi"
  #define AEGIS_HAS_RADIO 1
#elif defined(ARDUINO_MINIMA)
  #define AEGIS_BOARD_NAME "uno_r4_minima"
  #define AEGIS_HAS_RADIO 0
#else
  #define AEGIS_BOARD_NAME "unknown"
  #define AEGIS_HAS_RADIO 0
#endif

/* ------------------------------------------------------- scan sources ---- */

/* 2.4 GHz WiFi access point scan, through the ESP32-S3. */
#define AEGIS_ENABLE_WIFI (AEGIS_HAS_RADIO)

/*
 * BLE advertisement scan, also through the ESP32-S3.
 *
 * Important: on the R4 WiFi the WiFi and BLE stacks share one radio and cannot
 * run at the same time. The sketch tears one down before bringing the other
 * up, which is why a full sweep takes a few seconds. Set this to 0 if you only
 * care about WiFi and want faster sweeps.
 */
#define AEGIS_ENABLE_BLE (AEGIS_HAS_RADIO)

/*
 * Raw 2.4 GHz energy sweep using a stacked nRF24L01+ module.
 *
 * This sees the whole band, not just WiFi: Bluetooth, Zigbee, wireless mice,
 * video senders, microwave leakage. It is a carrier detector, not a receiver —
 * it reports how often each 1 MHz channel is above roughly -64 dBm, which is
 * all the nRF24's RPD register can tell you.
 */
#define AEGIS_ENABLE_NRF24 1

/* ------------------------------------------------------------- wiring ---- */

/*
 * nRF24L01+ on the standard SPI header.
 *
 *   nRF24  →  UNO R4
 *   VCC    →  3V3      (NOT 5V — the module's supply is 1.9-3.6 V)
 *   GND    →  GND
 *   CE     →  D9
 *   CSN    →  D10
 *   SCK    →  D13
 *   MOSI   →  D11
 *   MISO   →  D12
 *
 * The module's digital inputs are 5 V tolerant, so no level shifter is needed
 * on the signal lines. Put 10 µF across VCC/GND at the module — a bare
 * nRF24L01+ browning out on scan current is the single most common cause of
 * "it reads all zeros".
 */
#define AEGIS_NRF_CE_PIN  9
#define AEGIS_NRF_CSN_PIN 10

/* ------------------------------------------------------------ sweeping ---- */

/* nRF24 channels are 1 MHz apart: 0 → 2400 MHz, 125 → 2525 MHz. */
#define AEGIS_RF_CHANNELS 126

/* Samples per channel per sweep. Higher is smoother and slower. */
#define AEGIS_RF_SAMPLES 48

/* How long to listen for BLE advertisements, milliseconds. */
#define AEGIS_BLE_SCAN_MS 4000

/* Idle time between automatic sweeps, milliseconds. */
#define AEGIS_AUTO_INTERVAL_MS 8000

/* ------------------------------------------------------------- output ---- */

#define AEGIS_BAUD 115200
#define AEGIS_FW_VERSION "aegis-scanner/1.0"

/* Draw a live activity bar on the R4 WiFi's 12x8 LED matrix. */
#define AEGIS_ENABLE_MATRIX (AEGIS_HAS_RADIO)
