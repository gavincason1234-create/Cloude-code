import { Connector, isSecure } from './base.js';

/**
 * Geolocation. Prompted, revocable, and reported at whatever accuracy the
 * platform chooses to give — the app never stores or transmits a fix.
 */
export class GeoConnector extends Connector {
  static id = 'geo';
  static name = 'Geolocation';
  static icon = '📍';
  static kind = 'env';
  static description =
    'High-accuracy position watch: latitude, longitude, accuracy radius, altitude, heading and speed.';
  static action = 'Locate';

  support() {
    if (!('geolocation' in navigator)) return { level: 'no', note: 'Geolocation unavailable' };
    if (!isSecure()) return { level: 'no', note: 'needs https or localhost' };
    return { level: 'ok', note: 'ready' };
  }

  async start() {
    await new Promise((resolve, reject) => {
      let settled = false;

      const id = navigator.geolocation.watchPosition(
        (pos) => {
          const c = pos.coords;
          const detail = [
            `±${Math.round(c.accuracy)} m`,
            c.altitude != null ? `alt ${c.altitude.toFixed(1)} m` : null,
            c.heading != null && !Number.isNaN(c.heading) ? `hdg ${Math.round(c.heading)}°` : null,
            c.speed != null && !Number.isNaN(c.speed) ? `${c.speed.toFixed(1)} m/s` : null,
          ].filter(Boolean);

          this.emit({
            id: `${GeoConnector.id}:fix`,
            name: 'Position Fix',
            identifier: `${c.latitude.toFixed(5)}, ${c.longitude.toFixed(5)}`,
            signal: accuracyToRssi(c.accuracy),
            detail: detail.join(' · '),
          });

          if (!settled) { settled = true; resolve(); }
        },
        (err) => {
          if (!settled) { settled = true; reject(err); }
          else this.log('warn', `position error: ${err.message}`);
        },
        { enableHighAccuracy: true, maximumAge: 5000, timeout: 20000 },
      );

      this.onStop(() => navigator.geolocation.clearWatch(id));
    });

    this.active = true;
    this.log('ok', 'position watch running');
  }
}

/**
 * Battery Status. Chromium-only these days; Firefox and Safari removed it as a
 * fingerprinting surface, which is the correct call.
 */
export class BatteryConnector extends Connector {
  static id = 'battery';
  static name = 'Power Source';
  static icon = '🔋';
  static kind = 'env';
  static description =
    'Charge level, charging state and time-to-full/empty estimates for this device.';
  static action = 'Read';

  support() {
    if (typeof navigator.getBattery !== 'function') {
      return { level: 'no', note: 'Battery Status removed in this browser' };
    }
    return { level: 'ok', note: 'ready' };
  }

  async start() {
    const battery = await navigator.getBattery();

    const publish = () => {
      const pct = Math.round(battery.level * 100);
      const detail = [
        battery.charging ? 'charging' : 'on battery',
        battery.charging && Number.isFinite(battery.chargingTime)
          ? `full in ${fmtSeconds(battery.chargingTime)}` : null,
        !battery.charging && Number.isFinite(battery.dischargingTime)
          ? `${fmtSeconds(battery.dischargingTime)} remaining` : null,
      ].filter(Boolean);

      this.emit({
        id: `${BatteryConnector.id}:pack`,
        name: `Battery ${pct}%`,
        identifier: battery.charging ? 'AC connected' : 'DC / internal',
        signal: Math.round(-100 + battery.level * 65),
        detail: detail.join(' · '),
      });
    };

    for (const ev of ['levelchange', 'chargingchange', 'chargingtimechange', 'dischargingtimechange']) {
      this.listen(battery, ev, publish);
    }

    publish();
    this.active = true;
    this.log('ok', 'power source monitored');
  }
}

/**
 * Motion and orientation sensors. iOS requires an explicit
 * `requestPermission()` call from a user gesture; other platforms expose the
 * events directly on secure origins.
 */
export class MotionConnector extends Connector {
  static id = 'motion';
  static name = 'Motion Sensors';
  static icon = '🧿';
  static kind = 'env';
  static description =
    'Accelerometer and gyroscope feed — device orientation, rotation rate and linear acceleration.';
  static action = 'Sense';

  support() {
    if (!('DeviceMotionEvent' in window) && !('DeviceOrientationEvent' in window)) {
      return { level: 'no', note: 'no motion sensors exposed' };
    }
    if (!isSecure()) return { level: 'no', note: 'needs https or localhost' };
    return { level: 'ok', note: 'ready' };
  }

  async start() {
    for (const Ctor of [window.DeviceMotionEvent, window.DeviceOrientationEvent]) {
      if (typeof Ctor?.requestPermission === 'function') {
        const state = await Ctor.requestPermission();
        if (state !== 'granted') throw new Error(`motion permission ${state}`);
      }
    }

    let motionSeen = false;
    let orientSeen = false;
    const throttle = makeThrottle(400);

    this.listen(window, 'devicemotion', (e) => throttle(() => {
      const a = e.accelerationIncludingGravity ?? e.acceleration;
      const r = e.rotationRate;
      if (!a && !r) return;
      motionSeen = true;
      this.emit({
        id: `${MotionConnector.id}:imu`,
        name: 'Inertial Unit',
        identifier: `${Math.round(1000 / (e.interval || 16))} Hz nominal`,
        signal: null,
        detail: [
          a ? `accel ${fx(a.x)}/${fx(a.y)}/${fx(a.z)} m/s²` : null,
          r ? `gyro ${fx(r.alpha)}/${fx(r.beta)}/${fx(r.gamma)} °/s` : null,
        ].filter(Boolean).join(' · '),
      });
    }));

    this.listen(window, 'deviceorientation', (e) => throttle(() => {
      if (e.alpha == null && e.beta == null && e.gamma == null) return;
      orientSeen = true;
      this.emit({
        id: `${MotionConnector.id}:orientation`,
        name: 'Orientation',
        identifier: e.absolute ? 'absolute frame' : 'relative frame',
        signal: null,
        detail: `α ${fx(e.alpha)}° · β ${fx(e.beta)}° · γ ${fx(e.gamma)}°`,
      });
    }));

    this.active = true;
    this.log('ok', 'listening for motion events');

    setTimeout(() => {
      if (!motionSeen && !orientSeen) {
        this.log('warn', 'sensors granted but silent — this machine likely has no IMU');
      }
    }, 2500);
  }
}

/**
 * Static hardware and display profile. No permission needed — these are the
 * same properties any page can read, surfaced here so you can see exactly what
 * a site learns about your machine without asking.
 */
export class HostProfileConnector extends Connector {
  static id = 'host';
  static name = 'Host Profile';
  static icon = '🖥️';
  static kind = 'env';
  static description =
    'What any page can read with no prompt at all: CPU threads, memory class, display geometry, colour depth and platform.';
  static action = 'Read';

  support() { return { level: 'ok', note: 'ready' }; }

  async start() {
    this.publish();
    // Display geometry is the only part that moves; re-read it on resize.
    this.listen(window, 'resize', () => this.publish());

    this.active = true;
    this.log('ok', 'host profile read (no permission required — that is the point)');
  }

  publish() {
    const s = window.screen;

    this.emit({
      id: `${HostProfileConnector.id}:cpu`,
      name: 'Compute',
      identifier: `${navigator.hardwareConcurrency ?? '?'} logical cores`,
      signal: null,
      detail: [
        navigator.deviceMemory ? `≥${navigator.deviceMemory} GB RAM class` : 'memory class withheld',
        navigator.platform ? `platform ${navigator.platform}` : null,
        `${navigator.maxTouchPoints ?? 0} touch points`,
      ].filter(Boolean).join(' · '),
    });

    this.emit({
      id: `${HostProfileConnector.id}:display`,
      name: 'Display',
      identifier: `${s.width}×${s.height} @ ${window.devicePixelRatio}x`,
      signal: null,
      detail: [
        `viewport ${window.innerWidth}×${window.innerHeight}`,
        `${s.colorDepth}-bit colour`,
        `avail ${s.availWidth}×${s.availHeight}`,
        s.orientation?.type ?? null,
      ].filter(Boolean).join(' · '),
    });

    this.emit({
      id: `${HostProfileConnector.id}:locale`,
      name: 'Locale & Time',
      identifier: navigator.language ?? 'unknown',
      signal: null,
      detail: [
        Intl.DateTimeFormat().resolvedOptions().timeZone,
        `UTC${-new Date().getTimezoneOffset() / 60 >= 0 ? '+' : ''}${-new Date().getTimezoneOffset() / 60}`,
        `${navigator.languages?.length ?? 1} preferred language(s)`,
      ].join(' · '),
    });

    const gpu = readGpu();
    if (gpu) {
      this.emit({
        id: `${HostProfileConnector.id}:gpu`,
        name: 'Graphics',
        identifier: gpu.renderer,
        signal: null,
        detail: `vendor ${gpu.vendor}`,
      });
    }
  }
}

function readGpu() {
  try {
    const canvas = document.createElement('canvas');
    const gl = canvas.getContext('webgl') || canvas.getContext('experimental-webgl');
    if (!gl) return null;
    const ext = gl.getExtension('WEBGL_debug_renderer_info');
    return {
      renderer: ext ? gl.getParameter(ext.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER),
      vendor: ext ? gl.getParameter(ext.UNMASKED_VENDOR_WEBGL) : gl.getParameter(gl.VENDOR),
    };
  } catch {
    return null;
  }
}

function makeThrottle(ms) {
  let last = 0;
  return (fn) => {
    const now = performance.now();
    if (now - last < ms) return;
    last = now;
    fn();
  };
}

const fx = (n) => (n == null || Number.isNaN(n) ? '—' : n.toFixed(1));

function fmtSeconds(s) {
  if (!Number.isFinite(s) || s <= 0) return '—';
  const h = Math.floor(s / 3600);
  const m = Math.round((s % 3600) / 60);
  return h ? `${h}h ${m}m` : `${m}m`;
}

/** Map a GPS accuracy radius onto the radar's dBm-style scale. */
function accuracyToRssi(metres) {
  if (metres == null) return null;
  const clamped = Math.max(1, Math.min(2000, metres));
  return Math.round(-30 - (Math.log10(clamped) / Math.log10(2000)) * 70);
}
