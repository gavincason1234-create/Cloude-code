#include "wifi_tools.h"
#include <esp_wifi.h>

static bool _sniffRunning = false;
static uint8_t _currentCh  = 1;
static uint32_t _lastHop    = 0;

struct ProbeFrame {
    uint8_t frameCtrl[2];
    uint8_t duration[2];
    uint8_t bssid[6];
    uint8_t src[6];
    uint8_t dst[6];
    uint8_t seqCtrl[2];
    // variable: SSID IE follows
};

void ProbeSniffer::_promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;

    const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
    const uint8_t* payload = pkt->payload;
    int len = pkt->rx_ctrl.sig_len;

    if (len < 24) return;

    // Frame control: subtype probe request = 0x40
    uint8_t fc = payload[0];
    uint8_t subtype = (fc >> 4) & 0x0F;
    uint8_t ftype   = (fc >> 2) & 0x03;
    if (ftype != 0 || subtype != 4) return; // only probe requests

    const uint8_t* src = payload + 10; // SA field

    // Parse SSID IE (tag 0, at offset 24)
    char ssid[33] = "(wildcard)";
    if (len > 26 && payload[24] == 0x00) {
        uint8_t ssidLen = payload[25];
        if (ssidLen > 0 && ssidLen <= 32 && len > 26 + ssidLen) {
            memcpy(ssid, payload + 26, ssidLen);
            ssid[ssidLen] = '\0';
        }
    }

    int8_t rssi = pkt->rx_ctrl.rssi;
    Serial.printf("[Probe] %02X:%02X:%02X:%02X:%02X:%02X  SSID='%s'  RSSI=%d\n",
                  src[0],src[1],src[2],src[3],src[4],src[5],
                  ssid, rssi);
}

void ProbeSniffer::start(uint8_t channel) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_start();

    wifi_promiscuous_filter_t f = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT };
    esp_wifi_set_promiscuous_filter(&f);
    esp_wifi_set_promiscuous_rx_cb(_promiscuousCb);
    esp_wifi_set_promiscuous(true);

    if (channel > 0) {
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        _currentCh = channel;
    }

    _sniffRunning = true;
    _running      = true;
    _lastHop      = millis();
    Serial.println("[Probe] Sniffer started — watching for probe requests");
}

void ProbeSniffer::stop() {
    esp_wifi_set_promiscuous(false);
    _sniffRunning = false;
    _running      = false;
    Serial.println("[Probe] Sniffer stopped");
}
