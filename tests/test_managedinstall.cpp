#include <QtTest>

#include <QDir>
#include <QFile>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QStringConverter>
#include <QTemporaryDir>
#include <QThread>

#include "settings/interfacescale.h"
#include "update/managedinstall.h"
#include "update/windowsinstaller.h"

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace {

const QString OLD_VERSION = QStringLiteral("0.8.0-uj.13");
const QString NEW_VERSION = QStringLiteral("0.8.0-uj.14");

bool touch(const QString &path, const QByteArray &content = {}) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}

bool identityTrue(const QString &) {
    return true;
}

bool identityFalse(const QString &) {
    return false;
}

QString g_identityPath;
bool identityRecorded(const QString &path) {
    g_identityPath = path;
    return true;
}

// <root>/QK4.exe, <root>/current.txt, <root>/versions/<OLD_VERSION>/QK4.exe. Returns the running exe's path.
QString makeTree(const QString &root) {
    QDir(root).mkpath(QStringLiteral("versions/") + OLD_VERSION);
    touch(root + QStringLiteral("/QK4.exe"));
    touch(root + QStringLiteral("/current.txt"), OLD_VERSION.toUtf8() + "\r\n");
    const QString exe = root + QStringLiteral("/versions/") + OLD_VERSION + QStringLiteral("/QK4.exe");
    touch(exe);
    return exe;
}

QStringList incomingFolders(const QString &root) {
    return QDir(root + QStringLiteral("/versions"))
        .entryList({QStringLiteral(".incoming-*")}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
}

// A fake extractor that behaves like a successful extraction, one that wrote part of the app, or a slow one.
bool extractComplete(const QString &, const QString &incoming, const QString &) {
    return touch(incoming + QStringLiteral("/QK4.exe"), "new") && touch(incoming + QStringLiteral("/Qt6Core.dll"));
}

bool extractNoExe(const QString &, const QString &incoming, const QString &) {
    return touch(incoming + QStringLiteral("/Qt6Core.dll"));
}

bool extractDiskFull(const QString &, const QString &incoming, const QString &) {
    touch(incoming + QStringLiteral("/QK4.exe"));
    touch(incoming + QStringLiteral("/partial.dll"));
    return false;
}

bool skipOnWindows() {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

// Everything an install needs, with no launcher start: tests that want one set `hooks.startLauncher` to record it.
struct InstallFixture {
    QTemporaryDir dir;
    QString root;
    QString zip;
    QString program;
    QStringList arguments;
    QProcessEnvironment environment;
    int starts = 0;
    int extractions = 0;
    WindowsInstaller::Hooks hooks;

    InstallFixture() {
        root = dir.path() + QStringLiteral("/QK4");
        QDir().mkpath(root);
        makeTree(root);
        zip = dir.path() + QStringLiteral("/update.zip");
        touch(zip, "zip");
        hooks.extract = [this](const QString &zipPath, const QString &incoming, const QString &version) {
            ++extractions;
            return extractComplete(zipPath, incoming, version);
        };
        hooks.startLauncher = [this](const QString &p, const QStringList &a, const QProcessEnvironment &e) {
            ++starts;
            program = p;
            arguments = a;
            environment = e;
            return true;
        };
    }

    QString versionFolder(const QString &version = NEW_VERSION) const {
        return root + QStringLiteral("/versions/") + version;
    }
};

struct InstallSignals {
    QSignalSpy ready;
    QSignalSpy failed;
    QSignalSpy already;

    explicit InstallSignals(WindowsInstaller *installer)
        : ready(installer, &WindowsInstaller::readyToQuit), failed(installer, &WindowsInstaller::failed),
          already(installer, &WindowsInstaller::alreadyInstalled) {}

    // Exactly one outcome is ever emitted.
    bool waitForOutcome() {
        QElapsedTimer timer;
        timer.start();
        while (ready.count() + failed.count() + already.count() == 0 && timer.elapsed() < 10000)
            QTest::qWait(5);
        QTest::qWait(20);
        return ready.count() + failed.count() + already.count() == 1;
    }
};

// Stands in for tar.exe: `-tf` prints LISTING with Windows line endings, `-xf` records its arguments and unpacks.
QString makeFakeTar(const QString &folder, const QByteArray &listing, int listExit = 0, int extractExit = 0) {
    const QString path = folder + QStringLiteral("/fake-tar.sh");
    QByteArray script = "#!/bin/sh\n"
                        "printf '%s\\n' \"$@\" >> \"$(dirname \"$0\")/tar-calls.txt\"\n"
                        "echo --- >> \"$(dirname \"$0\")/tar-calls.txt\"\n"
                        "if [ \"$1\" = -tf ]; then\n";
    for (const QByteArray &line : listing.split('\n')) {
        if (!line.isEmpty())
            script += "  printf '" + line + "\\r\\n'\n";
    }
    script += "  exit " + QByteArray::number(listExit) +
              "\n"
              "fi\n"
              ": > \"$4/QK4.exe\"\n"
              "exit " +
              QByteArray::number(extractExit) + "\n";
    touch(path, script);
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return path;
}

QByteArray goodListing(const QString &version) {
    const QByteArray dir = "QK4/versions/" + version.toUtf8() + "/";
    return "QK4/\nQK4/QK4.exe\nQK4/current.txt\nQK4/versions/\n" + dir + "\n" + dir + "QK4.exe\n" + dir +
           "Qt6Core.dll\n";
}

QStringList splitTarCalls(const QString &folder) {
    QFile file(folder + QStringLiteral("/tar-calls.txt"));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll()).split(QStringLiteral("---\n"), Qt::SkipEmptyParts);
}

} // namespace

class TestManagedInstall : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void detectsManagedLayout();
    void rejectsOldStyleFolder();
    void rejectsMissingCurrentTxt();
    void rejectsLauncherWithoutIdentity();
    void rejectsReadOnlyRoot();
    void rejectsReadOnlyVersions();
    void realIdentityCheckRejectsAnOrdinaryFile();
    void identityStringMatchesLauncherResource();

    void readsCurrentTxtUj();

    void alreadyInstalledWhenCurrentCaughtUp_data();
    void alreadyInstalledWhenCurrentCaughtUp();
    void damagedCurrentTxtDoesNotStopInstall();
    void installsAndHandsOverToLauncher();
    void existingVersionFolderIsReused();
    void versionFolderAppearingDuringRenameIsReused();
    void extractionFailureRemovesIncoming();
    void missingExeRemovesIncoming();
    void unsafeVersionIsRefused();
    void transientRenameFailureIsRetried();
    void permanentRenameFailureRemovesIncoming();
    void launcherStartFailureKeepsFolder();
    void realLauncherGetsArgumentsAndEnvironment();
    void destroyingDuringExtractionStopsWork();

    void tarExtractorListsThenExtracts();
    void tarExtractorRefusesBadPackages_data();
    void tarExtractorRefusesBadPackages();
    void tarFailureIsReported_data();
    void tarFailureIsReported();

    void childEnvironmentStripsScale();

private:
    QTemporaryDir m_settingsDir;
};

void TestManagedInstall::initTestCase() {
    // childEnvironmentStripsScale writes the interface scale, which must never reach the real preferences.
    QVERIFY(m_settingsDir.isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
    const QSettings probe(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("QK4"),
                          QStringLiteral("QK4"));
    QVERIFY(probe.fileName().startsWith(m_settingsDir.path()));
}

void TestManagedInstall::detectsManagedLayout() {
    QTemporaryDir dir;
    const QString exe = makeTree(dir.path());

    const auto install = detectManagedInstall(exe, identityRecorded);

    QVERIFY(install.has_value());
    QCOMPARE(install->root, dir.path());
    QCOMPARE(install->version, OLD_VERSION);
    QCOMPARE(g_identityPath, dir.path() + QStringLiteral("/QK4.exe"));
    // The probes clean up after themselves.
    QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden).size(), 2);
    QCOMPARE(QDir(dir.path() + QStringLiteral("/versions")).entryList(QDir::Files | QDir::Hidden).size(), 0);
}

void TestManagedInstall::rejectsOldStyleFolder() {
    QTemporaryDir dir;
    touch(dir.path() + QStringLiteral("/QK4.exe"));
    touch(dir.path() + QStringLiteral("/current.txt"), OLD_VERSION.toUtf8());
    QVERIFY(!detectManagedInstall(dir.path() + QStringLiteral("/QK4.exe"), identityTrue).has_value());

    // One folder down, but the parent is not called versions.
    QDir(dir.path()).mkpath(QStringLiteral("other/") + OLD_VERSION);
    const QString nested = dir.path() + QStringLiteral("/other/") + OLD_VERSION + QStringLiteral("/QK4.exe");
    touch(nested);
    QVERIFY(!detectManagedInstall(nested, identityTrue).has_value());
}

void TestManagedInstall::rejectsMissingCurrentTxt() {
    QTemporaryDir dir;
    const QString exe = makeTree(dir.path());
    QVERIFY(QFile::remove(dir.path() + QStringLiteral("/current.txt")));
    QVERIFY(!detectManagedInstall(exe, identityTrue).has_value());
}

void TestManagedInstall::rejectsLauncherWithoutIdentity() {
    QTemporaryDir dir;
    const QString exe = makeTree(dir.path());
    QVERIFY(!detectManagedInstall(exe, identityFalse).has_value());
}

void TestManagedInstall::rejectsReadOnlyRoot() {
    if (skipOnWindows())
        QSKIP("A read-only attribute on a Windows folder does not stop file creation.");
#ifdef Q_OS_UNIX
    if (geteuid() == 0)
        QSKIP("Running as root ignores folder permissions, so this cannot be tested.");
#endif
    QTemporaryDir dir;
    const QString root = dir.path() + QStringLiteral("/QK4");
    QVERIFY(QDir().mkpath(root));
    const QString exe = makeTree(root);
    const auto normal = QFile::permissions(root);
    QVERIFY(QFile::setPermissions(root, QFile::ReadOwner | QFile::ExeOwner));
    const auto restore = qScopeGuard([&] { QFile::setPermissions(root, normal); });

    QVERIFY(!detectManagedInstall(exe, identityTrue).has_value());
}

void TestManagedInstall::rejectsReadOnlyVersions() {
    if (skipOnWindows())
        QSKIP("A read-only attribute on a Windows folder does not stop file creation.");
#ifdef Q_OS_UNIX
    if (geteuid() == 0)
        QSKIP("Running as root ignores folder permissions, so this cannot be tested.");
#endif
    QTemporaryDir dir;
    const QString exe = makeTree(dir.path());
    const QString versions = dir.path() + QStringLiteral("/versions");
    const auto normal = QFile::permissions(versions);
    QVERIFY(QFile::setPermissions(versions, QFile::ReadOwner | QFile::ExeOwner));
    const auto restore = qScopeGuard([&] { QFile::setPermissions(versions, normal); });

    QVERIFY(!detectManagedInstall(exe, identityTrue).has_value());
}

void TestManagedInstall::realIdentityCheckRejectsAnOrdinaryFile() {
    // This test program has no launcher resource on any platform; off Windows the check is always false.
    QVERIFY(!launcherHasIdentity(QCoreApplication::applicationFilePath()));
    QTemporaryDir dir;
    QVERIFY(!detectManagedInstall(makeTree(dir.path())).has_value());
}

void TestManagedInstall::identityStringMatchesLauncherResource() {
    QFile resource(QStringLiteral(QK4_SOURCE_DIR "/tools/windows-launcher/launcher.rc"));
    QVERIFY(resource.open(QIODevice::ReadOnly));
    const QByteArray expected = QByteArray("VALUE \"FileDescription\", \"") + LAUNCHER_FILE_DESCRIPTION + '"';
    QVERIFY(resource.readAll().contains(expected));
}

void TestManagedInstall::readsCurrentTxtUj() {
    QTemporaryDir dir;
    const QString path = dir.path() + QStringLiteral("/current.txt");

    QVERIFY(!readCurrentTxtUj(dir.path()).has_value()); // missing

    touch(path, "0.8.0-uj.14\r\n");
    QCOMPARE(readCurrentTxtUj(dir.path()), std::optional<int>(14));

    // The launcher accepts UTF-16 with a byte order mark, for a file edited by hand.
    QStringEncoder toUtf16(QStringConverter::Utf16LE, QStringConverter::Flag::WriteBom);
    const QByteArray utf16 = toUtf16.encode(QStringLiteral("0.8.0-uj.15\r\n"));
    touch(path, utf16);
    QCOMPARE(readCurrentTxtUj(dir.path()), std::optional<int>(15));

    touch(path, "garbage");
    QVERIFY(!readCurrentTxtUj(dir.path()).has_value());

    touch(path, "");
    QVERIFY(!readCurrentTxtUj(dir.path()).has_value());

    // Same rule as the launcher: not a plain folder name, not a version.
    touch(path, "..\\other-uj.14");
    QVERIFY(!readCurrentTxtUj(dir.path()).has_value());
    touch(path, "other/0.8.0-uj.14");
    QVERIFY(!readCurrentTxtUj(dir.path()).has_value());
}

void TestManagedInstall::alreadyInstalledWhenCurrentCaughtUp_data() {
    QTest::addColumn<QString>("current");
    QTest::newRow("same as offer") << NEW_VERSION;
    QTest::newRow("above offer") << QStringLiteral("0.9.0-uj.15");
}

void TestManagedInstall::alreadyInstalledWhenCurrentCaughtUp() {
    QFETCH(QString, current);
    InstallFixture f;
    touch(f.root + QStringLiteral("/current.txt"), current.toUtf8());
    WindowsInstaller installer({f.root, OLD_VERSION}, f.zip, NEW_VERSION, nullptr);
    installer.setHooks(f.hooks);
    InstallSignals outcome(&installer);

    installer.start();

    QVERIFY(outcome.waitForOutcome());
    QCOMPARE(outcome.already.count(), 1);
    QCOMPARE(f.extractions, 0);
    QCOMPARE(f.starts, 0);
    QVERIFY(!QFile::exists(f.zip));
    QVERIFY(incomingFolders(f.root).isEmpty());
}

void TestManagedInstall::damagedCurrentTxtDoesNotStopInstall() {
    InstallFixture f;
    touch(f.root + QStringLiteral("/current.txt"), "garbage");
    WindowsInstaller installer({f.root, OLD_VERSION}, f.zip, NEW_VERSION, nullptr);
    installer.setHooks(f.hooks);
    InstallSignals outcome(&installer);

    installer.start();

    QVERIFY(outcome.waitForOutcome());
    QCOMPARE(outcome.ready.count(), 1);
}

void TestManagedInstall::installsAndHandsOverToLauncher() {
    InstallFixture f;
    qputenv("QK4_SCALE_RELAUNCHED", "1");
    const auto restoreEnv = qScopeGuard([] { qunsetenv("QK4_SCALE_RELAUNCHED"); });
    WindowsInstaller installer({f.root, OLD_VERSION}, f.zip, NEW_VERSION, nullptr);
    installer.setHooks(f.hooks);
    InstallSignals outcome(&installer);

    installer.start();

    QVERIFY(outcome.waitForOutcome());
    QCOMPARE(outcome.ready.count(), 1);
    QVERIFY(QFile::exists(f.versionFolder() + QStringLiteral("/QK4.exe")));
    QVERIFY(incomingFolders(f.root).isEmpty());
    QVERIFY(!QFile::exists(f.zip));
    QCOMPARE(f.starts, 1);
    QCOMPARE(f.program, f.root + QStringLiteral("/QK4.exe"));
    const QStringList expected = {QStringLiteral("--qk4-finish-update"), NEW_VERSION,
                                  QString::number(QCoreApplication::applicationPid()), QStringLiteral("--")};
    QCOMPARE(f.arguments, expected + QCoreApplication::arguments().mid(1));
    QCOMPARE(f.environment.value(QStringLiteral("PATH")),
             QProcessEnvironment::systemEnvironment().value(QStringLiteral("PATH")));
    QVERIFY(!f.environment.contains(QStringLiteral("QK4_SCALE_RELAUNCHED")));
}

void TestManagedInstall::existingVersionFolderIsReused() {
    InstallFixture f;
    QVERIFY(QDir().mkpath(f.versionFolder()));
    QVERIFY(touch(f.versionFolder() + QStringLiteral("/QK4.exe"), "existing"));
    QVERIFY(touch(f.versionFolder() + QStringLiteral("/marker.txt"), "keep me"));
    WindowsInstaller installer({f.root, OLD_VERSION}, f.zip, NEW_VERSION, nullptr);
    installer.setHooks(f.hooks);
    InstallSignals outcome(&installer);

    installer.start();

    QVERIFY(outcome.waitForOutcome());
    QCOMPARE(outcome.ready.count(), 1);
    QCOMPARE(f.starts, 1);
    QVERIFY(incomingFolders(f.root).isEmpty());
    QFile exe(f.versionFolder() + QStringLiteral("/QK4.exe"));
    QVERIFY(exe.open(QIODevice::ReadOnly));
    QCOMPARE(exe.readAll(), QByteArray("existing"));
    QVERIFY(QFile::exists(f.versionFolder() + QStringLiteral("/marker.txt")));
    QVERIFY(!QFile::exists(f.versionFolder() + QStringLiteral("/Qt6Core.dll")));
    QVERIFY(!QFile::exists(f.zip));
}

void TestManagedInstall::versionFolderAppearingDuringRenameIsReused() {
    InstallFixture f;
    // Another copy wins the race after the exists check. The production rename must then refuse, not replace.
    f.hooks.renameFolder = [&f](const QString &from, const QString &to) {
        return QDir().mkpath(f.versionFolder()) && touch(f.versionFolder() + QStringLiteral("/marker.txt")) &&
               QDir().rename(from, to);
    };
    WindowsInstaller installer({f.root, OLD_VERSION}, f.zip, NEW_VERSION, nullptr);
    installer.setHooks(f.hooks);
    InstallSignals outcome(&installer);

    installer.start();

    QVERIFY(outcome.waitForOutcome());
    QCOMPARE(outcome.ready.count(), 1);
    QVERIFY(incomingFolders(f.root).isEmpty());
    QVERIFY(QFile::exists(f.versionFolder() + QStringLiteral("/marker.txt")));
    QVERIFY(!QFile::exists(f.versionFolder() + QStringLiteral("/QK4.exe")));
}

void TestManagedInstall::extractionFailureRemovesIncoming() {
    InstallFixture f;
    QStringList incomingSeen;
    f.hooks.extract = [&](const QString &zip, const QString &incoming, const QString &version) {
        incomingSeen << incoming;
        return extractDiskFull(zip, incoming, version);
    };
    WindowsInstaller installer({f.root, OLD_VERSION}, f.zip, NEW_VERSION, nullptr);
    installer.setHooks(f.hooks);
    InstallSignals outcome(&installer);

    installer.start();

    QVERIFY(outcome.waitForOutcome());
    QCOMPARE(outcome.failed.count(), 1);
    QVERIFY(!outcome.failed.first().first().toString().isEmpty());
    QCOMPARE(incomingSeen.size(), 1);
    QVERIFY(QFileInfo(incomingSeen.first()).fileName().startsWith(QStringLiteral(".incoming-")));
    QCOMPARE(QFileInfo(incomingSeen.first()).absolutePath(), f.root + QStringLiteral("/versions"));
    QVERIFY(incomingFolders(f.root).isEmpty());
    QVERIFY(!QFileInfo::exists(f.versionFolder()));
    QVERIFY(!QFile::exists(f.zip));
    QCOMPARE(f.starts, 0);
}

void TestManagedInstall::missingExeRemovesIncoming() {
    InstallFixture f;
    f.hooks.extract = extractNoExe;
    WindowsInstaller installer({f.root, OLD_VERSION}, f.zip, NEW_VERSION, nullptr);
    installer.setHooks(f.hooks);
    InstallSignals outcome(&installer);

    installer.start();

    QVERIFY(outcome.waitForOutcome());
    QCOMPARE(outcome.failed.count(), 1);
    QVERIFY(incomingFolders(f.root).isEmpty());
    QVERIFY(!QFileInfo::exists(f.versionFolder()));
    QVERIFY(!QFile::exists(f.zip));
    QCOMPARE(f.starts, 0);
}

void TestManagedInstall::unsafeVersionIsRefused() {
    InstallFixture f;
    WindowsInstaller installer({f.root, OLD_VERSION}, f.zip, QStringLiteral("../escape-uj.14"), nullptr);
    installer.setHooks(f.hooks);
    InstallSignals outcome(&installer);

    installer.start();

    QVERIFY(outcome.waitForOutcome());
    QCOMPARE(outcome.failed.count(), 1);
    QCOMPARE(f.extractions, 0);
    QVERIFY(!QFile::exists(f.zip));
}

void TestManagedInstall::transientRenameFailureIsRetried() {
    InstallFixture f;
    int attempts = 0;
    f.hooks.renameFolder = [&](const QString &from, const QString &to) {
        return ++attempts >= 3 && QDir().rename(from, to);
    };
    WindowsInstaller installer({f.root, OLD_VERSION}, f.zip, NEW_VERSION, nullptr);
    installer.setHooks(f.hooks);
    InstallSignals outcome(&installer);

    installer.start();

    QVERIFY(outcome.waitForOutcome());
    QCOMPARE(outcome.ready.count(), 1);
    QCOMPARE(attempts, 3);
    QVERIFY(QFile::exists(f.versionFolder() + QStringLiteral("/QK4.exe")));
    QCOMPARE(WindowsInstaller::Hooks().retryWindowMs, 30000);
}

void TestManagedInstall::permanentRenameFailureRemovesIncoming() {
    InstallFixture f;
    int attempts = 0;
    f.hooks.retryWindowMs = 150;
    f.hooks.renameFolder = [&](const QString &, const QString &) {
        ++attempts;
        return false;
    };
    WindowsInstaller installer({f.root, OLD_VERSION}, f.zip, NEW_VERSION, nullptr);
    installer.setHooks(f.hooks);
    InstallSignals outcome(&installer);

    installer.start();

    QVERIFY(outcome.waitForOutcome());
    QCOMPARE(outcome.failed.count(), 1);
    QVERIFY(attempts > 1);
    QVERIFY(incomingFolders(f.root).isEmpty());
    QVERIFY(!QFile::exists(f.zip));
    QCOMPARE(f.starts, 0);
}

void TestManagedInstall::launcherStartFailureKeepsFolder() {
    InstallFixture f;
    f.hooks.startLauncher = nullptr; // the real start
    QVERIFY(QFile::remove(f.root + QStringLiteral("/QK4.exe")));
    WindowsInstaller installer({f.root, OLD_VERSION}, f.zip, NEW_VERSION, nullptr);
    installer.setHooks(f.hooks);
    InstallSignals outcome(&installer);

    installer.start();

    QVERIFY(outcome.waitForOutcome());
    QCOMPARE(outcome.failed.count(), 1);
    QCOMPARE(outcome.ready.count(), 0);
    QVERIFY(QFile::exists(f.versionFolder() + QStringLiteral("/QK4.exe")));
    QVERIFY(!QFile::exists(f.zip));
}

void TestManagedInstall::realLauncherGetsArgumentsAndEnvironment() {
    if (skipOnWindows())
        QSKIP("Uses a shell script as the launcher.");
    InstallFixture f;
    f.hooks.startLauncher = nullptr; // the real start
    const QString launcher = f.root + QStringLiteral("/QK4.exe");
    const QString output = f.dir.path() + QStringLiteral("/launcher.txt");
    QVERIFY(touch(launcher,
                  "#!/bin/sh\n{ printf '%s\\n' \"$@\"; echo \"relaunched=${QK4_SCALE_RELAUNCHED:-unset}\"; } > \"" +
                      output.toUtf8() + ".tmp\"\nmv \"" + output.toUtf8() + ".tmp\" \"" + output.toUtf8() + "\"\n"));
    QVERIFY(QFile::setPermissions(launcher, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    qputenv("QK4_SCALE_RELAUNCHED", "1");
    const auto restoreEnv = qScopeGuard([] { qunsetenv("QK4_SCALE_RELAUNCHED"); });
    WindowsInstaller installer({f.root, OLD_VERSION}, f.zip, NEW_VERSION, nullptr);
    installer.setHooks(f.hooks);
    InstallSignals outcome(&installer);

    installer.start();

    QVERIFY(outcome.waitForOutcome());
    QCOMPARE(outcome.ready.count(), 1);
    QTRY_VERIFY(QFile::exists(output));
    QFile file(output);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QString text = QString::fromUtf8(file.readAll());
    QVERIFY(text.startsWith(QStringLiteral("--qk4-finish-update\n") + NEW_VERSION + QLatin1Char('\n') +
                            QString::number(QCoreApplication::applicationPid()) + QStringLiteral("\n--\n")));
    QVERIFY(text.contains(QStringLiteral("relaunched=unset")));
}

void TestManagedInstall::destroyingDuringExtractionStopsWork() {
    InstallFixture f;
    QAtomicInt entered;
    f.hooks.extract = [&entered](const QString &, const QString &, const QString &) {
        entered.storeRelease(1);
        while (!QThread::currentThread()->isInterruptionRequested())
            QThread::msleep(2);
        return false;
    };
    auto *installer = new WindowsInstaller({f.root, OLD_VERSION}, f.zip, NEW_VERSION, nullptr);
    installer->setHooks(f.hooks);
    installer->start();
    QTRY_VERIFY(entered.loadAcquire() == 1);

    delete installer; // must stop the worker and wait for it

    QVERIFY(incomingFolders(f.root).isEmpty());
    QVERIFY(!QFile::exists(f.zip));
    QCOMPARE(f.starts, 0);
}

void TestManagedInstall::tarExtractorListsThenExtracts() {
    if (skipOnWindows())
        QSKIP("Uses a shell script as tar.");
    QTemporaryDir dir;
    const QString tar = makeFakeTar(dir.path(), goodListing(NEW_VERSION));
    const QString zip = dir.path() + QStringLiteral("/my update.zip");
    const QString incoming = dir.path() + QStringLiteral("/incoming");
    QVERIFY(QDir().mkpath(incoming));

    QVERIFY(extractPackageWithTar(tar, zip, incoming, NEW_VERSION));

    const QStringList calls = splitTarCalls(dir.path());
    QCOMPARE(calls.size(), 2);
    QCOMPARE(calls[0].split(QLatin1Char('\n'), Qt::SkipEmptyParts), QStringList({QStringLiteral("-tf"), zip}));
    const QStringList extract = {QStringLiteral("-xf"),
                                 zip,
                                 QStringLiteral("-C"),
                                 incoming,
                                 QStringLiteral("--strip-components=3"),
                                 QStringLiteral("QK4/versions/") + NEW_VERSION};
    QCOMPARE(calls[1].split(QLatin1Char('\n'), Qt::SkipEmptyParts), extract);
}

void TestManagedInstall::tarExtractorRefusesBadPackages_data() {
    QTest::addColumn<QByteArray>("listing");
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("other version") << goodListing(QStringLiteral("0.8.0-uj.15"));
    QTest::newRow("no exe") << QByteArray("QK4/\nQK4/versions/\nQK4/versions/0.8.0-uj.14/\n");
    QTest::newRow("climbs out") << goodListing(NEW_VERSION) + "QK4/versions/0.8.0-uj.14/../../../evil.dll\n";
    QTest::newRow("extra top level") << goodListing(NEW_VERSION) + "QK4/evil.dll\n";
}

void TestManagedInstall::tarExtractorRefusesBadPackages() {
    if (skipOnWindows())
        QSKIP("Uses a shell script as tar.");
    QFETCH(QByteArray, listing);
    QTemporaryDir dir;
    const QString tar = makeFakeTar(dir.path(), listing);

    QVERIFY(!extractPackageWithTar(tar, dir.path() + QStringLiteral("/u.zip"), dir.path(), NEW_VERSION));

    QCOMPARE(splitTarCalls(dir.path()).size(), 1); // listed, never extracted
}

void TestManagedInstall::tarFailureIsReported_data() {
    QTest::addColumn<int>("listExit");
    QTest::addColumn<int>("extractExit");
    QTest::newRow("listing fails") << 1 << 0;
    QTest::newRow("extraction fails") << 0 << 1;
}

void TestManagedInstall::tarFailureIsReported() {
    if (skipOnWindows())
        QSKIP("Uses a shell script as tar.");
    QFETCH(int, listExit);
    QFETCH(int, extractExit);
    QTemporaryDir dir;
    const QString tar = makeFakeTar(dir.path(), goodListing(NEW_VERSION), listExit, extractExit);

    QVERIFY(!extractPackageWithTar(tar, dir.path() + QStringLiteral("/u.zip"), dir.path(), NEW_VERSION));
}

void TestManagedInstall::childEnvironmentStripsScale() {
    const auto restoreEnv = qScopeGuard([] {
        qunsetenv("QT_SCALE_FACTOR");
        qunsetenv("QK4_SCALE_RELAUNCHED");
    });
    qputenv("QK4_SCALE_RELAUNCHED", "1");

    // Set by the operator: passed on. This comes first because the "QK4 set it" state lasts for the process.
    qputenv("QT_SCALE_FACTOR", "1.5");
    InterfaceScale::applyBeforeApp();
    QProcessEnvironment env = InterfaceScale::childEnvironment();
    QCOMPARE(env.value(QStringLiteral("QT_SCALE_FACTOR")), QStringLiteral("1.5"));
    QVERIFY(!env.contains(QStringLiteral("QK4_SCALE_RELAUNCHED")));

    // Set by QK4 from the saved setting: removed, so the new version fits itself to the screen.
    qunsetenv("QT_SCALE_FACTOR");
    {
        QSettings store(QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("QK4"), QStringLiteral("QK4"));
        store.setValue(QStringLiteral("ui/interfaceScale"), 125);
    }
    InterfaceScale::applyBeforeApp();
    QCOMPARE(qEnvironmentVariable("QT_SCALE_FACTOR"), QStringLiteral("1.25"));
    env = InterfaceScale::childEnvironment();
    QVERIFY(!env.contains(QStringLiteral("QT_SCALE_FACTOR")));
    QVERIFY(!env.contains(QStringLiteral("QK4_SCALE_RELAUNCHED")));
}

QTEST_GUILESS_MAIN(TestManagedInstall)
#include "test_managedinstall.moc"
