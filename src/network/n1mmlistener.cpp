#include "n1mmlistener.h"

#include <QXmlStreamReader>

N1mmListener::N1mmListener(QObject *parent) : QObject(parent) {
    m_expiryTimer = new QTimer(this);
    m_expiryTimer->setInterval(30000); // Check every 30 seconds
    connect(m_expiryTimer, &QTimer::timeout, this, &N1mmListener::expireOldSpots);
}

N1mmListener::~N1mmListener() {
    stop();
}

bool N1mmListener::start(quint16 port) {
    stop();

    m_socket = new QUdpSocket(this);
    if (!m_socket->bind(QHostAddress::Any, port, QAbstractSocket::ShareAddress | QAbstractSocket::ReuseAddressHint)) {
        emit listenError(QString("Failed to bind UDP port %1: %2").arg(port).arg(m_socket->errorString()));
        delete m_socket;
        m_socket = nullptr;
        return false;
    }

    connect(m_socket, &QUdpSocket::readyRead, this, [this]() {
        while (m_socket->hasPendingDatagrams()) {
            QByteArray datagram;
            datagram.resize(m_socket->pendingDatagramSize());
            m_socket->readDatagram(datagram.data(), datagram.size());
            processDatagram(datagram);
        }
    });

    m_expiryTimer->start();
    return true;
}

void N1mmListener::stop() {
    m_expiryTimer->stop();
    if (m_socket) {
        m_socket->close();
        delete m_socket;
        m_socket = nullptr;
    }
}

bool N1mmListener::isListening() const {
    return m_socket && m_socket->state() == QAbstractSocket::BoundState;
}

void N1mmListener::setExpiryMinutes(int minutes) {
    m_expiryMinutes = qMax(1, minutes);
}

void N1mmListener::clearSpots() {
    QStringList keys = m_spots.keys();
    m_spots.clear();
    for (const QString &key : keys) {
        emit spotRemoved(key);
    }
}

void N1mmListener::processDatagram(const QByteArray &data) {
    QXmlStreamReader xml(data);

    // Find the root element
    while (!xml.atEnd() && !xml.hasError()) {
        xml.readNext();
        if (xml.isStartElement() && xml.name() == u"spot") {
            break;
        }
        if (xml.isStartElement()) {
            return; // Not a spot message, ignore
        }
    }

    if (xml.hasError() || xml.atEnd())
        return;

    // Parse spot fields
    QString callsign;
    double frequencyKHz = 0.0;
    QString mode;
    QString status;
    QString action;
    QString timestampStr;

    while (!xml.atEnd() && !xml.hasError()) {
        xml.readNext();
        if (xml.isStartElement()) {
            QString name = xml.name().toString();
            QString text = xml.readElementText();
            if (name == "dxcall") {
                callsign = text.trimmed();
            } else if (name == "frequency") {
                frequencyKHz = text.toDouble();
            } else if (name == "mode") {
                mode = text.trimmed();
            } else if (name == "status") {
                status = text.trimmed();
            } else if (name == "action") {
                action = text.trimmed().toLower();
            } else if (name == "timestamp") {
                timestampStr = text.trimmed();
            }
        }
    }

    if (callsign.isEmpty())
        return;

    if (action == "delete") {
        if (m_spots.remove(callsign)) {
            emit spotRemoved(callsign);
        }
        return;
    }

    // action == "add" (or unspecified, treat as add)
    SpotData spot;
    spot.callsign = callsign;
    spot.frequencyHz = static_cast<qint64>(frequencyKHz * 1000.0);
    spot.mode = mode;
    spot.status = status;
    spot.timestamp = QDateTime::fromString(timestampStr, "yyyy-MM-dd HH:mm:ss");
    spot.age.start();

    m_spots.insert(callsign, spot);
    emit spotReceived(spot);
}

void N1mmListener::expireOldSpots() {
    qint64 expiryMs = m_expiryMinutes * 60 * 1000;
    QStringList expired;

    for (auto it = m_spots.begin(); it != m_spots.end();) {
        if (it->age.elapsed() > expiryMs) {
            expired.append(it.key());
            it = m_spots.erase(it);
        } else {
            ++it;
        }
    }

    for (const QString &callsign : expired) {
        emit spotRemoved(callsign);
    }
}
