#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include <functional>

using PMKIDCallback = std::function<void(const char* pmkid,
                                         const uint8_t* bssid,
                                         const char* ssid)>;
using PacketCallback = std::function<void(const uint8_t* payload,
                                           uint32_t len,
                                           uint8_t channel)>;

class WiFiAttacks {
public:
    // Deauth
    static void deauth(const uint8_t* bssid, uint8_t channel, uint16_t count);

    // PMKID capture
    static char* capture_pmkid_targeted(const uint8_t* bssid,
                                         uint8_t channel,
                                         uint32_t timeout_ms);
    static void  capture_pmkid_passive(uint32_t duration_ms,
                                        PMKIDCallback cb);

    // Evil Twin
    static void start_evil_twin(const char* ssid, uint8_t channel);
    static void evil_twin_tick();

    // Beacon spam
    static void beacon_spam_targeted(const char* ssid, uint16_t count);
    static void beacon_spam_random(uint16_t count);

    // Monitor mode
    static void start_monitor();
    static void stop_monitor();
    static void monitor_tick();

    // Stop everything
    static void stop_all();

private:
    static void promiscuous_cb(void* buf, wifi_promiscuous_pkt_type_t type);
    static void build_deauth_frame(uint8_t* frame, const uint8_t* bssid,
                                    const uint8_t* client, uint16_t reason);
    static void build_beacon_frame(uint8_t* frame, size_t* len,
                                    const char* ssid, const uint8_t* bssid,
                                    uint8_t channel);
    static bool extract_pmkid(const uint8_t* payload, uint32_t len,
                               uint8_t* pmkid_out, uint8_t* bssid_out);

    static volatile bool monitor_running;
    static volatile bool evil_twin_running;
    static PMKIDCallback pmkid_cb;
    static PacketCallback packet_cb;
    static uint8_t pmkid_target_bssid[6];
    static bool pmkid_targeting;
    static char pmkid_result[65];
    static volatile bool pmkid_found;
};
