# JARVIS — Flipper Zero Cybersecurity Suite

Custom FAP application suite for Flipper Zero targeting offensive security research.
Designed for use with the ESP32-S2 Wi-Fi Developer Board and External CC1101 SubGhz Antenna.

## Hardware

| Component | Purpose |
|-----------|---------|
| Flipper Zero | Core device |
| ESP32-S2 Wi-Fi Dev Board | Wi-Fi attacks via GPIO UART (pins 13/14) |
| External CC1101 GPIO Board | Extended Sub-GHz range via GPIO SPI |

## Applications

### `jarvis_suite` — Hub
Central launcher and status monitor. Shows live connectivity to ESP32 and external CC1101.
Coordinates logging and launches all sub-modules.

### `wifi_arsenal` — Wi-Fi Attack Toolkit
Controls the ESP32-S2 over UART using a JSON command protocol.
- AP/station scanner with RSSI visualization
- Targeted deauthentication
- PMKID harvest → export `.hc22000` to SD for offline cracking
- Evil Twin captive portal with credential logging
- Beacon spam (targeted SSIDs)
- Passive monitor mode with pcap-style logging

### `rf_recon` — Sub-GHz Recon
Uses the external GPIO CC1101 for extended range and sensitivity.
- Wide-band scanner (300–928 MHz)
- Signal capture with protocol auto-detection
- Replay with adjustable delay/repeat
- Rolling code analysis and bruteforce (static code only)

## ESP32-S2 Firmware (`jarvis_wifi`)
Custom firmware for the ESP32-S2 board.
JSON command protocol over UART at 115200 baud.
Adds credential harvesting captive portal and PMKID export not in stock Marauder.

## Build

```bash
# Install toolchain and clone base firmware
./scripts/setup.sh

# Build all Flipper FAPs
./scripts/build_flipper.sh

# Build and flash ESP32-S2 firmware
./scripts/build_esp32.sh

# Flash FAPs to Flipper (qFlipper must be running)
./scripts/flash_faps.sh
```

## Wiring

See `docs/wiring.md` for GPIO pinout diagrams.

## Legal

For authorized security testing only. Obtain written permission before testing any system you do not own.
