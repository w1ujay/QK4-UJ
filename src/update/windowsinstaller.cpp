#include "update/windowsinstaller.h"
#include "settings/interfacescale.h"
#include "update/updatepolicy.h"
#include "update/updateversion.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QThread>
#include <QUuid>

namespace {

constexpr int TAR_LIST_TIMEOUT_MS = 60000;
constexpr int TAR_EXTRACT_TIMEOUT_MS = 600000;
constexpr int TAR_POLL_MS = 100;
constexpr int RETRY_FIRST_DELAY_MS = 50;
constexpr int RETRY_MAX_DELAY_MS = 2000;

// Runs attempt until it succeeds or the window ends. Stops early when the installer is being destroyed.
bool retryWithBackoff(int windowMs, const std::function<bool()> &attempt) {
    QElapsedTimer timer;
    timer.start();
    int delayMs = RETRY_FIRST_DELAY_MS;
    while (!attempt()) {
        if (timer.elapsed() >= windowMs || QThread::currentThread()->isInterruptionRequested())
            return false;
        QThread::msleep(delayMs);
        delayMs = qMin(delayMs * 2, RETRY_MAX_DELAY_MS);
    }
    return true;
}

bool removeFolder(const QString &path, int windowMs) {
    return retryWithBackoff(windowMs, [&] { return !QFileInfo::exists(path) || QDir(path).removeRecursively(); });
}

bool runTar(const QString &program, const QStringList &arguments, int timeoutMs, QByteArray *output = nullptr) {
    QProcess tar;
    tar.setProgram(program);
    tar.setArguments(arguments);
    tar.setStandardInputFile(QProcess::nullDevice());
    tar.start(QIODevice::ReadOnly);

    QElapsedTimer timer;
    timer.start();
    if (!tar.waitForStarted()) {
        qWarning() << "Update: could not start" << program << tar.errorString();
        return false;
    }
    while (!tar.waitForFinished(TAR_POLL_MS) && tar.state() != QProcess::NotRunning) {
        if (timer.elapsed() >= timeoutMs || QThread::currentThread()->isInterruptionRequested()) {
            qWarning() << "Update: stopped tar after" << timer.elapsed() << "ms";
            tar.kill();
            tar.waitForFinished();
            return false;
        }
    }
    if (tar.exitStatus() != QProcess::NormalExit || tar.exitCode() != 0) {
        qWarning() << "Update: tar failed with exit code" << tar.exitCode() << tar.readAllStandardError();
        return false;
    }
    if (output)
        *output = tar.readAllStandardOutput();
    return true;
}

// tar.exe writes to stdout in text mode, so each line ends in CR LF on Windows.
QStringList parseTarListing(const QByteArray &listing) {
    QStringList entries;
    for (QString line : QString::fromUtf8(listing).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (line.endsWith(QLatin1Char('\r')))
            line.chop(1);
        if (!line.isEmpty())
            entries << line;
    }
    return entries;
}

QString systemTarPath() {
    return qEnvironmentVariable("SystemRoot") + QStringLiteral("/System32/tar.exe");
}

bool startDetachedLauncher(const QString &program, const QStringList &arguments,
                           const QProcessEnvironment &environment) {
    QProcess process;
    process.setProgram(program);
    process.setArguments(arguments);
    process.setProcessEnvironment(environment);
    return process.startDetached();
}

} // namespace

bool extractPackageWithTar(const QString &tarProgram, const QString &zipPath, const QString &incomingPath,
                           const QString &version) {
    const QString nativeZip = QDir::toNativeSeparators(zipPath);
    QByteArray listing;
    if (!runTar(tarProgram, {QStringLiteral("-tf"), nativeZip}, TAR_LIST_TIMEOUT_MS, &listing))
        return false;
    if (!validateZipEntries(parseTarListing(listing), version)) {
        qWarning() << "Update: the package does not have the expected layout for" << version;
        return false;
    }
    return runTar(tarProgram,
                  {QStringLiteral("-xf"), nativeZip, QStringLiteral("-C"), QDir::toNativeSeparators(incomingPath),
                   QStringLiteral("--strip-components=3"), QStringLiteral("QK4/versions/") + version},
                  TAR_EXTRACT_TIMEOUT_MS);
}

WindowsInstaller::WindowsInstaller(ManagedInstall install, QString zipPath, QString version, QObject *parent)
    : QObject(parent), m_install(std::move(install)), m_zipPath(std::move(zipPath)), m_version(std::move(version)) {}

WindowsInstaller::~WindowsInstaller() {
    if (m_thread) {
        m_thread->requestInterruption();
        m_thread->wait();
    }
}

void WindowsInstaller::setHooks(Hooks hooks) {
    m_hooks = std::move(hooks);
}

void WindowsInstaller::start() {
    if (m_thread)
        return;
    if (!m_hooks.extract)
        m_hooks.extract = [](const QString &zipPath, const QString &incomingPath, const QString &version) {
            return extractPackageWithTar(systemTarPath(), zipPath, incomingPath, version);
        };
    if (!m_hooks.renameFolder)
        // Refuses an existing target, on Windows and Linux alike, so it can never replace a version folder.
        m_hooks.renameFolder = [](const QString &from, const QString &to) { return QDir().rename(from, to); };
    if (!m_hooks.startLauncher)
        m_hooks.startLauncher = startDetachedLauncher;

    m_thread = QThread::create([this] { prepare(); });
    m_thread->setParent(this);
    connect(m_thread, &QThread::finished, this, &WindowsInstaller::onPreparationFinished);
    m_thread->start();
}

void WindowsInstaller::prepare() {
    m_outcome = prepareVersionFolder();
    const bool removed = retryWithBackoff(m_hooks.retryWindowMs,
                                          [&] { return !QFileInfo::exists(m_zipPath) || QFile::remove(m_zipPath); });
    if (!removed)
        qWarning() << "Update: could not delete" << m_zipPath;
}

WindowsInstaller::Outcome WindowsInstaller::prepareVersionFolder() {
    const std::optional<int> offered = UpdateVersion::parseUjNumber(m_version);
    if (!offered || !isSafeVersionName(m_version)) {
        qWarning() << "Update: refusing version name" << m_version;
        m_failure = tr("The update has an invalid version name, so nothing was installed.");
        return Outcome::Failed;
    }

    const std::optional<int> current = readCurrentTxtUj(m_install.root);
    if (current && *current >= *offered)
        return Outcome::AlreadyInstalled;

    const QDir versions(m_install.root + QStringLiteral("/versions"));
    const QString incomingName = QStringLiteral(".incoming-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString incoming = versions.filePath(incomingName);
    const QString target = versions.filePath(m_version);
    if (!versions.mkdir(incomingName)) {
        m_failure = tr("Could not create a temporary folder for the update in %1.")
                        .arg(QDir::toNativeSeparators(versions.path()));
        return Outcome::Failed;
    }

    if (!m_hooks.extract(m_zipPath, incoming, m_version) ||
        !QFileInfo(QDir(incoming).filePath(QStringLiteral("QK4.exe"))).isFile()) {
        removeFolder(incoming, m_hooks.retryWindowMs);
        m_failure = tr("The update could not be unpacked. Check there is enough free disk space, then try again. "
                       "Your current version has not been changed.");
        return Outcome::Failed;
    }

    // A folder at the final name is always complete, so an existing one is reused and never touched.
    const bool placed = retryWithBackoff(m_hooks.retryWindowMs, [&] {
        return QFileInfo::exists(target) || m_hooks.renameFolder(incoming, target) || QFileInfo::exists(target);
    });
    const bool discarded = removeFolder(incoming, m_hooks.retryWindowMs);
    if (!discarded)
        qWarning() << "Update: could not delete" << incoming; // cleanup retries a day later
    if (!placed) {
        m_failure = tr("The new version could not be put in place because a file was in use. "
                       "Your current version has not been changed.");
        return Outcome::Failed;
    }
    return Outcome::Prepared;
}

void WindowsInstaller::onPreparationFinished() {
    switch (m_outcome) {
    case Outcome::AlreadyInstalled:
        emit alreadyInstalled();
        return;
    case Outcome::Failed:
        emit failed(m_failure);
        return;
    case Outcome::Prepared:
        break;
    }

    QStringList arguments = {QStringLiteral("--qk4-finish-update"), m_version,
                             QString::number(QCoreApplication::applicationPid()), QStringLiteral("--")};
    arguments += QCoreApplication::arguments().mid(1);
    const QString launcher = m_install.root + QStringLiteral("/QK4.exe");
    if (!m_hooks.startLauncher(launcher, arguments, InterfaceScale::childEnvironment())) {
        qWarning() << "Update: could not start" << launcher;
        emit failed(tr("The update launcher could not be started. QK4 will carry on running the current version."));
        return;
    }
    emit readyToQuit();
}
