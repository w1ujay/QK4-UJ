#ifndef MINIVIEWWINDOW_H
#define MINIVIEWWINDOW_H

#include "../network/n1mmlistener.h"
#include <QLabel>
#include <QPoint>
#include <QWidget>

class QPushButton;
class MiniPanRhiWidget;
class QVBoxLayout;

/**
 * MiniViewWindow - Compact horizontal strip showing VFO frequencies, mode/band controls, and spots.
 *
 * Features:
 * - Always-on-top frameless window
 * - Shows VFO A/B frequencies with mode indicators
 * - MODE and BAND buttons for quick selection
 * - Optional panadapter display below the strip
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

    // Panadapter
    void updateSpectrum(const QByteArray &data);
    void setPanMode(const QString &mode);
    void setPanFilterBandwidth(int bwHz);
    void setPanIfShift(int shift);
    void setPanCwPitch(int pitchHz);
    void setPanNotchFilter(bool enabled, int pitchHz);
    bool isPanadapterVisible() const;

    // Access to buttons for popup positioning
    QPushButton *bandButton() const { return m_bandBtn; }
    QPushButton *modeButton() const { return m_modeBtn; }

    // Apply settings (show/hide sections)
    void applySettings();

signals:
    void restoreRequested();
    void bandClicked();
    void modeClicked();

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
    void togglePanadapter();
    void updateWindowSize();

    // Main layout
    QVBoxLayout *m_mainLayout;
    QWidget *m_stripWidget;

    // VFO labels
    QLabel *m_freqALabel;
    QLabel *m_modeALabel;
    QLabel *m_freqBLabel;
    QLabel *m_modeBLabel;

    // Band/Mode buttons
    QPushButton *m_bandBtn;
    QPushButton *m_modeBtn;

    // Separator widgets (for show/hide)
    QWidget *m_bandModeSeparator;
    QWidget *m_spotSeparator;

    // Spot display
    QLabel *m_spotLabel;
    QList<SpotData> m_spots;

    // Panadapter
    MiniPanRhiWidget *m_miniPan = nullptr;
    QPushButton *m_panToggleBtn;
    bool m_panVisible = false;

    // Restore button
    QPushButton *m_restoreBtn;

    // Dragging
    bool m_dragging = false;
    QPoint m_dragPosition;
};

#endif // MINIVIEWWINDOW_H
