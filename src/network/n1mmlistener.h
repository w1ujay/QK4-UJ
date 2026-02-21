#ifndef N1MMLISTENER_H
#define N1MMLISTENER_H

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>

struct SpotData {
    QString callsign;
    qint64 frequencyHz;
    QString mode;
    QString status;
    QDateTime timestamp;
    QElapsedTimer age;
};

class N1mmListener : public QObject {
    Q_OBJECT

public:
    explicit N1mmListener(QObject *parent = nullptr);
    ~N1mmListener();

    bool start(quint16 port = 12060);
    void stop();
    bool isListening() const;

    void setExpiryMinutes(int minutes);
    int expiryMinutes() const { return m_expiryMinutes; }

    QList<SpotData> activeSpots() const { return m_spots.values(); }
    void clearSpots();

signals:
    void spotReceived(const SpotData &spot);
    void spotRemoved(const QString &callsign);
    void listenError(const QString &error);

private:
    void processDatagram(const QByteArray &data);
    void expireOldSpots();

    QUdpSocket *m_socket = nullptr;
    QTimer *m_expiryTimer;
    QHash<QString, SpotData> m_spots;
    int m_expiryMinutes = 10;
};

#endif // N1MMLISTENER_H
