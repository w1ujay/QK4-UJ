#ifndef MINIVIEWWINDOW_H
#define MINIVIEWWINDOW_H

#include "../network/n1mmlistener.h"
#include <QLabel>
#include <QPoint>
#include <QWidget>

class QPushButton;

/**
 * MiniViewWindow - Compact horizontal strip showing VFO frequencies and spots.
 *
 * Features:
 * - Always-on-top frameless window
 * - Shows VFO A/B frequencies with mode indicators
 * - Scrolling spot list from N1MM
 * - Draggable, position saved/restored
 * - Double-click to restore full view
 */
class MiniViewWindow : public QWidget {
    Q_OBJECT

public:
    explicit MiniViewWindow(QWidget *parent = nullptr);

    void setFrequencyA(const QString &freq);
    void setFrequencyB(const QString &freq);
    void setModeA(const QString &mode);
    void setModeB(const QString &mode);

    void addSpot(const SpotData &spot);
    void removeSpot(const QString &callsign);
    void clearSpots();

signals:
    void restoreRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void setupUi();
    void updateSpotDisplay();
    void savePosition();
    void restorePosition();

    // VFO labels
    QLabel *m_freqALabel;
    QLabel *m_modeALabel;
    QLabel *m_freqBLabel;
    QLabel *m_modeBLabel;

    // Spot display
    QLabel *m_spotLabel;
    QList<SpotData> m_spots;

    // Restore button
    QPushButton *m_restoreBtn;

    // Dragging
    bool m_dragging = false;
    QPoint m_dragPosition;
};

#endif // MINIVIEWWINDOW_H
