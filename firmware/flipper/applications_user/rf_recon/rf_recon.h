#pragma once

#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/menu.h>
#include <gui/modules/text_box.h>
#include <gui/modules/variable_item_list.h>
#include <lib/subghz/subghz_tx_rx_worker.h>
#include <lib/subghz/protocols/registry.h>
#include <lib/subghz/receiver.h>
#include <lib/subghz/transmitter.h>
#include <storage/storage.h>
#include <notification/notification_messages.h>

#define RF_LOG_DIR       "/ext/rf_recon"
#define RF_LOG_CAPTURES  RF_LOG_DIR "/captures.sub"
#define RF_LOG_SCAN      RF_LOG_DIR "/scan.csv"

#define RF_SCAN_STEP_HZ  50000   // 50 kHz steps for wideband scan
#define RF_SCAN_DWELL_MS 80      // dwell per frequency

// Preset frequency bands for quick scanning
#define RF_BAND_ISM_315  315000000UL
#define RF_BAND_ISM_433  433920000UL
#define RF_BAND_ISM_868  868350000UL
#define RF_BAND_ISM_915  915000000UL
#define RF_BAND_TIRE_315 314980000UL  // TPMS

typedef enum {
    RfReconViewMenu = 0,
    RfReconViewScanner,
    RfReconViewCapture,
    RfReconViewReplay,
    RfReconViewAnalyze,
} RfReconView;

typedef enum {
    RfReconMenuScan = 0,
    RfReconMenuCapture,
    RfReconMenuReplay,
    RfReconMenuBruteforce,
    RfReconMenuAnalyze,
} RfReconMenuItem;

typedef struct {
    uint32_t frequency;
    int8_t rssi;
    char protocol[32];
    char data[256];
} RfCapture;

#define MAX_CAPTURES 32

typedef struct {
    RfReconView current_view;

    // Scan state
    uint32_t scan_start_hz;
    uint32_t scan_end_hz;
    uint32_t scan_current_hz;
    int8_t scan_rssi_peak;
    uint32_t scan_peak_freq;
    bool scanning;

    // Capture state
    RfCapture captures[MAX_CAPTURES];
    uint8_t capture_count;
    uint8_t selected_capture;
    bool capturing;

    // Replay state
    bool replaying;
    uint8_t replay_count;
    uint16_t replay_delay_ms;

    // SubGhz worker
    SubGhzTxRxWorker* subghz_worker;
    SubGhzReceiver* receiver;

    // UI
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Menu* menu;
    TextBox* text_box;
    VariableItemList* var_item_list;

    // Storage
    Storage* storage;
    NotificationApp* notifications;

    // Scan results buffer (for draw)
    char scan_status[64];
} RfReconApp;

int32_t rf_recon_app(void* p);
