/**
 * In-memory contact store.
 *
 * Everything lives for the lifetime of the tab and nothing is persisted — no
 * localStorage, no IndexedDB, no network. Close the tab and the scan is gone.
 */
export class ContactStore extends EventTarget {
  constructor() {
    super();
    this.contacts = new Map();
    this.startedAt = null;
  }

  /**
   * Insert or merge a contact. Repeat sightings update the record in place and
   * keep a bounded signal history for trend display.
   */
  upsert(contact) {
    const now = Date.now();
    const existing = this.contacts.get(contact.id);

    if (existing) {
      existing.name = contact.name ?? existing.name;
      existing.identifier = contact.identifier ?? existing.identifier;
      existing.detail = contact.detail ?? existing.detail;
      existing.lastSeen = now;
      existing.hits++;
      if (contact.signal != null) {
        existing.signal = contact.signal;
        existing.history.push(contact.signal);
        if (existing.history.length > 60) existing.history.shift();
      }
      this._changed('update', existing);
      return existing;
    }

    const record = {
      id: contact.id,
      name: contact.name ?? 'unknown',
      source: contact.source,
      kind: contact.kind ?? 'env',
      identifier: contact.identifier ?? '—',
      detail: contact.detail ?? '',
      signal: contact.signal ?? null,
      history: contact.signal != null ? [contact.signal] : [],
      firstSeen: now,
      lastSeen: now,
      hits: 1,
    };
    this.contacts.set(record.id, record);
    this._changed('add', record);
    return record;
  }

  drop(id) {
    const record = this.contacts.get(id);
    if (!record) return;
    this.contacts.delete(id);
    this._changed('drop', record);
  }

  /** Remove every contact produced by a given connector. */
  dropSource(source) {
    for (const [id, record] of this.contacts) {
      if (record.source === source) this.contacts.delete(id);
    }
    this._changed('drop', null);
  }

  clear() {
    this.contacts.clear();
    this._changed('clear', null);
  }

  list() {
    return [...this.contacts.values()].sort((a, b) => b.lastSeen - a.lastSeen);
  }

  filtered(query) {
    const q = query.trim().toLowerCase();
    if (!q) return this.list();
    return this.list().filter((c) =>
      `${c.name} ${c.identifier} ${c.source} ${c.detail}`.toLowerCase().includes(q));
  }

  get size() { return this.contacts.size; }

  /** Serialisable snapshot for the export button. */
  snapshot(meta = {}) {
    return {
      tool: 'AEGIS Consent-Based Device Scanner',
      exportedAt: new Date().toISOString(),
      note: 'All records were produced by permission-gated browser APIs on this device.',
      ...meta,
      contactCount: this.contacts.size,
      contacts: this.list().map((c) => ({
        name: c.name,
        source: c.source,
        kind: c.kind,
        identifier: c.identifier,
        detail: c.detail,
        signal: c.signal,
        hits: c.hits,
        firstSeen: new Date(c.firstSeen).toISOString(),
        lastSeen: new Date(c.lastSeen).toISOString(),
      })),
    };
  }

  _changed(type, record) {
    this.dispatchEvent(new CustomEvent('change', { detail: { type, record } }));
  }
}
