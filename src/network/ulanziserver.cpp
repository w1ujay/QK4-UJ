#include "ulanziserver.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QTimer>
#include <cmath>

Q_LOGGING_CATEGORY(netUlanzi, "net.ulanzi")

UlanziServer::UlanziServer(QObject *parent) : QObject(parent) {
    for (int key = 0; key < static_cast<int>(m_keys.size()); ++key) {
        auto *timer = new QTimer(this);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, this, [this, key]() { onHoldTimeout(key); });
        m_keys[key].timer = timer;
    }
}

UlanziServer::~UlanziServer() = default;

void UlanziServer::setHoldMs(int ms) {
    m_holdMs = ms;
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
