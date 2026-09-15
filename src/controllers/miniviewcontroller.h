#ifndef MINIVIEWCONTROLLER_H
#define MINIVIEWCONTROLLER_H

#include <QByteArray>
#include <QObject>

class ConnectionController;
class DxClusterController;
class MainWindow;
class MiniViewWindow;
class RadioState;
class VFOWidget;

// Owns the parentless MiniViewWindow and keeps it in sync with RadioState,
// the VFO widgets, and the DX cluster spot cache. MainWindow calls
// showMiniView() when the MINI button is pressed and feeds MiniPAN bins
// in via onSpectrumData(); everything else is observed here.
//
// The K4 only streams MiniPAN after #MP1. While the mini view's panadapter is
// showing, the controller sends #MP1 unless VFO A's mini-pan already turned the
// stream on, and sends #MP0 afterwards only if it was the one that turned it on.
//
// The window is deliberately parentless so it survives MainWindow::hide()
// — the controller therefore deletes it explicitly in its destructor.
//
// See PATTERNS.md → Controller Pattern.
class MiniViewController : public QObject {
    Q_OBJECT

public:
    MiniViewController(RadioState *radioState, ConnectionController *connection,
                       DxClusterController *dxClusterController, VFOWidget *vfoA, VFOWidget *vfoB,
                       MainWindow *mainWindow, QObject *parent = nullptr);
    ~MiniViewController() override;

    MiniViewWindow *window() const { return m_window; }

    // Populate from current state, show the mini view, and hide the main window.
    void showMiniView();

    // Feed main-receiver MiniPAN bins (packet header already stripped) to the embedded
    // mini panadapter. Cheap no-op unless the mini view is visible with its panadapter shown.
    void onSpectrumData(int receiver, const QByteArray &bins);

    // Re-read the VFO frequency displays (e.g. after a RIT/XIT change shifts what they show).
    void refreshFrequencies();

signals:
    // Emitted when the user asks to return to the full window, and when the
    // mini view's BAND / MODE buttons are pressed (those restore first, then
    // open the corresponding popup in the main window).
    void restoreRequested();
    void bandPopupRequested();
    void modePopupRequested();

    // Arrow keys (active VFO) or wheel over a frequency (that VFO) in the mini view.
    void tuneStepsRequested(bool vfoB, int steps);
    // Right-click on the mini view's panadapter (VFO A's spectrum): Hz offset from its center.
    void miniPanRightClicked(int offsetHz);

private:
    void refreshAll();
    void refreshPan();
    void refreshSpots();
    void hideWindow();
    void updateMiniPanStream();

    RadioState *m_radioState;                   // injected, not owned
    ConnectionController *m_connection;         // injected, not owned
    DxClusterController *m_dxClusterController; // injected, not owned
    VFOWidget *m_vfoA;                          // injected, not owned
    VFOWidget *m_vfoB;                          // injected, not owned
    MainWindow *m_mainWindow;                   // injected, not owned
    MiniViewWindow *m_window;                   // parentless — deleted in dtor
    bool m_ownsMiniPanStream = false;           // true while this controller's #MP1 is in effect
};

#endif // MINIVIEWCONTROLLER_H
