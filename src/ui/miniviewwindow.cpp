#include "miniviewwindow.h"
#include "dsp/minipan_rhi.h"
#include "settings/radiosettings.h"
#include "ui/styling/k4styles.h"
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLoggingCategory>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWindow>

Q_LOGGING_CATEGORY(uiMiniView, "ui.miniview")

namespace {
// Diagnostics for the blank Mini View pan: adding the first QRhiWidget to this already-shown window
// can make Qt recreate it for GPU composition, which changes its native handle and surface type.
void logWindowSurface(const QWidget *window, const char *when) {
    const QWindow *handle = window->windowHandle();
    if (!handle) {
        qCDebug(uiMiniView) << "Mini View window" << when << "- no native window";
        return;
    }
    qCDebug(uiMiniView) << "Mini View window" << when << "- winId" << handle->winId() << "surface"
                        << handle->surfaceType() << "widget pos" << window->pos() << "frame" << window->frameGeometry()
                        << "native pos" << handle->position();
}

const int StripHeight = 52;
const int StripWidth = 780;
const int PanHeight = 130;
const int BorderWidth = 1;
const int FreqFontSize = 16;
const int ModeFontSize = 11;
const int SpotFontSize = 10;
const int MaxVisibleSpots = 4;
const int BtnFontSize = 10;
const int BtnWidth = 40;
const int BtnHeight = 24;
} // namespace

MiniViewWindow::MiniViewWindow(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) {
    // WHY no WA_TranslucentBackground: the pan used to render nothing here and the desktop showed
    // through it. The real cause was the pan never getting a QRhi (see togglePanadapter), not
    // translucency; the window stays opaque with square corners because that is what is tested.
    setFocusPolicy(Qt::StrongFocus); // receive Up/Down for tuning
    setupUi();
    restorePosition();
}

void MiniViewWindow::setupUi() {
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);

    // ===== Top strip =====
    m_stripWidget = new QWidget(this);
    m_stripWidget->setFixedSize(StripWidth, StripHeight);
    m_stripWidget->setCursor(Qt::SizeAllCursor);

    auto *layout = new QHBoxLayout(m_stripWidget);
    layout->setContentsMargins(12, 4, 8, 4);
    layout->setSpacing(0);

    const QString freqStyle = QString("color: %1; font-size: %2px; font-weight: bold; background: transparent; "
                                      "font-feature-settings: 'tnum';")
                                  .arg(K4Styles::Colors::VfoACyan)
                                  .arg(FreqFontSize);
    const QString freqBStyle = QString("color: %1; font-size: %2px; font-weight: bold; background: transparent; "
                                       "font-feature-settings: 'tnum';")
                                   .arg(K4Styles::Colors::VfoBGreen)
                                   .arg(FreqFontSize);
    const QString modeStyle = QString("color: %1; font-size: %2px; background: transparent;")
                                  .arg(K4Styles::Colors::TextGray)
                                  .arg(ModeFontSize);

    // VFO A section
    auto *labelA = new QLabel("A", m_stripWidget);
    labelA->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold; background: transparent;")
                              .arg(K4Styles::Colors::VfoACyan)
                              .arg(ModeFontSize));
    layout->addWidget(labelA);
    layout->addSpacing(4);

    m_freqALabel = new QLabel("14.074.000", m_stripWidget);
    m_freqALabel->setStyleSheet(freqStyle);
    m_freqALabel->installEventFilter(this); // wheel tunes VFO A
    layout->addWidget(m_freqALabel);
    layout->addSpacing(4);

    m_modeALabel = new QLabel("USB", m_stripWidget);
    m_modeALabel->setStyleSheet(modeStyle);
    m_modeALabel->setFixedWidth(32);
    layout->addWidget(m_modeALabel);

    // Separator
    layout->addSpacing(8);
    auto *sep1 = new QWidget(m_stripWidget);
    sep1->setFixedSize(1, 28);
    sep1->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::BorderNormal));
    layout->addWidget(sep1);
    layout->addSpacing(8);

    // VFO B section
    auto *labelB = new QLabel("B", m_stripWidget);
    labelB->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold; background: transparent;")
                              .arg(K4Styles::Colors::VfoBGreen)
                              .arg(ModeFontSize));
    layout->addWidget(labelB);
    layout->addSpacing(4);

    m_freqBLabel = new QLabel("14.076.000", m_stripWidget);
    m_freqBLabel->setStyleSheet(freqBStyle);
    m_freqBLabel->installEventFilter(this); // wheel tunes VFO B
    layout->addWidget(m_freqBLabel);
    layout->addSpacing(4);

    m_modeBLabel = new QLabel("USB", m_stripWidget);
    m_modeBLabel->setStyleSheet(modeStyle);
    m_modeBLabel->setFixedWidth(32);
    layout->addWidget(m_modeBLabel);

    // Separator before band/mode
    layout->addSpacing(6);
    m_bandModeSeparator = new QWidget(m_stripWidget);
    m_bandModeSeparator->setFixedSize(1, 28);
    m_bandModeSeparator->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::BorderNormal));
    layout->addWidget(m_bandModeSeparator);
    layout->addSpacing(6);

    // Band/Mode button style
    const QString btnStyle =
        QString("QPushButton { "
                "  background-color: %1; "
                "  color: %2; "
                "  border: 1px solid %3; "
                "  border-radius: 3px; "
                "  font-size: %4px; "
                "  font-weight: bold; "
                "  padding: 1px 4px; "
                "} "
                "QPushButton:hover { "
                "  background-color: %5; "
                "  color: %1; "
                "}")
            .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::AccentAmber, K4Styles::Colors::BorderNormal)
            .arg(BtnFontSize)
            .arg(K4Styles::Colors::AccentAmber);

    // BAND button
    m_bandBtn = new QPushButton("BAND", m_stripWidget);
    m_bandBtn->setFixedSize(BtnWidth, BtnHeight);
    m_bandBtn->setCursor(Qt::PointingHandCursor);
    m_bandBtn->setFocusPolicy(Qt::NoFocus); // keep Up/Down for tuning
    m_bandBtn->setToolTip("Select band");
    m_bandBtn->setStyleSheet(btnStyle);
    connect(m_bandBtn, &QPushButton::clicked, this, &MiniViewWindow::bandClicked);
    layout->addWidget(m_bandBtn);
    layout->addSpacing(4);

    // MODE button
    m_modeBtn = new QPushButton("MODE", m_stripWidget);
    m_modeBtn->setFixedSize(BtnWidth, BtnHeight);
    m_modeBtn->setCursor(Qt::PointingHandCursor);
    m_modeBtn->setFocusPolicy(Qt::NoFocus);
    m_modeBtn->setToolTip("Select mode");
    m_modeBtn->setStyleSheet(btnStyle);
    connect(m_modeBtn, &QPushButton::clicked, this, &MiniViewWindow::modeClicked);
    layout->addWidget(m_modeBtn);

    // Separator before spots
    layout->addSpacing(6);
    m_spotSeparator = new QWidget(m_stripWidget);
    m_spotSeparator->setFixedSize(1, 28);
    m_spotSeparator->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::BorderNormal));
    layout->addWidget(m_spotSeparator);
    layout->addSpacing(6);

    // Spots section
    m_spotLabel = new QLabel("No spots", m_stripWidget);
    m_spotLabel->setStyleSheet(QString("color: %1; font-size: %2px; background: transparent;")
                                   .arg(K4Styles::Colors::TextGray)
                                   .arg(SpotFontSize));
    m_spotLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(m_spotLabel, 1);

    // Panadapter toggle button
    m_panToggleBtn = new QPushButton(QString::fromUtf8("\u25BC"), m_stripWidget); // Down arrow
    m_panToggleBtn->setFixedSize(24, 24);
    m_panToggleBtn->setCursor(Qt::PointingHandCursor);
    m_panToggleBtn->setFocusPolicy(Qt::NoFocus);
    m_panToggleBtn->setToolTip("Toggle panadapter");
    m_panToggleBtn->setStyleSheet(QString("QPushButton { "
                                          "  background-color: transparent; "
                                          "  color: %1; "
                                          "  border: none; "
                                          "  font-size: 10px; "
                                          "} "
                                          "QPushButton:hover { "
                                          "  background-color: %2; "
                                          "  border-radius: 3px; "
                                          "}")
                                      .arg(K4Styles::Colors::TextGray)
                                      .arg(K4Styles::Colors::BorderNormal));
    connect(m_panToggleBtn, &QPushButton::clicked, this, &MiniViewWindow::togglePanadapter);
    layout->addWidget(m_panToggleBtn);
    layout->addSpacing(4);

    // Restore button
    m_restoreBtn = new QPushButton(QString::fromUtf8("\u25A1"), m_stripWidget); // Unicode square (maximize icon)
    m_restoreBtn->setFixedSize(24, 24);
    m_restoreBtn->setCursor(Qt::PointingHandCursor);
    m_restoreBtn->setFocusPolicy(Qt::NoFocus);
    m_restoreBtn->setToolTip("Restore full view");
    m_restoreBtn->setStyleSheet(QString("QPushButton { "
                                        "  background-color: transparent; "
                                        "  color: %1; "
                                        "  border: none; "
                                        "  font-size: 14px; "
                                        "  font-weight: bold; "
                                        "} "
                                        "QPushButton:hover { "
                                        "  background-color: %2; "
                                        "  border-radius: 3px; "
                                        "}")
                                    .arg(K4Styles::Colors::TextGray)
                                    .arg(K4Styles::Colors::BorderNormal));
    connect(m_restoreBtn, &QPushButton::clicked, this, &MiniViewWindow::restoreRequested);
    layout->addWidget(m_restoreBtn);

    m_mainLayout->addWidget(m_stripWidget);

    // Apply saved settings for section visibility
    applySettings();
    updateWindowSize();
}

void MiniViewWindow::applySettings() {
    auto *s = RadioSettings::instance();
    bool showBand = s->miniViewShowBand();
    bool showMode = s->miniViewShowMode();
    bool showSpots = s->miniViewShowSpots();

    m_bandBtn->setVisible(showBand);
    m_modeBtn->setVisible(showMode);
    m_bandModeSeparator->setVisible(showBand || showMode);
    m_spotLabel->setVisible(showSpots);
    m_spotSeparator->setVisible(showSpots && (showBand || showMode));

    // If pan was showing and setting changed, handle it
    bool showPan = s->miniViewShowPanadapter();
    m_panToggleBtn->setVisible(true); // Always show pan toggle
    if (!showPan && m_panVisible) {
        m_panVisible = false;
        if (m_miniPan)
            m_miniPan->setVisible(false);
        emit panadapterVisibilityChanged(false);
    }

    updateWindowSize();
}

void MiniViewWindow::updateWindowSize() {
    int totalHeight = StripHeight;
    if (m_panVisible) {
        totalHeight += PanHeight;
    }
    setFixedSize(StripWidth, totalHeight);
}

void MiniViewWindow::togglePanadapter() {
    m_panVisible = !m_panVisible;

    if (m_panVisible) {
        // Lazily create MiniPanRhiWidget
        if (!m_miniPan) {
            logWindowSurface(this, "before pan created");
            // WHY parentless until addWidget(): Qt switches an already-shown window to GPU composition
            // only when a QRhiWidget is reparented into it. Constructed with `this` as parent, the
            // window stayed on raster flush and the pan never got a QRhi ("QRhiWidget: No QRhi").
            m_miniPan = new MiniPanRhiWidget(nullptr);
            m_miniPan->setObjectName("MiniView");
            connect(m_miniPan, &QRhiWidget::renderFailed, this,
                    []() { qCWarning(uiMiniView) << "Mini View pan renderFailed() - QRhi could not be obtained"; });
            QTimer::singleShot(500, this, [this]() { logWindowSurface(this, "500 ms after pan created"); });
            m_miniPan->setMinimumWidth(StripWidth - 4);
            m_miniPan->setMaximumWidth(StripWidth - 4);
            m_miniPan->setFixedHeight(PanHeight);
            m_mainLayout->addWidget(m_miniPan);

            // Cyan passband, matching VFO A's mini-pan in the main window
            QColor passband(K4Styles::Colors::VfoACyan);
            passband.setAlpha(64);
            m_miniPan->setPassbandColor(passband);
            // Clicks on the pan don't reach this window's mousePressEvent, so take keyboard focus here too
            connect(m_miniPan, &MiniPanRhiWidget::clicked, this, [this]() { activateWindow(); });
            connect(m_miniPan, &MiniPanRhiWidget::rightClicked, this, [this](int offsetHz) {
                activateWindow();
                emit panRightClicked(offsetHz);
            });
        }
        m_miniPan->setVisible(true);
        m_panToggleBtn->setText(QString::fromUtf8("\u25B2")); // Up arrow
        m_panToggleBtn->setToolTip("Hide panadapter");
    } else {
        if (m_miniPan)
            m_miniPan->setVisible(false);
        m_panToggleBtn->setText(QString::fromUtf8("\u25BC")); // Down arrow
        m_panToggleBtn->setToolTip("Show panadapter");
    }

    // Save pan state
    RadioSettings::instance()->setMiniViewShowPanadapter(m_panVisible);

    updateWindowSize();
    emit panadapterVisibilityChanged(m_panVisible);
}

void MiniViewWindow::updateSpectrum(const QByteArray &data) {
    if (m_miniPan && m_panVisible)
        m_miniPan->updateSpectrum(data);
}

void MiniViewWindow::setPanMode(const QString &mode) {
    if (m_miniPan)
        m_miniPan->setMode(mode);
}

void MiniViewWindow::setPanFilterBandwidth(int bwHz) {
    if (m_miniPan)
        m_miniPan->setFilterBandwidth(bwHz);
}

void MiniViewWindow::setPanIfShift(int shift) {
    if (m_miniPan)
        m_miniPan->setIfShift(shift);
}

void MiniViewWindow::setPanCwPitch(int pitchHz) {
    if (m_miniPan)
        m_miniPan->setCwPitch(pitchHz);
}

void MiniViewWindow::setPanNotchFilter(bool enabled, int pitchHz) {
    if (m_miniPan)
        m_miniPan->setNotchFilter(enabled, pitchHz);
}

void MiniViewWindow::setPanDataSubMode(int subMode) {
    if (m_miniPan)
        m_miniPan->setDataSubMode(subMode);
}

void MiniViewWindow::setPanAveraging(int level) {
    if (m_miniPan)
        m_miniPan->setAveraging(level);
}

void MiniViewWindow::setPanWaterfallHeight(int percent) {
    if (m_miniPan)
        m_miniPan->setWaterfallHeight(percent);
}

bool MiniViewWindow::isPanadapterVisible() const {
    return m_panVisible && m_miniPan;
}

void MiniViewWindow::setFrequencyA(const QString &freq) {
    m_freqALabel->setText(freq);
}

void MiniViewWindow::setFrequencyB(const QString &freq) {
    m_freqBLabel->setText(freq);
}

void MiniViewWindow::setModeA(const QString &mode) {
    m_modeALabel->setText(mode);
}

void MiniViewWindow::setModeB(const QString &mode) {
    m_modeBLabel->setText(mode);
}

void MiniViewWindow::addSpot(const DxSpot &spot) {
    // Update or add
    for (int i = 0; i < m_spots.size(); ++i) {
        if (m_spots[i].spottedCall == spot.spottedCall) {
            m_spots[i] = spot;
            updateSpotDisplay();
            return;
        }
    }
    m_spots.prepend(spot);
    updateSpotDisplay();
}

void MiniViewWindow::removeSpot(const QString &callsign) {
    for (int i = 0; i < m_spots.size(); ++i) {
        if (m_spots[i].spottedCall == callsign) {
            m_spots.removeAt(i);
            break;
        }
    }
    updateSpotDisplay();
}

void MiniViewWindow::clearSpots() {
    m_spots.clear();
    updateSpotDisplay();
}

void MiniViewWindow::updateSpotDisplay() {
    if (m_spots.isEmpty()) {
        m_spotLabel->setText("No spots");
        return;
    }

    QStringList parts;
    int count = qMin(m_spots.size(), MaxVisibleSpots);
    for (int i = 0; i < count; ++i) {
        const auto &s = m_spots[i];
        // Format frequency as MHz with kHz precision
        double mhz = s.frequencyHz / 1000000.0;
        parts << QString("%1 %2").arg(s.spottedCall, QString::number(mhz, 'f', 1));
    }
    QString text = parts.join(QString::fromUtf8("  \u00B7  ")); // middle dot separator
    if (m_spots.size() > MaxVisibleSpots) {
        text += QString(" +%1").arg(m_spots.size() - MaxVisibleSpots);
    }
    m_spotLabel->setText(text);
}

void MiniViewWindow::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    QPainter painter(this);

    // Opaque, square-cornered frame (rounded corners needed a translucent window — see constructor)
    painter.fillRect(rect(), QColor(K4Styles::Colors::Background));
    painter.setPen(QPen(QColor(K4Styles::Colors::BorderNormal), BorderWidth));
    painter.drawRect(rect().adjusted(0, 0, -BorderWidth, -BorderWidth));
}

void MiniViewWindow::mousePressEvent(QMouseEvent *event) {
    activateWindow(); // take keyboard focus so Up/Down tune
    if (event->button() == Qt::LeftButton) {
        // Only drag from the strip area, not from the panadapter
        if (event->position().y() <= StripHeight) {
            m_dragging = true;
            m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
        }
    }
    QWidget::mousePressEvent(event);
}

void MiniViewWindow::mouseMoveEvent(QMouseEvent *event) {
    if (m_dragging) {
        move(event->globalPosition().toPoint() - m_dragPosition);
    }
    QWidget::mouseMoveEvent(event);
}

void MiniViewWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (m_dragging) {
        m_dragging = false;
        savePosition();
    }
    QWidget::mouseReleaseEvent(event);
}

void MiniViewWindow::mouseDoubleClickEvent(QMouseEvent *event) {
    // Only restore on double-click in strip area
    if (event->position().y() <= StripHeight) {
        emit restoreRequested();
    }
}

void MiniViewWindow::keyPressEvent(QKeyEvent *event) {
    // Up/Down arrows tune like the main window (VFO A, or VFO B while B SET is on)
    const bool plain = (event->modifiers() & ~Qt::KeyboardModifiers(Qt::KeypadModifier)) == Qt::NoModifier;
    if (plain && (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
        emit tuneStepsRequested(event->key() == Qt::Key_Up ? 1 : -1);
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

bool MiniViewWindow::eventFilter(QObject *watched, QEvent *event) {
    // Mouse wheel over a VFO frequency tunes that VFO by its tuning step
    if (event->type() == QEvent::Wheel && (watched == m_freqALabel || watched == m_freqBLabel)) {
        const bool vfoB = (watched == m_freqBLabel);
        const int steps = (vfoB ? m_wheelB : m_wheelA).accumulate(static_cast<QWheelEvent *>(event));
        if (steps != 0)
            emit frequencyScrolled(vfoB, steps);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void MiniViewWindow::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    restorePosition();
    applySettings();
}

void MiniViewWindow::hideEvent(QHideEvent *event) {
    savePosition();
    QWidget::hideEvent(event);
}

void MiniViewWindow::savePosition() {
    RadioSettings::instance()->setMiniViewWindowPosition(pos());
}

void MiniViewWindow::restorePosition() {
    QPoint savedPos = RadioSettings::instance()->miniViewWindowPosition();
    if (!savedPos.isNull()) {
        // Verify the saved position is visible on at least one screen
        QRect windowRect(savedPos, size());
        bool onScreen = false;
        for (QScreen *screen : QGuiApplication::screens()) {
            if (screen->availableGeometry().intersects(windowRect)) {
                onScreen = true;
                break;
            }
        }
        if (onScreen) {
            move(savedPos);
        } else {
            if (QScreen *screen = QGuiApplication::primaryScreen()) {
                QRect screenGeo = screen->availableGeometry();
                move(screenGeo.center() - QPoint(width() / 2, height() / 2));
            }
            savePosition();
        }
    }
}
