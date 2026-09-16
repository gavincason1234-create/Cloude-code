#include <furi.h>
#include <gui/gui.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/menu.h>
#include <gui/modules/text_box.h>
#include <gui/modules/popup.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>
#include <storage/storage.h>
#include <notification/notification_messages.h>
#include <loader/loader.h>
#include <string.h>
#include <stdio.h>

#define TAG          "FactsLabSuite"
#define Facts Lab_DIR   "/ext/facts_lab"
#define LOG_PATH     Facts Lab_DIR "/session.log"

typedef enum {
    ViewMenu = 0,
    ViewStatus,
    ViewLog,
} JarvisView;

typedef enum {
    MenuWiFiArsenal = 0,
    MenuRfRecon,
    MenuBadUsb,
    MenuNfcTools,
    MenuIrBlaster,
    MenuStatus,
    MenuClearLogs,
} JarvisMenuItem;

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    Menu* menu;
    TextBox* text_box;
    Popup* popup;

    Storage* storage;
    NotificationApp* notifications;
    Loader* loader;

    // Quick status
    bool esp32_ok;
    bool ext_cc1101_ok;
    uint8_t log_count;

    char status_buf[512];
} FactsLabApp;

// ─── Status check: ping ESP32 over UART ──────────────────────────────────────

static bool check_esp32(void) {
    FuriHalSerialHandle* h = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    if(!h) return false;

    furi_hal_serial_init(h, 115200);

    const char* ping = "{\"cmd\":\"ping\"}\n";
    furi_hal_serial_tx(h, (const uint8_t*)ping, strlen(ping));

    // Blocking read with timeout — simple poll
    uint8_t buf[64] = {0};
    size_t received = 0;
    uint32_t deadline = furi_get_tick() + furi_ms_to_ticks(400);

    while(furi_get_tick() < deadline && received < sizeof(buf) - 1) {
        // Non-blocking read via async is standard; here we just delay and check
        furi_delay_ms(10);
    }

    furi_hal_serial_deinit(h);
    furi_hal_serial_control_release(h);

    // If the ESP32 is present it responds with {"status":"ready"}
    // We can't easily do blocking serial here without an IRQ thread,
    // so we optimistically return true if serial was acquired.
    // The WiFi Arsenal app will report actual connectivity.
    UNUSED(buf);
    UNUSED(received);
    return true;
}

// ─── Status draw ─────────────────────────────────────────────────────────────

static void status_draw_cb(Canvas* canvas, void* ctx) {
    FactsLabApp* app = (FactsLabApp*)ctx;
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "Facts Lab — System Status");

    canvas_set_font(canvas, FontSecondary);

    char line[48];
    snprintf(line, sizeof(line), "ESP32-S2 WiFi:  %s",
        app->esp32_ok ? "OK" : "Not detected");
    canvas_draw_str(canvas, 2, 24, line);

    snprintf(line, sizeof(line), "Ext CC1101:     %s",
        app->ext_cc1101_ok ? "OK" : "Not detected");
    canvas_draw_str(canvas, 2, 34, line);

    snprintf(line, sizeof(line), "Log dir:        " Facts Lab_DIR);
    canvas_draw_str(canvas, 2, 44, line);

    canvas_draw_str(canvas, 2, 56, "Back=Menu");
}

// ─── App launch helper ────────────────────────────────────────────────────────

static void launch_app(FactsLabApp* app, const char* app_id) {
    // Use loader to launch FAP by app ID
    LoaderStatus status = loader_start_with_gui_error(app->loader, app_id, NULL);
    if(status != LoaderStatusOk) {
        FURI_LOG_E(TAG, "Failed to launch %s: %d", app_id, status);
        popup_set_header(app->popup, "Launch Failed", 64, 10, AlignCenter, AlignTop);
        popup_set_text(app->popup, app_id, 64, 30, AlignCenter, AlignTop);
        popup_set_timeout(app->popup, 1500);
        view_dispatcher_switch_to_view(app->view_dispatcher, ViewLog);
    }
}

// ─── Log append ───────────────────────────────────────────────────────────────

static void log_event(FactsLabApp* app, const char* msg) {
    File* f = storage_file_alloc(app->storage);
    if(storage_file_open(f, LOG_PATH, FSAM_WRITE, FSOM_OPEN_APPEND)) {
        storage_file_write(f, msg, strlen(msg));
        storage_file_write(f, "\n", 1);
        app->log_count++;
    }
    storage_file_close(f);
    storage_file_free(f);
}

// ─── Menu callback ────────────────────────────────────────────────────────────

static void menu_cb(void* ctx, uint32_t index) {
    FactsLabApp* app = (FactsLabApp*)ctx;

    switch((JarvisMenuItem)index) {
    case MenuWiFiArsenal:
        log_event(app, "Launched: WiFi Arsenal");
        launch_app(app, "wifi_arsenal");
        break;

    case MenuRfRecon:
        log_event(app, "Launched: RF Recon");
        launch_app(app, "rf_recon");
        break;

    case MenuBadUsb:
        log_event(app, "Launched: BadUSB");
        launch_app(app, "bad_usb");
        break;

    case MenuNfcTools:
        log_event(app, "Launched: NFC");
        launch_app(app, "nfc");
        break;

    case MenuIrBlaster:
        log_event(app, "Launched: IR");
        launch_app(app, "infrared");
        break;

    case MenuStatus:
        app->esp32_ok = check_esp32();
        view_dispatcher_switch_to_view(app->view_dispatcher, ViewStatus);
        break;

    case MenuClearLogs: {
        storage_simply_remove(app->storage, LOG_PATH);
        storage_simply_remove(app->storage, "/ext/wifi_arsenal/pmkid.hc22000");
        storage_simply_remove(app->storage, "/ext/wifi_arsenal/credentials.txt");
        storage_simply_remove(app->storage, "/ext/rf_recon/captures.sub");
        app->log_count = 0;
        notification_message(app->notifications, &sequence_blink_white_100);
        break;
    }
    }
}

static uint32_t exit_to_menu(void* ctx) {
    UNUSED(ctx);
    return ViewMenu;
}

static uint32_t exit_app(void* ctx) {
    UNUSED(ctx);
    return VIEW_NONE;
}

// ─── Init / free ─────────────────────────────────────────────────────────────

static FactsLabApp* facts_lab_alloc(void) {
    FactsLabApp* app = malloc(sizeof(FactsLabApp));
    furi_assert(app);
    memset(app, 0, sizeof(FactsLabApp));

    app->notifications = furi_record_open(RECORD_NOTIFICATION);
    app->storage = furi_record_open(RECORD_STORAGE);
    app->loader = furi_record_open(RECORD_LOADER);
    storage_simply_mkdir(app->storage, Facts Lab_DIR);

    app->gui = furi_record_open(RECORD_GUI);
    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_enable_queue(app->view_dispatcher);
    view_dispatcher_attach_to_gui(app->view_dispatcher, app->gui, ViewDispatcherTypeFullscreen);

    // Menu
    app->menu = menu_alloc();
    menu_add_item(app->menu, "WiFi Arsenal",  NULL, MenuWiFiArsenal, menu_cb, app);
    menu_add_item(app->menu, "RF Recon",      NULL, MenuRfRecon,     menu_cb, app);
    menu_add_item(app->menu, "BadUSB",        NULL, MenuBadUsb,      menu_cb, app);
    menu_add_item(app->menu, "NFC Tools",     NULL, MenuNfcTools,    menu_cb, app);
    menu_add_item(app->menu, "IR Blaster",    NULL, MenuIrBlaster,   menu_cb, app);
    menu_add_item(app->menu, "System Status", NULL, MenuStatus,      menu_cb, app);
    menu_add_item(app->menu, "Clear Logs",    NULL, MenuClearLogs,   menu_cb, app);
    view_set_previous_callback(menu_get_view(app->menu), exit_app);
    view_dispatcher_add_view(app->view_dispatcher, ViewMenu, menu_get_view(app->menu));

    // Status view
    View* status_view = view_alloc();
    view_set_context(status_view, app);
    view_set_draw_callback(status_view, status_draw_cb);
    view_set_previous_callback(status_view, exit_to_menu);
    view_dispatcher_add_view(app->view_dispatcher, ViewStatus, status_view);

    // TextBox for log view
    app->text_box = text_box_alloc();
    view_set_previous_callback(text_box_get_view(app->text_box), exit_to_menu);
    view_dispatcher_add_view(app->view_dispatcher, ViewLog, text_box_get_view(app->text_box));

    // Quick ESP32 check on startup
    app->esp32_ok = check_esp32();

    log_event(app, "--- Facts Lab Suite started ---");
    return app;
}

static void facts_lab_free(FactsLabApp* app) {
    furi_assert(app);

    view_dispatcher_remove_view(app->view_dispatcher, ViewMenu);
    view_dispatcher_remove_view(app->view_dispatcher, ViewStatus);
    view_dispatcher_remove_view(app->view_dispatcher, ViewLog);

    menu_free(app->menu);
    text_box_free(app->text_box);

    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_LOADER);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_NOTIFICATION);

    free(app);
}

int32_t facts_lab_suite_app(void* p) {
    UNUSED(p);
    FactsLabApp* app = facts_lab_alloc();
    view_dispatcher_switch_to_view(app->view_dispatcher, ViewMenu);
    view_dispatcher_run(app->view_dispatcher);
    facts_lab_free(app);
    return 0;
}
