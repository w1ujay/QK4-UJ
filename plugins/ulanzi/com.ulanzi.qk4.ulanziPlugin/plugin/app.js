// QK4 plugin main service: constructs the pieces and hands them to wire(). All behaviour lives in
// protocol.js, relay.js, keys.js and wiring.js, which have tests.
import net from 'node:net';
import UlanziApi from '../vendor/plugin-common-node/index.js';
import { DEFAULT_PORT, PLUGIN_UUID } from './protocol.js';
import { Relay } from './relay.js';
import { KeyTracker } from './keys.js';
import { wire } from './wiring.js';

const relay = new Relay({
  createSocket: (port) => {
    const socket = net.createConnection({ host: '127.0.0.1', port });
    socket.setNoDelay(true); // one small line per detent; do not let Nagle batch them
    return socket;
  },
});
const keys = new KeyTracker((message) => relay.send(message));

const $UD = new UlanziApi();
wire($UD, { relay, keys, defaultPort: DEFAULT_PORT });
$UD.connect(PLUGIN_UUID);
