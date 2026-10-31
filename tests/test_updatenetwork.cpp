#include <QtTest>

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QHostAddress>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

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
    UpdateDownloader d(assetFor(srv, srv.m_body), dest, nullptr);
    QSignalSpy ok(&d, &UpdateDownloader::succeeded);
    QSignalSpy bad(&d, &UpdateDownloader::failed);
    QSignalSpy prog(&d, &UpdateDownloader::progress);
    d.start();
    QTRY_COMPARE_WITH_TIMEOUT(ok.count() + bad.count(), 1, 15000);
    QCOMPARE(bad.count(), 0);
    QCOMPARE(ok.at(0).at(0).toString(), dest);
    QVERIFY(!QFile::exists(dest + ".part"));
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
    UpdateDownloader d(a, dest, nullptr);
    QSignalSpy ok(&d, &UpdateDownloader::succeeded);
    QSignalSpy bad(&d, &UpdateDownloader::failed);
    d.start();
    QTRY_COMPARE_WITH_TIMEOUT(ok.count() + bad.count(), 1, 15000);
    QCOMPARE(bad.count(), 1);
    QVERIFY(!QFile::exists(dest));
    QVERIFY(!QFile::exists(dest + ".part"));
}

void TestUpdateNetwork::downloaderRejectsWrongSize() {
    FakeServer srv;
    srv.m_type = "application/zip";
    srv.m_body = payload();
    QTemporaryDir dir;
    const QString dest = dir.filePath("QK4.zip");
    UpdateAsset a = assetFor(srv, srv.m_body);
    a.size += 5; // manifest claims more than the server sends; hash would also differ
    UpdateDownloader d(a, dest, nullptr);
    QSignalSpy ok(&d, &UpdateDownloader::succeeded);
    QSignalSpy bad(&d, &UpdateDownloader::failed);
    d.start();
    QTRY_COMPARE_WITH_TIMEOUT(ok.count() + bad.count(), 1, 15000);
    QCOMPARE(bad.count(), 1);
    QVERIFY(bad.at(0).at(0).toString().contains("size", Qt::CaseInsensitive));
    QVERIFY(!QFile::exists(dest));
    QVERIFY(!QFile::exists(dest + ".part"));
}

void TestUpdateNetwork::downloaderCancelDeletesFile() {
    FakeServer srv;
    srv.m_type = "application/zip";
    srv.m_body = payload();
    QTemporaryDir dir;
    const QString dest = dir.filePath("QK4.zip");
    UpdateDownloader d(assetFor(srv, srv.m_body), dest, nullptr);
    QSignalSpy ok(&d, &UpdateDownloader::succeeded);
    QSignalSpy bad(&d, &UpdateDownloader::failed);
    d.start();
    d.cancel(); // immediately: the transfer cannot have completed
    QTest::qWait(500);
    QCOMPARE(ok.count(), 0);
    QCOMPARE(bad.count(), 0);
    QVERIFY(!QFile::exists(dest));
    QVERIFY(!QFile::exists(dest + ".part"));
}

void TestUpdateNetwork::downloaderReportsDiskError() {
    FakeServer srv;
    srv.m_type = "application/zip";
    srv.m_body = payload();
    UpdateDownloader d(assetFor(srv, srv.m_body), "/nonexistent-dir-qk4/sub/QK4.zip", nullptr);
    QSignalSpy ok(&d, &UpdateDownloader::succeeded);
    QSignalSpy bad(&d, &UpdateDownloader::failed);
    d.start();
    QTRY_COMPARE_WITH_TIMEOUT(ok.count() + bad.count(), 1, 15000);
    QCOMPARE(bad.count(), 1);
}

// A small body stays in QFile's write buffer until flush, so only a flush check sees a full disk. /dev/full fails
// every flush with ENOSPC; the .part path is a symlink to it. Linux only.
void TestUpdateNetwork::downloaderFailsWhenLastBytesDoNotReachDisk() {
#ifndef Q_OS_LINUX
    QSKIP("needs /dev/full");
#else
    if (!QFileInfo::exists("/dev/full"))
        QSKIP("needs /dev/full");
    FakeServer srv;
    srv.m_type = "application/zip";
    srv.m_body = QByteArray(100, 'q'); // under the 16 KB write buffer
    QTemporaryDir dir;
    const QString dest = dir.filePath("QK4.zip");
    QVERIFY(QFile::link("/dev/full", dest + ".part"));
    UpdateDownloader d(assetFor(srv, srv.m_body), dest, nullptr);
    QSignalSpy ok(&d, &UpdateDownloader::succeeded);
    QSignalSpy bad(&d, &UpdateDownloader::failed);
    d.start();
    QTRY_COMPARE_WITH_TIMEOUT(ok.count() + bad.count(), 1, 15000);
    QCOMPARE(ok.count(), 0);
    QCOMPARE(bad.count(), 1);
    QVERIFY(!QFile::exists(dest));
#endif
}

QTEST_MAIN(TestUpdateNetwork)
#include "test_updatenetwork.moc"
