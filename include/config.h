#pragma once

// ─── Hardware pin assignments ────────────────────────────────────────────────
// CC1101 Sub-GHz (SPI2 / FSPI on ESP32-S2)
#define CC1101_SCK   36
#define CC1101_MISO  37
#define CC1101_MOSI  35
#define CC1101_CS    34
#define CC1101_GDO0  33   // sync / data out
#define CC1101_GDO2  26   // optional second GDO

// SD Card (shared SPI bus)
#define SD_CS        13
#define SD_SCK       CC1101_SCK
#define SD_MISO      CC1101_MISO
#define SD_MOSI      CC1101_MOSI

// SSD1306 OLED (I2C)
#define OLED_SDA     8
#define OLED_SCL     9
#define OLED_ADDR    0x3C
#define OLED_WIDTH   128
#define OLED_HEIGHT  64

// Optional navigation buttons (add later)
#define BTN_UP       -1
#define BTN_DOWN     -1
#define BTN_LEFT     -1
#define BTN_RIGHT    -1
#define BTN_OK       -1
#define BTN_BACK     -1

// ─── Feature flags ───────────────────────────────────────────────────────────
#define FEATURE_SUBGHZ   1
#define FEATURE_WIFI     1
// CONFIG_BT_ENABLED is set by platformio.ini build_flags

// ─── Sub-GHz defaults ────────────────────────────────────────────────────────
#define SUBGHZ_DEFAULT_FREQ_HZ   433920000UL
#define SUBGHZ_XOSC_HZ           26000000UL
#define SUBGHZ_MAX_RAW_SAMPLES   4096

// ─── WiFi defaults ───────────────────────────────────────────────────────────
#define WIFI_DEFAULT_CHANNEL     6
#define EVIL_TWIN_AP_SSID        "FlipperAP"
#define EVIL_TWIN_AP_PASS        ""          // open network
#define CAPTIVE_PORTAL_IP        "192.168.4.1"

// ─── Storage ─────────────────────────────────────────────────────────────────
#define STORAGE_USE_SD           1           // 1 = SD, 0 = SPIFFS fallback
#define SUBGHZ_DIR               "/subghz"
#define SUBGHZ_EXT               ".sub"

// ─── Serial UI ───────────────────────────────────────────────────────────────
#define SERIAL_BAUD              115200
