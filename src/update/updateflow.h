#ifndef UPDATEFLOW_H
#define UPDATEFLOW_H

#include "update/managedinstall.h"
#include "update/updatedownloader.h"
#include "update/updategate.h"
#include "update/updatemanifest.h"
#include "update/updatepolicy.h"
#include "update/windowsinstaller.h"

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QWidget>
#include <functional>
#include <optional>

class UpdateChecker;
class UpdateDialog;

// The startup side of the fork update check. It runs once per process: readiness file, failure notes, cleanup, then
// the check, the popup and the download. The startup radio connection waits on startupRadioAllowed().
class UpdateFlow : public QObject {
    Q_OBJECT
public:
    // For tests. An empty value means the production behaviour.
    struct Hooks {
        std::optional<UpdatePlatform> platform;
        QString exePath;
        LauncherIdentityCheck launcherIdentity = nullptr;
        std::function<bool(const QUrl &url)> allowedUrl;
        std::function<bool(const QString &program, const QStringList &arguments)> startDetached;
        // Empty result: the person cancelled.
        std::function<QString(const QString &suggestedPath)> chooseSavePath;
        QString downloadsFolder;
        QString localDataFolder;
        WindowsInstaller::Hooks installer;
    };

    // runningVersion is QK4_VERSION, which only the app target defines. window parents the popup, and is closed to
    // hand over to the launcher.
    UpdateFlow(QString runningVersion, QUrl manifestUrl, QWidget *window, QObject *parent);

    static QUrl defaultManifestUrl();

    void setHooks(Hooks hooks);

    void start();
    // Must be called synchronously as a connection begins. The state signal comes from the I/O thread, too late to stop
    // a queued startup connection replacing the radio the person chose. Does nothing once the gate has finished.
    void noteManualConnection();

signals:
    // Emitted at most once. Never emitted after a manual connection, or for a successful install.
    void startupRadioAllowed();

private:
    enum class Stage { Idle, Downloading, Installing, Quitting };

    QString exePath() const;
    void writeReadinessFile();
    void consumeFailureNotes();
    void startCleanup();
    void onCheckFinished(const std::optional<UpdateManifest> &manifest);
    bool manifestIsUsable(const UpdateManifest &manifest) const;
    void apply(UpdateGate::Action action);
    void showPopup();
    void onPopupDismissed();
    void onSkipRequested();
    void onInstallRequested();
    void onCancelRequested();
    void startDownload(const QString &destination, UpdateDownloader::ExistingFile existing);
    void onDownloadSucceeded(const QString &path);
    void onDownloadFailed(const QString &message);
    void startInstaller(const QString &zipPath);
    void onReadyToQuit();
    void onInstallFailed(const QString &message);
    void onAlreadyInstalled();
    void showAlreadyUpdated();
    void showSavedFileSteps(const QString &path);
    void showReleasePage();
    void endDownload();
    void endInstaller();

    QString m_runningVersion;
    QUrl m_manifestUrl;
    QPointer<QWidget> m_window;
    Hooks m_hooks;
    UpdateGate m_gate;
    bool m_started = false;
    UpdatePlatform m_platform = UpdatePlatform::Unsupported;
    std::optional<int> m_runningUj;
    std::optional<ManagedInstall> m_install;
    std::optional<UpdateManifest> m_offer;
    UpdateAsset m_asset;
    Stage m_stage = Stage::Idle;
    UpdateChecker *m_checker = nullptr;
    UpdateDownloader *m_downloader = nullptr;
    WindowsInstaller *m_installer = nullptr;
    QPointer<UpdateDialog> m_dialog;
};

#endif // UPDATEFLOW_H
