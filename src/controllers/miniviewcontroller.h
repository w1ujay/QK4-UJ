#ifndef MINIVIEWCONTROLLER_H
#define MINIVIEWCONTROLLER_H

#include <QByteArray>
#include <QObject>

class DxClusterController;
class MainWindow;
class MiniViewWindow;
class RadioState;
class VFOWidget;

// Owns the parentless MiniViewWindow and keeps it in sync with RadioState,
// the VFO widgets, and the DX cluster spot cache. MainWindow calls
// showMiniView() when the MINI button is pressed and feeds spectrum
// payloads in via onSpectrumData(); everything else is observed here.
//
// The window is deliberately parentless so it survives MainWindow::hide()
// — the controller therefore deletes it explicitly in its destructor.
//
// See PATTERNS.md → Controller Pattern.
class MiniViewController : public QObject {
    Q_OBJECT

public:
    MiniViewController(RadioState *radioState, DxClusterController *dxClusterController, VFOWidget *vfoA,
                       VFOWidget *vfoB, MainWindow *mainWindow, QObject *parent = nullptr);
    ~MiniViewController() override;

    MiniViewWindow *window() const { return m_window; }

    // Populate from current state, show the mini view, and hide the main window.
    void showMiniView();

    // Feed main-receiver spectrum payloads to the embedded mini panadapter.
    // Cheap no-op unless the mini view is visible with its panadapter shown.
    void onSpectrumData(int receiver, const QByteArray &payload);

signals:
    // Emitted when the user asks to return to the full window, and when the
    // mini view's BAND / MODE buttons are pressed (those restore first, then
    // open the corresponding popup in the main window).
    void restoreRequested();
    void bandPopupRequested();
    void modePopupRequested();

private:
    void refreshAll();
    void refreshSpots();

    RadioState *m_radioState;                   // injected, not owned
    DxClusterController *m_dxClusterController; // injected, not owned
    VFOWidget *m_vfoA;                          // injected, not owned
    VFOWidget *m_vfoB;                          // injected, not owned
    MainWindow *m_mainWindow;                   // injected, not owned
    MiniViewWindow *m_window;                   // parentless — deleted in dtor
};

#endif // MINIVIEWCONTROLLER_H
