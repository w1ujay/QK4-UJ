# QK4 plugin for Ulanzi Studio (Ulanzi D100H dial)

Drive QK4 from an Ulanzi D100H: turn to tune VFO A, hold the dial down and turn to tune VFO B, run
QK4 macros from the seven buttons and the dial press (tap and hold), and transmit with a PTT button.

The plugin is a thin relay. It forwards presses, releases and detents to QK4 over a local TCP
connection; QK4 decides everything else (tap vs. hold, what each button does).

## Install

1. Quit Ulanzi Studio.
2. Copy the `com.ulanzi.qk4.ulanziPlugin` folder into Ulanzi Studio's plugin folder:
   - macOS: `~/Library/Application Support/Ulanzi/UlanziDeck/Plugins/`
   - Windows: `%APPDATA%\Ulanzi\UlanziDeck\Plugins\` (check: some versions use `UlanziStudio` instead of
     `UlanziDeck` in this path)
3. Start Ulanzi Studio. The QK4 category appears with three actions.

## Set up the dial

1. Drag **QK4 Dial** onto the D100H's dial.
2. Drag **QK4 Button** onto each key you want to use and pick its **Slot** (1-7) in the action's settings.
3. Optionally drag **QK4 PTT** onto one key. It transmits while held.

## Set up QK4

1. Options → **Ulanzi Dial** → tick **Enable Ulanzi Dial**. The status shows "Ulanzi Studio connected"
   once the plugin finds QK4 (it retries every 2 seconds).
2. Macros: give the buttons something to do. Slot *n* runs **Ulanzi.*n*T** on a tap and **Ulanzi.*n*H**
   once held for half a second. The dial press runs **Ulanzi.DialT** on a tap and **Ulanzi.DialH** when released
   after half a second or more; holding the dial and turning it tunes VFO B and runs neither.

## Changing the port

The default is 9410. If something else uses it, set a new port in QK4 (Options → Ulanzi Dial) **and** in
any QK4 action's settings in Ulanzi Studio (the port is shared by all QK4 actions).

## Safety

- PTT is released if Ulanzi Studio quits, crashes or loses its connection to the plugin or to QK4, if the PTT
  key is removed from the deck while held, and if the Ulanzi server is disabled in QK4.
- With two PTT keys (or two buttons on one slot), letting go of one does not release the other.
- Events that happen while the plugin is not connected are dropped, never delivered late.
- QK4 treats the dial's PTT like its own PTT button: it can take transmit over from WSJT-X (CAT or TCI),
  and WSJT-X cannot take it from the dial.

## Protocol

Newline-delimited JSON over one TCP connection to `127.0.0.1:<port>`, from the plugin to QK4. Any other host
can drive QK4 the same way.

| Line | Meaning |
|---|---|
| `{"t":"rotate","n":1,"hold":false}` | One detent; `n` is `1` (right) or `-1` (left); `hold` true while the dial is pressed |
| `{"t":"dial","down":true}` | Dial pressed (`false`: released) |
| `{"t":"button","slot":3,"down":true}` | Button slot 1-7 pressed (`false`: released) |
| `{"t":"ptt","down":true}` | PTT pressed (`false`: released) |
| `{"t":"button","slot":3,"down":false,"cancel":true}` | Released, but not a press: no tap, no hold (also on `dial`) |

Messages describe keys, not deck actions: a host combining several controls on one key sends its `down` once
and its `up` when the last is released. Each line may be at most 4096 bytes before its newline; a longer line
disconnects the client. Malformed lines are ignored. QK4 accepts one client at a time; a new connection
replaces the old one.

## Developing

```bash
cd plugins/ulanzi/com.ulanzi.qk4.ulanziPlugin
npm install
npm test         # protocol, relay, key-tracking and wiring unit tests
npm run build    # bundles plugin/app.js + the SDK + ws into dist/app.js (commit the result)
node scripts/make-icons.mjs   # regenerate the action icons
```

Test without hardware in Ulanzi's simulator (`UlanziDeckSimulator` in
[UlanziDeckPlugin-SDK](https://github.com/UlanziTechnology/UlanziDeckPlugin-SDK)): `npm install && npm start`
there, open http://127.0.0.1:39069, copy this plugin folder into the simulator's `plugins/`, refresh, then
start the main service yourself from the plugin folder: `node dist/app.js 127.0.0.1 39069 en`.

Vendored, unmodified, under the Apache License 2.0 (see each folder's `LICENSE`):
- `vendor/plugin-common-node/` from UlanziTechnology/plugin-common-node @ `9e478b2`
- `property-inspector/libs/` from UlanziTechnology/plugin-common-html @ `0aeb6a2`

`dist/app.js` also bundles `ws` (MIT; the notice is in `dist/ws-LICENSE`).
