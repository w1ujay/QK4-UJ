#include "rfkitpanel.h"
#include "k4styles.h"
#include <QHBoxLayout>
#include <QLinearGradient>
#include <QPainter>
#include <QVBoxLayout>

RFKitPanel::RFKitPanel(QWidget *parent) : QWidget(parent) {
    setFixedSize(270, 270);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setupUi();

    m_decayTimer = new QTimer(this);
    connect(m_decayTimer, &QTimer::timeout, this, &RFKitPanel::onDecayTimer);
}

void RFKitPanel::setupUi() {
    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->setContentsMargins(8, 0, 8, 8);
    buttonLayout->setSpacing(4);

    m_modeBtn = new QPushButton("STANDBY", this);
    m_modeBtn->setFixedHeight(K4Styles::Dimensions::ButtonHeightSmall);
    m_modeBtn->setStyleSheet(K4Styles::panelButtonWithDisabled());
    connect(m_modeBtn, &QPushButton::clicked, this, [this]() {
        if (m_sleeping) {
            emit wakeUpRequested();
        } else {
            emit modeToggled(!m_operate);
        }
    });

    m_antBtn = new QPushButton("ANT", this);
    m_antBtn->setFixedHeight(K4Styles::Dimensions::ButtonHeightSmall);
    m_antBtn->setStyleSheet(K4Styles::panelButtonWithDisabled());
    connect(m_antBtn, &QPushButton::clicked, this, [this]() {
        int nextAnt = (m_antennaNumber % m_antennaCount) + 1;
        emit antennaChanged(nextAnt);
    });

    m_tuneBtn = new QPushButton("TUNE", this);
    m_tuneBtn->setFixedHeight(K4Styles::Dimensions::ButtonHeightSmall);
    m_tuneBtn->setStyleSheet(K4Styles::panelButtonWithDisabled());
    m_tuneBtn->setEnabled(false);
    m_tuneBtn->setToolTip("Waiting for RF-Kit to implement this endpoint \xF0\x9F\x99\x84");

    m_clrBtn = new QPushButton("CLR", this);
    m_clrBtn->setFixedHeight(K4Styles::Dimensions::ButtonHeightSmall);
    m_clrBtn->setStyleSheet(K4Styles::panelButtonWithDisabled());
    connect(m_clrBtn, &QPushButton::clicked, this, &RFKitPanel::errorResetRequested);

    buttonLayout->addWidget(m_modeBtn);
    buttonLayout->addWidget(m_antBtn);
    buttonLayout->addWidget(m_tuneBtn);
    buttonLayout->addWidget(m_clrBtn);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addStretch();
    mainLayout->addLayout(buttonLayout);

    updateButtonStates();
}

void RFKitPanel::setMode(bool operate) {
    if (m_operate != operate) {
        m_operate = operate;
        updateButtonStates();
        update();
    }
}

void RFKitPanel::setAntenna(int number, const QString &name) {
    if (m_antennaNumber != number || m_antennaName != name) {
        m_antennaNumber = number;
        m_antennaName = name;
        updateButtonStates();
        update();
    }
}

void RFKitPanel::setForwardPower(float watts, float maxWatts) {
    m_maxForwardPower = qMax(100.0f, maxWatts);
    m_forwardPower = qBound(0.0f, watts, m_maxForwardPower);
    if (m_forwardPower > m_peakForwardPower) {
        m_peakForwardPower = m_forwardPower;
    }
    startDecayTimer();
    update();
}

void RFKitPanel::setReflectedPower(float watts) {
    m_reflectedPower = qBound(0.0f, watts, 100.0f);
    if (m_reflectedPower > m_peakReflectedPower) {
        m_peakReflectedPower = m_reflectedPower;
    }
    startDecayTimer();
    update();
}

void RFKitPanel::setSWR(float swr) {
    m_swr = qMax(1.0f, swr);
    if (m_swr > m_peakSwr) {
        m_peakSwr = m_swr;
    }
    startDecayTimer();
    update();
}

void RFKitPanel::setTemperature(float celsius) {
    m_temperature = qBound(0.0f, celsius, 150.0f);
    startDecayTimer();
    update();
}

void RFKitPanel::setVoltage(float volts) {
    if (m_voltage != volts) {
        m_voltage = volts;
        bool wasSleeping = m_sleeping;
        m_sleeping = (volts < SLEEP_VOLTAGE_THRESHOLD && m_connected);
        if (m_sleeping != wasSleeping) {
            updateButtonStates();
        }
        update();
    }
}

void RFKitPanel::setCurrent(float amps) {
    if (m_current != amps) {
        m_current = amps;
        update();
    }
}

void RFKitPanel::setFault(bool fault) {
    if (m_fault != fault) {
        m_fault = fault;
        update();
    }
}

void RFKitPanel::setConnected(bool connected) {
    if (m_connected != connected) {
        m_connected = connected;
        if (!connected) {
            m_sleeping = false;
            m_operateLocked = false;
        }
        m_antBtn->setEnabled(connected);
        m_clrBtn->setEnabled(connected);
        updateButtonStates();
        update();
    }
}

void RFKitPanel::setTempFahrenheit(bool fahrenheit) {
    if (m_tempFahrenheit != fahrenheit) {
        m_tempFahrenheit = fahrenheit;
        update();
    }
}

void RFKitPanel::setOperateLocked(bool locked) {
    if (m_operateLocked != locked) {
        m_operateLocked = locked;
        updateButtonStates();
        update();
    }
}

void RFKitPanel::setDeviceName(const QString &name) {
    if (m_deviceName != name) {
        m_deviceName = name;
        update();
    }
}

void RFKitPanel::setStatus(const QString &status) {
    if (m_status != status) {
        m_status = status;
        m_fault = status.contains("fault", Qt::CaseInsensitive) || status.contains("error", Qt::CaseInsensitive);
        update();
    }
}

void RFKitPanel::setAntennaCount(int count) {
    m_antennaCount = qMax(1, count);
}

void RFKitPanel::startDecayTimer() {
    if (!m_decayTimer->isActive()) {
        m_decayTimer->start(DECAY_INTERVAL_MS);
    }
}

void RFKitPanel::onDecayTimer() {
    bool needsUpdate = false;
    bool allSettled = true;

    auto animate = [&](float &display, float target, float minStep) {
        if (qAbs(display - target) < minStep) {
            if (display != target) {
                display = target;
                needsUpdate = true;
            }
        } else if (display < target) {
            float step = (target - display) * ATTACK_RATE;
            display += qMax(step, minStep);
            if (display > target)
                display = target;
            needsUpdate = true;
            allSettled = false;
        } else {
            float step = (display - target) * DECAY_RATE;
            display -= qMax(step, minStep);
            if (display < target)
                display = target;
            needsUpdate = true;
            allSettled = false;
        }
    };

    animate(m_displayForwardPower, m_forwardPower, 2.0f);
    animate(m_displayReflectedPower, m_reflectedPower, 0.2f);
    animate(m_displaySwr, m_swr, 0.01f);
    animate(m_displayTemperature, m_temperature, 0.2f);

    if (m_peakForwardPower > m_forwardPower) {
        float decay = (m_peakForwardPower - m_forwardPower) * PEAK_DECAY_RATE;
        m_peakForwardPower -= qMax(decay, 1.0f);
        if (m_peakForwardPower < m_forwardPower)
            m_peakForwardPower = m_forwardPower;
        needsUpdate = true;
        allSettled = false;
    }

    if (m_peakReflectedPower > m_reflectedPower) {
        float decay = (m_peakReflectedPower - m_reflectedPower) * PEAK_DECAY_RATE;
        m_peakReflectedPower -= qMax(decay, 0.1f);
        if (m_peakReflectedPower < m_reflectedPower)
            m_peakReflectedPower = m_reflectedPower;
        needsUpdate = true;
        allSettled = false;
    }

    if (m_peakSwr > m_swr) {
        float decay = (m_peakSwr - m_swr) * PEAK_DECAY_RATE;
        m_peakSwr -= qMax(decay, 0.005f);
        if (m_peakSwr < m_swr)
            m_peakSwr = m_swr;
        needsUpdate = true;
        allSettled = false;
    }

    if (allSettled) {
        m_decayTimer->stop();
    }

    if (needsUpdate) {
        update();
    }
}

void RFKitPanel::updateButtonStates() {
    if (m_sleeping) {
        m_modeBtn->setText("WAKE UP!");
        m_modeBtn->setEnabled(m_connected);
    } else if (m_operateLocked) {
        m_modeBtn->setText("STANDBY");
        m_modeBtn->setEnabled(false);
    } else {
        m_modeBtn->setText(m_operate ? "OPERATE" : "STANDBY");
        m_modeBtn->setEnabled(m_connected);
    }
    if (!m_antennaName.isEmpty()) {
        m_antBtn->setText(m_antennaName);
    } else if (m_antennaNumber > 0) {
        m_antBtn->setText(QString("ANT%1").arg(m_antennaNumber));
    }
}

void RFKitPanel::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);

    int w = width();

    // Background with border
    painter.fillRect(rect(), QColor(K4Styles::Colors::Background));
    painter.setPen(QPen(QColor(K4Styles::Colors::BorderNormal), 1));
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);

    // Header
    const int headerY = 6;
    const int headerHeight = 16;

    QFont titleFont = font();
    titleFont.setPixelSize(K4Styles::Dimensions::FontSizeMedium);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(QColor(K4Styles::Colors::AccentAmber));

    QString title = m_deviceName.isEmpty() ? "RFKit" : m_deviceName;
    if (title.length() > 12) {
        title = title.left(11) + "\u2026";
    }
    painter.drawText(8, headerY, 90, headerHeight, Qt::AlignLeft | Qt::AlignVCenter, title);

    drawStatusLabels(painter, headerY, headerHeight);

    // Separator line
    painter.setPen(QColor(K4Styles::Colors::BorderNormal));
    painter.drawLine(8, headerY + headerHeight + 2, w - 8, headerY + headerHeight + 2);

    // Meters
    const int meterSpacing = 50;
    int meterY = 28;

    // FWD meter — scale based on maxForwardPower
    float fwdRatio = m_displayForwardPower / m_maxForwardPower;
    float fwdPeakRatio = m_peakForwardPower / m_maxForwardPower;
    QString fwdValue = QString("%1W").arg(qRound(m_displayForwardPower));
    int maxW = qRound(m_maxForwardPower);
    QStringList fwdLabels = {"0", QString::number(maxW / 4), QString::number(maxW / 2), QString::number(maxW * 3 / 4),
                             QString::number(maxW)};
    drawMeter(painter, meterY, "FWD", fwdValue, fwdRatio, fwdPeakRatio, fwdLabels);
    meterY += meterSpacing;

    // SWR meter (1.0-3.0)
    float swrRatio = qMax(0.0f, (m_displaySwr - 1.0f) / 2.0f);
    float swrPeakRatio = qMax(0.0f, (m_peakSwr - 1.0f) / 2.0f);
    QString swrValue = m_displaySwr >= 3.0f ? ">3.0" : QString::number(qMax(1.0f, m_displaySwr), 'f', 1);
    drawMeter(painter, meterY, "SWR", swrValue, swrRatio, swrPeakRatio, {"1.0", "1.5", "2.0", "2.5", "3.0"});
    meterY += meterSpacing;

    // REF meter (0-100W)
    float refRatio = m_displayReflectedPower / 100.0f;
    float refPeakRatio = m_peakReflectedPower / 100.0f;
    QString refValue = QString("%1W").arg(qRound(m_displayReflectedPower));
    drawMeter(painter, meterY, "REF", refValue, refRatio, refPeakRatio, {"0", "25", "50", "75", "100"});
    meterY += meterSpacing;

    // TMP meter — C or F based on setting
    if (m_tempFahrenheit) {
        float tempF = m_displayTemperature * 9.0f / 5.0f + 32.0f;
        float tmpRatio = tempF / 212.0f;
        QString tmpValue = QString("%1\u00B0F").arg(qRound(tempF));
        drawMeter(painter, meterY, "TMP", tmpValue, tmpRatio, tmpRatio, {"32", "77", "122", "167", "212"}, false);
    } else {
        float tmpRatio = m_displayTemperature / 100.0f;
        QString tmpValue = QString("%1\u00B0C").arg(qRound(m_displayTemperature));
        drawMeter(painter, meterY, "TMP", tmpValue, tmpRatio, tmpRatio, {"0", "25", "50", "75", "100"}, false);
    }
    meterY += 38;

    // Info row: Voltage + Current
    drawInfoRow(painter, meterY);
}

void RFKitPanel::drawStatusLabels(QPainter &painter, int y, int height) {
    int w = width();

    QFont labelFont = font();
    labelFont.setPixelSize(K4Styles::Dimensions::FontSizeNormal);
    labelFont.setBold(true);
    painter.setFont(labelFont);

    // OPERATE/STANDBY/SLEEPING/OVER PWR
    QString modeText = m_sleeping ? "SLEEPING" : m_operateLocked ? "OVER PWR" : m_operate ? "OPERATE" : "STANDBY";
    QColor modeColor = m_sleeping        ? QColor(K4Styles::Colors::AccentAmber)
                       : m_operateLocked ? QColor(K4Styles::Colors::MeterRed)
                       : m_operate       ? QColor(K4Styles::Colors::StatusGreen)
                                         : QColor(K4Styles::Colors::InactiveGray);
    painter.setPen(modeColor);
    painter.drawText(100, y, 60, height, Qt::AlignLeft | Qt::AlignVCenter, modeText);

    // Antenna name or FAULT
    if (m_fault) {
        painter.setPen(QColor(K4Styles::Colors::MeterRed));
        painter.drawText(w - 50, y, 45, height, Qt::AlignRight | Qt::AlignVCenter, "FAULT");
    } else if (!m_antennaName.isEmpty()) {
        painter.setPen(QColor(K4Styles::Colors::TextGray));
        QString antText = m_antennaName;
        if (antText.length() > 8) {
            antText = antText.left(7) + "\u2026";
        }
        painter.drawText(w - 70, y, 65, height, Qt::AlignRight | Qt::AlignVCenter, antText);
    }
}

void RFKitPanel::drawInfoRow(QPainter &painter, int y) {
    int w = width();
    const int margin = 8;

    QFont infoFont = font();
    infoFont.setPixelSize(K4Styles::Dimensions::FontSizeNormal);
    painter.setFont(infoFont);

    // Voltage
    painter.setPen(QColor(K4Styles::Colors::TextGray));
    painter.drawText(margin, y, 30, 14, Qt::AlignLeft | Qt::AlignVCenter, "VDC");
    painter.setPen(QColor(K4Styles::Colors::TextWhite));
    painter.drawText(margin + 32, y, 60, 14, Qt::AlignLeft | Qt::AlignVCenter,
                     QString("%1V").arg(m_voltage, 0, 'f', 1));

    // Current
    painter.setPen(QColor(K4Styles::Colors::TextGray));
    painter.drawText(w / 2 + 10, y, 30, 14, Qt::AlignLeft | Qt::AlignVCenter, "IDC");
    painter.setPen(QColor(K4Styles::Colors::TextWhite));
    painter.drawText(w / 2 + 42, y, 60, 14, Qt::AlignLeft | Qt::AlignVCenter, QString("%1A").arg(m_current, 0, 'f', 1));
}

void RFKitPanel::drawMeter(QPainter &painter, int y, const QString &label, const QString &valueStr, float displayRatio,
                           float peakRatio, const QStringList &scaleLabels, bool large) {
    int w = width();
    const int margin = 8;
    const int labelWidth = 32;
    const int valueWidth = large ? 55 : 45;
    const int barHeight = large ? 14 : 10;
    const int fontSize = large ? 10 : 9;
    const int valueFontSize = large ? 14 : 11;

    QFont labelFont = font();
    labelFont.setPixelSize(fontSize);
    labelFont.setBold(true);
    painter.setFont(labelFont);
    painter.setPen(QColor(K4Styles::Colors::TextWhite));
    painter.drawText(margin, y, labelWidth, barHeight, Qt::AlignLeft | Qt::AlignVCenter, label);

    QFont valueFont = font();
    valueFont.setPixelSize(valueFontSize);
    valueFont.setBold(true);
    painter.setFont(valueFont);
    painter.drawText(w - margin - valueWidth, y, valueWidth, barHeight + 6, Qt::AlignRight | Qt::AlignVCenter,
                     valueStr);

    int barX = margin + labelWidth + 4;
    int barY = y + 4;
    int barWidth = w - barX - margin - valueWidth - 4;

    QRect trackRect(barX, barY, barWidth, barHeight);
    painter.fillRect(trackRect, QColor(K4Styles::Colors::DarkBackground));
    painter.setPen(QColor("#2a2a2a"));
    painter.drawRect(trackRect);

    if (displayRatio > 0.001f) {
        int fillWidth = static_cast<int>(barWidth * qMin(displayRatio, 1.0f));
        QLinearGradient gradient = K4Styles::meterGradient(barX, 0, barX + barWidth, 0);
        painter.fillRect(barX + 1, barY + 1, fillWidth - 2, barHeight - 2, gradient);
    }

    if (peakRatio > displayRatio + 0.01f) {
        drawPeakMarker(painter, barX, barY, barWidth, barHeight, peakRatio);
    }

    QFont scaleFont = font();
    scaleFont.setPixelSize(K4Styles::Dimensions::FontSizeSmall);
    painter.setFont(scaleFont);
    painter.setPen(QColor(K4Styles::Colors::TextGray));

    int scaleY = barY + barHeight + 2;
    int numLabels = scaleLabels.size();
    for (int i = 0; i < numLabels; i++) {
        int x = barX + (barWidth * i) / (numLabels - 1);
        int labelW = 36;
        int labelX = x - labelW / 2;
        if (i == 0)
            labelX = x;
        else if (i == numLabels - 1)
            labelX = x - labelW;
        painter.drawText(labelX, scaleY, labelW, 10, Qt::AlignCenter, scaleLabels[i]);
    }

    painter.setPen(QColor(K4Styles::Colors::InactiveGray));
    for (int i = 0; i < numLabels; i++) {
        int x = barX + (barWidth * i) / (numLabels - 1);
        painter.drawLine(x, barY, x, barY + 2);
    }
}

void RFKitPanel::drawPeakMarker(QPainter &painter, int barX, int barY, int barWidth, int barHeight, float peakRatio) {
    int peakX = barX + static_cast<int>(barWidth * qMin(peakRatio, 1.0f));
    peakX = qBound(barX + 1, peakX, barX + barWidth - 2);
    painter.setPen(QPen(QColor(K4Styles::Colors::TextWhite), 2));
    painter.drawLine(peakX, barY + 1, peakX, barY + barHeight - 1);
}
