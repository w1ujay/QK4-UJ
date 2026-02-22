#include "catserver.h"
#include "models/radiostate.h"
#include "settings/radiosettings.h"
#include "tcpclient.h"
#include <QDebug>
#include <QTimer>

CatServer::CatServer(RadioState *state, QObject *parent)
    : QObject(parent), m_server(new QTcpServer(this)), m_radioState(state) {
    connect(m_server, &QTcpServer::newConnection, this, &CatServer::onNewConnection);
}

CatServer::~CatServer() {
    stop();
}

void CatServer::setTcpClient(TcpClient *client) {
    m_tcpClient = client;
}

bool CatServer::start(quint16 port) {
    if (m_server->isListening()) {
        if (m_server->serverPort() == port) {
            return true;
        }
        stop();
    }

    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        emit errorOccurred(QString("Failed to start CAT server: %1").arg(m_server->errorString()));
        return false;
    }

    m_port = port;
    emit started(port);
    return true;
}

void CatServer::stop() {
    for (QTcpSocket *client : m_clients) {
        client->disconnectFromHost();
    }
    m_clients.clear();
    m_clientBuffers.clear();

    if (m_server->isListening()) {
        m_server->close();
        m_port = 0;
        emit stopped();
    }
}

bool CatServer::isListening() const {
    return m_server->isListening();
}

quint16 CatServer::port() const {
    return m_port;
}

int CatServer::clientCount() const {
    return m_clients.count();
}

void CatServer::onNewConnection() {
    while (m_server->hasPendingConnections()) {
        QTcpSocket *client = m_server->nextPendingConnection();
        m_clients.append(client);
        m_clientBuffers[client] = QByteArray();

        connect(client, &QTcpSocket::readyRead, this, &CatServer::onClientData);
        connect(client, &QTcpSocket::disconnected, this, &CatServer::onClientDisconnected);

        QString address = QString("%1:%2").arg(client->peerAddress().toString()).arg(client->peerPort());
        emit clientConnected(address);
    }
}

void CatServer::onClientData() {
    QTcpSocket *client = qobject_cast<QTcpSocket *>(sender());
    if (!client)
        return;

    m_clientBuffers[client].append(client->readAll());

    // K4 CAT commands are semicolon-terminated
    while (m_clientBuffers[client].contains(';')) {
        int idx = m_clientBuffers[client].indexOf(';');
        QString command = QString::fromUtf8(m_clientBuffers[client].left(idx + 1)).trimmed();
        m_clientBuffers[client].remove(0, idx + 1);

        if (!command.isEmpty()) {
            QString response = handleCommand(command);
            qDebug() << "CatServer:" << command << "->" << (response.isEmpty() ? "(forwarded)" : response);
            if (!response.isEmpty()) {
                client->write(response.toUtf8());
            }
        }
    }
}

void CatServer::onClientDisconnected() {
    QTcpSocket *client = qobject_cast<QTcpSocket *>(sender());
    if (!client)
        return;

    QString address = QString("%1:%2").arg(client->peerAddress().toString()).arg(client->peerPort());
    m_clients.removeOne(client);
    m_clientBuffers.remove(client);
    client->deleteLater();

    emit clientDisconnected(address);
}

QString CatServer::handleCommand(const QString &cmd) {
    // K4 CAT commands: 2-3 letter prefix, optional parameters, semicolon
    // GET commands have no parameters (e.g., "FA;", "MD;")
    // SET commands have parameters (e.g., "FA14074000;", "MD1;")

    QString command = cmd.trimmed();
    if (!command.endsWith(';')) {
        return QString(); // Invalid command
    }

    // Remove trailing semicolon for parsing
    command = command.left(command.length() - 1);

    if (command.isEmpty()) {
        return QString();
    }

    // Handle special commands where numbers are part of the command name
    // K2, K3, K40, PS - these need special handling before normal parsing
    if (command == "K2") {
        return "K22;"; // K2 extended mode level 2
    }
    if (command == "K3") {
        return "K31;"; // K3 extended mode level 1
    }
    if (command.startsWith("K2") || command.startsWith("K3") || command.startsWith("K4")) {
        // K22, K31, K40 etc - SET commands, silently acknowledge
        return QString();
    }
    if (command == "PS") {
        return "PS1;"; // Power on status
    }
    if (command == "RVM") {
        // Firmware revision - Front Panel version from RadioState
        QString fp = m_radioState->firmwareVersions().value("FP", "01.00");
        return QString("RVM%1;").arg(fp);
    }
    if (command == "RVD") {
        // DSP firmware revision from RadioState
        QString dsp = m_radioState->firmwareVersions().value("DSP", "01.00");
        return QString("RVD%1;").arg(dsp);
    }
    if (command.startsWith("PS")) {
        // PS0, PS1 - power control SET commands, silently acknowledge
        return QString();
    }

    // Extract command prefix (2-3 uppercase letters, plus optional '$' for Sub VFO commands)
    // K4 uses '$' suffix for Sub VFO: MD$, BW$, FA$, FB$, etc.
    QString prefix;
    QString args;
    for (int i = 0; i < command.length(); i++) {
        if (command[i].isLetter()) {
            prefix += command[i].toUpper();
        } else if (command[i] == '$' && prefix.length() >= 2) {
            // '$' is part of the command prefix (Sub VFO suffix)
            prefix += '$';
        } else {
            args = command.mid(i);
            break;
        }
    }

    // TX/RX commands (no args) - must be handled before the GET block
    // Audio disabled or CW/CW-R: forward directly to K4
    // Voice/Data with audio enabled: control audio input gate (VOX triggers TX from audio stream)
    if (prefix == "TX" && args.isEmpty()) {
        int mode = m_radioState->mode();
        if (!RadioSettings::instance()->audioEnabled() || mode == RadioState::CW || mode == RadioState::CW_R) {
            emit catCommandReceived(cmd);
        } else {
            emit pttRequested(true);
        }
        return QString();
    }
    if (prefix == "RX" && args.isEmpty()) {
        int mode = m_radioState->mode();
        if (!RadioSettings::instance()->audioEnabled() || mode == RadioState::CW || mode == RadioState::CW_R) {
            emit catCommandReceived(cmd);
        } else {
            emit pttRequested(false);
        }
        m_cwPending = 0;
        return QString();
    }

    // RU/RD/RC (RIT Up/Down/Clear) - no args, forward to K4 then query new state
    if ((prefix == "RU" || prefix == "RD" || prefix == "RC") && args.isEmpty()) {
        emit catCommandReceived(cmd);
        // K4 doesn't echo RIT changes — query for updated offset and on/off state
        emit catCommandReceived("RT;");
        emit catCommandReceived("RO;");
        return QString();
    }

    // Handle GET commands (no args) - respond from RadioState
    if (args.isEmpty()) {
        // VFO A frequency
        if (prefix == "FA") {
            return buildFrequencyResponse(m_radioState->frequency(), "FA");
        }
        // VFO B frequency
        if (prefix == "FB") {
            return buildFrequencyResponse(m_radioState->vfoB(), "FB");
        }
        // Mode (VFO A)
        if (prefix == "MD") {
            return buildModeResponse(m_radioState->mode());
        }
        // PTT state
        if (prefix == "TQ") {
            return QString("TQ%1;").arg(m_radioState->isTransmitting() ? 1 : 0);
        }
        // Split state
        if (prefix == "FT") {
            return QString("FT%1;").arg(m_radioState->splitEnabled() ? 1 : 0);
        }
        // RX VFO indicator
        if (prefix == "FR") {
            return "FR0;"; // Always VFO A for RX
        }
        // IF command - comprehensive status (Kenwood format)
        // Format:
        // IF[freq:11][step:5][±offset:6][rit:1][xit:1][bank:1][ch:2][tx:1][mode:1][vfo:1][scan:1][split:1][data:2];
        // Note: Must use string concatenation, NOT arg() with %10+ (Qt replaces %1 inside %10)
        if (prefix == "IF") {
            quint64 freq = m_radioState->frequency();
            int offset = m_radioState->ritXitOffset();
            int ritOn = m_radioState->ritEnabled() ? 1 : 0;
            int xitOn = m_radioState->xitEnabled() ? 1 : 0;
            int mode = m_radioState->mode();
            int tx = m_radioState->isTransmitting() ? 1 : 0;
            int split = m_radioState->splitEnabled() ? 1 : 0;

            // K4 mode for IF: use the raw mode code (1=LSB,2=USB,3=CW,etc)
            // If unknown, default to USB (2)
            int k4Mode = (mode >= 1 && mode <= 9) ? mode : 2;

            QString response = QStringLiteral("IF") + QString("%1").arg(freq, 11, 10, QChar('0')) // P1: freq (11)
                               + QStringLiteral("     ")                                          // P2: step (5 blanks)
                               + QString(offset >= 0 ? "+" : "-")                                 // P3: offset sign
                               + QString("%1").arg(qAbs(offset), 5, 10, QChar('0'))               // P3: offset (5)
                               + QString::number(ritOn)                                           // P4: RIT (1)
                               + QString::number(xitOn)                                           // P5: XIT (1)
                               + QStringLiteral("0")                                              // P6: bank (1)
                               + QStringLiteral("00")                                             // P7: chan (2)
                               + QString::number(tx)                                              // P8: TX (1)
                               + QString::number(k4Mode)                                          // P9: mode (1 digit!)
                               + QStringLiteral("0")                                              // P10: VFO (1)
                               + QStringLiteral("0")                                              // P11: scan (1)
                               + QString::number(split)                                           // P12: split (1)
                               + QStringLiteral("00")                                             // P13: data (2)
                               + QStringLiteral(";");
            return response;
        }
        // RIT offset
        if (prefix == "RO") {
            int offset = m_radioState->ritXitOffset();
            return QString("RO%1%2;").arg(offset >= 0 ? "+" : "-").arg(qAbs(offset), 4, 10, QChar('0'));
        }
        // RIT on/off
        if (prefix == "RT") {
            return QString("RT%1;").arg(m_radioState->ritEnabled() ? 1 : 0);
        }
        // XIT on/off
        if (prefix == "XT") {
            return QString("XT%1;").arg(m_radioState->xitEnabled() ? 1 : 0);
        }
        // RF power
        if (prefix == "PC") {
            return QString("PC%1;").arg(static_cast<int>(m_radioState->rfPower()), 3, 10, QChar('0'));
        }
        // AGC
        if (prefix == "GT") {
            int agc = static_cast<int>(m_radioState->agcSpeed());
            // K4 AGC: 0=off, 1=slow, 2=fast
            return QString("GT%1;").arg(agc, 3, 10, QChar('0'));
        }
        // Keyer speed
        if (prefix == "KS") {
            return QString("KS%1;").arg(m_radioState->keyerSpeed(), 3, 10, QChar('0'));
        }
        // Noise blanker
        if (prefix == "NB") {
            return QString("NB%1;").arg(m_radioState->noiseBlankerEnabled() ? 1 : 0);
        }
        // Noise reduction
        if (prefix == "NR") {
            return QString("NR%1;").arg(m_radioState->noiseReductionEnabled() ? 1 : 0);
        }
        // VOX
        if (prefix == "VX") {
            return QString("VX%1;").arg(m_radioState->voxEnabled() ? 1 : 0);
        }
        // Filter bandwidth
        if (prefix == "BW") {
            int bw = m_radioState->filterBandwidth();
            return QString("BW%1;").arg(bw, 4, 10, QChar('0'));
        }
        // ID - Radio identification
        if (prefix == "ID") {
            return "ID017;"; // K4 ID
        }
        // DT - Data sub-mode
        if (prefix == "DT") {
            return QString("DT%1;").arg(m_radioState->dataSubMode());
        }
        // OM - Option modules query
        if (prefix == "OM") {
            // Return option modules from RadioState if available, else default
            // Format: OM <options>; where options is a 12-char string with dashes for absent
            QString om = m_radioState->optionModules();
            if (om.isEmpty()) {
                om = "AP----------"; // Basic K4 with ATU and PA (12 chars)
            }
            return QString("OM %1;").arg(om); // Note: space after OM
        }
        // AI - Auto-information (transceive mode)
        // QK4 uses AI4 globally - report that, don't let external apps change it
        if (prefix == "AI") {
            return "AI4;";
        }
        // KY - Keyer buffer status read: KY0; = space available, KY1; = full
        if (prefix == "KY") {
            // K4 buffer holds ~60 chars; report full if we have 50+ pending
            return QString("KY%1;").arg(m_cwPending >= 50 ? 1 : 0);
        }
        // TB - Text buffer status: TBtaabb; where t=pending(0-9), aa=rx chars, bb=rx text
        if (prefix == "TB") {
            int pending = qBound(0, m_cwPending, 9);
            return QString("TB%100;").arg(pending);
        }
        // SB - Sub RX on/off
        if (prefix == "SB") {
            int subStatus = 0;
            if (m_radioState->diversityEnabled())
                subStatus = 3;
            else if (m_radioState->subReceiverEnabled())
                subStatus = 1;
            return QString("SB%1;").arg(subStatus);
        }
        // SM - S-meter reading
        if (prefix == "SM") {
            // S-meter value: 0000-0021 typical range
            // RadioState stores S-units (0-9 for S1-S9, higher for +dB)
            int smeter = static_cast<int>(m_radioState->sMeter());
            // Convert to K4 format (roughly 3 per S-unit)
            int k4Value = qBound(0, smeter * 3, 21);
            return QString("SM%1;").arg(k4Value, 4, 10, QChar('0'));
        }
        // PCX - Extended power reading
        if (prefix == "PCX") {
            // Format: PCnnnM; where nnn=power (3 digits), M=mode (H=high, L=low/QRP)
            int power = static_cast<int>(m_radioState->rfPower());
            QString mode = m_radioState->isQrpMode() ? "L" : "H";
            return QString("PC%1%2;").arg(power, 3, 10, QChar('0')).arg(mode);
        }
        // AG/AG$ - AF gain (controls QK4 local volume when audio enabled, forwarded to K4 when disabled)
        if (prefix == "AG" || prefix == "AG$") {
            if (RadioSettings::instance()->audioEnabled()) {
                int vol = (prefix == "AG") ? m_mainVolume : m_subVolume;
                return QString("%1%2;").arg(prefix).arg(vol, 3, 10, QChar('0'));
            }
            // Audio disabled — forward to K4
            emit catCommandReceived(cmd);
            return QString();
        }
        // SQ - Squelch level
        if (prefix == "SQ") {
            // Squelch 0-255, but we don't track this - return 0
            return "SQ000;";
        }
        // FW - Filter width (bandwidth)
        if (prefix == "FW") {
            int bw = m_radioState->filterBandwidth();
            return QString("FW%1;").arg(bw, 8, 10, QChar('0'));
        }
        // TM - TX metering (polled during TX)
        if (prefix == "TM") {
            // Return TX meter values - format varies by mode
            // For now return a basic response
            return "TM0;";
        }
        // MD$ - Sub VFO mode
        if (prefix == "MD$") {
            QString resp = buildModeResponse(m_radioState->modeB());
            // Insert '$' → "MD3;" becomes "MD$3;"
            resp.insert(2, '$');
            return resp;
        }
        // DV - Diversity mode
        if (prefix == "DV") {
            return QString("DV%1;").arg(m_radioState->diversityEnabled() ? 1 : 0);
        }
        // BW$ - Sub filter bandwidth
        if (prefix == "BW$") {
            int bwB = m_radioState->filterBandwidthB();
            return QString("BW$%1;").arg(bwB, 4, 10, QChar('0'));
        }
        // PB - Playback status
        if (prefix == "PB") {
            return "PB0;";
        }
        // Unrecognized GET command - don't forward, just return empty
        return QString();
    }

    // AI SET commands - silently ignore, don't let external apps change our AI4 mode
    if (prefix == "AI") {
        return QString();
    }

    // KY - Keyer CW text: track pending chars and forward to K4
    if (prefix == "KY") {
        if (args == "0") {
            // KY0 = abort CW
            m_cwPending = 0;
        } else {
            // KY <text> — text starts after space, strip trailing semicolon from cmd
            QString text = args.trimmed();
            m_cwPending = text.length();
            // Start a timer to clear pending count (K4 sends at ~WPM rate)
            // Approximate: at 29 WPM, ~5 chars/sec → clear after text.length()/5 seconds
            int wpm = m_radioState->keyerSpeed();
            int charsPerSec = qMax(1, wpm / 6);
            int clearMs = qMax(500, (m_cwPending * 1000) / charsPerSec);
            QTimer::singleShot(clearMs, this, [this]() { m_cwPending = 0; });
        }
        emit catCommandReceived(cmd);
        return QString();
    }

    // AG/AG$ SET - control QK4 local volume when audio enabled, forward to K4 when disabled
    // K4 AG range is 000-060 (not 255)
    if (prefix == "AG" || prefix == "AG$") {
        if (RadioSettings::instance()->audioEnabled()) {
            int gain = qBound(0, args.toInt(), 60);
            int percent = (gain * 100 + 30) / 60; // Map 0-60 → 0-100
            if (prefix == "AG") {
                m_mainVolume = gain;
                emit volumeRequested(percent);
            } else {
                m_subVolume = gain;
                emit subVolumeRequested(percent);
            }
            return QString();
        }
        // Audio disabled — fall through to forward to K4
    }

    // SET commands (have args) - forward to real K4
    // Commands like FA14074000;, MD1;, etc.
    emit catCommandReceived(cmd);

    // Optimistically update RadioState for freq/mode/split so the next poll returns the new value
    // (K4 echo may take 50-100ms, but N1MM polls immediately after SET)
    // Only update freq/mode/split — other commands could trigger audio flushes or side effects
    if (prefix == "FA" || prefix == "FB" || prefix == "MD" || prefix == "MD$" || prefix == "FT" || prefix == "RT" ||
        prefix == "XT" || prefix == "RO") {
        m_radioState->parseCATCommand(cmd);
    }

    // Most SET commands echo the new value
    return QString();
}

QString CatServer::buildFrequencyResponse(quint64 freq, const QString &prefix) const {
    // K4 frequency format: 11 digits with leading zeros
    return QString("%1%2;").arg(prefix).arg(freq, 11, 10, QChar('0'));
}

QString CatServer::buildModeResponse(int mode) const {
    // RadioState::Mode enum values match K4 mode codes directly:
    // 1=LSB, 2=USB, 3=CW, 4=FM, 5=AM, 6=DATA, 7=CW-R, 9=DATA-R, 0=Unknown
    int k4Mode = (mode >= 1 && mode <= 9) ? mode : 2; // Default USB if unknown
    return QString("MD%1;").arg(k4Mode);
}
