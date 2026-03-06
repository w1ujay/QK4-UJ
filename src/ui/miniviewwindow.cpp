#include "miniviewwindow.h"
#include "../settings/radiosettings.h"
#include "k4styles.h"
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>

namespace {
const int StripHeight = 48;
const int StripWidth = 700;
const int BorderWidth = 1;
const int FreqFontSize = 16;
const int ModeFontSize = 11;
const int SpotFontSize = 10;
const int MaxVisibleSpots = 4;
} // namespace

MiniViewWindow::MiniViewWindow(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) {
    setAttribute(Qt::WA_TranslucentBackground);
    setupUi();
    restorePosition();
}

void MiniViewWindow::setupUi() {
    setFixedSize(StripWidth, StripHeight);
    setCursor(Qt::SizeAllCursor);

    auto *layout = new QHBoxLayout(this);
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
    const QString labelStyle = QString("color: %1; font-size: %2px; font-weight: bold; background: transparent;")
                                   .arg(K4Styles::Colors::TextGray)
                                   .arg(ModeFontSize);

    // VFO A section
    auto *labelA = new QLabel("A", this);
    labelA->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold; background: transparent;")
                              .arg(K4Styles::Colors::VfoACyan)
                              .arg(ModeFontSize));
    layout->addWidget(labelA);
    layout->addSpacing(4);

    m_freqALabel = new QLabel("14.074.000", this);
    m_freqALabel->setStyleSheet(freqStyle);
    layout->addWidget(m_freqALabel);
    layout->addSpacing(4);

    m_modeALabel = new QLabel("USB", this);
    m_modeALabel->setStyleSheet(modeStyle);
    m_modeALabel->setFixedWidth(32);
    layout->addWidget(m_modeALabel);

    // Separator
    layout->addSpacing(8);
    auto *sep1 = new QWidget(this);
    sep1->setFixedSize(1, 28);
    sep1->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::BorderNormal));
    layout->addWidget(sep1);
    layout->addSpacing(8);

    // VFO B section
    auto *labelB = new QLabel("B", this);
    labelB->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold; background: transparent;")
                              .arg(K4Styles::Colors::VfoBGreen)
                              .arg(ModeFontSize));
    layout->addWidget(labelB);
    layout->addSpacing(4);

    m_freqBLabel = new QLabel("14.076.000", this);
    m_freqBLabel->setStyleSheet(freqBStyle);
    layout->addWidget(m_freqBLabel);
    layout->addSpacing(4);

    m_modeBLabel = new QLabel("USB", this);
    m_modeBLabel->setStyleSheet(modeStyle);
    m_modeBLabel->setFixedWidth(32);
    layout->addWidget(m_modeBLabel);

    // Separator before spots
    layout->addSpacing(8);
    auto *sep2 = new QWidget(this);
    sep2->setFixedSize(1, 28);
    sep2->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::BorderNormal));
    layout->addWidget(sep2);
    layout->addSpacing(8);

    // Spots section
    m_spotLabel = new QLabel("No spots", this);
    m_spotLabel->setStyleSheet(QString("color: %1; font-size: %2px; background: transparent;")
                                   .arg(K4Styles::Colors::TextGray)
                                   .arg(SpotFontSize));
    m_spotLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(m_spotLabel, 1);

    // Restore button
    m_restoreBtn = new QPushButton(QString::fromUtf8("\u25A1"), this); // Unicode square (maximize icon)
    m_restoreBtn->setFixedSize(28, 28);
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
        m_dragging = true;
        m_dragPosition = event->globalPosition().toPoint() - frameGeometry().topLeft();
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
    Q_UNUSED(event)
    emit restoreRequested();
}

void MiniViewWindow::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    restorePosition();
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
