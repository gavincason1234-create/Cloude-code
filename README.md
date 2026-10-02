# FlipperESP — Flipper Zero-inspired toolkit for ESP32-S2

A full-featured sub-GHz / WiFi multi-tool built on the ESP32-S2, using a CC1101 radio module and an SSD1306 OLED display.  Designed to be Flipper-compatible: captured `.sub` files load directly on a real Flipper Zero.

> **Legal notice**: WiFi deauthentication and evil-twin tools are for use only on networks and devices you own or have **explicit written authorization** to test.  Unauthorized use is illegal in most jurisdictions.

---

## Hardware

| Component | Notes |
|-----------|-------|
| ESP32-S2 dev board | Any variant (Saola-1, DevKit-C, etc.) |
| CC1101 module | 300–928 MHz, SPI interface |
| SSD1306 OLED | 128×64 monochrome, I2C |
| Micro-SD card module | SPI, shares bus with CC1101 |
| Sub-GHz antenna | Matched to your target band |

---

## Wiring

### CC1101 → ESP32-S2

| CC1101 | ESP32-S2 GPIO |
|--------|--------------|
| VCC    | 3.3 V        |
| GND    | GND          |
| SCK    | 36           |
| MISO   | 37           |
| MOSI   | 35           |
| CSN    | 34           |
| GDO0   | 33           |
| GDO2   | 26 (optional)|

### SSD1306 OLED → ESP32-S2 (I2C)

| OLED | ESP32-S2 GPIO |
|------|--------------|
| VCC  | 3.3 V        |
| GND  | GND          |
| SDA  | 8            |
| SCL  | 9            |

### SD Card → ESP32-S2

| SD | ESP32-S2 GPIO |
|----|--------------|
| VCC | 3.3 V       |
| GND | GND         |
| SCK | 36 (shared) |
| MISO| 37 (shared) |
| MOSI| 35 (shared) |
| CS  | 13          |

> All pin assignments are in `include/config.h` — change them to match your board.

---

## Flashing

### Prerequisites

Install [PlatformIO](https://platformio.org/) — either the CLI or the VS Code extension.

```bash
# Install PlatformIO CLI
pip install platformio

# Clone and enter the repo
git clone https://github.com/gavincason1234-create/cloude-code.git
cd cloude-code

# Flash to ESP32-S2
pio run -e esp32-s2 --target upload

# Open serial monitor
pio device monitor -b 115200
```

### Upgrade to ESP32-S3 (for BLE)

1. Open `platformio.ini`, uncomment the `[env:esp32-s3]` section.
2. Change `build_flags` to `-D CONFIG_BT_ENABLED=1`.
3. Run `pio run -e esp32-s3 --target upload`.

BLE features (Apple/Samsung/Windows spam, BLE scanner) activate automatically.

---

## Serial commands (115200 baud)

```
sub scan              Scan 300–915 MHz bands, show RSSI
sub cap [freq_hz]     Capture raw signal (default: 433920000)
sub tx [name]         Retransmit last capture, or load file by name
sub save [name]       Save to SD: /subghz/<name>.sub
sub load <name>       Load from SD

wifi scan             List nearby networks
wifi deauth <BSSID> [ch]   Deauth all clients (authorized use only)
wifi evil <SSID> [ch]      Evil twin AP + captive portal
wifi stopevil
wifi probe [ch]       Probe-request sniffer (0 = all channels)
wifi stopprobe

ble scan              5-second BLE device scan  (S3 only)
ble spam apple|samsung|windows|generic    (S3 only)
ble stop

rssi                  Read current CC1101 RSSI
menu                  Redraw OLED menu
help                  List all commands
```

---

## .sub file format

Captured files are Flipper-compatible RAW files:

```
Filetype: Flipper SubGhz RAW File
Version: 1
Frequency: 433920000
Preset: FuriHalSubGhzPresetOok650Async
Protocol: RAW
RAW_Data: 500 -1000 500 -1500 ...
```

Copy `.sub` files to a Flipper Zero's SD card under `/subghz/` to replay them on real Flipper hardware.

---

## Project layout

```
├── include/config.h          Pin definitions, feature flags
├── src/
│   ├── main.cpp              Entry point, serial command loop
│   ├── subghz/
│   │   ├── cc1101.h/cpp      CC1101 SPI driver (full register map)
│   │   └── subghz.h/cpp      Capture, replay, .sub file I/O
│   ├── wifi_tools/
│   │   ├── wifi_tools.h      WiFi tool interfaces
│   │   ├── wifi_scanner.cpp  Network scanner
│   │   ├── deauth.cpp        802.11 deauth frame injection
│   │   ├── evil_twin.cpp     AP + captive portal + credential capture
│   │   └── probe_sniffer.cpp Probe request sniffer
│   ├── bluetooth/
│   │   ├── ble_tools.h
│   │   └── ble_tools.cpp     BLE spam + scanner (S3 only, no-op on S2)
│   └── ui/
│       ├── display.h/cpp     SSD1306 wrapper
│       └── menu.h/cpp        Scrollable menu system
└── platformio.ini
```

---

## Adding buttons

When you wire up navigation buttons, set the GPIO numbers in `include/config.h`:

```c
#define BTN_UP    21
#define BTN_DOWN  20
#define BTN_OK    19
#define BTN_BACK  18
```

Then in `src/main.cpp` `loop()`, read the buttons and call `mainMenu.moveUp()`, `mainMenu.moveDown()`, `mainMenu.select()`.
