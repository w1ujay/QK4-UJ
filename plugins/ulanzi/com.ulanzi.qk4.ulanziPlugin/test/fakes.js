import { EventEmitter } from 'node:events';
import { Relay } from '../plugin/relay.js';

export class FakeSocket extends EventEmitter {
  constructor(port) {
    super();
    this.port = port;
    this.written = [];
    this.destroyed = false;
  }
  write(data) {
    this.written.push(data);
    return true;
  }
  destroy() {
    this.destroyed = true;
    this.emit('close');
  }
}

// A Relay over fake sockets and fake timers, so tests drive connects, closes and retries by hand.
export function relayHarness() {
  const sockets = [];
  const timers = [];
  const relay = new Relay({
    createSocket: (port) => {
      const s = new FakeSocket(port);
      sockets.push(s);
      return s;
    },
    setTimer: (fn, ms) => {
      const t = { fn, ms, cleared: false };
      timers.push(t);
      return t;
    },
    clearTimer: (t) => {
      t.cleared = true;
    },
  });
  return { relay, sockets, timers };
}
