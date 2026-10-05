import { test } from 'node:test';
import assert from 'node:assert/strict';
import { RETRY_MS } from '../plugin/relay.js';
import { relayHarness } from './fakes.js';

test('sends one JSON line per message once connected', () => {
  const { relay, sockets } = relayHarness();
  relay.connectTo(9410);
  assert.equal(sockets[0].port, 9410);
  sockets[0].emit('connect');
  assert.equal(relay.send({ t: 'ptt', down: true }), true);
  assert.deepEqual(sockets[0].written, ['{"t":"ptt","down":true}\n']);
});

test('drops events while connecting and while disconnected, and never replays them', () => {
  const { relay, sockets, timers } = relayHarness();
  relay.connectTo(9410);
  assert.equal(relay.send({ t: 'ptt', down: true }), false); // still connecting
  sockets[0].emit('connect');
  sockets[0].emit('close');
  assert.equal(relay.send({ t: 'rotate', n: 1, hold: false }), false); // link down
  timers[0].fn();
  sockets[1].emit('connect');
  assert.deepEqual(sockets[1].written, []); // nothing queued came through
});

test('retries every 2 s after a close, one timer per loss', () => {
  const { relay, sockets, timers } = relayHarness();
  relay.connectTo(9410);
  sockets[0].emit('error', new Error('ECONNREFUSED'));
  sockets[0].emit('close');
  assert.equal(timers.length, 1);
  assert.equal(timers[0].ms, RETRY_MS);
  assert.equal(RETRY_MS, 2000);
  timers[0].fn();
  assert.equal(sockets.length, 2);
  sockets[1].emit('close');
  assert.equal(timers.length, 2);
});

test('a new port drops the old socket and connects to the new one', () => {
  const { relay, sockets, timers } = relayHarness();
  relay.connectTo(9410);
  sockets[0].emit('connect');
  relay.connectTo(9500);
  assert.equal(sockets[0].destroyed, true);
  assert.equal(sockets[1].port, 9500);
  assert.equal(timers.length, 0); // the old socket's close does not schedule a retry
  assert.equal(relay.connected, false);
});

test('a new port cancels a pending retry', () => {
  const { relay, sockets, timers } = relayHarness();
  relay.connectTo(9410);
  sockets[0].emit('close');
  relay.connectTo(9500);
  assert.equal(timers[0].cleared, true);
  assert.equal(sockets.at(-1).port, 9500);
});

test('the same port again is a no-op', () => {
  const { relay, sockets } = relayHarness();
  relay.connectTo(9410);
  relay.connectTo(9410);
  assert.equal(sockets.length, 1);
});

test('close() drops the connection for good: no retry, nothing sent', () => {
  const { relay, sockets, timers } = relayHarness();
  relay.connectTo(9410);
  sockets[0].emit('connect');
  relay.close();
  assert.equal(sockets[0].destroyed, true);
  assert.equal(timers.length, 0);
  assert.equal(relay.connected, false);
  assert.equal(relay.send({ t: 'ptt', down: true }), false);
});

test('close() also cancels a pending retry, and connectTo() afterwards starts again', () => {
  const { relay, sockets, timers } = relayHarness();
  relay.connectTo(9410);
  sockets[0].emit('close');
  relay.close();
  assert.equal(timers[0].cleared, true);
  relay.connectTo(9410);
  assert.equal(sockets.length, 2);
});
