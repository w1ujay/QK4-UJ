#include <QtTest>

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QScopeGuard>
#include <QTemporaryDir>

#ifdef Q_OS_LINUX
#include <csignal>
#include <sys/resource.h>
#endif

#include "update/updatechecker.h"
#include "update/updatedownloader.h"
#include "update/updatemanifest.h"

Q_DECLARE_METATYPE(std::optional<UpdateManifest>)

// Minimal HTTP server: answers every request with the configured status/body, or never answers.
class FakeServer : public QObject {
public:
    explicit FakeServer(QObject *parent = nullptr) : QObject(parent) {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = m_server.nextPendingConnection()) {
                m_sockets.append(s);
                connect(s, &QTcpSocket::readyRead, this, [this, s] {
                    m_buffers[s] += s->readAll();
                    if (!m_buffers[s].contains("\r\n\r\n") || m_silent)
                        return;
                    m_buffers[s].clear();
                    const QByteArray head =
                        "HTTP/1.1 " + QByteArray::number(m_status) + " X\r\nContent-Type: " + m_type +
                        "\r\nConnection: close\r\nContent-Length: " + QByteArray::number(m_body.size()) + "\r\n\r\n";
                    s->write(head + m_body);
                    s->disconnectFromHost();
                });
            }
        });
        QVERIFY(m_server.listen(QHostAddress::LocalHost));
    }
    QUrl url(const QString &path = "/update.json") const {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(path));
    }
    int m_status = 200;
    QByteArray m_type = "application/json";
    QByteArray m_body;
    bool m_silent = false;

private:
    QTcpServer m_server;
    QList<QTcpSocket *> m_sockets;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

class TestUpdateNetwork : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { qRegisterMetaType<std::optional<UpdateManifest>>(); }
    void checkerReturnsManifest();
    void checkerGivesNullOnHtml200();
    void checkerGivesNullOn404();
    void checkerGivesNullAfterFiveSeconds();
    void checkerEmitsFinishedOnce();
    void downloaderVerifiesHash();
    void downloaderRejectsWrongHash();
    void downloaderRejectsWrongSize();
    void downloaderCancelDeletesFile();
    void downloaderReportsDiskError();
    void downloaderFailsWhenLastBytesDoNotReachDisk();
    void cancelledDownloadLeavesAnotherCopysDownloadAlone();
    void concurrentDownloadsEachKeepTheirOwnFile();
    void keepNeverTouchesAnExistingFile();
    void replaceOverwritesAnExistingFile();

private:
    static QByteArray manifestJson() {
        return R"({"schema":1,"version":"0.8.0-uj.13","uj":13,"date":"2026-11-02",)"
               R"("assets":{"windows-x64":{"name":"QK4.zip","url":"https://example.com/QK4.zip",)"
               R"("size":10,"sha256":"abc123"}},)"
               R"("history":[{"uj":13,"version":"0.8.0-uj.13","date":"2026-11-02",)"
               R"("added":["a"],"fixed":[],"changed":[]}]})";
    }
    static UpdateAsset assetFor(const FakeServer &srv, const QByteArray &body) {
        UpdateAsset a;
        a.name = "QK4.zip";
        a.url = srv.url("/QK4.zip");
        a.size = body.size();
        a.sha256Hex = QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex();
        return a;
    }
    static QByteArray payload() { return QByteArray(300000, 'q') + "tail"; }
    static QStringList partFiles(const QTemporaryDir &dir) {
        return QDir(dir.path()).entryList({"*.part"}, QDir::Files | QDir::Hidden);
    }
    static QByteArray contentsOf(const QString &path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }
};

void TestUpdateNetwork::checkerReturnsManifest() {
    FakeServer srv;
    srv.m_body = manifestJson();
    UpdateChecker c(srv.url(), nullptr);
    QSignalSpy spy(&c, &UpdateChecker::finished);
    c.start();
    QVERIFY(spy.wait(10000));
    const auto m = spy.at(0).at(0).value<std::optional<UpdateManifest>>();
    QVERIFY(m.has_value());
    QCOMPARE(m->uj, 13);
}

void TestUpdateNetwork::checkerGivesNullOnHtml200() {
    FakeServer srv;
    srv.m_type = "text/html";
    srv.m_body = "<html><body>Sign in to GitHub</body></html>";
    UpdateChecker c(srv.url(), nullptr);
    QSignalSpy spy(&c, &UpdateChecker::finished);
    c.start();
    QVERIFY(spy.wait(10000));
    QVERIFY(!spy.at(0).at(0).value<std::optional<UpdateManifest>>().has_value());
}

void TestUpdateNetwork::checkerGivesNullOn404() {
    FakeServer srv;
    srv.m_status = 404;
    srv.m_body = manifestJson(); // even a valid body must not count on a 404
    UpdateChecker c(srv.url(), nullptr);
    QSignalSpy spy(&c, &UpdateChecker::finished);
    c.start();
    QVERIFY(spy.wait(10000));
    QVERIFY(!spy.at(0).at(0).value<std::optional<UpdateManifest>>().has_value());
}

void TestUpdateNetwork::checkerGivesNullAfterFiveSeconds() {
    FakeServer srv;
    srv.m_silent = true;
    UpdateChecker c(srv.url(), nullptr);
    QSignalSpy spy(&c, &UpdateChecker::finished);
    QElapsedTimer t;
    t.start();
    c.start();
    // Generous wait so a loaded machine cannot flake; the lower bound proves the 5 s limit is honoured
    // and the result is not an early failure.
    QVERIFY(spy.wait(30000));
    QVERIFY2(t.elapsed() >= 4500, qPrintable(QString::number(t.elapsed())));
    QVERIFY(!spy.at(0).at(0).value<std::optional<UpdateManifest>>().has_value());
}

void TestUpdateNetwork::checkerEmitsFinishedOnce() {
    FakeServer srv;
    srv.m_status = 500;
    UpdateChecker c(srv.url(), nullptr);
    QSignalSpy spy(&c, &UpdateChecker::finished);
    c.start();
    QVERIFY(spy.wait(10000));
    QTest::qWait(300); // let any late timer or reply signal fire
    QCOMPARE(spy.count(), 1);
}

void TestUpdateNetwork::downloaderVerifiesHash() {
    FakeServer srv;
    srv.m_type = "application/zip";
    srv.m_body = payload();
    QTemporaryDir dir;
    const QString dest = dir.filePath("QK4.zip");
    UpdateDownloader d(assetFor(srv, srv.m_body), dest, UpdateDownloader::ExistingFile::Replace, nullptr);
    QSignalSpy ok(&d, &UpdateDownloader::succeeded);
    QSignalSpy bad(&d, &UpdateDownloader::failed);
    QSignalSpy prog(&d, &UpdateDownloader::progress);
    d.start();
    QTRY_COMPARE_WITH_TIMEOUT(ok.count() + bad.count(), 1, 15000);
    QCOMPARE(bad.count(), 0);
    QCOMPARE(ok.at(0).at(0).toString(), dest);
    QVERIFY(partFiles(dir).isEmpty());
    QFile f(dest);
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), srv.m_body);
    QVERIFY(prog.count() >= 1);
}

void TestUpdateNetwork::downloaderRejectsWrongHash() {
    FakeServer srv;
    srv.m_type = "application/zip";
    srv.m_body = payload();
    QTemporaryDir dir;
    const QString dest = dir.filePath("QK4.zip");
    UpdateAsset a = assetFor(srv, srv.m_body);
    a.sha256Hex = QByteArray(64, '0');
    UpdateDownloader d(a, dest, UpdateDownloader::ExistingFile::Replace, nullptr);
    QSignalSpy ok(&d, &UpdateDownloader::succeeded);
    QSignalSpy bad(&d, &UpdateDownloader::failed);
    d.start();
    QTRY_COMPARE_WITH_TIMEOUT(ok.count() + bad.count(), 1, 15000);
    QCOMPARE(bad.count(), 1);
    QVERIFY(!QFile::exists(dest));
    QVERIFY(partFiles(dir).isEmpty());
}

void TestUpdateNetwork::downloaderRejectsWrongSize() {
    FakeServer srv;
    srv.m_type = "application/zip";
    srv.m_body = payload();
    QTemporaryDir dir;
    const QString dest = dir.filePath("QK4.zip");
    UpdateAsset a = assetFor(srv, srv.m_body);
    a.size += 5; // manifest claims more than the server sends; hash would also differ
    UpdateDownloader d(a, dest, UpdateDownloader::ExistingFile::Replace, nullptr);
    QSignalSpy ok(&d, &UpdateDownloader::succeeded);
    QSignalSpy bad(&d, &UpdateDownloader::failed);
    d.start();
    QTRY_COMPARE_WITH_TIMEOUT(ok.count() + bad.count(), 1, 15000);
    QCOMPARE(bad.count(), 1);
    QVERIFY(bad.at(0).at(0).toString().contains("size", Qt::CaseInsensitive));
    QVERIFY(!QFile::exists(dest));
    QVERIFY(partFiles(dir).isEmpty());
}

void TestUpdateNetwork::downloaderCancelDeletesFile() {
    FakeServer srv;
    srv.m_type = "application/zip";
    srv.m_body = payload();
    QTemporaryDir dir;
    const QString dest = dir.filePath("QK4.zip");
    UpdateDownloader d(assetFor(srv, srv.m_body), dest, UpdateDownloader::ExistingFile::Replace, nullptr);
    QSignalSpy ok(&d, &UpdateDownloader::succeeded);
    QSignalSpy bad(&d, &UpdateDownloader::failed);
    d.start();
    d.cancel(); // immediately: the transfer cannot have completed
    QTest::qWait(500);
    QCOMPARE(ok.count(), 0);
    QCOMPARE(bad.count(), 0);
    QVERIFY(!QFile::exists(dest));
    QVERIFY(partFiles(dir).isEmpty());
}

void TestUpdateNetwork::downloaderReportsDiskError() {
    FakeServer srv;
    srv.m_type = "application/zip";
    srv.m_body = payload();
    UpdateDownloader d(assetFor(srv, srv.m_body), "/nonexistent-dir-qk4/sub/QK4.zip",
                       UpdateDownloader::ExistingFile::Replace, nullptr);
    QSignalSpy ok(&d, &UpdateDownloader::succeeded);
    QSignalSpy bad(&d, &UpdateDownloader::failed);
    d.start();
    QTRY_COMPARE_WITH_TIMEOUT(ok.count() + bad.count(), 1, 15000);
    QCOMPARE(bad.count(), 1);
}

// A small body stays in QFile's write buffer until flush, so only a flush check sees a full disk. A file size limit of
// zero fails every flush with EFBIG, as a full disk does with ENOSPC. Linux only.
void TestUpdateNetwork::downloaderFailsWhenLastBytesDoNotReachDisk() {
#ifndef Q_OS_LINUX
    QSKIP("needs a file size limit");
#else
    FakeServer srv;
    srv.m_type = "application/zip";
    srv.m_body = QByteArray(100, 'q'); // under the 16 KB write buffer
    QTemporaryDir dir;
    const QString dest = dir.filePath("QK4.zip");
    UpdateDownloader d(assetFor(srv, srv.m_body), dest, UpdateDownloader::ExistingFile::Replace, nullptr);
    QSignalSpy ok(&d, &UpdateDownloader::succeeded);
    QSignalSpy bad(&d, &UpdateDownloader::failed);

    rlimit normal{};
    QCOMPARE(getrlimit(RLIMIT_FSIZE, &normal), 0);
    auto oldHandler = std::signal(SIGXFSZ, SIG_IGN);
    rlimit none = normal;
    none.rlim_cur = 0;
    QCOMPARE(setrlimit(RLIMIT_FSIZE, &none), 0);
    {
        // WHY a guard: a failing macro below must not leave the limit on for the rest of the run.
        const auto restore = qScopeGuard([&] {
            setrlimit(RLIMIT_FSIZE, &normal);
            std::signal(SIGXFSZ, oldHandler);
        });
        d.start();
        QTRY_COMPARE_WITH_TIMEOUT(ok.count() + bad.count(), 1, 15000);
    }
    QCOMPARE(ok.count(), 0);
    QCOMPARE(bad.count(), 1);
    QVERIFY(!QFile::exists(dest));
    QVERIFY(partFiles(dir).isEmpty());
#endif
}

void TestUpdateNetwork::cancelledDownloadLeavesAnotherCopysDownloadAlone() {
    FakeServer srv;
    srv.m_type = "application/zip";
    srv.m_body = payload();
    QTemporaryDir dir;
    const QString dest = dir.filePath("QK4.zip");
    const UpdateAsset asset = assetFor(srv, srv.m_body);
    UpdateDownloader first(asset, dest, UpdateDownloader::ExistingFile::Keep, nullptr);
    UpdateDownloader second(asset, dest, UpdateDownloader::ExistingFile::Keep, nullptr);
    QSignalSpy ok(&first, &UpdateDownloader::succeeded);
    QSignalSpy bad(&first, &UpdateDownloader::failed);
    first.start();
    second.start();
    second.cancel();
    QTRY_COMPARE_WITH_TIMEOUT(ok.count() + bad.count(), 1, 15000);
    QCOMPARE(bad.count(), 0);
    QCOMPARE(ok.at(0).at(0).toString(), dest);
    QCOMPARE(contentsOf(dest), srv.m_body);
    QVERIFY(partFiles(dir).isEmpty());
}

void TestUpdateNetwork::concurrentDownloadsEachKeepTheirOwnFile() {
    FakeServer srv;
    srv.m_type = "application/zip";
    srv.m_body = payload();
    QTemporaryDir dir;
    const QString dest = dir.filePath("QK4.zip");
    const UpdateAsset asset = assetFor(srv, srv.m_body);
    UpdateDownloader first(asset, dest, UpdateDownloader::ExistingFile::Keep, nullptr);
    UpdateDownloader second(asset, dest, UpdateDownloader::ExistingFile::Keep, nullptr);
    QSignalSpy ok1(&first, &UpdateDownloader::succeeded);
    QSignalSpy ok2(&second, &UpdateDownloader::succeeded);
    QSignalSpy bad1(&first, &UpdateDownloader::failed);
    QSignalSpy bad2(&second, &UpdateDownloader::failed);
    first.start();
    second.start();
    QTRY_COMPARE_WITH_TIMEOUT(ok1.count() + bad1.count() + ok2.count() + bad2.count(), 2, 15000);
    QCOMPARE(bad1.count() + bad2.count(), 0);
    const QString path1 = ok1.at(0).at(0).toString();
    const QString path2 = ok2.at(0).at(0).toString();
    QVERIFY(path1 != path2);
    QCOMPARE(contentsOf(path1), srv.m_body);
    QCOMPARE(contentsOf(path2), srv.m_body);
    QVERIFY(partFiles(dir).isEmpty());
}

void TestUpdateNetwork::keepNeverTouchesAnExistingFile() {
    FakeServer srv;
    srv.m_type = "application/zip";
    srv.m_body = payload();
    QTemporaryDir dir;
    const QString dest = dir.filePath("QK4.zip");
    QFile existing(dest);
    QVERIFY(existing.open(QIODevice::WriteOnly));
    existing.write("mine");
    existing.close();
    UpdateDownloader d(assetFor(srv, srv.m_body), dest, UpdateDownloader::ExistingFile::Keep, nullptr);
    QSignalSpy ok(&d, &UpdateDownloader::succeeded);
    QSignalSpy bad(&d, &UpdateDownloader::failed);
    d.start();
    QTRY_COMPARE_WITH_TIMEOUT(ok.count() + bad.count(), 1, 15000);
    QCOMPARE(bad.count(), 0);
    QCOMPARE(ok.at(0).at(0).toString(), dir.filePath("2-QK4.zip"));
    QCOMPARE(contentsOf(dest), QByteArray("mine"));
    QCOMPARE(contentsOf(dir.filePath("2-QK4.zip")), srv.m_body);
}

void TestUpdateNetwork::replaceOverwritesAnExistingFile() {
    FakeServer srv;
    srv.m_type = "application/zip";
    srv.m_body = payload();
    QTemporaryDir dir;
    const QString dest = dir.filePath("QK4.zip");
    QFile existing(dest);
    QVERIFY(existing.open(QIODevice::WriteOnly));
    existing.write("old");
    existing.close();
    UpdateDownloader d(assetFor(srv, srv.m_body), dest, UpdateDownloader::ExistingFile::Replace, nullptr);
    QSignalSpy ok(&d, &UpdateDownloader::succeeded);
    QSignalSpy bad(&d, &UpdateDownloader::failed);
    d.start();
    QTRY_COMPARE_WITH_TIMEOUT(ok.count() + bad.count(), 1, 15000);
    QCOMPARE(bad.count(), 0);
    QCOMPARE(ok.at(0).at(0).toString(), dest);
    QCOMPARE(contentsOf(dest), srv.m_body);
}

QTEST_MAIN(TestUpdateNetwork)
#include "test_updatenetwork.moc"
