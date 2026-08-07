/**
 * Device chooser overlay for the desktop shell.
 *
 * A browser draws its own picker for `requestDevice()`; Electron does not, so
 * the app has to. This keeps the consent model intact — the page still sees
 * exactly one device, chosen by hand, and cancelling produces the same
 * NotFoundError a browser chooser would.
 *
 * No-op in a plain browser, where `aegisNative` is absent.
 */
export function installDevicePicker(log) {
  const native = globalThis.aegisNative?.picker;
  if (!native) return;

  let overlay = null;

  const close = (id) => {
    native.choose(id);
    overlay?.remove();
    overlay = null;
    document.removeEventListener('keydown', onKey);
  };

  const onKey = (e) => { if (e.key === 'Escape') close(''); };

  native.onShow(({ kind, devices }) => {
    overlay?.remove();

    overlay = document.createElement('div');
    overlay.className = 'picker-backdrop';
    overlay.addEventListener('click', (e) => { if (e.target === overlay) close(''); });

    const panel = document.createElement('div');
    panel.className = 'picker';
    panel.setAttribute('role', 'dialog');
    panel.setAttribute('aria-modal', 'true');
    panel.setAttribute('aria-label', `Select a ${kind} device`);

    const head = document.createElement('h2');
    head.className = 'picker-title';
    head.textContent = `Select a ${kind} device`;

    const sub = document.createElement('p');
    sub.className = 'picker-sub';
    sub.textContent = devices.length
      ? 'Only the device you choose is exposed to the page.'
      : 'Nothing found yet — devices appear here as they are discovered.';

    const list = document.createElement('div');
    list.className = 'picker-list';

    for (const device of devices) {
      const item = document.createElement('button');
      item.type = 'button';
      item.className = 'picker-item';

      const name = document.createElement('span');
      name.className = 'picker-name';
      name.textContent = device.name;

      const detail = document.createElement('span');
      detail.className = 'picker-detail';
      detail.textContent = device.detail ?? '';

      item.append(name, detail);
      item.addEventListener('click', () => {
        log?.('ok', `selected ${device.name}`);
        close(device.id);
      });
      list.append(item);
    }

    const cancel = document.createElement('button');
    cancel.type = 'button';
    cancel.className = 'picker-cancel';
    cancel.textContent = 'Cancel';
    cancel.addEventListener('click', () => close(''));

    panel.append(head, sub, list, cancel);
    overlay.append(panel);
    document.body.append(overlay);

    document.addEventListener('keydown', onKey);
    (list.querySelector('.picker-item') ?? cancel).focus();
  });
}
