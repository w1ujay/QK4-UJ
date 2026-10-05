#include "ui/pages/ulanzipage.h"
#include "ui/styling/k4styles.h"
#include "network/ulanziserver.h"
#include "settings/radiosettings.h"
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>

UlanziPage::UlanziPage(UlanziServer *server, QWidget *parent) : QWidget(parent), m_server(server) {
    setStyleSheet(K4Styles::Dialog::pageBackground());

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    auto *titleLabel = new QLabel("Ulanzi Dial", this);
    titleLabel->setStyleSheet(K4Styles::Dialog::titleLabel());
    layout->addWidget(titleLabel);

    auto *descLabel = new QLabel("Use an Ulanzi D100H dial with QK4. Turning tunes VFO A (VFO B while the dial is "
                                 "held down), the buttons and the dial press run the Ulanzi macros, and a QK4 PTT "
                                 "button transmits while held.",
                                 this);
    descLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::FontSizeButton));
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    auto *line = new QFrame(this);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(K4Styles::Dialog::separator());
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line);

    // Status
    auto *statusLayout = new QHBoxLayout();
    auto *statusTitleLabel = new QLabel("Status:", this);
    statusTitleLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    statusTitleLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);
    m_statusLabel = new QLabel(this);
    statusLayout->addWidget(statusTitleLabel);
    statusLayout->addWidget(m_statusLabel);
    statusLayout->addStretch();
    layout->addLayout(statusLayout);

    auto *line2 = new QFrame(this);
    line2->setFrameShape(QFrame::HLine);
    line2->setStyleSheet(K4Styles::Dialog::separator());
    line2->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line2);

    auto *sectionLabel = new QLabel("Settings", this);
    sectionLabel->setStyleSheet(K4Styles::Dialog::sectionHeader());
    layout->addWidget(sectionLabel);

    // Port
    auto *portLayout = new QHBoxLayout();
    auto *portLabel = new QLabel("Port:", this);
    portLabel->setStyleSheet(K4Styles::Dialog::formLabel());
    portLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_portEdit = new QLineEdit(this);
    m_portEdit->setPlaceholderText(QString::number(UlanziServer::DEFAULT_PORT));
    m_portEdit->setFixedWidth(K4Styles::Dimensions::InputFieldWidthSmall);
    m_portEdit->setStyleSheet(K4Styles::Dialog::lineEdit());
    m_portEdit->setText(QString::number(RadioSettings::instance()->ulanziPort()));

    auto *portHint = new QLabel(QString("(default: %1)").arg(UlanziServer::DEFAULT_PORT), this);
    portHint->setStyleSheet(QString("color: %1; font-size: %2px;")
                                .arg(K4Styles::Colors::TextGray)
                                .arg(K4Styles::Dimensions::FontSizeLarge));

    portLayout->addWidget(portLabel);
    portLayout->addWidget(m_portEdit);
    portLayout->addWidget(portHint);
    portLayout->addStretch();
    layout->addLayout(portLayout);

    auto *line3 = new QFrame(this);
    line3->setFrameShape(QFrame::HLine);
    line3->setStyleSheet(K4Styles::Dialog::separator());
    line3->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line3);

    m_enableCheckbox = new QCheckBox("Enable Ulanzi Dial", this);
    m_enableCheckbox->setStyleSheet(K4Styles::Dialog::checkBox());
    m_enableCheckbox->setChecked(RadioSettings::instance()->ulanziEnabled());
    layout->addWidget(m_enableCheckbox);

    auto *helpLabel = new QLabel("Install the QK4 plugin in Ulanzi Studio (plugins/ulanzi/README.md in the QK4 "
                                 "source) and set the same port in any QK4 action there. Assign the buttons in "
                                 "Macros, under Ulanzi.1T to Ulanzi.DialH.",
                                 this);
    helpLabel->setStyleSheet(K4Styles::Dialog::helpText());
    helpLabel->setWordWrap(true);
    layout->addWidget(helpLabel);

    layout->addStretch();

    connect(m_portEdit, &QLineEdit::editingFinished, this, [this]() {
        bool ok;
        const quint16 port = m_portEdit->text().toUShort(&ok);
        if (ok && port >= 1024) {
            RadioSettings::instance()->setUlanziPort(port);
        } else {
            m_portEdit->setText(QString::number(RadioSettings::instance()->ulanziPort()));
        }
    });
    connect(m_enableCheckbox, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setUlanziEnabled(checked); });

    // Follow the settings, not just write them (see KpodPage for why).
    connect(RadioSettings::instance(), &RadioSettings::ulanziEnabledChanged, this, [this](bool enabled) {
        m_enableCheckbox->setChecked(enabled);
        updateStatus();
    });
    connect(RadioSettings::instance(), &RadioSettings::ulanziPortChanged, this, [this](quint16 port) {
        m_portEdit->setText(QString::number(port));
        updateStatus();
    });

    if (m_server) {
        connect(m_server, &UlanziServer::started, this, &UlanziPage::updateStatus);
        connect(m_server, &UlanziServer::stopped, this, &UlanziPage::updateStatus);
        connect(m_server, &UlanziServer::clientConnectedChanged, this, &UlanziPage::updateStatus);
        connect(m_server, &UlanziServer::errorOccurred, this, &UlanziPage::updateStatus);
    }

    updateStatus();
}

void UlanziPage::refresh() {
    updateStatus();
}

void UlanziPage::updateStatus() {
    if (!RadioSettings::instance()->ulanziEnabled()) {
        m_statusLabel->setText("Disabled");
        m_statusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::TextGray));
    } else if (m_server && m_server->isListening()) {
        if (m_server->hasClient()) {
            m_statusLabel->setText("Ulanzi Studio connected");
            m_statusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::StatusGreen));
        } else {
            m_statusLabel->setText(QString("Listening on port %1, waiting for Ulanzi Studio").arg(m_server->port()));
            m_statusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::TextWhite));
        }
    } else if (m_server && !m_server->lastError().isEmpty()) {
        m_statusLabel->setText(m_server->lastError()); // "Port N unavailable: <reason>"
        m_statusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::ErrorRed));
    } else {
        m_statusLabel->setText("Not running");
        m_statusLabel->setStyleSheet(K4Styles::Dialog::statusLabel(K4Styles::Colors::ErrorRed));
    }
}
