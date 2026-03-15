#include "miniviewwindow.h"
#include "../dsp/minipan_rhi.h"
#include "../settings/radiosettings.h"
#include "k4styles.h"
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QVBoxLayout>

namespace {
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
    setAttribute(Qt::WA_TranslucentBackground);
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
    m_bandBtn->setToolTip("Select band");
    m_bandBtn->setStyleSheet(btnStyle);
    connect(m_bandBtn, &QPushButton::clicked, this, &MiniViewWindow::bandClicked);
    layout->addWidget(m_bandBtn);
    layout->addSpacing(4);

    // MODE button
    m_modeBtn = new QPushButton("MODE", m_stripWidget);
    m_modeBtn->setFixedSize(BtnWidth, BtnHeight);
    m_modeBtn->setCursor(Qt::PointingHandCursor);
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
            m_miniPan = new MiniPanRhiWidget(this);
            m_miniPan->setMinimumWidth(StripWidth - 4);
            m_miniPan->setMaximumWidth(StripWidth - 4);
            m_miniPan->setFixedHeight(PanHeight);
            m_mainLayout->addWidget(m_miniPan);
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

void MiniViewWindow::addSpot(const SpotData &spot) {
    // Update or add
    for (int i = 0; i < m_spots.size(); ++i) {
        if (m_spots[i].callsign == spot.callsign) {
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
        if (m_spots[i].callsign == callsign) {
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
        parts << QString("%1 %2").arg(s.callsign, QString::number(mhz, 'f', 1));
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
    painter.setRenderHint(QPainter::Antialiasing);

    QColor bgColor(K4Styles::Colors::Background);
    QColor borderColor(K4Styles::Colors::BorderNormal);

    QPainterPath path;
    path.addRoundedRect(QRectF(1, 1, width() - 2, height() - 2), 6, 6);
    painter.fillPath(path, bgColor);

    painter.setPen(QPen(borderColor, BorderWidth));
    painter.drawPath(path);
}

void MiniViewWindow::mousePressEvent(QMouseEvent *event) {
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
