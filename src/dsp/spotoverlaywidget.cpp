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
    if (m_centerFreq == centerFreq && m_spanHz == spanHz && m_cwPitch == cwPitch && m_mode == mode)
        return;
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

void SpotOverlayWidget::setSpotColors(const QColor &mult, const QColor &newQso, const QColor &dupe) {
    m_multColor = mult;
    m_newQsoColor = newQso;
    m_dupeColor = dupe;
    update();
}

QColor SpotOverlayWidget::colorForStatus(const QString &status) const {
    if (status.contains("mult")) {
        return m_multColor.isValid() ? m_multColor : QColor(K4Styles::Colors::SpotMult);
    } else if (status.contains("dupe")) {
        return m_dupeColor.isValid() ? m_dupeColor : QColor(K4Styles::Colors::SpotDupe);
    } else if (status.contains("new qso") || status.isEmpty()) {
        return m_newQsoColor.isValid() ? m_newQsoColor : QColor(K4Styles::Colors::SpotNewQso);
    }
    return m_newQsoColor.isValid() ? m_newQsoColor : QColor(K4Styles::Colors::SpotDefault);
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
    std::sort(visible.begin(), visible.end(), [](const VisibleSpot &a, const VisibleSpot &b) { return a.x < b.x; });

    // Cap at 30 visible spots (keep newest by age timer)
    if (visible.size() > 30) {
        std::sort(visible.begin(), visible.end(), [](const VisibleSpot &a, const VisibleSpot &b) {
            return a.spot.age.elapsed() < b.spot.age.elapsed();
        });
        visible.resize(30);
        std::sort(visible.begin(), visible.end(), [](const VisibleSpot &a, const VisibleSpot &b) { return a.x < b.x; });
    }

    m_paintedLabels.clear();
    m_paintedLabels.reserve(visible.size());

    // Stagger rows to avoid overlap (up to 3 rows)
    static constexpr int MAX_ROWS = 3;
    int rowHeight = fm.height() + 4;
    QVector<float> rowRightEdge(MAX_ROWS, -100.0f);

    for (const VisibleSpot &vs : visible) {
        QColor color = colorForStatus(vs.spot.status);
        bool isMult = vs.spot.status.contains("mult");

        // Set font weight based on status
        QFont spotFont = font;
        spotFont.setBold(isMult);
        spotFont.setPixelSize(isMult ? 12 : 10);
        painter.setFont(spotFont);
        QFontMetrics spotFm(spotFont);

        // Draw tick mark
        QPen tickPen(color, isMult ? 2.0 : 1.0);
        painter.setPen(tickPen);
        int tickBottom = h - 2;
        int tickTop = tickBottom - tickHeight;
        painter.drawLine(QPointF(vs.x, tickTop), QPointF(vs.x, tickBottom));

        // Find best row (lowest row where label doesn't overlap)
        int textWidth = spotFm.horizontalAdvance(vs.spot.callsign);
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
        QRectF textRect(labelLeft, labelY - spotFm.height(), textWidth + 4, spotFm.height() + 2);

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
