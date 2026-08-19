#include "rfkituicontroller.h"

#include "controllers/statusbarcontroller.h"
#include "models/radiostate.h"
#include "network/rfkitclient.h"
#include "settings/radiosettings.h"
#include "ui/rfkitpanel.h"
#include "ui/rfkitwindow.h"
#include "ui/styling/k4styles.h"

#include <QLoggingCategory>
#include <QString>

Q_LOGGING_CATEGORY(qk4Rfkit, "qk4.rfkit")

RFKitUiController::RFKitUiController(StatusBarController *statusBar, RadioState *radioState, QObject *parent)
    : QObject(parent), m_statusBar(statusBar), m_radioState(radioState), m_window(new RFKitWindow(nullptr)),
      m_client(new RFKitClient(this)) {

    m_window->hide();

    // === Connection lifecycle signals ===
    connect(m_client, &RFKitClient::connected, this, &RFKitUiController::onConnected);
    connect(m_client, &RFKitClient::disconnected, this, &RFKitUiController::onDisconnected);
    connect(m_client, &RFKitClient::errorOccurred, this, &RFKitUiController::onError);

    // === Amplifier telemetry → panel ===
    connect(m_client, &RFKitClient::powerChanged, this, [this](double fwd, double ref, double swr) {
        m_window->panel()->setForwardPower(static_cast<float>(fwd), static_cast<float>(m_client->maxForwardPower()));
        m_window->panel()->setReflectedPower(static_cast<float>(ref));
        m_window->panel()->setSWR(static_cast<float>(swr));
    });
    connect(m_client, &RFKitClient::temperatureChanged, this,
            [this](double tempC) { m_window->panel()->setTemperature(static_cast<float>(tempC)); });
    connect(m_client, &RFKitClient::voltageChanged, this,
            [this](double v) { m_window->panel()->setVoltage(static_cast<float>(v)); });
    connect(m_client, &RFKitClient::currentChanged, this,
            [this](double a) { m_window->panel()->setCurrent(static_cast<float>(a)); });
    connect(m_client, &RFKitClient::operatingStateChanged, this, [this](RFKitClient::OperatingState state) {
        m_window->panel()->setMode(state == RFKitClient::StateOperate);
    });
    connect(m_client, &RFKitClient::antennaChanged, this,
            [this](int number, const QString &name) { m_window->panel()->setAntenna(number, name); });
    connect(m_client, &RFKitClient::antennasUpdated, this,
            [this]() { m_window->panel()->setAntennaCount(m_client->antennas().size()); });
    connect(m_client, &RFKitClient::deviceInfoChanged, this,
            [this](const QString &name, const QString &) { m_window->panel()->setDeviceName(name); });
    connect(m_client, &RFKitClient::statusChanged, this,
            [this](const QString &status) { m_window->panel()->setStatus(status); });

    // === Panel button signals → amplifier commands ===
    connect(m_window->panel(), &RFKitPanel::modeToggled, this,
            [this](bool operate) { m_client->setOperateMode(operate); });
    connect(m_window->panel(), &RFKitPanel::wakeUpRequested, this, [this]() { m_client->setOperateMode(false); });
    connect(m_window->panel(), &RFKitPanel::antennaChanged, this, [this](int number) { m_client->setAntenna(number); });
    connect(m_window->panel(), &RFKitPanel::errorResetRequested, this, [this]() { m_client->resetError(); });

    // Close button on the floating window hides it without disconnecting.
    connect(m_window, &RFKitWindow::closeRequested, this, [this]() { m_window->hide(); });

    // === RadioSettings observers ===
    connect(RadioSettings::instance(), &RadioSettings::rfkitEnabledChanged, this, &RFKitUiController::onEnabledChanged);
    connect(RadioSettings::instance(), &RadioSettings::rfkitSettingsChanged, this,
            &RFKitUiController::onSettingsChanged);
    connect(RadioSettings::instance(), &RadioSettings::rfkitLowPowerChanged, this, &RFKitUiController::checkDrivePower);
    connect(RadioSettings::instance(), &RadioSettings::rfkitTempUnitChanged, this,
            [this](bool fahrenheit) { m_window->panel()->setTempFahrenheit(fahrenheit); });

    // K4 drive power drives the lockout — re-evaluate whenever it changes.
    connect(m_radioState, &RadioState::rfPowerChanged, this,
            [this](double, LevelsState::PowerRange) { checkDrivePower(); });

    m_window->panel()->setTempFahrenheit(RadioSettings::instance()->rfkitTempFahrenheit());
    updateStatus();
}

RFKitUiController::~RFKitUiController() {
    // WHY: sever both directions before disconnectFromHost() — that call can
    // synchronously emit disconnected, whose slot touches sibling controllers'
    // widgets that may already be gone under Qt's child-destruction order.
    // See CONVENTIONS.md → Architecture Rule 11.
    if (m_client) {
        disconnect(m_client, nullptr, this, nullptr);
        m_client->disconnectFromHost();
    }
    disconnect(this);
    delete m_window; // parentless so it floats above the main window
}

void RFKitUiController::connectIfEnabled() {
    if (RadioSettings::instance()->rfkitEnabled() && !RadioSettings::instance()->rfkitHost().isEmpty()) {
        m_client->connectToHost(RadioSettings::instance()->rfkitHost(), RadioSettings::instance()->rfkitPort());
    }
}

void RFKitUiController::disconnectFromHost() {
    if (m_client->isConnected()) {
        m_client->disconnectFromHost();
    }
}

void RFKitUiController::onConnected() {
    qCDebug(qk4Rfkit) << "RFKit: Connected to amplifier";
    const int pollInterval = RadioSettings::instance()->rfkitPollInterval();
    m_client->startPolling(pollInterval);
    updateStatus();
}

void RFKitUiController::onDisconnected() {
    qCDebug(qk4Rfkit) << "RFKit: Disconnected from amplifier";
    updateStatus();
}

void RFKitUiController::onError(const QString &error) {
    qWarning() << "RFKit: Error -" << error;
    updateStatus();
}

void RFKitUiController::onEnabledChanged(bool enabled) {
    if (enabled) {
        const QString host = RadioSettings::instance()->rfkitHost();
        if (!host.isEmpty()) {
            m_client->connectToHost(host, RadioSettings::instance()->rfkitPort());
        }
    } else {
        m_client->disconnectFromHost();
    }
    updateStatus();
}

void RFKitUiController::onSettingsChanged() {
    // Reconnect with new settings if currently enabled.
    if (RadioSettings::instance()->rfkitEnabled()) {
        m_client->disconnectFromHost();
        const QString host = RadioSettings::instance()->rfkitHost();
        if (!host.isEmpty()) {
            m_client->connectToHost(host, RadioSettings::instance()->rfkitPort());
        }
    }
    updateStatus();
}

void RFKitUiController::checkDrivePower() {
    RadioSettings *settings = RadioSettings::instance();
    if (!settings->rfkitEnabled() || !settings->rfkitLowPowerEnabled()) {
        m_window->panel()->setOperateLocked(false);
        return;
    }

    const double maxDrive = settings->rfkitMaxDrivePower();
    const double currentPower = m_radioState->rfPower();
    const bool overLimit = currentPower > maxDrive;
    m_window->panel()->setOperateLocked(overLimit);

    if (overLimit && m_client->isConnected() && m_client->operatingState() == RFKitClient::StateOperate) {
        qCWarning(qk4Rfkit) << "RFKit: K4 drive power" << currentPower << "W exceeds limit" << maxDrive
                            << "W - forcing standby";
        m_client->setOperateMode(false);
    }
}

void RFKitUiController::updateStatus() {
    const bool enabled = RadioSettings::instance()->rfkitEnabled();
    const bool connected = m_client && m_client->isConnected();

    if (!enabled) {
        m_statusBar->setRfkitVisible(false);
    } else {
        m_statusBar->setRfkitVisible(true);
        if (connected) {
            const QString name = m_client->deviceName();
            m_statusBar->setRfkitStatus(name.isEmpty() ? QStringLiteral("RFKit") : name,
                                        QString("color: %1; font-size: %2px; font-weight: bold;")
                                            .arg(K4Styles::Colors::StatusGreen)
                                            .arg(K4Styles::Dimensions::FontSizeButton));
        } else {
            m_statusBar->setRfkitStatus(QStringLiteral("RFKit"), QString("color: %1; font-size: %2px;")
                                                                     .arg(K4Styles::Colors::InactiveGray)
                                                                     .arg(K4Styles::Dimensions::FontSizeButton));
        }
    }

    // Show the floating window only when enabled AND connected.
    m_window->setVisible(enabled && connected);
    m_window->panel()->setConnected(connected);
}
