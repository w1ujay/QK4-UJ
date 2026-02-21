#include "rfkitclient.h"
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>

RFKitClient::RFKitClient(QObject *parent)
    : QObject(parent), m_networkManager(new QNetworkAccessManager(this)), m_pollTimer(new QTimer(this)) {
    connect(m_pollTimer, &QTimer::timeout, this, &RFKitClient::onPollTimer);
}

RFKitClient::~RFKitClient() {
    stopPolling();
}

void RFKitClient::connectToHost(const QString &host, quint16 port) {
    if (m_state != Disconnected) {
        disconnectFromHost();
    }

    m_host = host;
    m_port = port;
    m_consecutiveErrors = 0;

    setState(Connecting);

    // Make an initial info request to verify connectivity
    pollInfo();
}

void RFKitClient::disconnectFromHost() {
    stopPolling();
    m_consecutiveErrors = 0;
    setState(Disconnected);
}

bool RFKitClient::isConnected() const {
    return m_state == Connected;
}

RFKitClient::ConnectionState RFKitClient::connectionState() const {
    return m_state;
}

void RFKitClient::startPolling(int intervalMs) {
    if (intervalMs > 0) {
        m_pollTimer->start(intervalMs);
        // Send initial poll immediately
        onPollTimer();
    }
}

void RFKitClient::stopPolling() {
    m_pollTimer->stop();
}

void RFKitClient::setState(ConnectionState state) {
    if (m_state != state) {
        ConnectionState oldState = m_state;
        m_state = state;
        emit stateChanged(state);

        if (state == Connected && oldState != Connected) {
            emit connected();
        } else if (state == Disconnected && oldState != Disconnected) {
            emit disconnected();
        }
    }
}

QString RFKitClient::buildUrl(const QString &endpoint) const {
    return QString("http://%1:%2%3").arg(m_host).arg(m_port).arg(endpoint);
}

// ============== Commands ==============

void RFKitClient::setOperateMode(bool operate) {
    QNetworkRequest request(QUrl(buildUrl("/operate-mode")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["operate_mode"] = operate ? "OPERATE" : "STANDBY";
    QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkReply *reply = m_networkManager->put(request, data);
    connect(reply, &QNetworkReply::finished, reply, [reply, operate, this]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            OperatingState newState = operate ? StateOperate : StateStandby;
            if (m_operatingState != newState) {
                m_operatingState = newState;
                emit operatingStateChanged(newState);
            }
        } else {
            qWarning() << "RFKit: Failed to set operate mode:" << reply->errorString();
        }
    });
}

void RFKitClient::setAntenna(int antennaNumber) {
    QNetworkRequest request(QUrl(buildUrl("/antennas/active")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["number"] = antennaNumber;
    QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);

    QNetworkReply *reply = m_networkManager->put(request, data);
    connect(reply, &QNetworkReply::finished, reply, [reply, this]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            // Refresh active antenna on next poll
            pollActiveAntenna();
        } else {
            qWarning() << "RFKit: Failed to set antenna:" << reply->errorString();
        }
    });
}

void RFKitClient::resetError() {
    QNetworkRequest request(QUrl(buildUrl("/error/reset")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QNetworkReply *reply = m_networkManager->post(request, QByteArray("{}"));
    connect(reply, &QNetworkReply::finished, reply, [reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            qWarning() << "RFKit: Failed to reset error:" << reply->errorString();
        }
    });
}

// ============== Polling ==============

void RFKitClient::onPollTimer() {
    if (m_state == Disconnected) {
        return;
    }

    pollPower();
    pollOperateMode();
    pollData();
    pollActiveAntenna();
}

void RFKitClient::pollPower() {
    QNetworkRequest request(QUrl(buildUrl("/power")));
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, reply, [reply, this]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            m_consecutiveErrors = 0;
            if (m_state == Connecting) {
                setState(Connected);
            }
            handlePowerResponse(reply->readAll());
        } else {
            m_consecutiveErrors++;
            if (m_consecutiveErrors >= MAX_CONSECUTIVE_ERRORS && m_state == Connected) {
                qWarning() << "RFKit: Lost connection after" << m_consecutiveErrors << "errors";
                emit errorOccurred(reply->errorString());
                disconnectFromHost();
            }
        }
    });
}

void RFKitClient::pollOperateMode() {
    QNetworkRequest request(QUrl(buildUrl("/operate-mode")));
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, reply, [reply, this]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            handleOperateModeResponse(reply->readAll());
        }
    });
}

void RFKitClient::pollData() {
    QNetworkRequest request(QUrl(buildUrl("/data")));
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, reply, [reply, this]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            handleDataResponse(reply->readAll());
        }
    });
}

void RFKitClient::pollAntennas() {
    QNetworkRequest request(QUrl(buildUrl("/antennas")));
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, reply, [reply, this]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            handleAntennasResponse(reply->readAll());
        }
    });
}

void RFKitClient::pollActiveAntenna() {
    QNetworkRequest request(QUrl(buildUrl("/antennas/active")));
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, reply, [reply, this]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            handleActiveAntennaResponse(reply->readAll());
        }
    });
}

void RFKitClient::pollInfo() {
    QNetworkRequest request(QUrl(buildUrl("/info")));
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, reply, [reply, this]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            m_consecutiveErrors = 0;
            handleInfoResponse(reply->readAll());
            if (m_state == Connecting) {
                setState(Connected);
                // Fetch full antenna list on initial connection
                pollAntennas();
            }
        } else {
            qWarning() << "RFKit: Connection failed:" << reply->errorString();
            emit errorOccurred(reply->errorString());
            if (m_state == Connecting) {
                setState(Disconnected);
            }
        }
    });
}

// ============== JSON Response Parsers ==============

void RFKitClient::handlePowerResponse(const QByteArray &data) {
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        return;
    }

    QJsonObject obj = doc.object();
    bool changed = false;

    // Forward power: { "forward": { "unit": "W", "value": 1200, "max_value": 1500 } }
    if (obj.contains("forward")) {
        QJsonObject fwd = obj["forward"].toObject();
        double val = fwd["value"].toDouble();
        double maxVal = fwd["max_value"].toDouble(1500.0);
        if (m_forwardPower != val || m_maxForwardPower != maxVal) {
            m_forwardPower = val;
            m_maxForwardPower = maxVal;
            changed = true;
        }
    }

    // Reflected power
    if (obj.contains("reflected")) {
        QJsonObject ref = obj["reflected"].toObject();
        double val = ref["value"].toDouble();
        if (m_reflectedPower != val) {
            m_reflectedPower = val;
            changed = true;
        }
    }

    // SWR
    if (obj.contains("swr")) {
        QJsonObject swrObj = obj["swr"].toObject();
        double val = swrObj["value"].toDouble(1.0);
        if (val < 1.0)
            val = 1.0;
        if (m_swr != val) {
            m_swr = val;
            changed = true;
        }
    }

    if (changed) {
        emit powerChanged(m_forwardPower, m_reflectedPower, m_swr);
    }

    // Temperature
    if (obj.contains("temperature")) {
        QJsonObject tempObj = obj["temperature"].toObject();
        double val = tempObj["value"].toDouble();
        if (m_temperature != val) {
            m_temperature = val;
            emit temperatureChanged(val);
        }
    }

    // Voltage
    if (obj.contains("voltage")) {
        QJsonObject voltObj = obj["voltage"].toObject();
        double val = voltObj["value"].toDouble();
        if (m_voltage != val) {
            m_voltage = val;
            emit voltageChanged(val);
        }
    }

    // Current
    if (obj.contains("current")) {
        QJsonObject curObj = obj["current"].toObject();
        double val = curObj["value"].toDouble();
        if (m_current != val) {
            m_current = val;
            emit currentChanged(val);
        }
    }
}

void RFKitClient::handleOperateModeResponse(const QByteArray &data) {
    // Response is a quoted string: "OPERATE" or "STANDBY"
    QString mode = QString::fromUtf8(data).trimmed().remove('"');
    OperatingState newState = StateUnknown;
    if (mode == "OPERATE") {
        newState = StateOperate;
    } else if (mode == "STANDBY") {
        newState = StateStandby;
    }

    if (m_operatingState != newState) {
        m_operatingState = newState;
        emit operatingStateChanged(newState);
    }
}

void RFKitClient::handleDataResponse(const QByteArray &data) {
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        return;
    }

    QJsonObject obj = doc.object();

    // Band: { "band": { "unit": "", "value": "20m" } }
    if (obj.contains("band")) {
        QJsonObject bandObj = obj["band"].toObject();
        QString band = bandObj["value"].toString();
        if (m_band != band) {
            m_band = band;
            emit bandChanged(band);
        }
    }

    // Frequency: { "frequency": { "unit": "MHz", "value": 14.200 } }
    if (obj.contains("frequency")) {
        QJsonObject freqObj = obj["frequency"].toObject();
        double freqMhz = freqObj["value"].toDouble();
        qint64 freqHz = static_cast<qint64>(freqMhz * 1000000.0);
        if (m_frequency != freqHz) {
            m_frequency = freqHz;
        }
    }

    // Status
    if (obj.contains("status")) {
        QString status = obj["status"].toString();
        if (m_status != status) {
            m_status = status;
            emit statusChanged(status);
        }
    }
}

void RFKitClient::handleAntennasResponse(const QByteArray &data) {
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) {
        return;
    }

    QJsonArray arr = doc.array();
    QVector<AntennaInfo> antennas;
    for (const QJsonValue &val : arr) {
        QJsonObject obj = val.toObject();
        AntennaInfo info;
        info.id = obj["id"].toInt();
        info.number = obj["number"].toInt();
        info.name = obj["name"].toString();
        antennas.append(info);
    }

    if (m_antennas.size() != antennas.size()) {
        m_antennas = antennas;
        emit antennasUpdated();
    }
}

void RFKitClient::handleActiveAntennaResponse(const QByteArray &data) {
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        // Might be a plain string
        QString name = QString::fromUtf8(data).trimmed().remove('"');
        if (!name.isEmpty() && m_activeAntennaName != name) {
            m_activeAntennaName = name;
            emit antennaChanged(m_activeAntennaNumber, m_activeAntennaName);
        }
        return;
    }

    QJsonObject obj = doc.object();
    int number = obj["number"].toInt(m_activeAntennaNumber);
    QString name = obj["name"].toString(m_activeAntennaName);

    if (m_activeAntennaNumber != number || m_activeAntennaName != name) {
        m_activeAntennaNumber = number;
        m_activeAntennaName = name;
        emit antennaChanged(number, name);
    }
}

void RFKitClient::handleInfoResponse(const QByteArray &data) {
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        return;
    }

    QJsonObject obj = doc.object();
    QString name = obj["custom_device_name"].toString();
    if (name.isEmpty()) {
        name = obj["device"].toString();
    }

    QString version;
    if (obj.contains("software_version")) {
        QJsonObject sw = obj["software_version"].toObject();
        version = sw["GUI"].toString();
        if (version.isEmpty()) {
            version = sw["controller"].toString();
        }
    }

    if (m_deviceName != name || m_softwareVersion != version) {
        m_deviceName = name;
        m_softwareVersion = version;
        emit deviceInfoChanged(name, version);
    }
}
