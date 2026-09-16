#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/menu.h>
#include <gui/modules/text_box.h>
#include <gui/modules/variable_item_list.h>
#include <gui/modules/dialog_ex.h>
#include <gui/modules/popup.h>
#include <storage/storage.h>
#include <notification/notification_messages.h>
#include "uart_bridge.h"

#define WIFI_LOG_DIR   "/ext/wifi_arsenal"
#define WIFI_LOG_PMKID WIFI_LOG_DIR "/pmkid.hc22000"
#define WIFI_LOG_CREDS WIFI_LOG_DIR "/credentials.txt"
#define WIFI_LOG_PCAP  WIFI_LOG_DIR "/capture.log"

#define MAX_AP_COUNT   64
#define MAX_SSID_LEN   33
#define MAX_BSSID_LEN  18

typedef enum {
    WiFiArsenalViewMenu = 0,
    WiFiArsenalViewScanner,
    WiFiArsenalViewAttack,
    WiFiArsenalViewLog,
    WiFiArsenalViewSettings,
} WiFiArsenalView;

typedef enum {
    WiFiArsenalMenuScanner = 0,
    WiFiArsenalMenuDeauth,
    WiFiArsenalMenuPMKID,
    WiFiArsenalMenuEvilTwin,
    WiFiArsenalMenuBeaconSpam,
    WiFiArsenalMenuMonitor,
    WiFiArsenalMenuSettings,
} WiFiArsenalMenuItem;

typedef struct {
    char ssid[MAX_SSID_LEN];
    char bssid[MAX_BSSID_LEN];
    int8_t rssi;
    uint8_t channel;
    uint8_t enc;       // 0=Open 1=WEP 2=WPA 3=WPA2 4=WPA3
    bool selected;
} AccessPoint;

typedef struct {
    // State
    WiFiArsenalView current_view;
    AccessPoint ap_list[MAX_AP_COUNT];
    uint8_t ap_count;
    uint8_t selected_ap;
    bool esp32_connected;
    bool attack_running;

    // Modules
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Menu* menu;
    TextBox* text_box;
    VariableItemList* var_item_list;
    DialogEx* dialog;
    Popup* popup;

    // UART
    UartBridge* uart;

    // Response buffer
    char rx_buf[4096];
    size_t rx_len;
    FuriMutex* rx_mutex;

    // Storage
    Storage* storage;
    File* log_file;

    // Notification
    NotificationApp* notifications;
} WiFiArsenalApp;

int32_t wifi_arsenal_app(void* p);
