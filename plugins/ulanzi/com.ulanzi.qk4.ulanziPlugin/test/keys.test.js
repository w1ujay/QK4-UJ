import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ACTIONS } from '../plugin/protocol.js';
import { KeyTracker } from '../plugin/keys.js';

function tracker() {
  const sent = [];
  return { keys: new KeyTracker((m) => sent.push(m)), sent };
}
const ptt = (context) => ({ uuid: ACTIONS.ptt, context });
const btn = (context, slot) => ({ uuid: ACTIONS.button, context, param: slot === undefined ? undefined : { slot } });

test('a QK4 Button uses the slot carried on the key event', () => {
  const { keys, sent } = tracker();
  keys.keyDown(btn('a', 4));
  keys.keyUp(btn('a', 4));
  assert.deepEqual(sent, [
    { t: 'button', slot: 4, down: true },
    { t: 'button', slot: 4, down: false },
  ]);
});

test('without a param on the key event, the remembered one is used', () => {
  const { keys, sent } = tracker();
  keys.remember(btn('a', 6));
  keys.keyDown(btn('a'));
  assert.deepEqual(sent, [{ t: 'button', slot: 6, down: true }]);
});

// Review Focus 6. Two PTT keys: releasing one must not unkey while the other is held.
test('overlapping PTT keys key once and unkey only when the last is released', () => {
  const { keys, sent } = tracker();
  keys.keyDown(ptt('A'));
  keys.keyDown(ptt('B'));
  keys.keyUp(ptt('A'));
  assert.deepEqual(sent, [{ t: 'ptt', down: true }]);
  keys.keyUp(ptt('B'));
  assert.deepEqual(sent, [{ t: 'ptt', down: true }, { t: 'ptt', down: false }]);
});

test('two buttons on the same slot combine the same way', () => {
  const { keys, sent } = tracker();
  keys.keyDown(btn('a', 3));
  keys.keyDown(btn('b', 3));
  keys.keyUp(btn('a', 3));
  keys.keyUp(btn('b', 3));
  assert.deepEqual(sent, [
    { t: 'button', slot: 3, down: true },
    { t: 'button', slot: 3, down: false },
  ]);
});

test('a press stays on the slot it started on when the slot is changed mid-press', () => {
  const { keys, sent } = tracker();
  keys.keyDown(btn('a', 1));
  keys.remember(btn('a', 2)); // the property inspector changed the slot while the key was down
  keys.keyUp(btn('a', 2));
  assert.deepEqual(sent, [
    { t: 'button', slot: 1, down: true },
    { t: 'button', slot: 1, down: false },
  ]);
});

test('a repeated keydown and a keyup with no press are ignored', () => {
  const { keys, sent } = tracker();
  keys.keyUp(ptt('A'));
  keys.keyDown(ptt('A'));
  keys.keyDown(ptt('A'));
  assert.deepEqual(sent, [{ t: 'ptt', down: true }]);
});

// Review Focus 5.
test('removing a held PTT key releases it', () => {
  const { keys, sent } = tracker();
  keys.keyDown(ptt('p'));
  keys.clear({ param: [ptt('p')] });
  assert.deepEqual(sent, [{ t: 'ptt', down: true }, { t: 'ptt', down: false }]);
});

test('removing one of two held PTT keys keeps transmitting', () => {
  const { keys, sent } = tracker();
  keys.keyDown(ptt('A'));
  keys.keyDown(ptt('B'));
  keys.clear({ param: [ptt('A')] });
  assert.deepEqual(sent, [{ t: 'ptt', down: true }]);
});

test('removing a held button or dial cancels its press instead of releasing it', () => {
  const { keys, sent } = tracker();
  keys.keyDown(btn('a', 5));
  keys.dialDown({ uuid: ACTIONS.dial, context: 'd' });
  keys.clear({ param: [btn('a', 5), { uuid: ACTIONS.dial, context: 'd' }] });
  assert.deepEqual(sent, [
    { t: 'button', slot: 5, down: true },
    { t: 'dial', down: true },
    { t: 'button', slot: 5, down: false, cancel: true },
    { t: 'dial', down: false, cancel: true },
  ]);
});

test('removing a key that is not held sends nothing', () => {
  const { keys, sent } = tracker();
  keys.keyDown(ptt('p'));
  keys.keyUp(ptt('p'));
  keys.clear({ param: [ptt('p')] });
  assert.equal(sent.length, 2);
});

test('reset() forgets every held key without sending anything', () => {
  const { keys, sent } = tracker();
  keys.keyDown(ptt('p'));
  keys.reset();
  keys.keyUp(ptt('p')); // no longer held: ignored
  keys.keyDown(ptt('p')); // a fresh press keys again
  assert.deepEqual(sent, [{ t: 'ptt', down: true }, { t: 'ptt', down: true }]);
});
