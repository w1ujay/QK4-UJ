#include "update/updateflow.h"
#include "ui/dialogs/updatedialog.h"
#include "update/updatechecker.h"
#include "update/updatedownloader.h"
#include "update/updateversion.h"
#include "update/windowsappid.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUuid>
#include <algorithm>

namespace {

const char SKIPPED_KEY[] = "update/skippedUj";
const char FAILED_KEY[] = "update/failedUj";
const char ATTEMPT_ENV[] = "QK4_UPDATE_ATTEMPT";
constexpr qint64 FAILURE_NOTE_MAX_BYTES = 65536;

UpdatePlatform detectRunningPlatform() {
#ifdef Q_OS_WIN
    const bool isWindows = true;
#else
    const bool isWindows = false;
#endif
#ifdef Q_OS_MACOS
    const bool isMacos = true;
#else
    const bool isMacos = false;
#endif
    const bool isFlatpak =
        qEnvironmentVariableIsSet("FLATPAK_ID") || QFileInfo::exists(QStringLiteral("/.flatpak-info"));
    return detectPlatform(isWindows, isMacos, isFlatpak, QSysInfo::currentCpuArchitecture());
}

QSet<int> failedVersions() {
    QSet<int> failed;
    const QStringList stored = QSettings().value(FAILED_KEY).toStringList();
    for (const QString &item : stored) {
        bool ok = false;
        const int uj = item.toInt(&ok);
        if (ok)
            failed.insert(uj);
    }
    return failed;
}

// One command for a POSIX shell, whatever the path holds.
QString shellQuoted(const QString &text) {
    QString quoted = text;
    quoted.replace(QLatin1Char('\''), QLatin1String("'\\''"));
    return QLatin1Char('\'') + quoted + QLatin1Char('\'');
}

QString standardFolderOr(const QString &override, QStandardPaths::StandardLocation location) {
    return override.isEmpty() ? QStandardPaths::writableLocation(location) : override;
}

} // namespace

UpdateFlow::UpdateFlow(QString runningVersion, QUrl manifestUrl, QWidget *window, QObject *parent)
    : QObject(parent), m_runningVersion(std::move(runningVersion)), m_manifestUrl(std::move(manifestUrl)),
      m_window(window) {}

QUrl UpdateFlow::defaultManifestUrl() {
    return QUrl(QStringLiteral("https://github.com/%1/releases/latest/download/update.json")
                    .arg(QLatin1String(UpdateVersion::UPDATE_REPOSITORY)));
}

void UpdateFlow::setHooks(Hooks hooks) {
    m_hooks = std::move(hooks);
}

void UpdateFlow::start() {
    if (m_started)
        return;
    m_started = true;

    // First: the launcher is waiting on this, and every later step can stop early.
    writeReadinessFile();

    m_platform = m_hooks.platform.value_or(detectRunningPlatform());
    if (m_platform == UpdatePlatform::Windows) {
        m_install =
            detectManagedInstall(exePath(), m_hooks.launcherIdentity ? m_hooks.launcherIdentity : launcherHasIdentity);
    }
    m_runningUj = UpdateVersion::parseUjNumber(m_runningVersion);
    consumeFailureNotes();

    if (!m_runningUj || m_platform == UpdatePlatform::Unsupported) {
        apply(m_gate.onCheckDone(false));
        return;
    }

    if (m_install) {
        applyWindowsRelaunchIdentity(m_window, m_install->root + QStringLiteral("/QK4.exe"));
        if (readCurrentTxtUj(m_install->root) == m_runningUj)
            startCleanup();
    }

    m_checker = new UpdateChecker(m_manifestUrl, this);
    connect(m_checker, &UpdateChecker::finished, this, &UpdateFlow::onCheckFinished);
    m_checker->start();
}

void UpdateFlow::noteManualConnection() {
    apply(m_gate.onManualConnection());
}

QString UpdateFlow::exePath() const {
    return m_hooks.exePath.isEmpty() ? QCoreApplication::applicationFilePath() : m_hooks.exePath;
}

void UpdateFlow::writeReadinessFile() {
    const QString attempt = qEnvironmentVariable(ATTEMPT_ENV);
    if (attempt.isEmpty())
        return;
    // WHY cleared whatever happens: an auto-scale restart later in this session must not claim this attempt again.
    qunsetenv(ATTEMPT_ENV);

    if (!isSafeVersionName(attempt)) {
        qWarning() << "Update: ignoring attempt name" << attempt;
        return;
    }
    // A failed write is not fatal: the launcher falls back to looking for a visible window.
    QFile marker(QFileInfo(exePath()).absoluteDir().filePath(QStringLiteral(".ready-") + attempt));
    if (!marker.open(QIODevice::WriteOnly))
        qWarning() << "Update: could not write" << marker.fileName() << marker.errorString();
}

void UpdateFlow::consumeFailureNotes() {
    if (!m_install)
        return;
    const QString path = QDir(m_install->root).filePath(QStringLiteral("update-failed.txt"));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QString text = QString::fromUtf8(file.read(FAILURE_NOTE_MAX_BYTES));
    file.close();
    if (!QFile::remove(path))
        qWarning() << "Update: could not delete" << path;

    // WHY compared with current.txt and the running version: the launcher can leave a note for a version that another
    // attempt then committed, or that is running now while current.txt still names the one before it.
    const std::optional<int> current = readCurrentTxtUj(m_install->root);
    QSet<int> failed = failedVersions();
    QStringList names;
    for (const QString &line : text.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString version = line.trimmed();
        const std::optional<int> uj = UpdateVersion::parseUjNumber(version);
        if (!uj || (current && *uj <= *current) || (m_runningUj && *uj == *m_runningUj))
            continue;
        failed.insert(*uj);
        names << version;
    }
    if (names.isEmpty())
        return;

    QStringList stored;
    QList<int> ordered = failed.values();
    std::sort(ordered.begin(), ordered.end());
    for (int uj : ordered)
        stored << QString::number(uj);
    QSettings().setValue(FAILED_KEY, stored);

    // WHY not exec(): a nested loop here would hold the check, and with it the radio, until someone clicks OK.
    auto *note = new QMessageBox(QMessageBox::Warning, tr("QK4 update"),
                                 tr("The update to %1 did not start correctly, so QK4 went back to this version. "
                                    "It will not be offered again.")
                                     .arg(names.join(QStringLiteral(", "))),
                                 QMessageBox::Ok, m_window);
    note->setTextFormat(Qt::PlainText);
    note->setAttribute(Qt::WA_DeleteOnClose);
    note->setModal(false);
    note->show();
}

void UpdateFlow::startCleanup() {
    QList<int> failed = failedVersions().values();
    std::sort(failed.begin(), failed.end());
    QStringList csv;
    for (int uj : failed)
        csv << QString::number(uj);

    const QString launcher = m_install->root + QStringLiteral("/QK4.exe");
    const QStringList arguments = {QStringLiteral("--qk4-cleanup"), m_install->version, csv.join(QLatin1Char(','))};
    const bool started = m_hooks.startDetached ? m_hooks.startDetached(launcher, arguments)
                                               : QProcess::startDetached(launcher, arguments);
    if (!started)
        qWarning() << "Update: could not start cleanup through" << launcher;
}

void UpdateFlow::onCheckFinished(const std::optional<UpdateManifest> &manifest) {
    m_checker->deleteLater();
    m_checker = nullptr;

    bool found = false;
    if (!manifest) {
        qInfo() << "Update: no update information (network failure, bad file or timeout)";
    } else if (manifestIsUsable(*manifest)) {
        found = shouldOffer(*m_runningUj, m_install ? readCurrentTxtUj(m_install->root) : std::nullopt, manifest->uj,
                            QSettings().value(SKIPPED_KEY, 0).toInt(), failedVersions());
        if (found)
            m_offer = manifest;
        else
            qInfo() << "Update: nothing to offer, latest is" << manifest->version;
    }
    apply(m_gate.onCheckDone(found));
}

bool UpdateFlow::manifestIsUsable(const UpdateManifest &manifest) const {
    if (UpdateVersion::parseUjNumber(manifest.version) != manifest.uj || !isSafeVersionName(manifest.version)) {
        qInfo() << "Update: ignoring update.json, its version" << manifest.version << "is not uj" << manifest.uj;
        return false;
    }
    const QString key = assetKeyFor(m_platform);
    if (key.isEmpty())
        return true; // OtherLinux downloads nothing, so it needs no asset

    const auto asset = manifest.assets.constFind(key);
    if (asset == manifest.assets.constEnd()) {
        qInfo() << "Update: ignoring update.json, it has no" << key << "asset";
        return false;
    }
    const bool allowed = m_hooks.allowedUrl ? m_hooks.allowedUrl(asset->url) : isAllowedDownloadUrl(asset->url);
    if (!allowed || !isSafeVersionName(asset->name)) {
        qInfo() << "Update: ignoring update.json, asset" << asset->name << "at" << asset->url << "is not allowed";
        return false;
    }
    return true;
}

void UpdateFlow::apply(UpdateGate::Action action) {
    switch (action) {
    case UpdateGate::Action::None:
        return;
    case UpdateGate::Action::AllowStartupRadio:
        emit startupRadioAllowed();
        return;
    case UpdateGate::Action::ShowPopup:
        showPopup();
        return;
    case UpdateGate::Action::Finish:
        qInfo() << "Update: a manual connection ended the startup check for this session";
        return;
    }
}

void UpdateFlow::showPopup() {
    bool olderChangesMissing = false;
    const QList<UpdateRelease> releases = releasesNewerThan(*m_offer, *m_runningUj, &olderChangesMissing);
    const auto asset = m_offer->assets.constFind(assetKeyFor(m_platform));
    if (asset != m_offer->assets.constEnd())
        m_asset = *asset;

    m_dialog = new UpdateDialog(m_runningVersion, m_offer->version, releases, olderChangesMissing,
                                m_install.has_value(), m_window);
    m_dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(m_dialog, &UpdateDialog::installRequested, this, &UpdateFlow::onInstallRequested);
    connect(m_dialog, &UpdateDialog::retryRequested, this, &UpdateFlow::onInstallRequested);
    connect(m_dialog, &UpdateDialog::cancelRequested, this, &UpdateFlow::onCancelRequested);
    connect(m_dialog, &UpdateDialog::laterRequested, this, &UpdateFlow::onPopupDismissed);
    connect(m_dialog, &UpdateDialog::skipRequested, this, &UpdateFlow::onSkipRequested);
    m_dialog->show();
}

void UpdateFlow::onSkipRequested() {
    QSettings().setValue(SKIPPED_KEY, m_offer->uj);
    onPopupDismissed();
}

void UpdateFlow::onPopupDismissed() {
    // WHY queued: the dialog is still open while it emits this. The startup connection can raise its own message box,
    // and that must not stack on a popup that is closing.
    QTimer::singleShot(0, this, [this] { apply(m_gate.onPopupCleared()); });
}

void UpdateFlow::onInstallRequested() {
    if (m_stage != Stage::Idle || !m_dialog)
        return;

    switch (m_platform) {
    case UpdatePlatform::OtherLinux:
        showReleasePage();
        return;
    case UpdatePlatform::Flatpak: {
        // The sandbox cannot reach ~/Downloads, so the portal's save dialog picks the place.
        const QString suggested =
            QDir(standardFolderOr(m_hooks.downloadsFolder, QStandardPaths::DownloadLocation)).filePath(m_asset.name);
        const QString chosen = m_hooks.chooseSavePath
                                   ? m_hooks.chooseSavePath(suggested)
                                   : QFileDialog::getSaveFileName(m_dialog, tr("Save the QK4 update"), suggested);
        if (!chosen.isEmpty())
            startDownload(chosen, UpdateDownloader::ExistingFile::Replace); // the save dialog already asked
        return;
    }
    default:
        break;
    }

    if (!m_install) {
        const QString folder = standardFolderOr(m_hooks.downloadsFolder, QStandardPaths::DownloadLocation);
        QDir().mkpath(folder);
        startDownload(QDir(folder).filePath(m_asset.name), UpdateDownloader::ExistingFile::Keep);
        return;
    }

    // WHY again: the popup can have been open for hours, and another copy may have updated meanwhile.
    const std::optional<int> current = readCurrentTxtUj(m_install->root);
    if (current && *current >= m_offer->uj) {
        showAlreadyUpdated();
        return;
    }
    const QString folder =
        QDir(standardFolderOr(m_hooks.localDataFolder, QStandardPaths::AppLocalDataLocation)).filePath("update");
    QDir().mkpath(folder);
    startDownload(QDir(folder).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".zip")),
                  UpdateDownloader::ExistingFile::Replace);
}

void UpdateFlow::onCancelRequested() {
    // WHY only here: the installer's work cannot be left half-done, so it runs on until it signals.
    if (m_stage != Stage::Downloading)
        return;
    m_downloader->cancel(); // emits nothing, and removes the partial file
    endDownload();
    m_dialog->showIdleButtons();
}

void UpdateFlow::startDownload(const QString &destination, UpdateDownloader::ExistingFile existing) {
    m_stage = Stage::Downloading;
    m_downloader = new UpdateDownloader(m_asset, destination, existing, this);
    connect(m_downloader, &UpdateDownloader::progress, m_dialog, &UpdateDialog::showProgress);
    connect(m_downloader, &UpdateDownloader::succeeded, this, &UpdateFlow::onDownloadSucceeded);
    connect(m_downloader, &UpdateDownloader::failed, this, &UpdateFlow::onDownloadFailed);
    m_dialog->showProgress(0, m_asset.size);
    m_downloader->start();
}

void UpdateFlow::endDownload() {
    m_stage = Stage::Idle;
    m_downloader->deleteLater();
    m_downloader = nullptr;
}

void UpdateFlow::onDownloadSucceeded(const QString &path) {
    endDownload();
    if (m_install)
        startInstaller(path);
    else
        showSavedFileSteps(path);
}

void UpdateFlow::onDownloadFailed(const QString &message) {
    endDownload();
    m_dialog->showError(message);
}

void UpdateFlow::startInstaller(const QString &zipPath) {
    m_stage = Stage::Installing;
    m_dialog->showInstalling();
    m_installer = new WindowsInstaller(*m_install, zipPath, m_offer->version, this);
    m_installer->setHooks(m_hooks.installer);
    connect(m_installer, &WindowsInstaller::readyToQuit, this, &UpdateFlow::onReadyToQuit);
    connect(m_installer, &WindowsInstaller::failed, this, &UpdateFlow::onInstallFailed);
    connect(m_installer, &WindowsInstaller::alreadyInstalled, this, &UpdateFlow::onAlreadyInstalled);
    m_installer->start();
}

void UpdateFlow::endInstaller() {
    m_stage = Stage::Idle;
    m_installer->deleteLater();
    m_installer = nullptr;
}

void UpdateFlow::onReadyToQuit() {
    endInstaller();
    m_stage = Stage::Quitting;

    // The gate stays shut: no startup connection runs now, because QK4 is closing.
    m_dialog->disconnect(this);
    m_dialog->hide();
    m_dialog->deleteLater();
    m_window->close();
    // WHY both: close() runs closeEvent, which stops audio and USB in order, but Qt only ends the app when a visible
    // window closes. Mini View keeps the main window hidden, and the launcher would then wait 30 seconds and kill us.
    QCoreApplication::quit();
}

void UpdateFlow::onInstallFailed(const QString &message) {
    endInstaller();
    m_dialog->showError(message);
}

void UpdateFlow::onAlreadyInstalled() {
    endInstaller();
    showAlreadyUpdated();
}

void UpdateFlow::showAlreadyUpdated() {
    m_dialog->showManualSteps(tr("Another copy of QK4 has already installed this update. "
                                 "Close QK4 and reopen it to use the new version."),
                              QString());
}

void UpdateFlow::showSavedFileSteps(const QString &path) {
    const QString shown = QDir::toNativeSeparators(path);
    switch (m_platform) {
    case UpdatePlatform::Flatpak: {
        const QString command = QStringLiteral("flatpak install --user ") + shellQuoted(shown);
        m_dialog->showManualSteps(tr("Downloaded and verified. To install, close QK4 and run:\n\n%1").arg(command),
                                  command);
        return;
    }
    case UpdatePlatform::PiArm64: {
        const QString command = QStringLiteral("tar xzf ") + shellQuoted(shown);
        m_dialog->showManualSteps(
            tr("Downloaded and verified. To install, close QK4 and run this in the folder you want it in:\n\n%1\n\n"
               "Then start it with QK4/run.sh.")
                .arg(command),
            command);
        return;
    }
    default:
        m_dialog->showManualSteps(
            tr("Downloaded and verified. Saved to %1\n\nThis copy of QK4 cannot update itself. Close it, unzip the "
               "file into a folder you own, not Program Files, and run QK4.exe from there.")
                .arg(shown),
            shown);
        return;
    }
}

void UpdateFlow::showReleasePage() {
    const QString url = QStringLiteral("https://github.com/%1/releases/tag/uj.%2")
                            .arg(QLatin1String(UpdateVersion::UPDATE_REPOSITORY))
                            .arg(m_offer->uj);
    m_dialog->showManualSteps(
        tr("There is no download for this system. The new version is on its release page:\n\n%1").arg(url), url);
}
