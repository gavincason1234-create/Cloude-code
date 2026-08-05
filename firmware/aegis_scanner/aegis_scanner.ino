/*
 * AEGIS 2.4 GHz scanner — Arduino UNO R4 firmware.
 *
 * Streams newline-delimited JSON over USB serial. The AEGIS web app opens the
 * port with Web Serial and plots each record on its radar, which means the
 * browser build gets real RF scanning without any native shell at all — the
 * board does what the browser is not allowed to.
 *
 * Three independent sources, any of which can be compiled out in config.h:
 *
 *   wifi   2.4 GHz access points, via the ESP32-S3 (R4 WiFi only)
 *   ble    Bluetooth LE advertisements, via the same radio (R4 WiFi only)
 *   rf     raw band energy per 1 MHz channel, via a stacked nRF24L01+
 *
 * The nRF24 source is the interesting one: it sees everything radiating in the
 * band, including devices that never announce themselves — Zigbee, wireless
 * peripherals, video senders, a leaky microwave oven.
 *
 * Receive only. This firmware never transmits, never associates, never
 * deauthenticates. The nRF24 is configured as a carrier detector and its
 * transmit path is never enabled.
 */

#include "config.h"

#if AEGIS_ENABLE_WIFI
  #include <WiFiS3.h>
#endif
#if AEGIS_ENABLE_BLE
  #include <ArduinoBLE.h>
#endif
#if AEGIS_ENABLE_NRF24
  #include "nrf24.h"
  static Nrf24Sweeper nrf(AEGIS_NRF_CE_PIN, AEGIS_NRF_CSN_PIN);
  static bool nrfPresent = false;
#endif
#if AEGIS_ENABLE_MATRIX
  #include "Arduino_LED_Matrix.h"
  static ArduinoLEDMatrix matrix;
#endif

static bool autoSweep = true;
static bool wantWifi = AEGIS_ENABLE_WIFI;
static bool wantBle = AEGIS_ENABLE_BLE;
static bool wantRf = AEGIS_ENABLE_NRF24;
static uint32_t lastSweep = 0;

/* Peak activity per WiFi channel, for the LED matrix bar graph. */
static uint8_t channelLoad[14] = {0};

/* ------------------------------------------------------------ output ----- */

/** Emit a JSON string value with the escaping the spec actually requires. */
static void printJsonString(const char *s) {
  Serial.print('"');
  for (const char *p = s; *p; p++) {
    const uint8_t c = (uint8_t)*p;
    switch (c) {
      case '"':  Serial.print("\\\""); break;
      case '\\': Serial.print("\\\\"); break;
      case '\n': Serial.print("\\n"); break;
      case '\r': Serial.print("\\r"); break;
      case '\t': Serial.print("\\t"); break;
      default:
        if (c < 0x20) {
          // Control characters must be \u escaped or the host's JSON.parse
          // rejects the whole line. SSIDs are attacker-controlled bytes, so
          // this is not hypothetical.
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          Serial.print(buf);
        } else {
          Serial.print((char)c);
        }
    }
  }
  Serial.print('"');
}

static void emitError(const char *msg) {
  Serial.print(F("{\"t\":\"err\",\"msg\":"));
  printJsonString(msg);
  Serial.println('}');
}

#if AEGIS_ENABLE_WIFI
static void formatMac(const uint8_t *mac, char *out, bool reversed) {
  for (uint8_t i = 0; i < 6; i++) {
    const uint8_t b = reversed ? mac[5 - i] : mac[i];
    snprintf(out + i * 3, 4, "%02x%s", b, i < 5 ? ":" : "");
  }
}
#endif

static void emitHello() {
  Serial.print(F("{\"t\":\"hello\",\"fw\":\"" AEGIS_FW_VERSION "\",\"board\":\"" AEGIS_BOARD_NAME "\",\"caps\":["));
  bool first = true;
  auto cap = [&](const char *name) {
    if (!first) Serial.print(',');
    Serial.print('"'); Serial.print(name); Serial.print('"');
    first = false;
  };
#if AEGIS_ENABLE_WIFI
  cap("wifi");
#endif
#if AEGIS_ENABLE_BLE
  cap("ble");
#endif
#if AEGIS_ENABLE_NRF24
  if (nrfPresent) cap("rf");
#endif
  Serial.print(F("],\"radio\":"));
#if AEGIS_ENABLE_WIFI
  printJsonString(WiFi.firmwareVersion());
#else
  Serial.print(F("null"));
#endif
  // Publish the real detection floor so the UI never claims -64 dBm on a
  // module whose LNA actually pulls it 20 dB lower.
  Serial.print(F(",\"lna\":"));
  Serial.print(AEGIS_NRF_LNA_GAIN_DB);
  Serial.print(F(",\"floorDbm\":"));
  Serial.print(-64 - AEGIS_NRF_LNA_GAIN_DB);
  Serial.print(F(",\"channels\":"));
  Serial.print(AEGIS_RF_CHANNELS);
  Serial.print(F(",\"samples\":"));
  Serial.print(AEGIS_RF_SAMPLES);
  Serial.println('}');
}

/* -------------------------------------------------------------- WiFi ----- */

#if AEGIS_ENABLE_WIFI
static const char *encryptionName(uint8_t type) {
  // These values are WiFiS3's, which differ from WiFiNINA's — do not reuse
  // constants from UNO WiFi Rev2 examples here.
  switch (type) {
    case ENC_TYPE_NONE:            return "Open";
    case ENC_TYPE_WEP:             return "WEP";
    case ENC_TYPE_WPA:             return "WPA";
    case ENC_TYPE_WPA2:            return "WPA2";
    case ENC_TYPE_WPA2_ENTERPRISE: return "WPA2-Enterprise";
    case ENC_TYPE_WPA3:            return "WPA3";
    case ENC_TYPE_AUTO:            return "Auto";
    default:                       return "Unknown";
  }
}

static void sweepWifi() {
  const uint32_t started = millis();

  if (WiFi.status() == WL_NO_SHIELD) {
    emitError("no ESP32-S3 radio detected");
    return;
  }

  const int8_t found = WiFi.scanNetworks();
  if (found < 0) {
    emitError("wifi scan failed");
    return;
  }

  for (int8_t i = 0; i < found; i++) {
    uint8_t bssid[6] = {0};
    WiFi.BSSID(i, bssid);
    char mac[18];
    // WiFiS3 hands back the BSSID least-significant byte first.
    formatMac(bssid, mac, true);

    const uint8_t ch = WiFi.channel(i);
    const int32_t rssi = WiFi.RSSI(i);

    if (ch < 14) {
      // Track the strongest thing on each channel for the LED display.
      const uint8_t strength = (uint8_t)constrain(map(rssi, -95, -35, 0, 8), 0, 8);
      if (strength > channelLoad[ch]) channelLoad[ch] = strength;
    }

    Serial.print(F("{\"t\":\"wifi\",\"ssid\":"));
    printJsonString(WiFi.SSID(i));
    Serial.print(F(",\"bssid\":\"")); Serial.print(mac);
    Serial.print(F("\",\"rssi\":")); Serial.print(rssi);
    Serial.print(F(",\"ch\":")); Serial.print(ch);
    Serial.print(F(",\"mhz\":")); Serial.print(ch == 14 ? 2484 : 2407 + ch * 5);
    Serial.print(F(",\"enc\":\"")); Serial.print(encryptionName(WiFi.encryptionType(i)));
    Serial.println(F("\"}"));
  }

  Serial.print(F("{\"t\":\"sweep\",\"src\":\"wifi\",\"n\":"));
  Serial.print(found);
  Serial.print(F(",\"ms\":"));
  Serial.print(millis() - started);
  Serial.println('}');
}
#endif

/* --------------------------------------------------------------- BLE ----- */

#if AEGIS_ENABLE_BLE
static void sweepBle() {
  const uint32_t started = millis();

  // WiFi and BLE share the one ESP32-S3 and will not run concurrently, so the
  // WiFi side has to be released before BLE can come up.
#if AEGIS_ENABLE_WIFI
  WiFi.end();
  delay(150);
#endif

  if (!BLE.begin()) {
    emitError("BLE stack failed to start");
    return;
  }

  BLE.scan(true);   // duplicates on, so RSSI keeps updating

  uint16_t seen = 0;
  const uint32_t deadline = millis() + AEGIS_BLE_SCAN_MS;
  while ((int32_t)(deadline - millis()) > 0) {
    BLEDevice peripheral = BLE.available();
    if (!peripheral) continue;
    seen++;

    Serial.print(F("{\"t\":\"ble\",\"addr\":"));
    printJsonString(peripheral.address().c_str());
    Serial.print(F(",\"name\":"));
    if (peripheral.hasLocalName()) printJsonString(peripheral.localName().c_str());
    else Serial.print(F("null"));
    Serial.print(F(",\"rssi\":")); Serial.print(peripheral.rssi());
    Serial.println('}');
  }

  BLE.stopScan();
  BLE.end();
  delay(150);

  Serial.print(F("{\"t\":\"sweep\",\"src\":\"ble\",\"n\":"));
  Serial.print(seen);
  Serial.print(F(",\"ms\":"));
  Serial.print(millis() - started);
  Serial.println('}');
}
#endif

/* ---------------------------------------------------------- raw band ----- */

#if AEGIS_ENABLE_NRF24
static void sweepRf() {
  if (!nrfPresent) return;
  const uint32_t started = millis();

  for (uint8_t ch = 0; ch < AEGIS_RF_CHANNELS; ch++) {
    const uint16_t hits = nrf.sampleChannel(ch, AEGIS_RF_SAMPLES);
    if (hits == 0) continue;      // silent channels are the common case

    Serial.print(F("{\"t\":\"rf\",\"ch\":")); Serial.print(ch);
    Serial.print(F(",\"mhz\":")); Serial.print(2400 + ch);
    Serial.print(F(",\"hits\":")); Serial.print(hits);
    Serial.print(F(",\"max\":")); Serial.print(AEGIS_RF_SAMPLES);
    Serial.println('}');
  }

  Serial.print(F("{\"t\":\"sweep\",\"src\":\"rf\",\"n\":"));
  Serial.print(AEGIS_RF_CHANNELS);
  Serial.print(F(",\"ms\":"));
  Serial.print(millis() - started);
  Serial.println('}');
}
#endif

/* ------------------------------------------------------------ display ---- */

#if AEGIS_ENABLE_MATRIX
/** Bar graph of the 11 usable 2.4 GHz channels across the 12x8 matrix. */
static void drawMatrix() {
  uint8_t grid[8][12] = {{0}};
  for (uint8_t ch = 1; ch <= 11; ch++) {
    const uint8_t height = channelLoad[ch];
    for (uint8_t row = 0; row < height && row < 8; row++) {
      grid[7 - row][ch] = 1;
    }
  }
  matrix.renderBitmap(grid, 8, 12);
}
#endif

/* -------------------------------------------------------------- sweep ---- */

static void runSweep() {
  memset(channelLoad, 0, sizeof(channelLoad));

  Serial.println(F("{\"t\":\"sweep\",\"phase\":\"begin\"}"));

#if AEGIS_ENABLE_NRF24
  if (wantRf) sweepRf();
#endif
#if AEGIS_ENABLE_WIFI
  if (wantWifi) sweepWifi();
#endif
#if AEGIS_ENABLE_MATRIX
  drawMatrix();
#endif
#if AEGIS_ENABLE_BLE
  if (wantBle) sweepBle();
#endif

  Serial.println(F("{\"t\":\"sweep\",\"phase\":\"end\"}"));
  lastSweep = millis();
}

/* ----------------------------------------------------------- commands ---- */

static void handleCommand(char *line) {
  for (char *p = line; *p; p++) *p = tolower(*p);

  auto toggle = [&](const char *name, bool &flag) -> bool {
    const size_t n = strlen(name);
    if (strncmp(line, name, n) != 0) return false;
    flag = (strstr(line + n, "off") == nullptr);
    return true;
  };

  if (strcmp(line, "scan") == 0)      { runSweep(); return; }
  if (strcmp(line, "id") == 0)        { emitHello(); return; }
  if (toggle("auto", autoSweep))      { return; }
  if (toggle("wifi", wantWifi))       { return; }
  if (toggle("ble", wantBle))         { return; }
  if (toggle("rf", wantRf))           { return; }

  if (*line) emitError("unknown command");
}

static void pollSerial() {
  static char buffer[48];
  static uint8_t length = 0;

  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (length) {
        buffer[length] = '\0';
        handleCommand(buffer);
        length = 0;
      }
    } else if (length < sizeof(buffer) - 1) {
      buffer[length++] = c;
    }
  }
}

/* --------------------------------------------------------------- main ---- */

void setup() {
  Serial.begin(AEGIS_BAUD);

  // Wait briefly for a host, but never block forever — the board must still
  // run standalone on a USB charger.
  const uint32_t deadline = millis() + 3000;
  while (!Serial && (int32_t)(deadline - millis()) > 0) { }

#if AEGIS_ENABLE_MATRIX
  matrix.begin();
#endif

#if AEGIS_ENABLE_NRF24
  nrfPresent = nrf.begin();
  if (!nrfPresent) {
    emitError("no nRF24L01+ on SPI — check 3V3 supply, CE/CSN wiring and the 10uF cap");
  }
#endif

  emitHello();
  runSweep();
}

void loop() {
  pollSerial();

  if (autoSweep && (millis() - lastSweep) >= AEGIS_AUTO_INTERVAL_MS) {
    runSweep();
  }
}
