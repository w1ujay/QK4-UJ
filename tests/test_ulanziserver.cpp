// Tests for UlanziServer: the line protocol the Ulanzi Studio plugin speaks, tap/hold timing, and
// (further down) the socket lifecycle. Protocol: docs/superpowers/specs/2026-10-04-ulanzi-d100h-design.md §1.
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <memory>
#include "network/ulanziserver.h"
#include "utils/macroids.h"

using Type = UlanziEvent::Type;

namespace {
UlanziEvent rotate(int steps, bool hold) {
    UlanziEvent e;
    e.type = Type::Rotate;
    e.steps = steps;
    e.hold = hold;
    return e;
}
UlanziEvent dial(bool down) {
    UlanziEvent e;
    e.type = Type::Dial;
    e.down = down;
    return e;
}
UlanziEvent button(int slot, bool down) {
    UlanziEvent e;
    e.type = Type::Button;
    e.slot = slot;
    e.down = down;
    return e;
}
UlanziEvent ptt(bool down) {
    UlanziEvent e;
    e.type = Type::Ptt;
    e.down = down;
    return e;
}
UlanziEvent cancelled(UlanziEvent release) {
    release.cancel = true;
    return release;
}
// A valid PTT line padded with JSON whitespace to exactly `rawBytes`, newline not included.
QByteArray paddedPttLine(int rawBytes) {
    const QByteArray head = R"({"t":"ptt","down":true)";
    return head + QByteArray(rawBytes - head.size() - 1, ' ') + '}';
}
} // namespace

class TestUlanziServer : public QObject {
    Q_OBJECT

private:
    // Short enough to keep the suite fast, long enough that a loaded CI machine does not fire it early.
    static constexpr int kHoldMs = 60;
    // Comfortably past kHoldMs, for asserting that something did NOT happen.
    static constexpr int kPastHoldMs = 200;

    static constexpr int kTimeoutMs = 2000;
    static constexpr int kPollStepMs = 5;

    // The server lives on this thread and only runs while the event loop does, so every wait spins it.
    template <typename Predicate> bool spinUntil(Predicate ready, int timeoutMs = kTimeoutMs) {
        QElapsedTimer timer;
        timer.start();
        while (!ready()) {
            if (timer.elapsed() >= timeoutMs)
                return false;
            QTest::qWait(kPollStepMs);
        }
        return true;
    }

    // A client the server has adopted (hasClient() true), or null.
    std::unique_ptr<QTcpSocket> connectClient(UlanziServer &server) {
        auto sock = std::make_unique<QTcpSocket>();
        sock->connectToHost(QHostAddress::LocalHost, server.port());
        if (!sock->waitForConnected(1000))
            return nullptr;
        if (!spinUntil([&] { return server.hasClient(); }))
            return nullptr;
        return sock;
    }

    void send(QTcpSocket &sock, const QByteArray &bytes) {
        sock.write(bytes);
        sock.flush();
    }

private slots:
    // ---- parseLine ---------------------------------------------------------------------------

    void parseLine_data() {
        QTest::addColumn<QByteArray>("line");
        QTest::addColumn<int>("type");
        QTest::addColumn<int>("steps");
        QTest::addColumn<bool>("hold");
        QTest::addColumn<int>("slot");
        QTest::addColumn<bool>("down");

        const int invalid = int(Type::Invalid);
        QTest::newRow("rotate right") << QByteArray(R"({"t":"rotate","n":1,"hold":false})") << int(Type::Rotate) << 1
                                      << false << 0 << false;
        QTest::newRow("rotate left held")
            << QByteArray(R"({"t":"rotate","n":-1,"hold":true})") << int(Type::Rotate) << -1 << true << 0 << false;
        QTest::newRow("dial down") << QByteArray(R"({"t":"dial","down":true})") << int(Type::Dial) << 0 << false << 0
                                   << true;
        QTest::newRow("dial up") << QByteArray(R"({"t":"dial","down":false})") << int(Type::Dial) << 0 << false << 0
                                 << false;
        QTest::newRow("button 1 down") << QByteArray(R"({"t":"button","slot":1,"down":true})") << int(Type::Button) << 0
                                       << false << 1 << true;
        QTest::newRow("button 7 up") << QByteArray(R"({"t":"button","slot":7,"down":false})") << int(Type::Button) << 0
                                     << false << 7 << false;
        QTest::newRow("ptt down") << QByteArray(R"({"t":"ptt","down":true})") << int(Type::Ptt) << 0 << false << 0
                                  << true;
        QTest::newRow("ptt up") << QByteArray(R"({"t":"ptt","down":false})") << int(Type::Ptt) << 0 << false << 0
                                << false;
        QTest::newRow("extra field ignored")
            << QByteArray(R"({"t":"ptt","down":true,"v":2})") << int(Type::Ptt) << 0 << false << 0 << true;

        QTest::newRow("not json") << QByteArray(R"({"t":)") << invalid << 0 << false << 0 << false;
        QTest::newRow("array") << QByteArray("[1]") << invalid << 0 << false << 0 << false;
        QTest::newRow("empty object") << QByteArray("{}") << invalid << 0 << false << 0 << false;
        QTest::newRow("unknown type") << QByteArray(R"({"t":"spin","n":1,"hold":false})") << invalid << 0 << false << 0
                                      << false;
        QTest::newRow("rotate n 0") << QByteArray(R"({"t":"rotate","n":0,"hold":false})") << invalid << 0 << false << 0
                                    << false;
        QTest::newRow("rotate n 2") << QByteArray(R"({"t":"rotate","n":2,"hold":false})") << invalid << 0 << false << 0
                                    << false;
        QTest::newRow("rotate n string") << QByteArray(R"({"t":"rotate","n":"1","hold":false})") << invalid << 0
                                         << false << 0 << false;
        QTest::newRow("rotate missing hold")
            << QByteArray(R"({"t":"rotate","n":1})") << invalid << 0 << false << 0 << false;
        QTest::newRow("button slot 0") << QByteArray(R"({"t":"button","slot":0,"down":true})") << invalid << 0 << false
                                       << 0 << false;
        QTest::newRow("button slot 8") << QByteArray(R"({"t":"button","slot":8,"down":true})") << invalid << 0 << false
                                       << 0 << false;
        QTest::newRow("button slot fraction")
            << QByteArray(R"({"t":"button","slot":2.5,"down":true})") << invalid << 0 << false << 0 << false;
        QTest::newRow("button missing slot")
            << QByteArray(R"({"t":"button","down":true})") << invalid << 0 << false << 0 << false;
        QTest::newRow("down not bool") << QByteArray(R"({"t":"dial","down":1})") << invalid << 0 << false << 0 << false;
        QTest::newRow("ptt missing down") << QByteArray(R"({"t":"ptt"})") << invalid << 0 << false << 0 << false;
        QTest::newRow("cancel on a press") << QByteArray(R"({"t":"button","slot":1,"down":true,"cancel":true})")
                                           << invalid << 0 << false << 0 << false;
        QTest::newRow("cancel not bool") << QByteArray(R"({"t":"dial","down":false,"cancel":1})") << invalid << 0
                                         << false << 0 << false;
    }

    void parseLineReadsCancel() {
        QCOMPARE(UlanziServer::parseLine(R"({"t":"button","slot":2,"down":false,"cancel":true})").cancel, true);
        QCOMPARE(UlanziServer::parseLine(R"({"t":"dial","down":false,"cancel":true})").cancel, true);
        QCOMPARE(UlanziServer::parseLine(R"({"t":"dial","down":false,"cancel":false})").cancel, false);
        QCOMPARE(UlanziServer::parseLine(R"({"t":"dial","down":false})").cancel, false);
        // PTT has no tap to suppress; an extra field there is ignored like any other.
        const UlanziEvent e = UlanziServer::parseLine(R"({"t":"ptt","down":false,"cancel":true})");
        QCOMPARE(int(e.type), int(Type::Ptt));
        QCOMPARE(e.cancel, false);
    }

    void parseLine() {
        QFETCH(QByteArray, line);
        QFETCH(int, type);
        QFETCH(int, steps);
        QFETCH(bool, hold);
        QFETCH(int, slot);
        QFETCH(bool, down);

        const UlanziEvent e = UlanziServer::parseLine(line);
        QCOMPARE(int(e.type), type);
        if (e.type == Type::Invalid)
            return;
        QCOMPARE(e.steps, steps);
        QCOMPARE(e.hold, hold);
        QCOMPARE(e.slot, slot);
        QCOMPARE(e.down, down);
    }

    // ---- tap / hold --------------------------------------------------------------------------

    void quickPressIsATap() {
        UlanziServer server;
        server.setHoldMs(kHoldMs);
        QSignalSpy tapped(&server, &UlanziServer::buttonTapped);
        QSignalSpy held(&server, &UlanziServer::buttonHeld);

        server.handleEvent(button(3, true));
        server.handleEvent(button(3, false));
        QCOMPARE(tapped.count(), 1);
        QCOMPARE(tapped.at(0).at(0).toInt(), 3);

        QTest::qWait(kPastHoldMs); // the hold timer must not fire after the release
        QCOMPARE(held.count(), 0);
    }

    // Hold fires while the key is still down, as the KPOD's does, not on release.
    void longPressFiresHoldWhileDown() {
        UlanziServer server;
        server.setHoldMs(kHoldMs);
        QSignalSpy tapped(&server, &UlanziServer::buttonTapped);
        QSignalSpy held(&server, &UlanziServer::buttonHeld);

        server.handleEvent(button(5, true));
        QTRY_COMPARE_WITH_TIMEOUT(held.count(), 1, 2000);
        QCOMPARE(held.at(0).at(0).toInt(), 5);

        server.handleEvent(button(5, false));
        QCOMPARE(tapped.count(), 0); // a hold is not also a tap
        QCOMPARE(held.count(), 1);
    }

    void dialQuickPressIsATap() {
        UlanziServer server;
        server.setHoldMs(kHoldMs);
        QSignalSpy tapped(&server, &UlanziServer::dialTapped);
        QSignalSpy held(&server, &UlanziServer::dialHeld);

        server.handleEvent(dial(true));
        server.handleEvent(dial(false));
        QCOMPARE(tapped.count(), 1);
        QTest::qWait(kPastHoldMs);
        QCOMPARE(held.count(), 0);
    }

    // Unlike the buttons, the dial's hold waits for the release: until then a turn can still make the press
    // a VFO B gesture, and a macro already run could not be taken back.
    void dialLongPressHoldsOnRelease() {
        UlanziServer server;
        server.setHoldMs(kHoldMs);
        QSignalSpy tapped(&server, &UlanziServer::dialTapped);
        QSignalSpy held(&server, &UlanziServer::dialHeld);

        server.handleEvent(dial(true));
        QTest::qWait(kPastHoldMs);
        QCOMPARE(held.count(), 0); // nothing while still down
        server.handleEvent(dial(false));
        QCOMPARE(held.count(), 1);
        QCOMPARE(tapped.count(), 0);
    }

    // The case a hold fired at the threshold could not handle: held past 500 ms, THEN turned.
    void turningAfterTheHoldThresholdRunsNoDialMacro() {
        UlanziServer server;
        server.setHoldMs(kHoldMs);
        QSignalSpy tapped(&server, &UlanziServer::dialTapped);
        QSignalSpy held(&server, &UlanziServer::dialHeld);

        server.handleEvent(dial(true));
        QTest::qWait(kPastHoldMs);
        server.handleEvent(rotate(-1, true));
        server.handleEvent(dial(false));
        QCOMPARE(held.count(), 0);
        QCOMPARE(tapped.count(), 0);
    }

    // A cancelled release (the deck action was removed mid-press) is not a press at all.
    void aCancelledReleaseFiresNothing() {
        UlanziServer server;
        server.setHoldMs(kHoldMs);
        QSignalSpy buttonTapped(&server, &UlanziServer::buttonTapped);
        QSignalSpy buttonHeld(&server, &UlanziServer::buttonHeld);
        QSignalSpy dialTapped(&server, &UlanziServer::dialTapped);
        QSignalSpy dialHeld(&server, &UlanziServer::dialHeld);

        server.handleEvent(button(1, true));
        server.handleEvent(cancelled(button(1, false)));
        server.handleEvent(dial(true));
        QTest::qWait(kPastHoldMs);
        server.handleEvent(cancelled(dial(false)));
        QTest::qWait(kPastHoldMs);

        QCOMPARE(buttonTapped.count(), 0);
        QCOMPARE(buttonHeld.count(), 0); // the timer was stopped by the cancel
        QCOMPARE(dialTapped.count(), 0);
        QCOMPARE(dialHeld.count(), 0);

        // And the key is free again: the next press works normally.
        server.handleEvent(button(1, true));
        server.handleEvent(button(1, false));
        QCOMPARE(buttonTapped.count(), 1);
    }

    // Holding the dial down and turning it tunes VFO B; that press must not also run a dial macro.
    void turningWhileHeldCancelsTheDialPress() {
        UlanziServer server;
        server.setHoldMs(kHoldMs);
        QSignalSpy rotated(&server, &UlanziServer::rotated);
        QSignalSpy tapped(&server, &UlanziServer::dialTapped);
        QSignalSpy held(&server, &UlanziServer::dialHeld);

        server.handleEvent(dial(true));
        server.handleEvent(rotate(1, true));
        QCOMPARE(rotated.count(), 1);
        QCOMPARE(rotated.at(0).at(0).toInt(), 1);
        QCOMPARE(rotated.at(0).at(1).toBool(), true);

        QTest::qWait(kPastHoldMs);
        server.handleEvent(dial(false));
        QCOMPARE(held.count(), 0);
        QCOMPARE(tapped.count(), 0);
    }

    void plainRotationIsPassedThrough() {
        UlanziServer server;
        QSignalSpy rotated(&server, &UlanziServer::rotated);
        server.handleEvent(rotate(-1, false));
        QCOMPARE(rotated.count(), 1);
        QCOMPARE(rotated.at(0).at(0).toInt(), -1);
        QCOMPARE(rotated.at(0).at(1).toBool(), false);
    }

    void pttFollowsPressAndRelease() {
        UlanziServer server;
        QSignalSpy spy(&server, &UlanziServer::pttChanged);
        server.handleEvent(ptt(true));
        server.handleEvent(ptt(false));
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(0).at(0).toBool(), true);
        QCOMPARE(spy.at(1).at(0).toBool(), false);
    }

    // Review Focus 4. A dropped keydown or a repeated one must not invent or double a press.
    void unpairedAndRepeatedEdgesAreIgnored() {
        UlanziServer server;
        server.setHoldMs(kHoldMs);
        QSignalSpy tapped(&server, &UlanziServer::buttonTapped);
        QSignalSpy pttSpy(&server, &UlanziServer::pttChanged);

        server.handleEvent(button(2, false)); // release with no press
        QCOMPARE(tapped.count(), 0);

        server.handleEvent(button(2, true));
        server.handleEvent(button(2, true)); // repeated press
        server.handleEvent(button(2, false));
        QCOMPARE(tapped.count(), 1);

        server.handleEvent(ptt(false)); // release while not keyed
        QCOMPARE(pttSpy.count(), 0);
        server.handleEvent(ptt(true));
        server.handleEvent(ptt(true)); // repeated press
        QCOMPARE(pttSpy.count(), 1);
    }

    // ---- socket lifecycle --------------------------------------------------------------------

    void startsOnAnEphemeralPortAndReportsIt() {
        UlanziServer server;
        QSignalSpy started(&server, &UlanziServer::started);
        QVERIFY(server.start(0));
        QVERIFY(server.isListening());
        QVERIFY(server.port() != 0);
        QCOMPARE(started.count(), 1);
        QVERIFY(server.lastError().isEmpty());
    }

    void eventsArriveOverTheSocket() {
        UlanziServer server;
        QVERIFY(server.start(0));
        QSignalSpy connected(&server, &UlanziServer::clientConnectedChanged);
        QSignalSpy rotated(&server, &UlanziServer::rotated);
        QSignalSpy pttSpy(&server, &UlanziServer::pttChanged);

        auto client = connectClient(server);
        QVERIFY(client);
        QCOMPARE(connected.count(), 1);
        QCOMPARE(connected.at(0).at(0).toBool(), true);

        send(*client, R"({"t":"rotate","n":1,"hold":false})"
                      "\n"
                      R"({"t":"ptt","down":true})"
                      "\n");
        QTRY_COMPARE_WITH_TIMEOUT(pttSpy.count(), 1, kTimeoutMs);
        QCOMPARE(rotated.count(), 1);
    }

    // Review Focus 1. A fast spin: many detents land in one read and every one must count.
    void burstOfDetentsInOneWriteAreAllDelivered() {
        UlanziServer server;
        QVERIFY(server.start(0));
        QSignalSpy rotated(&server, &UlanziServer::rotated);
        auto client = connectClient(server);
        QVERIFY(client);

        QByteArray burst;
        for (int i = 0; i < 20; ++i)
            burst += R"({"t":"rotate","n":-1,"hold":false})"
                     "\n";
        send(*client, burst);
        QTRY_COMPARE_WITH_TIMEOUT(rotated.count(), 20, kTimeoutMs);
    }

    // Review Focus 2.
    void aLineSplitAcrossWritesIsJoined() {
        UlanziServer server;
        QVERIFY(server.start(0));
        QSignalSpy rotated(&server, &UlanziServer::rotated);
        auto client = connectClient(server);
        QVERIFY(client);

        send(*client, R"({"t":"rotate",)");
        QTest::qWait(50);
        QCOMPARE(rotated.count(), 0);
        send(*client, R"("n":1,"hold":false})"
                      "\n");
        QTRY_COMPARE_WITH_TIMEOUT(rotated.count(), 1, kTimeoutMs);
    }

    // Review Focus 3.
    void crlfLineEndingsAreAccepted() {
        UlanziServer server;
        QVERIFY(server.start(0));
        QSignalSpy pttSpy(&server, &UlanziServer::pttChanged);
        auto client = connectClient(server);
        QVERIFY(client);

        send(*client, "{\"t\":\"ptt\",\"down\":true}  \r\n");
        QTRY_COMPARE_WITH_TIMEOUT(pttSpy.count(), 1, kTimeoutMs);
    }

    void aMalformedLineIsSkippedAndTheConnectionStays() {
        UlanziServer server;
        QVERIFY(server.start(0));
        QSignalSpy pttSpy(&server, &UlanziServer::pttChanged);
        auto client = connectClient(server);
        QVERIFY(client);

        send(*client, "garbage\n"
                      R"({"t":"ptt","down":true})"
                      "\n");
        QTRY_COMPARE_WITH_TIMEOUT(pttSpy.count(), 1, kTimeoutMs);
        QVERIFY(server.hasClient());
    }

    // A new client replaces the old one, and whatever the old one held is released.
    void aSecondClientReplacesTheFirst() {
        UlanziServer server;
        QVERIFY(server.start(0));
        QSignalSpy pttSpy(&server, &UlanziServer::pttChanged);
        QSignalSpy rotated(&server, &UlanziServer::rotated);

        auto first = connectClient(server);
        QVERIFY(first);
        send(*first, R"({"t":"ptt","down":true})"
                     "\n");
        QTRY_COMPARE_WITH_TIMEOUT(pttSpy.count(), 1, kTimeoutMs);

        auto second = std::make_unique<QTcpSocket>();
        second->connectToHost(QHostAddress::LocalHost, server.port());
        QVERIFY(second->waitForConnected(1000));
        QTRY_COMPARE_WITH_TIMEOUT(pttSpy.count(), 2, kTimeoutMs);
        QCOMPARE(pttSpy.at(1).at(0).toBool(), false);
        QVERIFY(spinUntil([&] { return first->state() == QAbstractSocket::UnconnectedState; }));

        send(*second, R"({"t":"rotate","n":1,"hold":false})"
                      "\n");
        QTRY_COMPARE_WITH_TIMEOUT(rotated.count(), 1, kTimeoutMs);
        QVERIFY(server.hasClient());
    }

    // ---- the 4096-byte raw line limit (newline excluded, measured before trimming) -----------

    void aLineOfExactlyTheLimitIsAccepted() {
        UlanziServer server;
        QVERIFY(server.start(0));
        QSignalSpy pttSpy(&server, &UlanziServer::pttChanged);
        auto client = connectClient(server);
        QVERIFY(client);

        const QByteArray line = paddedPttLine(UlanziServer::MAX_LINE_BYTES);
        QCOMPARE(line.size(), UlanziServer::MAX_LINE_BYTES);
        send(*client, line + '\n');
        QTRY_COMPARE_WITH_TIMEOUT(pttSpy.count(), 1, kTimeoutMs);
        QVERIFY(server.hasClient());
    }

    // The bound is per raw line, so a terminating newline does not make an oversized line acceptable.
    void aTerminatedLineOverTheLimitDropsTheClientUnparsed() {
        UlanziServer server;
        QVERIFY(server.start(0));
        QSignalSpy pttSpy(&server, &UlanziServer::pttChanged);
        auto client = connectClient(server);
        QVERIFY(client);

        send(*client, paddedPttLine(UlanziServer::MAX_LINE_BYTES + 1) + '\n');
        QVERIFY(spinUntil([&] { return !server.hasClient(); }));
        QCOMPARE(pttSpy.count(), 0); // never parsed
    }

    // Trailing whitespace counts: the limit is checked before trimming.
    void trailingWhitespaceCountsTowardTheLimit() {
        UlanziServer server;
        QVERIFY(server.start(0));
        auto client = connectClient(server);
        QVERIFY(client);

        const QByteArray line = R"({"t":"ptt","down":true})";
        send(*client, line + QByteArray(UlanziServer::MAX_LINE_BYTES + 1 - line.size(), ' ') + '\n');
        QVERIFY(spinUntil([&] { return !server.hasClient(); }));
    }

    void anUnterminatedLineAtTheLimitWaitsForItsNewline() {
        UlanziServer server;
        QVERIFY(server.start(0));
        QSignalSpy pttSpy(&server, &UlanziServer::pttChanged);
        auto client = connectClient(server);
        QVERIFY(client);

        send(*client, paddedPttLine(UlanziServer::MAX_LINE_BYTES));
        QTest::qWait(100);
        QVERIFY(server.hasClient());
        QCOMPARE(pttSpy.count(), 0);
        send(*client, "\n");
        QTRY_COMPARE_WITH_TIMEOUT(pttSpy.count(), 1, kTimeoutMs);
    }

    void anUnterminatedLineOverTheLimitDropsTheClient() {
        UlanziServer server;
        QVERIFY(server.start(0));
        auto client = connectClient(server);
        QVERIFY(client);

        send(*client, QByteArray(UlanziServer::MAX_LINE_BYTES + 1, 'x'));
        QVERIFY(spinUntil([&] { return !server.hasClient(); }));
    }

    // A flood with no newline is refused as soon as it passes the limit, not after it has all arrived.
    void aFloodWithoutNewlinesIsDroppedEarly() {
        UlanziServer server;
        QVERIFY(server.start(0));
        auto client = connectClient(server);
        QVERIFY(client);

        send(*client, QByteArray(4 * 1024 * 1024, 'x'));
        QVERIFY(spinUntil([&] { return !server.hasClient(); }));
    }

    // Ulanzi Studio crashing or quitting mid-transmission must unkey the radio.
    void aDisconnectWhilePttIsDownReleasesIt() {
        UlanziServer server;
        QVERIFY(server.start(0));
        QSignalSpy pttSpy(&server, &UlanziServer::pttChanged);
        QSignalSpy connected(&server, &UlanziServer::clientConnectedChanged);
        auto client = connectClient(server);
        QVERIFY(client);

        send(*client, R"({"t":"ptt","down":true})"
                      "\n");
        QTRY_COMPARE_WITH_TIMEOUT(pttSpy.count(), 1, kTimeoutMs);
        client->disconnectFromHost();
        QTRY_COMPARE_WITH_TIMEOUT(pttSpy.count(), 2, kTimeoutMs);
        QCOMPARE(pttSpy.at(1).at(0).toBool(), false);
        QVERIFY(!server.hasClient());
        QCOMPARE(connected.last().at(0).toBool(), false);
    }

    // A press left unfinished by a disconnect is dropped: no tap on the way out, no hold later.
    void aDisconnectWhileAButtonIsDownFiresNothing() {
        UlanziServer server;
        server.setHoldMs(kHoldMs);
        QVERIFY(server.start(0));
        QSignalSpy tapped(&server, &UlanziServer::buttonTapped);
        QSignalSpy held(&server, &UlanziServer::buttonHeld);
        auto client = connectClient(server);
        QVERIFY(client);

        send(*client, R"({"t":"button","slot":4,"down":true})"
                      "\n");
        QTest::qWait(10);
        client->disconnectFromHost();
        QVERIFY(spinUntil([&] { return !server.hasClient(); }));
        QTest::qWait(kPastHoldMs);
        QCOMPARE(tapped.count(), 0);
        QCOMPARE(held.count(), 0);
    }

    void stopWhilePttIsDownReleasesItFirst() {
        UlanziServer server;
        QVERIFY(server.start(0));
        QSignalSpy pttSpy(&server, &UlanziServer::pttChanged);
        QSignalSpy stopped(&server, &UlanziServer::stopped);
        auto client = connectClient(server);
        QVERIFY(client);

        send(*client, R"({"t":"ptt","down":true})"
                      "\n");
        QTRY_COMPARE_WITH_TIMEOUT(pttSpy.count(), 1, kTimeoutMs);
        server.stop();
        QCOMPARE(pttSpy.count(), 2);
        QCOMPARE(pttSpy.at(1).at(0).toBool(), false);
        QCOMPARE(stopped.count(), 1);
        QVERIFY(!server.isListening());
        QCOMPARE(server.port(), quint16(0));
    }

    void aTakenPortIsReported() {
        QTcpServer blocker;
        QVERIFY(blocker.listen(QHostAddress::LocalHost, 0));

        UlanziServer server;
        QSignalSpy errors(&server, &UlanziServer::errorOccurred);
        QVERIFY(!server.start(blocker.serverPort()));
        QVERIFY(!server.isListening());
        QCOMPARE(errors.count(), 1);
        QVERIFY(server.lastError().startsWith(QStringLiteral("Port %1 unavailable: ").arg(blocker.serverPort())));
    }

    // ---- macro IDs ---------------------------------------------------------------------------

    // HardwareController builds IDs with the helpers; the macro dialog lists the constants. They must agree,
    // or a configured macro never runs.
    void macroIdsMatchTheMacroDialogSlots() {
        QCOMPARE(MacroIds::ulanziButton(1, false), MacroIds::Ulanzi1T);
        QCOMPARE(MacroIds::ulanziButton(1, true), MacroIds::Ulanzi1H);
        QCOMPARE(MacroIds::ulanziButton(7, false), MacroIds::Ulanzi7T);
        QCOMPARE(MacroIds::ulanziButton(7, true), MacroIds::Ulanzi7H);
        QCOMPARE(MacroIds::ulanziDial(false), MacroIds::UlanziDialT);
        QCOMPARE(MacroIds::ulanziDial(true), MacroIds::UlanziDialH);
        QCOMPARE(MacroIds::Ulanzi3T, QStringLiteral("Ulanzi.3T"));
        QCOMPARE(MacroIds::UlanziDialH, QStringLiteral("Ulanzi.DialH"));
    }
};

QTEST_GUILESS_MAIN(TestUlanziServer)
#include "test_ulanziserver.moc"
