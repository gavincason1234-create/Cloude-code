import test from 'node:test';
import assert from 'node:assert/strict';

import {
  splitTerse,
  parseNmcli,
  parseNetsh,
  parseSystemProfiler,
  percentToDbm,
  dbmToPercent,
} from './parsers.js';

/* ------------------------------------------------------------ conversions --- */

test('percent/dBm conversion follows the documented linear mapping', () => {
  assert.equal(percentToDbm(0), -100);
  assert.equal(percentToDbm(100), -50);
  assert.equal(percentToDbm(50), -75);
  assert.equal(percentToDbm(null), null);
  assert.equal(percentToDbm(140), -50, 'clamps above 100');

  assert.equal(dbmToPercent(-100), 0);
  assert.equal(dbmToPercent(-50), 100);
  assert.equal(dbmToPercent(-120), 0, 'clamps below the floor');
});

/* ------------------------------------------------------------------ nmcli --- */

test('splitTerse keeps escaped colons inside a BSSID intact', () => {
  const fields = splitTerse('*:Home Net:AA\\:BB\\:CC\\:DD\\:EE\\:FF:72:6:2437 MHz:WPA2');
  assert.deepEqual(fields, [
    '*', 'Home Net', 'AA:BB:CC:DD:EE:FF', '72', '6', '2437 MHz', 'WPA2',
  ]);
});

test('splitTerse unescapes literal backslashes', () => {
  assert.deepEqual(splitTerse('a\\\\b:c'), ['a\\b', 'c']);
});

const NMCLI = [
  '*:Home Net:AA\\:BB\\:CC\\:DD\\:EE\\:FF:72:6:2437 MHz:WPA2',
  ' :Cafe WiFi:11\\:22\\:33\\:44\\:55\\:66:45:44:5220 MHz:WPA2 WPA3',
  ' :--:99\\:88\\:77\\:66\\:55\\:44:30:1:2412 MHz:--',
  ' :Six GHz AP:AB\\:CD\\:EF\\:00\\:11\\:22:88:37:6135 MHz:WPA3',
  '',
].join('\n');

test('parseNmcli normalises a full scan', () => {
  const nets = parseNmcli(NMCLI);
  assert.equal(nets.length, 4);

  assert.deepEqual(nets[0], {
    ssid: 'Home Net',
    bssid: 'aa:bb:cc:dd:ee:ff',
    signal: -64,
    quality: 72,
    channel: 6,
    frequency: 2437,
    band: '2.4 GHz',
    security: 'WPA2',
    phy: null,
    connected: true,
  });

  assert.equal(nets[1].band, '5 GHz');
  assert.equal(nets[1].connected, false);
  assert.equal(nets[1].security, 'WPA2 WPA3');
});

test('parseNmcli treats -- as hidden SSID and open security', () => {
  const hidden = parseNmcli(NMCLI)[2];
  assert.equal(hidden.ssid, null);
  assert.equal(hidden.security, 'Open');
});

test('parseNmcli recognises the 6 GHz band', () => {
  assert.equal(parseNmcli(NMCLI)[3].band, '6 GHz');
});

test('parseNmcli ignores blank input and header noise', () => {
  assert.deepEqual(parseNmcli(''), []);
  assert.deepEqual(parseNmcli('Error: NetworkManager is not running.'), []);
});

/* ------------------------------------------------------------------ netsh --- */

const NETSH = `
Interface name : Wi-Fi
There are 3 networks currently visible.

SSID 1 : Home Net
    Network type            : Infrastructure
    Authentication          : WPA2-Personal
    Encryption              : CCMP
    BSSID 1                 : aa:bb:cc:dd:ee:ff
         Signal             : 78%
         Radio type         : 802.11ac
         Band               : 5 GHz
         Channel            : 44
    BSSID 2                 : aa:bb:cc:dd:ee:00
         Signal             : 52%
         Radio type         : 802.11n
         Band               : 2.4 GHz
         Channel            : 6

SSID 2 :
    Network type            : Infrastructure
    Authentication          : Open
    Encryption              : None
    BSSID 1                 : 11:22:33:44:55:66
         Signal             : 24%
         Radio type         : 802.11g
         Band               : 2.4 GHz
         Channel            : 11
`;

test('parseNetsh splits every BSSID of a multi-radio AP', () => {
  const nets = parseNetsh(NETSH);
  assert.equal(nets.length, 3);

  assert.equal(nets[0].ssid, 'Home Net');
  assert.equal(nets[0].bssid, 'aa:bb:cc:dd:ee:ff');
  assert.equal(nets[0].quality, 78);
  assert.equal(nets[0].signal, -61);
  assert.equal(nets[0].channel, 44);
  assert.equal(nets[0].band, '5 GHz');
  assert.equal(nets[0].phy, '802.11ac');
  assert.equal(nets[0].security, 'WPA2-Personal');

  assert.equal(nets[1].ssid, 'Home Net', 'second radio keeps its SSID');
  assert.equal(nets[1].channel, 6);
});

test('parseNetsh reports a hidden SSID as null rather than empty string', () => {
  const hidden = parseNetsh(NETSH)[2];
  assert.equal(hidden.ssid, null);
  assert.equal(hidden.security, 'Open');
});

test('parseNetsh handles CRLF and the access-denied case', () => {
  assert.equal(parseNetsh(NETSH.replace(/\n/g, '\r\n')).length, 3);
  assert.deepEqual(parseNetsh('The wireless local area network interface is powered down.'), []);
});

/* ---------------------------------------------------------- system_profiler --- */

const PROFILER = {
  SPAirPortDataType: [{
    spairport_airport_interfaces: [{
      _name: 'en0',
      spairport_current_network_information: {
        _name: 'Home Net',
        spairport_network_channel: '44 (5GHz, 80MHz)',
        spairport_network_phymode: '802.11ax',
        spairport_security_mode: 'spairport_security_mode_wpa2_personal',
        spairport_signal_noise: '-52 dBm / -91 dBm',
      },
      spairport_airport_other_local_wireless_networks: [
        {
          _name: 'Cafe WiFi',
          spairport_network_channel: '6 (2GHz, 20MHz)',
          spairport_network_phymode: '802.11n',
          spairport_security_mode: 'spairport_security_mode_none',
          spairport_signal_noise: '-78 dBm / -92 dBm',
        },
        {
          _name: 'Neighbour',
          spairport_network_channel: '149 (5GHz, 160MHz)',
          spairport_network_phymode: '802.11ax',
          spairport_security_mode: 'spairport_security_mode_wpa3_personal',
          spairport_signal_noise: '-88 dBm / -90 dBm',
        },
      ],
    }],
  }],
};

test('parseSystemProfiler returns the connected network first, then the scan', () => {
  const nets = parseSystemProfiler(PROFILER);
  assert.equal(nets.length, 3);

  assert.equal(nets[0].ssid, 'Home Net');
  assert.equal(nets[0].connected, true);
  assert.equal(nets[0].signal, -52);
  assert.equal(nets[0].quality, 96);
  assert.equal(nets[0].channel, 44);
  assert.equal(nets[0].band, '5 GHz');

  assert.equal(nets[1].connected, false);
  assert.equal(nets[1].band, '2 GHz');
  assert.equal(nets[1].security, 'Open');
  assert.equal(nets[2].security, 'WPA3 personal');
});

test('parseSystemProfiler leaves BSSID null because macOS does not expose it', () => {
  for (const net of parseSystemProfiler(PROFILER)) {
    assert.equal(net.bssid, null);
  }
});

test('parseSystemProfiler accepts a JSON string and tolerates empty output', () => {
  assert.equal(parseSystemProfiler(JSON.stringify(PROFILER)).length, 3);
  assert.deepEqual(parseSystemProfiler('{"SPAirPortDataType":[]}'), []);
  assert.deepEqual(parseSystemProfiler({}), []);
});

/* ------------------------------------------------------------- cross-check --- */

test('every parser emits the same record shape', () => {
  const keys = ['ssid', 'bssid', 'signal', 'quality', 'channel', 'frequency', 'band', 'security', 'phy', 'connected'];
  const samples = [
    parseNmcli(NMCLI)[0],
    parseNetsh(NETSH)[0],
    parseSystemProfiler(PROFILER)[0],
  ];
  for (const sample of samples) {
    assert.deepEqual(Object.keys(sample).sort(), [...keys].sort());
  }
});
