// The startup update flow (src/update/updateflow.cpp) against a local server, offscreen. No connection signal exists
// anywhere in this file, which proves the flow does not depend on one.

#include "update/updateflow.h"
#include "ui/dialogs/updatedialog.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <atomic>

namespace {

const char ALLOWED_URL[] = "https://github.com/w1ujay/QK4-UJ/releases/download/uj.14/package.bin";

// update.json after a delay, or never. The package is served from /package, or held back so a download stays running.
class FakeServer : public QObject {
public:
    explicit FakeServer(QObject *parent = nullptr) : QObject(parent) {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onRead(socket); });
            }
        });
        QVERIFY(m_server.listen(QHostAddress::LocalHost));
    }

    QUrl url(const QString &path) const {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path));
    }

    QByteArray m_manifest;
    QByteArray m_package;
    int m_manifestDelayMs = 0;
    bool m_silent = false;
    bool m_holdPackage = false;
    int m_manifestRequests = 0;
    int m_packageRequests = 0;

private:
    void onRead(QTcpSocket *socket) {
        m_buffers[socket] += socket->readAll();
        if (!m_buffers[socket].contains("\r\n\r\n"))
            return;
        const QByteArray request = m_buffers.take(socket);
        if (request.startsWith("GET /package")) {
            ++m_packageRequests;
            if (!m_holdPackage)
                reply(socket, "application/octet-stream", m_package);
            return;
        }
        ++m_manifestRequests;
        if (m_silent)
            return;
        QTimer::singleShot(m_manifestDelayMs, socket,
                           [this, socket] { reply(socket, "application/json", m_manifest); });
    }

    static void reply(QTcpSocket *socket, const QByteArray &type, const QByteArray &body) {
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: " + type + "\r\nConnection: close\r\nContent-Length: " +
                      QByteArray::number(body.size()) + "\r\n\r\n" + body);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

// Counts close events, and is what the flow closes.
class Window : public QMainWindow {
public:
    int closes = 0;

protected:
    void closeEvent(QCloseEvent *event) override {
        ++closes;
        QMainWindow::closeEvent(event);
    }
};

QByteArray manifestJson(const QString &version, int uj, const QString &assetKey, const QString &url,
                        const QByteArray &package = QByteArray("package"), const QString &name = "package.bin") {
    const QByteArray hash = QCryptographicHash::hash(package, QCryptographicHash::Sha256).toHex();
    return QStringLiteral(R"({"schema":1,"version":"%1","uj":%2,"date":"2026-11-02",)"
                          R"("assets":{"%3":{"name":"%4","url":"%5","size":%6,"sha256":"%7"}},)"
                          R"("history":[{"uj":%2,"version":"%1","date":"2026-11-02",)"
                          R"("added":["New thing"],"fixed":[],"changed":[]}]})")
        .arg(version)
        .arg(uj)
        .arg(assetKey, name, url)
        .arg(package.size())
        .arg(QString::fromLatin1(hash))
        .toUtf8();
}

bool hasLauncherIdentity(const QString &) {
    return true;
}

QPushButton *button(const QWidget *widget, const QString &text) {
    for (QPushButton *b : widget->findChildren<QPushButton *>())
        if (b->text() == text)
            return b;
    return nullptr;
}

bool shown(const QWidget *widget, const QString &text) {
    QPushButton *b = button(widget, text);
    return b && b->isVisible();
}

QString labelText(const QWidget *widget) {
    QString text;
    for (const QLabel *label : widget->findChildren<QLabel *>())
        if (label->isVisible())
            text += label->text() + '\n';
    return text;
}

void writeFile(const QString &path, const QByteArray &bytes) {
    QVERIFY(QDir().mkpath(QFileInfo(path).path()));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(bytes);
}

// A launcher-managed layout, with QK4 running from versions\<version>.
struct ManagedLayout {
    explicit ManagedLayout(const QString &running = "0.8.0-uj.13", const QByteArray &current = "0.8.0-uj.13\n") {
        writeFile(root() + "/QK4.exe", "launcher");
        writeFile(root() + "/current.txt", current);
        writeFile(exePath(running), "app");
        version = running;
    }
    QString root() const { return dir.path(); }
    QString exePath(const QString &v) const { return dir.path() + "/versions/" + v + "/QK4.exe"; }
    QString exePath() const { return exePath(version); }

    QTemporaryDir dir;
    QString version;
};

UpdateFlow::Hooks hooksFor(UpdatePlatform platform, const QString &exePath = QString()) {
    UpdateFlow::Hooks hooks;
    hooks.platform = platform;
    hooks.exePath = exePath;
    return hooks;
}

UpdateFlow::Hooks managedHooks(const ManagedLayout &layout) {
    UpdateFlow::Hooks hooks = hooksFor(UpdatePlatform::Windows, layout.exePath());
    hooks.launcherIdentity = hasLauncherIdentity;
    hooks.startDetached = [](const QString &, const QStringList &) { return true; };
    return hooks;
}

} // namespace

class UpdateFlowTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QVERIFY(m_settingsDir.isValid());
        QCoreApplication::setOrganizationName("QK4Tests");
        QCoreApplication::setApplicationName("UpdateFlowTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
    }
    void init() {
        QSettings().clear();
        qunsetenv("QK4_UPDATE_ATTEMPT");
    }

    // The gate, through the real check.
    void manualConnectBeforeSlowCheckBlocksRadio();
    void manualConnectBeforeUpdateFoundShowsNoPopup();
    void noManualConnectAllowsRadioOnce();
    void timeoutWithoutManualConnectAllowsRadio();
    void noteAfterRadioAllowedIsHarmless();
    void nonUjBuildAllowsRadioImmediately();
    void unsupportedPlatformAllowsRadioImmediately();
    void manualConnectOnNonUjBuildStillBlocksRadio();
    void defaultManifestUrlIsTheForksLatestRelease();

    // What counts as an offer.
    void popupHoldsRadioUntilLater();
    void skipStoresVersionAndAllowsRadio();
    void skippedVersionIsNotOffered();
    void failedVersionIsNotOffered();
    void olderCopyIsNotOfferedWhatCurrentTxtHas();
    void mismatchedVersionIsAFailedCheck();
    void disallowedAssetUrlIsAFailedCheck();
    void missingAssetIsAFailedCheck();
    void unsafeAssetNameIsAFailedCheck();
    void malformedManifestIsAFailedCheck();

    // Startup work.
    void readinessFileWrittenBesideExeAndVariableCleared();
    void readinessFileWrittenOnNonUjBuild();
    void unsafeAttemptNameWritesNothing();
    void noAttemptWritesNothing();
    void failureNoteRecordedShownOnceAndDeleted();
    void failureNoteAtOrBelowCurrentIsIgnored();
    void failureNoteNamingTheRunningVersionIsIgnored();
    void failureNoteAddsToEarlierFailures();
    void failureNoteDoesNotDelayTheRadio();
    void cleanupRunsWhenOwnVersionIsCurrent();
    void cleanupSkippedForVersionUnderTest();
    void cleanupSkippedWhenNotManaged();

    // Downloading and installing.
    void cancelDuringDownloadReturnsToIdleButtons();
    void downloadErrorOffersRetryAndLater();
    void managedInstallClosesWindowWithoutRadio();
    void quitsEvenWithTheWindowHidden();
    void cancelIsIgnoredWhileInstalling();
    void installFailureShowsErrorAndRetryWorks();
    void anotherCopyUpdatedBeforeDownload();
    void anotherCopyUpdatedDuringInstall();
    void unmanagedWindowsSavesToDownloads();
    void downloadKeepsAnExistingFile();
    void piSavesToDownloadsAndShowsSteps();
    void flatpakUsesTheSaveDialog();
    void flatpakCancelledSaveStaysOnPopup();
    void otherLinuxShowsReleasePageAndDownloadsNothing();

private:
    static UpdateDialog *dialogOf(const QWidget &window) { return window.findChild<UpdateDialog *>(); }

    QTemporaryDir m_settingsDir;
};

// --- the gate ----------------------------------------------------------------------------------------------------

void UpdateFlowTests::manualConnectBeforeSlowCheckBlocksRadio() {
    FakeServer server;
    server.m_manifest = manifestJson("0.8.0-uj.13", 13, "raspberry-pi-arm64", ALLOWED_URL); // nothing new
    server.m_manifestDelayMs = 300;
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    flow.setHooks(hooksFor(UpdatePlatform::PiArm64));
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    flow.noteManualConnection();
    QTest::qWait(800);

    QCOMPARE(server.m_manifestRequests, 1);
    QCOMPARE(radio.count(), 0);
    QVERIFY(!dialogOf(window));
}

void UpdateFlowTests::manualConnectBeforeUpdateFoundShowsNoPopup() {
    FakeServer server;
    server.m_manifest = manifestJson("0.8.0-uj.14", 14, "raspberry-pi-arm64", ALLOWED_URL);
    server.m_manifestDelayMs = 300;
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    flow.setHooks(hooksFor(UpdatePlatform::PiArm64));
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    flow.noteManualConnection();
    QTest::qWait(800);

    QCOMPARE(server.m_manifestRequests, 1);
    QCOMPARE(radio.count(), 0);
    QVERIFY(!dialogOf(window));
}

void UpdateFlowTests::noManualConnectAllowsRadioOnce() {
    FakeServer server;
    server.m_manifest = manifestJson("0.8.0-uj.13", 13, "raspberry-pi-arm64", ALLOWED_URL);
    server.m_manifestDelayMs = 100;
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    flow.setHooks(hooksFor(UpdatePlatform::PiArm64));
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    QCOMPARE(radio.count(), 0); // held behind the check
    QTRY_COMPARE(radio.count(), 1);
    QTest::qWait(200);
    QCOMPARE(radio.count(), 1);
    QVERIFY(!dialogOf(window));
}

void UpdateFlowTests::timeoutWithoutManualConnectAllowsRadio() {
    FakeServer server;
    server.m_silent = true;
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    flow.setHooks(hooksFor(UpdatePlatform::PiArm64));
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    QElapsedTimer timer;
    timer.start();
    flow.start();
    QTRY_COMPARE_WITH_TIMEOUT(radio.count(), 1, 5500);
    QVERIFY(timer.elapsed() < 5500);
    QVERIFY(!dialogOf(window));
}

void UpdateFlowTests::noteAfterRadioAllowedIsHarmless() {
    FakeServer server;
    server.m_manifest = manifestJson("0.8.0-uj.13", 13, "raspberry-pi-arm64", ALLOWED_URL);
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    flow.setHooks(hooksFor(UpdatePlatform::PiArm64));
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);
    // As in MainWindow, where the startup connection passes through connectToRadio.
    connect(&flow, &UpdateFlow::startupRadioAllowed, &flow, &UpdateFlow::noteManualConnection);

    flow.start();
    QTRY_COMPARE(radio.count(), 1);
    flow.noteManualConnection();
    QTest::qWait(100);
    QCOMPARE(radio.count(), 1);
}

void UpdateFlowTests::nonUjBuildAllowsRadioImmediately() {
    FakeServer server;
    server.m_manifest = manifestJson("0.8.0-uj.14", 14, "raspberry-pi-arm64", ALLOWED_URL);
    Window window;
    UpdateFlow flow("main-3d4e37b", server.url("/update.json"), &window, nullptr);
    flow.setHooks(hooksFor(UpdatePlatform::PiArm64));
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    QCOMPARE(radio.count(), 1);
    QTest::qWait(200);
    QCOMPARE(server.m_manifestRequests, 0);
    QCOMPARE(radio.count(), 1);
}

void UpdateFlowTests::unsupportedPlatformAllowsRadioImmediately() {
    FakeServer server;
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    flow.setHooks(hooksFor(UpdatePlatform::Unsupported));
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    QCOMPARE(radio.count(), 1);
    QTest::qWait(200);
    QCOMPARE(server.m_manifestRequests, 0);
}

void UpdateFlowTests::manualConnectOnNonUjBuildStillBlocksRadio() {
    FakeServer server;
    Window window;
    UpdateFlow flow("main-3d4e37b", server.url("/update.json"), &window, nullptr);
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.noteManualConnection();
    flow.start();
    QCOMPARE(radio.count(), 0);
}

void UpdateFlowTests::defaultManifestUrlIsTheForksLatestRelease() {
    QCOMPARE(UpdateFlow::defaultManifestUrl(),
             QUrl("https://github.com/w1ujay/QK4-UJ/releases/latest/download/update.json"));
}

// --- offers ------------------------------------------------------------------------------------------------------

void UpdateFlowTests::popupHoldsRadioUntilLater() {
    FakeServer server;
    server.m_manifest = manifestJson("0.8.0-uj.14", 14, "raspberry-pi-arm64", ALLOWED_URL);
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    flow.setHooks(hooksFor(UpdatePlatform::PiArm64));
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    QTRY_VERIFY(dialogOf(window));
    UpdateDialog *dialog = dialogOf(window);
    QVERIFY(dialog->isVisible());
    QCOMPARE(dialog->parent(), &window);
    QVERIFY(shown(dialog, "Download"));
    QTest::qWait(100);
    QCOMPARE(radio.count(), 0);

    QTest::mouseClick(button(dialog, "Later"), Qt::LeftButton);
    QTRY_COMPARE(radio.count(), 1);
    QTRY_VERIFY(!dialogOf(window));
    QCOMPARE(QSettings().value("update/skippedUj", 0).toInt(), 0);
    QTest::qWait(50);
    QCOMPARE(radio.count(), 1);
}

void UpdateFlowTests::skipStoresVersionAndAllowsRadio() {
    FakeServer server;
    server.m_manifest = manifestJson("0.8.0-uj.14", 14, "raspberry-pi-arm64", ALLOWED_URL);
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    flow.setHooks(hooksFor(UpdatePlatform::PiArm64));
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    QTRY_VERIFY(dialogOf(window));
    QTest::mouseClick(button(dialogOf(window), "Skip this version"), Qt::LeftButton);
    QTRY_COMPARE(radio.count(), 1);
    QCOMPARE(QSettings().value("update/skippedUj").toInt(), 14);
}

void UpdateFlowTests::skippedVersionIsNotOffered() {
    QSettings().setValue("update/skippedUj", 14);
    FakeServer server;
    server.m_manifest = manifestJson("0.8.0-uj.14", 14, "raspberry-pi-arm64", ALLOWED_URL);
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    flow.setHooks(hooksFor(UpdatePlatform::PiArm64));
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    QTRY_COMPARE(radio.count(), 1);
    QVERIFY(!dialogOf(window));
}

void UpdateFlowTests::failedVersionIsNotOffered() {
    QSettings().setValue("update/failedUj", QStringList{"14"});
    FakeServer server;
    server.m_manifest = manifestJson("0.8.0-uj.14", 14, "raspberry-pi-arm64", ALLOWED_URL);
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    flow.setHooks(hooksFor(UpdatePlatform::PiArm64));
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    QTRY_COMPARE(radio.count(), 1);
    QVERIFY(!dialogOf(window));
}

void UpdateFlowTests::olderCopyIsNotOfferedWhatCurrentTxtHas() {
    ManagedLayout layout("0.8.0-uj.13", "0.8.0-uj.14\n");
    FakeServer server;
    server.m_manifest = manifestJson("0.8.0-uj.14", 14, "windows-x64", ALLOWED_URL);
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    UpdateFlow::Hooks hooks = managedHooks(layout);
    flow.setHooks(hooks);
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    QTRY_COMPARE(radio.count(), 1);
    QVERIFY(!dialogOf(window));
}

void UpdateFlowTests::mismatchedVersionIsAFailedCheck() {
    FakeServer server;
    server.m_manifest = manifestJson("0.8.0-uj.15", 14, "raspberry-pi-arm64", ALLOWED_URL);
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    flow.setHooks(hooksFor(UpdatePlatform::PiArm64));
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    QTRY_COMPARE(radio.count(), 1);
    QVERIFY(!dialogOf(window));
}

void UpdateFlowTests::disallowedAssetUrlIsAFailedCheck() {
    FakeServer server;
    server.m_manifest =
        manifestJson("0.8.0-uj.14", 14, "raspberry-pi-arm64", "https://example.com/w1ujay/QK4-UJ/releases/download/x");
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    flow.setHooks(hooksFor(UpdatePlatform::PiArm64));
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    QTRY_COMPARE(radio.count(), 1);
    QVERIFY(!dialogOf(window));
}

void UpdateFlowTests::missingAssetIsAFailedCheck() {
    FakeServer server;
    server.m_manifest = manifestJson("0.8.0-uj.14", 14, "windows-x64", ALLOWED_URL);
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    flow.setHooks(hooksFor(UpdatePlatform::PiArm64));
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    QTRY_COMPARE(radio.count(), 1);
    QVERIFY(!dialogOf(window));
}

void UpdateFlowTests::unsafeAssetNameIsAFailedCheck() {
    FakeServer server;
    server.m_manifest =
        manifestJson("0.8.0-uj.14", 14, "raspberry-pi-arm64", ALLOWED_URL, QByteArray("package"), "../../evil.bin");
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    flow.setHooks(hooksFor(UpdatePlatform::PiArm64));
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    QTRY_COMPARE(radio.count(), 1);
    QVERIFY(!dialogOf(window));
}

void UpdateFlowTests::malformedManifestIsAFailedCheck() {
    FakeServer server;
    server.m_manifest = "<html>not json</html>";
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    flow.setHooks(hooksFor(UpdatePlatform::PiArm64));
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    QTRY_COMPARE(radio.count(), 1);
    QVERIFY(!dialogOf(window));
}

// --- startup work ------------------------------------------------------------------------------------------------

void UpdateFlowTests::readinessFileWrittenBesideExeAndVariableCleared() {
    QTemporaryDir dir;
    FakeServer server;
    qputenv("QK4_UPDATE_ATTEMPT", "abc123");
    Window window;
    UpdateFlow flow("0.8.0-uj.14", server.url("/update.json"), &window, nullptr);
    flow.setHooks(hooksFor(UpdatePlatform::Unsupported, dir.filePath("QK4.exe")));

    flow.start();
    QVERIFY(QFileInfo::exists(dir.filePath(".ready-abc123")));
    QVERIFY(!qEnvironmentVariableIsSet("QK4_UPDATE_ATTEMPT"));
}

void UpdateFlowTests::readinessFileWrittenOnNonUjBuild() {
    QTemporaryDir dir;
    FakeServer server;
    qputenv("QK4_UPDATE_ATTEMPT", "abc123");
    Window window;
    UpdateFlow flow("main-3d4e37b", server.url("/update.json"), &window, nullptr);
    flow.setHooks(hooksFor(UpdatePlatform::Windows, dir.filePath("QK4.exe")));

    flow.start();
    QVERIFY(QFileInfo::exists(dir.filePath(".ready-abc123")));
}

void UpdateFlowTests::unsafeAttemptNameWritesNothing() {
    QTemporaryDir dir;
    QVERIFY(QDir(dir.path()).mkdir("app"));
    FakeServer server;
    qputenv("QK4_UPDATE_ATTEMPT", "../escaped");
    Window window;
    UpdateFlow flow("main-3d4e37b", server.url("/update.json"), &window, nullptr);
    flow.setHooks(hooksFor(UpdatePlatform::Unsupported, dir.filePath("app/QK4.exe")));

    flow.start();
    QVERIFY(QDir(dir.filePath("app")).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot));
    QVERIFY(!QFileInfo::exists(dir.filePath(".ready-../escaped")));
    QVERIFY(!qEnvironmentVariableIsSet("QK4_UPDATE_ATTEMPT"));
}

void UpdateFlowTests::noAttemptWritesNothing() {
    QTemporaryDir dir;
    FakeServer server;
    Window window;
    UpdateFlow flow("main-3d4e37b", server.url("/update.json"), &window, nullptr);
    flow.setHooks(hooksFor(UpdatePlatform::Unsupported, dir.filePath("QK4.exe")));

    flow.start();
    QVERIFY(QDir(dir.path()).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot));
}

void UpdateFlowTests::failureNoteRecordedShownOnceAndDeleted() {
    ManagedLayout layout;
    writeFile(layout.root() + "/update-failed.txt", "0.8.0-uj.14\r\n0.8.0-uj.15\n");
    FakeServer server;
    Window window;
    {
        UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
        UpdateFlow::Hooks hooks = managedHooks(layout);
        flow.setHooks(hooks);
        flow.noteManualConnection(); // keeps the check out of this test
        flow.start();

        QStringList recorded = QSettings().value("update/failedUj").toStringList();
        recorded.sort();
        QCOMPARE(recorded, (QStringList{"14", "15"}));
        QVERIFY(!QFileInfo::exists(layout.root() + "/update-failed.txt"));
        QMessageBox *note = window.findChild<QMessageBox *>();
        QVERIFY(note);
        QVERIFY(note->isVisible());
        QVERIFY(note->text().contains("0.8.0-uj.14"));
        delete note;
    }

    UpdateFlow next("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    UpdateFlow::Hooks hooks = managedHooks(layout);
    next.setHooks(hooks);
    next.noteManualConnection();
    next.start();
    QVERIFY(!window.findChild<QMessageBox *>());
}

void UpdateFlowTests::failureNoteAtOrBelowCurrentIsIgnored() {
    ManagedLayout layout("0.8.0-uj.13", "0.8.0-uj.14\n");
    writeFile(layout.root() + "/update-failed.txt", "0.8.0-uj.13\n0.8.0-uj.14\nnonsense\n0.8.0-uj.15\n");
    FakeServer server;
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    UpdateFlow::Hooks hooks = managedHooks(layout);
    flow.setHooks(hooks);
    flow.noteManualConnection();
    flow.start();

    QCOMPARE(QSettings().value("update/failedUj").toStringList(), QStringList{"15"});
    QVERIFY(!QFileInfo::exists(layout.root() + "/update-failed.txt"));
    QMessageBox *note = window.findChild<QMessageBox *>();
    QVERIFY(note);
    QVERIFY(note->text().contains("0.8.0-uj.15"));
    QVERIFY(!note->text().contains("0.8.0-uj.14"));

    // A note holding only committed versions is deleted and says nothing.
    delete note;
    writeFile(layout.root() + "/update-failed.txt", "0.8.0-uj.14\n");
    UpdateFlow second("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    second.setHooks(hooks);
    second.noteManualConnection();
    second.start();
    QVERIFY(!QFileInfo::exists(layout.root() + "/update-failed.txt"));
    QVERIFY(!window.findChild<QMessageBox *>());
    QCOMPARE(QSettings().value("update/failedUj").toStringList(), QStringList{"15"});
}

// Two copies install 15 together: one rolls back and leaves a note, the other starts 15 while current.txt is still 14.
void UpdateFlowTests::failureNoteNamingTheRunningVersionIsIgnored() {
    ManagedLayout layout("0.8.0-uj.15", "0.8.0-uj.14\n");
    writeFile(layout.root() + "/update-failed.txt", "0.8.0-uj.15\n");
    FakeServer server;
    Window window;
    UpdateFlow flow("0.8.0-uj.15", server.url("/update.json"), &window, nullptr);
    UpdateFlow::Hooks hooks = managedHooks(layout);
    flow.setHooks(hooks);
    flow.noteManualConnection();
    flow.start();

    QVERIFY(!QFileInfo::exists(layout.root() + "/update-failed.txt"));
    QVERIFY(QSettings().value("update/failedUj").toStringList().isEmpty());
    QVERIFY(!window.findChild<QMessageBox *>());
}

void UpdateFlowTests::failureNoteAddsToEarlierFailures() {
    QSettings().setValue("update/failedUj", QStringList{"14"});
    ManagedLayout layout;
    writeFile(layout.root() + "/update-failed.txt", "0.8.0-uj.14\n0.8.0-uj.15\n");
    FakeServer server;
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    UpdateFlow::Hooks hooks = managedHooks(layout);
    flow.setHooks(hooks);
    flow.noteManualConnection();
    flow.start();

    QCOMPARE(QSettings().value("update/failedUj").toStringList(), (QStringList{"14", "15"}));
}

// The note must not sit in a nested event loop, or the radio would wait for someone to dismiss it.
void UpdateFlowTests::failureNoteDoesNotDelayTheRadio() {
    ManagedLayout layout;
    writeFile(layout.root() + "/update-failed.txt", "0.8.0-uj.14\n");
    FakeServer server;
    server.m_manifest = manifestJson("0.8.0-uj.13", 13, "windows-x64", ALLOWED_URL);
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    UpdateFlow::Hooks hooks = managedHooks(layout);
    flow.setHooks(hooks);
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    QTRY_COMPARE(radio.count(), 1);
    QMessageBox *note = window.findChild<QMessageBox *>();
    QVERIFY(note);
    QVERIFY(note->isVisible());
    QVERIFY(!note->isModal());
}

void UpdateFlowTests::cleanupRunsWhenOwnVersionIsCurrent() {
    QSettings().setValue("update/failedUj", QStringList{"15", "14"});
    ManagedLayout layout;
    FakeServer server;
    server.m_manifest = manifestJson("0.8.0-uj.13", 13, "windows-x64", ALLOWED_URL);
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    QString program;
    QStringList arguments;
    UpdateFlow::Hooks hooks = managedHooks(layout);
    hooks.startDetached = [&](const QString &p, const QStringList &a) {
        program = p;
        arguments = a;
        return true;
    };
    flow.setHooks(hooks);
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    QCOMPARE(program, layout.root() + "/QK4.exe");
    QCOMPARE(arguments, (QStringList{"--qk4-cleanup", "0.8.0-uj.13", "14,15"}));
    QTRY_COMPARE(radio.count(), 1);
}

void UpdateFlowTests::cleanupSkippedForVersionUnderTest() {
    ManagedLayout layout("0.8.0-uj.14", "0.8.0-uj.13\n");
    FakeServer server;
    server.m_manifest = manifestJson("0.8.0-uj.13", 13, "windows-x64", ALLOWED_URL);
    Window window;
    UpdateFlow flow("0.8.0-uj.14", server.url("/update.json"), &window, nullptr);
    int started = 0;
    UpdateFlow::Hooks hooks = managedHooks(layout);
    hooks.startDetached = [&](const QString &, const QStringList &) {
        ++started;
        return true;
    };
    flow.setHooks(hooks);
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    QTRY_COMPARE(radio.count(), 1);
    QCOMPARE(started, 0);
}

void UpdateFlowTests::cleanupSkippedWhenNotManaged() {
    QTemporaryDir dir;
    FakeServer server;
    server.m_manifest = manifestJson("0.8.0-uj.13", 13, "windows-x64", ALLOWED_URL);
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    int started = 0;
    UpdateFlow::Hooks hooks;
    hooks.platform = UpdatePlatform::Windows;
    hooks.exePath = dir.filePath("QK4.exe");
    hooks.startDetached = [&](const QString &, const QStringList &) {
        ++started;
        return true;
    };
    flow.setHooks(hooks);
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    QTRY_COMPARE(radio.count(), 1);
    QCOMPARE(started, 0);
}

// --- downloading and installing ----------------------------------------------------------------------------------

void UpdateFlowTests::cancelDuringDownloadReturnsToIdleButtons() {
    QTemporaryDir downloads;
    FakeServer server;
    server.m_holdPackage = true;
    server.m_manifest = manifestJson("0.8.0-uj.14", 14, "raspberry-pi-arm64", server.url("/package").toString());
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    UpdateFlow::Hooks hooks;
    hooks.platform = UpdatePlatform::PiArm64;
    hooks.allowedUrl = [](const QUrl &) { return true; };
    hooks.downloadsFolder = downloads.path();
    flow.setHooks(hooks);
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    QTRY_VERIFY(dialogOf(window));
    UpdateDialog *dialog = dialogOf(window);
    QTest::mouseClick(button(dialog, "Download"), Qt::LeftButton);
    QTRY_COMPARE(server.m_packageRequests, 1);
    QVERIFY(shown(dialog, "Cancel"));

    QTest::mouseClick(button(dialog, "Cancel"), Qt::LeftButton);
    QVERIFY(shown(dialog, "Download"));
    QVERIFY(shown(dialog, "Later"));
    QVERIFY(shown(dialog, "Skip this version"));
    QVERIFY(dialog->isVisible());
    QCOMPARE(radio.count(), 0);
    QTRY_VERIFY(QDir(downloads.path()).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot));

    QTest::mouseClick(button(dialog, "Later"), Qt::LeftButton);
    QTRY_COMPARE(radio.count(), 1);
}

void UpdateFlowTests::downloadErrorOffersRetryAndLater() {
    QTemporaryDir downloads;
    FakeServer server;
    const QByteArray good("the real package");
    server.m_package = "damaged package!";
    QCOMPARE(server.m_package.size(), good.size());
    server.m_manifest = manifestJson("0.8.0-uj.14", 14, "raspberry-pi-arm64", server.url("/package").toString(), good);
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    UpdateFlow::Hooks hooks;
    hooks.platform = UpdatePlatform::PiArm64;
    hooks.allowedUrl = [](const QUrl &) { return true; };
    hooks.downloadsFolder = downloads.path();
    flow.setHooks(hooks);
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    QTRY_VERIFY(dialogOf(window));
    UpdateDialog *dialog = dialogOf(window);
    QTest::mouseClick(button(dialog, "Download"), Qt::LeftButton);
    QTRY_VERIFY(shown(dialog, "Retry"));
    QVERIFY(shown(dialog, "Later"));
    QVERIFY(labelText(dialog).contains("checksum"));
    QCOMPARE(radio.count(), 0);

    server.m_package = good;
    QTest::mouseClick(button(dialog, "Retry"), Qt::LeftButton);
    QTRY_VERIFY(labelText(dialog).contains("tar xzf"));
    QCOMPARE(server.m_packageRequests, 2);
    QVERIFY(QFileInfo::exists(downloads.filePath("package.bin")));
}

void UpdateFlowTests::managedInstallClosesWindowWithoutRadio() {
    ManagedLayout layout;
    QTemporaryDir localData;
    FakeServer server;
    server.m_package = "package";
    server.m_manifest = manifestJson("0.8.0-uj.14", 14, "windows-x64", server.url("/package").toString());
    Window window;
    window.show();
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    QStringList launcherArguments;
    QString launcher;
    UpdateFlow::Hooks hooks = managedHooks(layout);
    hooks.allowedUrl = [](const QUrl &) { return true; };
    hooks.localDataFolder = localData.path();
    hooks.installer.extract = [](const QString &, const QString &incoming, const QString &) {
        QFile exe(incoming + "/QK4.exe");
        return exe.open(QIODevice::WriteOnly);
    };
    hooks.installer.startLauncher = [&](const QString &program, const QStringList &arguments,
                                        const QProcessEnvironment &) {
        launcher = program;
        launcherArguments = arguments;
        return true;
    };
    flow.setHooks(hooks);
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);
    QSignalSpy lastWindow(qApp, &QGuiApplication::lastWindowClosed);

    flow.start();
    QTRY_VERIFY(dialogOf(window));
    UpdateDialog *dialog = dialogOf(window);
    QVERIFY(shown(dialog, "Install now"));

    // Exits the same way the app does, with the event loop running.
    QTimer::singleShot(0, button(dialog, "Install now"), &QPushButton::click);
    QTimer::singleShot(10000, qApp, [] { QCoreApplication::exit(99); });
    QCOMPARE(QCoreApplication::exec(), 0);

    QCOMPARE(window.closes, 1);
    QCOMPARE(lastWindow.count(), 1);
    QVERIFY(!window.isVisible());
    QCOMPARE(radio.count(), 0);
    QCOMPARE(launcher, layout.root() + "/QK4.exe");
    QCOMPARE(launcherArguments.mid(0, 2), (QStringList{"--qk4-finish-update", "0.8.0-uj.14"}));
    QVERIFY(QFileInfo::exists(layout.root() + "/versions/0.8.0-uj.14/QK4.exe"));
    QVERIFY(QDir(localData.path()).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot) ||
            QDir(localData.path() + "/update").isEmpty(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot));
}

// Mini View hides the main window. Closing a hidden window does not end the app by itself.
void UpdateFlowTests::quitsEvenWithTheWindowHidden() {
    ManagedLayout layout;
    QTemporaryDir localData;
    FakeServer server;
    server.m_package = "package";
    server.m_manifest = manifestJson("0.8.0-uj.14", 14, "windows-x64", server.url("/package").toString());
    Window window; // never shown
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    UpdateFlow::Hooks hooks = managedHooks(layout);
    hooks.allowedUrl = [](const QUrl &) { return true; };
    hooks.localDataFolder = localData.path();
    hooks.installer.extract = [](const QString &, const QString &incoming, const QString &) {
        QFile exe(incoming + "/QK4.exe");
        return exe.open(QIODevice::WriteOnly);
    };
    hooks.installer.startLauncher = [](const QString &, const QStringList &, const QProcessEnvironment &) {
        return true;
    };
    flow.setHooks(hooks);

    flow.start();
    QTRY_VERIFY(dialogOf(window));
    QTimer::singleShot(0, button(dialogOf(window), "Install now"), &QPushButton::click);
    QTimer::singleShot(5000, qApp, [] { QCoreApplication::exit(99); });
    QCOMPARE(QCoreApplication::exec(), 0);
    QCOMPARE(window.closes, 1);
}

void UpdateFlowTests::cancelIsIgnoredWhileInstalling() {
    ManagedLayout layout;
    QTemporaryDir localData;
    FakeServer server;
    server.m_package = "package";
    server.m_manifest = manifestJson("0.8.0-uj.14", 14, "windows-x64", server.url("/package").toString());
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    std::atomic<bool> extracting{false};
    std::atomic<bool> release{false};
    UpdateFlow::Hooks hooks = managedHooks(layout);
    hooks.allowedUrl = [](const QUrl &) { return true; };
    hooks.localDataFolder = localData.path();
    hooks.installer.extract = [&](const QString &, const QString &, const QString &) {
        extracting = true;
        while (!release)
            QThread::msleep(10);
        return false;
    };
    hooks.installer.retryWindowMs = 100;
    flow.setHooks(hooks);
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    QTRY_VERIFY(dialogOf(window));
    UpdateDialog *dialog = dialogOf(window);
    QTest::mouseClick(button(dialog, "Install now"), Qt::LeftButton);
    QTRY_VERIFY(extracting.load());

    QProgressBar *bar = dialog->findChild<QProgressBar *>();
    QVERIFY(bar->isVisible());
    QCOMPARE(bar->maximum(), 0); // busy bar

    QVERIFY(!shown(dialog, "Cancel")); // nothing to cancel: Esc does nothing either
    QVERIFY(!shown(dialog, "Later"));
    QTest::keyClick(dialog, Qt::Key_Escape);
    QCoreApplication::processEvents();
    QVERIFY(dialog->isVisible());
    QVERIFY(bar->isVisible());
    QVERIFY(!shown(dialog, "Install now"));
    QCOMPARE(radio.count(), 0);

    release = true;
    QTRY_VERIFY(shown(dialog, "Retry")); // the installer's signal still lands
}

void UpdateFlowTests::installFailureShowsErrorAndRetryWorks() {
    ManagedLayout layout;
    QTemporaryDir localData;
    FakeServer server;
    server.m_package = "package";
    server.m_manifest = manifestJson("0.8.0-uj.14", 14, "windows-x64", server.url("/package").toString());
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    int launcherStarts = 0;
    UpdateFlow::Hooks hooks = managedHooks(layout);
    hooks.allowedUrl = [](const QUrl &) { return true; };
    hooks.localDataFolder = localData.path();
    hooks.installer.extract = [](const QString &, const QString &incoming, const QString &) {
        QFile exe(incoming + "/QK4.exe");
        return exe.open(QIODevice::WriteOnly);
    };
    hooks.installer.startLauncher = [&](const QString &, const QStringList &, const QProcessEnvironment &) {
        ++launcherStarts;
        return false;
    };
    flow.setHooks(hooks);
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    QTRY_VERIFY(dialogOf(window));
    UpdateDialog *dialog = dialogOf(window);
    QTest::mouseClick(button(dialog, "Install now"), Qt::LeftButton);
    QTRY_VERIFY(shown(dialog, "Retry"));
    QVERIFY(labelText(dialog).contains("launcher"));
    QCOMPARE(window.closes, 0);

    QTest::mouseClick(button(dialog, "Retry"), Qt::LeftButton);
    QTRY_COMPARE(launcherStarts, 2);
    QTRY_VERIFY(shown(dialog, "Retry"));
    QCOMPARE(server.m_packageRequests, 2); // the zip does not outlive an attempt

    QTest::mouseClick(button(dialog, "Later"), Qt::LeftButton);
    QTRY_COMPARE(radio.count(), 1);
}

void UpdateFlowTests::anotherCopyUpdatedBeforeDownload() {
    ManagedLayout layout;
    QTemporaryDir localData;
    FakeServer server;
    server.m_package = "package";
    server.m_manifest = manifestJson("0.8.0-uj.14", 14, "windows-x64", server.url("/package").toString());
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    UpdateFlow::Hooks hooks = managedHooks(layout);
    hooks.allowedUrl = [](const QUrl &) { return true; };
    hooks.localDataFolder = localData.path();
    flow.setHooks(hooks);
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    QTRY_VERIFY(dialogOf(window));
    UpdateDialog *dialog = dialogOf(window);
    writeFile(layout.root() + "/current.txt", "0.8.0-uj.14\n");

    QTest::mouseClick(button(dialog, "Install now"), Qt::LeftButton);
    QVERIFY(labelText(dialog).contains("reopen", Qt::CaseInsensitive));
    QTest::qWait(100);
    QCOMPARE(server.m_packageRequests, 0);
    QCOMPARE(window.closes, 0);

    QTest::mouseClick(button(dialog, "Later"), Qt::LeftButton);
    QTRY_COMPARE(radio.count(), 1);
}

void UpdateFlowTests::anotherCopyUpdatedDuringInstall() {
    ManagedLayout layout;
    QTemporaryDir localData;
    FakeServer server;
    server.m_holdPackage = false;
    server.m_package = "package";
    server.m_manifest = manifestJson("0.8.0-uj.14", 14, "windows-x64", server.url("/package").toString());
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    UpdateFlow::Hooks hooks = managedHooks(layout);
    hooks.allowedUrl = [](const QUrl &) { return true; };
    hooks.localDataFolder = localData.path();
    flow.setHooks(hooks);

    flow.start();
    QTRY_VERIFY(dialogOf(window));
    UpdateDialog *dialog = dialogOf(window);
    QTest::mouseClick(button(dialog, "Install now"), Qt::LeftButton);
    // The zip lands while this copy waits, so moving current.txt now is seen by the installer's own check.
    writeFile(layout.root() + "/current.txt", "0.8.0-uj.14\n");
    QTRY_VERIFY(labelText(dialog).contains("reopen", Qt::CaseInsensitive));
    QCOMPARE(window.closes, 0);
}

void UpdateFlowTests::unmanagedWindowsSavesToDownloads() {
    QTemporaryDir dir;
    QTemporaryDir downloads;
    FakeServer server;
    server.m_package = "package";
    server.m_manifest = manifestJson("0.8.0-uj.14", 14, "windows-x64", server.url("/package").toString());
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    UpdateFlow::Hooks hooks;
    hooks.platform = UpdatePlatform::Windows;
    hooks.exePath = dir.filePath("QK4.exe");
    hooks.allowedUrl = [](const QUrl &) { return true; };
    hooks.downloadsFolder = downloads.path();
    flow.setHooks(hooks);
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    QTRY_VERIFY(dialogOf(window));
    UpdateDialog *dialog = dialogOf(window);
    QVERIFY(shown(dialog, "Download")); // never Install now
    QTest::mouseClick(button(dialog, "Download"), Qt::LeftButton);
    QTRY_VERIFY(labelText(dialog).contains("unzip", Qt::CaseInsensitive));
    QVERIFY(QFileInfo::exists(downloads.filePath("package.bin")));
    QVERIFY(labelText(dialog).contains(QDir::toNativeSeparators(downloads.filePath("package.bin"))));
    QCOMPARE(window.closes, 0);

    QTest::mouseClick(button(dialog, "Later"), Qt::LeftButton);
    QTRY_COMPARE(radio.count(), 1);
}

void UpdateFlowTests::downloadKeepsAnExistingFile() {
    QTemporaryDir downloads;
    writeFile(downloads.filePath("package.bin"), "the person's own file");
    FakeServer server;
    server.m_package = "package";
    server.m_manifest = manifestJson("0.8.0-uj.14", 14, "raspberry-pi-arm64", server.url("/package").toString());
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    UpdateFlow::Hooks hooks;
    hooks.platform = UpdatePlatform::PiArm64;
    hooks.allowedUrl = [](const QUrl &) { return true; };
    hooks.downloadsFolder = downloads.path();
    flow.setHooks(hooks);

    flow.start();
    QTRY_VERIFY(dialogOf(window));
    UpdateDialog *dialog = dialogOf(window);
    QTest::mouseClick(button(dialog, "Download"), Qt::LeftButton);
    QTRY_VERIFY(labelText(dialog).contains("tar xzf"));
    QFile mine(downloads.filePath("package.bin"));
    QVERIFY(mine.open(QIODevice::ReadOnly));
    QCOMPARE(mine.readAll(), QByteArray("the person's own file"));
    QCOMPARE(QDir(downloads.path()).entryList(QDir::Files).size(), 2);
}

void UpdateFlowTests::piSavesToDownloadsAndShowsSteps() {
    QTemporaryDir parent;
    const QString folder = parent.filePath("with space's");
    FakeServer server;
    server.m_package = "package";
    server.m_manifest = manifestJson("0.8.0-uj.14", 14, "raspberry-pi-arm64", server.url("/package").toString());
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    UpdateFlow::Hooks hooks;
    hooks.platform = UpdatePlatform::PiArm64;
    hooks.allowedUrl = [](const QUrl &) { return true; };
    hooks.downloadsFolder = folder; // not created yet
    flow.setHooks(hooks);

    flow.start();
    QTRY_VERIFY(dialogOf(window));
    UpdateDialog *dialog = dialogOf(window);
    QTest::mouseClick(button(dialog, "Download"), Qt::LeftButton);
    QTRY_VERIFY(labelText(dialog).contains("tar xzf"));
    QVERIFY(QFileInfo::exists(folder + "/package.bin"));
    QVERIFY(labelText(dialog).contains("'\\''")); // the path is shell-quoted
    QVERIFY(shown(dialog, "Copy"));
    QVERIFY(!shown(dialog, "Skip this version"));
    QVERIFY(dialog->isVisible());
    QCOMPARE(window.closes, 0);
}

void UpdateFlowTests::flatpakUsesTheSaveDialog() {
    QTemporaryDir downloads;
    FakeServer server;
    server.m_package = "package";
    server.m_manifest = manifestJson("0.8.0-uj.14", 14, "linux-flatpak-x86_64", server.url("/package").toString());
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    const QString chosen = downloads.filePath("chosen.flatpak");
    QString suggested;
    UpdateFlow::Hooks hooks;
    hooks.platform = UpdatePlatform::Flatpak;
    hooks.allowedUrl = [](const QUrl &) { return true; };
    hooks.chooseSavePath = [&](const QString &path) {
        suggested = path;
        return chosen;
    };
    flow.setHooks(hooks);

    flow.start();
    QTRY_VERIFY(dialogOf(window));
    UpdateDialog *dialog = dialogOf(window);
    QTest::mouseClick(button(dialog, "Download"), Qt::LeftButton);
    QTRY_VERIFY(labelText(dialog).contains("flatpak install --user"));
    QVERIFY(suggested.endsWith("package.bin"));
    QVERIFY(QFileInfo::exists(chosen));
    QVERIFY(shown(dialog, "Copy"));
}

void UpdateFlowTests::flatpakCancelledSaveStaysOnPopup() {
    FakeServer server;
    server.m_manifest = manifestJson("0.8.0-uj.14", 14, "linux-flatpak-x86_64", server.url("/package").toString());
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    UpdateFlow::Hooks hooks;
    hooks.platform = UpdatePlatform::Flatpak;
    hooks.allowedUrl = [](const QUrl &) { return true; };
    hooks.chooseSavePath = [](const QString &) { return QString(); };
    flow.setHooks(hooks);

    flow.start();
    QTRY_VERIFY(dialogOf(window));
    UpdateDialog *dialog = dialogOf(window);
    QTest::mouseClick(button(dialog, "Download"), Qt::LeftButton);
    QVERIFY(shown(dialog, "Download"));
    QVERIFY(shown(dialog, "Later"));
    QTest::qWait(100);
    QCOMPARE(server.m_packageRequests, 0);
}

void UpdateFlowTests::otherLinuxShowsReleasePageAndDownloadsNothing() {
    QTemporaryDir downloads;
    FakeServer server;
    server.m_manifest = manifestJson("0.8.0-uj.14", 14, "windows-x64", server.url("/package").toString());
    Window window;
    UpdateFlow flow("0.8.0-uj.13", server.url("/update.json"), &window, nullptr);
    UpdateFlow::Hooks hooks;
    hooks.platform = UpdatePlatform::OtherLinux;
    hooks.downloadsFolder = downloads.path();
    flow.setHooks(hooks);
    QSignalSpy radio(&flow, &UpdateFlow::startupRadioAllowed);

    flow.start();
    QTRY_VERIFY(dialogOf(window)); // no asset to check, and none is needed
    UpdateDialog *dialog = dialogOf(window);
    QTest::mouseClick(button(dialog, "Download"), Qt::LeftButton);
    QVERIFY(labelText(dialog).contains("https://github.com/w1ujay/QK4-UJ/releases/tag/uj.14"));
    QVERIFY(shown(dialog, "Copy"));
    QTest::qWait(100);
    QCOMPARE(server.m_packageRequests, 0);
    QVERIFY(QDir(downloads.path()).isEmpty(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot));

    QTest::mouseClick(button(dialog, "Later"), Qt::LeftButton);
    QTRY_COMPARE(radio.count(), 1);
}

QTEST_MAIN(UpdateFlowTests)
#include "test_updateflow.moc"
