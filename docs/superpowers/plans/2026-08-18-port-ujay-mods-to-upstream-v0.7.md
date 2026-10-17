# Port UJ Fork Features onto Upstream v0.7 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Re-establish the RFKit amplifier interface, the Mini View window, and the CatServer command extensions on a fresh branch cut from current `origin/main` (post-v0.7.0-beta.5), discarding the unmaintainable merge history of `ujay-mods`.

**Architecture:** Branch `ujay-mods-v2` is cut from `origin/main`. Self-contained fork-only files (RFKit client/panel/window, MiniViewWindow) are restored verbatim from fork history via `git checkout <rev> -- <path>`. Integration points that upstream has since rewritten (`mainwindow.cpp`, `optionsdialog.cpp`, `catserver.cpp`, `radiosettings.cpp`) are re-applied by hand against the new upstream code, not merged. CatServer extensions are rewritten against upstream's new `handleCommand` signature and covered by additions to the existing `tests/test_catserver.cpp`.

**Tech Stack:** Qt 6.7+ (Core, Widgets, Network, Multimedia, Gui/GuiPrivate, SerialPort, ShaderTools, Svg, Test), C++17, CMake, libopus, OpenSSL 3.x, HIDAPI.

## Global Constraints

- **Branch:** all work lands on `ujay-mods-v2`, cut from `origin/main` at `ba6e3d1`. Do not merge or rebase `ujay-mods` into it.
- **Backup:** the pre-existing fork tip is preserved at tag `backup/ujay-mods-2026-08-18` (= `91888d0`). Never delete or move this tag.
- **Port sources:** MainWindow and OptionsDialog wiring comes from **`fff2e8e`**, NOT from `ujay-mods` HEAD. The merge `91888d0` deleted all 62 RFKit/MiniView lines from `src/mainwindow.cpp` and all 70 from `src/ui/optionsdialog.cpp`. Standalone class files (`rfkitclient`, `rfkitpanel`, `rfkitwindow`, `miniviewwindow`) are identical in both and may come from `ujay-mods`.
- **Out of scope (explicitly dropped):** N1MM UDP spot listener (`src/network/n1mmlistener.*`), the N1MM spot overlay (`src/dsp/spotoverlaywidget.*`), all AudioEngine modifications, and all fork CI changes (`.github/workflows/release.yml`, `.github/scripts/build-linux.sh`). Upstream's own DX-cluster spot pipeline replaces the N1MM one.
- **Formatting:** every commit must pass `find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror`. Style is LLVM-based, 4-space indent, 120-char limit, right-aligned pointers (`Type *name`).
- **Tests:** upstream now ships a real Qt Test suite in `tests/`. `CLAUDE.md`'s claim that there is "no test suite" is stale and is corrected in Task 10. All tests must pass via `ctest` before any task is considered complete.
- **Commit format:** Conventional Commits (`feat(scope):`, `fix(scope):`, `refactor(scope):`, `docs:`, `chore:`). Summary under 72 characters.
- **Style constants:** never hardcode colors or dimensions — use `K4Styles::Colors::*` and `K4Styles::Dimensions::*` from `src/ui/styling/k4styles.h`.
- **Fonts:** use `setPixelSize()`, never `setPointSize()`.

---

## Known Divergences From The Fork (read before starting)

These are the four places where a verbatim copy will **not** compile or will misbehave. Each is handled by a specific task.

| # | Divergence | Task |
|---|---|---|
| 1 | `CatServer::handleCommand` changed signature: fork has `QString handleCommand(const QString &cmd)`; upstream has `QByteArray handleCommand(const QString &cmd, QTcpSocket *client)`. Upstream also replaced `QList<QTcpSocket*> m_clients` + `QMap<QTcpSocket*,QByteArray> m_clientBuffers` with `QHash<QTcpSocket*, ClientState> m_clients` (per-client `aiMode`), and added `CatPushBroadcaster`. | Task 8 |
| 2 | `MiniViewWindow` consumes `SpotData`, defined in the dropped `src/network/n1mmlistener.h`. Upstream's equivalent is `DxSpot` in `src/network/dxclusterclient.h`. MiniView's spot API must be retargeted to `DxSpot`. | Task 6 |
| 3 | `CatServer` extensions call `RadioSettings::instance()->audioEnabled()`, a fork-only flag with no upstream equivalent. It must be ported as a supporting change. | Task 2 |
| 4 | `OptionsDialog`'s fork constructor took an `N1mmListener *` parameter alongside `RFKitClient *`. N1MM is dropped, so the re-applied constructor takes `RFKitClient *` only. | Task 5 |

All RadioState accessors the port depends on (`rfGain`, `rfGainB`, `setRfGain`, `setRfGainB`, `isQrpMode`, `diversityEnabled`, `subReceiverEnabled`, `filterBandwidthB`, `modeB`, `keyerSpeed`, `manualNotchEnabled`, `ifShift`, `cwPitch`) survive upstream's split of `RadioState` into `src/models/radiostate/*.cpp`. No adaptation needed there.

---

## File Structure

**Created (restored from fork history):**
- `src/network/rfkitclient.{cpp,h}` — HTTP REST client for the RF-Kit amplifier. Owns polling, parses power/SWR/temp/voltage/current/antenna/state, emits typed signals.
- `src/ui/rfkitpanel.{cpp,h}` — the amplifier readout widget (meters, mode toggle, antenna selector). Pure view; emits intent signals.
- `src/ui/rfkitwindow.{cpp,h}` — frameless floating window hosting one `RFKitPanel`; persists its position.
- `src/ui/miniviewwindow.{cpp,h}` — compact always-on-top window: both VFOs, mode, band/mode buttons, optional mini panadapter and spot list.

**Modified:**
- `CMakeLists.txt` — add the 4 new `.cpp` to `SOURCES` and 4 new `.h` to `HEADERS`.
- `src/settings/radiosettings.{cpp,h}` — RFKit settings block, MiniView settings block, `audioEnabled` flag.
- `src/network/catserver.{cpp,h}` — command extensions, rewritten for the new signature.
- `src/ui/widgets/bottommenubar.{cpp,h}` — MINI button + `miniClicked()` signal.
- `src/mainwindow.{cpp,h}` — RFKit client/window lifecycle and MiniView lifecycle + data feeds.
- `src/ui/optionsdialog.{cpp,h}` — RFKit configuration tab.
- `tests/test_catserver.cpp` — regression coverage for every new CAT command.
- `CLAUDE.md` — correct the stale "no test suite" claim.

---

### Task 1: Cut the branch and establish a green baseline

Nothing may be ported until upstream itself is proven to build and test clean in this environment. A failure here is an environment problem, not a port problem, and must not be diagnosed later while buried under 7k lines of ported code.

**Files:**
- Modify: none (branch creation only)

**Interfaces:**
- Consumes: nothing
- Produces: branch `ujay-mods-v2` at `origin/main`; a verified-good `build/` directory; the exact `cmake` invocation later tasks reuse.

- [ ] **Step 1: Confirm the backup tag exists**

```bash
cd /home/ujay/repos/QK4
git rev-parse backup/ujay-mods-2026-08-18
```

Expected: prints `91888d0...`. If it errors, STOP and recreate it with `git tag backup/ujay-mods-2026-08-18 ujay-mods` before continuing.

- [ ] **Step 2: Confirm the working tree is clean and cut the branch**

```bash
git status --porcelain          # must print nothing
git fetch origin
git checkout -b ujay-mods-v2 origin/main
git log -1 --oneline            # expect ba6e3d1 or newer origin/main tip
```

- [ ] **Step 3: Configure the build with tests enabled**

```bash
cmake -B build -DCMAKE_PREFIX_PATH="$HOME/6.8.1/gcc_64" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
```

If `$HOME/6.8.1/gcc_64` does not exist, follow the Ubuntu 24.04/WSL Qt install steps in `CLAUDE.md` first. Record the working `CMAKE_PREFIX_PATH` — every later build step reuses it.

- [ ] **Step 4: Build upstream unmodified**

```bash
cmake --build build -j$(nproc)
```

Expected: succeeds with no errors. If it fails, the environment is broken — fix it before any porting.

- [ ] **Step 5: Run the upstream test suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all tests pass. Note the pass count — later tasks must never reduce it. `RadioStateGoldenTests` may report SKIP (its fixture is gitignored); that is expected and fine.

- [ ] **Step 6: Confirm upstream is already clang-format clean**

```bash
find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror
```

Expected: no output.

- [ ] **Step 7: Commit the branch point marker**

No code changed, so there is nothing to commit. Record the baseline instead:

```bash
git log -1 --format='baseline: %H %s' | tee docs/superpowers/plans/.baseline-v2
git add docs/superpowers/plans/.baseline-v2
git commit -m "chore: record upstream baseline for ujay-mods-v2 port"
```

---

### Task 2: Port the RadioSettings support layer

Every other task depends on these settings existing. This task carries the `audioEnabled` flag (divergence #3) because CatServer cannot compile without it.

**Files:**
- Modify: `src/settings/radiosettings.h`
- Modify: `src/settings/radiosettings.cpp`

**Interfaces:**
- Consumes: nothing
- Produces:
  - `bool RadioSettings::audioEnabled() const` / `void setAudioEnabled(bool)` / `signal audioEnabledChanged(bool)`
  - `QString rfkitHost() const` / `setRfkitHost(const QString &)`
  - `quint16 rfkitPort() const` / `setRfkitPort(quint16)`
  - `bool rfkitEnabled() const` / `setRfkitEnabled(bool)`
  - `int rfkitPollInterval() const` / `setRfkitPollInterval(int)`
  - `QPoint rfkitWindowPosition() const` / `setRfkitWindowPosition(const QPoint &)`
  - `bool rfkitLowPowerEnabled() const` / `setRfkitLowPowerEnabled(bool)`
  - `double rfkitMaxDrivePower() const` / `setRfkitMaxDrivePower(double)`
  - `bool rfkitTempFahrenheit() const` / `setRfkitTempFahrenheit(bool)`
  - `QPoint miniViewWindowPosition() const` / `setMiniViewWindowPosition(const QPoint &)`
  - `bool miniViewShowBand() const` / `setMiniViewShowBand(bool)`
  - `bool miniViewShowMode() const` / `setMiniViewShowMode(bool)`
  - `bool miniViewShowSpots() const` / `setMiniViewShowSpots(bool)`
  - `bool miniViewShowPanadapter() const` / `setMiniViewShowPanadapter(bool)`
  - signals: `rfkitEnabledChanged(bool)`, `rfkitSettingsChanged()`, `rfkitPollIntervalChanged(int)`, `rfkitLowPowerChanged()`, `rfkitTempUnitChanged(bool)`, `miniViewSettingsChanged()`

- [ ] **Step 1: Extract the fork's settings diff for reference**

```bash
git show ujay-mods:src/settings/radiosettings.h > /tmp/fork-radiosettings.h
git show ujay-mods:src/settings/radiosettings.cpp > /tmp/fork-radiosettings.cpp
```

Do **not** copy these files wholesale — they also contain N1MM and audio-latency settings that are out of scope. Copy only the blocks named below.

- [ ] **Step 2: Add the declarations to `radiosettings.h`**

In the public accessors section, add:

```cpp
    // Audio master enable (fork-only): gates CatServer TX/RX and AG routing
    bool audioEnabled() const;
    void setAudioEnabled(bool enabled);

    // RFKit Amplifier settings
    QString rfkitHost() const;
    void setRfkitHost(const QString &host);
    quint16 rfkitPort() const;
    void setRfkitPort(quint16 port);
    bool rfkitEnabled() const;
    void setRfkitEnabled(bool enabled);
    int rfkitPollInterval() const;
    void setRfkitPollInterval(int intervalMs);
    QPoint rfkitWindowPosition() const;
    void setRfkitWindowPosition(const QPoint &pos);
    bool rfkitLowPowerEnabled() const;
    void setRfkitLowPowerEnabled(bool enabled);
    double rfkitMaxDrivePower() const;
    void setRfkitMaxDrivePower(double watts);
    bool rfkitTempFahrenheit() const;
    void setRfkitTempFahrenheit(bool fahrenheit);

    // Mini View window settings
    QPoint miniViewWindowPosition() const;
    void setMiniViewWindowPosition(const QPoint &pos);
    bool miniViewShowBand() const;
    void setMiniViewShowBand(bool show);
    bool miniViewShowMode() const;
    void setMiniViewShowMode(bool show);
    bool miniViewShowSpots() const;
    void setMiniViewShowSpots(bool show);
    bool miniViewShowPanadapter() const;
    void setMiniViewShowPanadapter(bool show);
```

In the `signals:` section, add:

```cpp
    void audioEnabledChanged(bool enabled);
    void rfkitEnabledChanged(bool enabled);
    void rfkitSettingsChanged();
    void rfkitPollIntervalChanged(int intervalMs);
    void rfkitLowPowerChanged();
    void rfkitTempUnitChanged(bool fahrenheit);
    void miniViewSettingsChanged();
```

In the private members section, add:

```cpp
    // Audio master enable
    bool m_audioEnabled = true;

    // RFKit Amplifier settings
    QString m_rfkitHost;
    quint16 m_rfkitPort = 8080;
    bool m_rfkitEnabled = false;
    int m_rfkitPollInterval = 1000;    // Default: 1000ms for HTTP polling
    bool m_rfkitLowPowerEnabled = false;
    double m_rfkitMaxDrivePower = 1.5; // Default: 1.5W max K4 drive power
    bool m_rfkitTempFahrenheit = false;
```

Ensure `#include <QPoint>` is present at the top of the header.

- [ ] **Step 3: Add the definitions to `radiosettings.cpp`**

Append this block before the load/save functions:

```cpp
// ============== Audio Master Enable ==============

bool RadioSettings::audioEnabled() const {
    return m_audioEnabled;
}

void RadioSettings::setAudioEnabled(bool enabled) {
    if (m_audioEnabled != enabled) {
        m_audioEnabled = enabled;
        saveSettings();
        emit audioEnabledChanged(enabled);
    }
}

// ============== RFKit Amplifier Settings ==============

QString RadioSettings::rfkitHost() const {
    return m_rfkitHost;
}

void RadioSettings::setRfkitHost(const QString &host) {
    if (m_rfkitHost != host) {
        m_rfkitHost = host;
        saveSettings();
        emit rfkitSettingsChanged();
    }
}

quint16 RadioSettings::rfkitPort() const {
    return m_rfkitPort;
}

void RadioSettings::setRfkitPort(quint16 port) {
    if (m_rfkitPort != port) {
        m_rfkitPort = port;
        saveSettings();
        emit rfkitSettingsChanged();
    }
}

bool RadioSettings::rfkitEnabled() const {
    return m_rfkitEnabled;
}

void RadioSettings::setRfkitEnabled(bool enabled) {
    if (m_rfkitEnabled != enabled) {
        m_rfkitEnabled = enabled;
        saveSettings();
        emit rfkitEnabledChanged(enabled);
    }
}

int RadioSettings::rfkitPollInterval() const {
    return m_rfkitPollInterval;
}

void RadioSettings::setRfkitPollInterval(int intervalMs) {
    intervalMs = qBound(100, intervalMs, 10000);
    if (m_rfkitPollInterval != intervalMs) {
        m_rfkitPollInterval = intervalMs;
        saveSettings();
        emit rfkitPollIntervalChanged(intervalMs);
    }
}

QPoint RadioSettings::rfkitWindowPosition() const {
    int x = m_settings.value("rfkit/windowX", 0).toInt();
    int y = m_settings.value("rfkit/windowY", 0).toInt();
    return QPoint(x, y);
}

void RadioSettings::setRfkitWindowPosition(const QPoint &pos) {
    m_settings.setValue("rfkit/windowX", pos.x());
    m_settings.setValue("rfkit/windowY", pos.y());
}

bool RadioSettings::rfkitLowPowerEnabled() const {
    return m_rfkitLowPowerEnabled;
}

void RadioSettings::setRfkitLowPowerEnabled(bool enabled) {
    if (m_rfkitLowPowerEnabled != enabled) {
        m_rfkitLowPowerEnabled = enabled;
        saveSettings();
        emit rfkitLowPowerChanged();
    }
}

double RadioSettings::rfkitMaxDrivePower() const {
    return m_rfkitMaxDrivePower;
}

void RadioSettings::setRfkitMaxDrivePower(double watts) {
    watts = qBound(0.1, watts, 100.0);
    if (m_rfkitMaxDrivePower != watts) {
        m_rfkitMaxDrivePower = watts;
        saveSettings();
        emit rfkitLowPowerChanged();
    }
}

bool RadioSettings::rfkitTempFahrenheit() const {
    return m_rfkitTempFahrenheit;
}

void RadioSettings::setRfkitTempFahrenheit(bool fahrenheit) {
    if (m_rfkitTempFahrenheit != fahrenheit) {
        m_rfkitTempFahrenheit = fahrenheit;
        saveSettings();
        emit rfkitTempUnitChanged(fahrenheit);
    }
}

// ============== Mini View Window Settings ==============

QPoint RadioSettings::miniViewWindowPosition() const {
    int x = m_settings.value("miniview/windowX", 0).toInt();
    int y = m_settings.value("miniview/windowY", 0).toInt();
    return QPoint(x, y);
}

void RadioSettings::setMiniViewWindowPosition(const QPoint &pos) {
    m_settings.setValue("miniview/windowX", pos.x());
    m_settings.setValue("miniview/windowY", pos.y());
}

bool RadioSettings::miniViewShowBand() const {
    return m_settings.value("miniview/showBand", true).toBool();
}

void RadioSettings::setMiniViewShowBand(bool show) {
    m_settings.setValue("miniview/showBand", show);
    emit miniViewSettingsChanged();
}

bool RadioSettings::miniViewShowMode() const {
    return m_settings.value("miniview/showMode", true).toBool();
}

void RadioSettings::setMiniViewShowMode(bool show) {
    m_settings.setValue("miniview/showMode", show);
    emit miniViewSettingsChanged();
}

bool RadioSettings::miniViewShowSpots() const {
    return m_settings.value("miniview/showSpots", true).toBool();
}

void RadioSettings::setMiniViewShowSpots(bool show) {
    m_settings.setValue("miniview/showSpots", show);
    emit miniViewSettingsChanged();
}

bool RadioSettings::miniViewShowPanadapter() const {
    return m_settings.value("miniview/showPanadapter", false).toBool();
}

void RadioSettings::setMiniViewShowPanadapter(bool show) {
    m_settings.setValue("miniview/showPanadapter", show);
    emit miniViewSettingsChanged();
}
```

- [ ] **Step 4: Wire load and save**

In `RadioSettings::loadSettings()`, add:

```cpp
    // Audio master enable
    m_audioEnabled = m_settings.value("audio/enabled", true).toBool();

    // RFKit Amplifier settings
    m_rfkitHost = m_settings.value("rfkit/host", "").toString();
    m_rfkitPort = m_settings.value("rfkit/port", 8080).toUInt();
    m_rfkitEnabled = m_settings.value("rfkit/enabled", false).toBool();
    m_rfkitPollInterval = m_settings.value("rfkit/pollInterval", 1000).toInt();
    m_rfkitLowPowerEnabled = m_settings.value("rfkit/lowPowerEnabled", false).toBool();
    m_rfkitMaxDrivePower = m_settings.value("rfkit/maxDrivePower", 1.5).toDouble();
    m_rfkitTempFahrenheit = m_settings.value("rfkit/tempFahrenheit", false).toBool();
```

In `RadioSettings::saveSettings()`, add:

```cpp
    // Audio master enable
    m_settings.setValue("audio/enabled", m_audioEnabled);

    // RFKit Amplifier settings
    m_settings.setValue("rfkit/host", m_rfkitHost);
    m_settings.setValue("rfkit/port", m_rfkitPort);
    m_settings.setValue("rfkit/enabled", m_rfkitEnabled);
    m_settings.setValue("rfkit/pollInterval", m_rfkitPollInterval);
    m_settings.setValue("rfkit/lowPowerEnabled", m_rfkitLowPowerEnabled);
    m_settings.setValue("rfkit/maxDrivePower", m_rfkitMaxDrivePower);
    m_settings.setValue("rfkit/tempFahrenheit", m_rfkitTempFahrenheit);
```

- [ ] **Step 5: Build and verify**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -20
ctest --test-dir build --output-on-failure
```

Expected: builds clean; test count unchanged from Task 1.

- [ ] **Step 6: Format and commit**

```bash
find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format -i
find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror
git add src/settings/radiosettings.cpp src/settings/radiosettings.h
git commit -m "feat(settings): add RFKit, MiniView, and audio-enable settings"
```

---

### Task 3: Restore the RFKit client and UI classes

These four files have no upstream counterpart and no upstream dependencies beyond Qt and `K4Styles`, so they are restored verbatim. This task proves they still compile against upstream headers.

**Files:**
- Create: `src/network/rfkitclient.cpp`, `src/network/rfkitclient.h`
- Create: `src/ui/rfkitpanel.cpp`, `src/ui/rfkitpanel.h`
- Create: `src/ui/rfkitwindow.cpp`, `src/ui/rfkitwindow.h`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `RadioSettings` RFKit accessors from Task 2.
- Produces:
  - `class RFKitClient : public QObject` — methods `connectToHost(const QString &host, quint16 port)`, `disconnectFromHost()`, `bool isConnected() const`, `startPolling(int intervalMs)`, `setOperateMode(bool operate)`, `setAntenna(int number)`, `resetError()`, `double maxForwardPower() const`, `QString deviceName() const`, `OperatingState operatingState() const`, `QList<...> antennas() const`; enum `OperatingState { StateStandby, StateOperate, ... }`; signals `connected()`, `disconnected()`, `errorOccurred(const QString &)`, `powerChanged(double fwd, double ref, double swr)`, `temperatureChanged(double tempC)`, `voltageChanged(double v)`, `currentChanged(double a)`, `operatingStateChanged(RFKitClient::OperatingState)`, `antennaChanged(int number, const QString &name)`, `antennasUpdated()`, `deviceInfoChanged(const QString &name, const QString &)`, `statusChanged(const QString &status)`.
  - `class RFKitPanel : public QWidget` — setters `setForwardPower(float, float max)`, `setReflectedPower(float)`, `setSWR(float)`, `setTemperature(float)`, `setVoltage(float)`, `setCurrent(float)`, `setMode(bool operate)`, `setAntenna(int, const QString &)`, `setAntennaCount(int)`, `setDeviceName(const QString &)`, `setStatus(const QString &)`, `setConnected(bool)`, `setTempFahrenheit(bool)`, `setOperateLocked(bool)`; signals `modeToggled(bool operate)`, `wakeUpRequested()`, `antennaChanged(int number)`, `errorResetRequested()`.
  - `class RFKitWindow : public QWidget` — `RFKitPanel *panel() const`.

- [ ] **Step 1: Restore the six files verbatim**

```bash
git checkout ujay-mods -- src/network/rfkitclient.cpp src/network/rfkitclient.h \
                          src/ui/rfkitpanel.cpp src/ui/rfkitpanel.h \
                          src/ui/rfkitwindow.cpp src/ui/rfkitwindow.h
git status --short
```

Expected: six files staged as new (`A`).

- [ ] **Step 2: Register them in `CMakeLists.txt`**

In the `set(SOURCES` list, add `src/network/rfkitclient.cpp` after `src/network/k4discovery.cpp`, and add `src/ui/rfkitpanel.cpp` and `src/ui/rfkitwindow.cpp` after `src/ui/widgets/wheelaccumulator.cpp`.

> Note: upstream moved `wheelaccumulator` to `src/utils/`. Locate the actual current line by reading `CMakeLists.txt` rather than assuming the fork's line numbers. Any position inside the list works; keep related entries adjacent.

In the `set(HEADERS` list, add `src/network/rfkitclient.h`, `src/ui/rfkitpanel.h`, and `src/ui/rfkitwindow.h` at the matching positions.

- [ ] **Step 3: Build and fix any include drift**

```bash
cmake -B build -DCMAKE_PREFIX_PATH="$HOME/6.8.1/gcc_64" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j$(nproc) 2>&1 | tail -30
```

Expected failure mode: `k4styles.h: No such file or directory`. Upstream moved it to `src/ui/styling/k4styles.h`. If the restored files include `"k4styles.h"` or `"ui/k4styles.h"`, change the include to `"ui/styling/k4styles.h"`. Verify the correct path first:

```bash
git ls-tree origin/main -r --name-only | grep k4styles
```

Apply the same treatment to any other include that fails — check `git ls-tree origin/main -r --name-only` for the header's real location. Do not change any logic, only include paths.

- [ ] **Step 4: Confirm the build is clean and tests still pass**

```bash
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Expected: builds clean, test count unchanged. The classes are compiled but not yet instantiated — that is correct at this stage.

- [ ] **Step 5: Format and commit**

```bash
find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format -i
find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror
git add CMakeLists.txt src/network/rfkitclient.cpp src/network/rfkitclient.h \
        src/ui/rfkitpanel.cpp src/ui/rfkitpanel.h src/ui/rfkitwindow.cpp src/ui/rfkitwindow.h
git commit -m "feat(rfkit): restore RF-Kit amplifier client, panel, and window"
```

---

### Task 4: Wire RFKit into MainWindow

Re-applies the wiring that merge `91888d0` deleted. Source of truth is `fff2e8e:src/mainwindow.cpp`.

**Files:**
- Modify: `src/mainwindow.h`
- Modify: `src/mainwindow.cpp`

**Interfaces:**
- Consumes: `RFKitClient`, `RFKitWindow`, `RFKitPanel` (Task 3); RFKit settings (Task 2).
- Produces: private slots `onRfkitConnected()`, `onRfkitDisconnected()`, `onRfkitError(const QString &error)`, `onRfkitEnabledChanged(bool enabled)`, `onRfkitSettingsChanged()`, `checkRfkitDrivePower()`, `updateRfkitStatus()`; members `RFKitClient *m_rfkitClient`, `RFKitWindow *m_rfkitWindow`, `QLabel *m_rfkitStatusLabel`.

- [ ] **Step 1: Dump the reference wiring**

```bash
git show fff2e8e:src/mainwindow.cpp > /tmp/fork-mainwindow.cpp
git show fff2e8e:src/mainwindow.h  > /tmp/fork-mainwindow.h
grep -n -i "rfkit" /tmp/fork-mainwindow.cpp
```

Keep `/tmp/fork-mainwindow.cpp` open while working. Every block below is quoted from it.

- [ ] **Step 2: Add declarations to `mainwindow.h`**

With the other forward declarations:

```cpp
class RFKitClient;
class RFKitWindow;
```

In the private slots section:

```cpp
    // RFKit slots
    void onRfkitConnected();
    void onRfkitDisconnected();
    void onRfkitError(const QString &error);
    void onRfkitEnabledChanged(bool enabled);
    void onRfkitSettingsChanged();
    void checkRfkitDrivePower();
    void updateRfkitStatus();
```

In the private members section:

```cpp
    // RFKit amplifier client
    RFKitClient *m_rfkitClient = nullptr;
    RFKitWindow *m_rfkitWindow = nullptr;
    QLabel *m_rfkitStatusLabel = nullptr;
```

- [ ] **Step 3: Add the includes to `mainwindow.cpp`**

```cpp
#include "network/rfkitclient.h"
#include "ui/rfkitwindow.h"
#include "ui/rfkitpanel.h"
```

- [ ] **Step 4: Create the floating window during UI setup**

In the function that builds the auxiliary windows (in the fork this was around line 3856, inside the window-creation section — locate the equivalent upstream setup function by finding where other floating windows are constructed):

```cpp
    // ===== RFKit Floating Window =====
    m_rfkitWindow = new RFKitWindow(this);
    m_rfkitWindow->hide();
```

- [ ] **Step 5: Construct the client and connect every signal**

In the constructor's subsystem-setup section, after `m_radioState` exists:

```cpp
    // RFKit amplifier client (HTTP REST)
    m_rfkitClient = new RFKitClient(this);

    // Connect RFKit signals
    connect(m_rfkitClient, &RFKitClient::connected, this, &MainWindow::onRfkitConnected);
    connect(m_rfkitClient, &RFKitClient::disconnected, this, &MainWindow::onRfkitDisconnected);
    connect(m_rfkitClient, &RFKitClient::errorOccurred, this, &MainWindow::onRfkitError);

    // Connect RFKit data signals to panel
    connect(m_rfkitClient, &RFKitClient::powerChanged, this, [this](double fwd, double ref, double swr) {
        m_rfkitWindow->panel()->setForwardPower(static_cast<float>(fwd),
                                                static_cast<float>(m_rfkitClient->maxForwardPower()));
        m_rfkitWindow->panel()->setReflectedPower(static_cast<float>(ref));
        m_rfkitWindow->panel()->setSWR(static_cast<float>(swr));
    });
    connect(m_rfkitClient, &RFKitClient::temperatureChanged, this,
            [this](double tempC) { m_rfkitWindow->panel()->setTemperature(static_cast<float>(tempC)); });
    connect(m_rfkitClient, &RFKitClient::voltageChanged, this,
            [this](double v) { m_rfkitWindow->panel()->setVoltage(static_cast<float>(v)); });
    connect(m_rfkitClient, &RFKitClient::currentChanged, this,
            [this](double a) { m_rfkitWindow->panel()->setCurrent(static_cast<float>(a)); });
    connect(m_rfkitClient, &RFKitClient::operatingStateChanged, this, [this](RFKitClient::OperatingState state) {
        m_rfkitWindow->panel()->setMode(state == RFKitClient::StateOperate);
    });
    connect(m_rfkitClient, &RFKitClient::antennaChanged, this,
            [this](int number, const QString &name) { m_rfkitWindow->panel()->setAntenna(number, name); });
    connect(m_rfkitClient, &RFKitClient::antennasUpdated, this,
            [this]() { m_rfkitWindow->panel()->setAntennaCount(m_rfkitClient->antennas().size()); });
    connect(m_rfkitClient, &RFKitClient::deviceInfoChanged, this,
            [this](const QString &name, const QString &) { m_rfkitWindow->panel()->setDeviceName(name); });
    connect(m_rfkitClient, &RFKitClient::statusChanged, this,
            [this](const QString &status) { m_rfkitWindow->panel()->setStatus(status); });

    // Connect panel signals to send RFKit commands
    connect(m_rfkitWindow->panel(), &RFKitPanel::modeToggled, this,
            [this](bool operate) { m_rfkitClient->setOperateMode(operate); });
    connect(m_rfkitWindow->panel(), &RFKitPanel::wakeUpRequested, this,
            [this]() { m_rfkitClient->setOperateMode(false); });
    connect(m_rfkitWindow->panel(), &RFKitPanel::antennaChanged, this,
            [this](int number) { m_rfkitClient->setAntenna(number); });
    connect(m_rfkitWindow->panel(), &RFKitPanel::errorResetRequested, this, [this]() { m_rfkitClient->resetError(); });

    // Connect to settings for RFKit enable/disable and settings changes
    connect(RadioSettings::instance(), &RadioSettings::rfkitEnabledChanged, this, &MainWindow::onRfkitEnabledChanged);
    connect(RadioSettings::instance(), &RadioSettings::rfkitSettingsChanged, this, &MainWindow::onRfkitSettingsChanged);
    connect(RadioSettings::instance(), &RadioSettings::rfkitLowPowerChanged, this, &MainWindow::checkRfkitDrivePower);
    connect(m_radioState, &RadioState::rfPowerChanged, this, [this](double, bool) { checkRfkitDrivePower(); });
    connect(RadioSettings::instance(), &RadioSettings::rfkitTempUnitChanged, this,
            [this](bool fahrenheit) { m_rfkitWindow->panel()->setTempFahrenheit(fahrenheit); });
    m_rfkitWindow->panel()->setTempFahrenheit(RadioSettings::instance()->rfkitTempFahrenheit());

    // Initialize RFKit status display
    updateRfkitStatus();
```

This must run **after** Step 4 creates `m_rfkitWindow` — every lambda dereferences it. If the upstream constructor orders things differently, move the window creation earlier rather than reordering these connects.

- [ ] **Step 6: Add the status-bar label**

In the status-bar construction function, alongside the other status labels:

```cpp
    // RFKit status
    m_rfkitStatusLabel = new QLabel("", statusBar);
    m_rfkitStatusLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(K4Styles::Colors::InactiveGray));
    m_rfkitStatusLabel->hide();
    layout->addWidget(m_rfkitStatusLabel);
```

- [ ] **Step 7: Add the slot implementations**

At the end of `mainwindow.cpp`:

```cpp
// ============== RFKit Amplifier Slots ==============

void MainWindow::onRfkitConnected() {
    qDebug() << "RFKit: Connected to amplifier";
    int pollInterval = RadioSettings::instance()->rfkitPollInterval();
    m_rfkitClient->startPolling(pollInterval);
    updateRfkitStatus();
}

void MainWindow::onRfkitDisconnected() {
    qDebug() << "RFKit: Disconnected from amplifier";
    updateRfkitStatus();
}

void MainWindow::onRfkitError(const QString &error) {
    qWarning() << "RFKit: Error -" << error;
    updateRfkitStatus();
}

void MainWindow::onRfkitEnabledChanged(bool enabled) {
    if (enabled) {
        QString host = RadioSettings::instance()->rfkitHost();
        if (!host.isEmpty())
            m_rfkitClient->connectToHost(host, RadioSettings::instance()->rfkitPort());
    } else {
        m_rfkitClient->disconnectFromHost();
    }
    updateRfkitStatus();
}

void MainWindow::onRfkitSettingsChanged() {
    if (RadioSettings::instance()->rfkitEnabled()) {
        m_rfkitClient->disconnectFromHost();
        QString host = RadioSettings::instance()->rfkitHost();
        if (!host.isEmpty())
            m_rfkitClient->connectToHost(host, RadioSettings::instance()->rfkitPort());
    }
}

void MainWindow::updateRfkitStatus() {
    bool enabled = RadioSettings::instance()->rfkitEnabled();
    bool connected = m_rfkitClient && m_rfkitClient->isConnected();

    if (!enabled) {
        m_rfkitStatusLabel->hide();
    } else {
        m_rfkitStatusLabel->show();
        if (connected) {
            QString name = m_rfkitClient->deviceName();
            m_rfkitStatusLabel->setText(name.isEmpty() ? "RFKit" : name);
            m_rfkitStatusLabel->setStyleSheet(
                QString("color: %1; font-size: 12px;").arg(K4Styles::Colors::ActiveGreen));
        } else {
            m_rfkitStatusLabel->setText("RFKit");
            m_rfkitStatusLabel->setStyleSheet(
                QString("color: %1; font-size: 12px;").arg(K4Styles::Colors::InactiveGray));
        }
    }

    // Show RFKit window only when enabled AND connected
    m_rfkitWindow->setVisible(enabled && connected);
    m_rfkitWindow->panel()->setConnected(connected);
}

void MainWindow::checkRfkitDrivePower() {
    RadioSettings *settings = RadioSettings::instance();
    if (!settings->rfkitEnabled() || !settings->rfkitLowPowerEnabled()) {
        m_rfkitWindow->panel()->setOperateLocked(false);
        return;
    }

    double maxDrive = settings->rfkitMaxDrivePower();
    double currentPower = m_radioState->rfPower();
    bool overLimit = currentPower > maxDrive;
    m_rfkitWindow->panel()->setOperateLocked(overLimit);

    if (overLimit && m_rfkitClient->isConnected() && m_rfkitClient->operatingState() == RFKitClient::StateOperate) {
        qWarning() << "RFKit: K4 drive power" << currentPower << "W exceeds limit" << maxDrive << "W - forcing standby";
        m_rfkitClient->setOperateMode(false);
    }
}
```

Verify the exact `K4Styles::Colors::` member names against `src/ui/styling/k4styles.h` — if `ActiveGreen` or `InactiveGray` were renamed upstream, use the current names rather than adding new constants.

- [ ] **Step 8: Connect on radio connect, and disconnect on teardown**

In the "radio connected" handler:

```cpp
    // Connect RFKit if enabled and configured
    if (RadioSettings::instance()->rfkitEnabled() && !RadioSettings::instance()->rfkitHost().isEmpty()) {
        m_rfkitClient->connectToHost(RadioSettings::instance()->rfkitHost(), RadioSettings::instance()->rfkitPort());
    }
```

In the "radio disconnected" handler:

```cpp
        // Disconnect RFKit when K4 disconnects
        if (m_rfkitClient->isConnected()) {
            m_rfkitClient->disconnectFromHost();
        }
```

In the destructor and in `closeEvent` (both sites in the fork):

```cpp
    if (m_rfkitClient) {
        disconnect(m_rfkitClient, nullptr, this, nullptr);
        m_rfkitClient->disconnectFromHost();
    }
```

- [ ] **Step 9: Build and verify**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -30
ctest --test-dir build --output-on-failure
```

Expected: builds clean, tests unchanged.

- [ ] **Step 10: Smoke-test the app launches**

```bash
LD_LIBRARY_PATH="$HOME/6.8.1/gcc_64/lib:$LD_LIBRARY_PATH" timeout 15 ./build/QK4 2>&1 | head -20
```

Expected: the window appears and no RFKit-related warnings are printed. With RFKit disabled by default, the status label and floating window must both stay hidden.

- [ ] **Step 11: Format and commit**

```bash
find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format -i
find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror
git add src/mainwindow.cpp src/mainwindow.h
git commit -m "feat(rfkit): wire amplifier client and window into MainWindow"
```

---

### Task 5: Restore the RFKit settings tab in OptionsDialog

`fff2e8e:src/ui/optionsdialog.cpp` carried 70 RFKit references that merge `91888d0` deleted. Divergence #4 applies: the fork constructor took an `N1mmListener *`, which is dropped here.

**Files:**
- Modify: `src/ui/optionsdialog.h`
- Modify: `src/ui/optionsdialog.cpp`
- Modify: `src/mainwindow.cpp` (constructor call site)

**Interfaces:**
- Consumes: `RFKitClient` (Task 3); RFKit settings (Task 2).
- Produces: an `OptionsDialog` constructor accepting an `RFKitClient *` and an "Amplifier"/"RFKit" tab that reads and writes every RFKit setting.

- [ ] **Step 1: Dump the reference implementation**

```bash
git show fff2e8e:src/ui/optionsdialog.cpp > /tmp/fork-optionsdialog.cpp
git show fff2e8e:src/ui/optionsdialog.h  > /tmp/fork-optionsdialog.h
grep -n -i "rfkit\|n1mm" /tmp/fork-optionsdialog.cpp
diff <(git show origin/main:src/ui/optionsdialog.h) /tmp/fork-optionsdialog.h
```

The `diff` shows exactly what to re-add and what upstream changed independently.

- [ ] **Step 2: Extend the constructor signature**

Add an `RFKitClient *rfkitClient` parameter to the upstream `OptionsDialog` constructor, placed immediately before the trailing `QWidget *parent`. Do **not** add an `N1mmListener *` parameter. Add `class RFKitClient;` as a forward declaration and store the pointer in a member `RFKitClient *m_rfkitClient = nullptr;`.

- [ ] **Step 3: Port the RFKit tab**

Copy the RFKit tab construction and its handlers from `/tmp/fork-optionsdialog.cpp` into the upstream dialog, following upstream's current tab-registration pattern. Omit any N1MM controls. The tab must expose: enable checkbox, host, port, poll interval, low-power-lockout checkbox, max drive power, and the °C/°F toggle — each bound to the matching `RadioSettings` setter from Task 2.

- [ ] **Step 4: Update the call site in `mainwindow.cpp`**

Replace the fork's call, which referenced the now-absent `m_n1mmListener`:

```cpp
        m_optionsDialog = new OptionsDialog(m_radioState, m_audioEngine, m_kpodDevice, m_catServer, m_halikeyDevice,
                                            m_rfkitClient, m_kpa1500Client, this);
```

Match the parameter order to whatever upstream's constructor now declares, with `m_rfkitClient` inserted where Step 2 placed it.

- [ ] **Step 5: Build and verify**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -30
ctest --test-dir build --output-on-failure
```

- [ ] **Step 6: Verify the tab round-trips settings**

```bash
LD_LIBRARY_PATH="$HOME/6.8.1/gcc_64/lib:$LD_LIBRARY_PATH" ./build/QK4
```

Open Tools > Settings, find the RFKit tab, enable it, set host `192.168.1.50` and port `8080`, close the app, reopen it, and confirm both values persisted.

- [ ] **Step 7: Format and commit**

```bash
find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format -i
find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror
git add src/ui/optionsdialog.cpp src/ui/optionsdialog.h src/mainwindow.cpp
git commit -m "feat(rfkit): restore amplifier settings tab in OptionsDialog"
```

---

### Task 6: Restore MiniViewWindow and retarget its spot API to DxSpot

Divergence #2. The fork's `MiniViewWindow` takes `SpotData` from the dropped `n1mmlistener.h`; upstream's live spot source emits `DxSpot` from `dxclusterclient.h`.

**Files:**
- Create: `src/ui/miniviewwindow.cpp`, `src/ui/miniviewwindow.h`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: MiniView settings (Task 2); `DxSpot` from `src/network/dxclusterclient.h`.
- Produces:
  - `class MiniViewWindow : public QWidget` — `setFrequencyA(const QString &)`, `setFrequencyB(const QString &)`, `setModeA(const QString &)`, `setModeB(const QString &)`, `setPanMode(const QString &)`, `setPanFilterBandwidth(int)`, `setPanIfShift(int)`, `setPanCwPitch(int)`, `setPanNotchFilter(bool enabled, int pitch)`, `updateSpectrum(const QByteArray &data)`, `bool isPanadapterVisible() const`, `applySettings()`, `QPushButton *bandButton() const`, `QPushButton *modeButton() const`
  - spot API, **retargeted**: `void addSpot(const DxSpot &spot)`, `void removeSpot(const QString &callsign)`, `void clearSpots()`
  - signals: `restoreRequested()`, `bandClicked()`, `modeClicked()`

- [ ] **Step 1: Restore the two files**

```bash
git checkout ujay-mods -- src/ui/miniviewwindow.cpp src/ui/miniviewwindow.h
```

- [ ] **Step 2: Compare the two spot structs before editing**

```bash
git show ujay-mods:src/network/n1mmlistener.h | sed -n '1,30p'
git show origin/main:src/network/dxclusterclient.h | sed -n '10,30p'
```

Note each field name in `SpotData` and its counterpart in `DxSpot`. Callsign and frequency exist in both; comment/time/spotter fields may be named differently, and `SpotData`'s N1MM-specific fields (multiplier/worked flags) may have no counterpart.

- [ ] **Step 3: Retarget the type**

In `src/ui/miniviewwindow.h`:
- Replace `#include "network/n1mmlistener.h"` with `#include "network/dxclusterclient.h"`.
- Change `void addSpot(const SpotData &spot);` to `void addSpot(const DxSpot &spot);`.
- Change `QList<SpotData> m_spots;` to `QList<DxSpot> m_spots;`.

In `src/ui/miniviewwindow.cpp`, update the `addSpot` definition to match and rename every field access to its `DxSpot` equivalent per Step 2. For any `SpotData` field with no `DxSpot` counterpart, drop the corresponding display element rather than inventing a value.

- [ ] **Step 4: Register in `CMakeLists.txt`**

Add `src/ui/miniviewwindow.cpp` to `SOURCES` and `src/ui/miniviewwindow.h` to `HEADERS`, next to the RFKit entries added in Task 3.

- [ ] **Step 5: Build and fix include drift**

```bash
cmake -B build -DCMAKE_PREFIX_PATH="$HOME/6.8.1/gcc_64" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j$(nproc) 2>&1 | tail -30
```

Apply the same include-path corrections as Task 3 Step 3 (`k4styles.h` → `ui/styling/k4styles.h`, and any other header upstream relocated).

- [ ] **Step 6: Verify tests still pass**

```bash
ctest --test-dir build --output-on-failure
```

- [ ] **Step 7: Format and commit**

```bash
find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format -i
find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror
git add CMakeLists.txt src/ui/miniviewwindow.cpp src/ui/miniviewwindow.h
git commit -m "feat(miniview): restore mini view window, retarget spots to DxSpot"
```

---

### Task 7: Add the MINI button to BottomMenuBar

**Files:**
- Modify: `src/ui/widgets/bottommenubar.h`
- Modify: `src/ui/widgets/bottommenubar.cpp`

**Interfaces:**
- Consumes: nothing
- Produces: `QPushButton *BottomMenuBar::miniButton() const`; signal `void miniClicked()`.

- [ ] **Step 1: Add the declarations to `bottommenubar.h`**

In the public accessors, after `pttButton()`:

```cpp
    QPushButton *miniButton() const { return m_miniBtn; }
```

In `signals:`, after `txClicked()`:

```cpp
    void miniClicked();
```

In the private members, before `m_pttBtn`:

```cpp
    QPushButton *m_miniBtn;
```

- [ ] **Step 2: Create and wire the button in `setupUi()`**

After the `layout->addStretch();` that precedes the PTT button:

```cpp
    // MINI button (switch to compact mini view)
    m_miniBtn = createMenuButton("MINI");
    layout->addWidget(m_miniBtn);

    layout->addSpacing(10);
```

And with the other button connects, after the `txClicked` connect:

```cpp
    connect(m_miniBtn, &QPushButton::clicked, this, &BottomMenuBar::miniClicked);
```

- [ ] **Step 3: Build and verify the button renders**

```bash
cmake --build build -j$(nproc)
LD_LIBRARY_PATH="$HOME/6.8.1/gcc_64/lib:$LD_LIBRARY_PATH" ./build/QK4
```

Expected: a MINI button appears in the bottom menu bar immediately left of PTT. Clicking it does nothing yet — Task 8 connects it.

- [ ] **Step 4: Format and commit**

```bash
find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format -i
find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror
git add src/ui/widgets/bottommenubar.cpp src/ui/widgets/bottommenubar.h
git commit -m "feat(ui): add MINI button to bottom menu bar"
```

---

### Task 8: Wire MiniView into MainWindow

**Files:**
- Modify: `src/mainwindow.h`
- Modify: `src/mainwindow.cpp`

**Interfaces:**
- Consumes: `MiniViewWindow` (Task 6); `BottomMenuBar::miniClicked()` (Task 7); MiniView settings (Task 2); upstream's `DxClusterClient::spotReceived(const DxSpot &)`.
- Produces: member `MiniViewWindow *m_miniViewWindow = nullptr;` and its full lifecycle.

- [ ] **Step 1: Declare the member**

In `mainwindow.h`, forward declare `class MiniViewWindow;` and add:

```cpp
    MiniViewWindow *m_miniViewWindow = nullptr;
```

Add `#include "ui/miniviewwindow.h"` to `mainwindow.cpp`.

- [ ] **Step 2: Create the window parentless and connect its signals**

In the same setup function that created `m_rfkitWindow` (Task 4 Step 4):

```cpp
    // ===== Mini View Window =====
    m_miniViewWindow = new MiniViewWindow(nullptr); // No parent so it persists when main window hides
    m_miniViewWindow->hide();
    connect(m_miniViewWindow, &MiniViewWindow::restoreRequested, this, [this]() {
        m_miniViewWindow->hide();
        show();
        raise();
        activateWindow();
    });
    connect(m_miniViewWindow, &MiniViewWindow::bandClicked, this, [this]() {
        if (m_bandPopup)
            m_bandPopup->showAboveButton(m_miniViewWindow->bandButton());
    });
    connect(m_miniViewWindow, &MiniViewWindow::modeClicked, this, [this]() {
        if (m_modePopup)
            m_modePopup->showAboveButton(m_miniViewWindow->modeButton());
    });
    connect(RadioSettings::instance(), &RadioSettings::miniViewSettingsChanged, this, [this]() {
        if (m_miniViewWindow)
            m_miniViewWindow->applySettings();
    });
```

`MiniViewWindow` is deliberately parentless so it survives `hide()` on the main window. That makes manual deletion mandatory — Step 6 handles it.

Cross-check the popup member names (`m_bandPopup`, `m_modePopup`) and their `showAboveButton` signature against upstream's current `mainwindow.h`; the fork's names may have been renamed. The fork's bodies also re-created popups when null — reproduce upstream's own popup-creation idiom here rather than the fork's if they differ.

- [ ] **Step 3: Show the mini view on MINI click**

Connect `BottomMenuBar::miniClicked` where the other bottom-bar signals are connected, populating every field before showing:

```cpp
    connect(m_bottomMenuBar, &BottomMenuBar::miniClicked, this, [this]() {
        m_miniViewWindow->setFrequencyA(m_vfoA->frequencyDisplay()->frequency());
        m_miniViewWindow->setFrequencyB(m_vfoB->frequencyDisplay()->frequency());
        m_miniViewWindow->setModeA(m_modeALabel->text());
        m_miniViewWindow->setModeB(m_modeBLabel->text());
        QString modeStr = m_modeALabel->text();
        m_miniViewWindow->setPanMode(modeStr);
        m_miniViewWindow->setPanFilterBandwidth(m_radioState->filterBandwidth());
        m_miniViewWindow->setPanIfShift(m_radioState->ifShift());
        m_miniViewWindow->setPanCwPitch(m_radioState->cwPitch());
        m_miniViewWindow->setPanNotchFilter(m_radioState->manualNotchEnabled(), m_radioState->manualNotchPitch());
        m_miniViewWindow->show();
        m_miniViewWindow->raise();
        hide();
    });
```

Verify `m_vfoA`, `m_vfoB`, `m_modeALabel`, `m_modeBLabel`, and `m_bottomMenuBar` still carry these names upstream; substitute the current names where they differ.

- [ ] **Step 4: Feed live frequency and mode updates**

In the VFO A frequency-changed handler, after the main display update:

```cpp
    if (m_miniViewWindow && m_miniViewWindow->isVisible())
        m_miniViewWindow->setFrequencyA(formatted);
```

In the VFO B handler:

```cpp
    if (m_miniViewWindow && m_miniViewWindow->isVisible())
        m_miniViewWindow->setFrequencyB(formatted);
```

In the mode-changed handler:

```cpp
    if (m_miniViewWindow && m_miniViewWindow->isVisible()) {
        m_miniViewWindow->setModeA(modeA);
        m_miniViewWindow->setModeB(modeB);
    }
```

Use whatever local variable actually holds the formatted string in each upstream handler — `formatted`, `modeA`, and `modeB` are the fork's names.

- [ ] **Step 5: Feed the mini panadapter and spots**

In the spectrum-data handler, guarded so the work is skipped when the mini panadapter is hidden:

```cpp
    if (receiver == 0 && m_miniViewWindow && m_miniViewWindow->isVisible() && m_miniViewWindow->isPanadapterVisible()) {
        m_miniViewWindow->updateSpectrum(data);
    }
```

Where upstream handles `DxClusterClient::spotReceived`, mirror the spot into the mini view:

```cpp
        if (m_miniViewWindow)
            m_miniViewWindow->addSpot(spot);
```

and in the corresponding spot-removal handler:

```cpp
        if (m_miniViewWindow)
            m_miniViewWindow->removeSpot(callsign);
```

If upstream has no spot-removal signal, omit the `removeSpot` mirror and leave the method unused rather than inventing a signal.

- [ ] **Step 6: Delete it explicitly on teardown**

In the destructor, after the RFKit teardown from Task 4 Step 8:

```cpp
    delete m_miniViewWindow; // No parent, must delete manually
```

- [ ] **Step 7: Build and verify**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -30
ctest --test-dir build --output-on-failure
```

- [ ] **Step 8: Functionally verify the mini view**

```bash
LD_LIBRARY_PATH="$HOME/6.8.1/gcc_64/lib:$LD_LIBRARY_PATH" ./build/QK4
```

Confirm all of: clicking MINI hides the main window and shows the compact one; both VFO frequencies are correct; the restore control brings the main window back; the mini window's position survives a restart; closing the app from the mini view exits cleanly with no crash on teardown.

- [ ] **Step 9: Format and commit**

```bash
find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format -i
find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror
git add src/mainwindow.cpp src/mainwindow.h
git commit -m "feat(miniview): wire mini view window into MainWindow"
```

---

### Task 9: Port the CatServer command extensions

Divergence #1, and the highest-risk task. Upstream rewrote `catserver.cpp` (−194/+102 lines) and changed the handler signature. The fork's diff cannot be applied — it must be re-expressed. Unlike every other task, this one has real test coverage available, so it is written test-first.

**Files:**
- Modify: `src/network/catserver.h`
- Modify: `src/network/catserver.cpp`
- Test: `tests/test_catserver.cpp`

**Interfaces:**
- Consumes: `RadioSettings::audioEnabled()` (Task 2); RadioState accessors listed in Known Divergences.
- Produces: signals `void volumeRequested(int level)` and `void subVolumeRequested(int level)` on `CatServer` (level 0–100).

**Behaviour being added** — each row is one test below:

| Command | Behaviour |
|---|---|
| `$` prefix parsing | `MD$;`, `BW$;`, `RG$;` parse as prefix `MD$`/`BW$`/`RG$`, not prefix + args |
| `TX;` / `RX;` | forwarded to K4 when audio disabled or mode is CW/CW-R; otherwise gate audio via `pttRequested` |
| `RU`/`RD`/`RC` | forward, then emit `RT;` and `RO;` to re-query RIT state |
| `UP`/`DN`/`UPB`/`DNB` | forward verbatim |
| `RG+` / `RG-` / `RG/` | adjust/toggle RF gain optimistically, then re-query `RG;` |
| `RG$+` / `RG$-` / `RG$/` | same for sub RX, re-query `RG$;` |
| `PC;` | response includes the `H`/`L` power-mode suffix |
| `RG;` / `RG$;` | respond `RG-nn;` / `RG$-nn;` |
| `AG;` / `AG$;` | serve local volume when audio enabled, else forward to K4 |
| `AGnnn;` / `AG$nnn;` | set local volume (0–60 → 0–100) and emit `volumeRequested`/`subVolumeRequested` |
| `KY;` | report `KY1;` when ≥50 chars pending, else `KY0;` |
| `KYtext;` | track pending char count, schedule decay at keyer WPM, forward |
| `TB;` | report real pending count as `TBn00;` |
| `SB;` | `3` when diversity, `1` when sub RX on, else `0` |
| `MD$;` / `BW$;` | respond from sub-VFO state |
| unrecognized GET | return empty, do not forward |
| SET commands | optimistically applied to RadioState via `parseCATCommand` |

- [ ] **Step 1: Read the upstream handler in full before changing anything**

```bash
git show origin/main:src/network/catserver.cpp | sed -n '136,340p'
```

Note precisely: the return type is `QByteArray`, the second parameter is `QTcpSocket *client`, and per-client `aiMode` lives in `m_clients[client].aiMode`. Every `return QString()` in the fork's code becomes `return QByteArray()`, and every `return QString("XX;")` becomes `return QByteArray("XX;")` or `.toUtf8()` on a built `QString`.

- [ ] **Step 2: Write the failing tests**

Append to `tests/test_catserver.cpp`, inside the `private slots:` section:

```cpp
    // =========================================================================
    // UJ fork extensions
    // =========================================================================

    void testSubVfoModeQuery() {
        RadioState rs;
        rs.parseCATCommand("MD$3;");

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QString response = sendCommand(server, "MD$;");
        QCOMPARE(response, QString("MD$3;"));
    }

    void testSubVfoBandwidthQuery() {
        RadioState rs;
        rs.parseCATCommand("BW$0400;");

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QString response = sendCommand(server, "BW$;");
        QCOMPARE(response, QString("BW$0400;"));
    }

    void testPowerQueryIncludesModeSuffix() {
        RadioState rs;
        rs.parseCATCommand("PC010H;");

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QString response = sendCommand(server, "PC;");
        QCOMPARE(response, QString("PC010H;"));
    }

    void testRfGainQuery() {
        RadioState rs;
        rs.parseCATCommand("RG-20;");

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QString response = sendCommand(server, "RG;");
        QCOMPARE(response, QString("RG-20;"));
    }

    void testRfGainIncrementForwardsAndRequeries() {
        RadioState rs;
        rs.parseCATCommand("RG-20;");

        CatServer server(&rs);
        QVERIFY(server.start(0));
        QSignalSpy spy(&server, &CatServer::catCommandReceived);

        sendCommand(server, "RG+;");

        // Forwards the raw command, then re-queries authoritative value
        QVERIFY(spy.count() >= 2);
        QCOMPARE(spy.at(0).at(0).toString(), QString("RG+;"));
        QCOMPARE(spy.at(spy.count() - 1).at(0).toString(), QString("RG;"));
    }

    void testRfGainToggleZeroesThenRestores() {
        RadioState rs;
        rs.parseCATCommand("RG-20;");

        CatServer server(&rs);
        QVERIFY(server.start(0));

        sendCommand(server, "RG/;");
        QCOMPARE(rs.rfGain(), 0);

        sendCommand(server, "RG/;");
        QCOMPARE(rs.rfGain(), 20);
    }

    void testRitUpRequeriesRitState() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));
        QSignalSpy spy(&server, &CatServer::catCommandReceived);

        sendCommand(server, "RU;");

        QCOMPARE(spy.count(), 3);
        QCOMPARE(spy.at(0).at(0).toString(), QString("RU;"));
        QCOMPARE(spy.at(1).at(0).toString(), QString("RT;"));
        QCOMPARE(spy.at(2).at(0).toString(), QString("RO;"));
    }

    void testVfoStepCommandsForward() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));
        QSignalSpy spy(&server, &CatServer::catCommandReceived);

        sendCommand(server, "UP;");
        sendCommand(server, "DNB;");

        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(0).at(0).toString(), QString("UP;"));
        QCOMPARE(spy.at(1).at(0).toString(), QString("DNB;"));
    }

    void testAudioGainSetEmitsVolumeRequest() {
        RadioSettings::instance()->setAudioEnabled(true);
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));
        QSignalSpy spy(&server, &CatServer::volumeRequested);

        sendCommand(server, "AG030;");

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 50); // 30/60 -> 50%
    }

    void testAudioGainQueryReturnsLocalVolume() {
        RadioSettings::instance()->setAudioEnabled(true);
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        sendCommand(server, "AG030;");
        QString response = sendCommand(server, "AG;");
        QCOMPARE(response, QString("AG030;"));
    }

    void testSubReceiverStatus() {
        RadioState rs;
        rs.parseCATCommand("SB1;");

        CatServer server(&rs);
        QVERIFY(server.start(0));

        QString response = sendCommand(server, "SB;");
        QCOMPARE(response, QString("SB1;"));
    }

    void testKeyerBufferEmptyByDefault() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        QCOMPARE(sendCommand(server, "KY;"), QString("KY0;"));
        QCOMPARE(sendCommand(server, "TB;"), QString("TB000;"));
    }

    void testUnrecognizedGetIsNotForwarded() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));
        QSignalSpy spy(&server, &CatServer::catCommandReceived);

        QString response = sendCommand(server, "ZZ;");

        QCOMPARE(response, QString());
        QCOMPARE(spy.count(), 0);
    }

    void testSetCommandUpdatesRadioStateOptimistically() {
        RadioState rs;
        CatServer server(&rs);
        QVERIFY(server.start(0));

        sendCommand(server, "FA00021074000;");

        QCOMPARE(rs.vfoA(), Q_UINT64_C(21074000));
    }
```

Add `#include "settings/radiosettings.h"` to the test file's includes.

- [ ] **Step 3: Add the settings dependency to the test target**

`tests/CMakeLists.txt` builds `test_catserver` from an explicit source list. `CatServer` now references `RadioSettings`, so add to the `add_executable(test_catserver ...)` list:

```cmake
    ${CMAKE_SOURCE_DIR}/src/settings/radiosettings.cpp
```

and add `Qt6::Gui` to its `target_link_libraries` if `RadioSettings` pulls in `QPoint`-related symbols that fail to link.

- [ ] **Step 3a: Isolate the test QSettings store**

`RadioSettings` is a singleton backed by `QSettings`, so `setAudioEnabled(true)` in a test would otherwise write into the developer's real QK4 configuration and could flip audio off in their running app. Redirect the store before any test runs by adding an `initTestCase` slot at the top of the `private slots:` section:

```cpp
    void initTestCase() {
        // Redirect QSettings to a throwaway org/app so tests never touch the
        // developer's real QK4 configuration.
        QCoreApplication::setOrganizationName("QK4Test");
        QCoreApplication::setApplicationName("CatServerTest");
        QSettings().clear();
    }
```

Add `#include <QSettings>` to the test includes. Verify the redirect actually took effect before trusting any test that writes settings:

```bash
ctest --test-dir build -R CatServer --output-on-failure
ls ~/.config/QK4Test/    # expect the throwaway store to appear here
```

If `RadioSettings` hardcodes its own organization/application name in its `QSettings` constructor rather than inheriting the application's, this redirect will not work. In that case, set the `XDG_CONFIG_HOME` environment variable for the test instead:

```cmake
set_tests_properties(CatServerTests PROPERTIES ENVIRONMENT "XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-config")
```

- [ ] **Step 4: Run the tests and confirm they fail**

```bash
cmake --build build -j$(nproc)
ctest --test-dir build -R CatServer --output-on-failure
```

Expected: compile error on `CatServer::volumeRequested` (the signal does not exist yet), and once that is declared, the new tests FAIL. Both are correct at this point. Do not proceed until you have seen real failures — a passing test here means the test is not exercising the new behaviour.

- [ ] **Step 5: Declare the new signals and state in `catserver.h`**

In `signals:`, after `pttRequested`:

```cpp
    // Emitted when external app sets AF gain via AG/AG$ commands
    // level is 0-100 (mapped from K4's 0-60 range)
    void volumeRequested(int level);
    void subVolumeRequested(int level);
```

In the private members:

```cpp
    int m_cwPending = 0;    // Approximate chars pending in K4 CW keyer buffer
    int m_mainVolume = 27;  // AG value 0-60 (default ~45%)
    int m_subVolume = 27;   // AG$ value 0-60 (default ~45%)
    int m_lastRfGain = 20;  // Last non-zero RF gain for RG/ toggle
    int m_lastRfGainB = 20; // Last non-zero RF gain B for RG$/ toggle
```

- [ ] **Step 6: Extend the prefix parser for `$`**

In `catserver.cpp`, add the includes:

```cpp
#include "settings/radiosettings.h"

#include <QTimer>
```

Then replace the prefix-extraction loop with:

```cpp
    // Extract command prefix (2-3 uppercase letters, plus optional '$' for Sub VFO commands)
    // K4 uses '$' suffix for Sub VFO: MD$, BW$, FA$, FB$, etc.
    QString prefix;
    QString args;
    for (int i = 0; i < command.length(); i++) {
        if (command[i].isLetter()) {
            prefix += command[i].toUpper();
        } else if (command[i] == '$' && prefix.length() >= 2) {
            // '$' is part of the command prefix (Sub VFO suffix)
            prefix += '$';
        } else {
            args = command.mid(i);
            break;
        }
    }
```

This changes how upstream's existing `args == "$"` VFO-B branch is reached. Read upstream's block at line ~195 and delete it if the new parser makes it unreachable — leaving both will produce dead code that silently shadows the new `MD$` handler.

- [ ] **Step 7: Add the pre-GET command handlers**

Immediately after the prefix loop, before upstream's GET block:

```cpp
    // TX/RX commands (no args) - must be handled before the GET block
    // Audio disabled or CW/CW-R: forward directly to K4
    // Voice/Data with audio enabled: control audio input gate (audio stream triggers K4 TX)
    if (prefix == "TX" && args.isEmpty()) {
        int mode = m_radioState->mode();
        if (!RadioSettings::instance()->audioEnabled() || mode == RadioState::CW || mode == RadioState::CW_R) {
            emit catCommandReceived(cmd);
        } else {
            emit pttRequested(true);
        }
        return QByteArray();
    }
    if (prefix == "RX" && args.isEmpty()) {
        int mode = m_radioState->mode();
        if (!RadioSettings::instance()->audioEnabled() || mode == RadioState::CW || mode == RadioState::CW_R) {
            emit catCommandReceived(cmd);
        } else {
            emit pttRequested(false);
        }
        m_cwPending = 0;
        return QByteArray();
    }

    // RU/RD/RC (RIT Up/Down/Clear) - forward to K4 then query new state
    // K4 doesn't echo RIT changes, so re-query offset and on/off state
    if (prefix == "RU" || prefix == "RD" || prefix == "RC") {
        emit catCommandReceived(cmd);
        emit catCommandReceived("RT;");
        emit catCommandReceived("RO;");
        return QByteArray();
    }

    // DN/DNB/UP/UPB (VFO step up/down) - no args, forward directly to K4
    if ((prefix == "DN" || prefix == "UP" || prefix == "DNB" || prefix == "UPB") && args.isEmpty()) {
        emit catCommandReceived(cmd);
        return QByteArray();
    }

    // RG+/RG-/RG/ (RF gain increment/decrement/toggle) - K4 firmware 2.x+
    // These use special suffixes that look like args to the prefix parser
    if (prefix == "RG" && (args == "+" || args == "-" || args == "/")) {
        emit catCommandReceived(cmd);
        int current = m_radioState->rfGain();
        if (args == "+") {
            m_radioState->setRfGain(qMax(0, current - 1));
        } else if (args == "-") {
            m_radioState->setRfGain(qMin(60, current + 1));
        } else {
            // Toggle: non-zero -> store and go to 0; zero -> restore previous
            if (current > 0) {
                m_lastRfGain = current;
                m_radioState->setRfGain(0);
            } else {
                m_radioState->setRfGain(m_lastRfGain > 0 ? m_lastRfGain : 20);
            }
        }
        emit catCommandReceived("RG;");
        return QByteArray();
    }
    if (prefix == "RG$" && (args == "+" || args == "-" || args == "/")) {
        emit catCommandReceived(cmd);
        int current = m_radioState->rfGainB();
        if (args == "+") {
            m_radioState->setRfGainB(qMax(0, current - 1));
        } else if (args == "-") {
            m_radioState->setRfGainB(qMin(60, current + 1));
        } else {
            if (current > 0) {
                m_lastRfGainB = current;
                m_radioState->setRfGainB(0);
            } else {
                m_radioState->setRfGainB(m_lastRfGainB > 0 ? m_lastRfGainB : 20);
            }
        }
        emit catCommandReceived("RG$;");
        return QByteArray();
    }
```

- [ ] **Step 8: Extend the GET block**

Replace upstream's `PC` responder with:

```cpp
        // RF power — include mode suffix (H=QRO, L=QRP) to match K4 format
        if (prefix == "PC") {
            int power = static_cast<int>(m_radioState->rfPower());
            QString mode = m_radioState->isQrpMode() ? "L" : "H";
            return QString("PC%1%2;").arg(power, 3, 10, QChar('0')).arg(mode).toUtf8();
        }
```

Replace upstream's `AG`, `TB`, and `SB` responders with:

```cpp
        // AG/AG$ - AF gain (local volume when audio enabled, forwarded to K4 when disabled)
        if (prefix == "AG" || prefix == "AG$") {
            if (RadioSettings::instance()->audioEnabled()) {
                int vol = (prefix == "AG") ? m_mainVolume : m_subVolume;
                return QString("%1%2;").arg(prefix).arg(vol, 3, 10, QChar('0')).toUtf8();
            }
            emit catCommandReceived(cmd);
            return QByteArray();
        }
        // KY - Keyer buffer status: KY0; = space available, KY1; = full
        if (prefix == "KY") {
            // K4 buffer holds ~60 chars; report full at 50+ pending
            return QString("KY%1;").arg(m_cwPending >= 50 ? 1 : 0).toUtf8();
        }
        // TB - Text buffer status: TBtaabb; where t=pending(0-9), aa=rx chars, bb=rx text
        if (prefix == "TB") {
            int pending = qBound(0, m_cwPending, 9);
            return QString("TB%100;").arg(pending).toUtf8();
        }
        // SB - Sub RX status: 3=diversity, 1=sub RX on, 0=off
        if (prefix == "SB") {
            int subStatus = 0;
            if (m_radioState->diversityEnabled())
                subStatus = 3;
            else if (m_radioState->subReceiverEnabled())
                subStatus = 1;
            return QString("SB%1;").arg(subStatus).toUtf8();
        }
```

Add these new responders at the end of the GET block:

```cpp
        // RG/RG$ - RF Gain
        if (prefix == "RG") {
            return QString("RG-%1;").arg(m_radioState->rfGain(), 2, 10, QChar('0')).toUtf8();
        }
        if (prefix == "RG$") {
            return QString("RG$-%1;").arg(m_radioState->rfGainB(), 2, 10, QChar('0')).toUtf8();
        }
        // MD$ - Sub VFO mode
        if (prefix == "MD$") {
            QString resp = buildModeResponse(m_radioState->modeB());
            resp.insert(2, '$'); // "MD3;" -> "MD$3;"
            return resp.toUtf8();
        }
        // BW$ - Sub filter bandwidth
        if (prefix == "BW$") {
            return QString("BW$%1;").arg(m_radioState->filterBandwidthB(), 4, 10, QChar('0')).toUtf8();
        }
        // PB - Playback status
        if (prefix == "PB") {
            return QByteArray("PB0;");
        }
        // Unrecognized GET command - don't forward, just return empty
        return QByteArray();
```

The final `return QByteArray();` must be the last statement inside the GET block. Anything after it in that block becomes dead code.

- [ ] **Step 9: Add the SET handlers**

Before upstream's catch-all forward:

```cpp
    // KY - Keyer CW text: track pending chars and forward to K4
    if (prefix == "KY") {
        if (args == "0") {
            m_cwPending = 0; // KY0 = abort CW
        } else {
            QString text = args.trimmed();
            m_cwPending = text.length();
            // K4 sends at keyer WPM; decay the pending count on that schedule
            int wpm = m_radioState->keyerSpeed();
            int charsPerSec = qMax(1, wpm / 6);
            int clearMs = qMax(500, (m_cwPending * 1000) / charsPerSec);
            QTimer::singleShot(clearMs, this, [this]() { m_cwPending = 0; });
        }
        emit catCommandReceived(cmd);
        return QByteArray();
    }

    // AG/AG$ SET - local volume when audio enabled, forward to K4 when disabled
    // K4 AG range is 000-060 (not 255)
    if (prefix == "AG" || prefix == "AG$") {
        if (RadioSettings::instance()->audioEnabled()) {
            int gain = qBound(0, args.toInt(), 60);
            int percent = (gain * 100 + 30) / 60; // Map 0-60 -> 0-100
            if (prefix == "AG") {
                m_mainVolume = gain;
                emit volumeRequested(percent);
            } else {
                m_subVolume = gain;
                emit subVolumeRequested(percent);
            }
            return QByteArray();
        }
        // Audio disabled — fall through to forward to K4
    }
```

And immediately after upstream's `emit catCommandReceived(cmd);` catch-all:

```cpp
    // Optimistically update RadioState so the UI and the next poll reflect the new
    // value immediately (K4 echo takes 50-100ms, but N1MM polls right after a SET).
    // Safe for all commands — RadioState only updates fields it has handlers for.
    m_radioState->parseCATCommand(cmd);
```

- [ ] **Step 10: Run the tests until green**

```bash
cmake --build build -j$(nproc)
ctest --test-dir build -R CatServer --output-on-failure
```

Expected: every test passes, including all pre-existing upstream CatServer tests. If an upstream test now fails, the `$`-parser change (Step 6) is the most likely cause — fix the implementation, never the upstream test.

- [ ] **Step 11: Run the whole suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: total pass count is Task 1's baseline plus the 15 new tests.

- [ ] **Step 12: Connect the volume signals in MainWindow**

The new signals are inert until consumed. Where `m_catServer`'s other signals are connected in `mainwindow.cpp`:

```cpp
    connect(m_catServer, &CatServer::volumeRequested, this, [this](int level) {
        if (m_audioEngine)
            m_audioEngine->setVolume(level);
    });
```

Check `AudioEngine`'s current volume setter name and units (0–100 vs 0.0–1.0) before wiring, and scale accordingly. If upstream has a separate sub-RX volume path, connect `subVolumeRequested` to it; otherwise leave it unconnected and note that in the commit body.

- [ ] **Step 13: Build, test, format, and commit**

```bash
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format -i
find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror
git add src/network/catserver.cpp src/network/catserver.h tests/test_catserver.cpp tests/CMakeLists.txt src/mainwindow.cpp
git commit -m "feat(catserver): add RF gain, sub-VFO, keyer, and AG volume commands"
```

---

### Task 10: Final verification and documentation

**Files:**
- Modify: `CLAUDE.md`
- Create: `CHANGELOG-UJ.md` entry

- [ ] **Step 1: Clean-build from scratch**

An incremental build can hide a missing `CMakeLists.txt` entry, because the stale object file is still linked.

```bash
rm -rf build
cmake -B build -DCMAKE_PREFIX_PATH="$HOME/6.8.1/gcc_64" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j$(nproc)
```

Expected: succeeds with no errors and no warnings about missing sources.

- [ ] **Step 2: Run the full suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all pass; count equals Task 1 baseline + 15.

- [ ] **Step 3: Verify formatting across the whole tree**

```bash
find src tests -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror
```

Expected: no output.

- [ ] **Step 4: Confirm no dropped subsystem leaked in**

```bash
git grep -l -i "n1mm\|spotoverlay" src/ tests/ CMakeLists.txt
```

Expected: no output. Any hit means an out-of-scope file was pulled in and must be removed.

- [ ] **Step 5: Confirm nothing references the dead classes**

```bash
git grep -c -i -E "rfkitwindow|miniviewwindow" src/mainwindow.cpp
```

Expected: a non-zero count. A zero here means the port reproduced the exact bug that motivated it.

- [ ] **Step 6: Correct the stale test-suite claim in `CLAUDE.md`**

Replace the "Linting and Formatting" opening sentence:

```markdown
There is **no test suite**. The only CI check is clang-format linting (`.github/workflows/lint.yml`).
```

with:

```markdown
Tests live in `tests/` and use the Qt Test framework. Configure with `-DBUILD_TESTING=ON` and run via `ctest --test-dir build --output-on-failure`. CI also enforces clang-format linting (`.github/workflows/lint.yml`).
```

- [ ] **Step 7: Record the port in `CHANGELOG-UJ.md`**

Create the file with:

```markdown
# QK4-UJ Fork Changelog

## Unreleased — rebuilt on upstream v0.7.0-beta.5+

Fork rebuilt from a clean `origin/main` branch point rather than merged, after
the v0.6.0-beta.1 merge silently dropped all MainWindow and OptionsDialog
wiring for the fork's features.

### Added
- RF-Kit amplifier interface: HTTP REST client, readout panel, floating window,
  settings tab, and drive-power lockout.
- Mini View window: compact dual-VFO display with optional mini panadapter and
  DX spot list, reachable from the MINI button in the bottom menu bar.
- CatServer extensions: sub-VFO (`MD$`, `BW$`, `RG$`) queries, RF gain
  increment/decrement/toggle, RIT re-query, VFO step commands, keyer buffer
  tracking, and AG-driven local volume control.

### Changed
- Mini View spots now consume upstream's `DxSpot` from `DxClusterClient`
  instead of the fork's N1MM UDP listener.

### Removed
- N1MM UDP spot listener and the N1MM panadapter spot overlay — superseded by
  upstream's DX cluster spot pipeline.
- Fork-specific audio latency and gain modifications — superseded by upstream's
  audio device and buffer-recovery rework.
- Fork CI workflow changes — superseded by upstream's reusable workflows.
```

- [ ] **Step 8: Commit the documentation**

```bash
git add CLAUDE.md CHANGELOG-UJ.md
git commit -m "docs: record UJ fork port onto upstream v0.7 and fix test-suite note"
```

- [ ] **Step 9: Manual acceptance pass**

```bash
LD_LIBRARY_PATH="$HOME/6.8.1/gcc_64/lib:$LD_LIBRARY_PATH" ./build/QK4
```

Confirm every item, and report any failure rather than working around it:
1. App launches; no RFKit UI visible while RFKit is disabled.
2. Tools > Settings shows the RFKit tab; host/port/enable persist across a restart.
3. MINI button appears left of PTT; clicking it swaps to the mini view.
4. Mini view shows correct VFO A/B frequencies and modes, and tracks live changes.
5. Restoring from the mini view brings the main window back.
6. Mini view window position persists across a restart.
7. App exits from both windows with no crash.
8. Connect a CAT client and confirm `MD$;`, `RG;`, and `PC;` return the new formats.

- [ ] **Step 10: Push the branch**

Only after every check above passes, and only with the user's explicit go-ahead — this publishes to the fork remote:

```bash
git push -u ujay ujay-mods-v2
```

Leave `ujay-mods` and `backup/ujay-mods-2026-08-18` untouched. Do not delete the old branch until the user has run the new one against real hardware.

---

## Deferred Decisions

These are recorded rather than resolved. Raise them with the user; do not silently pick one.

1. **Sub-RX volume** — `subVolumeRequested` may have no upstream consumer (Task 9 Step 12). If so, it stays connected to nothing.
2. **Retiring `ujay-mods`** — the old branch and its remote counterpart remain until the user confirms `ujay-mods-v2` works against real hardware.
3. **Fork CI** — `uj.*` release tags, the Linux x86_64 build, and the dropped macOS build are out of scope here. Upstream now has reusable workflows plus Flatpak packaging, so re-adding fork CI is a separate piece of work against a completely different workflow structure.
4. **N1MM spots** — dropped in favour of upstream's DX cluster. If the user specifically needs N1MM's UDP feed (multiplier/worked colouring that DX cluster lacks), that is a follow-up task, best written as an adapter emitting `DxSpot` rather than a second parallel spot system.
