#ifndef RFKITWINDOW_H
#define RFKITWINDOW_H

#include <QPoint>
#include <QWidget>

class RFKitPanel;
class QPushButton;

/**
 * RFKitWindow - Floating window container for RFKit amplifier panel.
 *
 * Features:
 * - Custom dark title bar with device name and close button
 * - Draggable by title bar
 * - Always on top of main window
 * - Position saved/restored via RadioSettings
 * - Close button hides window (doesn't disconnect amp)
 */
class RFKitWindow : public QWidget {
    Q_OBJECT

public:
    explicit RFKitWindow(QWidget *parent = nullptr);

    RFKitPanel *panel() const { return m_panel; }

signals:
    void closeRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    void setupUi();
    QRect titleBarRect() const;
    void savePosition();
    void restorePosition();

    RFKitPanel *m_panel = nullptr;
    QPushButton *m_closeBtn = nullptr;

    bool m_dragging = false;
    QPoint m_dragPosition;
};

#endif // RFKITWINDOW_H
