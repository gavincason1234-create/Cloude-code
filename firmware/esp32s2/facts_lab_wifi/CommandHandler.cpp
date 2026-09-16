#include "CommandHandler.h"
#include "WiFiAttacks.h"
#include <esp_wifi.h>
#include <string.h>
#include <stdio.h>

CommandHandler handler;

// ─── JSON helpers ─────────────────────────────────────────────────────────────

const char* CommandHandler::json_str(const char* json, const char* key,
                                      char* out, size_t out_len) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":\"", key);
    const char* pos = strstr(json, search);
    if(!pos) { out[0] = '\0'; return nullptr; }
    pos += strlen(search);
    const char* end = strchr(pos, '"');
    if(!end) { out[0] = '\0'; return nullptr; }
    size_t len = (size_t)(end - pos);
    if(len >= out_len) len = out_len - 1;
    memcpy(out, pos, len);
    out[len] = '\0';
    return out;
}

int CommandHandler::json_int(const char* json, const char* key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char* pos = strstr(json, search);
    if(!pos) return 0;
    return atoi(pos + strlen(search));
}

bool CommandHandler::json_bool(const char* json, const char* key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":true", key);
    return strstr(json, search) != nullptr;
}

bool CommandHandler::parse_bssid(const char* str, uint8_t* out) {
    if(!str || strlen(str) < 17) return false;
    return sscanf(str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
        &out[0], &out[1], &out[2], &out[3], &out[4], &out[5]) == 6;
}

// ─── Output helpers ───────────────────────────────────────────────────────────

void CommandHandler::send_json(const char* json) {
    Serial.println(json);
}

void CommandHandler::send_ok(const char* data) {
    if(data) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "{\"status\":\"ok\",\"data\":%s}", data);
        send_json(buf);
    } else {
        send_json("{\"status\":\"ok\"}");
    }
}

void CommandHandler::send_error(const char* msg) {
    char buf[256];
    snprintf(buf, sizeof(buf), "{\"status\":\"error\",\"msg\":\"%s\"}", msg);
    send_json(buf);
}

// ─── Command handlers ─────────────────────────────────────────────────────────

void CommandHandler::cmd_ping() {
    send_json("{\"status\":\"ready\",\"fw\":\"facts_lab_wifi_v1\"}");
}

void CommandHandler::cmd_scan() {
    state = STATE_SCANNING;
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    int n = WiFi.scanNetworks(false, true); // hidden SSIDs too
    ap_count = 0;

    String json_arr = "[";
    for(int i = 0; i < n && ap_count < MAX_APS; i++) {
        APRecord* ap = &ap_list[ap_count];

        strncpy(ap->ssid, WiFi.SSID(i).c_str(), 32);
        ap->ssid[32] = '\0';
        ap->rssi = WiFi.RSSI(i);
        ap->channel = WiFi.channel(i);
        ap->enc = (uint8_t)WiFi.encryptionType(i);

        uint8_t* bssid = WiFi.BSSID(i);
        memcpy(ap->bssid, bssid, 6);

        char bssid_str[18];
        snprintf(bssid_str, sizeof(bssid_str),
            "%02x:%02x:%02x:%02x:%02x:%02x",
            bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);

        char entry[256];
        // Escape SSID for JSON
        char safe_ssid[65];
        size_t j = 0, k = 0;
        while(ap->ssid[k] && j < 63) {
            if(ap->ssid[k] == '"' || ap->ssid[k] == '\\') safe_ssid[j++] = '\\';
            safe_ssid[j++] = ap->ssid[k++];
        }
        safe_ssid[j] = '\0';

        snprintf(entry, sizeof(entry),
            "{\"ssid\":\"%s\",\"bssid\":\"%s\",\"rssi\":%d,\"ch\":%u,\"enc\":%u}",
            safe_ssid, bssid_str, ap->rssi, ap->channel, ap->enc);

        if(ap_count > 0) json_arr += ",";
        json_arr += entry;
        ap_count++;
    }
    json_arr += "]";

    WiFi.scanDelete();
    state = STATE_IDLE;

    send_ok(json_arr.c_str());
}

void CommandHandler::cmd_deauth(const char* bssid_str, bool continuous) {
    if(!parse_bssid(bssid_str, target_bssid)) {
        send_error("invalid bssid");
        return;
    }

    // Find channel from scan results
    target_channel = 1;
    for(uint8_t i = 0; i < ap_count; i++) {
        if(memcmp(ap_list[i].bssid, target_bssid, 6) == 0) {
            target_channel = ap_list[i].channel;
            break;
        }
    }

    state = STATE_DEAUTH;
    WiFiAttacks::deauth(target_bssid, target_channel, continuous ? 100 : 5);
    state = STATE_IDLE;
    send_ok();
}

void CommandHandler::cmd_pmkid(const char* bssid_str) {
    state = STATE_PMKID;

    if(bssid_str && strlen(bssid_str) == 17) {
        parse_bssid(bssid_str, target_bssid);
        // Targeted PMKID capture for one AP
        char* pmkid = WiFiAttacks::capture_pmkid_targeted(target_bssid, target_channel, 10000);
        if(pmkid) {
            char resp[512];
            char bssid_out[18];
            snprintf(bssid_out, sizeof(bssid_out),
                "%02x:%02x:%02x:%02x:%02x:%02x",
                target_bssid[0], target_bssid[1], target_bssid[2],
                target_bssid[3], target_bssid[4], target_bssid[5]);
            snprintf(resp, sizeof(resp),
                "{\"pmkid\":\"%s\",\"bssid\":\"%s\",\"ssid\":\"\"}",
                pmkid, bssid_out);
            send_ok(resp);
            free(pmkid);
        } else {
            send_error("pmkid not captured");
        }
    } else {
        // Passive: capture from all visible APs for 30 seconds
        WiFiAttacks::capture_pmkid_passive(30000, [](const char* pmkid,
                                                      const uint8_t* bssid,
                                                      const char* ssid) {
            char bssid_str[18];
            snprintf(bssid_str, sizeof(bssid_str),
                "%02x:%02x:%02x:%02x:%02x:%02x",
                bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
            char resp[512];
            snprintf(resp, sizeof(resp),
                "{\"pmkid\":\"%s\",\"bssid\":\"%s\",\"ssid\":\"%s\"}",
                pmkid, bssid_str, ssid);
            Serial.println(resp);
        });
    }

    state = STATE_IDLE;
}

void CommandHandler::cmd_evil_twin(const char* ssid, uint8_t channel) {
    strncpy(target_ssid, ssid, 32);
    target_ssid[32] = '\0';
    target_channel = channel;
    state = STATE_EVILTWIN;
    WiFiAttacks::start_evil_twin(ssid, channel);
    send_ok();
}

void CommandHandler::cmd_beacon(const char* ssid) {
    state = STATE_BEACON;
    if(ssid && ssid[0]) {
        WiFiAttacks::beacon_spam_targeted(ssid, 50);
    } else {
        WiFiAttacks::beacon_spam_random(50);
    }
    state = STATE_IDLE;
    send_ok();
}

void CommandHandler::cmd_monitor(bool enable) {
    if(enable) {
        state = STATE_MONITOR;
        WiFiAttacks::start_monitor();
    } else {
        WiFiAttacks::stop_monitor();
        state = STATE_IDLE;
    }
    send_ok();
}

void CommandHandler::cmd_stop() {
    WiFiAttacks::stop_all();
    state = STATE_IDLE;
    send_ok();
}

// ─── Command dispatch ─────────────────────────────────────────────────────────

void CommandHandler::process_command(const char* json) {
    char cmd[32] = {0};
    json_str(json, "cmd", cmd, sizeof(cmd));

    if(strcmp(cmd, "ping") == 0) {
        cmd_ping();
    } else if(strcmp(cmd, "scan") == 0) {
        cmd_scan();
    } else if(strcmp(cmd, "deauth") == 0) {
        char bssid[18] = {0};
        json_str(json, "bssid", bssid, sizeof(bssid));
        bool cont = json_bool(json, "continuous");
        cmd_deauth(bssid, cont);
    } else if(strcmp(cmd, "pmkid") == 0) {
        char bssid[18] = {0};
        json_str(json, "bssid", bssid, sizeof(bssid));
        cmd_pmkid(bssid[0] ? bssid : nullptr);
    } else if(strcmp(cmd, "eviltwin") == 0) {
        char ssid[33] = {0};
        json_str(json, "ssid", ssid, sizeof(ssid));
        uint8_t ch = (uint8_t)json_int(json, "channel");
        cmd_evil_twin(ssid, ch ? ch : 1);
    } else if(strcmp(cmd, "beacon") == 0) {
        char ssid[33] = {0};
        json_str(json, "ssid", ssid, sizeof(ssid));
        cmd_beacon(ssid[0] ? ssid : nullptr);
    } else if(strcmp(cmd, "monitor") == 0) {
        cmd_monitor(json_bool(json, "enable"));
    } else if(strcmp(cmd, "stop") == 0) {
        cmd_stop();
    } else {
        send_error("unknown command");
    }
}

// ─── Main loop ────────────────────────────────────────────────────────────────

void CommandHandler::begin() {
    Serial.begin(UART_BAUD);
    WiFi.mode(WIFI_STA);
    // Send ready beacon so Flipper knows we're alive
    delay(500);
    send_json("{\"status\":\"ready\",\"fw\":\"facts_lab_wifi_v1\"}");
}

void CommandHandler::loop() {
    // Pass monitor callbacks through if active
    if(state == STATE_MONITOR) {
        WiFiAttacks::monitor_tick();
    }
    if(state == STATE_EVILTWIN) {
        WiFiAttacks::evil_twin_tick();
    }

    // Read incoming bytes
    while(Serial.available()) {
        char c = (char)Serial.read();
        if(c == '\n' || c == '\r') {
            if(cmd_len > 0) {
                cmd_buf[cmd_len] = '\0';
                process_command(cmd_buf);
                cmd_len = 0;
            }
        } else if(cmd_len < CMD_BUF_SIZE - 1) {
            cmd_buf[cmd_len++] = c;
        }
    }
}
