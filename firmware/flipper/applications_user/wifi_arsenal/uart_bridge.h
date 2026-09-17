#pragma once

#include <furi.h>
#include <furi_hal_serial.h>
#include <furi_hal_serial_control.h>

#define UART_BAUD         115200
#define UART_RX_BUF_SIZE  2048
#define UART_TX_BUF_SIZE  512
#define UART_CMD_TIMEOUT  5000  // ms

typedef void (*UartBridgeRxCallback)(const uint8_t* data, size_t len, void* ctx);

typedef struct {
    FuriHalSerialHandle* handle;
    FuriStreamBuffer* rx_stream;
    FuriThread* rx_thread;
    UartBridgeRxCallback rx_cb;
    void* rx_cb_ctx;
    bool running;
} UartBridge;

UartBridge* uart_bridge_alloc(UartBridgeRxCallback cb, void* ctx);
void uart_bridge_free(UartBridge* bridge);
bool uart_bridge_send(UartBridge* bridge, const char* cmd);
bool uart_bridge_send_raw(UartBridge* bridge, const uint8_t* data, size_t len);
