#include "ulanziserver.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <cmath>
#include <cstring>

Q_LOGGING_CATEGORY(netUlanzi, "net.ulanzi")

UlanziServer::UlanziServer(QObject *parent) : QObject(parent), m_server(new QTcpServer(this)) {
    connect(m_server, &QTcpServer::newConnection, this, &UlanziServer::onNewConnection);
    for (int key = 0; key < static_cast<int>(m_keys.size()); ++key) {
        auto *timer = new QTimer(this);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, this, [this, key]() { onHoldTimeout(key); });
        m_keys[key].timer = timer;
    }
}

UlanziServer::~UlanziServer() {
    stop();
}

void UlanziServer::setHoldMs(int ms) {
    m_holdMs = ms;
}

bool UlanziServer::start(quint16 port) {
    if (m_server->isListening()) {
        if (m_server->serverPort() == port)
            return true;
        stop();
    }
    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        m_lastError = QStringLiteral("Port %1 unavailable: %2").arg(port).arg(m_server->errorString());
        qCWarning(netUlanzi) << m_lastError;
        emit errorOccurred(m_lastError);
        return false;
    }
    m_lastError.clear();
    qCInfo(netUlanzi) << "Ulanzi server listening on port" << m_server->serverPort();
    emit started(m_server->serverPort());
    return true;
}

void UlanziServer::stop() {
    // Unkey before the listener goes, as CatServer does: disabling the server mid-transmission must
    // not leave the radio keyed with no client left to release it.
    dropClient();
    m_lastError.clear();
    if (m_server->isListening()) {
        m_server->close();
        emit stopped();
    }
}

bool UlanziServer::isListening() const {
    return m_server->isListening();
}

bool UlanziServer::hasClient() const {
    return m_client != nullptr;
}

quint16 UlanziServer::port() const {
    return m_server->isListening() ? m_server->serverPort() : 0;
}

QString UlanziServer::lastError() const {
    return m_lastError;
}

void UlanziServer::onNewConnection() {
    while (m_server->hasPendingConnections()) {
        QTcpSocket *next = m_server->nextPendingConnection();
        if (m_client) {
            qCInfo(netUlanzi) << "A new Ulanzi client replaces the current one";
            dropClient();
        }
        m_client = next;
        // Bound what Qt buffers from the socket. Past this the kernel applies TCP backpressure, so a
        // client that floods is held at the socket instead of in our memory.
        next->setReadBufferSize(SOCKET_READ_BUFFER_BYTES);
        connect(next, &QTcpSocket::readyRead, this, &UlanziServer::onReadyRead);
        connect(next, &QTcpSocket::disconnected, this, [this, next]() {
            if (m_client == next)
                dropClient();
        });
        qCInfo(netUlanzi) << "Ulanzi client connected from port" << next->peerPort();
        emit clientConnectedChanged(true);
    }
}

void UlanziServer::onReadyRead() {
    QTcpSocket *client = m_client;
    if (!client)
        return;

    // WHY bounded chunks with the limit checked as bytes arrive: reading everything first and checking
    // afterwards let a newline-terminated line of any size through to the parser, and held a whole flood in
    // memory before looking at it. Here no line ever grows past MAX_LINE_BYTES raw bytes.
    char chunk[MAX_LINE_BYTES];
    while (m_client == client && client->bytesAvailable() > 0) {
        const qint64 n = client->read(chunk, sizeof(chunk));
        if (n <= 0)
            return;
        const char *p = chunk;
        const char *const end = chunk + n;
        while (p < end) {
            const char *newline = static_cast<const char *>(std::memchr(p, '\n', end - p));
            const qsizetype take = (newline ? newline : end) - p;
            if (m_buffer.size() + take > MAX_LINE_BYTES) {
                qCWarning(netUlanzi) << "Ulanzi client sent a line over" << MAX_LINE_BYTES << "bytes - disconnecting";
                dropClient();
                return;
            }
            m_buffer.append(p, take);
            if (!newline)
                break;
            p = newline + 1;
            const QByteArray line = m_buffer;
            m_buffer.clear();
            processLine(line);
            if (m_client != client)
                return; // a receiver stopped the server or dropped the client
        }
    }
}

void UlanziServer::processLine(const QByteArray &rawLine) {
    const QByteArray line = rawLine.trimmed(); // after the length check, so whitespace counts toward it
    if (line.isEmpty())
        return;
    const UlanziEvent event = parseLine(line);
    if (event.type == UlanziEvent::Type::Invalid) {
        qCDebug(netUlanzi) << "Ignoring malformed line:" << line.left(120);
        return;
    }
    handleEvent(event);
}

void UlanziServer::dropClient() {
    if (!m_client)
        return;
    QTcpSocket *old = m_client;
    m_client = nullptr;
    m_buffer.clear();
    old->disconnect(this); // no re-entry from the disconnected() that abort() raises
    old->abort();
    old->deleteLater();
    releaseInputs();
    emit clientConnectedChanged(false);
}

UlanziEvent UlanziServer::parseLine(const QByteArray &line) {
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    const QJsonObject o = doc.object();
    const QString t = o.value(QStringLiteral("t")).toString();

    UlanziEvent e;
    if (t == QStringLiteral("rotate")) {
        const QJsonValue n = o.value(QStringLiteral("n"));
        const QJsonValue hold = o.value(QStringLiteral("hold"));
        if (!n.isDouble() || !hold.isBool())
            return {};
        const double steps = n.toDouble();
        if (steps != 1.0 && steps != -1.0)
            return {};
        e.type = UlanziEvent::Type::Rotate;
        e.steps = steps > 0 ? 1 : -1;
        e.hold = hold.toBool();
        return e;
    }

    const QJsonValue down = o.value(QStringLiteral("down"));
    if (!down.isBool())
        return {};
    e.down = down.toBool();

    // Optional on a dial or button release. Read before the type branches, applied only to those two.
    const QJsonValue cancel = o.value(QStringLiteral("cancel"));
    const bool hasCancel = !cancel.isUndefined();
    if (hasCancel && !cancel.isBool())
        return {};
    const bool cancelled = hasCancel && cancel.toBool();
    const bool keyType = t == QStringLiteral("dial") || t == QStringLiteral("button");
    if (keyType && cancelled && e.down)
        return {}; // a cancelled press is meaningless
    if (keyType)
        e.cancel = cancelled;

    if (t == QStringLiteral("dial")) {
        e.type = UlanziEvent::Type::Dial;
        return e;
    }
    if (t == QStringLiteral("ptt")) {
        e.type = UlanziEvent::Type::Ptt;
        return e;
    }
    if (t == QStringLiteral("button")) {
        const QJsonValue slot = o.value(QStringLiteral("slot"));
        if (!slot.isDouble())
            return {};
        const double s = slot.toDouble();
        if (s != std::floor(s) || s < 1 || s > BUTTON_COUNT)
            return {};
        e.type = UlanziEvent::Type::Button;
        e.slot = static_cast<int>(s);
        return e;
    }
    return {};
}

void UlanziServer::handleEvent(const UlanziEvent &event) {
    switch (event.type) {
    case UlanziEvent::Type::Rotate:
        // Turning while the dial is held is the VFO B gesture, before or after the hold threshold. It is
        // not a press, so that press must never also fire Ulanzi.DialT or Ulanzi.DialH.
        if (event.hold && m_keys[DIAL_KEY].down) {
            m_keys[DIAL_KEY].cancelled = true;
            m_keys[DIAL_KEY].timer->stop();
        }
        emit rotated(event.steps, event.hold);
        break;
    case UlanziEvent::Type::Dial:
        pressKey(DIAL_KEY, event.down, event.cancel);
        break;
    case UlanziEvent::Type::Button:
        if (event.slot >= 1 && event.slot <= BUTTON_COUNT)
            pressKey(event.slot, event.down, event.cancel);
        break;
    case UlanziEvent::Type::Ptt:
        if (event.down != m_pttDown) {
            m_pttDown = event.down;
            emit pttChanged(event.down);
        }
        break;
    case UlanziEvent::Type::Invalid:
        break;
    }
}

void UlanziServer::pressKey(int key, bool down, bool cancel) {
    KeyState &k = m_keys[key];
    if (down) {
        if (k.down)
            return; // a repeated press is not a second press
        k.down = true;
        k.held = false;
        k.cancelled = false;
        k.timer->start(m_holdMs);
        return;
    }
    if (!k.down)
        return; // a release with no press
    k.down = false;
    k.timer->stop();
    const bool wasHeld = k.held;
    const bool notAPress = k.cancelled || cancel;
    k.held = false;
    k.cancelled = false;
    if (notAPress)
        return;

    if (key == DIAL_KEY) {
        // The dial's hold is decided at the threshold but emitted here, so a turn after the threshold
        // can still make the press a VFO B gesture (see onHoldTimeout).
        if (wasHeld)
            emit dialHeld();
        else
            emit dialTapped();
    } else if (!wasHeld) {
        emit buttonTapped(key); // a held button already emitted buttonHeld at the threshold
    }
}

void UlanziServer::onHoldTimeout(int key) {
    KeyState &k = m_keys[key];
    if (!k.down || k.cancelled || k.held)
        return;
    k.held = true;
    // Buttons hold at the threshold, while still down, like the KPOD. The dial only records it: holding
    // the dial and turning tunes VFO B, and a hold macro run at 500 ms could not be undone by a turn at
    // 600 ms. Its hold is emitted on release instead, if no turn happened.
    if (key != DIAL_KEY)
        emit buttonHeld(key);
}

void UlanziServer::releaseInputs() {
    // Whatever was down when the plugin went away stays unfinished: no tap, no hold. PTT is the one
    // input that must be actively undone, or the radio stays keyed with nobody holding the key.
    for (KeyState &k : m_keys) {
        k.timer->stop();
        k.down = false;
        k.held = false;
        k.cancelled = false;
    }
    if (m_pttDown) {
        m_pttDown = false;
        emit pttChanged(false);
    }
}
