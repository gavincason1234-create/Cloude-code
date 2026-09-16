#include "uart_bridge.h"
#include <furi_hal.h>

#define TAG "UartBridge"

static int32_t uart_rx_thread(void* ctx) {
    UartBridge* bridge = (UartBridge*)ctx;
    uint8_t buf[64];

    while(bridge->running) {
        size_t len = furi_stream_buffer_receive(bridge->rx_stream, buf, sizeof(buf), 10);
        if(len > 0 && bridge->rx_cb) {
            bridge->rx_cb(buf, len, bridge->rx_cb_ctx);
        }
    }
    return 0;
}

static void uart_irq_cb(FuriHalSerialHandle* handle, FuriHalSerialRxEvent event, void* ctx) {
    UartBridge* bridge = (UartBridge*)ctx;
    if(event == FuriHalSerialRxEventData) {
        uint8_t byte = furi_hal_serial_async_rx(handle);
        furi_stream_buffer_send(bridge->rx_stream, &byte, 1, 0);
    }
}

UartBridge* uart_bridge_alloc(UartBridgeRxCallback cb, void* ctx) {
    UartBridge* bridge = malloc(sizeof(UartBridge));
    furi_assert(bridge);

    bridge->rx_cb = cb;
    bridge->rx_cb_ctx = ctx;
    bridge->running = true;

    bridge->rx_stream = furi_stream_buffer_alloc(UART_RX_BUF_SIZE, 1);

    bridge->handle = furi_hal_serial_control_acquire(FuriHalSerialIdUsart);
    furi_assert(bridge->handle);
    furi_hal_serial_init(bridge->handle, UART_BAUD);
    furi_hal_serial_async_rx_start(bridge->handle, uart_irq_cb, bridge, false);

    bridge->rx_thread = furi_thread_alloc_ex("UartRx", 1024, uart_rx_thread, bridge);
    furi_thread_set_priority(bridge->rx_thread, FuriThreadPriorityNormal);
    furi_thread_start(bridge->rx_thread);

    return bridge;
}

void uart_bridge_free(UartBridge* bridge) {
    furi_assert(bridge);

    bridge->running = false;
    furi_thread_join(bridge->rx_thread);
    furi_thread_free(bridge->rx_thread);

    furi_hal_serial_async_rx_stop(bridge->handle);
    furi_hal_serial_deinit(bridge->handle);
    furi_hal_serial_control_release(bridge->handle);

    furi_stream_buffer_free(bridge->rx_stream);
    free(bridge);
}

bool uart_bridge_send(UartBridge* bridge, const char* cmd) {
    furi_assert(bridge);
    furi_assert(cmd);
    return uart_bridge_send_raw(bridge, (const uint8_t*)cmd, strlen(cmd));
}

bool uart_bridge_send_raw(UartBridge* bridge, const uint8_t* data, size_t len) {
    furi_assert(bridge);
    furi_assert(data);
    furi_hal_serial_tx(bridge->handle, data, len);
    return true;
}
