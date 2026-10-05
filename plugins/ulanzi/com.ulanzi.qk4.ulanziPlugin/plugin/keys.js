import { DIAL_KEY, downMessage, keyFor, upMessage } from './protocol.js';

// Turns deck presses into QK4 key presses.
//
// WHY presses are combined per key: several deck actions can be the same QK4 key - two PTT keys, two
// buttons on slot 3. Sending every edge straight through meant press A, press B, release A unkeyed the
// radio while B was still held. A key goes down when its first holder presses and up when its last
// holder lets go.
//
// A press is bound to the key it started on, so a slot changed mid-press still releases the slot that
// was pressed. An action removed from the deck mid-press stops holding its key; if it was the last
// holder, the key is released with cancel, so QK4 does not count it as a tap.
export class KeyTracker {
  #send;
  #params = new Map(); // context -> last known param, for key events that arrive without one
  #pressed = new Map(); // context -> the key it is holding
  #holders = new Map(); // key -> Set of contexts holding it

  constructor(send) {
    this.#send = send;
  }

  remember(jsn) {
    if (jsn?.param) this.#params.set(jsn.context, jsn.param);
  }

  keyDown(jsn) {
    const key = keyFor(jsn.uuid, jsn.param ?? this.#params.get(jsn.context));
    if (key) this.#press(jsn.context, key);
  }

  keyUp(jsn) {
    this.#release(jsn.context, false);
  }

  dialDown(jsn) {
    this.#press(jsn.context, DIAL_KEY);
  }

  dialUp(jsn) {
    this.#release(jsn.context, false);
  }

  clear(jsn) {
    for (const item of jsn?.param ?? []) {
      this.#params.delete(item.context);
      this.#release(item.context, true);
    }
  }

  /// Studio went away. Forget every press without sending: the QK4 connection is closed next, and QK4
  /// releases everything a departing client held.
  reset() {
    this.#pressed.clear();
    this.#holders.clear();
  }

  #press(context, key) {
    if (this.#pressed.has(context)) return; // a repeated down
    this.#pressed.set(context, key);
    let holders = this.#holders.get(key);
    if (!holders) {
      holders = new Set();
      this.#holders.set(key, holders);
    }
    holders.add(context);
    if (holders.size === 1) this.#send(downMessage(key));
  }

  #release(context, cancel) {
    const key = this.#pressed.get(context);
    if (key === undefined) return; // not holding anything
    this.#pressed.delete(context);
    const holders = this.#holders.get(key);
    holders.delete(context);
    if (holders.size > 0) return; // someone else still holds this key
    this.#holders.delete(key);
    this.#send(upMessage(key, cancel));
  }
}
