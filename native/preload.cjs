/**
 * Preload bridge.
 *
 * CommonJS on purpose: Electron loads preload scripts as CJS even when the
 * package is `"type": "module"`.
 *
 * The renderer never touches Node. It gets three narrow, non-generic calls —
 * there is no `invoke(channel, ...)` escape hatch — so a compromised page
 * cannot reach beyond scanning WiFi and answering a device chooser.
 */
const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('aegisNative', {
  version: '1.0.0',

  wifi: {
    /** @returns {Promise<{platform, tool, note, release}>} */
    backend: () => ipcRenderer.invoke('wifi:backend'),
    /** @returns {Promise<{ok:true, networks:[]}|{ok:false, code, message, hint}>} */
    scan: () => ipcRenderer.invoke('wifi:scan'),
  },

  picker: {
    /** Main asks the page to show a chooser for Bluetooth/USB/HID/Serial. */
    onShow: (handler) => {
      const listener = (_event, payload) => handler(payload);
      ipcRenderer.on('picker:show', listener);
      return () => ipcRenderer.off('picker:show', listener);
    },
    /** Empty string cancels, which the page sees as a normal NotFoundError. */
    choose: (id) => ipcRenderer.send('picker:choose', typeof id === 'string' ? id : ''),
  },
});
