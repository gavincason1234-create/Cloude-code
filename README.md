# AEGIS — Consent-Based Device Scanner

A browser-native scanner for the devices and links around you, built on standard
web platform APIs. Canvas radar plot, extruded 3D lettering, twelve pluggable
connectors, zero dependencies, zero build step.

Everything it reports comes from an API the browser gated behind a permission
prompt and a user gesture. Nothing is persisted and nothing is transmitted — no
storage, no analytics, no network calls of any kind.

## Running it

```bash
python3 serve.py          # http://localhost:8000
python3 serve.py 8123     # or pick a port
```

Any static server works (`npx serve`, `php -S`, …), but you do need one:
opening `index.html` over `file://` fails, because ES modules are blocked by
CORS and every device API requires a secure context. `http://localhost` counts
as secure; anything else needs real HTTPS.

Chromium-family browsers expose the most connectors. Firefox and Safari have
deliberately not shipped WebUSB, WebHID, Web Serial or Web Bluetooth — the app
detects this and marks those connectors unavailable rather than failing at the
prompt.

## About WiFi scanning

**No web page in any browser can enumerate nearby WiFi networks.** The
[Network Information API](https://wicg.github.io/netinfo/) is the only spec that
describes the connection at all, and its entire surface is `type`,
`effectiveType`, `downlink`, `downlinkMax`, `rtt` and `saveData` — no SSID, no
BSSID, no access point list, no scan method. Network identity is treated as
sensitive under [W3C fingerprinting guidance](https://www.w3.org/TR/fingerprinting-guidance/),
and SSIDs in particular leak [names, locations and occasionally passwords](https://link.springer.com/chapter/10.1007/978-3-031-09234-3_19).
A web app that shows you a list of nearby networks is fabricating it.

Three near-misses, none of which change the answer for a web page:

- **`chrome.networking.onc`** does real scanning — `requestNetworkScan()`,
  plus `SSID`, `BSSID`, `SignalStrength` and `Security` per network. It is
  ChromeOS-only, restricted to extensions and auto-launched kiosk sessions, and
  part of the Chrome Apps platform deprecated in 2020. Unreachable from a page.
- **Firefox OS `WifiManager.getNetworks()`** did the same for certified apps.
  That platform is archived.
- **[Neighbour Awareness Networking](https://discourse.wicg.io/t/proposal-neighbour-awareness-networking-js-api/3478/)**
  (Wi-Fi Aware) is an early WICG *draft proposal* for peer-to-peer device
  discovery — not access point scanning. Not shipped in any browser.

So the honest framing is that WiFi scanning was never specified for the web
rather than formally rejected: it is deliberately outside what the netinfo spec
chose to expose, and no vendor has shipped an equivalent.

What the platform *does* expose is the properties of the link this device is
already on, which is what the **Network Link** connector reports: bearer type
(wifi / ethernet / cellular), effective class, downlink estimate, RTT, and every
transition between them. Real data, honestly labelled. For actual airspace
surveys you need a native tool with adapter access — `nmcli dev wifi`,
`airport -s`, or the Windows `netsh wlan show networks`.

## Connectors

| Connector | API | What it reports |
| --- | --- | --- |
| Bluetooth LE Scan | `requestLEScan` | Nearby advertisements: name, RSSI, TX power, service UUIDs, manufacturer data |
| Bluetooth Device Pair | `requestDevice` + GATT | Manufacturer, model, firmware, serial, battery of a device you pick |
| Network Link | Network Information | Bearer, effective class, downlink, RTT, online transitions |
| Local Interfaces | WebRTC host candidates | This machine's interface count and mDNS-masked addresses |
| USB Devices | WebUSB | VID/PID, class codes, USB version, serial, configuration count |
| HID Devices | WebHID | Usage page/ID, report collection counts, open state |
| Serial Ports | Web Serial | Granted ports and their USB IDs — listed, never opened |
| Media Devices | `enumerateDevices` | Cameras, microphones, speakers (labels withheld until granted) |
| Geolocation | Geolocation | Lat/lon, accuracy radius, altitude, heading, speed |
| Power Source | Battery Status | Charge level, charging state, time to full/empty |
| Motion Sensors | DeviceMotion/Orientation | Accelerometer, gyroscope, orientation frame |
| Host Profile | ambient properties | CPU threads, memory class, display, GPU, locale — no prompt needed |

Bluetooth LE Scan is the one connector that needs more than a permission grant:
it is Chromium-only and sits behind
`chrome://flags/#enable-experimental-web-platform-features`. The app shows a
`FLAG NEEDED` badge when that is the case rather than silently doing nothing.

**Host Profile** is included to make a point. It reads a dozen identifying
properties with no prompt whatsoever, because that is what every page you visit
can already do.

## Layout

```
index.html              markup and panel structure
styles.css              theme, glass panels, responsive grid
serve.py                dev server (localhost = secure context)
js/
  app.js                orchestration, UI rendering, export
  text3d.js             extruded 3D text renderer
  radar.js              canvas PPI radar
  store.js              in-memory contact store
  connectors/
    base.js             Connector contract and lifecycle helpers
    bluetooth.js        LE advertisement scan, GATT device pairing
    network.js          link telemetry, local interface enumeration
    wired.js            USB, HID, Serial, media devices
    environment.js      geolocation, battery, motion, host profile
```

### The 3D text

`text3d.js` renders extruded lettering on a plain 2D canvas — no WebGL, no
libraries. Depth comes from stacking ~24 offset copies of the glyphs along a
light-aware extrusion vector with per-slice shading, topped with a gradient
face, a clipped specular sweep and a chromatic edge fringe. Yaw and pitch follow
the pointer through a critically-damped spring, so the letters read as a solid
object rather than a parallax gimmick.

### The radar

Bearing is a deterministic FNV-1a hash of the contact id, so a device keeps its
slot on the scope across re-renders. Range maps signal to distance — strong
signal plots near the centre. A contact only lights up as the sweep line crosses
its bearing and then decays, which is how a real PPI display behaves. Non-RSSI
sources (GPS accuracy, battery level, link quality) are mapped onto the same
dBm-style scale so everything shares one visual language.

## Adding a connector

Extend `Connector`, declare the static metadata, and register the class in
`js/connectors/index.js`:

```js
import { Connector } from './base.js';

export class MyConnector extends Connector {
  static id = 'mine';
  static name = 'My Connector';
  static icon = '🛰️';
  static kind = 'env';          // ble | net | usb | env — sets the radar colour
  static description = 'What it reads and what it needs.';
  static action = 'Enable';

  support() {
    return 'mySensor' in navigator
      ? { level: 'ok', note: 'ready' }
      : { level: 'no', note: 'unsupported here' };
  }

  async start() {
    // listen() and every() auto-unbind in stop()
    this.every(1000, () => this.emit({
      id: `${MyConnector.id}:thing`,
      name: 'Thing',
      identifier: 'abc',
      signal: -55,              // dBm-ish, or null
      detail: 'free-form text',
    }));
    this.active = true;
  }
}
```

`emit()` upserts by id, so repeat sightings update a row in place and extend its
signal history instead of piling up duplicates. Throwing from `start()` is the
correct way to report a denied permission — `app.js` translates the DOMException
into plain language and leaves the connector off.

## Export

**Export JSON** downloads the current contact list with first/last-seen
timestamps, hit counts, the set of active connectors and the user agent. The
file is generated from an in-memory blob and never uploaded.

## Scope and lawful use

This tool observes only what your own device is authorised to see. It does not
capture packets, intercept traffic, deanonymise anyone, or touch hardware you
have not personally granted it. Those limits are the browser's, not a policy
choice made here — which is precisely why a consent-based scanner is worth
building on the web platform.
