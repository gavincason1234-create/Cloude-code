/**
 * FlipperESP — Flipper Zero-inspired multi-tool for ESP32-S2
 *
 * Hardware:
 *   CC1101 sub-GHz module  (SPI)
 *   SSD1306 128x64 OLED    (I2C)
 *   SD card module         (SPI, shared bus with CC1101)
 *   ESP32-S2               (WiFi only; upgrade to S3 for BLE)
 *
 * Control: USB serial @ 115200 baud
 *   help            — list commands
 *   sub scan        — sub-GHz band scan
 *   sub cap [freq]  — capture raw signal (default 433.92 MHz)
 *   sub tx [file]   — replay last captured or load from SD
 *   sub save [name] — save captured signal to SD
 *   sub load [file] — load signal from SD
 *   wifi scan       — list nearby networks
 *   wifi deauth <bssid> <channel> — send deauth (authorized use only)
 *   wifi evil <ssid> [channel]    — start evil twin AP
 *   wifi stopevil   — stop evil twin
 *   wifi probe [ch] — start probe request sniffer
 *   wifi stopprobe  — stop sniffer
 *   ble scan        — BLE scan (ESP32-S3 only)
 *   ble spam <type> — BLE spam: apple|samsung|windows|generic
 *   ble stop        — stop BLE spam
 *   rssi            — current CC1101 RSSI
 */

#include <Arduino.h>
#include "subghz/cc1101.h"
#include "subghz/subghz.h"
#include "wifi_tools/wifi_tools.h"
#include "bluetooth/ble_tools.h"
#include "ui/display.h"
#include "ui/menu.h"
#include "../include/config.h"

// ─── Global objects ───────────────────────────────────────────────────────────
CC1101         radio(CC1101_CS, CC1101_GDO0, CC1101_GDO2);
SubGhz         subghz(radio);
WifiScanner    wifiScanner;
WifiDeauth     wifiDeauth;
EvilTwin       evilTwin;
ProbeSniffer   probeSniffer;
BleTools       bleTools;
Display        display;
Menu           mainMenu(display);

SubGhzRawSignal lastCapture;
bool            hasCapture = false;

// ─── Forward declarations ─────────────────────────────────────────────────────
static void handleSerial();
static void processCommand(char* cmd);
static void buildMainMenu();
static void handleSubCmd(char* args);
static void handleWifiCmd(char* args);
static void handleBleCmd(char* args);
static void printHelp();

// ─── Menu actions ─────────────────────────────────────────────────────────────
static void menuSubScan()    { subghz.scanBands(); }
static void menuSubCapture() {
    display.clear();
    display.printCenter(28, "Listening...");
    display.printCenter(40, "433.92 MHz");
    display.show();
    bool ok = subghz.captureRaw(lastCapture, 433920000UL);
    hasCapture = ok;
    display.clear();
    display.printCenter(20, ok ? "Captured!" : "No signal");
    if (ok) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%d samples", lastCapture.count);
        display.printCenter(34, buf);
    }
    display.show();
    delay(2000);
}
static void menuWifiScan() { wifiScanner.scan(); }
static void menuBleTools() {
    display.clear();
    display.printCenter(24, "BLE Tools");
#if CONFIG_BT_ENABLED
    display.printCenter(38, "Use serial cmds");
#else
    display.printCenter(34, "Not available");
    display.printCenter(46, "(S2 has no BLE)");
#endif
    display.show();
    delay(2000);
}

// ─── Splash screen ────────────────────────────────────────────────────────────
static void splash() {
    if (!display.ok()) return;
    display.clear();
    display.printCenter(4,  "FlipperESP", 2);
    display.printCenter(26, "ESP32-S2 Edition");
    display.printCenter(38, "CC1101 + WiFi");
    display.printCenter(50, "115200 baud serial");
    display.show();
    delay(2500);
}

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(SERIAL_BAUD);
    delay(500);
    Serial.println("\n\n=== FlipperESP booting ===");

    display.begin();
    splash();

    bool radioOk = subghz.begin();

    buildMainMenu();
    mainMenu.draw("FlipperESP");

    Serial.println("Type 'help' for commands.");
    if (!radioOk) Serial.println("[!] CC1101 not found — sub-GHz disabled");
}

// ─── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
    handleSerial();

    // Keep evil twin / probe sniffer ticking
    if (evilTwin.isRunning()) {
        if (evilTwin.checkCaptures()) {
            Serial.printf("[!] CAPTURE  user='%s'  pass='%s'\n",
                          evilTwin.capturedUser, evilTwin.capturedPass);
        }
    }

    delay(10);
}

// ─── Menu builder ─────────────────────────────────────────────────────────────
static void buildMainMenu() {
    mainMenu.clear();
    mainMenu.addItem("> Sub-GHz Scan",    menuSubScan);
    mainMenu.addItem("> Sub-GHz Capture", menuSubCapture);
    mainMenu.addItem("> WiFi Scan",       menuWifiScan);
    mainMenu.addItem("> BLE Tools",       menuBleTools);
}

// ─── Serial command dispatcher ────────────────────────────────────────────────
static char _buf[256];
static int  _bufLen = 0;

static void handleSerial() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\r') continue;
        if (c == '\n' || _bufLen >= (int)sizeof(_buf) - 1) {
            _buf[_bufLen] = '\0';
            if (_bufLen > 0) processCommand(_buf);
            _bufLen = 0;
        } else {
            _buf[_bufLen++] = c;
        }
    }
}

static void processCommand(char* cmd) {
    while (*cmd == ' ') cmd++;
    if (!*cmd) return;

    char* space = strchr(cmd, ' ');
    char* args  = space ? space + 1 : (char*)"";
    if (space) *space = '\0';

    if      (strcasecmp(cmd, "help")  == 0) printHelp();
    else if (strcasecmp(cmd, "sub")   == 0) handleSubCmd(args);
    else if (strcasecmp(cmd, "wifi")  == 0) handleWifiCmd(args);
    else if (strcasecmp(cmd, "ble")   == 0) handleBleCmd(args);
    else if (strcasecmp(cmd, "rssi")  == 0) Serial.printf("RSSI: %d dBm\n", subghz.readRSSI());
    else if (strcasecmp(cmd, "menu")  == 0) mainMenu.draw("FlipperESP");
    else    Serial.printf("Unknown command '%s'. Type 'help'.\n", cmd);
}

static void handleSubCmd(char* args) {
    char sub[32]; char rest[200] = "";
    int n = sscanf(args, "%31s %199[^\n]", sub, rest);
    if (n < 1) { Serial.println("Usage: sub <scan|cap|tx|save|load>"); return; }

    if (strcasecmp(sub, "scan") == 0) {
        subghz.scanBands();

    } else if (strcasecmp(sub, "cap") == 0) {
        uint32_t freq = (strlen(rest) > 0) ? strtoul(rest, nullptr, 10) : 433920000UL;
        if (freq < 1000000UL) freq *= 1000000UL; // allow "433" shorthand

        Serial.printf("[Sub] Capturing @ %lu Hz (5 s timeout)...\n", freq);
        display.clear();
        display.printCenter(24, "Listening...");
        display.show();

        bool ok = subghz.captureRaw(lastCapture, freq);
        hasCapture = ok;
        if (ok) {
            Serial.printf("[Sub] Captured %d samples\n", lastCapture.count);
        } else {
            Serial.println("[Sub] No signal detected");
        }
        mainMenu.draw("FlipperESP");

    } else if (strcasecmp(sub, "tx") == 0) {
        if (strlen(rest) > 0) {
            char path[64];
            snprintf(path, sizeof(path), "%s/%s%s",
                     SUBGHZ_DIR, rest,
                     (strstr(rest, ".sub") ? "" : SUBGHZ_EXT));
            subghz.loadSignal(lastCapture, path);
            hasCapture = (lastCapture.count > 0);
        }
        if (!hasCapture) { Serial.println("[Sub] Nothing to transmit"); return; }
        Serial.println("[Sub] Transmitting...");
        display.clear();
        display.printCenter(24, "Transmitting...");
        display.show();
        subghz.transmitRaw(lastCapture);
        mainMenu.draw("FlipperESP");

    } else if (strcasecmp(sub, "save") == 0) {
        if (!hasCapture) { Serial.println("[Sub] Nothing captured"); return; }
        char name[32]; char path[64];
        snprintf(name, sizeof(name), "%s", (strlen(rest) > 0) ? rest : "signal");
        snprintf(path, sizeof(path), "%s/%s%s", SUBGHZ_DIR, name, SUBGHZ_EXT);
        subghz.saveSignal(lastCapture, path);

    } else if (strcasecmp(sub, "load") == 0) {
        if (strlen(rest) == 0) { Serial.println("Usage: sub load <name>"); return; }
        char path[64];
        snprintf(path, sizeof(path), "%s/%s%s",
                 SUBGHZ_DIR, rest, (strstr(rest, ".sub") ? "" : SUBGHZ_EXT));
        subghz.loadSignal(lastCapture, path);
        hasCapture = (lastCapture.count > 0);

    } else {
        Serial.printf("[Sub] Unknown subcommand '%s'\n", sub);
    }
}

static void handleWifiCmd(char* args) {
    char sub[32]; char a1[64]=""; char a2[16]="";
    int  n = sscanf(args, "%31s %63s %15s", sub, a1, a2);
    if (n < 1) { Serial.println("Usage: wifi <scan|deauth|evil|stopevil|probe|stopprobe>"); return; }

    if (strcasecmp(sub, "scan") == 0) {
        wifiScanner.scan();

    } else if (strcasecmp(sub, "deauth") == 0) {
        if (n < 2) {
            Serial.println("Usage: wifi deauth <BSSID:XX:XX:XX:XX:XX:XX> [channel]");
            return;
        }
        uint8_t bssid[6] = {};
        if (sscanf(a1, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                   &bssid[0],&bssid[1],&bssid[2],&bssid[3],&bssid[4],&bssid[5]) != 6) {
            Serial.println("[WiFi] Invalid BSSID format");
            return;
        }
        uint8_t ch = (n >= 3) ? (uint8_t)atoi(a2) : 1;
        static const uint8_t BROADCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
        WifiDeauth::broadcastDeauth(bssid, ch, 20);

    } else if (strcasecmp(sub, "evil") == 0) {
        const char* ssid = (strlen(a1) > 0) ? a1 : EVIL_TWIN_AP_SSID;
        uint8_t     ch   = (n >= 3) ? (uint8_t)atoi(a2) : WIFI_DEFAULT_CHANNEL;
        evilTwin.start(ssid, ch);

    } else if (strcasecmp(sub, "stopevil") == 0) {
        evilTwin.stop();

    } else if (strcasecmp(sub, "probe") == 0) {
        uint8_t ch = (strlen(a1) > 0) ? (uint8_t)atoi(a1) : 0;
        probeSniffer.start(ch);

    } else if (strcasecmp(sub, "stopprobe") == 0) {
        probeSniffer.stop();

    } else {
        Serial.printf("[WiFi] Unknown subcommand '%s'\n", sub);
    }
}

static void handleBleCmd(char* args) {
    char sub[32]; char a1[32]="";
    int  n = sscanf(args, "%31s %31s", sub, a1);
    if (n < 1) { Serial.println("Usage: ble <scan|spam|stop>"); return; }

    if (strcasecmp(sub, "scan") == 0) {
        bleTools.begin();
        bleTools.scanDevices(5000);

    } else if (strcasecmp(sub, "spam") == 0) {
        bleTools.begin();
        BleSpamType t = BleSpamType::GENERIC_ADV;
        if      (strcasecmp(a1, "apple")   == 0) t = BleSpamType::APPLE_PROXIMITY;
        else if (strcasecmp(a1, "samsung")  == 0) t = BleSpamType::SAMSUNG_FAST_PAIR;
        else if (strcasecmp(a1, "windows") == 0) t = BleSpamType::WINDOWS_SWIFT_PAIR;
        bleTools.startSpam(t);

    } else if (strcasecmp(sub, "stop") == 0) {
        bleTools.stop();

    } else {
        Serial.printf("[BLE] Unknown subcommand '%s'\n", sub);
    }
}

static void printHelp() {
    Serial.println(R"(
FlipperESP — Serial Commands
─────────────────────────────────────────────────────
Sub-GHz (CC1101):
  sub scan              Scan common bands for signals
  sub cap [freq_hz]     Capture raw signal (default 433920000)
  sub tx [name]         Retransmit last capture (or load by name)
  sub save [name]       Save last capture to SD as /subghz/<name>.sub
  sub load <name>       Load signal from SD

WiFi:
  wifi scan             Scan for nearby networks
  wifi deauth <BSSID> [ch]  Deauth all clients (authorized only)
  wifi evil <SSID> [ch]     Start evil twin + captive portal
  wifi stopevil         Stop evil twin
  wifi probe [ch]       Probe request sniffer (0 = all channels)
  wifi stopprobe        Stop sniffer

Bluetooth (ESP32-S3 only — not available on S2):
  ble scan              Scan for BLE devices (5 s)
  ble spam <type>       Spam BLE ads: apple | samsung | windows | generic
  ble stop              Stop BLE spam

Misc:
  rssi                  Read CC1101 RSSI
  menu                  Redraw OLED menu
  help                  This help

⚠  Use WiFi and sub-GHz tools only on networks/devices you own or have
   explicit written authorization to test.
─────────────────────────────────────────────────────)");
}
