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

### B2. Audio-enable UI + gating
`RadioSettings::audioEnabled` and the CatServer checks were ported, but there's no checkbox and no gating of
audio start/Opus decode. **Risk:** both branches share `QSettings("QK4","QK4")` key `audio/enabled`. If audio
was turned off in the old fork, v2 inherits "off" with no UI to change it, and CatServer then forwards
TX/RX/AG to the K4 while local audio keeps playing. Checkbox goes in `src/ui/pages/audiooutputpage.cpp`,
gating in `AudioController` (must handle PTT mic and sidetone). Fork: 63f544c, f642d66.

**Notes:**

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

## On-air checks

### C1. CW notch placement
Fork c2777c1 used `NM - cwPitch`; upstream 2559f7a uses NM directly on the main pan and `NM - pitch` on the
mini-pan. The fork formula only made sense with its own panadapter offsets (X2). Verify the notch lines up
with the audible null.

**Notes:**

### C2. PTT keys the radio for voice/data
Fork's last PTT sent `TX;`/`RX;` (4026238, marked experimental). Upstream keys from the audio stream
(`AudioController::setPttActive`); XMIT still sends `TX;`. Confirm voice and data PTT key the K4 remotely.

**Notes:**

### C3. FT8 timing without latency knobs
Fork had tunable buffer/latency targets (74d6c87, 349645e); upstream replaced them with a self-correcting
jitter buffer (6e1c1df). Confirm FT8 decodes and DT look normal remotely.

**Notes:**

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
