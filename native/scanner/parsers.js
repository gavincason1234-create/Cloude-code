/**
 * Pure parsers for platform WiFi scan output.
 *
 * Kept free of child_process so they can be tested against captured fixtures
 * on any machine, including CI runners with no wireless adapter.
 *
 * Every parser returns the same record shape:
 *   { ssid, bssid, signal, quality, channel, band, frequency, security, phy }
 * where `signal` is dBm and `quality` is 0-100. Fields a platform does not
 * expose are null rather than guessed.
 */

/**
 * Windows and NetworkManager both report a 0-100 quality percentage. The
 * mapping Microsoft documents for WLAN_SIGNAL_QUALITY is linear from
 * -100 dBm (0%) to -50 dBm (100%), which is also what nmcli's bars imply.
 */
export function percentToDbm(percent) {
  if (percent == null || Number.isNaN(percent)) return null;
  const p = Math.max(0, Math.min(100, percent));
  return Math.round(p / 2 - 100);
}

export function dbmToPercent(dbm) {
  if (dbm == null || Number.isNaN(dbm)) return null;
  const d = Math.max(-100, Math.min(-50, dbm));
  return Math.round((d + 100) * 2);
}

function bandFromFrequency(mhz) {
  if (mhz == null) return null;
  if (mhz >= 5925) return '6 GHz';
  if (mhz >= 4900) return '5 GHz';
  if (mhz >= 2400) return '2.4 GHz';
  return null;
}

/* ------------------------------------------------------------------ Linux --- */

/**
 * Split one line of `nmcli -t` output.
 *
 * Terse mode is colon-separated *and* BSSIDs contain colons, which nmcli
 * escapes as `\:`. Splitting on a bare `:` shreds every MAC address, so walk
 * the string and honour the escapes.
 */
export function splitTerse(line) {
  const fields = [];
  let current = '';
  for (let i = 0; i < line.length; i++) {
    const ch = line[i];
    if (ch === '\\' && (line[i + 1] === ':' || line[i + 1] === '\\')) {
      current += line[i + 1];
      i++;
    } else if (ch === ':') {
      fields.push(current);
      current = '';
    } else {
      current += ch;
    }
  }
  fields.push(current);
  return fields;
}

/**
 * Parse `nmcli -t -f IN-USE,SSID,BSSID,SIGNAL,CHAN,FREQ,SECURITY dev wifi list`.
 */
export function parseNmcli(stdout) {
  const networks = [];
  for (const raw of stdout.split('\n')) {
    const line = raw.trimEnd();
    if (!line) continue;

    const [inUse, ssid, bssid, signal, chan, freq, security] = splitTerse(line);
    if (bssid == null) continue;                 // not a data row
    if (!/^[0-9A-Fa-f:]{11,}$/.test(bssid)) continue;

    const quality = signal === '' ? null : Number.parseInt(signal, 10);
    const frequency = freq ? Number.parseInt(freq, 10) : null;

    networks.push({
      ssid: !ssid || ssid === '--' ? null : ssid,
      bssid: bssid.toLowerCase(),
      signal: percentToDbm(quality),
      quality,
      channel: chan ? Number.parseInt(chan, 10) : null,
      frequency: Number.isNaN(frequency) ? null : frequency,
      band: bandFromFrequency(frequency),
      security: !security || security === '--' ? 'Open' : security,
      phy: null,
      connected: inUse.trim() === '*',
    });
  }
  return networks;
}

/* ---------------------------------------------------------------- Windows --- */

/**
 * Parse `netsh wlan show networks mode=bssid`.
 *
 * One SSID block can carry several BSSIDs; each becomes its own record so a
 * mesh or multi-radio AP shows every radio. Labels are localised by Windows,
 * so anchoring is done on the structural `SSID n :` / `BSSID n :` markers and
 * the value shapes rather than on English words alone.
 */
export function parseNetsh(stdout) {
  const networks = [];
  let ssidBlock = null;
  let current = null;

  const push = () => {
    if (current) networks.push(current);
    current = null;
  };

  for (const raw of stdout.split(/\r?\n/)) {
    const line = raw.trim();
    if (!line) continue;

    const ssidMatch = line.match(/^SSID\s+\d+\s*:\s*(.*)$/i);
    if (ssidMatch) {
      push();
      ssidBlock = { ssid: ssidMatch[1].trim() || null, security: null, phy: null };
      continue;
    }

    const bssidMatch = line.match(/^BSSID\s+\d+\s*:\s*([0-9A-Fa-f:]{17})\s*$/i);
    if (bssidMatch) {
      push();
      current = {
        ssid: ssidBlock?.ssid ?? null,
        bssid: bssidMatch[1].toLowerCase(),
        signal: null,
        quality: null,
        channel: null,
        frequency: null,
        band: null,
        security: ssidBlock?.security ?? null,
        phy: ssidBlock?.phy ?? null,
        connected: false,
      };
      continue;
    }

    const value = line.split(/\s*:\s*/).slice(1).join(':').trim();
    if (!value) continue;

    // Percentages only ever appear on the Signal row.
    const pct = line.match(/:\s*(\d{1,3})%\s*$/);
    if (pct && current) {
      current.quality = Number.parseInt(pct[1], 10);
      current.signal = percentToDbm(current.quality);
      continue;
    }

    if (/^Channel\b/i.test(line) && current) {
      current.channel = Number.parseInt(value, 10);
      continue;
    }
    if (/^Band\b/i.test(line) && current) {
      current.band = value.replace(/\s+/g, ' ');
      continue;
    }
    if (/^Radio type\b/i.test(line)) {
      if (current) current.phy = value;
      else if (ssidBlock) ssidBlock.phy = value;
      continue;
    }
    if (/^Authentication\b/i.test(line)) {
      if (ssidBlock) ssidBlock.security = value;
      if (current) current.security = value;
      continue;
    }
  }

  push();
  return networks;
}

/* ------------------------------------------------------------------ macOS --- */

/**
 * Parse `system_profiler SPAirPortDataType -json`.
 *
 * `airport -s` printed a deprecation notice and nothing else from macOS 14.4,
 * so this reads the profiler's "other local wireless networks" list instead.
 * Apple does not surface BSSIDs here at all — the field stays null rather than
 * being invented.
 */
export function parseSystemProfiler(json) {
  const data = typeof json === 'string' ? JSON.parse(json) : json;
  const root = data?.SPAirPortDataType ?? [];
  const networks = [];

  for (const entry of root) {
    for (const iface of entry?.spairport_airport_interfaces ?? []) {
      const connected = iface?.spairport_current_network_information;
      if (connected) networks.push(fromAirportNetwork(connected, true));

      for (const net of iface?.spairport_airport_other_local_wireless_networks ?? []) {
        networks.push(fromAirportNetwork(net, false));
      }
    }
  }
  return networks;
}

function fromAirportNetwork(net, connected) {
  // "6 (2GHz, 20MHz)" / "149 (5GHz, 80MHz)"
  const channelRaw = net.spairport_network_channel ?? '';
  const channel = Number.parseInt(channelRaw, 10);
  const bandMatch = channelRaw.match(/(\d)\s*GHz/i);

  // "-62 dBm / -92 dBm"  (signal / noise)
  const signalRaw = net.spairport_signal_noise ?? '';
  const signalMatch = signalRaw.match(/(-?\d+)\s*dBm/);
  const signal = signalMatch ? Number.parseInt(signalMatch[1], 10) : null;

  return {
    ssid: net._name ?? null,
    bssid: null,                       // not exposed by system_profiler
    signal,
    quality: dbmToPercent(signal),
    channel: Number.isNaN(channel) ? null : channel,
    frequency: null,
    band: bandMatch ? `${bandMatch[1]} GHz` : null,
    security: cleanSecurityMode(net.spairport_security_mode),
    phy: net.spairport_network_phymode ?? null,
    connected,
  };
}

/** "spairport_security_mode_wpa2_personal" -> "WPA2 Personal" */
function cleanSecurityMode(mode) {
  if (!mode) return null;
  const stripped = mode.replace(/^spairport_security_mode_/, '').replace(/_/g, ' ');
  if (stripped === 'none') return 'Open';
  return stripped.replace(/\bwpa(\d?)\b/gi, (_, n) => `WPA${n}`).replace(/^\w/, (c) => c.toUpperCase());
}
