import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  ACTIONS, DEFAULT_PORT, DIAL_KEY, PLUGIN_UUID, downMessage, keyFor, portFromSettings, rotateMessage, upMessage,
} from '../plugin/protocol.js';

test('the plugin UUID has exactly four parts, or the SDK will not treat it as the main service', () => {
  assert.equal(PLUGIN_UUID.split('.').length, 4);
  for (const uuid of Object.values(ACTIONS)) assert.ok(uuid.startsWith(`${PLUGIN_UUID}.`));
});

test('each rotate event maps to one detent', () => {
  assert.deepEqual(rotateMessage('right'), { t: 'rotate', n: 1, hold: false });
  assert.deepEqual(rotateMessage('left'), { t: 'rotate', n: -1, hold: false });
  assert.deepEqual(rotateMessage('hold-right'), { t: 'rotate', n: 1, hold: true });
  assert.deepEqual(rotateMessage('hold-left'), { t: 'rotate', n: -1, hold: true });
});

test('unknown rotate events are dropped, including inherited property names', () => {
  assert.equal(rotateMessage('up'), null);
  assert.equal(rotateMessage(undefined), null);
  assert.equal(rotateMessage('toString'), null);
});

test('a QK4 Button is keyed by its slot, defaulting to 1', () => {
  assert.equal(keyFor(ACTIONS.button, { slot: 3 }), 'button:3');
  assert.equal(keyFor(ACTIONS.button, { slot: '7' }), 'button:7');
  assert.equal(keyFor(ACTIONS.button, undefined), 'button:1');
  assert.equal(keyFor(ACTIONS.button, { slot: 9 }), 'button:1');
  assert.equal(keyFor(ACTIONS.button, { slot: 2.5 }), 'button:1');
});

test('QK4 PTT is the ptt key; the dial action and other plugins have no key from a key event', () => {
  assert.equal(keyFor(ACTIONS.ptt, null), 'ptt');
  assert.equal(keyFor(ACTIONS.dial, null), null);
  assert.equal(keyFor('com.other.plugin.x', null), null);
});

test('keys turn into protocol messages; cancel only marks button and dial releases', () => {
  assert.deepEqual(downMessage('ptt'), { t: 'ptt', down: true });
  assert.deepEqual(upMessage('ptt', true), { t: 'ptt', down: false });
  assert.deepEqual(downMessage('button:4'), { t: 'button', slot: 4, down: true });
  assert.deepEqual(upMessage('button:4', false), { t: 'button', slot: 4, down: false });
  assert.deepEqual(upMessage('button:4', true), { t: 'button', slot: 4, down: false, cancel: true });
  assert.deepEqual(downMessage(DIAL_KEY), { t: 'dial', down: true });
  assert.deepEqual(upMessage(DIAL_KEY, true), { t: 'dial', down: false, cancel: true });
});

test('the port comes from global settings when it is a valid unprivileged port', () => {
  assert.equal(portFromSettings({ port: 9500 }), 9500);
  assert.equal(portFromSettings({ port: '9600' }), 9600);
  assert.equal(portFromSettings({}), DEFAULT_PORT);
  assert.equal(portFromSettings(undefined), DEFAULT_PORT);
  assert.equal(portFromSettings({ port: 80 }), DEFAULT_PORT);
  assert.equal(portFromSettings({ port: 70000 }), DEFAULT_PORT);
});
