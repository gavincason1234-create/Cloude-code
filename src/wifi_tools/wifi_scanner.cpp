#include "wifi_tools.h"

void WifiScanner::scan() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    Serial.println("[WiFi] Scanning...");
    int n = WiFi.scanNetworks(false, true); // async=false, hidden=true
    _count = 0;

    if (n <= 0) {
        Serial.println("[WiFi] No networks found");
        return;
    }

    for (int i = 0; i < n && _count < MAX_NETS; i++) {
        NetworkInfo& net = _nets[_count++];
        strncpy(net.ssid, WiFi.SSID(i).c_str(), 32);
        net.ssid[32] = '\0';
        memcpy(net.bssid, WiFi.BSSID(i), 6);
        net.rssi    = WiFi.RSSI(i);
        net.channel = WiFi.channel(i);
        net.auth    = (wifi_auth_mode_t)WiFi.encryptionType(i);
    }

    Serial.printf("[WiFi] Found %d networks:\n", _count);
    for (int i = 0; i < _count; i++) {
        NetworkInfo& net = _nets[i];
        const char* enc = "OPEN";
        switch (net.auth) {
            case WIFI_AUTH_WEP:          enc = "WEP";      break;
            case WIFI_AUTH_WPA_PSK:      enc = "WPA";      break;
            case WIFI_AUTH_WPA2_PSK:     enc = "WPA2";     break;
            case WIFI_AUTH_WPA_WPA2_PSK: enc = "WPA/2";    break;
            case WIFI_AUTH_WPA3_PSK:     enc = "WPA3";     break;
            case WIFI_AUTH_WPA2_ENTERPRISE: enc = "ENTPR"; break;
            default: break;
        }
        Serial.printf("  [%2d] %-32s  ch%2d  %4d dBm  %s  %02X:%02X:%02X:%02X:%02X:%02X\n",
            i, net.ssid[0] ? net.ssid : "(hidden)",
            net.channel, net.rssi, enc,
            net.bssid[0], net.bssid[1], net.bssid[2],
            net.bssid[3], net.bssid[4], net.bssid[5]);
    }

    WiFi.scanDelete();
}
