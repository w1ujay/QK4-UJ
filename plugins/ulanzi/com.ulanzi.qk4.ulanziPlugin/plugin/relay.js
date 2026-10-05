// One persistent TCP connection to QK4.
//
// WHY nothing is ever queued: a PTT press delivered seconds late would key the radio when nobody is
// holding the key, and a backlog of detents would spin the VFO on reconnect. While the link is down,
// events are dropped; QK4 releases anything held when the connection goes.
export const RETRY_MS = 2000;

export class Relay {
  #createSocket;
  #retryMs;
  #setTimer;
  #clearTimer;
  #port = null;
  #socket = null;
  #timer = null;
  #connected = false;

  constructor({ createSocket, retryMs = RETRY_MS, setTimer = setTimeout, clearTimer = clearTimeout }) {
    this.#createSocket = createSocket;
    this.#retryMs = retryMs;
    this.#setTimer = setTimer;
    this.#clearTimer = clearTimer;
  }

  get connected() {
    return this.#connected;
  }

  /// Connect (or reconnect) to 127.0.0.1:port. The same port while connected or retrying is a no-op.
  connectTo(port) {
    if (port === this.#port && (this.#socket || this.#timer)) return;
    this.#teardown();
    this.#port = port;
    this.#open();
  }

  /// Drop the connection and stop retrying, until the next connectTo().
  close() {
    this.#teardown();
    this.#port = null;
  }

  /// Send one message. Returns false, and drops it, when not connected.
  send(message) {
    if (!this.#connected) return false;
    this.#socket.write(`${JSON.stringify(message)}\n`);
    return true;
  }

  #open() {
    this.#timer = null;
    const socket = this.#createSocket(this.#port);
    this.#socket = socket;
    socket.on('connect', () => {
      if (this.#socket === socket) this.#connected = true;
    });
    socket.on('error', () => {}); // 'close' follows every error and does the work
    socket.on('close', () => {
      if (this.#socket !== socket) return; // a socket we already replaced or closed
      this.#socket = null;
      this.#connected = false;
      this.#timer = this.#setTimer(() => this.#open(), this.#retryMs);
    });
  }

  #teardown() {
    if (this.#timer) {
      this.#clearTimer(this.#timer);
      this.#timer = null;
    }
    const socket = this.#socket;
    this.#socket = null;
    this.#connected = false;
    if (socket) socket.destroy();
  }
}
