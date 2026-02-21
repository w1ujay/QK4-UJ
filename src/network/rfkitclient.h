#ifndef RFKITCLIENT_H
#define RFKITCLIENT_H

#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QTimer>

class RFKitClient : public QObject {
    Q_OBJECT

public:
    enum ConnectionState { Disconnected, Connecting, Connected };
    Q_ENUM(ConnectionState)

    enum OperatingState { StateUnknown = -1, StateStandby = 0, StateOperate = 1 };
    Q_ENUM(OperatingState)

    struct AntennaInfo {
        int id = 0;
        int number = 0;
        QString name;
    };

    explicit RFKitClient(QObject *parent = nullptr);
    ~RFKitClient();

    void connectToHost(const QString &host, quint16 port);
    void disconnectFromHost();
    bool isConnected() const;
    ConnectionState connectionState() const;

    void startPolling(int intervalMs);
    void stopPolling();

    // Commands (PUT/POST requests)
    void setOperateMode(bool operate);
    void setAntenna(int antennaNumber);
    void resetError();

    // State getters
    QString deviceName() const { return m_deviceName; }
    QString softwareVersion() const { return m_softwareVersion; }
    double forwardPower() const { return m_forwardPower; }
    double maxForwardPower() const { return m_maxForwardPower; }
    double reflectedPower() const { return m_reflectedPower; }
    double swr() const { return m_swr; }
    double temperature() const { return m_temperature; }
    double voltage() const { return m_voltage; }
    double current() const { return m_current; }
    OperatingState operatingState() const { return m_operatingState; }
    QString band() const { return m_band; }
    qint64 frequency() const { return m_frequency; }
    QString status() const { return m_status; }
    int activeAntennaNumber() const { return m_activeAntennaNumber; }
    QString activeAntennaName() const { return m_activeAntennaName; }
    QVector<AntennaInfo> antennas() const { return m_antennas; }

signals:
    void stateChanged(ConnectionState state);
    void connected();
    void disconnected();
    void errorOccurred(const QString &error);

    void powerChanged(double forward, double reflected, double swr);
    void temperatureChanged(double tempC);
    void voltageChanged(double voltage);
    void currentChanged(double current);
    void operatingStateChanged(OperatingState state);
    void antennaChanged(int number, const QString &name);
    void antennasUpdated();
    void bandChanged(const QString &band);
    void statusChanged(const QString &status);
    void deviceInfoChanged(const QString &name, const QString &version);

private slots:
    void onPollTimer();

private:
    void setState(ConnectionState state);
    QString buildUrl(const QString &endpoint) const;

    // Poll endpoint handlers
    void pollPower();
    void pollOperateMode();
    void pollData();
    void pollAntennas();
    void pollActiveAntenna();
    void pollInfo();

    // JSON response parsers
    void handlePowerResponse(const QByteArray &data);
    void handleOperateModeResponse(const QByteArray &data);
    void handleDataResponse(const QByteArray &data);
    void handleAntennasResponse(const QByteArray &data);
    void handleActiveAntennaResponse(const QByteArray &data);
    void handleInfoResponse(const QByteArray &data);

    QNetworkAccessManager *m_networkManager;
    QTimer *m_pollTimer;
    int m_consecutiveErrors = 0;

    QString m_host;
    quint16 m_port = 8080;
    ConnectionState m_state = Disconnected;

    // Cached state values
    QString m_deviceName;
    QString m_softwareVersion;
    double m_forwardPower = 0.0;
    double m_maxForwardPower = 1500.0;
    double m_reflectedPower = 0.0;
    double m_swr = 1.0;
    double m_temperature = 0.0;
    double m_voltage = 0.0;
    double m_current = 0.0;
    OperatingState m_operatingState = StateUnknown;
    QString m_band;
    qint64 m_frequency = 0;
    QString m_status;
    int m_activeAntennaNumber = 0;
    QString m_activeAntennaName;
    QVector<AntennaInfo> m_antennas;

    static constexpr int MAX_CONSECUTIVE_ERRORS = 3;
};

#endif // RFKITCLIENT_H
