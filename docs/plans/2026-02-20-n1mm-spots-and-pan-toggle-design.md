# N1MM DX Spots on Panadapter & Panadapter Off Toggle

**Date:** 2026-02-20
**Status:** Approved

## Overview

Two features for QK4:

1. **N1MM DX Spots on Panadapter** — Display callsign labels from N1MM Logger+ UDP spot broadcasts overlaid on the panadapter spectrum, with click-to-tune.
2. **Panadapter Off Toggle** — A button in the DISP popup to disable spectrum streaming from the K4, saving TCP bandwidth.

## Feature 1: N1MM DX Spots

### N1MM UDP Listener

**New class: `N1mmListener`** (`src/network/n1mmlistener.h/.cpp`)

- Binds `QUdpSocket` to configurable port (default 12060) on `0.0.0.0`
- Parses incoming XML datagrams, filtering for `<spot>` root elements
- Extracts: `dxcall`, `frequency` (kHz float converted to Hz qint64), `mode`, `status`, `timestamp`, `action`
- On `action="add"` emits `spotReceived(SpotData)` signal
- On `action="delete"` emits `spotRemoved(QString callsign)`
- Auto-expires spots older than 10 minutes (configurable)
- Stores active spots in `QHash<QString, SpotData>` keyed by callsign

**SpotData struct:**

```cpp
struct SpotData {
    QString callsign;    // dxcall
    qint64 frequencyHz;  // converted from kHz
    QString mode;        // CW, USB, FT8, etc.
    QString status;      // "dupe", "single mult", "new qso", etc.
    QDateTime timestamp;
    QElapsedTimer age;   // for auto-expiry
};
```

**N1MM `<spot>` XML format:**

```xml
<spot>
  <app>N1MM</app>
  <StationName>CONTEST-PC</StationName>
  <dxcall>W1AW</dxcall>
  <frequency>14025.3</frequency>
  <spottercall>K2PO/7-#</spottercall>
  <timestamp>2026-02-20 17:19:37</timestamp>
  <action>add</action>
  <mode>CW</mode>
  <comment>CW 9 DB 18 WPM CQ</comment>
  <status>single mult</status>
  <statuslist>single mult</statuslist>
</spot>
```

### Spot Overlay Widget

**New class: `SpotOverlayWidget`** (`src/dsp/spotoverlaywidget.h/.cpp`)

- Transparent child widget of `PanadapterRhiWidget`, same pattern as `FrequencyScaleOverlay` and `DbmScaleOverlay`
- Covers the spectrum area only (top portion defined by `m_spectrumRatio`)
- Repositioned in `resizeEvent()` like existing overlays

**Rendering (QPainter in `paintEvent`):**

- For each spot within visible frequency range (`centerFreq +/- spanHz/2`):
  - Map frequency to X position using same `freqToNormalized()` math
  - Draw callsign text angled ~45 degrees, positioned near top of spectrum area
  - Small downward tick mark from label to the frequency point
  - Color-coded by N1MM status:
    - `"single mult"` / `"double mult"` -> yellow
    - `"new qso"` -> white
    - `"dupe"` -> dim gray (de-emphasized)
    - Default -> light cyan

**Click handling:**

- `mousePressEvent()` hit-tests spot label bounding rects
- Emits `spotClicked(qint64 frequencyHz)` on hit
- MainWindow connects to send `FA` command to tune VFO A

**Anti-clutter:**

- When labels overlap horizontally, stagger vertically (up to 3 rows)
- Cap visible spots at ~30, prioritizing newest/highest-priority

**Data flow:**

```
N1mmListener::spotReceived -> MainWindow -> SpotOverlayWidget::addSpot()
N1mmListener::spotRemoved  -> MainWindow -> SpotOverlayWidget::removeSpot()
PanadapterRhiWidget frame  -> SpotOverlayWidget::update() (repaint)
```

## Feature 2: Panadapter Off Toggle

### Mechanism

**When toggled off:**

1. Send `#FPS00;` to K4 to stop spectrum packets (if K4 supports FPS=0; otherwise `#FPS12;` + client-side suppression)
2. Send `#MP0;` / `#MP$0;` to disable MiniPAN streams
3. Hide `m_panadapterA` / `m_panadapterB` widgets
4. Collapse `m_spectrumContainer` to 0 height (or small "PAN OFF" banner)

**When toggled back on:**

1. Restore `m_spectrumContainer` to previous height
2. Show panadapter widgets
3. Send `#FPS<saved_value>;` to restore user's preferred FPS
4. Re-enable MiniPAN if previously on

### DISP Popup Integration

- New `DisplayMenuButton` in bottom row (8th button) labeled `"PAN ON"` / `"PAN OFF"`
- Styled with `K4Styles::popupButtonNormal()` / `popupButtonSelected()`
- Left-click toggles on/off
- New signal: `panadapterToggled(bool enabled)`

### Bandwidth Impact

- At 30 FPS, spectrum packets are ~6-15 KB/s of non-audio TCP traffic
- Disabling pan + minipan eliminates all display-related bandwidth

## Settings & Configuration

### RadioSettings Changes

Add to `RadioEntry` struct:

```cpp
bool n1mmEnabled = false;       // Whether to listen for N1MM spots
quint16 n1mmPort = 12060;       // UDP listen port
int spotExpiryMinutes = 10;     // Auto-expire spots after N minutes
bool panadapterEnabled = true;  // Persist pan on/off state
```

### N1MM Settings UI

Added to existing radio connection settings dialog:

- Enable checkbox
- Port number field (default 12060)

No dedicated popup. Minimal UI — color customization, band filtering, and expiry tuning deferred to future work.

## File Changes

### New Files

| File | Purpose |
|------|---------|
| `src/network/n1mmlistener.h` | N1MM UDP listener class declaration |
| `src/network/n1mmlistener.cpp` | XML parsing, spot management, auto-expiry |
| `src/dsp/spotoverlaywidget.h` | Spot overlay widget declaration |
| `src/dsp/spotoverlaywidget.cpp` | QPainter rendering, click-to-tune, label layout |

### Modified Files

| File | Changes |
|------|---------|
| `CMakeLists.txt` | Add 4 new source files to SOURCES/HEADERS |
| `src/settings/radiosettings.h/.cpp` | Add N1MM + pan toggle fields to RadioEntry |
| `src/ui/displaypopupwidget.h/.cpp` | Add PAN ON/OFF button and signal |
| `src/mainwindow.h/.cpp` | Wire up N1mmListener, SpotOverlayWidget, pan toggle logic |
| `src/dsp/panadapter_rhi.h/.cpp` | Add SpotOverlayWidget as child, reposition in resizeEvent |
