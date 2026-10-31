#ifndef WINDOWSINSTALLER_H
#define WINDOWSINSTALLER_H

#include "update/managedinstall.h"

#include <QObject>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <functional>

class QThread;

// Lists the zip with tarProgram, checks the entries with validateZipEntries, and only then unpacks the version folder
// so its QK4.exe lands directly in incomingPath. Blocking.
bool extractPackageWithTar(const QString &tarProgram, const QString &zipPath, const QString &incomingPath,
                           const QString &version);

// QK4's side of a Windows install (design "Install" steps 1 and 3-5): rechecks current.txt, unpacks the verified zip
// to versions\.incoming-<random>, gives it its final name and hands over to the launcher. The file work runs on its
// own thread, because extraction and lock retries take too long for the UI. The zip is deleted on every outcome.
//
// Exactly one of readyToQuit(), failed() or alreadyInstalled() is emitted after start(), unless the object is
// destroyed first, which stops the work.
class WindowsInstaller : public QObject {
    Q_OBJECT
public:
    // Antivirus and the search indexer briefly hold newly written files.
    static constexpr int RETRY_WINDOW_MS = 30000;

    // For tests. An empty function means the production behaviour. extract and renameFolder run on the worker thread.
    struct Hooks {
        std::function<bool(const QString &zipPath, const QString &incomingPath, const QString &version)> extract;
        std::function<bool(const QString &from, const QString &to)> renameFolder;
        std::function<bool(const QString &program, const QStringList &arguments,
                           const QProcessEnvironment &environment)>
            startLauncher;
        int retryWindowMs = RETRY_WINDOW_MS;
    };

    WindowsInstaller(ManagedInstall install, QString zipPath, QString version, QObject *parent);
    ~WindowsInstaller() override;

    void setHooks(Hooks hooks);
    void start();

signals:
    // The launcher is running and waiting for this process to exit.
    void readyToQuit();
    void failed(QString message);
    // current.txt is already at or above this version: another copy has updated, so nothing was installed.
    void alreadyInstalled();

private:
    enum class Outcome { Prepared, AlreadyInstalled, Failed };

    void prepare();
    Outcome prepareVersionFolder();
    void onPreparationFinished();

    ManagedInstall m_install;
    QString m_zipPath;
    QString m_version;
    Hooks m_hooks;
    QThread *m_thread = nullptr;
    // Written by the worker, and read only after it has finished.
    Outcome m_outcome = Outcome::Failed;
    QString m_failure;
};

#endif // WINDOWSINSTALLER_H
