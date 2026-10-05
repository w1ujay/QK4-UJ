#ifndef ULANZISERVER_H
#define ULANZISERVER_H

#include <QByteArray>
#include <QObject>
#include <QString>
#include <array>

class QTimer;
class QTcpServer;
class QTcpSocket;

/// One decoded line of the Ulanzi plugin protocol. See plugins/ulanzi/README.md for the wire format.
struct UlanziEvent {
    enum class Type { Invalid, Rotate, Dial, Button, Ptt };
    Type type = Type::Invalid;
    int steps = 0;       // Rotate: +1 / -1, one detent
    bool hold = false;   // Rotate: the dial is held down while turning
    int slot = 0;        // Button: 1..BUTTON_COUNT
    bool down = false;   // Dial / Button / Ptt
    bool cancel = false; // Dial / Button release only: not a press — no tap, no deferred hold
};

/**
 * @brief Localhost server for the QK4 Ulanzi Studio plugin (Ulanzi D100H dial).
 *
 * The plugin is a thin relay: it forwards raw presses, releases and detents as newline-delimited
 * JSON. Everything with timing in it — tap versus hold — is decided here, so the plugin stays dumb
 * and any other host can speak the same protocol.
 */
class UlanziServer : public QObject {
    Q_OBJECT

public:
    static constexpr quint16 DEFAULT_PORT = 9410;
    // QK4 has no hold timing of its own to reuse — the KPOD decides hold in its own hardware.
    static constexpr int HOLD_MS = 500;
    static constexpr int MAX_LINE_BYTES = 4096;
    static constexpr int BUTTON_COUNT = 7;

    explicit UlanziServer(QObject *parent = nullptr);
    ~UlanziServer() override;

    /// Listen on 127.0.0.1:port. Already listening on that port is a no-op; another port restarts.
    /// On failure: false, errorOccurred("Port N unavailable: <reason>"), and lastError() keeps it.
    bool start(quint16 port);
    /// Drop the client (releasing PTT first) and close the listener.
    void stop();
    bool isListening() const;
    bool hasClient() const;
    quint16 port() const; // the actual listening port; 0 when not listening
    QString lastError() const;

    /// Tests shorten the hold threshold; production uses HOLD_MS.
    void setHoldMs(int ms);

    /// Pure: one line (without its newline) to an event. Type::Invalid for anything outside the protocol.
    static UlanziEvent parseLine(const QByteArray &line);

    /// Apply one decoded event. The socket path calls this for every valid line; tests call it directly.
    void handleEvent(const UlanziEvent &event);

signals:
    void rotated(int steps, bool hold);
    void buttonTapped(int slot); // 1..BUTTON_COUNT
    void buttonHeld(int slot);
    void dialTapped();
    void dialHeld();
    void pttChanged(bool down);
    void clientConnectedChanged(bool connected);
    void errorOccurred(const QString &message);
    void started(quint16 port);
    void stopped();

private:
    // m_keys[DIAL_KEY] is the dial press; m_keys[1..BUTTON_COUNT] are the buttons.
    static constexpr int DIAL_KEY = 0;
    static constexpr qint64 SOCKET_READ_BUFFER_BYTES = 64 * 1024;

    struct KeyState {
        bool down = false;
        bool held = false;      // past the hold threshold (buttons: hold already emitted)
        bool cancelled = false; // turned while held: a VFO B gesture, not a press
        QTimer *timer = nullptr;
    };

    void pressKey(int key, bool down, bool cancel);
    void onHoldTimeout(int key);
    void releaseInputs();
    void onNewConnection();
    void onReadyRead();
    void processLine(const QByteArray &rawLine);
    void dropClient();

    std::array<KeyState, BUTTON_COUNT + 1> m_keys;
    bool m_pttDown = false;
    int m_holdMs = HOLD_MS;
    QTcpServer *m_server;
    QTcpSocket *m_client = nullptr; // one at a time; a new connection replaces it
    QByteArray m_buffer;            // the current line so far, never more than MAX_LINE_BYTES
    QString m_lastError;
};

#endif // ULANZISERVER_H
