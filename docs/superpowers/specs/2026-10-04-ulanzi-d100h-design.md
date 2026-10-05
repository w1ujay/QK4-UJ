# Ulanzi D100H Dial Support — Design

Date: 2026-10-04
Status: approved in brainstorming, awaiting spec review

## Goal

Let any QK4 user drive the radio from an Ulanzi D100H Bluetooth dial: a stepless haptic dial (with push) and
seven RGB buttons, configured through Ulanzi Studio. The dial tunes VFO A (VFO B while the dial is held down and
turned), the seven buttons and the dial press run QK4 macros (tap and hold), and one button can be PTT.

Built on its own branch, `feature/ulanzi-dial`, cut from upstream `origin/development`, so it can be offered to
`mikeg-dal/QK4` as a PR. The fork merges it into its merge branch with a `CHANGELOG-UJ.md` entry.

## Non-goals (v1)

- Button lighting or any feedback from QK4 to the dial (commands only, one direction).
- A "learn" step or any per-button configuration inside QK4 beyond the existing macro dialog.
- Mapping the dial to anything other than VFO A / VFO B tuning.
- Remote (non-localhost) connections.

## Overview

```
D100H ──BT──▶ Ulanzi Studio ──▶ QK4 plugin (Node 20)  ──TCP 127.0.0.1:9410, NDJSON──▶ UlanziServer (QK4)
                                 plugins/ulanzi/...                                        │
                                                                                            ▼
                                                                                  HardwareController
                                                                ┌──────────────┬────────────┴─────────────┐
                                                        VFO A/B tuning   macroRequested("Ulanzi.*")   pttRequested(bool)
                                                   (onKpodEncoderRotated   → MacroController          → MainWindow →
                                                        WithRocker)                                    TransmitController
```

Two independent pieces joined by a small, host-agnostic line protocol:

1. **Ulanzi Studio plugin** — a thin relay. It knows nothing about the radio.
2. **QK4 `UlanziServer`** — parses the protocol, decides tap vs. hold, and hands events to
   `HardwareController`, which maps them onto existing QK4 machinery.

## 1. Wire protocol

Newline-delimited JSON (one object per line, UTF-8, `\n` terminated) over one persistent TCP connection from the
plugin to `127.0.0.1:<port>`. Default port **9410**.

| Message | Meaning |
|---|---|
| `{"t":"rotate","n":1,"hold":false}` | Dial turned one detent; `n` is `+1` (right) or `-1` (left); `hold` true while the dial is pressed |
| `{"t":"dial","down":true}` / `false` | Dial pressed / released |
| `{"t":"button","slot":3,"down":true}` / `false` | Button in slot 1–7 pressed / released |
| `{"t":"button","slot":3,"down":false,"cancel":true}` | Button released **without** counting as a press (its action was removed from the deck mid-press). Also valid on `dial`. |
| `{"t":"ptt","down":true}` / `false` | PTT button pressed / released |

Rules:
- One rotate message per detent; `n` is exactly ±1. Anything else is malformed.
- Raw press and release only. The plugin does no timing; tap/hold is QK4's decision.
- Unknown extra fields are ignored (forward compatibility). Unknown `t` values are malformed.
- `cancel` is optional, boolean, and only meaningful on a `button` or `dial` release. `cancel:true` with
  `down:true` is malformed. A cancelled release fires no tap and no deferred hold; a button hold that already
  fired stays fired.
- **Line limit:** each line's raw bytes (before the `\n`, before any trimming) may be at most 4096. A longer
  line, terminated or not, drops the client.
- Messages describe **keys** (PTT, button slot *n*, the dial), not deck actions. The plugin combines several
  actions on one key: the key goes down when the first is pressed and up when the last is released.

## 2. Ulanzi Studio plugin

Location: `plugins/ulanzi/com.ulanzi.qk4.ulanziPlugin/`, plugin UUID `com.ulanzi.ulanzistudio.qk4`. The SDK
(`github.com/UlanziTechnology/UlanziDeckPlugin-SDK`, Apache-2.0) requires this shape: the folder is
`com.ulanzi.<name>.ulanziPlugin`, and the main service is recognised by a UUID of **exactly four** dot-separated
parts. Action UUIDs add a fifth: `.dial`, `.button`, `.ptt`. Ulanzi Studio runs it on Node 20.

Three actions in `manifest.json` (no `Devices` key, so every device can use them; whether the D100H reports as
the SDK's `"Dial"` model is unverified):

| Action | Controller | SDK events used | Sends |
|---|---|---|---|
| QK4 Dial | Encoder | `onDialRotate` (`rotateEvent`: `left`, `right`, `hold-left`, `hold-right`), `onDialDown`, `onDialUp`, `onClear` | `rotate`, `dial` |
| QK4 Button | Keypad | `onKeyDown`, `onKeyUp`, `onClear` | `button` with the slot from its property inspector |
| QK4 PTT | Keypad | `onKeyDown`, `onKeyUp`, `onClear` | `ptt` |

- **QK4 Button** has a property inspector with one setting, "Slot" (1-7, default 1), saved as the action's
  param. The user places one QK4 Button per physical key in Ulanzi Studio and picks its slot.
- `hold-left` / `hold-right` map to `n:-1/+1, hold:true`; `left` / `right` to `hold:false`.
- **Key aggregation:** presses are tracked per action instance (context) and combined per key (`ptt`,
  `button:<slot>`, `dial`). A key's `down` is sent when its first holder presses and its `up` when its last
  holder releases, so releasing one of two PTT keys never unkeys the other. A press stays bound to the key it
  started on: changing a button's slot mid-press releases the slot that was pressed.
- **Key state is per QK4 connection.** Every new QK4 connection (first connect, reconnect after a loss,
  a port change) starts with no held keys: holds delivered on the previous connection are forgotten (QK4 has
  already released them), presses made while disconnected are forgotten, and nothing is replayed. A key still
  physically held across a reconnect sends nothing until it is pressed again; its release is ignored. Fresh
  presses after the reconnect go down and up on their own.
- **Action removed while held** (`onClear`): it stops holding its key. If it was the last holder, the release
  is sent with `cancel:true` for a button or the dial (no tap, no deferred hold) and as a plain release for PTT.
- **Studio connection lost** (`onClose`; the SDK neither exits nor reconnects): forget all held keys and close
  the QK4 socket with no retry. QK4 releases everything on disconnect, PTT included. With nothing left open,
  the Node process ends.
- **Connection:** one TCP socket (Node `net`) for all action instances. On close or error, retry every 2 s,
  indefinitely. Events that arrive while disconnected are **dropped, never queued**. A queued PTT press or a
  burst of stale detents would be dangerous or surprising when the link returns.
- **Port** is an SDK global setting (default 9410), shown on every action's property inspector, so it can follow
  a changed QK4 port. A new port drops the socket and reconnects.
- **Code layout:** the protocol mapping (`plugin/protocol.js`), the reconnecting socket (`plugin/relay.js`),
  key aggregation (`plugin/keys.js`) and the SDK event wiring (`plugin/wiring.js`, taking the SDK object as a
  parameter) are modules with `node --test` unit tests; `plugin/app.js` only constructs them. The SDK's
  Node library is vendored (with its licence) and needs `ws`; Ulanzi Studio installs no npm dependencies, so
  `npm run build` bundles everything with esbuild into `dist/app.js` (`CodePath`), which is committed so that
  installing is copying the folder. The property inspector uses the SDK's vendored HTML library.

## 3. QK4: `UlanziServer`

Files: `src/network/ulanziserver.h`, `src/network/ulanziserver.cpp` (added to `SOURCES`/`HEADERS`).

Modelled on `CatServer`:
- `QTcpServer` on the main thread, listening on `QHostAddress::LocalHost` only.
- **One client at a time.** A new connection replaces the current one. The old one is closed and treated as a
  disconnect (see §6).
- **4096-byte raw line limit**, enforced while reading and before trimming or parsing: reads are taken in
  bounded chunks and the socket's read buffer is capped, so an oversized line or a flood is never held whole.
  A line over the limit, terminated or not, drops the client.
- `start(quint16 port)`, `stop()`, `isListening()`, `hasClient()`, `port()`, `lastError()`.

### Parsing

A pure, static function so the tests can drive it without sockets:

```cpp
struct UlanziEvent {
    enum class Type { Invalid, Rotate, Dial, Button, Ptt };
    Type type = Type::Invalid;
    int steps = 0;      // Rotate: +1 / -1
    bool hold = false;  // Rotate: dial held while turning
    int slot = 0;       // Button: 1..7
    bool down = false;  // Dial / Button / Ptt
    bool cancel = false; // Dial / Button release: not a press (no tap, no deferred hold)
};
static UlanziEvent parseLine(const QByteArray &line);
```

Uses `QJsonDocument`. Returns `Type::Invalid` for anything outside §1.

### Tap / hold

Decided in the server for buttons 1–7 and the dial press (not PTT):

- `static constexpr int HOLD_MS = 500;` (overridable in tests via a setter).
- On press, start a single-shot timer for that key. On release before it fires, emit **tap**.
- **Buttons:** if the timer fires while the button is still down, emit **hold** immediately (do not wait for
  release); the release after it emits nothing.
- **Dial press:** the timer only marks the press as long. **Hold is emitted on release**, and only if the dial
  was not turned while held. This is what makes the no-macro guarantee below possible: a hold emitted at
  500 ms could not be taken back by a turn at 600 ms.
- A release with `cancel:true` emits nothing (no tap, no deferred dial hold).
- QK4 has no hold timing of its own to reuse: the KPOD decides hold in its own hardware. 500 ms is the
  chosen value, as a named constant.
- A dial press that is turned (any `rotate` with `hold:true` while down), before or after 500 ms, is a VFO B
  gesture, not a press: it cancels that press's tap and hold, so turning while held never fires
  `Ulanzi.DialT/H`.

### Signals

```cpp
void rotated(int steps, bool hold);
void buttonTapped(int slot);     // 1..7
void buttonHeld(int slot);
void dialTapped();
void dialHeld();
void pttChanged(bool down);
void clientConnectedChanged(bool connected);
void errorOccurred(const QString &message); // "Port N unavailable: <reason>", also kept as lastError()
void started(quint16 port);
void stopped();
```

## 4. QK4: wiring

`HardwareController` constructs and owns the `UlanziServer` and exposes `ulanziServer()` beside the existing
device accessors, under the same documented exception for Options pages.

| Server signal | HardwareController action |
|---|---|
| `rotated(n, false)` | `onKpodEncoderRotatedWithRocker(n, 2)`: VFO A, same step and lock handling as KPOD. Ignored until the radio is connected and has reported the VFO's frequency (the KPOD's USB-007 guard). |
| `rotated(n, true)` | `onKpodEncoderRotatedWithRocker(n, 0)`: VFO B |
| `buttonTapped(n)` / `buttonHeld(n)` | `emit macroRequested("Ulanzi.<n>T")` / `"Ulanzi.<n>H"` |
| `dialTapped()` / `dialHeld()` | `emit macroRequested("Ulanzi.DialT")` / `"Ulanzi.DialH"` |
| `pttChanged(down)` | `emit pttRequested(down)` (new signal) |

`macroRequested` already reaches `MacroController` (`mainwindow.cpp`), so macros need no new plumbing.

`MainWindow` connects `HardwareController::pttRequested` beside the bottom-bar PTT button:
- `true` → `m_transmitController->engage(TransmitOwner::Owner::Ulanzi, TransmitOwner::Route::StreamedFromHere)`
- `false` → `m_transmitController->release(TransmitOwner::Owner::Ulanzi)`

### Transmit ownership

`src/models/transmitowner.h`:
- Add `Owner::Ulanzi` with a comment ("the Ulanzi D100H PTT button, over the local Ulanzi server").
- Add it to `isLocal()`. The dial sits on the operator's desk, so it preempts remote owners (CAT, TCI) and is
  never preempted by them, the same as the PTT button.
  Accepted trade-off: the signal arrives over a localhost socket, so any local process could claim to be the
  dial. This is the same trust boundary as the CAT server, which is localhost-only.
- `sourceFor(Owner::Ulanzi)` is the microphone (the existing default branch; no change needed).

### Macros

`src/utils/macroids.h`: 16 new IDs, `Ulanzi1T`/`Ulanzi1H` … `Ulanzi7T`/`Ulanzi7H`, `UlanziDialT`/`UlanziDialH`,
with values `"Ulanzi.1T"` … `"Ulanzi.DialH"`. Added after the KPOD block.
`src/ui/dialogs/macrodialog.cpp`: the same 16 entries in the slot list, after the K-pod entries.

## 5. Settings and Options page

`RadioSettings`:
- `ulanzi/enabled` (bool, default `false`) and `ulanzi/port` (quint16, default `9410`), with getters, setters and
  `ulanziEnabledChanged` / `ulanziPortChanged` signals, following `catServer/*`.
- `HardwareController` starts the server when enabled and restarts it on a port change.

`src/ui/pages/ulanzipage.{h,cpp}` ("Ulanzi Dial" in the Options list, after K-Pod), laid out like `rigcontrolpage` (the closest existing page: enable, port, status):
- Enable checkbox, port field (1024–65535, a `QLineEdit` as on the Rig Control page), status line.
- Status text: "Disabled", "Listening on port 9410, waiting for Ulanzi Studio", "Ulanzi Studio connected",
  or "Port 9410 unavailable: <reason>".
- A one-line hint pointing at the plugin README.
- Colours, dimensions and fonts from `K4Styles`; font sizes with `setPixelSize()`.

## 6. Error handling

| Situation | Behaviour |
|---|---|
| Port in use / listen fails | `errorOccurred(msg)` as `CatServer` does. The page shows "Port N unavailable: <reason>". No automatic retry; toggling enable or changing the port retries. |
| Malformed line (bad JSON, unknown `t`, missing or wrong-typed field, `n` not ±1, slot outside 1–7) | Skipped; logged with `qCDebug`. The connection stays up. |
| Line over 4096 raw bytes, with or without a newline | Client dropped before the line is parsed. |
| Unmapped slot | `macroRequested` fires; `MacroController` already ignores empty bindings. |
| Client disconnects or is replaced while PTT, a button or the dial is down | Emit `pttChanged(false)` if PTT was down, cancel pending hold timers, emit no tap and no deferred dial hold. |
| Ulanzi Studio connection lost while the plugin keeps running | The plugin closes its QK4 socket (no retry), which is the disconnect above. |
| Deck action removed while held | The plugin sends a cancelled release for its key (if it was the last holder): no tap, no hold. |
| Server stopped or disabled while a client is connected | Same as a disconnect, before the listener is torn down (mirrors `CatServer`'s unkey-before-teardown). |
| Radio not connected | Events still route and behave exactly as KPOD input does; no Ulanzi-specific handling. |
| Esc / losing the radio while Ulanzi holds PTT | Existing `TransmitController::releaseAll()`; a later `ptt down:false` from the plugin is ignored as a release from a non-owner. |

## 7. Testing

Qt Test, registered in `tests/CMakeLists.txt`.

`tests/test_ulanziserver.cpp` (`UlanziServerTests`):
- `parseLine()` table: every valid message from §1; malformed cases (invalid JSON, non-object, unknown `t`,
  missing fields, `n` = 0 / 2 / "1", slot 0 / 8, `down` not bool); extra fields ignored.
- Tap vs. hold with a short hold override: quick press → tap on release; long button press → hold while
  down, nothing on release; long dial press → nothing while down, hold on release; rotate with `hold:true`
  during a dial press, before **and after** the threshold, cancels its tap and hold; a cancelled release fires
  nothing.
- Over a real `QTcpSocket` on an ephemeral port, as `test_catserver` does: events arrive as signals; a second
  client replaces the first; lines of exactly 4096 raw bytes are accepted and 4097 drop the client, terminated
  or not; disconnect while PTT is down emits `pttChanged(false)`;
  `stop()` while PTT is down emits `pttChanged(false)`; listening on a taken port emits `errorOccurred`.

`tests/test_transmitowner.cpp`: `Owner::Ulanzi` preempts `CatClient` and `TciClient`; neither preempts it; a
release from another owner is ignored; `Ulanzi` and `PttButton` do not preempt each other (local never
preempts local).

Plugin: `node --test` for `protocol.js`, `relay.js`, `keys.js` and `wiring.js` (mapping, drop-while-disconnected,
2 s reconnect, port change, overlapping PTT and same-slot presses, slot changed mid-press, removal while held,
losing Studio while the QK4 connection is up). Then by hand in UlanziDeckSimulator: rotate, hold-rotate, dial press, each button slot, PTT; then quit and
restart QK4 and confirm the plugin reconnects and drops events while disconnected.

Hardware, first thing on the real D100H (the main unknowns): what the plugin actually receives (key ids,
exactly one `onDialRotate` per detent, `hold-left`/`hold-right` while pressed). Then on the air: tuning A and B,
tap/hold macros, PTT keys and unkeys, and quitting Ulanzi Studio mid-transmit unkeys the radio.

## 8. Documentation

`plugins/ulanzi/README.md`: install the plugin into Ulanzi Studio, place the three actions on the D100H, set
each QK4 Button's slot, enable "Ulanzi Dial" in QK4 Options, change the port on both sides if needed, and the
protocol from §1 for anyone writing a different host.

## 9. Delivery

- Branch `feature/ulanzi-dial` from `origin/development`; conventional commits, clang-format clean.
- Merge into the fork's merge branch with a `CHANGELOG-UJ.md` entry.
- Offer upstream as a PR to `mikeg-dal/QK4` once the hardware check passes.
- This spec lives on the fork's merge branch alongside `docs/BACKLOG.md`, not on the feature branch.
