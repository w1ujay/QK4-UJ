import { test } from 'node:test';
import assert from 'node:assert/strict';
import { EventEmitter } from 'node:events';
import { ACTIONS } from '../plugin/protocol.js';
import { KeyTracker } from '../plugin/keys.js';
import { wire } from '../plugin/wiring.js';
import { relayHarness } from './fakes.js';

// Stands in for the SDK object: the same on* registration methods, plus emit() to fire them.
class FakeUd extends EventEmitter {
  constructor() {
    super();
    this.globalSettingsRequests = [];
    const events = {
      onConnected: 'connected', onClose: 'close', onAdd: 'add', onParamFromApp: 'paramfromapp',
      onParamFromPlugin: 'paramfromplugin', onDidReceiveGlobalSettings: 'didReceiveGlobalSettings',
      onKeyDown: 'keydown', onKeyUp: 'keyup', onClear: 'clear', onDialDown: 'dialdown', onDialUp: 'dialup',
      onDialRotate: 'dialrotate', onError: 'error',
    };
    for (const [method, event] of Object.entries(events)) {
      this[method] = (fn) => {
        this.on(event, fn);
        return this;
      };
    }
  }
  getGlobalSettings(context) {
    this.globalSettingsRequests.push(context);
  }
}

function setup() {
  const ud = new FakeUd();
  const { relay, sockets, timers } = relayHarness();
  const keys = new KeyTracker((m) => relay.send(m));
  wire(ud, { relay, keys, defaultPort: 9410 });
  ud.emit('connected', {});
  sockets[0].emit('connect');
  return { ud, relay, sockets, timers };
}

const lines = (socket) => socket.written.map((l) => JSON.parse(l));

test('Studio events reach QK4 as protocol lines', () => {
  const { ud, sockets } = setup();
  ud.emit('dialrotate', { uuid: ACTIONS.dial, context: 'd', rotateEvent: 'hold-left' });
  ud.emit('keydown', { uuid: ACTIONS.ptt, context: 'p' });
  ud.emit('dialdown', { uuid: ACTIONS.dial, context: 'd' });
  assert.deepEqual(lines(sockets[0]), [
    { t: 'rotate', n: -1, hold: true },
    { t: 'ptt', down: true },
    { t: 'dial', down: true },
  ]);
});

test('a global-settings port moves the connection', () => {
  const { ud, sockets } = setup();
  ud.emit('add', { uuid: ACTIONS.ptt, context: 'p' });
  ud.emit('didReceiveGlobalSettings', { settings: { port: 9500 } });
  assert.equal(sockets.at(-1).port, 9500);
});

test('adding an action asks Studio for the global settings', () => {
  const { ud } = setup();
  ud.emit('add', { uuid: ACTIONS.button, context: 'b', param: { slot: 2 } });
  assert.deepEqual(ud.globalSettingsRequests, ['b']);
});

// Review finding 1: the SDK neither exits nor reconnects when Studio's socket closes. Keeping the QK4
// connection open would leave QK4 holding whatever was down, PTT included, with nobody to release it.
test('losing Studio while keyed closes the QK4 connection for good', () => {
  const { ud, relay, sockets, timers } = setup();
  ud.emit('keydown', { uuid: ACTIONS.ptt, context: 'p' });
  assert.equal(relay.connected, true);

  ud.emit('close');
  assert.equal(sockets[0].destroyed, true); // QK4 sees the disconnect and releases PTT
  assert.equal(relay.connected, false);
  assert.equal(timers.length, 0); // and the plugin does not reconnect on its own
});

test('an SDK error event does not throw and leaves the QK4 relay connected', () => {
  const { ud, relay } = setup();
  assert.doesNotThrow(() => ud.emit('error', 'boom'));
  assert.equal(relay.connected, true);
});
