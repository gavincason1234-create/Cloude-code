# AEGIS scanner firmware — Arduino UNO R4

Turns a stacked UNO R4 into a 2.4 GHz scanner that streams results to the AEGIS
web app over USB serial.

This is the piece that closes the gap in the main project. A browser is not
allowed to scan RF, and a native shell can only ask the host OS what its own
WiFi adapter sees. The board has its own radios, so it reports the band
directly — and because the app reads it with **Web Serial**, the plain browser
build gets real 2.4 GHz scanning with no Electron involved.

**Receive only.** This firmware never transmits, never associates, and never
deauthenticates. The nRF24's transmit path is never enabled; it is configured
purely as a carrier detector.

## The stack

```
     ┌──────────────────────────┐
     │  nRF24L01+  (2.4 GHz)    │   raw band energy, 126 × 1 MHz channels
     ├──────────────────────────┤
     │  proto / shield          │   D9, D10, D11-D13, 3V3, GND
     ├──────────────────────────┤
     │  Arduino UNO R4 WiFi     │   ESP32-S3 radio: WiFi AP scan + BLE
     └──────────┬───────────────┘
                │ USB-C
           host running AEGIS
```

| Board | WiFi scan | BLE scan | Raw band sweep |
| --- | --- | --- | --- |
| UNO R4 WiFi | ✅ ESP32-S3 | ✅ ESP32-S3 | ✅ with nRF24L01+ |
| UNO R4 Minima | ❌ no radio | ❌ no radio | ✅ with nRF24L01+ |

The sketch detects the board at compile time, so the same code builds for both —
the Minima build simply omits the radio sources (55 KB vs 95 KB of flash).

## Wiring the nRF24L01+

```
nRF24L01+        UNO R4
─────────        ──────
VCC        →     3V3       ← NOT 5V. Module supply is 1.9-3.6 V.
GND        →     GND
CE         →     D9
CSN        →     D10
SCK        →     D13
MOSI       →     D11
MISO       →     D12
```

The module's **digital inputs are 5 V tolerant**, so the signal lines need no
level shifter — only the supply rail must be 3.3 V.

**Put 10 µF across VCC/GND right at the module.** A bare nRF24L01+ browning out
under scan current is the single most common cause of "it reads all zeros", and
the 3V3 pin's inrush behaviour makes it likely without the cap. If the firmware
prints `no nRF24L01+ on SPI`, check this before anything else — the driver
probes by writing a register and reading it back, so a floating or
under-powered module fails the check rather than silently returning garbage.

## Flashing

```bash
arduino-cli core install arduino:renesas_uno
arduino-cli lib install ArduinoBLE            # only needed for the R4 WiFi

arduino-cli compile --fqbn arduino:renesas_uno:unor4wifi firmware/aegis_scanner
arduino-cli upload  --fqbn arduino:renesas_uno:unor4wifi -p /dev/ttyACM0 firmware/aegis_scanner
```

For the Minima, swap `unor4wifi` for `minima`. The Arduino IDE works too — open
`firmware/aegis_scanner/aegis_scanner.ino` and select the board.

## Connecting it to AEGIS

1. Flash the board and plug it in.
2. Open AEGIS (`python3 serve.py`, or `npm start`).
3. Enable **Arduino 2.4 GHz Scanner** and pick the board in the browser's port
   chooser.

Access points, BLE devices and raw-band channels all plot on the radar together.
Web Serial needs Chrome, Edge or Opera — Firefox and Safari have not shipped it.

## What each source actually measures

**WiFi** and **BLE** come from the ESP32-S3 and are real receivers: the RSSI
figures are genuine dBm.

**Raw band sweep** is not a receiver. The nRF24's Received Power Detector is a
one-bit comparator that latches when a channel exceeds roughly **-64 dBm**. The
firmware parks on each 1 MHz channel, samples the RPD 48 times, and reports the
hit count. So `41/48` means "carrier above -64 dBm on 85% of samples" — an
occupancy figure, not a power measurement. The app maps occupancy onto the
radar's dBm scale so these plot sensibly beside real RSSI readings, and labels
them as occupancy so the distinction survives.

That limitation aside, this source sees things the other two cannot: Zigbee,
wireless mice and keyboards, video senders, cordless phones, a microwave oven
with a tired door seal. Anything radiating in the band shows up, whether or not
it ever announces itself.

## Why a sweep takes a few seconds

On the R4 WiFi, **WiFi and BLE share the single ESP32-S3 and cannot run at the
same time.** The sketch runs the raw sweep first, then WiFi, then tears the WiFi
stack down (`WiFi.end()`) before bringing up BLE, and shuts BLE down again
afterwards. Those transitions are most of the elapsed time. Set
`AEGIS_ENABLE_BLE` to `0` in `config.h` for roughly 2× faster sweeps if you only
care about WiFi.

## Serial protocol

Newline-delimited JSON at **115200 baud**. One object per line.

```jsonc
{"t":"hello","fw":"aegis-scanner/1.0","board":"uno_r4_wifi","caps":["wifi","ble","rf"],"radio":"0.4.1"}
{"t":"sweep","phase":"begin"}
{"t":"rf","ch":12,"mhz":2412,"hits":41,"max":48}
{"t":"wifi","ssid":"Home Net","bssid":"aa:bb:cc:dd:ee:ff","rssi":-52,"ch":6,"mhz":2437,"enc":"WPA2"}
{"t":"ble","addr":"c4:2f:19:aa:bb:cc","name":"Pixel Buds","rssi":-70}
{"t":"sweep","src":"wifi","n":3,"ms":2100}
{"t":"err","msg":"no nRF24L01+ on SPI"}
{"t":"sweep","phase":"end"}
```

SSIDs are attacker-controlled bytes, so the firmware escapes them properly —
quotes, backslashes and control characters are all `\u`-escaped rather than
emitted raw, which would otherwise let a crafted SSID break the host's
`JSON.parse` for the whole line.

### Commands

Send a line to the board:

| Command | Effect |
| --- | --- |
| `scan` | Run one sweep immediately |
| `id` | Re-emit the `hello` record |
| `auto off` / `auto on` | Stop or resume automatic sweeps |
| `wifi off` / `wifi on` | Toggle the WiFi source |
| `ble off` / `ble on` | Toggle the BLE source |
| `rf off` / `rf on` | Toggle the raw band sweep |

Any serial monitor works for poking at it by hand — 115200 baud, newline
endings.

## Tuning

Everything lives in `config.h`:

| Setting | Default | Notes |
| --- | --- | --- |
| `AEGIS_RF_SAMPLES` | 48 | Samples per channel. Higher is smoother and slower. |
| `AEGIS_RF_CHANNELS` | 126 | Full band. Drop to ~85 to skip above 2485 MHz. |
| `AEGIS_BLE_SCAN_MS` | 4000 | BLE listen window. |
| `AEGIS_AUTO_INTERVAL_MS` | 8000 | Idle time between sweeps. |
| `AEGIS_ENABLE_*` | auto | Compile out any source. |

## Verification status

Both targets compile clean with `--warnings all` (no warnings from any sketch
file):

| Target | Flash | RAM |
| --- | --- | --- |
| `arduino:renesas_uno:unor4wifi` | 94,876 B (36%) | 9,908 B (30%) |
| `arduino:renesas_uno:minima` | 55,360 B (21%) | 4,632 B (14%) |

The host side — line framing across split serial chunks, JSON escaping,
occupancy thresholding, channel-overlap maths — is tested against a mock port
that replays firmware output in awkward 37-byte chunks.

**Not verified on hardware.** No UNO R4 or nRF24L01+ was available to run this
against. The register sequence follows the nRF24L01+ datasheet and the WiFiS3
calls match the current `ArduinoCore-renesas` headers, but first light on a real
board is still first light — if a field comes back wrong, the serial output is
plain text and easy to paste back for a fix.
