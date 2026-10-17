#include "ui/pages/rfkitpage.h"
#include "network/rfkitclient.h"
#include "settings/radiosettings.h"
#include "ui/styling/k4styles.h"
#include <QDoubleValidator>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QSlider>
#include <QVBoxLayout>

RfkitPage::RfkitPage(RFKitClient *rfkitClient, QWidget *parent) : QWidget(parent), m_rfkitClient(rfkitClient) {
    setStyleSheet(K4Styles::Dialog::pageBackground());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    // === Status row ===
    auto *statusLayout = new QHBoxLayout();
    auto *statusLabel = new QLabel("Status:", this);
    statusLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    statusLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);
    m_statusLabel = new QLabel("Not Connected", this);
    m_statusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::ErrorRed));
    statusLayout->addWidget(statusLabel);
    statusLayout->addWidget(m_statusLabel);
    statusLayout->addStretch();
    layout->addLayout(statusLayout);

    // === Separator ===
    auto *line1 = new QFrame(this);
    line1->setFrameShape(QFrame::HLine);
    line1->setStyleSheet(K4Styles::Dialog::separator());
    line1->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line1);

    // === Connection Settings section ===
    auto *sectionLabel = new QLabel("Connection Settings", this);
    sectionLabel->setStyleSheet(K4Styles::Dialog::sectionHeader());
    layout->addWidget(sectionLabel);

    // Enable checkbox
    m_enableCheckbox = new QCheckBox("Enable RFKit", this);
    m_enableCheckbox->setStyleSheet(K4Styles::Dialog::checkBox());
    m_enableCheckbox->setChecked(RadioSettings::instance()->rfkitEnabled());
    connect(m_enableCheckbox, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setRfkitEnabled(checked); });
    layout->addWidget(m_enableCheckbox);

    // Host row
    auto *hostLayout = new QHBoxLayout();
    auto *hostLabel = new QLabel("Host:", this);
    hostLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    hostLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);
    m_hostEdit = new QLineEdit(this);
    m_hostEdit->setStyleSheet(K4Styles::Dialog::lineEdit());
    m_hostEdit->setPlaceholderText("IP address or hostname");
    m_hostEdit->setText(RadioSettings::instance()->rfkitHost());
    connect(m_hostEdit, &QLineEdit::editingFinished, this,
            [this]() { RadioSettings::instance()->setRfkitHost(m_hostEdit->text().trimmed()); });
    hostLayout->addWidget(hostLabel);
    hostLayout->addWidget(m_hostEdit, 1);
    layout->addLayout(hostLayout);

    // Port row
    auto *portLayout = new QHBoxLayout();
    auto *portLabel = new QLabel("Port:", this);
    portLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    portLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);
    m_portEdit = new QLineEdit(this);
    m_portEdit->setStyleSheet(K4Styles::Dialog::lineEdit());
    m_portEdit->setFixedWidth(K4Styles::Dimensions::InputFieldWidthSmall);
    m_portEdit->setText(QString::number(RadioSettings::instance()->rfkitPort()));
    connect(m_portEdit, &QLineEdit::editingFinished, this, [this]() {
        bool ok;
        quint16 port = m_portEdit->text().toUShort(&ok);
        if (ok && port > 0) {
            RadioSettings::instance()->setRfkitPort(port);
        } else {
            m_portEdit->setText(QString::number(RadioSettings::instance()->rfkitPort()));
        }
    });
    portLayout->addWidget(portLabel);
    portLayout->addWidget(m_portEdit);
    portLayout->addStretch();
    layout->addLayout(portLayout);

    // Poll interval row
    auto *pollLayout = new QHBoxLayout();
    auto *pollLabel = new QLabel("Poll:", this);
    pollLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    pollLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);
    m_pollLabel = new QLabel(QString("%1 ms").arg(RadioSettings::instance()->rfkitPollInterval()), this);
    m_pollLabel->setStyleSheet(K4Styles::Dialog::formValue());
    auto *pollSlider = new QSlider(Qt::Horizontal, this);
    pollSlider->setRange(100, 5000);
    pollSlider->setSingleStep(100);
    pollSlider->setValue(RadioSettings::instance()->rfkitPollInterval());
    pollSlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::DarkBackground, K4Styles::Colors::AccentAmber));
    connect(pollSlider, &QSlider::valueChanged, this, [this](int value) {
        RadioSettings::instance()->setRfkitPollInterval(value);
        if (m_pollLabel)
            m_pollLabel->setText(QString("%1 ms").arg(value));
    });
    pollLayout->addWidget(pollLabel);
    pollLayout->addWidget(pollSlider, 1);
    pollLayout->addWidget(m_pollLabel);
    layout->addLayout(pollLayout);

    // Connect/Disconnect button
    m_connectBtn = new QPushButton("Connect", this);
    m_connectBtn->setStyleSheet(K4Styles::Dialog::actionButtonSmall());
    m_connectBtn->setCursor(Qt::PointingHandCursor);
    connect(m_connectBtn, &QPushButton::clicked, this, [this]() {
        if (!m_rfkitClient)
            return;
        if (m_rfkitClient->isConnected()) {
            m_rfkitClient->disconnectFromHost();
        } else {
            QString host = m_hostEdit->text().trimmed();
            quint16 port = m_portEdit->text().toUShort();
            if (!host.isEmpty() && port > 0) {
                m_rfkitClient->connectToHost(host, port);
            }
        }
    });
    layout->addWidget(m_connectBtn);

    // === Separator ===
    auto *line2 = new QFrame(this);
    line2->setFrameShape(QFrame::HLine);
    line2->setStyleSheet(K4Styles::Dialog::separator());
    line2->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line2);

    // === Drive Power Lockout section ===
    auto *lockoutLabel = new QLabel("Drive Power Lockout", this);
    lockoutLabel->setStyleSheet(K4Styles::Dialog::sectionHeader());
    layout->addWidget(lockoutLabel);

    m_lowPowerCheckbox = new QCheckBox("Force amplifier to standby above max drive power", this);
    m_lowPowerCheckbox->setStyleSheet(K4Styles::Dialog::checkBox());
    m_lowPowerCheckbox->setChecked(RadioSettings::instance()->rfkitLowPowerEnabled());
    connect(m_lowPowerCheckbox, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setRfkitLowPowerEnabled(checked); });
    layout->addWidget(m_lowPowerCheckbox);

    auto *driveLayout = new QHBoxLayout();
    auto *driveLabel = new QLabel("Max drive:", this);
    driveLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    driveLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);
    m_maxDriveEdit = new QLineEdit(this);
    m_maxDriveEdit->setStyleSheet(K4Styles::Dialog::lineEdit());
    m_maxDriveEdit->setFixedWidth(K4Styles::Dimensions::InputFieldWidthSmall);
    m_maxDriveEdit->setValidator(new QDoubleValidator(0.1, 100.0, 1, m_maxDriveEdit));
    m_maxDriveEdit->setText(QString::number(RadioSettings::instance()->rfkitMaxDrivePower(), 'f', 1));
    connect(m_maxDriveEdit, &QLineEdit::editingFinished, this, [this]() {
        bool ok;
        double watts = m_maxDriveEdit->text().toDouble(&ok);
        if (ok) {
            RadioSettings::instance()->setRfkitMaxDrivePower(watts);
        }
        // Re-read so the field always shows the clamped, stored value.
        m_maxDriveEdit->setText(QString::number(RadioSettings::instance()->rfkitMaxDrivePower(), 'f', 1));
    });
    auto *wattsLabel = new QLabel("W", this);
    wattsLabel->setStyleSheet(K4Styles::Dialog::formValue());
    driveLayout->addWidget(driveLabel);
    driveLayout->addWidget(m_maxDriveEdit);
    driveLayout->addWidget(wattsLabel);
    driveLayout->addStretch();
    layout->addLayout(driveLayout);

    // === Display section ===
    m_fahrenheitCheckbox = new QCheckBox("Show temperature in °F", this);
    m_fahrenheitCheckbox->setStyleSheet(K4Styles::Dialog::checkBox());
    m_fahrenheitCheckbox->setChecked(RadioSettings::instance()->rfkitTempFahrenheit());
    connect(m_fahrenheitCheckbox, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setRfkitTempFahrenheit(checked); });
    layout->addWidget(m_fahrenheitCheckbox);

    // === Separator ===
    auto *line3 = new QFrame(this);
    line3->setFrameShape(QFrame::HLine);
    line3->setStyleSheet(K4Styles::Dialog::separator());
    line3->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line3);

    // === Amplifier Info section ===
    auto *infoLabel = new QLabel("Amplifier Info", this);
    infoLabel->setStyleSheet(K4Styles::Dialog::sectionHeader());
    layout->addWidget(infoLabel);

    auto *infoWidget = new QWidget(this);
    auto *infoGrid = new QGridLayout(infoWidget);
    infoGrid->setContentsMargins(0, 0, 0, 0);
    infoGrid->setHorizontalSpacing(K4Styles::Dimensions::DialogMargin);
    infoGrid->setVerticalSpacing(K4Styles::Dimensions::PopupButtonSpacing);

    auto addInfoRow = [&](int row, const QString &label, QLabel *&valueLabel) {
        auto *lbl = new QLabel(label, infoWidget);
        lbl->setStyleSheet(K4Styles::Dialog::formLabel());
        valueLabel = new QLabel("--", infoWidget);
        valueLabel->setStyleSheet(K4Styles::Dialog::formValue());
        infoGrid->addWidget(lbl, row, 0, Qt::AlignLeft);
        infoGrid->addWidget(valueLabel, row, 1, Qt::AlignLeft);
    };

    addInfoRow(0, "Device:", m_deviceLabel);
    addInfoRow(1, "Band:", m_bandLabel);
    addInfoRow(2, "Antenna:", m_antennaLabel);
    infoGrid->setColumnStretch(1, 1);
    layout->addWidget(infoWidget);

    // === Help text ===
    auto *helpLabel = new QLabel("When enabled, QK4 polls the RF-Kit amplifier over HTTP for power, SWR, temperature, "
                                 "and antenna status. RFKit connects only while the K4 is connected.",
                                 this);
    helpLabel->setStyleSheet(K4Styles::Dialog::helpText());
    helpLabel->setWordWrap(true);
    layout->addWidget(helpLabel);

    layout->addStretch();

    // Connect to RFKit signals for real-time status updates
    if (m_rfkitClient) {
        connect(m_rfkitClient, &RFKitClient::connected, this, &RfkitPage::updateRfkitStatus);
        connect(m_rfkitClient, &RFKitClient::disconnected, this, &RfkitPage::updateRfkitStatus);
        connect(m_rfkitClient, &RFKitClient::deviceInfoChanged, this, &RfkitPage::updateRfkitStatus);
        connect(m_rfkitClient, &RFKitClient::bandChanged, this, &RfkitPage::updateRfkitStatus);
        connect(m_rfkitClient, &RFKitClient::antennaChanged, this, &RfkitPage::updateRfkitStatus);
    }

    updateRfkitStatus();
}

void RfkitPage::refresh() {
    updateRfkitStatus();
}

void RfkitPage::updateRfkitStatus() {
    if (!m_statusLabel)
        return;

    const bool connected = m_rfkitClient && m_rfkitClient->isConnected();

    if (connected) {
        QString version = m_rfkitClient->softwareVersion();
        QString statusText = "Connected";
        if (!version.isEmpty())
            statusText += QString(" (SW %1)").arg(version);
        m_statusLabel->setText(statusText);
        m_statusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::StatusGreen));
        m_connectBtn->setText("Disconnect");
    } else {
        m_statusLabel->setText("Not Connected");
        m_statusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::ErrorRed));
        if (m_connectBtn)
            m_connectBtn->setText("Connect");
    }

    if (m_deviceLabel)
        m_deviceLabel->setText(connected ? m_rfkitClient->deviceName() : "--");
    if (m_bandLabel)
        m_bandLabel->setText(connected ? m_rfkitClient->band() : "--");
    if (m_antennaLabel) {
        if (connected && m_rfkitClient->activeAntennaNumber() > 0) {
            const QString name = m_rfkitClient->activeAntennaName();
            m_antennaLabel->setText(name.isEmpty()
                                        ? QString::number(m_rfkitClient->activeAntennaNumber())
                                        : QString("%1 (%2)").arg(m_rfkitClient->activeAntennaNumber()).arg(name));
        } else {
            m_antennaLabel->setText("--");
        }
    }

    // Disable host/port editing while connected
    if (m_hostEdit)
        m_hostEdit->setEnabled(!connected);
    if (m_portEdit)
        m_portEdit->setEnabled(!connected);
}
