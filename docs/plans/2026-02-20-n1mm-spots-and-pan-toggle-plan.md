# N1MM Spots & Panadapter Toggle Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add N1MM UDP spot overlay on the panadapter with click-to-tune, and a DISP popup toggle to disable spectrum streaming for bandwidth savings.

**Architecture:** N1mmListener (QUdpSocket) receives XML spot broadcasts and feeds SpotOverlayWidget (QPainter child of PanadapterRhiWidget) for rendering. A new DISP popup button sends #FPS CAT commands to enable/disable spectrum streaming from the K4. Both features persist settings via RadioSettings.

**Tech Stack:** Qt 6.7+ (QUdpSocket, QXmlStreamReader, QPainter), C++17

**Design doc:** `docs/plans/2026-02-20-n1mm-spots-and-pan-toggle-design.md`

**Note:** This project has no test suite. The only CI check is clang-format linting. Skip TDD steps. Format all new/modified files before each commit:
```bash
find src -name '*.cpp' -o -name '*.h' | xargs clang-format -i
```

---

## Task 1: Add N1MM and Panadapter Settings to RadioEntry

**Files:**
- Modify: `src/settings/radiosettings.h:34-48` (RadioEntry struct)
- Modify: `src/settings/radiosettings.cpp:369-384` (load), `src/settings/radiosettings.cpp:459-471` (save)

**Step 1: Add fields to RadioEntry struct**

In `src/settings/radiosettings.h`, add after `displayFps` (line 43):

```cpp
bool n1mmEnabled = false;       // Whether to listen for N1MM spot broadcasts
quint16 n1mmPort = 12060;       // N1MM UDP listen port
int spotExpiryMinutes = 10;     // Auto-expire spots after N minutes
bool panadapterEnabled = true;  // Persist panadapter on/off state
```

**Step 2: Add load() deserialization**

In `src/settings/radiosettings.cpp`, after the `displayFps` line (line 383), add:

```cpp
entry.n1mmEnabled = m_settings.value("n1mmEnabled", false).toBool();
entry.n1mmPort = m_settings.value("n1mmPort", 12060).toUInt();
entry.spotExpiryMinutes = m_settings.value("spotExpiryMinutes", 10).toInt();
entry.panadapterEnabled = m_settings.value("panadapterEnabled", true).toBool();
```

**Step 3: Add save() serialization**

In `src/settings/radiosettings.cpp`, after the `displayFps` line (line 471), add:

```cpp
m_settings.setValue("n1mmEnabled", m_radios[i].n1mmEnabled);
m_settings.setValue("n1mmPort", m_radios[i].n1mmPort);
m_settings.setValue("spotExpiryMinutes", m_radios[i].spotExpiryMinutes);
m_settings.setValue("panadapterEnabled", m_radios[i].panadapterEnabled);
```

**Step 4: Build and verify**

```bash
cmake --build build -j$(nproc)
```

Expected: Clean compile, no errors.

**Step 5: Format and commit**

```bash
find src -name '*.cpp' -o -name '*.h' | xargs clang-format -i
git add src/settings/radiosettings.h src/settings/radiosettings.cpp
git commit -m "feat(settings): add N1MM and panadapter toggle settings to RadioEntry"
```

---

## Task 2: Create N1mmListener Class

**Files:**
- Create: `src/network/n1mmlistener.h`
- Create: `src/network/n1mmlistener.cpp`
- Modify: `CMakeLists.txt:77,138` (add to SOURCES/HEADERS)

**Step 1: Create the header**

Create `src/network/n1mmlistener.h`:

```cpp
#ifndef N1MMLISTENER_H
#define N1MMLISTENER_H

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>

struct SpotData {
    QString callsign;
    qint64 frequencyHz;
    QString mode;
    QString status;
    QDateTime timestamp;
    QElapsedTimer age;
};

class N1mmListener : public QObject {
    Q_OBJECT

public:
    explicit N1mmListener(QObject *parent = nullptr);
    ~N1mmListener();

    bool start(quint16 port = 12060);
    void stop();
    bool isListening() const;

    void setExpiryMinutes(int minutes);
    int expiryMinutes() const { return m_expiryMinutes; }

    QList<SpotData> activeSpots() const { return m_spots.values(); }
    void clearSpots();

signals:
    void spotReceived(const SpotData &spot);
    void spotRemoved(const QString &callsign);
    void listenError(const QString &error);

private:
    void processDatagram(const QByteArray &data);
    void expireOldSpots();

    QUdpSocket *m_socket = nullptr;
    QTimer *m_expiryTimer;
    QHash<QString, SpotData> m_spots;
    int m_expiryMinutes = 10;
};

#endif // N1MMLISTENER_H
```

**Step 2: Create the implementation**

Create `src/network/n1mmlistener.cpp`:

```cpp
#include "n1mmlistener.h"

#include <QXmlStreamReader>

N1mmListener::N1mmListener(QObject *parent) : QObject(parent) {
    m_expiryTimer = new QTimer(this);
    m_expiryTimer->setInterval(30000); // Check every 30 seconds
    connect(m_expiryTimer, &QTimer::timeout, this, &N1mmListener::expireOldSpots);
}

N1mmListener::~N1mmListener() {
    stop();
}

bool N1mmListener::start(quint16 port) {
    stop();

    m_socket = new QUdpSocket(this);
    if (!m_socket->bind(QHostAddress::Any, port, QAbstractSocket::ShareAddress | QAbstractSocket::ReuseAddressHint)) {
        emit listenError(QString("Failed to bind UDP port %1: %2").arg(port).arg(m_socket->errorString()));
        delete m_socket;
        m_socket = nullptr;
        return false;
    }

    connect(m_socket, &QUdpSocket::readyRead, this, [this]() {
        while (m_socket->hasPendingDatagrams()) {
            QByteArray datagram;
            datagram.resize(m_socket->pendingDatagramSize());
            m_socket->readDatagram(datagram.data(), datagram.size());
            processDatagram(datagram);
        }
    });

    m_expiryTimer->start();
    return true;
}

void N1mmListener::stop() {
    m_expiryTimer->stop();
    if (m_socket) {
        m_socket->close();
        delete m_socket;
        m_socket = nullptr;
    }
}

bool N1mmListener::isListening() const {
    return m_socket && m_socket->state() == QAbstractSocket::BoundState;
}

void N1mmListener::setExpiryMinutes(int minutes) {
    m_expiryMinutes = qMax(1, minutes);
}

void N1mmListener::clearSpots() {
    QStringList keys = m_spots.keys();
    m_spots.clear();
    for (const QString &key : keys) {
        emit spotRemoved(key);
    }
}

void N1mmListener::processDatagram(const QByteArray &data) {
    QXmlStreamReader xml(data);

    // Find the root element
    while (!xml.atEnd() && !xml.hasError()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == u"spot") {
            break;
        }
        if (xml.isStartElement()) {
            return; // Not a spot message, ignore
        }
    }

    if (xml.hasError() || xml.atEnd())
        return;

    // Parse spot fields
    QString callsign;
    double frequencyKHz = 0.0;
    QString mode;
    QString status;
    QString action;
    QString timestampStr;

    while (!xml.atEnd() && !xml.hasError()) {
        xml.readNext();
        if (xml.isStartElement()) {
            QString name = xml.name().toString();
            QString text = xml.readElementText();
            if (name == "dxcall") {
                callsign = text.trimmed();
            } else if (name == "frequency") {
                frequencyKHz = text.toDouble();
            } else if (name == "mode") {
                mode = text.trimmed();
            } else if (name == "status") {
                status = text.trimmed();
            } else if (name == "action") {
                action = text.trimmed().toLower();
            } else if (name == "timestamp") {
                timestampStr = text.trimmed();
            }
        }
    }

    if (callsign.isEmpty())
        return;

    if (action == "delete") {
        if (m_spots.remove(callsign)) {
            emit spotRemoved(callsign);
        }
        return;
    }

    // action == "add" (or unspecified, treat as add)
    SpotData spot;
    spot.callsign = callsign;
    spot.frequencyHz = static_cast<qint64>(frequencyKHz * 1000.0);
    spot.mode = mode;
    spot.status = status;
    spot.timestamp = QDateTime::fromString(timestampStr, "yyyy-MM-dd HH:mm:ss");
    spot.age.start();

    m_spots.insert(callsign, spot);
    emit spotReceived(spot);
}

void N1mmListener::expireOldSpots() {
    qint64 expiryMs = m_expiryMinutes * 60 * 1000;
    QStringList expired;

    for (auto it = m_spots.begin(); it != m_spots.end();) {
        if (it->age.elapsed() > expiryMs) {
            expired.append(it.key());
            it = m_spots.erase(it);
        } else {
            ++it;
        }
    }

    for (const QString &callsign : expired) {
        emit spotRemoved(callsign);
    }
}
```

**Step 3: Add to CMakeLists.txt**

In `CMakeLists.txt`, add to SOURCES list after `src/network/catserver.cpp` (line 77):

```
    src/network/n1mmlistener.cpp
```

Add to HEADERS list after `src/network/catserver.h` (line 138):

```
    src/network/n1mmlistener.h
```

**Step 4: Build and verify**

```bash
cmake -B build -DCMAKE_PREFIX_PATH="$HOME/6.8.1/gcc_64" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Expected: Clean compile.

**Step 5: Format and commit**

```bash
find src -name '*.cpp' -o -name '*.h' | xargs clang-format -i
git add src/network/n1mmlistener.h src/network/n1mmlistener.cpp CMakeLists.txt
git commit -m "feat(network): add N1MM UDP spot listener"
```

---

## Task 3: Add Spot Color Constants to K4Styles

**Files:**
- Modify: `src/ui/k4styles.h:196` (after OverlayDividerLight)

**Step 1: Add spot color constants**

In `src/ui/k4styles.h`, add after `OverlayDividerLight` (line 196), within the `Colors` namespace:

```cpp
// N1MM Spot Overlay Colors
constexpr const char *SpotMult = "#FFD700";    // Gold - new multiplier
constexpr const char *SpotNewQso = "#FFFFFF";  // White - new QSO (not dupe, not mult)
constexpr const char *SpotDupe = "#555555";    // Dim gray - already worked
constexpr const char *SpotDefault = "#87CEEB"; // Light sky blue - default/unknown
```

**Step 2: Build and verify**

```bash
cmake --build build -j$(nproc)
```

**Step 3: Format and commit**

```bash
find src -name '*.cpp' -o -name '*.h' | xargs clang-format -i
git add src/ui/k4styles.h
git commit -m "feat(ui): add N1MM spot color constants to K4Styles"
```

---

## Task 4: Create SpotOverlayWidget

**Files:**
- Create: `src/dsp/spotoverlaywidget.h`
- Create: `src/dsp/spotoverlaywidget.cpp`
- Modify: `CMakeLists.txt` (add to SOURCES/HEADERS)

**Step 1: Create the header**

Create `src/dsp/spotoverlaywidget.h`:

```cpp
#ifndef SPOTOVERLAYWIDGET_H
#define SPOTOVERLAYWIDGET_H

#include "../network/n1mmlistener.h"
#include <QHash>
#include <QWidget>

class SpotOverlayWidget : public QWidget {
    Q_OBJECT

public:
    explicit SpotOverlayWidget(QWidget *parent = nullptr);

    void addSpot(const SpotData &spot);
    void removeSpot(const QString &callsign);
    void clearSpots();

    // Called by parent panadapter to keep frequency mapping in sync
    void setFrequencyRange(qint64 centerFreq, int spanHz, int cwPitch, const QString &mode);

signals:
    void spotClicked(qint64 frequencyHz);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    struct SpotLabel {
        SpotData spot;
        QRectF boundingRect; // Computed during paint for hit testing
    };

    float freqToX(qint64 freq, int width) const;
    QColor colorForStatus(const QString &status) const;

    QHash<QString, SpotData> m_spots;
    QVector<SpotLabel> m_paintedLabels; // Updated each paint for click detection

    qint64 m_centerFreq = 0;
    int m_spanHz = 10000;
    int m_cwPitch = 500;
    QString m_mode = "USB";
};

#endif // SPOTOVERLAYWIDGET_H
```

**Step 2: Create the implementation**

Create `src/dsp/spotoverlaywidget.cpp`:

```cpp
#include "spotoverlaywidget.h"
#include "../ui/k4styles.h"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

SpotOverlayWidget::SpotOverlayWidget(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);
}

void SpotOverlayWidget::addSpot(const SpotData &spot) {
    m_spots.insert(spot.callsign, spot);
    update();
}

void SpotOverlayWidget::removeSpot(const QString &callsign) {
    if (m_spots.remove(callsign)) {
        update();
    }
}

void SpotOverlayWidget::clearSpots() {
    m_spots.clear();
    update();
}

void SpotOverlayWidget::setFrequencyRange(qint64 centerFreq, int spanHz, int cwPitch, const QString &mode) {
    m_centerFreq = centerFreq;
    m_spanHz = spanHz;
    m_cwPitch = cwPitch;
    m_mode = mode;
    update();
}

float SpotOverlayWidget::freqToX(qint64 freq, int w) const {
    qint64 effectiveCenter = m_centerFreq;
    if (m_mode == "CW") {
        effectiveCenter = m_centerFreq + m_cwPitch;
    } else if (m_mode == "CW-R") {
        effectiveCenter = m_centerFreq - m_cwPitch;
    }
    qint64 startFreq = effectiveCenter - m_spanHz / 2;
    float normalized = static_cast<float>(freq - startFreq) / static_cast<float>(m_spanHz);
    return normalized * w;
}

QColor SpotOverlayWidget::colorForStatus(const QString &status) const {
    if (status.contains("mult")) {
        return QColor(K4Styles::Colors::SpotMult);
    } else if (status.contains("dupe")) {
        return QColor(K4Styles::Colors::SpotDupe);
    } else if (status.contains("new qso") || status.isEmpty()) {
        return QColor(K4Styles::Colors::SpotNewQso);
    }
    return QColor(K4Styles::Colors::SpotDefault);
}

void SpotOverlayWidget::paintEvent(QPaintEvent * /*event*/) {
    if (m_spots.isEmpty() || m_spanHz <= 0)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QFont font;
    font.setPixelSize(10);
    font.setBold(true);
    painter.setFont(font);
    QFontMetrics fm(font);

    int w = width();
    int h = height();
    int tickHeight = 8;

    // Collect visible spots
    qint64 effectiveCenter = m_centerFreq;
    if (m_mode == "CW") {
        effectiveCenter = m_centerFreq + m_cwPitch;
    } else if (m_mode == "CW-R") {
        effectiveCenter = m_centerFreq - m_cwPitch;
    }
    qint64 startFreq = effectiveCenter - m_spanHz / 2;
    qint64 endFreq = effectiveCenter + m_spanHz / 2;

    struct VisibleSpot {
        SpotData spot;
        float x;
    };
    QVector<VisibleSpot> visible;

    for (const SpotData &spot : m_spots) {
        if (spot.frequencyHz >= startFreq && spot.frequencyHz <= endFreq) {
            float x = freqToX(spot.frequencyHz, w);
            visible.append({spot, x});
        }
    }

    // Sort by frequency for consistent overlap handling
    std::sort(visible.begin(), visible.end(), [](const VisibleSpot &a, const VisibleSpot &b) {
        return a.x < b.x;
    });

    // Cap at 30 visible spots (keep newest by age timer)
    if (visible.size() > 30) {
        std::sort(visible.begin(), visible.end(),
                  [](const VisibleSpot &a, const VisibleSpot &b) { return a.spot.age.elapsed() < b.spot.age.elapsed(); });
        visible.resize(30);
        std::sort(visible.begin(), visible.end(),
                  [](const VisibleSpot &a, const VisibleSpot &b) { return a.x < b.x; });
    }

    m_paintedLabels.clear();
    m_paintedLabels.reserve(visible.size());

    // Stagger rows to avoid overlap (up to 3 rows)
    static constexpr int MAX_ROWS = 3;
    int rowHeight = fm.height() + 4;
    QVector<float> rowRightEdge(MAX_ROWS, -100.0f);

    for (const VisibleSpot &vs : visible) {
        QColor color = colorForStatus(vs.spot.status);

        // Draw tick mark
        QPen tickPen(color, 1.0);
        painter.setPen(tickPen);
        int tickBottom = h - 2;
        int tickTop = tickBottom - tickHeight;
        painter.drawLine(QPointF(vs.x, tickTop), QPointF(vs.x, tickBottom));

        // Find best row (lowest row where label doesn't overlap)
        int textWidth = fm.horizontalAdvance(vs.spot.callsign);
        float labelLeft = vs.x - textWidth / 2.0f;
        float labelRight = vs.x + textWidth / 2.0f + 4.0f;
        int row = 0;
        for (int r = 0; r < MAX_ROWS; ++r) {
            if (labelLeft > rowRightEdge[r]) {
                row = r;
                break;
            }
            if (r == MAX_ROWS - 1) {
                row = r; // Force into last row if all overlap
            }
        }
        rowRightEdge[row] = labelRight;

        // Draw label
        float labelY = tickTop - 2 - (row * rowHeight);
        QRectF textRect(labelLeft, labelY - fm.height(), textWidth + 4, fm.height() + 2);

        // Background for readability
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(0, 0, 0, 160));
        painter.drawRoundedRect(textRect, 2, 2);

        // Text
        painter.setPen(color);
        painter.drawText(textRect, Qt::AlignCenter, vs.spot.callsign);

        // Store for click detection
        SpotLabel label;
        label.spot = vs.spot;
        label.boundingRect = textRect;
        m_paintedLabels.append(label);
    }
}

void SpotOverlayWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        QPointF pos = event->position();
        for (const SpotLabel &label : m_paintedLabels) {
            if (label.boundingRect.contains(pos)) {
                emit spotClicked(label.spot.frequencyHz);
                event->accept();
                return;
            }
        }
    }
    // Pass through to parent (panadapter) if no spot was hit
    event->ignore();
}
```

**Step 3: Add to CMakeLists.txt**

In SOURCES list, add after `src/dsp/minipan_rhi.cpp` (line 83):

```
    src/dsp/spotoverlaywidget.cpp
```

In HEADERS list, add after `src/dsp/minipan_rhi.h` (line 144):

```
    src/dsp/spotoverlaywidget.h
```

**Step 4: Build and verify**

```bash
cmake -B build -DCMAKE_PREFIX_PATH="$HOME/6.8.1/gcc_64" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

**Step 5: Format and commit**

```bash
find src -name '*.cpp' -o -name '*.h' | xargs clang-format -i
git add src/dsp/spotoverlaywidget.h src/dsp/spotoverlaywidget.cpp CMakeLists.txt
git commit -m "feat(dsp): add SpotOverlayWidget for N1MM spot rendering"
```

---

## Task 5: Integrate SpotOverlayWidget into PanadapterRhiWidget

**Files:**
- Modify: `src/dsp/panadapter_rhi.h:258` (add member)
- Modify: `src/dsp/panadapter_rhi.h:60-68` (add public methods)
- Modify: `src/dsp/panadapter_rhi.cpp:275-294` (create + reposition overlay)

**Step 1: Add forward declaration and member**

In `src/dsp/panadapter_rhi.h`, add forward declaration near top (after existing includes):

```cpp
class SpotOverlayWidget;
```

Add public method after the color configuration methods (after line 68):

```cpp
// Spot overlay
SpotOverlayWidget *spotOverlay() const { return m_spotOverlay; }
```

Add member variable after `m_freqScaleOverlay` (line 258):

```cpp
// N1MM spot overlay (child widget for spot labels)
SpotOverlayWidget *m_spotOverlay = nullptr;
```

**Step 2: Create and position the overlay**

In `src/dsp/panadapter_rhi.cpp`, add include at top:

```cpp
#include "spotoverlaywidget.h"
```

After the FrequencyScaleOverlay creation (line 283), add:

```cpp
// Create spot overlay (child widget for N1MM spot labels)
m_spotOverlay = new SpotOverlayWidget(this);
m_spotOverlay->show();
```

**Step 3: Add repositioning in resizeEvent**

In `src/dsp/panadapter_rhi.cpp`, add a new method and call it from `resizeEvent()` (line 293):

After `updateFreqScaleOverlay();` add:

```cpp
updateSpotOverlay();
```

Add the method implementation (after `updateFreqScaleOverlay()` method):

```cpp
void PanadapterRhiWidget::updateSpotOverlay() {
    if (!m_spotOverlay)
        return;
    // Cover the spectrum area (top portion)
    int spectrumHeight = static_cast<int>(height() * m_spectrumRatio);
    m_spotOverlay->setGeometry(0, 0, width(), spectrumHeight);
    m_spotOverlay->setFrequencyRange(m_centerFreq, m_spanHz, m_cwPitch, m_mode);
}
```

Also add `updateSpotOverlay();` declaration to the private section of the header, after `updateFreqScaleOverlay()`.

**Step 4: Update spot overlay when spectrum data changes**

In `src/dsp/panadapter_rhi.cpp`, find the `updateSpectrum()` method and add at the end (before the closing brace):

```cpp
// Keep spot overlay in sync with frequency range
if (m_spotOverlay) {
    m_spotOverlay->setFrequencyRange(m_centerFreq, m_spanHz, m_cwPitch, m_mode);
}
```

**Step 5: Build and verify**

```bash
cmake --build build -j$(nproc)
```

**Step 6: Format and commit**

```bash
find src -name '*.cpp' -o -name '*.h' | xargs clang-format -i
git add src/dsp/panadapter_rhi.h src/dsp/panadapter_rhi.cpp
git commit -m "feat(dsp): integrate SpotOverlayWidget into PanadapterRhiWidget"
```

---

## Task 6: Add PAN ON/OFF Toggle to DISP Popup

**Files:**
- Modify: `src/ui/displaypopupwidget.h:19` (MenuItem enum)
- Modify: `src/ui/displaypopupwidget.h:59-98` (signals)
- Modify: `src/ui/displaypopupwidget.cpp:442-448` (contentSize)
- Modify: `src/ui/displaypopupwidget.cpp:766-769` (setupBottomRow menu items)
- Modify: `src/ui/displaypopupwidget.cpp:785+` (onMenuItemClicked handler)

**Step 1: Add PanOnOff to MenuItem enum**

In `src/ui/displaypopupwidget.h` line 19, change:

```cpp
enum MenuItem { PanWaterfall = 0, NbWtrClrs, RefLvlScale, SpanCenter, AveragePeak, FixedFreeze, CursAB };
```

to:

```cpp
enum MenuItem { PanWaterfall = 0, NbWtrClrs, RefLvlScale, SpanCenter, AveragePeak, FixedFreeze, CursAB, PanOnOff };
```

**Step 2: Add signal and state**

In the signals section (after line 98), add:

```cpp
// Panadapter on/off toggle
void panadapterToggled(bool enabled);
```

In the private member section, add:

```cpp
bool m_panadapterOn = true;
```

Add public slot:

```cpp
void setPanadapterEnabled(bool enabled);
```

**Step 3: Widen popup for 8th button**

In `src/ui/displaypopupwidget.cpp` line 445, change:

```cpp
int width = 7 * MenuButtonWidth + 6 * ButtonSpacing + 2 * cm;
```

to:

```cpp
int width = 8 * MenuButtonWidth + 7 * ButtonSpacing + 2 * cm;
```

**Step 4: Add 8th button to setupBottomRow**

In `src/ui/displaypopupwidget.cpp` lines 766-769, add to the items list:

```cpp
{"PAN ON", "PAN OFF", PanOnOff}
```

So the full list becomes:

```cpp
QList<MenuItemDef> items = {{"PAN = A", "WTRFALL", PanWaterfall}, {"NB", "WTR CLRS", NbWtrClrs},
                            {"REF LVL", "SCALE", RefLvlScale},    {"SPAN", "CENTER", SpanCenter},
                            {"AVERAGE", "PEAK OFF", AveragePeak}, {"FIXED2", "FREEZE", FixedFreeze},
                            {"CURS A+", "CURS B+", CursAB},       {"PAN ON", "PAN OFF", PanOnOff}};
```

**Step 5: Handle PanOnOff click in onMenuItemClicked**

In `src/ui/displaypopupwidget.cpp`, in the `onMenuItemClicked()` method, add a case for `PanOnOff`:

```cpp
case PanOnOff:
    m_panadapterOn = !m_panadapterOn;
    updateMenuButtonLabels();
    emit panadapterToggled(m_panadapterOn);
    break;
```

**Step 6: Update button label in updateMenuButtonLabels**

Find `updateMenuButtonLabels()` and add logic to update button 7 (index 7) primary text based on `m_panadapterOn`:

```cpp
// PAN ON/OFF button
if (m_menuButtons.size() > PanOnOff) {
    m_menuButtons[PanOnOff]->setPrimaryText(m_panadapterOn ? "PAN ON" : "PAN OFF");
}
```

**Step 7: Implement setPanadapterEnabled slot**

```cpp
void DisplayPopupWidget::setPanadapterEnabled(bool enabled) {
    if (m_panadapterOn != enabled) {
        m_panadapterOn = enabled;
        updateMenuButtonLabels();
    }
}
```

**Step 8: Build and verify**

```bash
cmake --build build -j$(nproc)
```

**Step 9: Format and commit**

```bash
find src -name '*.cpp' -o -name '*.h' | xargs clang-format -i
git add src/ui/displaypopupwidget.h src/ui/displaypopupwidget.cpp
git commit -m "feat(ui): add PAN ON/OFF toggle button to DISP popup"
```

---

## Task 7: Wire Everything Together in MainWindow

**Files:**
- Modify: `src/mainwindow.h:318-324` (add members)
- Modify: `src/mainwindow.cpp` (N1MM init, spot routing, pan toggle logic, spectrum gating)

**Step 1: Add includes and members to MainWindow header**

In `src/mainwindow.h`, add forward declaration:

```cpp
class N1mmListener;
```

Add member variables after `m_notificationWidget` (line 324):

```cpp
// N1MM spot listener
N1mmListener *m_n1mmListener = nullptr;
bool m_panadapterEnabled = true;
```

Add private slot declarations:

```cpp
void onPanadapterToggled(bool enabled);
```

**Step 2: Add include to mainwindow.cpp**

```cpp
#include "network/n1mmlistener.h"
#include "dsp/spotoverlaywidget.h"
```

**Step 3: Initialize N1mmListener on radio connect**

Find where `m_catServer` is initialized (around line 1764 in mainwindow.cpp). After that block, add N1MM listener initialization:

```cpp
// N1MM spot listener
if (!m_n1mmListener) {
    m_n1mmListener = new N1mmListener(this);
    connect(m_n1mmListener, &N1mmListener::spotReceived, this, [this](const SpotData &spot) {
        if (m_panadapterA && m_panadapterA->spotOverlay()) {
            m_panadapterA->spotOverlay()->addSpot(spot);
        }
        if (m_panadapterB && m_panadapterB->spotOverlay()) {
            m_panadapterB->spotOverlay()->addSpot(spot);
        }
    });
    connect(m_n1mmListener, &N1mmListener::spotRemoved, this, [this](const QString &callsign) {
        if (m_panadapterA && m_panadapterA->spotOverlay()) {
            m_panadapterA->spotOverlay()->removeSpot(callsign);
        }
        if (m_panadapterB && m_panadapterB->spotOverlay()) {
            m_panadapterB->spotOverlay()->removeSpot(callsign);
        }
    });
}

if (m_currentRadio.n1mmEnabled) {
    m_n1mmListener->setExpiryMinutes(m_currentRadio.spotExpiryMinutes);
    m_n1mmListener->start(m_currentRadio.n1mmPort);
}
```

**Step 4: Connect spot click-to-tune**

After the N1MM listener setup, connect the spot overlay click signals:

```cpp
// Connect spot click-to-tune (VFO A)
if (m_panadapterA && m_panadapterA->spotOverlay()) {
    connect(m_panadapterA->spotOverlay(), &SpotOverlayWidget::spotClicked, this, [this](qint64 freq) {
        QString cmd = QString("FA%1;").arg(freq, 11, 10, QChar('0'));
        m_tcpClient->sendCommand(cmd);
    });
}
if (m_panadapterB && m_panadapterB->spotOverlay()) {
    connect(m_panadapterB->spotOverlay(), &SpotOverlayWidget::spotClicked, this, [this](qint64 freq) {
        QString cmd = QString("FA%1;").arg(freq, 11, 10, QChar('0'));
        m_tcpClient->sendCommand(cmd);
    });
}
```

**Step 5: Connect DISP popup panadapter toggle**

Find where DisplayPopupWidget signals are connected (around lines 171-194 or 1362-1410). Add:

```cpp
connect(m_displayPopup, &DisplayPopupWidget::panadapterToggled, this, &MainWindow::onPanadapterToggled);
```

**Step 6: Implement onPanadapterToggled**

Add the handler method:

```cpp
void MainWindow::onPanadapterToggled(bool enabled) {
    m_panadapterEnabled = enabled;

    if (enabled) {
        // Restore spectrum streaming
        int fps = m_currentRadio.displayFps;
        QString fpsCmd = QString("#FPS%1;").arg(fps, 2, 10, QChar('0'));
        m_tcpClient->sendCommand(fpsCmd);

        // Show panadapter
        m_spectrumContainer->setVisible(true);
    } else {
        // Stop spectrum streaming — try FPS 0, fall back to minimum
        m_tcpClient->sendCommand("#FPS00;");

        // Disable MiniPAN streams
        m_tcpClient->sendCommand("#MP0;");
        m_tcpClient->sendCommand("#MP$0;");

        // Hide panadapter
        m_spectrumContainer->setVisible(false);
    }

    // Persist setting
    m_currentRadio.panadapterEnabled = enabled;
    RadioSettings::instance()->updateRadio(m_currentRadio);
}
```

**Step 7: Gate spectrum data on panadapter state**

In `onSpectrumData()` (line 4406), add early return:

```cpp
void MainWindow::onSpectrumData(int receiver, const QByteArray &data,
                                qint64 centerFreq, qint32 sampleRate, float noiseFloor) {
    if (!m_panadapterEnabled)
        return;
    // ... existing code
}
```

Similarly in `onMiniSpectrumData()` (line 4417):

```cpp
void MainWindow::onMiniSpectrumData(int receiver, const QByteArray &data) {
    if (!m_panadapterEnabled)
        return;
    // ... existing code
}
```

**Step 8: Restore panadapter state on connect**

Where the radio connection is established and initial settings are applied, add:

```cpp
m_panadapterEnabled = m_currentRadio.panadapterEnabled;
if (!m_panadapterEnabled) {
    m_spectrumContainer->setVisible(false);
    m_displayPopup->setPanadapterEnabled(false);
    // Don't send #FPS command — radio will start sending spectrum by default,
    // so we need to send #FPS00 after handshake completes
}
```

**Step 9: Stop N1MM listener on disconnect**

In the disconnect handler, add:

```cpp
if (m_n1mmListener) {
    m_n1mmListener->stop();
    m_n1mmListener->clearSpots();
}
```

**Step 10: Build and verify**

```bash
cmake --build build -j$(nproc)
```

**Step 11: Format and commit**

```bash
find src -name '*.cpp' -o -name '*.h' | xargs clang-format -i
git add src/mainwindow.h src/mainwindow.cpp
git commit -m "feat: wire N1MM spot overlay and panadapter toggle into MainWindow"
```

---

## Task 8: Add N1MM Settings to Radio Connection Dialog

**Files:**
- Modify: `src/ui/radiomanagerdialog.h` (add N1MM UI fields)
- Modify: `src/ui/radiomanagerdialog.cpp` (add N1MM section to dialog)

**Step 1: Explore current dialog layout**

Read `src/ui/radiomanagerdialog.h` and `src/ui/radiomanagerdialog.cpp` to understand how existing fields (host, port, TLS, encodeMode, streamingLatency, displayFps) are laid out. The N1MM settings section should follow the same pattern.

**Step 2: Add member variables to header**

Add to the private section:

```cpp
QCheckBox *m_n1mmEnabledCheck;
QSpinBox *m_n1mmPortSpin;
```

**Step 3: Add N1MM section to dialog UI**

In the dialog's form/layout setup, add after the existing settings fields:

```cpp
// N1MM Spots section
auto *n1mmGroup = new QGroupBox("N1MM Spots");
auto *n1mmLayout = new QHBoxLayout(n1mmGroup);
m_n1mmEnabledCheck = new QCheckBox("Enable N1MM UDP Spots");
m_n1mmPortSpin = new QSpinBox();
m_n1mmPortSpin->setRange(1024, 65535);
m_n1mmPortSpin->setValue(12060);
n1mmLayout->addWidget(m_n1mmEnabledCheck);
n1mmLayout->addWidget(new QLabel("Port:"));
n1mmLayout->addWidget(m_n1mmPortSpin);
```

**Step 4: Load/save N1MM fields**

When populating the dialog from a `RadioEntry`:

```cpp
m_n1mmEnabledCheck->setChecked(entry.n1mmEnabled);
m_n1mmPortSpin->setValue(entry.n1mmPort);
```

When saving dialog back to `RadioEntry`:

```cpp
entry.n1mmEnabled = m_n1mmEnabledCheck->isChecked();
entry.n1mmPort = m_n1mmPortSpin->value();
```

**Step 5: Build and verify**

```bash
cmake --build build -j$(nproc)
```

**Step 6: Format and commit**

```bash
find src -name '*.cpp' -o -name '*.h' | xargs clang-format -i
git add src/ui/radiomanagerdialog.h src/ui/radiomanagerdialog.cpp
git commit -m "feat(ui): add N1MM spot settings to radio connection dialog"
```

---

## Task 9: Final Integration Test and Cleanup

**Step 1: Full rebuild**

```bash
cmake -B build -DCMAKE_PREFIX_PATH="$HOME/6.8.1/gcc_64" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

**Step 2: Lint check**

```bash
find src -name '*.cpp' -o -name '*.h' | xargs clang-format --dry-run --Werror
```

Expected: No formatting errors.

**Step 3: Manual testing checklist**

- [ ] Launch QK4, connect to K4 radio
- [ ] Open DISP popup, verify 8th "PAN ON" button appears
- [ ] Click PAN ON — verify label changes to "PAN OFF", panadapter hides, spectrum stops
- [ ] Click PAN OFF — verify label changes to "PAN ON", panadapter shows, spectrum resumes
- [ ] Enable N1MM in radio settings dialog
- [ ] Verify N1MM listener starts on connect (check UDP port 12060 binding)
- [ ] Send test spot from N1MM (or test script), verify callsign label appears on panadapter
- [ ] Click a spot label, verify radio tunes to that frequency
- [ ] Disconnect/reconnect, verify settings persist

**Step 4: Test N1MM without a radio (UDP only)**

For testing the N1MM listener independently, you can send a test UDP datagram:

```bash
echo '<spot><app>N1MM</app><dxcall>W1AW</dxcall><frequency>14025.0</frequency><action>add</action><mode>CW</mode><status>single mult</status><timestamp>2026-02-20 12:00:00</timestamp></spot>' | nc -u 127.0.0.1 12060
```
