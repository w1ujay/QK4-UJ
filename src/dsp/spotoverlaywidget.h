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

    // Dynamic spot colors (overrides K4Styles defaults)
    void setSpotColors(const QColor &mult, const QColor &newQso, const QColor &dupe);

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

    // Custom spot colors (empty = use K4Styles defaults)
    QColor m_multColor;
    QColor m_newQsoColor;
    QColor m_dupeColor;
};

#endif // SPOTOVERLAYWIDGET_H
