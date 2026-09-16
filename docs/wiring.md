# Hardware Wiring Guide

## ESP32-S2 Wi-Fi Developer Board

The official Flipper Zero Wi-Fi Developer Board plugs directly into the GPIO header.
No additional wiring needed — it uses the standard pinout.

```
Flipper Zero GPIO Header
┌─────────────────────────────┐
│  1  2  3  4  5  6  7  8    │
│  9 10 11 12 13 14 15 16    │
│ 17 18                       │
└─────────────────────────────┘

ESP32-S2 UART Bridge:
  GPIO 13 (PC10) → ESP32 RX
  GPIO 14 (PC11) → ESP32 TX
  3.3V / GND     → Power (board self-powered via USB-C on some revisions)

UART Settings: 115200 baud, 8N1
```

## External CC1101 Sub-GHz Board

The external CC1101 GPIO board connects via SPI on the GPIO header.

```
Flipper Zero Pin → CC1101 Pin
──────────────────────────────
GPIO  1 (5V)     → VCC (if 5V tolerant) or use 3.3V
GPIO  8 (GND)    → GND  
GPIO  2 (A7)     → GDO0 (interrupt)
GPIO  3 (A6)     → GDO2 (optional)
GPIO  4 (B3/SCK) → SCK
GPIO  5 (B2/SDA) → MOSI
GPIO  6 (B1/RXD) → MISO
GPIO  7 (A4/CS)  → CS (chip select)
3.3V             → VCC (preferred)
```

> **Note:** Pin numbers above are Flipper Zero GPIO header positions.
> Verify against your specific board's silkscreen — some multiboards
> use slightly different routing.

## CC1101 External Module Sensitivity Notes

The external CC1101 antenna typically provides:
- +5 to +10 dBm improvement in receive sensitivity
- Extended range of 2–5× vs internal antenna in open air
- Better performance at edge frequencies (300–350 MHz, 900–928 MHz)

## Simultaneous Operation

Both boards can operate simultaneously:
- ESP32-S2 uses USART1 (GPIO 13/14)
- External CC1101 uses SPI bus (GPIO 4/5/6/7)
- No bus conflicts

## Power Consumption Notes

Combined draw with both boards active:
- Flipper Zero idle: ~100 mA
- ESP32-S2 Wi-Fi active: +120–200 mA
- External CC1101 TX: +30 mA
- **Total peak: ~330–400 mA**

This is within the Flipper Zero's battery output capability but will drain
the battery faster (~2–3 hours continuous vs ~5–6 hours idle).
Use with USB power for extended operations.
