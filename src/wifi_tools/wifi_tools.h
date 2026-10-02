#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>

// ─── Network info ─────────────────────────────────────────────────────────────
struct NetworkInfo {
    char     ssid[33];
    uint8_t  bssid[6];
    int32_t  rssi;
    uint8_t  channel;
    wifi_auth_mode_t auth;
};

// ─── Scanner ──────────────────────────────────────────────────────────────────
class WifiScanner {
public:
    void scan();
    int  count() const { return _count; }
    const NetworkInfo& network(int i) const { return _nets[i]; }

private:
    static const int MAX_NETS = 32;
    NetworkInfo _nets[MAX_NETS];
    int         _count = 0;
};

// ─── Deauth ──────────────────────────────────────────────────────────────────
// Educational / authorized pentesting use only.
class WifiDeauth {
public:
    // Deauthenticate a single client from a BSSID (one burst of frames)
    static void sendDeauth(const uint8_t bssid[6], const uint8_t target[6],
                           uint8_t channel, int count = 10);

    // Broadcast deauth against all clients on a BSSID
    static void broadcastDeauth(const uint8_t bssid[6], uint8_t channel,
                                int count = 10);

    static void setChannel(uint8_t ch);
};

// ─── Evil Twin ────────────────────────────────────────────────────────────────
class EvilTwin {
public:
    bool  start(const char* ssid, uint8_t channel,
                const char* pass = nullptr);
    void  stop();
    bool  isRunning() const { return _running; }

    // Returns true when a credential was captured (check capturedUser/Pass)
    bool  checkCaptures();
    char capturedUser[64];
    char capturedPass[64];

private:
    bool _running = false;
};

// ─── Probe sniffer ────────────────────────────────────────────────────────────
class ProbeSniffer {
public:
    void start(uint8_t channel = 0); // 0 = hop all channels
    void stop();
    bool isRunning() const { return _running; }

private:
    bool _running = false;
    static void _promiscuousCb(void* buf, wifi_promiscuous_pkt_type_t type);
};
