# QK4-UJ Backlog

Fork backlog for `ujay-mods-v2`. Sources: the fork-vs-v2 port audit (re-run 2026-09-14 against `e037152`,
covering all 51 fork-only commits in `ba6e3d1..ujay-mods`), plus new requests.

**How to use:** fill in the **Decision** column (`Keep` / `Defer` / `Drop`) and add a note in the item's
section. Effort is rough: S = hours, M = a day or two, L = several days.

## Summary

| ID | Item | Type | Effort | Decision |
|----|------|------|--------|----------|
| B1 | Mini View pan blank + zero-padded frequencies | Bug (v2) | M | Keep — done, on-air test pending |
| B2 | Audio-enable UI + gating (inherited-off risk) | Port gap / bug risk | S–M | Skip |
| B3 | CAT `IF` data sub-mode (fldigi RTTY) | Port gap | S | |
| N1 | Bundle BarnCat Sparks with the Windows distro | New | M | |
| N2 | "Launch Sparks" button in the GUI | New | S | |
| N3 | Tune up/down in Mini View | New | S | Keep — done, on-air test pending |
| N4 | Right-click a mini-pan to tune VFO B | New | S | Keep — done, on-air test pending |
| N5 | HaliKey connect indicator in the status bar | New | S | Keep — done, HaliKey test pending |
| P1 | Keyboard controls (arrow-key tuning, etc.) | Port gap | S–M | Keep — tuning done; Esc/KY0 dropped |
| P2 | MON button double-toggle fix | Port gap | S | |
| P3 | PAN ON/OFF toggle in DISP popup | Port gap | M | |
| P4 | RF gain "RF-n" indicator on VFO row | Port gap | S | |
| P5 | QK4-UJ window title + W1UJ attribution | Port gap | S | Keep — done |
| P6 | Mini View settings page | Port gap | S | |
| P7 | RX noise filter (3.5 kHz LPF) | Port gap | S–M | Skip |
| P8 | DATA-mode mic gain scaling | Port gap (uncertain) | S | |
| P9 | N1MM UDP spots + overlay | Port gap | L | |
| P10 | Fork release CI (`uj.*` tags) | Port gap | M | |
| P11 | Null audio sink guard | Port gap | S | |
| C1 | On-air: CW notch placement | Check | — | |
| C2 | On-air: PTT keys radio for voice/data | Check | — | |
| C3 | On-air: FT8 timing without latency knobs | Check | — | |
| X1 | PTT frame-count toast | Recommend drop | S | |
| X2 | Fork panadapter/CW offset centering | Recommend drop | — | |
| U1–U3 | Review findings: upstream issues/PRs, fork fixes, pending on-air tests | Tracking | — | See section |

---

## Bugs

### B1. Mini View pan blank + zero-padded frequencies
Mini View (MINI button) shows `A 0021033065` and an empty pan area with `-5 kHz / +5 kHz` labels.
Five separate causes, all verified in code except #5:
1. **Frequencies:** `miniviewcontroller.cpp:43,47,100,101` pass `frequencyDisplay()->frequency()` (raw
   10-digit string). Use `displayText()` instead. RIT/XIT changes also don't refresh the mini view.
2. **Wrong stream:** `mainwindow.cpp:185` feeds the main PAN (0x02) payload, header included. The mini pan
   expects bare MiniPAN (0x03) bins. Connect `ConnectionController::miniSpectrumDataReceived` and slice by
   offset/count, as `SpectrumController::onMiniSpectrumData` does.
3. **Stream never enabled:** the K4 only sends MiniPAN after `#MP1;`. Mini View never sends it (it only
   works if VFO A's mini-pan was already open). Send `#MP1;` when the pan is shown, and `#MP0;` on hide only
   if Mini View turned it on.
4. **Settings dropped on first toggle:** the first time the pan is turned on in a session, `refreshAll()`
   has already run while `m_miniPan` was null (`miniviewwindow.cpp:281-304`), so mode, filter, and span are
   ignored (shows ±5 kHz in CW). The window is reused, so reopening Mini View later applies them (±1.0 kHz).
   Re-apply right after lazy creation. Use `modeToString()` + `setDataSubMode()` like `VFOWidget`.
5. **Not rendering (confirmed on Windows, 2026-09-14):** the pan area is fully see-through; desktop content
   shows through it and only the window's own border line is visible. The QRhiWidget draws nothing, not even
   its clear color or center line. Suspects: the translucent frameless tool window
   (`WA_TranslucentBackground`, `miniviewwindow.cpp:30`) and lazily adding a QRhiWidget to an already-shown
   window. Create the widget up front in `setupUi()`, and drop translucency (opaque background + `setMask()`
   for rounded corners) if still see-through.

**Notes:** Done 2026-09-14. All five fixed. Kept lazy pan creation (a hidden QRhiWidget breaks QRhiWidget init,
per `vfowidget.cpp`) and removed `WA_TranslucentBackground` instead; the window now has square corners. On-air
check: pan draws, ±1.0 kHz in CW on first toggle, frequencies read `7.031.415`, VFO A mini-pan still works after
returning to the main window.

**Update 2026-09-15 — cause #5 was misdiagnosed.** Translucency was not it; the pan stayed blank (black once
opaque). Qt only switches an already-shown top-level to RHI composition when a QRhiWidget is *reparented* into
it: `QRhiWidget`'s constructor sets the render-to-texture flag after `QWidget` has already taken the parent, so
`new MiniPanRhiWidget(this)` never triggered the switch. The Mini View window stayed `RasterSurface` and every
frame failed with `QRhiWidget: No QRhi` + `renderFailed()`. Fixed by constructing the pan parentless and letting
`m_mainLayout->addWidget()` reparent it. Verified under WSLg: winId changes, surface becomes `OpenGLSurface`,
pan draws spectrum, waterfall, passband and center line. Diagnostics kept behind the `dsp.minipan` and
`ui.miniview` debug categories (`QT_LOGGING_RULES="dsp.minipan.debug=true;ui.miniview.debug=true"`).

**Open, WSLg only (verify on Windows):** the Mini View window can't be dragged — Qt reports it at `QPoint(0,0)`
both before and after the pan exists, though `miniview/windowX|Y` hold 569,448, so the compositor places it and
ignores `move()`. BAND / MODE also accept one click and then stop responding. Both match the WSLg `Qt::Tool`
bug (microsoft/wslg#1094) that broke popup clicks before, and neither is caused by the pan fix.

**Update 2026-09-28 — upstream mini-pan follow.** Upstream now opens/closes VFO A's mini-pan from the radio's
`#MP` reports. If the K4 echoes Mini View's `#MP1;`, VFO A would open too and the old release check (skip `#MP0`
while `miniPanAEnabled`) would leave the stream on. Mini View now always sends `#MP0;` and clears the flag when it
releases a stream it started, and stops owning it if the radio reports `#MP0` first. On-air check pending:
open/close Mini View with VFO A's mini-pan closed, then open, and confirm VFO A's state is unchanged afterwards.

**Update 2026-09-29 — two more Mini View fixes (`1735586`).** Mini View showed the dial frequency while
transmitting with XIT on, because it didn't refresh on TX/RX changes. It now refreshes after
`TxStateController` re-renders the VFO. A new window also always started with the pan hidden, even when it
had been left open. `applySettings()` now restores it, deferred until the show finishes so the QRhiWidget is
added to a window that is already showing (the cause #5 lesson above). On-air check pending: XIT +100 Hz while
transmitting shows the same frequency in Mini View as on VFO A; the pan reopens after a restart.

### B2. Audio-enable UI + gating
`RadioSettings::audioEnabled` and the CatServer checks were ported, but there's no checkbox and no gating of
audio start/Opus decode. **Risk:** both branches share `QSettings("QK4","QK4")` key `audio/enabled`. If audio
was turned off in the old fork, v2 inherits "off" with no UI to change it, and CatServer then forwards
TX/RX/AG to the K4 while local audio keeps playing. Checkbox goes in `src/ui/pages/audiooutputpage.cpp`,
gating in `AudioController` (must handle PTT mic and sidetone). Fork: 63f544c, f642d66.

**Notes:** 2026-09-29: a review found `AG;` / `AG$;` get no reply when `audio/enabled` is false. CatServer
forwards them to the K4, but nothing relays the K4's answer back to the CAT client, and RadioState doesn't track
AG. Not reachable here: Jay's Windows registry has `audio/enabled = true`, and there is no UI to change it.
Options: remove the audio-disabled branches from CatServer, which fits the Skip decision and removes the
inherited-off risk above, or answer from RadioState once upstream tracks AG (Mike's plan in #134). Undecided.

### B3. CAT `IF` data sub-mode
`catframes.cpp:153` hardcodes `'0'` for the data sub-mode, so fldigi can't detect RTTY. Upstream changed
the field to a single digit, so the fork's patch (a84d190) doesn't apply as written. Also affects AI pushes
(`catpushbroadcaster.cpp:63,72`). Add a test beside `testIfResponse*` in `tests/test_catserver.cpp`.

**Notes:**

---

## New features

### N1. Bundle BarnCat Sparks with the Windows distro
Sparks (`~/repos/barncat-sparks`, WinKeyer3 emulator + MIDI controller for the K4) defaults to
`-cat localhost:9299`, which is QK4's CAT server port, so they work together without configuration.
Licensing is fine: Sparks is MIT, QK4 is GPLv3.

**Blocker:** `w1ujay/barncat-sparks` is **private** and `w1ujay/QK4-UJ` is **public**. Options:
- (a) Make Sparks public; the CI (`_build-windows.yml`) downloads the release zip into `QK4-windows/`.
- (b) Keep Sparks private; CI uses a PAT secret, and only produces private artifacts.
- (c) Don't bundle; the launch button (N2) finds a separately installed Sparks.
- (d) Package locally: build Sparks from the local repo and add it to the zip by hand (public CI can't).

Also: latest Sparks release is v0.2.1 (2026-02-26) with 57 commits since (incl. radio-abstraction merge).
Cut a new release before bundling.

**Notes:**

### N2. "Launch Sparks" button
Start `barncat-sparks.exe` via `QProcess::startDetached`, looking next to `QK4.exe` first, then a
configurable path (Options). Consider enabling QK4's CAT server first if it's off, and disabling the button
when Sparks isn't found. Placement TBD (bottom menu bar, Fn popup, or Options).

**Notes:**

### N3. Tune up/down in Mini View
Mini View has no tuning. Options: up/down step buttons, arrow keys (shares code with P1), or mouse wheel on
the mini pan. Reuse the step math from the main wheel handlers (`RadioUtils::tuningStepToHz`).

**Notes:** Done 2026-09-14: Up/Down when Mini View has focus (active VFO per B SET), mouse wheel over the A or
B frequency tunes that VFO. No new buttons.

### N4. Right-click a mini-pan to tune VFO B
Right-click on the VFO A or VFO B mini-pan, or on the Mini View pan, tunes VFO B to the clicked frequency,
matching the main panadapter's L=A R=B rule. Uses the mini-pan span (2 kHz CW/FSK/AFSK, 10 kHz otherwise)
and CW/CW-R pitch on both sides (`RadioUtils::miniPanClickToDialHz`), snaps to B's step, respects VFO B lock and
Mouse QSY "Left Only". Left-click unchanged.

**Notes:** Done 2026-09-14. RIT on the clicked VFO is included (the mini-pan is centered on the receive
passband), and VFO B's own RIT is subtracted so B receives exactly at the clicked frequency. On-air check: assumes the K4 centers MiniPAN on the passband like QK4 draws it (dial in SSB,
dial ± pitch in CW) — confirm a right-clicked signal lands in VFO B's passband, also with RIT at +0.50.

### N5. HaliKey connect indicator in the status bar
Reconnecting the HaliKey meant opening Options → CW Keyer every session. A `HALIKEY` item now sits in the top
status bar left of the KPA1500 / RFKIT / K4 group: gray disconnected, green connected, port in the tooltip.
Click toggles `openPort()` on the remembered `halikey/portName` (both device types use a port name) /
`closePort()`; with no port saved it opens Options at the CW Keyer page via the new `OptionsDialog::showPage()`.
Port-open failures already surface on the notification overlay through `HardwareController::hardwareError`, and
the indicator stays gray. `StatusBarController` emits `halikeyToggleRequested` rather than knowing about
devices or settings; MainWindow owns that wiring in `setupHardwareController()`.

**Notes:** Done 2026-09-16. Verified building and running under WSL; the no-port path opens the CW Keyer page.
Not yet exercised against real HaliKey hardware — check on Windows that one click connects, the indicator turns
green, the tooltip names the port, and a second click disconnects.

---

## Port gaps (fork features not in v2)

### P1. Keyboard controls
Fork 034642a: Up/Down tunes active VFO by step; Up/Down on cursor digit in frequency entry; arrow keys on
DualControlButton and MonOverlay; Esc sends `KY0`. At HEAD, `MainWindow::keyPressEvent`
(`mainwindow.cpp:1370`) handles only F1–F12. Reuse wheel step math (`mainwindow.cpp:738`, `:1081`), add VFO
lock checks (fork lacked them). Esc already closes popups/overlays, so a global `KY0` may surprise; route via
`CwController` instead.

**Notes:** Tuning done 2026-09-14: Up/Down tunes VFO A (VFO B with B SET) by the step, snapped like the pan
wheel, respecting lock — also works when a button has focus (`ButtonTuneKeyFilter`). Up/Down on the cursor digit
in frequency entry tunes by that digit's place value (sent as `FA…;FA;` with no local update, so a refused
value can't stick; ignored once digits are typed; rapid presses build on the last target sent via
`RadioUtils::PendingTune`, reconciled against every FA/FB reply — a reply that doesn't match its request
means the value was refused, so pending state is dropped and the next press builds on the radio's frequency;
replies still owed to abandoned or expired requests are counted and skipped so they can't clear newer presses,
and are only forgotten when the connection drops). VFO A/B frequency wheel now also snaps and respects lock;
stepping down from an off-grid frequency lands on the nearest grid point (panadapter wheel too).
Esc → KY0 dropped by decision. DualControlButton arrow keys ported 2026-09-15 (click to focus, then Up/Down
adjust; an inactive button activates first, as with the wheel). No conflict with `ButtonTuneKeyFilter`: that
filter only intercepts `QAbstractButton`, and DualControlButton is a plain QWidget. Covered by
`tests/test_dualcontrolbutton.cpp`. MonOverlay arrow keys still not ported.

**Review follow-ups 2026-09-15:**
- Fixed: Enter in the frequency entry resent the displayed digits when nothing was typed, undoing an
  in-flight digit tune (`frequencydisplaywidget.cpp` `exitEditMode`). Covered by two new cases in
  `tests/test_frequencydisplaywidget.cpp`.
- Fixed 2026-09-16: every `FA`/`FB` reply used to consume a pending digit-tune entry, including replies
  to QK4's own bare `FA;`/`FB;` queries from the seven `SpectrumController` sites (spot click, pan click),
  so a click within the 1 s `HOLD_MS` window before arrow presses dropped the pending targets. Value
  comparison can't separate the cases — a refused tune and a foreign query both reply with the unchanged
  frequency — so `PendingTune` now keeps one queue of expected replies (tune / abandoned tune / foreign
  query) and matches by position. `ConnectionController::sendCAT` emits `frequencyQuerySent` for every
  bare query it sends (DirectConnection, so the queue updates in send order), and `sent()` skips the
  query its own tune issued. Five cases added to `tests/test_radioutils.cpp`. CatServer answers `FA;`/`FB;`
  from RadioState, so external loggers never triggered this.
- Fixed 2026-09-16: `sendCAT` announced a frequency query even when `TcpClient` dropped the command for
  being offline (`m_connected` mirrors the same `Connected` state the send checks), so entries queued
  while disconnected survived `resetUiForDisconnect()` and consumed real replies after reconnecting.
  The emit is now guarded, the typed-entry lambdas got the `isConnected()` check that `tuneVfoByHz`
  already had, and `onRadioReady()` resets both queues so a fresh link always starts empty. The
  remaining window — link drops between the check and the queued write — is covered by that reset.

### P2. MON button double-toggle fix
Fork 034642a: only send `SW128` when opening the MON overlay. At HEAD `sidecontrolpanel.cpp:166-174` sends
it on both open and close. Verify SW128 behavior on air.

**Notes:**

### P3. PAN ON/OFF toggle in DISP popup
Fork 95ea5df, 2bad812, 84fd63e: sends `#FPS00`/`#MP0`/`#MP$0`, hides spectrum, drops spectrum data, saves
per radio. Useful on slow remote links. Conflicts to handle: upstream mini-pan `#MP` ownership
(`mainwindow.cpp:724-728, 1067-1071`), FPS setting (`#FPS15` on connect, `mainwindow.cpp:1224`), fixed-size
popup grid (`displaypopupwidget.cpp:341-344`). Data gating goes in `SpectrumController`.

**Notes:**

### P4. RF gain "RF-n" indicator
Fork 69f9863: VFO feature-row label for RF gain attenuation. Wire in `ProcessingDisplayController` beside
`setAtt` (`processingdisplaycontroller.cpp:38`). Use K4Styles (fork hardcoded `#FFFFFF`/`11px`); check row
width.

**Notes:**

### P5. QK4-UJ window title + W1UJ attribution
Fork 69f9863, 74d6c87. Title at `mainwindow.cpp:482`. Don't edit LICENSE (verbatim GPL, breaks GitHub
license detection); put attribution in README (`:233`) and/or About box (`mainwindow.cpp:473`).

**Notes:** Done 2026-09-15. Title is `QK4-UJ v<version>`, or `QK4-UJ <branch>-<sha>` for CI branch builds (no
"v" when the version doesn't start with a digit). About box credits W1UJ and links QK4-UJ and upstream. README
restored the fork heading, intro, Build Windows badge (`ujay-mods-v2`; `ci.yml` doesn't run on the fork branch),
fork download note, Fork Changes section rewritten for v2, and the fork copyright line. LICENSE untouched. The
side panel version label still reads `QK4 V.<version>`.

### P6. Mini View settings page
Fork cacccfa, 9a43414: Options tab with Show BAND / MODE / spots checkboxes. Settings exist
(`radiosettings.h:189-198`) and `MiniViewWindow::applySettings` reads them, but nothing sets them. New page
in `src/ui/pages/` following `RfkitPage`. Rename "N1MM spots" to "DX spots".

**Notes:**

### P7. RX noise filter
Fork ddcbcd4: 3.5 kHz biquad low-pass on RX audio with a toggle. Insert before "Step 4: Clamp"
(`audioengine.cpp:393`); still 12 kHz so coefficients hold. Fork defaulted ON; suggest OFF (cuts wide
AM/FM/DATA).

**Notes:**

### P8. DATA-mode mic gain scaling
Fork a84d190: 0–0.5× mic range in DATA vs 0–2× voice. Upstream removed the 2× boost (curve tops at 1.0×,
`audioengine.cpp:619-623`), so 0.5× would now be much quieter. Check WSJT-X drive levels before porting.

**Notes:**

### P9. N1MM UDP spots + overlay
Fork a4747d2, 68352c9, 18e6d87, 97f54d6, 2bad812, 2f6ed14, aa29417, 70cafd8, 7b9d2f1: UDP listener on
12060, mult/dupe/new-QSO coloring, click-to-tune, colors, expiry up to 600 min. Dropped by the port plan.
Upstream's DX cluster has no mult/dupe status, so it isn't a contest replacement. If revived, feed upstream's
DX spot pipeline and `DxSpotOverlay` rather than a second overlay.

**Notes:**

### P10. Fork release CI
Fork fb5d770, d52a92a: `uj.*` tags → `0.4.0-UJ.N` prereleases, Linux tarball, no macOS. At HEAD only the
on-demand `build-windows.yml` (1ad645a) exists; upstream `release.yml` triggers on `v*` and publishes
`releases.json`, so use a separate fork-only workflow. Related to N1.

**Notes:**

### P11. Null audio sink guard
Fork a84d190: also check `m_audioSink` in `feedAudioDevice` (`audioengine.cpp:255`). Probably unreachable now
since teardown clears both pointers (uncertain). Low priority.

**Notes:**

---

## Review findings, 2026-09-28/29

Found by automated review sweeps of the whole repo. Fixes to fork-only code went straight onto
`merge/upstream-dev-2026-09-26`. Bugs in upstream-owned files (unchanged from `mikeg-dal/QK4` `development`) are
reported upstream; tested fixes live on one-commit branches cut from `development` and are merged into the fork.
Tom Schaefer (NY4I, wrote the TCI server) said PRs are welcome.

### U1. Upstream issues, fix branches and PRs

| Upstream | Problem | Fix branch (cut from `development`) | Status |
|---|---|---|---|
| #134 (comment) | CAT `BW` replies in Hz, not 10-Hz units | fixed in fork only (`964cf57`) | Mike's own issue; waiting |
| #159 | TCI TX audio decimated at the mic's rate | `fix/tci-tx-decimation` | **Fixed upstream** (`2a5bd39`, ours) |
| #160 | Re-assert/takeover clears `radioConfirmed`, radio's RX; ignored | `fix/transmit-confirmation` | **Fixed upstream** (`ea4aadd`, ours; Mike added `2bfe2b9`: a takeover that sends RX; starts a new episode) |
| #161 | Local unkey during TCI selects the mic before the gate closes | `fix/tci-release-source-order` | **Fixed upstream** (`4319afb`, ours; Mike's `67e5cfe` leaves the TX source to the transmit arbiter) |
| #162 | Reconnect: parser buffer, stale `.local` lookup, cleared startup macro | none | **Fixed upstream** by Mike (`3aa6669`, `113121b`, `77c334c`) |
| #163 | WebSocket drops never complete; silent handshakes hold slots | `fix/websocket-forced-drop` | **Fixed upstream** (PR #166 closed, landed as `c1ea65d`) |
| #164 | Mono TCI block with leading silence decoded as stereo | none | **Fixed upstream** by Mike (`cc6d8fe`) |
| #167 | Disconnecting one DX cluster clears every cluster's spots | none | **Fixed upstream** by Mike (`a34b386`) |
| #168 | Immediate TCI re-key leaves the room mic as TX source, gate open | `fix/tx-source-stale-switch` | **Fixed upstream** (`3dd4bf5`, ours) |
| #169 | Antenna config `ACM`/`ACS`/`ACT` sent without `;` | `fix/antenna-config-terminator` | **Fixed upstream** (`c617ad0`, ours) |
| #170 | Removing a DX cluster misaligns list rows and live connections | `fix/dxcluster-remove-entry` | **Fixed upstream** (`2464868`, ours) |

All of the above closed upstream and came into the fork with the 2026-10-04 merge of `origin/development`
(`7a5af6b`); conflicts were resolved by taking upstream's versions. The fix branches are obsolete and can be
deleted. Only #134 remains open.

**On later upstream merges:** where upstream fixes one of these its own way, take theirs and drop ours. The fork
patches upstream-owned files in `catframes.*`, `catserver.cpp`, `audioengine.*`, `transmitowner.h`,
`tcicontroller.cpp`, `websocketserver.*`, `audiocontroller.*`, `antennaconfigcontroller.cpp` and `dxcluster*`.

### U2. Fork-only fixes (on `merge/upstream-dev-2026-09-26`)

- `4a64ca8` Mini View releases the MiniPAN stream now that VFO A follows `#MP` (B1 update above).
- `aadb316` RFKit: replies from an earlier connection are dropped; the antenna list refreshes when names change.
- `edb085e` RFKit: drive-power limit rechecked on connect and on every amp state change; OPERATE refused over it.
- `964cf57` CAT: `BW`/`BW$` in 10-Hz units, `SB3` = Sub mini-pan (not diversity), `AG` follows the sliders,
  `TX/;` routed like `TX;`.
- `1735586` Mini View follows XIT on transmit and restores its saved pan (B1 update above).

### U3. On-air tests pending (remote site down, battery used for the RTTY contest)

Run QK4 with the logging block below so each run gets its own file in `C:\AX\QK4\QK4-UJ\logs\`.

```powershell
cd C:\AX\QK4\QK4-UJ
New-Item -ItemType Directory -Force -Path C:\AX\QK4\QK4-UJ\logs | Out-Null
$log = "C:\AX\QK4\QK4-UJ\logs\qk4-$(Get-Date -Format yyyyMMdd-HHmmss).txt"
$env:QT_FORCE_STDERR_LOGGING = "1"
$env:QT_MESSAGE_PATTERN = "%{time hh:mm:ss.zzz} %{category} %{type}: %{message}"
$env:QT_LOGGING_RULES = "net.ws=true;net.tci=true;tx.owner=true;qk4.audio.tx=true"
Start-Process C:\AX\QK4\QK4-UJ\QK4.exe -WorkingDirectory C:\AX\QK4\QK4-UJ -RedirectStandardError $log
Get-Content $log -Wait
```

The #159-#170 checks below now exercise upstream's versions of the fixes (merged 2026-10-04).

- [ ] **#161:** press Esc during a TCI transmission. Expect a clean unkey, no stray blip on the power meter.
- [ ] **#159:** 24 kHz headset as the mic, WSJT-X over TCI. Expect TX tones at the right frequency.
- [ ] **#168:** hard to provoke by hand. Mainly check nothing regressed: WSJT-X TX, Esc, PTT button.
- [ ] **#169:** change an antenna in ANT CFG. Expect the radio to apply it; the log shows `ACM…;` with `;`.
- [ ] **#170 (no radio needed):** two clusters, connect the second, remove the first. The second still
      shows connected and its console works.
- [ ] **CAT:** `BW;` / `BW$;` / `SB;` / `AG;` on port 9299 (PowerShell snippet in the 2026-09-28 session).
- [ ] **Mini View:** B1 update checks (stream ownership, XIT, saved pan).
- [ ] **RFKit:** with low-power protection on and the K4 over the limit, OPERATE from the amp's own panel
      returns to STANDBY within one poll. (RFKit amps are off at the moment; disable RFKit until then.)
- [ ] **Mini-pan follow (upstream feature):** toggle the mini-pan on the K4 front panel; QK4 follows.
- [ ] **Ulanzi D100H, simulator first (Windows):** Options → Ulanzi Dial status (disabled / listening / connected /
      port busy), then the plugin in UlanziDeckSimulator: rotate, hold-rotate, dial tap/hold (hold only on release,
      none after a turn), button slots tap/hold, two overlapping PTT keys, removing a held key, quitting QK4 and
      restarting (plugin reconnects in ~2 s, a fresh press then works), stopping the simulator while PTT is held.
- [ ] **Ulanzi D100H, hardware:** what the plugin receives on the real dial (one `dialrotate` per detent,
      `hold-left`/`hold-right` while pressed, key events for the seven buttons, whether it reports as `Dial`), the
      Windows plugin folder path for the README, then on the air: tune A/B, macros, PTT keys and unkeys, quitting
      Ulanzi Studio mid-transmit unkeys. **Also: power off / Bluetooth loss and a Studio page switch while PTT is
      held** — if no release arrives, decide on a PTT watchdog before the upstream PR.
- [ ] **Ulanzi upstream PR:** after the above, PR `feature/ulanzi-dial` to mikeg-dal/QK4; say that CI does not run
      the plugin tests or check `dist/app.js`, and that release builds don't include the plugin.
- [ ] **C2 voice**, **C3 clock check** above.
- [ ] **Fast-forward `ujay-mods-v2`** to the merge branch once the above looks good.

Done on the air 2026-09-28 (PR #166): suspend WSJT-X receiving (dropped at 40.0 s) and transmitting (radio
unkeyed at once, client dropped at 37.5 / 33.5 s), normal quit (immediate), 8 silent connections (dropped at
17.6-19.5 s). Use Resource Monitor → Suspend Process, not kill: the OS closes a killed process's socket.

**Tip for review sweeps:** point them at the fork's own changes (`git diff origin/development...HEAD`), not the
whole repo, or they keep finding upstream bugs faster than they can be tested.

---

## On-air checks

### C1. CW notch placement
Fork c2777c1 used `NM - cwPitch`; upstream 2559f7a uses NM directly on the main pan and `NM - pitch` on the
mini-pan. The fork formula only made sense with its own panadapter offsets (X2). Verify the notch lines up
with the audible null.

**Notes:**

### C2. PTT keys the radio for voice/data
Fork's last PTT sent `TX;`/`RX;` (4026238, marked experimental). Upstream keys from the audio stream
(`AudioController::setPttActive`); XMIT still sends `TX;`. Confirm voice and data PTT key the K4 remotely.

**Notes:** 2026-09-28: **data confirmed.** WSJT-X over TCI keyed the remote K4 (log: `PTT ON from client 1`,
`transmitter: none -> TCI client`, then the radio's `TX;` 300-400 ms later), and the radio's own `RX;` released
it. Voice PTT still to check.

### C3. FT8 timing without latency knobs
Fork had tunable buffer/latency targets (74d6c87, 349645e); upstream replaced them with a self-correcting
jitter buffer (6e1c1df). Confirm FT8 decodes and DT look normal remotely.

**Notes:** 2026-09-28: FT4 over TCI decodes normally and completed QSOs. DT was **-0.6 to -0.9 s on every
station**. A uniform negative offset points to the PC clock, since audio latency would push DT positive. Check
Windows time sync (or Meinberg NTP) first; if DT stays negative after that, look at QK4.

---

## Recommend drop

### X1. PTT frame-count toast
Fork 809c056, 4026238: debug "N frames sent" notification on PTT release. Diagnostic only.

**Notes:**

### X2. Fork panadapter/CW offset centering
Fork 22f71fc, 3f59877, 3b20330, 1c8015e. Conflicts with upstream's IF-shift-based CW model
(`panadapter_rhi.cpp`). Don't port.

**Notes:**

---

## Done / superseded (for reference)

Ported in v2: RFKit client/panel/window, Mini View window + MINI button, CatServer extensions (`$` prefixes,
KY/TB, MD$/BW$/DV/PB/SB, RU/RD/RC, UP/DN/UPB/DNB, RG, AG/AG$ volume).

Superseded upstream: IF format/mode digit, optimistic CAT parse on SETs, TCP_NODELAY (always on), spot font
size, VFO-B spot click, 12→48 kHz upsampling and volume gain (fork reverted itself), Linux x86_64 release
(Flatpak), `PC;` H/L suffix and GET suppression (intentionally left out).
