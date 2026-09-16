# QK4-UJ Fork Changelog

## Unreleased — rebuilt on upstream v0.7.0-beta.5+

The fork was rebuilt from a clean `origin/main` branch point (`ba6e3d1`) rather
than merged. The previous v0.6.0-beta.1 merge (`91888d0`) had resolved
`src/mainwindow.cpp` and `src/ui/optionsdialog.cpp` by taking upstream's copies
wholesale, silently deleting every line that wired the fork's features in — 62
references in MainWindow and 70 in OptionsDialog. The RFKit and Mini View
classes still compiled but nothing constructed them, so both features were
already dead on the old branch. Wiring was recovered from `fff2e8e`, the merge
before that.

The port follows upstream's current architecture rather than reinstating the
fork's inline MainWindow code, so future upstream merges have far less to
conflict with.

### Added
- **RF-Kit amplifier interface** — HTTP REST client, readout panel, and floating
  window, driven by a new `RFKitUiController` that mirrors upstream's
  `KPA1500UiController`. Includes the drive-power lockout that forces the
  amplifier to standby when K4 RF power exceeds a configured maximum, and an
  RFKit indicator in the status bar beside the KPA1500 one.
- **RFKit settings page** — a lazily-created `RfkitPage` in OptionsDialog
  (enable, host, port, poll interval, drive-power lockout, max drive power,
  °C/°F), matching upstream's page pattern.
- **Mini View window** — compact dual-VFO display with optional mini panadapter
  and spot strip, owned by a new `MiniViewController`. Reachable from the MINI
  button added to the bottom menu bar.
- **CatServer extensions** — `$`-suffix prefix parsing (`MD$`, `BW$`, `RG$`,
  `AG$`), `RG+`/`RG-`/`RG/` gain adjust with authoritative re-query, RIT
  re-query after `RU`/`RD`/`RC`, `UP`/`DN`/`UPB`/`DNB` passthrough, keyer buffer
  tracking behind `KY;` and `TB;`, real `SB;` sub-receiver status, `PB;`, and
  `AG`/`AG$` routed to QK4's own volume sliders while it owns the audio path.
  Covered by 17 new cases in `tests/test_catserver.cpp`.
- `RadioSettings`: RFKit and Mini View settings, plus the fork's `audioEnabled`
  master switch that gates CatServer's TX/RX and AG behaviour.
- **Keyboard tuning** — Up/Down tunes the active VFO (VFO B with B SET) by the
  tuning step, respecting VFO lock, even when a button has focus. Up/Down on a
  digit during frequency entry tunes by that digit's place value.
- **Mini View tuning** — arrow keys, or the mouse wheel over the A or B
  frequency.
- **Keyboard control values** — click a side-panel control button (WPM, PWR, BW,
  SHIFT, RF, SQL), then Up/Down adjust it, so a laptop with no mouse can change
  them. An inactive button activates on the first press, matching the wheel.
- **HaliKey status-bar indicator** — a HALIKEY item in the top status bar,
  gray when disconnected and green when connected, with the port in its
  tooltip. Clicking connects on the remembered port or disconnects, so the
  paddle no longer has to be reconnected through Options each session; with no
  port chosen yet it opens Options at the CW Keyer page. `OptionsDialog` gained
  `showPage()` for that.
- **Mini-pan right-click** — right-click a VFO mini-pan or the Mini View pan to
  tune VFO B to that frequency, with CW pitch and RIT handled.

### Changed
- Window title reads `QK4-UJ v<version>` (`QK4-UJ <branch>-<sha>` on CI branch
  builds), and the About box credits the fork and links to QK4-UJ.
- VFO and panadapter wheel tuning snap to the step grid and respect VFO lock.
- Mini View spots now consume upstream's `DxSpot` from `DxClusterClient`
  instead of the fork's N1MM UDP listener.
- Mini View's BAND / MODE buttons restore the main window before opening the
  popup. Upstream's `PopupManager` positions popups against the bottom menu
  bar, which isn't on screen while the mini view is up.

### Fixed
- `TB;` dropped a digit. The fork built the response with
  `QString("TB%100;")`, which Qt reads as placeholder index 10 followed by a
  literal `0`, yielding `TB70;` instead of `TB700;`. Now built by
  concatenation.
- Mini View's panadapter was blank: it now gets the MiniPAN stream, turns
  `#MP` on and off as needed, and applies pan settings when it first appears.
  The pan itself never rendered because Qt switches an already-shown window to
  GPU composition only when a `QRhiWidget` is reparented into it — building the
  pan parentless and letting the layout adopt it leaves the window on
  `RasterSurface` no longer, so the widget gets a QRhi instead of failing every
  frame with `QRhiWidget: No QRhi`. VFO frequencies are formatted (`7.031.415`,
  not `0007031415`) and refresh on RIT/XIT changes.
- A frequency query from elsewhere (pan click, spot click) discarded pending
  digit tunes, so rapid arrow presses lost steps. Its reply carries the
  unchanged frequency, exactly like a refused tune, so `PendingTune` now matches
  replies by position: one queue of expected replies, each marked as a tune, an
  abandoned tune or another path's query. `ConnectionController::sendCAT` is the
  single place that spots those queries, so a new call site can't drift.
- Enter in the frequency entry resent the displayed digits even when nothing had
  been typed. Up/Down digit tuning deliberately leaves the digits alone until the
  radio replies, so pressing Enter in that window tuned the radio back to the
  pre-tune frequency. Enter now just leaves edit mode unless digits were typed.
- RX ANT and SUB ANT buttons did nothing over the network (the K4 ignores the
  `SW70`/`SW157` switch codes from network clients). They now step `AR`/`AR$`
  through the ACM/ACS rotation, skipping ATU antennas without a KAT4.

### Removed
- N1MM UDP spot listener and the N1MM panadapter spot overlay — superseded by
  upstream's DX cluster spot pipeline and `DxSpotOverlay`.
- Fork-specific audio latency and gain modifications — superseded by upstream's
  audio device and buffer-recovery rework.
- Fork CI workflow changes — upstream has since rebuilt CI around reusable
  workflows plus Flatpak packaging.

### Deliberately not ported
- **Optimistic `parseCATCommand` inside CatServer** — upstream already does this
  in MainWindow's `catCommandReceived` handler. Adding it again would parse each
  SET twice and breaks `testExternalKsUpdatesKeyerSpeed`.
- **`PC;` H/L suffix** — upstream's `PCX;` already returns the extended form
  with the power-range suffix, which is what the fork's change was reaching for.
  Changing plain `PC;` risks confusing other CAT clients.
- **Catch-all empty response for unrecognized GET commands** — upstream forwards
  them to the real K4, which can answer authoritatively. The fork suppressed
  them to quiet N1MM polling noise.
