#include "rf_recon.h"
#include <string.h>
#include <stdio.h>
#include <furi_hal.h>

#define TAG "RfRecon"

// ─── SubGhz receive callback ─────────────────────────────────────────────────

static void subghz_rx_callback(SubGhzReceiver* receiver,
                                SubGhzProtocolDecoderBase* decoder,
                                void* ctx) {
    RfReconApp* app = (RfReconApp*)ctx;
    UNUSED(receiver);

    if(app->capture_count >= MAX_CAPTURES) return;

    RfCapture* cap = &app->captures[app->capture_count];
    cap->frequency = subghz_tx_rx_worker_get_frequency(app->subghz_worker);
    cap->rssi = (int8_t)subghz_tx_rx_worker_get_rssi(app->subghz_worker);

    // Protocol name
    const SubGhzProtocol* proto = subghz_protocol_decoder_base_get_protocol(decoder);
    if(proto) {
        strncpy(cap->protocol, proto->name, sizeof(cap->protocol) - 1);
    } else {
        strncpy(cap->protocol, "RAW", sizeof(cap->protocol) - 1);
    }

    // Decoded data
    subghz_protocol_decoder_base_get_string(decoder, cap->data, sizeof(cap->data));

    app->capture_count++;
    notification_message(app->notifications, &sequence_blink_cyan_100);

    // Append to log
    Storage* storage = app->storage;
    File* f = storage_file_alloc(storage);
    if(storage_file_open(f, RF_LOG_CAPTURES, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        char line[512];
        snprintf(line, sizeof(line),
            "Frequency:%lu Protocol:%s RSSI:%d Data:%s\n",
            (unsigned long)cap->frequency, cap->protocol, cap->rssi, cap->data);
        storage_file_write(f, line, strlen(line));
    }
    storage_file_close(f);
    storage_file_free(f);
}

// ─── Scan draw callback ───────────────────────────────────────────────────────

static void scan_draw_cb(Canvas* canvas, void* ctx) {
    RfReconApp* app = (RfReconApp*)ctx;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "RF Recon — Wideband Scan");

    canvas_set_font(canvas, FontSecondary);
    if(app->scanning) {
        char freq_str[32];
        uint32_t mhz = app->scan_current_hz / 1000000;
        uint32_t khz = (app->scan_current_hz % 1000000) / 1000;
        snprintf(freq_str, sizeof(freq_str), "Scanning: %lu.%03lu MHz", (unsigned long)mhz, (unsigned long)khz);
        canvas_draw_str(canvas, 2, 24, freq_str);

        if(app->scan_peak_freq) {
            char peak_str[48];
            uint32_t pmhz = app->scan_peak_freq / 1000000;
            uint32_t pkhz = (app->scan_peak_freq % 1000000) / 1000;
            snprintf(peak_str, sizeof(peak_str),
                "Peak: %lu.%03lu MHz @ %d dBm",
                (unsigned long)pmhz, (unsigned long)pkhz, app->scan_rssi_peak);
            canvas_draw_str(canvas, 2, 36, peak_str);
        }

        // Progress bar
        if(app->scan_end_hz > app->scan_start_hz) {
            uint32_t progress = (app->scan_current_hz - app->scan_start_hz) /
                                ((app->scan_end_hz - app->scan_start_hz) / 124);
            canvas_draw_box(canvas, 2, 48, progress, 8);
            canvas_draw_frame(canvas, 2, 48, 124, 8);
        }
    } else {
        canvas_draw_str(canvas, 2, 24, app->scan_status);
        if(app->scan_peak_freq) {
            char peak_str[48];
            uint32_t pmhz = app->scan_peak_freq / 1000000;
            uint32_t pkhz = (app->scan_peak_freq % 1000000) / 1000;
            snprintf(peak_str, sizeof(peak_str),
                "Best: %lu.%03lu MHz @ %d dBm",
                (unsigned long)pmhz, (unsigned long)pkhz, app->scan_rssi_peak);
            canvas_draw_str(canvas, 2, 36, peak_str);
        }
        canvas_draw_str(canvas, 2, 56, "OK=Start  Back=Menu");
    }
}

// ─── Capture list draw callback ────────────────────────────────────────────────

static void capture_draw_cb(Canvas* canvas, void* ctx) {
    RfReconApp* app = (RfReconApp*)ctx;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontPrimary);

    if(app->capture_count == 0) {
        canvas_draw_str(canvas, 2, 20, "No captures yet");
        canvas_draw_str(canvas, 2, 32, "OK=Start  Back=Menu");
        return;
    }

    char header[32];
    snprintf(header, sizeof(header), "Captures: %u", app->capture_count);
    canvas_draw_str(canvas, 0, 10, header);

    canvas_set_font(canvas, FontSecondary);
    uint8_t y = 20;
    uint8_t start = app->selected_capture > 3 ? app->selected_capture - 3 : 0;

    for(uint8_t i = start; i < app->capture_count && y < 62; i++) {
        RfCapture* cap = &app->captures[i];

        if(i == app->selected_capture) {
            canvas_draw_box(canvas, 0, y - 8, 128, 10);
            canvas_set_color(canvas, ColorWhite);
        }

        char line[48];
        uint32_t mhz = cap->frequency / 1000000;
        snprintf(line, sizeof(line), "%lu MHz %-8.8s %d dBm",
            (unsigned long)mhz, cap->protocol, cap->rssi);
        canvas_draw_str(canvas, 2, y, line);

        if(i == app->selected_capture) canvas_set_color(canvas, ColorBlack);
        y += 10;
    }
}

// ─── Wideband scan (runs in main thread via view input callback) ──────────────

static void run_wideband_scan(RfReconApp* app) {
    app->scanning = true;
    app->scan_rssi_peak = -120;
    app->scan_peak_freq = 0;

    // ISM bands: 300–350, 430–440, 860–870, 900–930 MHz
    uint32_t bands[][2] = {
        {300000000UL, 350000000UL},
        {430000000UL, 440000000UL},
        {860000000UL, 870000000UL},
        {900000000UL, 930000000UL},
    };

    File* scan_log = storage_file_alloc(app->storage);
    storage_file_open(scan_log, RF_LOG_SCAN, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    storage_file_write(scan_log, "frequency_hz,rssi_dbm\n", 22);

    for(size_t b = 0; b < 4; b++) {
        app->scan_start_hz = bands[b][0];
        app->scan_end_hz = bands[b][1];

        for(uint32_t freq = bands[b][0]; freq <= bands[b][1]; freq += RF_SCAN_STEP_HZ) {
            app->scan_current_hz = freq;

            if(furi_hal_subghz_is_frequency_valid(freq)) {
                furi_hal_subghz_set_frequency_and_path(freq);
                furi_hal_subghz_rx();
                furi_delay_ms(RF_SCAN_DWELL_MS);

                int rssi = (int)furi_hal_subghz_get_rssi();
                if(rssi > app->scan_rssi_peak) {
                    app->scan_rssi_peak = (int8_t)rssi;
                    app->scan_peak_freq = freq;
                }

                char csv[48];
                snprintf(csv, sizeof(csv), "%lu,%d\n", (unsigned long)freq, rssi);
                storage_file_write(scan_log, csv, strlen(csv));
            }
        }
    }

    storage_file_close(scan_log);
    storage_file_free(scan_log);

    app->scanning = false;
    snprintf(app->scan_status, sizeof(app->scan_status),
        "Done. %d captures logged.", app->capture_count);
}

// ─── Replay selected capture ─────────────────────────────────────────────────

static void replay_capture(RfReconApp* app) {
    if(app->capture_count == 0) return;
    RfCapture* cap = &app->captures[app->selected_capture];

    // For RAW captures we replay; for decoded protocols we re-encode
    for(uint8_t i = 0; i < app->replay_count; i++) {
        // Tune to capture frequency
        if(furi_hal_subghz_is_frequency_valid(cap->frequency)) {
            furi_hal_subghz_set_frequency_and_path(cap->frequency);
        }

        SubGhzTransmitter* tx = subghz_transmitter_alloc_init(
            subghz_tx_rx_worker_get_environment(app->subghz_worker),
            cap->protocol);

        if(tx) {
            subghz_transmitter_deserialize_raw(tx, cap->data);
            furi_hal_subghz_tx();
            subghz_transmitter_transmit(tx);
            subghz_transmitter_free(tx);
            furi_hal_subghz_idle();
        }

        if(i < app->replay_count - 1) {
            furi_delay_ms(app->replay_delay_ms);
        }
    }

    notification_message(app->notifications, &sequence_blink_blue_100);
}

// ─── Menu callback ────────────────────────────────────────────────────────────

static void menu_callback(void* ctx, uint32_t index) {
    RfReconApp* app = (RfReconApp*)ctx;

    switch((RfReconMenuItem)index) {
    case RfReconMenuScan:
        view_dispatcher_switch_to_view(app->view_dispatcher, RfReconViewScanner);
        run_wideband_scan(app);
        break;

    case RfReconMenuCapture:
        app->capturing = !app->capturing;
        if(app->capturing) {
            subghz_tx_rx_worker_start(app->subghz_worker, RF_BAND_ISM_433);
            view_dispatcher_switch_to_view(app->view_dispatcher, RfReconViewCapture);
        } else {
            subghz_tx_rx_worker_stop(app->subghz_worker);
        }
        break;

    case RfReconMenuReplay:
        view_dispatcher_switch_to_view(app->view_dispatcher, RfReconViewCapture);
        replay_capture(app);
        break;

    case RfReconMenuBruteforce: {
        // Iterate through common garage/gate fixed codes at 433.92 MHz
        furi_hal_subghz_set_frequency_and_path(RF_BAND_ISM_433);
        // Bruteforce 12-bit DIP switch codes (4096 combinations)
        for(uint32_t code = 0; code < 4096; code++) {
            SubGhzTransmitter* tx = subghz_transmitter_alloc_init(
                subghz_tx_rx_worker_get_environment(app->subghz_worker),
                "Princeton");
            if(tx) {
                char key_str[32];
                snprintf(key_str, sizeof(key_str), "Key:%lu", (unsigned long)code);
                subghz_transmitter_deserialize_raw(tx, key_str);
                furi_hal_subghz_tx();
                subghz_transmitter_transmit(tx);
                subghz_transmitter_free(tx);
                furi_hal_subghz_idle();
            }
            furi_delay_ms(50);  // 50ms between attempts
        }
        break;
    }

    case RfReconMenuAnalyze:
        view_dispatcher_switch_to_view(app->view_dispatcher, RfReconViewCapture);
        break;
    }
}

static uint32_t exit_to_menu(void* ctx) {
    UNUSED(ctx);
    return RfReconViewMenu;
}

static uint32_t exit_app(void* ctx) {
    UNUSED(ctx);
    return VIEW_NONE;
}

// ─── App alloc / free ─────────────────────────────────────────────────────────

static RfReconApp* rf_recon_alloc(void) {
    RfReconApp* app = malloc(sizeof(RfReconApp));
    furi_assert(app);
    memset(app, 0, sizeof(RfReconApp));

    app->replay_count = 3;
    app->replay_delay_ms = 200;
    snprintf(app->scan_status, sizeof(app->scan_status), "Press OK to scan");

    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(app->storage, RF_LOG_DIR);

    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_enable_queue(app->view_dispatcher);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    // SubGhz worker
    app->subghz_worker = subghz_tx_rx_worker_alloc();
    app->receiver = subghz_receiver_alloc_init(
        subghz_tx_rx_worker_get_environment(app->subghz_worker));
    subghz_receiver_set_rx_callback(app->receiver, subghz_rx_callback, app);

    // Menu
    app->menu = menu_alloc();
    menu_add_item(app->menu, "Wideband Scan",  NULL, RfReconMenuScan,        menu_callback, app);
    menu_add_item(app->menu, "Capture Signal", NULL, RfReconMenuCapture,     menu_callback, app);
    menu_add_item(app->menu, "Replay Signal",  NULL, RfReconMenuReplay,      menu_callback, app);
    menu_add_item(app->menu, "Bruteforce 433", NULL, RfReconMenuBruteforce,  menu_callback, app);
    menu_add_item(app->menu, "Analyze Capture",NULL, RfReconMenuAnalyze,     menu_callback, app);
    view_set_previous_callback(menu_get_view(app->menu), exit_app);
    view_dispatcher_add_view(app->view_dispatcher, RfReconViewMenu, menu_get_view(app->menu));

    // Scanner view
    View* scan_view = view_alloc();
    view_set_context(scan_view, app);
    view_set_draw_callback(scan_view, scan_draw_cb);
    view_set_previous_callback(scan_view, exit_to_menu);
    view_dispatcher_add_view(app->view_dispatcher, RfReconViewScanner, scan_view);

    // Capture view
    View* cap_view = view_alloc();
    view_set_context(cap_view, app);
    view_set_draw_callback(cap_view, capture_draw_cb);
    view_set_previous_callback(cap_view, exit_to_menu);
    view_dispatcher_add_view(app->view_dispatcher, RfReconViewCapture, cap_view);

    return app;
}

static void rf_recon_free(RfReconApp* app) {
    furi_assert(app);

    if(app->capturing || app->scanning) {
        subghz_tx_rx_worker_stop(app->subghz_worker);
        furi_hal_subghz_idle();
    }

    view_dispatcher_remove_view(app->view_dispatcher, RfReconViewMenu);
    view_dispatcher_remove_view(app->view_dispatcher, RfReconViewScanner);
    view_dispatcher_remove_view(app->view_dispatcher, RfReconViewCapture);

    menu_free(app->menu);

    subghz_receiver_free(app->receiver);
    subghz_tx_rx_worker_free(app->subghz_worker);

    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_NOTIFICATION);

    free(app);
}

// ─── Entry point ─────────────────────────────────────────────────────────────

int32_t rf_recon_app(void* p) {
    UNUSED(p);
    RfReconApp* app = rf_recon_alloc();
    view_dispatcher_switch_to_view(app->view_dispatcher, RfReconViewMenu);
    view_dispatcher_run(app->view_dispatcher);
    rf_recon_free(app);
    return 0;
}
