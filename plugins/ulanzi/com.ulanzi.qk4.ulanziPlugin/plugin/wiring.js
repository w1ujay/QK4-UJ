import { portFromSettings, rotateMessage } from './protocol.js';

// Every SDK event the plugin handles, in one place. Takes the SDK object as a parameter so the tests can
// drive it with a stand-in; app.js passes the real one.
export function wire(ud, { relay, keys, defaultPort }) {
  ud.onConnected(() => relay.connectTo(defaultPort));

  // WHY: key state is per QK4 connection. QK4 released everything the previous connection held, and presses
  // made while disconnected never reached it. Start each connection clean and never replay; otherwise a
  // stale holder would swallow the next press's down.
  relay.onConnected(() => keys.reset());

  // WHY: the SDK neither exits nor reconnects when Studio's socket closes. If the QK4 connection stayed
  // up, QK4 would keep whatever was held - PTT included - with nobody left to release it. Closing it is
  // the release: QK4 lets go of everything a departing client held. With nothing left open, Node exits.
  ud.onClose(() => {
    keys.reset();
    relay.close();
  });

  // The SDK emits 'error' on a failed or broken Studio socket; with no listener Node would throw and kill the
  // plugin. 'close' follows and does the work (see onClose).
  ud.onError(() => {});

  ud.onAdd((jsn) => {
    keys.remember(jsn);
    ud.getGlobalSettings(jsn.context);
  });
  ud.onParamFromApp((jsn) => keys.remember(jsn));
  ud.onParamFromPlugin((jsn) => keys.remember(jsn));
  ud.onDidReceiveGlobalSettings((jsn) => relay.connectTo(portFromSettings(jsn.settings)));

  ud.onKeyDown((jsn) => keys.keyDown(jsn));
  ud.onKeyUp((jsn) => keys.keyUp(jsn));
  ud.onClear((jsn) => keys.clear(jsn));
  ud.onDialDown((jsn) => keys.dialDown(jsn));
  ud.onDialUp((jsn) => keys.dialUp(jsn));
  ud.onDialRotate((jsn) => {
    const message = rotateMessage(jsn.rotateEvent);
    if (message) relay.send(message);
  });
}
