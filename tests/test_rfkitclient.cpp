#include <QtTest>

#include <QHash>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>

#include "network/rfkitclient.h"

// A minimal HTTP/1.1 server standing in for the RFKit REST API. Each path answers with a canned
// JSON body; a held path parks its requests until release() so a test can deliver them late.
class FakeRfkitServer : public QObject {
    Q_OBJECT

public:
    FakeRfkitServer() {
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                socket->setParent(this);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { onReadyRead(socket); });
            }
        });
        QVERIFY(m_server.listen(QHostAddress::LocalHost));
    }

    quint16 port() const { return m_server.serverPort(); }
    void setResponse(const QString &path, const QByteArray &body) { m_responses[path] = body; }
    void hold(const QString &path) { m_held.insert(path); }
    int heldCount() const { return m_parked.size(); }

    void release() {
        m_held.clear();
        const auto parked = m_parked;
        m_parked.clear();
        for (const auto &p : parked)
            if (p.first)
                respond(p.first, p.second);
    }

private:
    void onReadyRead(QTcpSocket *socket) {
        QByteArray &buf = m_buffers[socket];
        buf += socket->readAll();
        const int end = buf.indexOf("\r\n\r\n");
        if (end < 0)
            return;
        const QList<QByteArray> requestLine = buf.left(buf.indexOf("\r\n")).split(' ');
        buf.clear();
        const QString path = requestLine.value(1);
        if (m_held.contains(path))
            m_parked.append({QPointer<QTcpSocket>(socket), path});
        else
            respond(socket, path);
    }

    void respond(QTcpSocket *socket, const QString &path) {
        const QByteArray body = m_responses.value(path, QByteArrayLiteral("{}"));
        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
                      QByteArray::number(body.size()) + "\r\n\r\n" + body);
        socket->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QString, QByteArray> m_responses;
    QSet<QString> m_held;
    QHash<QTcpSocket *, QByteArray> m_buffers;
    QList<QPair<QPointer<QTcpSocket>, QString>> m_parked;
};

class TestRfkitClient : public QObject {
    Q_OBJECT

private slots:
    // Reconnecting elsewhere must not let the previous host's /info reply complete the new attempt.
    void aStaleInfoReplyDoesNotConnectANewAttempt() {
        FakeRfkitServer oldHost;
        oldHost.hold("/info");
        FakeRfkitServer newHost;
        newHost.hold("/info");

        RFKitClient client;
        client.connectToHost("127.0.0.1", oldHost.port());
        QTRY_COMPARE(oldHost.heldCount(), 1);

        client.connectToHost("127.0.0.1", newHost.port());
        QTRY_COMPARE(newHost.heldCount(), 1);

        oldHost.release();
        QTest::qWait(200);
        QCOMPARE(client.connectionState(), RFKitClient::Connecting);

        newHost.release();
        QTRY_COMPARE(client.connectionState(), RFKitClient::Connected);
    }

    // Same antenna count, new names and ids: the cached list must follow the amplifier.
    void aRefreshedAntennaListReplacesOneOfTheSameSize() {
        FakeRfkitServer server;
        server.setResponse("/info", R"({"device":"RFKit"})");
        server.setResponse("/antennas", R"([{"id":1,"number":1,"name":"Beam"},{"id":2,"number":2,"name":"Dipole"}])");

        RFKitClient client;
        QSignalSpy updated(&client, &RFKitClient::antennasUpdated);
        client.connectToHost("127.0.0.1", server.port());
        QTRY_COMPARE(updated.count(), 1);
        QCOMPARE(client.antennas().value(0).name, QStringLiteral("Beam"));

        client.disconnectFromHost();
        server.setResponse("/antennas", R"([{"id":7,"number":1,"name":"Yagi"},{"id":8,"number":2,"name":"Loop"}])");
        client.connectToHost("127.0.0.1", server.port());
        QTRY_COMPARE(updated.count(), 2);
        QCOMPARE(client.antennas().value(0).id, 7);
        QCOMPARE(client.antennas().value(0).name, QStringLiteral("Yagi"));
        QCOMPARE(client.antennas().value(1).name, QStringLiteral("Loop"));
    }

    // An operate state cached from the last amplifier must not hide the new one's state.
    void aReconnectReportsTheOperateStateAfresh() {
        FakeRfkitServer server;
        server.setResponse("/info", R"({"device":"RFKit"})");
        server.setResponse("/operate-mode", R"({"operate_mode":"OPERATE"})");

        RFKitClient client;
        QSignalSpy opState(&client, &RFKitClient::operatingStateChanged);
        QSignalSpy connectedSpy(&client, &RFKitClient::connected);
        connect(&client, &RFKitClient::connected, &client, [&client]() { client.startPolling(10000); });
        client.connectToHost("127.0.0.1", server.port());
        QTRY_COMPARE(opState.count(), 1);

        client.disconnectFromHost();
        client.connectToHost("127.0.0.1", server.port());
        QTRY_COMPARE(connectedSpy.count(), 2);
        QTRY_COMPARE(opState.count(), 2);
        QCOMPARE(client.operatingState(), RFKitClient::StateOperate);
    }
};

QTEST_GUILESS_MAIN(TestRfkitClient)
#include "test_rfkitclient.moc"
