// The QK4 side of the wire: one JSON object per line. See plugins/ulanzi/README.md.

// The SDK treats a UUID of exactly four parts as the plugin's main service; actions add a fifth.
export const PLUGIN_UUID = 'com.ulanzi.ulanzistudio.qk4';
export const ACTIONS = Object.freeze({
  dial: `${PLUGIN_UUID}.dial`,
  button: `${PLUGIN_UUID}.button`,
  ptt: `${PLUGIN_UUID}.ptt`,
});
export const DEFAULT_PORT = 9410;
export const BUTTON_COUNT = 7;
export const DIAL_KEY = 'dial';
const PTT_KEY = 'ptt';

const ROTATE = Object.freeze({
  left: [-1, false],
  right: [1, false],
  'hold-left': [-1, true],
  'hold-right': [1, true],
});

export function rotateMessage(rotateEvent) {
  if (!Object.hasOwn(ROTATE, rotateEvent)) return null;
  const [n, hold] = ROTATE[rotateEvent];
  return { t: 'rotate', n, hold };
}

function slotFromParam(param) {
  const n = Number(param?.slot);
  return Number.isInteger(n) && n >= 1 && n <= BUTTON_COUNT ? n : 1;
}

/// The QK4 key a deck key event presses: 'ptt', 'button:<slot>', or null for anything else.
export function keyFor(actionUuid, param) {
  if (actionUuid === ACTIONS.button) return `button:${slotFromParam(param)}`;
  if (actionUuid === ACTIONS.ptt) return PTT_KEY;
  return null;
}

function baseMessage(key) {
  if (key === PTT_KEY) return { t: 'ptt' };
  if (key === DIAL_KEY) return { t: 'dial' };
  return { t: 'button', slot: Number(key.slice('button:'.length)) };
}

export function downMessage(key) {
  return { ...baseMessage(key), down: true };
}

/// A cancelled release tells QK4 it was not a press (no tap, no hold). PTT has nothing to cancel.
export function upMessage(key, cancel) {
  const message = { ...baseMessage(key), down: false };
  if (cancel && key !== PTT_KEY) message.cancel = true;
  return message;
}

export function portFromSettings(settings) {
  const n = Number(settings?.port);
  return Number.isInteger(n) && n >= 1024 && n <= 65535 ? n : DEFAULT_PORT;
}
