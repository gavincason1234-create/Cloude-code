#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"

#define UART_BAUD     115200
#define CMD_BUF_SIZE  512
#define MAX_APS       64

// Attack state flags
#define STATE_IDLE      0
#define STATE_SCANNING  1
#define STATE_DEAUTH    2
#define STATE_EVILTWIN  3
#define STATE_PMKID     4
#define STATE_BEACON    5
#define STATE_MONITOR   6

struct APRecord {
    uint8_t  bssid[6];
    char     ssid[33];
    int8_t   rssi;
    uint8_t  channel;
    uint8_t  enc;         // 0=Open 1=WEP 2=WPA 3=WPA2 4=WPA3
};

class CommandHandler {
public:
    void begin();
    void loop();

private:
    char cmd_buf[CMD_BUF_SIZE];
    size_t cmd_len = 0;
    uint8_t state = STATE_IDLE;
    APRecord ap_list[MAX_APS];
    uint8_t ap_count = 0;
    char target_ssid[33];
    uint8_t target_bssid[6];
    uint8_t target_channel;

    void process_command(const char* json);
    void cmd_ping();
    void cmd_scan();
    void cmd_deauth(const char* bssid, bool continuous);
    void cmd_pmkid(const char* bssid);
    void cmd_evil_twin(const char* ssid, uint8_t channel);
    void cmd_beacon(const char* ssid);
    void cmd_monitor(bool enable);
    void cmd_stop();

    void send_json(const char* json);
    void send_ok(const char* data = nullptr);
    void send_error(const char* msg);

    bool parse_bssid(const char* str, uint8_t* out);
    const char* json_str(const char* json, const char* key, char* out, size_t out_len);
    int json_int(const char* json, const char* key);
    bool json_bool(const char* json, const char* key);
};

extern CommandHandler handler;
