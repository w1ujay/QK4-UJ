// WHY: CatServer reads a fixed set of RadioState getters to build CAT
// responses for WSJT-X / MacLoggerDX. Those getters are a frozen public
// API contract — see docs/radiostate-catserver-api-contract.md for the
// list and the rules. The contract survived the 2026-04 RadioState
// subsystem decomposition unchanged and must continue to. The regression
// gate is tests/test_catserver.cpp, pinned in CI.
#include "catserver.h"
#include "catframes.h"
#include "catpushbroadcaster.h"
#include "models/radiostate.h"
#include "settings/radiosettings.h"
#include "protocol.h"
#include "tcpclient.h"

#include <QLoggingCategory>
#include <QTimer>

Q_LOGGING_CATEGORY(netCat, "net.cat")

CatServer::CatServer(RadioState *state, QObject *parent)
    : QObject(parent), m_server(new QTcpServer(this)), m_radioState(state) {
    m_broadcaster = new CatPushBroadcaster(state, this);
    connect(m_server, &QTcpServer::newConnection, this, &CatServer::onNewConnection);
    // WHY one restartable timer: a timer per KY message let a short message's timer clear the
    // count for a longer one sent after it.
    m_cwDrainTimer = new QTimer(this);
    m_cwDrainTimer->setSingleShot(true);
    connect(m_cwDrainTimer, &QTimer::timeout, this, [this]() { m_cwPending = 0; });
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

    m_port = m_server->serverPort(); // use actual port (may differ from requested if 0)
    qCInfo(netCat) << "CAT server listening on port" << m_port;
    emit started(port);
    return true;
}

void CatServer::stop() {
    // Unkey before tearing the listener down. Disabling the CAT server while a client is
    // transmitting used to leave the mic gate open with no client left to close it.
    releasePttIfOwner(m_pttOwner);

    const auto sockets = m_clients.keys();
    for (QTcpSocket *client : sockets) {
        m_broadcaster->removeClient(client);
        client->disconnectFromHost();
    }
    m_clients.clear();
    m_pendingRadioQueries.clear();

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
        m_clients.insert(client, ClientState{});
        m_broadcaster->addClient(client);

        connect(client, &QTcpSocket::readyRead, this, [this, client]() {
            QByteArray &buffer = m_clients[client].buffer;
            buffer.append(client->readAll());

            // Protect against unbounded buffer growth from misbehaving clients
            if (buffer.size() > K4Protocol::MAX_BUFFER_SIZE) {
                qCWarning(netCat) << "CAT client buffer overflow from" << client->peerAddress().toString()
                                  << "- disconnecting";
                client->disconnectFromHost();
                return;
            }

            // K4 CAT commands are semicolon-terminated — single-pass offset parsing
            int offset = 0;
            int idx;
            while ((idx = buffer.indexOf(';', offset)) != -1) {
                QString command = QString::fromUtf8(buffer.constData() + offset, idx - offset + 1).trimmed();
                offset = idx + 1;

                if (!command.isEmpty()) {
                    qCDebug(netCat) << "RX <-" << client->peerPort() << command;
                    QByteArray response = handleCommand(command, client);
                    if (!response.isEmpty()) {
                        qCDebug(netCat) << "TX ->" << client->peerPort() << response;
                        client->write(response);
                    } else {
                        qCDebug(netCat) << "   (no response)" << client->peerPort() << command;
                    }
                }
            }
            // Keep only unprocessed remainder
            if (offset > 0) {
                buffer.remove(0, offset);
            }
        });

        connect(client, &QTcpSocket::disconnected, this, [this, client]() {
            QString address = QString("%1:%2").arg(client->peerAddress().toString()).arg(client->peerPort());
            qCInfo(netCat) << "CAT client disconnected:" << address;
            // A client that crashes or is killed mid-transmission never sends RX;. Without this
            // the mic gate stayed open and the K4 kept transmitting until the operator pressed
            // Escape.
            releasePttIfOwner(client);
            m_broadcaster->removeClient(client);
            m_clients.remove(client);
            client->deleteLater();

            emit clientDisconnected(address);
        });

        QString address = QString("%1:%2").arg(client->peerAddress().toString()).arg(client->peerPort());
        qCInfo(netCat) << "CAT client connected:" << address;
        emit clientConnected(address);
    }
}

void CatServer::askRadio(const QString &key, QTcpSocket *client) {
    PendingRadioQuery &pending = m_pendingRadioQueries[key];
    pending.waiters.append(client);
    if (pending.waiters.size() > 1)
        return;

    pending.generation = ++m_radioQueryGeneration;
    const quint64 generation = pending.generation;
    QTimer::singleShot(kRadioQueryTimeoutMs, this, [this, key, generation]() {
        auto it = m_pendingRadioQueries.find(key);
        if (it != m_pendingRadioQueries.end() && it->generation == generation) {
            qCDebug(netCat) << "   radio did not answer" << key << "- dropping" << it->waiters.size() << "waiter(s)";
            m_pendingRadioQueries.erase(it);
        }
    });
    qCDebug(netCat) << "   asking the radio:" << key;
    emit radioQueryRequested(key + ';');
}

void CatServer::onRadioResponse(const QString &response) {
    if (m_pendingRadioQueries.isEmpty())
        return;
    const QStringList frames = response.split(';', Qt::SkipEmptyParts);
    for (const QString &frame : frames) {
        // AG018 answers "AG"; AG$018 answers "AG$". The character after the key decides, so the
        // Main reply can never satisfy a Sub query or the reverse.
        for (auto it = m_pendingRadioQueries.begin(); it != m_pendingRadioQueries.end();) {
            const QString &key = it.key();
            const bool matches = frame.startsWith(key) && (key.endsWith('$') || frame.mid(key.size(), 1) != "$");
            if (!matches) {
                ++it;
                continue;
            }
            const QByteArray reply = (frame + ';').toUtf8();
            for (const QPointer<QTcpSocket> &client : std::as_const(it->waiters)) {
                if (client && m_clients.contains(client)) {
                    qCDebug(netCat) << "TX ->" << client->peerPort() << reply << "(from the radio)";
                    client->write(reply);
                }
            }
            it = m_pendingRadioQueries.erase(it);
        }
    }
}

void CatServer::releasePttIfOwner(QTcpSocket *client) {
    if (!client || m_pttOwner != client) {
        return;
    }
    qCInfo(netCat) << "CAT client that asserted PTT is gone - releasing";
    m_pttOwner = nullptr;
    if (m_pttOwnerForwarded) {
        m_pttOwnerForwarded = false;
        emit catCommandReceived(QStringLiteral("RX;"));
    } else {
        emit pttRequested(false);
    }
}

QByteArray CatServer::handleCommand(const QString &cmd, QTcpSocket *client) {
    // K4 CAT commands: 2-3 letter prefix, optional parameters, semicolon
    // GET commands have no parameters (e.g., "FA;", "MD;")
    // SET commands have parameters (e.g., "FA14074000;", "MD1;")

    QString command = cmd.trimmed();
    if (!command.endsWith(';')) {
        return QByteArray(); // Invalid command
    }

    // Remove trailing semicolon for parsing
    command = command.left(command.length() - 1);

    if (command.isEmpty()) {
        return QByteArray();
    }

    // Handle special commands where numbers are part of the command name
    // K2, K3, K40, PS - these need special handling before normal parsing
    if (command == "K2") {
        return QByteArray("K22;"); // K2 extended mode level 2
    }
    if (command == "K3") {
        return QByteArray("K31;"); // K3 extended mode level 1
    }
    if (command.startsWith("K2") || command.startsWith("K3") || command.startsWith("K4")) {
        // K22, K31, K40 etc - SET commands, silently acknowledge
        return QByteArray();
    }
    if (command == "PS") {
        return QByteArray("PS1;"); // Power on status
    }
    if (command == "RVM") {
        // Firmware revision - Front Panel version from RadioState
        QString fp = m_radioState->firmwareVersions().value("FP", "01.00");
        return QString("RVM%1;").arg(fp).toUtf8();
    }
    if (command == "RVD") {
        // DSP firmware revision from RadioState
        QString dsp = m_radioState->firmwareVersions().value("DSP", "01.00");
        return QString("RVD%1;").arg(dsp).toUtf8();
    }
    if (command.startsWith("PS")) {
        // PS0, PS1 - power control SET commands, silently acknowledge
        return QByteArray();
    }

    // Extract command prefix (2-3 uppercase letters, plus an optional '$' Sub-VFO
    // suffix). The K4 uses '$' to address the sub receiver: MD$, BW$, RG$, AG$.
    // Folding '$' into the prefix keeps those distinguishable from a SET value.
    QString prefix;
    QString args;
    for (int i = 0; i < command.length(); i++) {
        if (command[i].isLetter()) {
            prefix += command[i].toUpper();
        } else if (command[i] == '$' && prefix.length() >= 2) {
            prefix += '$';
        } else {
            args = command.mid(i);
            break;
        }
    }

    // TX;, RX; and the TX/; toggle — when QK4 owns the audio path, these gate the
    // audio input rather than keying the K4 directly (the audio stream itself
    // triggers K4 TX). CW/CW-R always forward: keying must reach the radio, and so
    // must everything when QK4's audio is disabled. A forwarded toggle is resolved
    // to TX; or RX; here from the radio's current TX state.
    const bool txToggle = (prefix == "TX" && args == "/");
    if ((prefix == "TX" || prefix == "RX") && (args.isEmpty() || txToggle)) {
        const bool key = txToggle ? !m_radioState->isTransmitting() : (prefix == "TX");
        const int mode = m_radioState->mode();
        const bool forward =
            !RadioSettings::instance()->audioEnabled() || mode == RadioState::CW || mode == RadioState::CW_R;
        // Remember who keyed, and how, so the transmission can be ended if that client vanishes.
        m_pttOwner = key ? client : nullptr;
        m_pttOwnerForwarded = key && forward;
        if (forward) {
            emit catCommandReceived(key ? QStringLiteral("TX;") : QStringLiteral("RX;"));
        } else {
            emit pttRequested(key);
        }
        if (!key) {
            m_cwPending = 0;
            m_cwDrainTimer->stop();
        }
        return QByteArray();
    }

    // RU/RD/RC (RIT up/down/clear) — the K4 doesn't echo RIT changes, so forward
    // and then re-query offset and on/off state so the cache stays truthful.
    if (prefix == "RU" || prefix == "RD" || prefix == "RC") {
        emit catCommandReceived(cmd);
        emit catCommandReceived(QStringLiteral("RT;"));
        emit catCommandReceived(QStringLiteral("RO;"));
        return QByteArray();
    }

    // UP/DN/UPB/DNB (VFO step) — forward verbatim.
    if ((prefix == "UP" || prefix == "DN" || prefix == "UPB" || prefix == "DNB") && args.isEmpty()) {
        emit catCommandReceived(cmd);
        return QByteArray();
    }

    // RG+/RG-/RG/ and the RG$ sub-receiver variants (K4 firmware 2.x+). These
    // suffixes look like SET values to the parser, so they're handled here.
    // Optimistically apply, then re-query the K4 for the authoritative value.
    if ((prefix == "RG" || prefix == "RG$") && (args == "+" || args == "-" || args == "/")) {
        const bool sub = (prefix == "RG$");
        const int current = sub ? m_radioState->rfGainB() : m_radioState->rfGain();
        int &lastGain = sub ? m_lastRfGainB : m_lastRfGain;

        emit catCommandReceived(cmd);

        int next = current;
        if (args == "+") {
            next = qMax(0, current - 1);
        } else if (args == "-") {
            next = qMin(60, current + 1);
        } else if (current > 0) {
            lastGain = current;
            next = 0;
        } else {
            next = lastGain > 0 ? lastGain : 20;
        }

        if (sub) {
            m_radioState->setRfGainB(next);
            emit catCommandReceived(QStringLiteral("RG$;"));
        } else {
            m_radioState->setRfGain(next);
            emit catCommandReceived(QStringLiteral("RG;"));
        }
        return QByteArray();
    }

    // Handle GET commands (no args) - respond from RadioState
    if (args.isEmpty()) {
        if (prefix == "FA") {
            return CatFrames::frequencyA(m_radioState->frequency());
        }
        if (prefix == "FB") {
            return CatFrames::frequencyB(m_radioState->vfoB());
        }
        if (prefix == "MD") {
            return CatFrames::modeA(m_radioState->mode());
        }
        if (prefix == "TQ") {
            return CatFrames::ptt(m_radioState->isTransmitting());
        }
        if (prefix == "FT") {
            return CatFrames::split(m_radioState->splitEnabled());
        }
        if (prefix == "FR") {
            return QByteArray("FR0;"); // Always VFO A for RX
        }
        if (prefix == "IF") {
            return CatFrames::ifFrame(*m_radioState);
        }
        if (prefix == "RO") {
            return CatFrames::ritOffset(m_radioState->ritXitOffset());
        }
        if (prefix == "RT") {
            return CatFrames::ritEnabled(m_radioState->ritEnabled());
        }
        if (prefix == "XT") {
            return CatFrames::xitEnabled(m_radioState->xitEnabled());
        }
        if (prefix == "PC") {
            return CatFrames::rfPower(m_radioState->rfPower(), m_radioState->powerRange());
        }
        if (prefix == "GT") {
            return CatFrames::agcSpeed(static_cast<int>(m_radioState->agcSpeed()));
        }
        if (prefix == "KS") {
            return CatFrames::keyerSpeed(m_radioState->keyerSpeed());
        }
        if (prefix == "NB") {
            return CatFrames::setNoiseBlanker(m_radioState->noiseBlankerLevel(), m_radioState->noiseBlankerEnabled(),
                                              m_radioState->noiseBlankerFilterWidth());
        }
        if (prefix == "NR") {
            return CatFrames::noiseReduction(m_radioState->noiseReductionLevel(),
                                             m_radioState->noiseReductionEnabled());
        }
        if (prefix == "VX") {
            return CatFrames::vox(*m_radioState);
        }
        if (prefix == "BW") {
            const int bw = m_radioState->filterBandwidth();
            return bw < 0 ? QByteArray() : CatFrames::setFilterBandwidth(bw);
        }
        if (prefix == "ID") {
            return QByteArray("ID017;"); // K4 ID
        }
        if (prefix == "DT") {
            const int dt = m_radioState->dataSubMode();
            return dt < 0 ? QByteArray() : CatFrames::dataSubMode(dt);
        }
        if (prefix == "OM") {
            QString om = m_radioState->optionModules();
            if (om.isEmpty()) {
                om = "AP----------"; // Basic K4 with ATU and PA (12 chars)
            }
            return QString("OM %1;").arg(om).toUtf8(); // Note: space after OM
        }
        if (prefix == "AI") {
            return CatFrames::aiMode(m_broadcaster->clientAiMode(client));
        }
        // Sub-RX queries. The prefix extractor folds '$' into the prefix, so "MD$;" arrives as "MD$".
        if (prefix == "MD$") {
            return CatFrames::modeB(m_radioState->modeB());
        }
        if (prefix == "SQ$") {
            const int sq = m_radioState->squelchLevelB();
            return sq < 0 ? QByteArray() : CatFrames::squelch(sq, true);
        }
        if (prefix == "RG$") {
            const int rg = m_radioState->rfGainB();
            return rg < 0 ? QByteArray() : CatFrames::rfGain(rg, true);
        }
        if (prefix == "RO$") {
            return CatFrames::subRx(CatFrames::ritOffset(m_radioState->ritXitOffsetB()));
        }
        if (prefix == "RT$") {
            return CatFrames::subRx(CatFrames::ritEnabled(m_radioState->ritEnabledB()));
        }
        if (prefix == "GT$") {
            return CatFrames::subRx(CatFrames::agcSpeed(static_cast<int>(m_radioState->agcSpeedB())));
        }
        if (prefix == "NB$") {
            return CatFrames::subRx(CatFrames::setNoiseBlanker(m_radioState->noiseBlankerLevelB(),
                                                               m_radioState->noiseBlankerEnabledB(),
                                                               m_radioState->noiseBlankerFilterWidthB()));
        }
        if (prefix == "NR$") {
            return CatFrames::subRx(CatFrames::noiseReduction(m_radioState->noiseReductionLevelB(),
                                                              m_radioState->noiseReductionEnabledB()));
        }
        if (prefix == "BW$") {
            const int bw = m_radioState->filterBandwidthB();
            return bw < 0 ? QByteArray() : CatFrames::subRx(CatFrames::setFilterBandwidth(bw));
        }
        if (prefix == "DT$") {
            const int dt = m_radioState->dataSubModeB();
            return dt < 0 ? QByteArray() : CatFrames::subRx(CatFrames::dataSubMode(dt));
        }
        // XT$ stays unanswered: the radio itself does not answer it.
        // KY GET — keyer buffer space: KY0; = room available, KY1; = full.
        // The K4's buffer holds ~60 characters; report full from 50 pending.
        if (prefix == "KY") {
            return QString("KY%1;").arg(m_cwPending >= 50 ? 1 : 0).toUtf8();
        }
        // TB — text buffer status: TBtaa; t=pending(0-9), aa=RX decode counts.
        // NOTE: built by concatenation, not QString::arg — "TB%100;" makes Qt read
        // "%10" as placeholder index 10 and silently drops a digit.
        if (prefix == "TB") {
            return QByteArray("TB") + QByteArray::number(qBound(0, m_cwPending, 9)) + "00;";
        }
        // SB — sub RX status: 0=off, 1=on, 3=on with the Sub mini-pan running. Diversity is DV, not
        // SB (docs/k4-protocol-quirks.md §12).
        if (prefix == "SB") {
            int subStatus = 0;
            if (m_radioState->subReceiverEnabled()) {
                subStatus = m_radioState->miniPanBEnabled() ? 3 : 1;
            }
            return QString("SB%1;").arg(subStatus).toUtf8();
        }
        if (prefix == "PB") {
            return QByteArray("PB0;"); // Playback idle
        }
        if (prefix == "DV") {
            return CatFrames::diversity(m_radioState->diversityEnabled());
        }
        if (prefix == "SM") {
            return CatFrames::sMeterMain(m_radioState->sMeter());
        }
        if (prefix == "PCX") {
            return CatFrames::rfPowerExtended(m_radioState->rfPower(), m_radioState->powerRange());
        }
        // AG/AG$ — while QK4 owns the audio path these report QK4's own volume.
        // WHY RadioSettings: the volume sliders persist every change there, including ones made
        // in the GUI or restored at startup, so it is the one place that is always current.
        if (prefix == "AG" || prefix == "AG$") {
            if (RadioSettings::instance()->audioEnabled()) {
                const int percent =
                    (prefix == "AG") ? RadioSettings::instance()->volume() : RadioSettings::instance()->subVolume();
                const int gain = (percent * 60 + 50) / 100; // 0-100 -> 0-60, rounded
                return QString("%1%2;").arg(prefix).arg(gain, 3, 10, QChar('0')).toUtf8();
            }
            // WHY ask: the radio does not report AF gain when the knob moves - measured on a K4D at
            // R40, turning it produced no AG traffic at all - so a stored value goes stale at the
            // first turn. Only the radio knows the current one.
            askRadio(prefix, client);
            return QByteArray();
        }
        if (prefix == "SQ") {
            // No answer until the radio has reported it - a made-up 000 is indistinguishable from
            // a real zero.
            const int sq = m_radioState->squelchLevel();
            return sq < 0 ? QByteArray() : CatFrames::squelch(sq, false);
        }
        if (prefix == "RG") {
            const int rg = m_radioState->rfGain();
            return rg < 0 ? QByteArray() : CatFrames::rfGain(rg, false);
        }
        if (prefix == "FW") {
            return CatFrames::filterWidthExtended(m_radioState->filterBandwidth());
        }
        if (prefix == "TM") {
            return CatFrames::txMeter(m_radioState->alcMeter(), m_radioState->compressionDb(),
                                      m_radioState->forwardPower(), m_radioState->swrMeter(),
                                      m_radioState->isQrpMode());
        }
    }

    // AI SET commands - update per-client AI mode subscription. K4 spec: no echo.
    if (prefix == "AI") {
        bool ok = false;
        int level = args.toInt(&ok);
        if (ok && (level == 0 || level == 1 || level == 2 || level == 4)) {
            m_broadcaster->setClientAiMode(client, level);
            qCDebug(netCat) << "   AI mode set to" << level << "for client" << client->peerPort();
        } else {
            qCDebug(netCat) << "   AI SET rejected (invalid level):" << cmd;
        }
        return QByteArray();
    }

    // TX/RX commands - control audio input gate for external app transmit.
    // Don't forward to K4 - the audio stream itself triggers K4 TX.
    // TX;, RX; and TX/; are routed by the audio-path block above; this catches any other args.
    if (prefix == "TX") {
        const bool on = (args == "/") ? !m_radioState->isTransmitting() : true;
        qCDebug(netCat) << "   PTT request:" << (on ? "ON" : "OFF");
        // Remember who keyed so the transmission can be ended if that client vanishes.
        m_pttOwner = on ? client : nullptr;
        m_pttOwnerForwarded = false;
        emit pttRequested(on);
        return QByteArray();
    }
    if (prefix == "RX") {
        qCDebug(netCat) << "   PTT request: OFF";
        m_pttOwner = nullptr;
        m_pttOwnerForwarded = false;
        emit pttRequested(false);
        return QByteArray();
    }

    // KY SET — track how much CW text is outstanding so KY;/TB; can report
    // buffer pressure to contest loggers, then forward the text to the K4.
    if (prefix == "KY") {
        if (args == "0") {
            m_cwPending = 0; // KY0; aborts the pending message
            m_cwDrainTimer->stop();
        } else {
            // The K4 drains the buffer at keyer speed; decay on that schedule. Text still keying
            // from an earlier message is estimated from the time its drain had left to run.
            const int wpm = qMax(1, m_radioState->keyerSpeed());
            const int charsPerSec = qMax(1, wpm / 6);
            const int stillPending =
                m_cwDrainTimer->isActive()
                    ? qMin(m_cwPending, (m_cwDrainTimer->remainingTime() * charsPerSec + 999) / 1000)
                    : 0;
            m_cwPending = stillPending + args.trimmed().length();
            m_cwDrainTimer->start(qMax(500, (m_cwPending * 1000) / charsPerSec));
        }
        emit catCommandReceived(cmd);
        return QByteArray();
    }

    // AG/AG$ SET — drive QK4's own volume when it owns the audio path.
    // The K4's AG range is 000-060, not 000-255.
    if (prefix == "AG" || prefix == "AG$") {
        if (RadioSettings::instance()->audioEnabled()) {
            const int gain = qBound(0, args.toInt(), 60);
            const int percent = (gain * 100 + 30) / 60; // 0-60 -> 0-100, rounded
            if (prefix == "AG") {
                emit volumeRequested(percent);
            } else {
                emit subVolumeRequested(percent);
            }
            return QByteArray();
        }
        // Audio disabled — fall through and let the K4 handle it.
    }

    // SET commands (have args) - forward to real K4
    // Commands like FA14074000;, MD1;, etc.
    qCDebug(netCat) << "   forwarding SET to K4:" << cmd;
    emit catCommandReceived(cmd);

    // Most SET commands echo the new value
    return QByteArray();
}
