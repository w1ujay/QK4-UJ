#ifndef RFKITUICONTROLLER_H
#define RFKITUICONTROLLER_H

#include <QObject>

class RadioState;
class RFKitClient;
class RFKitWindow;
class StatusBarController;

// Owns the RFKitClient lifetime and the floating RFKitWindow, and keeps
// both the window's panel and the top-status-bar RFKit indicator in sync
// with the amplifier's telemetry and connection state. Also owns the
// RadioSettings observers that react to enable/host/port/temperature-unit
// changes, plus the drive-power lockout that forces the amplifier to
// standby when the K4's RF power exceeds the configured maximum.
//
// MainWindow retains only: (1) a getter to hand the RFKitClient to
// OptionsDialog for the RFKit tab, and (2) two task-level calls —
// connectIfEnabled() on K4 auth success, and disconnectFromHost() on K4
// disconnect — because RFKit connectivity is gated on K4 connectivity,
// matching the KPA1500 pattern.
//
// See PATTERNS.md → Controller Pattern.
class RFKitUiController : public QObject {
    Q_OBJECT

public:
    // RadioState is injected so the controller can watch RF power for the
    // drive-power lockout. StatusBarController hosts the status indicator.
    explicit RFKitUiController(StatusBarController *statusBar, RadioState *radioState, QObject *parent = nullptr);
    ~RFKitUiController() override;

    // Accessor for OptionsDialog's RFKit tab — the dialog configures the
    // client indirectly via RadioSettings; the controller observes the
    // resulting changes to drive reconnects.
    RFKitClient *client() const { return m_client; }

    // Connect if RadioSettings has RFKit enabled + host configured.
    // Called from MainWindow after K4 auth succeeds.
    void connectIfEnabled();

    // Disconnect from the amplifier. Called from MainWindow when the K4
    // disconnects so the amplifier isn't polled over a dead link.
    void disconnectFromHost();

private:
    StatusBarController *m_statusBar; // injected, not owned
    RadioState *m_radioState;         // injected, not owned
    RFKitWindow *m_window;            // owned via Qt parent (nullptr -> deleted in dtor)
    RFKitClient *m_client = nullptr;  // owned via Qt parent (this)

    void onConnected();
    void onDisconnected();
    void onError(const QString &error);
    void onEnabledChanged(bool enabled);
    void onSettingsChanged();
    void checkDrivePower();
    void updateStatus();
};

#endif // RFKITUICONTROLLER_H
