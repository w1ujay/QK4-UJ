#include "optionsdialog.h"
#include "k4styles.h"
#include "micmeterwidget.h"
#include "../models/radiostate.h"
#include "../hardware/kpoddevice.h"
#include "../hardware/halikeydevice.h"
#include "../settings/radiosettings.h"
#include "../network/catserver.h"
#include "../audio/audioengine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QScrollArea>
#include <QStringList>
#include <QCheckBox>
#include <QGridLayout>
#include <QComboBox>
#include <QSlider>
#include <QPushButton>
#include <QSpinBox>
#include <QColorDialog>
#include "../network/n1mmlistener.h"
#include "../network/rfkitclient.h"
#include "../network/kpa1500client.h"

// Use K4Styles::Colors::DialogBorder for dialog-specific borders

OptionsDialog::OptionsDialog(RadioState *radioState, AudioEngine *audioEngine, KpodDevice *kpodDevice,
                             CatServer *catServer, HalikeyDevice *halikeyDevice, N1mmListener *n1mmListener,
                             RFKitClient *rfkitClient, KPA1500Client *kpa1500Client, QWidget *parent)
    : QDialog(parent), m_radioState(radioState), m_audioEngine(audioEngine), m_kpodDevice(kpodDevice),
      m_catServer(catServer), m_halikeyDevice(halikeyDevice), m_n1mmListener(n1mmListener), m_rfkitClient(rfkitClient),
      m_kpa1500Client(kpa1500Client), m_micDeviceCombo(nullptr), m_micGainSlider(nullptr), m_micGainValueLabel(nullptr),
      m_micTestBtn(nullptr), m_micMeter(nullptr), m_speakerDeviceCombo(nullptr), m_catServerEnableCheckbox(nullptr),
      m_catServerPortEdit(nullptr), m_catServerStatusLabel(nullptr), m_catServerClientsLabel(nullptr),
      m_cwKeyerDeviceTypeCombo(nullptr), m_cwKeyerDescLabel(nullptr), m_cwKeyerPortCombo(nullptr),
      m_cwKeyerRefreshBtn(nullptr), m_cwKeyerConnectBtn(nullptr), m_cwKeyerStatusLabel(nullptr) {
    setWindowModality(Qt::ApplicationModal);
    setupUi();

    // Connect to KPOD device signals for real-time status updates
    if (m_kpodDevice) {
        connect(m_kpodDevice, &KpodDevice::deviceConnected, this, &OptionsDialog::updateKpodStatus);
        connect(m_kpodDevice, &KpodDevice::deviceDisconnected, this, &OptionsDialog::updateKpodStatus);
    }

    // Connect to CatServer signals for status updates
    if (m_catServer) {
        connect(m_catServer, &CatServer::started, this, &OptionsDialog::updateCatServerStatus);
        connect(m_catServer, &CatServer::stopped, this, &OptionsDialog::updateCatServerStatus);
        connect(m_catServer, &CatServer::clientConnected, this, &OptionsDialog::updateCatServerStatus);
        connect(m_catServer, &CatServer::clientDisconnected, this, &OptionsDialog::updateCatServerStatus);
    }

    // Connect to HalikeyDevice signals for status updates
    if (m_halikeyDevice) {
        connect(m_halikeyDevice, &HalikeyDevice::connected, this, &OptionsDialog::updateCwKeyerStatus);
        connect(m_halikeyDevice, &HalikeyDevice::disconnected, this, &OptionsDialog::updateCwKeyerStatus);
    }
}

OptionsDialog::~OptionsDialog() {
    // Make sure to stop mic test if dialog is closed
    if (m_micTestActive && m_audioEngine) {
        QMetaObject::invokeMethod(m_audioEngine, "setMicEnabled", Qt::QueuedConnection, Q_ARG(bool, false));
    }
}

void OptionsDialog::setupUi() {
    setWindowTitle("Options");
    setMinimumSize(700, 550);
    resize(800, 650);

    // Dark theme styling
    setStyleSheet(QString("QDialog { background-color: %1; }"
                          "QLabel { color: %2; }"
                          "QListWidget { background-color: %3; color: %2; border: 1px solid %4; "
                          "             font-size: %6px; outline: none; }"
                          "QListWidget::item { padding: 10px 15px; border-bottom: 1px solid %4; }"
                          "QListWidget::item:selected { background-color: %5; color: %3; }"
                          "QListWidget::item:hover { background-color: %7; }")
                      .arg(K4Styles::Colors::Background, K4Styles::Colors::TextWhite, K4Styles::Colors::DarkBackground,
                           K4Styles::Colors::DialogBorder, K4Styles::Colors::AccentAmber)
                      .arg(K4Styles::Dimensions::FontSizePopup)
                      .arg(K4Styles::Colors::GradientBottom));

    auto *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Left side: vertical tab list
    m_tabList = new QListWidget(this);
    m_tabList->setFixedWidth(K4Styles::Dimensions::TabListWidth);
    m_tabList->addItem("About");
    m_tabList->addItem("Audio Input");
    m_tabList->addItem("Audio Output");
    m_tabList->addItem("Rig Control");
    m_tabList->addItem("CW Keyer");
    m_tabList->addItem("K-Pod");
    m_tabList->addItem("N1MM Spots");
    m_tabList->addItem("KPA1500");
    m_tabList->addItem("RFKit Amp");
    m_tabList->setCurrentRow(0);

    // Right side: stacked pages (lazy — only About is created eagerly)
    m_pageStack = new QStackedWidget(this);
    m_pageStack->addWidget(createAboutPage());
    m_pageCreated[PageAbout] = true;
    for (int i = 1; i < PageCount; ++i)
        m_pageStack->addWidget(new QWidget(this));

    // Connect tab selection to page switching with lazy creation
    connect(m_tabList, &QListWidget::currentRowChanged, this, [this](int index) {
        ensurePageCreated(index);
        m_pageStack->setCurrentIndex(index);
    });

    mainLayout->addWidget(m_tabList);
    mainLayout->addWidget(m_pageStack, 1);
}

void OptionsDialog::ensurePageCreated(int index) {
    if (index < 0 || index >= PageCount || m_pageCreated[index])
        return;

    QWidget *page = nullptr;
    switch (index) {
    case PageAudioInput:
        page = createAudioInputPage();
        break;
    case PageAudioOutput:
        page = createAudioOutputPage();
        break;
    case PageRigControl:
        page = createRigControlPage();
        break;
    case PageCwKeyer:
        page = createCwKeyerPage();
        break;
    case PageKpod:
        page = createKpodPage();
        break;
    case PageN1mm:
        page = createN1mmPage();
        break;
    case PageKpa1500:
        page = createKpa1500Page();
        break;
    case PageRfkit:
        page = createRfkitPage();
        break;
    default:
        return;
    }

    // Swap out the placeholder widget at this index
    QWidget *placeholder = m_pageStack->widget(index);
    m_pageStack->removeWidget(placeholder);
    delete placeholder;
    m_pageStack->insertWidget(index, page);
    m_pageCreated[index] = true;
}

void OptionsDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    refreshCurrentPage();
}

void OptionsDialog::hideEvent(QHideEvent *event) {
    // Stop mic test when dialog is hidden
    if (m_micTestActive && m_audioEngine) {
        QMetaObject::invokeMethod(m_audioEngine, "setMicEnabled", Qt::QueuedConnection, Q_ARG(bool, false));
        m_micTestActive = false;
        if (m_micTestBtn)
            m_micTestBtn->setChecked(false);
        if (m_micMeter)
            m_micMeter->setLevel(0.0f);
    }
    QDialog::hideEvent(event);
}

void OptionsDialog::refreshCurrentPage() {
    refreshPage(m_pageStack->currentIndex());
}

void OptionsDialog::refreshPage(int index) {
    if (index < 0 || index >= PageCount || !m_pageCreated[index])
        return;

    switch (index) {
    case PageAudioInput:
        populateMicDevices();
        break;
    case PageAudioOutput:
        populateSpeakerDevices();
        break;
    case PageRigControl:
        updateCatServerStatus();
        break;
    case PageCwKeyer:
        populateCwKeyerPorts();
        updateCwKeyerStatus();
        break;
    case PageKpod:
        updateKpodStatus();
        break;
    case PageAbout:
    default:
        break;
    }
}

namespace {
QStringList decodeOptionModules(const QString &om) {
    QStringList options;
    if (om.length() > 0 && om[0] == 'A')
        options << "KAT4 (ATU)";
    if (om.length() > 1 && om[1] == 'P')
        options << "KPA4 (PA)";
    if (om.length() > 2 && om[2] == 'X')
        options << "XVTR";
    if (om.length() > 3 && om[3] == 'S')
        options << "KRX4 (Sub RX)";
    if (om.length() > 4 && om[4] == 'H')
        options << "KHDR4 (HDR)";
    if (om.length() > 5 && om[5] == 'M')
        options << "K40 (Mini)";
    if (om.length() > 6 && om[6] == 'L')
        options << "Linear Amp";
    if (om.length() > 7 && om[7] == '1')
        options << "KPA1500";
    return options;
}
} // namespace

QWidget *OptionsDialog::createAboutPage() {
    auto *page = new QWidget(this);
    page->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::Background));

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    // Title
    auto *titleLabel = new QLabel("Connected Radio", page);
    titleLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                  .arg(K4Styles::Colors::AccentAmber)
                                  .arg(K4Styles::Dimensions::FontSizeTitle));
    layout->addWidget(titleLabel);

    // Separator line
    auto *line = new QFrame(page);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line);

    // Two-column layout for Radio Info and Installed Options
    auto *infoRow = new QHBoxLayout();
    infoRow->setSpacing(K4Styles::Dimensions::DialogMargin);

    // Left column widget: Radio ID and Model
    auto *leftWidget = new QWidget(page);
    auto *leftColumn = new QVBoxLayout(leftWidget);
    leftColumn->setContentsMargins(0, 0, 0, 0);
    leftColumn->setSpacing(K4Styles::Dimensions::PopupButtonSpacing);

    // Radio ID
    auto *idLayout = new QHBoxLayout();
    auto *idTitleLabel = new QLabel("Radio ID:", leftWidget);
    idTitleLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                    .arg(K4Styles::Colors::TextGray)
                                    .arg(K4Styles::Dimensions::FontSizePopup));
    idTitleLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    QString radioId = m_radioState ? m_radioState->radioID() : "Not connected";
    auto *idValueLabel = new QLabel(radioId, leftWidget);
    idValueLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                    .arg(K4Styles::Colors::TextWhite)
                                    .arg(K4Styles::Dimensions::FontSizePopup));

    idLayout->addWidget(idTitleLabel);
    idLayout->addWidget(idValueLabel);
    idLayout->addStretch();
    leftColumn->addLayout(idLayout);

    // Radio Model
    auto *modelLayout = new QHBoxLayout();
    auto *modelTitleLabel = new QLabel("Model:", leftWidget);
    modelTitleLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                       .arg(K4Styles::Colors::TextGray)
                                       .arg(K4Styles::Dimensions::FontSizePopup));
    modelTitleLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    QString radioModel = m_radioState ? m_radioState->radioModel() : "Unknown";
    auto *modelValueLabel = new QLabel(radioModel, leftWidget);
    modelValueLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                       .arg(K4Styles::Colors::TextWhite)
                                       .arg(K4Styles::Dimensions::FontSizePopup));

    modelLayout->addWidget(modelTitleLabel);
    modelLayout->addWidget(modelValueLabel);
    modelLayout->addStretch();
    leftColumn->addLayout(modelLayout);
    leftColumn->addStretch();

    // Vertical demarcation line
    auto *vline = new QFrame(page);
    vline->setFrameShape(QFrame::VLine);
    vline->setFrameShadow(QFrame::Plain);
    vline->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    vline->setFixedWidth(K4Styles::Dimensions::SeparatorHeight);

    // Right column widget: Installed Options
    auto *rightWidget = new QWidget(page);
    auto *rightColumn = new QVBoxLayout(rightWidget);
    rightColumn->setContentsMargins(0, 0, 0, 0);
    rightColumn->setSpacing(K4Styles::Dimensions::PaddingSmall);

    auto *optionsTitle = new QLabel("Installed Options", rightWidget);
    optionsTitle->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                    .arg(K4Styles::Colors::AccentAmber)
                                    .arg(K4Styles::Dimensions::FontSizePopup));
    rightColumn->addWidget(optionsTitle);

    if (m_radioState && !m_radioState->optionModules().isEmpty()) {
        QStringList options = decodeOptionModules(m_radioState->optionModules());
        if (options.isEmpty()) {
            auto *noOptionsLabel = new QLabel("No additional options", rightWidget);
            noOptionsLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-style: italic;")
                                              .arg(K4Styles::Colors::TextGray)
                                              .arg(K4Styles::Dimensions::FontSizeButton));
            rightColumn->addWidget(noOptionsLabel);
        } else {
            for (const QString &opt : options) {
                auto *optLabel = new QLabel(QString::fromUtf8("\u2022 ") + opt, rightWidget);
                optLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                            .arg(K4Styles::Colors::TextWhite)
                                            .arg(K4Styles::Dimensions::FontSizeButton));
                rightColumn->addWidget(optLabel);
            }
        }
    } else {
        auto *noDataLabel = new QLabel("Not connected", rightWidget);
        noDataLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-style: italic;")
                                       .arg(K4Styles::Colors::TextGray)
                                       .arg(K4Styles::Dimensions::FontSizeButton));
        rightColumn->addWidget(noDataLabel);
    }
    rightColumn->addStretch();

    // Assemble the info row
    infoRow->addWidget(leftWidget, 1);
    infoRow->addWidget(vline);
    infoRow->addWidget(rightWidget, 1);

    layout->addLayout(infoRow);

    // Software Versions section
    layout->addSpacing(K4Styles::Dimensions::PaddingMedium);

    auto *versionsTitle = new QLabel("Software Versions", page);
    versionsTitle->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                     .arg(K4Styles::Colors::AccentAmber)
                                     .arg(K4Styles::Dimensions::FontSizePopup));
    layout->addWidget(versionsTitle);

    auto *line2 = new QFrame(page);
    line2->setFrameShape(QFrame::HLine);
    line2->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line2->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line2);

    // Firmware versions list
    if (m_radioState) {
        QMap<QString, QString> versions = m_radioState->firmwareVersions();

        // Component name mappings for readable display
        QMap<QString, QString> componentNames = {
            {"DDC0", "DDC 0"},   {"DDC1", "DDC 1"},    {"DUC", "DUC"},   {"FP", "Front Panel"}, {"DSP", "DSP"},
            {"RFB", "RF Board"}, {"REF", "Reference"}, {"DAP", "DAP"},   {"KSRV", "K Server"},  {"KUI", "K UI"},
            {"KUP", "K Update"}, {"KCFG", "K Config"}, {"R", "Revision"}};

        for (auto it = versions.constBegin(); it != versions.constEnd(); ++it) {
            auto *versionLayout = new QHBoxLayout();

            QString displayName = componentNames.value(it.key(), it.key());
            auto *nameLabel = new QLabel(displayName + ":", page);
            nameLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                         .arg(K4Styles::Colors::TextGray)
                                         .arg(K4Styles::Dimensions::FontSizeButton));
            nameLabel->setFixedWidth(K4Styles::Dimensions::InputFieldWidthMedium);

            auto *valueLabel = new QLabel(it.value(), page);
            valueLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                          .arg(K4Styles::Colors::TextWhite)
                                          .arg(K4Styles::Dimensions::FontSizeButton));

            versionLayout->addWidget(nameLabel);
            versionLayout->addWidget(valueLabel);
            versionLayout->addStretch();
            layout->addLayout(versionLayout);
        }
    } else {
        auto *noDataLabel = new QLabel("Connect to a radio to view version information", page);
        noDataLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-style: italic;")
                                       .arg(K4Styles::Colors::TextGray)
                                       .arg(K4Styles::Dimensions::FontSizeButton));
        layout->addWidget(noDataLabel);
    }

    layout->addStretch();
    return page;
}

QWidget *OptionsDialog::createKpodPage() {
    auto *page = new QWidget(this);
    page->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::Background));

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    // Status indicator
    auto *statusLayout = new QHBoxLayout();
    auto *statusLabel = new QLabel("Status:", page);
    statusLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                   .arg(K4Styles::Colors::TextGray)
                                   .arg(K4Styles::Dimensions::FontSizePopup));
    statusLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_kpodStatusLabel = new QLabel("Not Detected", page);
    statusLayout->addWidget(statusLabel);
    statusLayout->addWidget(m_kpodStatusLabel);
    statusLayout->addStretch();
    layout->addLayout(statusLayout);

    // Separator line
    auto *line = new QFrame(page);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line);

    // Device Summary title
    auto *titleLabel = new QLabel("Device Summary", page);
    titleLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                  .arg(K4Styles::Colors::AccentAmber)
                                  .arg(K4Styles::Dimensions::FontSizeTitle));
    layout->addWidget(titleLabel);

    // Device info table using grid layout
    auto *tableWidget = new QWidget(page);
    auto *tableLayout = new QGridLayout(tableWidget);
    tableLayout->setContentsMargins(0, K4Styles::Dimensions::PaddingMedium, 0, K4Styles::Dimensions::PaddingMedium);
    tableLayout->setHorizontalSpacing(K4Styles::Dimensions::DialogMargin);
    tableLayout->setVerticalSpacing(K4Styles::Dimensions::PopupButtonSpacing);

    // Table styling
    QString headerStyle =
        QString("color: %1; font-size: 12px; font-weight: bold; padding: 5px;").arg(K4Styles::Colors::TextGray);

    // Create labels with property names
    QStringList properties = {"Product Name", "Manufacturer",     "Vendor ID", "Product ID",
                              "Device Type",  "Firmware Version", "Device ID"};
    QVector<QLabel **> valueLabels = {&m_kpodProductLabel,   &m_kpodManufacturerLabel, &m_kpodVendorIdLabel,
                                      &m_kpodProductIdLabel, &m_kpodDeviceTypeLabel,   &m_kpodFirmwareLabel,
                                      &m_kpodDeviceIdLabel};

    for (int row = 0; row < properties.size(); ++row) {
        auto *propLabel = new QLabel(properties[row], tableWidget);
        propLabel->setStyleSheet(headerStyle);

        *valueLabels[row] = new QLabel("N/A", tableWidget);

        tableLayout->addWidget(propLabel, row, 0, Qt::AlignLeft);
        tableLayout->addWidget(*valueLabels[row], row, 1, Qt::AlignLeft);
    }

    tableLayout->setColumnStretch(1, 1);
    layout->addWidget(tableWidget);

    // Another separator
    auto *line2 = new QFrame(page);
    line2->setFrameShape(QFrame::HLine);
    line2->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line2->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line2);

    // Enable checkbox
    m_kpodEnableCheckbox = new QCheckBox("Enable K-Pod", page);
    m_kpodEnableCheckbox->setChecked(RadioSettings::instance()->kpodEnabled());

    connect(m_kpodEnableCheckbox, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setKpodEnabled(checked); });

    layout->addWidget(m_kpodEnableCheckbox);

    // Help text
    m_kpodHelpLabel = new QLabel("Connect a K-Pod device to enable this feature.", page);
    m_kpodHelpLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-style: italic;")
                                       .arg(K4Styles::Colors::TextGray)
                                       .arg(K4Styles::Dimensions::FontSizeLarge));
    m_kpodHelpLabel->setWordWrap(true);
    layout->addWidget(m_kpodHelpLabel);

    layout->addStretch();

    // Initialize with current status
    updateKpodStatus();

    return page;
}

void OptionsDialog::updateKpodStatus() {
    if (!m_kpodStatusLabel)
        return;
    if (!m_kpodDevice)
        return;

    KpodDeviceInfo info = m_kpodDevice->deviceInfo();
    bool detected = info.detected;

    // Styling
    QString valueStyle = QString("color: %1; font-size: %2px; padding: 5px;")
                             .arg(K4Styles::Colors::TextWhite)
                             .arg(K4Styles::Dimensions::FontSizeButton);
    QString notDetectedStyle = QString("color: %1; font-size: %2px; font-style: italic; padding: 5px;")
                                   .arg(K4Styles::Colors::TextGray)
                                   .arg(K4Styles::Dimensions::FontSizeButton);

    // Update status label
    QString statusText = detected ? "Detected" : "Not Detected";
    QString statusColor = detected ? K4Styles::Colors::StatusGreen : K4Styles::Colors::ErrorRed;
    m_kpodStatusLabel->setText(statusText);
    m_kpodStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                         .arg(statusColor)
                                         .arg(K4Styles::Dimensions::FontSizePopup));

    // Update device info labels
    auto setLabel = [&](QLabel *label, const QString &value) {
        QString displayValue = value.isEmpty() ? "N/A" : value;
        label->setText(displayValue);
        label->setStyleSheet(displayValue == "N/A" ? notDetectedStyle : valueStyle);
    };

    setLabel(m_kpodProductLabel, detected ? info.productName : "");
    setLabel(m_kpodManufacturerLabel, detected ? info.manufacturer : "");
    setLabel(m_kpodVendorIdLabel,
             detected ? QString("%1 (0x%2)").arg(info.vendorId).arg(info.vendorId, 4, 16, QChar('0')).toUpper() : "");
    setLabel(m_kpodProductIdLabel,
             detected ? QString("%1 (0x%2)").arg(info.productId).arg(info.productId, 4, 16, QChar('0')).toUpper() : "");
    setLabel(m_kpodDeviceTypeLabel, detected ? "USB HID (Human Interface Device)" : "");
    setLabel(m_kpodFirmwareLabel, detected ? info.firmwareVersion : "");
    setLabel(m_kpodDeviceIdLabel, detected ? info.deviceId : "");

    // Update checkbox enabled state and styling
    m_kpodEnableCheckbox->setEnabled(detected);
    if (detected) {
        m_kpodEnableCheckbox->setStyleSheet(QString("QCheckBox { color: %1; font-size: %2px; spacing: %3px; }"
                                                    "QCheckBox::indicator { width: %4px; height: %4px; }")
                                                .arg(K4Styles::Colors::TextWhite)
                                                .arg(K4Styles::Dimensions::FontSizePopup)
                                                .arg(K4Styles::Dimensions::BorderRadiusLarge)
                                                .arg(K4Styles::Dimensions::CheckboxSize));
    } else {
        m_kpodEnableCheckbox->setStyleSheet(QString("QCheckBox { color: %1; font-size: %2px; spacing: %3px; }"
                                                    "QCheckBox::indicator { width: %4px; height: %4px; }")
                                                .arg(K4Styles::Colors::TextGray)
                                                .arg(K4Styles::Dimensions::FontSizePopup)
                                                .arg(K4Styles::Dimensions::BorderRadiusLarge)
                                                .arg(K4Styles::Dimensions::CheckboxSize));
    }

    // Update help text
    m_kpodHelpLabel->setText(detected ? "When enabled, the K-Pod VFO knob and buttons will control the radio."
                                      : "Connect a K-Pod device to enable this feature.");
}

QWidget *OptionsDialog::createAudioInputPage() {
    auto *page = new QWidget(this);
    page->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::Background));

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    // Title
    auto *titleLabel = new QLabel("Audio Input", page);
    titleLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                  .arg(K4Styles::Colors::AccentAmber)
                                  .arg(K4Styles::Dimensions::FontSizeTitle));
    layout->addWidget(titleLabel);

    // Separator line
    auto *line = new QFrame(page);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line);

    // === Microphone Device Selection ===
    auto *deviceLabel = new QLabel("Microphone:", page);
    deviceLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                   .arg(K4Styles::Colors::TextGray)
                                   .arg(K4Styles::Dimensions::FontSizePopup));
    layout->addWidget(deviceLabel);

    m_micDeviceCombo = new QComboBox(page);
    m_micDeviceCombo->setStyleSheet(
        QString("QComboBox { background-color: %1; color: %2; border: 1px solid %3; "
                "           padding: %6px; font-size: %5px; border-radius: %7px; }"
                "QComboBox:focus { border-color: %4; }"
                "QComboBox::drop-down { border: none; width: 20px; }"
                "QComboBox::down-arrow { image: none; border-left: 5px solid transparent; "
                "           border-right: 5px solid transparent; border-top: 5px solid %2; }"
                "QComboBox QAbstractItemView { background-color: %1; color: %2; selection-background-color: %4; }")
            .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextWhite, K4Styles::Colors::DialogBorder,
                 K4Styles::Colors::AccentAmber)
            .arg(K4Styles::Dimensions::FontSizePopup)
            .arg(K4Styles::Dimensions::PaddingSmall)
            .arg(K4Styles::Dimensions::SliderBorderRadius));
    populateMicDevices();
    connect(m_micDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &OptionsDialog::onMicDeviceChanged);
    layout->addWidget(m_micDeviceCombo);

    layout->addSpacing(K4Styles::Dimensions::PaddingMedium);

    // === Microphone Gain ===
    auto *gainLayout = new QHBoxLayout();
    auto *gainLabel = new QLabel("Mic Gain:", page);
    gainLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::FontSizePopup));
    gainLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);
    gainLayout->addWidget(gainLabel);

    m_micGainSlider = new QSlider(Qt::Horizontal, page);
    m_micGainSlider->setRange(0, 100);
    m_micGainSlider->setValue(RadioSettings::instance()->micGain());
    m_micGainSlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::TextDark, K4Styles::Colors::AccentAmber));
    connect(m_micGainSlider, &QSlider::valueChanged, this, &OptionsDialog::onMicGainChanged);
    gainLayout->addWidget(m_micGainSlider, 1);

    m_micGainValueLabel = new QLabel(QString("%1%").arg(m_micGainSlider->value()), page);
    m_micGainValueLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                           .arg(K4Styles::Colors::TextWhite)
                                           .arg(K4Styles::Dimensions::FontSizePopup));
    m_micGainValueLabel->setFixedWidth(K4Styles::Dimensions::SliderValueLabelWidth);
    m_micGainValueLabel->setAlignment(Qt::AlignRight);
    gainLayout->addWidget(m_micGainValueLabel);

    layout->addLayout(gainLayout);

    auto *gainHelpLabel = new QLabel("Adjust the microphone input level. 50% is unity gain.", page);
    gainHelpLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-style: italic;")
                                     .arg(K4Styles::Colors::TextGray)
                                     .arg(K4Styles::Dimensions::FontSizeLarge));
    layout->addWidget(gainHelpLabel);

    layout->addSpacing(K4Styles::Dimensions::PaddingLarge);

    // === Microphone Test Section ===
    auto *line2 = new QFrame(page);
    line2->setFrameShape(QFrame::HLine);
    line2->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line2->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line2);

    auto *testSectionLabel = new QLabel("Microphone Test", page);
    testSectionLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                        .arg(K4Styles::Colors::TextWhite)
                                        .arg(K4Styles::Dimensions::FontSizePopup));
    layout->addWidget(testSectionLabel);

    auto *testHelpLabel =
        new QLabel("Click the Test button to activate the microphone and check the input level.", page);
    testHelpLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                     .arg(K4Styles::Colors::TextGray)
                                     .arg(K4Styles::Dimensions::FontSizeButton));
    testHelpLabel->setWordWrap(true);
    layout->addWidget(testHelpLabel);

    layout->addSpacing(5); // Small gap before meter

    // Mic Level Meter
    auto *meterLayout = new QHBoxLayout();
    auto *meterLabel = new QLabel("Level:", page);
    meterLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                  .arg(K4Styles::Colors::TextGray)
                                  .arg(K4Styles::Dimensions::FontSizePopup));
    meterLabel->setFixedWidth(50);
    meterLayout->addWidget(meterLabel);

    m_micMeter = new MicMeterWidget(page);
    meterLayout->addWidget(m_micMeter, 1);

    layout->addLayout(meterLayout);

    layout->addSpacing(K4Styles::Dimensions::PaddingMedium);

    // Test Button
    m_micTestBtn = new QPushButton("Test Microphone", page);
    m_micTestBtn->setCheckable(true);
    m_micTestBtn->setStyleSheet(QString("QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
                                        "             padding: 10px 20px; font-size: %5px; border-radius: 4px; }"
                                        "QPushButton:hover { background-color: %6; }"
                                        "QPushButton:checked { background-color: %4; color: %1; border-color: %4; }")
                                    .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextWhite,
                                         K4Styles::Colors::DialogBorder, K4Styles::Colors::AccentAmber)
                                    .arg(K4Styles::Dimensions::FontSizePopup)
                                    .arg(K4Styles::Colors::GradientBottom));
    connect(m_micTestBtn, &QPushButton::toggled, this, &OptionsDialog::onMicTestToggled);
    layout->addWidget(m_micTestBtn);

    // Connect to AudioEngine for mic level updates (queued — AudioEngine lives on audio thread)
    if (m_audioEngine) {
        connect(m_audioEngine, &AudioEngine::micLevelChanged, this, &OptionsDialog::onMicLevelChanged,
                Qt::QueuedConnection);
    }

    layout->addStretch();
    return page;
}

void OptionsDialog::populateMicDevices() {
    if (!m_micDeviceCombo)
        return;

    m_micDeviceCombo->clear();

    auto devices = AudioEngine::availableInputDevices();
    QString savedDevice = RadioSettings::instance()->micDevice();
    int selectedIndex = 0;

    for (int i = 0; i < devices.size(); i++) {
        const auto &device = devices[i];
        m_micDeviceCombo->addItem(device.second, device.first);

        // Find the saved device
        if (device.first == savedDevice) {
            selectedIndex = i;
        }
    }

    m_micDeviceCombo->setCurrentIndex(selectedIndex);
}

void OptionsDialog::onMicDeviceChanged(int index) {
    if (!m_micDeviceCombo || index < 0)
        return;

    QString deviceId = m_micDeviceCombo->currentData().toString();
    RadioSettings::instance()->setMicDevice(deviceId);

    if (m_audioEngine) {
        QMetaObject::invokeMethod(m_audioEngine, "setMicDevice", Qt::QueuedConnection, Q_ARG(QString, deviceId));
    }
}

void OptionsDialog::onMicGainChanged(int value) {
    if (m_micGainValueLabel) {
        m_micGainValueLabel->setText(QString("%1%").arg(value));
    }

    RadioSettings::instance()->setMicGain(value);

    if (m_audioEngine) {
        m_audioEngine->setMicGain(value / 100.0f);
    }
}

void OptionsDialog::onMicTestToggled(bool checked) {
    m_micTestActive = checked;

    if (m_micTestBtn) {
        m_micTestBtn->setText(checked ? "Stop Test" : "Test Microphone");
    }

    if (m_audioEngine) {
        QMetaObject::invokeMethod(m_audioEngine, "setMicEnabled", Qt::QueuedConnection, Q_ARG(bool, checked));
    }

    // Reset meter when stopping
    if (!checked && m_micMeter) {
        m_micMeter->setLevel(0.0f);
    }
}

void OptionsDialog::onMicLevelChanged(float level) {
    if (m_micTestActive && m_micMeter) {
        // Scale level for better visualization (RMS tends to be low)
        float scaledLevel = qBound(0.0f, level * 5.0f, 1.0f);
        m_micMeter->setLevel(scaledLevel);
    }
}

QWidget *OptionsDialog::createAudioOutputPage() {
    auto *page = new QWidget(this);
    page->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::Background));

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    // Title
    auto *titleLabel = new QLabel("Audio Output", page);
    titleLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                  .arg(K4Styles::Colors::AccentAmber)
                                  .arg(K4Styles::Dimensions::FontSizeTitle));
    layout->addWidget(titleLabel);

    // Separator line
    auto *line = new QFrame(page);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line);

    // === Audio Enable/Disable ===
    auto *audioEnableCheckbox = new QCheckBox("Enable Audio", page);
    audioEnableCheckbox->setStyleSheet(QString("QCheckBox { color: %1; font-size: %2px; spacing: %3px; }"
                                               "QCheckBox::indicator { width: %4px; height: %4px; }")
                                           .arg(K4Styles::Colors::TextWhite)
                                           .arg(K4Styles::Dimensions::FontSizePopup)
                                           .arg(K4Styles::Dimensions::BorderRadiusLarge)
                                           .arg(K4Styles::Dimensions::CheckboxSize));
    audioEnableCheckbox->setChecked(RadioSettings::instance()->audioEnabled());
    connect(audioEnableCheckbox, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setAudioEnabled(checked); });
    layout->addWidget(audioEnableCheckbox);

    auto *noiseFilterCheckbox = new QCheckBox("RX Noise Filter (3.5 kHz low-pass)", page);
    noiseFilterCheckbox->setStyleSheet(QString("QCheckBox { color: %1; font-size: %2px; spacing: %3px; }"
                                               "QCheckBox::indicator { width: %4px; height: %4px; }")
                                           .arg(K4Styles::Colors::TextWhite)
                                           .arg(K4Styles::Dimensions::FontSizePopup)
                                           .arg(K4Styles::Dimensions::BorderRadiusLarge)
                                           .arg(K4Styles::Dimensions::CheckboxSize));
    noiseFilterCheckbox->setChecked(RadioSettings::instance()->noiseFilterEnabled());
    connect(noiseFilterCheckbox, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setNoiseFilterEnabled(checked); });
    layout->addWidget(noiseFilterCheckbox);

    layout->addSpacing(K4Styles::Dimensions::PaddingMedium);

    // === Speaker Device Selection ===
    auto *deviceLabel = new QLabel("Speaker:", page);
    deviceLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                   .arg(K4Styles::Colors::TextGray)
                                   .arg(K4Styles::Dimensions::FontSizePopup));
    layout->addWidget(deviceLabel);

    m_speakerDeviceCombo = new QComboBox(page);
    m_speakerDeviceCombo->setStyleSheet(
        QString("QComboBox { background-color: %1; color: %2; border: 1px solid %3; "
                "           padding: %6px; font-size: %5px; border-radius: %7px; }"
                "QComboBox:focus { border-color: %4; }"
                "QComboBox::drop-down { border: none; width: 20px; }"
                "QComboBox::down-arrow { image: none; border-left: 5px solid transparent; "
                "           border-right: 5px solid transparent; border-top: 5px solid %2; }"
                "QComboBox QAbstractItemView { background-color: %1; color: %2; selection-background-color: %4; }")
            .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextWhite, K4Styles::Colors::DialogBorder,
                 K4Styles::Colors::AccentAmber)
            .arg(K4Styles::Dimensions::FontSizePopup)
            .arg(K4Styles::Dimensions::PaddingSmall)
            .arg(K4Styles::Dimensions::SliderBorderRadius));
    populateSpeakerDevices();
    connect(m_speakerDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &OptionsDialog::onSpeakerDeviceChanged);
    layout->addWidget(m_speakerDeviceCombo);

    layout->addSpacing(K4Styles::Dimensions::PaddingMedium);

    // Help text
    auto *helpLabel = new QLabel("Select the audio output device for radio receive audio. "
                                 "Volume is controlled by the MAIN and SUB sliders on the side panel.",
                                 page);
    helpLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-style: italic;")
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::FontSizeLarge));
    helpLabel->setWordWrap(true);
    layout->addWidget(helpLabel);

    layout->addStretch();
    return page;
}

void OptionsDialog::populateSpeakerDevices() {
    if (!m_speakerDeviceCombo)
        return;

    m_speakerDeviceCombo->clear();

    auto devices = AudioEngine::availableOutputDevices();
    QString savedDevice = RadioSettings::instance()->speakerDevice();
    int selectedIndex = 0;

    for (int i = 0; i < devices.size(); i++) {
        const auto &device = devices[i];
        m_speakerDeviceCombo->addItem(device.second, device.first);

        // Find the saved device
        if (device.first == savedDevice) {
            selectedIndex = i;
        }
    }

    m_speakerDeviceCombo->setCurrentIndex(selectedIndex);
}

void OptionsDialog::onSpeakerDeviceChanged(int index) {
    if (!m_speakerDeviceCombo || index < 0)
        return;

    QString deviceId = m_speakerDeviceCombo->currentData().toString();
    RadioSettings::instance()->setSpeakerDevice(deviceId);

    if (m_audioEngine) {
        QMetaObject::invokeMethod(m_audioEngine, "setOutputDevice", Qt::QueuedConnection, Q_ARG(QString, deviceId));
    }
}

QWidget *OptionsDialog::createRigControlPage() {
    auto *page = new QWidget(this);
    page->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::Background));

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    // Title
    auto *titleLabel = new QLabel("CAT Server", page);
    titleLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                  .arg(K4Styles::Colors::AccentAmber)
                                  .arg(K4Styles::Dimensions::FontSizeTitle));
    layout->addWidget(titleLabel);

    // Description
    auto *descLabel = new QLabel("Enable the CAT server to allow external applications (WSJT-X, MacLoggerDX, fldigi) "
                                 "to connect using their native Elecraft K4 support. No protocol translation needed.",
                                 page);
    descLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::FontSizeButton));
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    // Separator line
    auto *line = new QFrame(page);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line);

    // Status indicator
    auto *statusLayout = new QHBoxLayout();
    auto *statusTitleLabel = new QLabel("Status:", page);
    statusTitleLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                        .arg(K4Styles::Colors::TextGray)
                                        .arg(K4Styles::Dimensions::FontSizePopup));
    statusTitleLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_catServerStatusLabel = new QLabel("Not running", page);
    m_catServerStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                              .arg(K4Styles::Colors::ErrorRed)
                                              .arg(K4Styles::Dimensions::FontSizePopup));

    statusLayout->addWidget(statusTitleLabel);
    statusLayout->addWidget(m_catServerStatusLabel);
    statusLayout->addStretch();
    layout->addLayout(statusLayout);

    // Clients indicator
    auto *clientsLayout = new QHBoxLayout();
    auto *clientsTitleLabel = new QLabel("Clients:", page);
    clientsTitleLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                         .arg(K4Styles::Colors::TextGray)
                                         .arg(K4Styles::Dimensions::FontSizePopup));
    clientsTitleLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_catServerClientsLabel = new QLabel("0 connected", page);
    m_catServerClientsLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                               .arg(K4Styles::Colors::TextWhite)
                                               .arg(K4Styles::Dimensions::FontSizePopup));

    clientsLayout->addWidget(clientsTitleLabel);
    clientsLayout->addWidget(m_catServerClientsLabel);
    clientsLayout->addStretch();
    layout->addLayout(clientsLayout);

    // Separator line
    auto *line2 = new QFrame(page);
    line2->setFrameShape(QFrame::HLine);
    line2->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line2->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line2);

    // Connection Settings section
    auto *sectionLabel = new QLabel("Settings", page);
    sectionLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                    .arg(K4Styles::Colors::TextWhite)
                                    .arg(K4Styles::Dimensions::FontSizePopup));
    layout->addWidget(sectionLabel);

    // Port input
    auto *portLayout = new QHBoxLayout();
    auto *portLabel = new QLabel("Port:", page);
    portLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::FontSizePopup));
    portLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_catServerPortEdit = new QLineEdit(page);
    m_catServerPortEdit->setPlaceholderText("9299");
    m_catServerPortEdit->setFixedWidth(K4Styles::Dimensions::InputFieldWidthSmall);
    m_catServerPortEdit->setStyleSheet(QString("QLineEdit { background-color: %1; color: %2; border: 1px solid %3; "
                                               "           padding: %6px; font-size: %5px; border-radius: %7px; }"
                                               "QLineEdit:focus { border-color: %4; }")
                                           .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextWhite,
                                                K4Styles::Colors::DialogBorder, K4Styles::Colors::AccentAmber)
                                           .arg(K4Styles::Dimensions::FontSizePopup)
                                           .arg(K4Styles::Dimensions::PaddingSmall)
                                           .arg(K4Styles::Dimensions::SliderBorderRadius));
    m_catServerPortEdit->setText(QString::number(RadioSettings::instance()->catServerPort()));

    auto *portHint = new QLabel("(default: 9299)", page);
    portHint->setStyleSheet(QString("color: %1; font-size: %2px;")
                                .arg(K4Styles::Colors::TextGray)
                                .arg(K4Styles::Dimensions::FontSizeLarge));

    portLayout->addWidget(portLabel);
    portLayout->addWidget(m_catServerPortEdit);
    portLayout->addWidget(portHint);
    portLayout->addStretch();
    layout->addLayout(portLayout);

    // Separator line
    auto *line3 = new QFrame(page);
    line3->setFrameShape(QFrame::HLine);
    line3->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line3->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line3);

    // Enable checkbox
    m_catServerEnableCheckbox = new QCheckBox("Enable CAT server", page);
    m_catServerEnableCheckbox->setStyleSheet(QString("QCheckBox { color: %1; font-size: %2px; spacing: %3px; }"
                                                     "QCheckBox::indicator { width: %4px; height: %4px; }")
                                                 .arg(K4Styles::Colors::TextWhite)
                                                 .arg(K4Styles::Dimensions::FontSizePopup)
                                                 .arg(K4Styles::Dimensions::BorderRadiusLarge)
                                                 .arg(K4Styles::Dimensions::CheckboxSize));
    m_catServerEnableCheckbox->setChecked(RadioSettings::instance()->catServerEnabled());
    layout->addWidget(m_catServerEnableCheckbox);

    // Help text
    auto *helpLabel = new QLabel("Configure external apps to use Elecraft K4, host 127.0.0.1, and the port above. "
                                 "Commands are forwarded to the real K4.",
                                 page);
    helpLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-style: italic;")
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::FontSizeLarge));
    helpLabel->setWordWrap(true);
    layout->addWidget(helpLabel);

    // Connect signals to save settings
    connect(m_catServerPortEdit, &QLineEdit::editingFinished, this, [this]() {
        bool ok;
        quint16 port = m_catServerPortEdit->text().toUShort(&ok);
        if (ok && port >= 1024) {
            RadioSettings::instance()->setCatServerPort(port);
        } else {
            // Reset to current value if invalid
            m_catServerPortEdit->setText(QString::number(RadioSettings::instance()->catServerPort()));
        }
    });

    connect(m_catServerEnableCheckbox, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setCatServerEnabled(checked); });

    layout->addStretch();

    // Initialize status display
    updateCatServerStatus();

    return page;
}

void OptionsDialog::updateCatServerStatus() {
    if (!m_catServerStatusLabel || !m_catServerClientsLabel) {
        return;
    }

    bool isListening = m_catServer && m_catServer->isListening();

    if (isListening) {
        m_catServerStatusLabel->setText(QString("Listening on port %1").arg(m_catServer->port()));
        m_catServerStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                                  .arg(K4Styles::Colors::StatusGreen)
                                                  .arg(K4Styles::Dimensions::FontSizePopup));
    } else {
        m_catServerStatusLabel->setText("Not running");
        m_catServerStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                                  .arg(K4Styles::Colors::ErrorRed)
                                                  .arg(K4Styles::Dimensions::FontSizePopup));
    }

    int clientCount = m_catServer ? m_catServer->clientCount() : 0;
    m_catServerClientsLabel->setText(QString("%1 connected").arg(clientCount));
}

QWidget *OptionsDialog::createCwKeyerPage() {
    auto *page = new QWidget(this);
    page->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::Background));

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    // Title
    auto *titleLabel = new QLabel("CW Keyer", page);
    titleLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                  .arg(K4Styles::Colors::AccentAmber)
                                  .arg(K4Styles::Dimensions::FontSizeTitle));
    layout->addWidget(titleLabel);

    // Description (dynamic based on device type)
    m_cwKeyerDescLabel = new QLabel(page);
    m_cwKeyerDescLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                          .arg(K4Styles::Colors::TextGray)
                                          .arg(K4Styles::Dimensions::FontSizeButton));
    m_cwKeyerDescLabel->setWordWrap(true);
    layout->addWidget(m_cwKeyerDescLabel);

    // Device Type selector
    auto *deviceTypeLayout = new QHBoxLayout();
    auto *deviceTypeLabel = new QLabel("Device Type:", page);
    deviceTypeLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                       .arg(K4Styles::Colors::TextGray)
                                       .arg(K4Styles::Dimensions::FontSizePopup));
    deviceTypeLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_cwKeyerDeviceTypeCombo = new QComboBox(page);
    m_cwKeyerDeviceTypeCombo->setStyleSheet(
        QString("QComboBox { background-color: %1; color: %2; border: 1px solid %3; "
                "           padding: %6px; font-size: %5px; border-radius: %7px; }"
                "QComboBox:focus { border-color: %4; }"
                "QComboBox::drop-down { border: none; width: 20px; }"
                "QComboBox::down-arrow { image: none; border-left: 5px solid transparent; "
                "           border-right: 5px solid transparent; border-top: 5px solid %2; }"
                "QComboBox QAbstractItemView { background-color: %1; color: %2; selection-background-color: %4; }")
            .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextWhite, K4Styles::Colors::DialogBorder,
                 K4Styles::Colors::AccentAmber)
            .arg(K4Styles::Dimensions::FontSizePopup)
            .arg(K4Styles::Dimensions::PaddingSmall)
            .arg(K4Styles::Dimensions::SliderBorderRadius));
    m_cwKeyerDeviceTypeCombo->addItem("HaliKey V1.4", 0);
    m_cwKeyerDeviceTypeCombo->addItem("HaliKey MIDI", 1);

    int savedDeviceType = RadioSettings::instance()->halikeyDeviceType();
    m_cwKeyerDeviceTypeCombo->setCurrentIndex(savedDeviceType);
    updateCwKeyerDescription();

    connect(m_cwKeyerDeviceTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        int type = m_cwKeyerDeviceTypeCombo->itemData(index).toInt();
        RadioSettings::instance()->setHalikeyDeviceType(type);
        // Disconnect if connected, since device type changed
        if (m_halikeyDevice && m_halikeyDevice->isConnected()) {
            m_halikeyDevice->closePort();
        }
        updateCwKeyerDescription();
        populateCwKeyerPorts();
    });

    deviceTypeLayout->addWidget(deviceTypeLabel);
    deviceTypeLayout->addWidget(m_cwKeyerDeviceTypeCombo, 1);
    layout->addLayout(deviceTypeLayout);

    // Separator line
    auto *line = new QFrame(page);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line);

    // Status indicator
    auto *statusLayout = new QHBoxLayout();
    auto *statusTitleLabel = new QLabel("Status:", page);
    statusTitleLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                        .arg(K4Styles::Colors::TextGray)
                                        .arg(K4Styles::Dimensions::FontSizePopup));
    statusTitleLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_cwKeyerStatusLabel = new QLabel("Not Connected", page);
    m_cwKeyerStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                            .arg(K4Styles::Colors::ErrorRed)
                                            .arg(K4Styles::Dimensions::FontSizePopup));

    statusLayout->addWidget(statusTitleLabel);
    statusLayout->addWidget(m_cwKeyerStatusLabel);
    statusLayout->addStretch();
    layout->addLayout(statusLayout);

    // Separator line
    auto *line2 = new QFrame(page);
    line2->setFrameShape(QFrame::HLine);
    line2->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line2->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line2);

    // Connection Settings section
    auto *sectionLabel = new QLabel("Connection Settings", page);
    sectionLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                    .arg(K4Styles::Colors::TextWhite)
                                    .arg(K4Styles::Dimensions::FontSizePopup));
    layout->addWidget(sectionLabel);

    // Port selection
    auto *portLayout = new QHBoxLayout();
    auto *portLabel = new QLabel("Port:", page);
    portLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::FontSizePopup));
    portLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_cwKeyerPortCombo = new QComboBox(page);
    m_cwKeyerPortCombo->setStyleSheet(
        QString("QComboBox { background-color: %1; color: %2; border: 1px solid %3; "
                "           padding: %6px; font-size: %5px; border-radius: %7px; }"
                "QComboBox:focus { border-color: %4; }"
                "QComboBox::drop-down { border: none; width: 20px; }"
                "QComboBox::down-arrow { image: none; border-left: 5px solid transparent; "
                "           border-right: 5px solid transparent; border-top: 5px solid %2; }"
                "QComboBox QAbstractItemView { background-color: %1; color: %2; selection-background-color: %4; }")
            .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextWhite, K4Styles::Colors::DialogBorder,
                 K4Styles::Colors::AccentAmber)
            .arg(K4Styles::Dimensions::FontSizePopup)
            .arg(K4Styles::Dimensions::PaddingSmall)
            .arg(K4Styles::Dimensions::SliderBorderRadius));
    populateCwKeyerPorts();

    m_cwKeyerRefreshBtn = new QPushButton("Refresh", page);
    m_cwKeyerRefreshBtn->setStyleSheet(
        QString("QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
                "             padding: %6px 12px; font-size: %4px; border-radius: %7px; }"
                "QPushButton:hover { background-color: %5; }")
            .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextWhite, K4Styles::Colors::DialogBorder)
            .arg(K4Styles::Dimensions::FontSizePopup)
            .arg(K4Styles::Colors::GradientBottom)
            .arg(K4Styles::Dimensions::PaddingSmall)
            .arg(K4Styles::Dimensions::SliderBorderRadius));
    connect(m_cwKeyerRefreshBtn, &QPushButton::clicked, this, &OptionsDialog::onCwKeyerRefreshClicked);

    portLayout->addWidget(portLabel);
    portLayout->addWidget(m_cwKeyerPortCombo, 1);
    portLayout->addWidget(m_cwKeyerRefreshBtn);
    layout->addLayout(portLayout);

    // Connect/Disconnect button
    m_cwKeyerConnectBtn = new QPushButton("Connect", page);
    m_cwKeyerConnectBtn->setStyleSheet(
        QString("QPushButton { background-color: %1; color: %2; border: 1px solid %3; "
                "             padding: 10px 20px; font-size: %4px; border-radius: 4px; }"
                "QPushButton:hover { background-color: %5; }")
            .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextWhite, K4Styles::Colors::DialogBorder)
            .arg(K4Styles::Dimensions::FontSizePopup)
            .arg(K4Styles::Colors::GradientBottom));
    connect(m_cwKeyerConnectBtn, &QPushButton::clicked, this, &OptionsDialog::onCwKeyerConnectClicked);
    layout->addWidget(m_cwKeyerConnectBtn);

    // Separator line
    auto *line3 = new QFrame(page);
    line3->setFrameShape(QFrame::HLine);
    line3->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line3->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line3);

    // Separator line
    auto *line5 = new QFrame(page);
    line5->setFrameShape(QFrame::HLine);
    line5->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line5->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line5);

    // Sidetone Settings section
    auto *sidetoneLabel = new QLabel("Sidetone Settings", page);
    sidetoneLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                     .arg(K4Styles::Colors::TextWhite)
                                     .arg(K4Styles::Dimensions::FontSizePopup));
    layout->addWidget(sidetoneLabel);

    // Sidetone volume slider
    auto *volumeLayout = new QHBoxLayout();
    auto *volumeLabel = new QLabel("Volume:", page);
    volumeLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                   .arg(K4Styles::Colors::TextGray)
                                   .arg(K4Styles::Dimensions::FontSizePopup));
    volumeLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_sidetoneVolumeSlider = new QSlider(Qt::Horizontal, page);
    m_sidetoneVolumeSlider->setRange(0, 100);
    m_sidetoneVolumeSlider->setValue(RadioSettings::instance()->sidetoneVolume());
    m_sidetoneVolumeSlider->setStyleSheet(
        K4Styles::sliderHorizontal(K4Styles::Colors::DarkBackground, K4Styles::Colors::AccentAmber));

    m_sidetoneVolumeValueLabel = new QLabel(QString("%1%").arg(RadioSettings::instance()->sidetoneVolume()), page);
    m_sidetoneVolumeValueLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                                  .arg(K4Styles::Colors::TextWhite)
                                                  .arg(K4Styles::Dimensions::FontSizePopup));
    m_sidetoneVolumeValueLabel->setFixedWidth(K4Styles::Dimensions::SliderValueLabelWidth);

    connect(m_sidetoneVolumeSlider, &QSlider::valueChanged, this, [this](int value) {
        m_sidetoneVolumeValueLabel->setText(QString("%1%").arg(value));
        RadioSettings::instance()->setSidetoneVolume(value);
    });

    volumeLayout->addWidget(volumeLabel);
    volumeLayout->addWidget(m_sidetoneVolumeSlider, 1);
    volumeLayout->addWidget(m_sidetoneVolumeValueLabel);
    layout->addLayout(volumeLayout);

    // Sidetone help text
    auto *sidetoneHelpLabel =
        new QLabel("Local sidetone volume for CW keying feedback. Frequency is linked to K4's CW pitch setting.", page);
    sidetoneHelpLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-style: italic;")
                                         .arg(K4Styles::Colors::TextGray)
                                         .arg(K4Styles::Dimensions::FontSizeLarge));
    sidetoneHelpLabel->setWordWrap(true);
    layout->addWidget(sidetoneHelpLabel);

    layout->addStretch();

    // Initialize status
    updateCwKeyerStatus();

    return page;
}

void OptionsDialog::populateCwKeyerPorts() {
    if (!m_cwKeyerPortCombo)
        return;

    // Block signals to avoid triggering save during repopulation
    m_cwKeyerPortCombo->blockSignals(true);
    m_cwKeyerPortCombo->clear();

    int deviceType = m_cwKeyerDeviceTypeCombo ? m_cwKeyerDeviceTypeCombo->currentData().toInt() : 0;
    QString savedPort = RadioSettings::instance()->halikeyPortName();
    int selectedIndex = -1;

    if (deviceType == 1) {
        // MIDI device — enumerate MIDI ports
        QStringList midiDevices = HalikeyDevice::availableMidiDevices();
        for (int i = 0; i < midiDevices.size(); i++) {
            m_cwKeyerPortCombo->addItem(midiDevices[i], midiDevices[i]);
            if (midiDevices[i] == savedPort) {
                selectedIndex = i;
            }
        }
        // Auto-select first HaliKey MIDI device if no saved selection matched
        if (selectedIndex < 0) {
            for (int i = 0; i < midiDevices.size(); i++) {
                if (midiDevices[i].contains("HaliKey", Qt::CaseInsensitive)) {
                    selectedIndex = i;
                    break;
                }
            }
        }
    } else {
        // V14 device — enumerate serial ports
        auto ports = HalikeyDevice::availablePortsDetailed();
        for (int i = 0; i < ports.size(); i++) {
            m_cwKeyerPortCombo->addItem(ports[i].portName, ports[i].portName);
            if (ports[i].portName == savedPort) {
                selectedIndex = i;
            }
        }
    }

    if (selectedIndex >= 0) {
        m_cwKeyerPortCombo->setCurrentIndex(selectedIndex);
    }
    m_cwKeyerPortCombo->blockSignals(false);

    // Save selection when changed (reconnect since we may be called multiple times)
    m_cwKeyerPortCombo->disconnect(this);
    connect(m_cwKeyerPortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (index >= 0) {
            QString portName = m_cwKeyerPortCombo->itemData(index).toString();
            RadioSettings::instance()->setHalikeyPortName(portName);
        }
    });
}

void OptionsDialog::updateCwKeyerDescription() {
    if (!m_cwKeyerDescLabel || !m_cwKeyerDeviceTypeCombo)
        return;

    int type = m_cwKeyerDeviceTypeCombo->currentData().toInt();
    if (type == 1) {
        m_cwKeyerDescLabel->setText("Connect a HaliKey MIDI paddle interface to send CW via the K4's keyer. "
                                    "The HaliKey MIDI uses standard MIDI note events to detect paddle and PTT inputs.");
    } else {
        m_cwKeyerDescLabel->setText("Connect a HaliKey paddle interface to send CW via the K4's keyer. "
                                    "The HaliKey uses serial port flow control signals to detect paddle inputs.");
    }
}

void OptionsDialog::onCwKeyerRefreshClicked() {
    populateCwKeyerPorts();
}

void OptionsDialog::onCwKeyerConnectClicked() {
    if (!m_halikeyDevice)
        return;

    if (m_halikeyDevice->isConnected()) {
        // Disconnect
        m_halikeyDevice->closePort();
    } else {
        // Connect — use port name from item data (without annotation suffix)
        QString portName = m_cwKeyerPortCombo->currentData().toString();
        if (!portName.isEmpty()) {
            RadioSettings::instance()->setHalikeyPortName(portName);
            m_halikeyDevice->openPort(portName);
        }
    }
}

void OptionsDialog::updateCwKeyerStatus() {
    if (!m_cwKeyerStatusLabel || !m_cwKeyerConnectBtn)
        return;

    bool isConnected = m_halikeyDevice && m_halikeyDevice->isConnected();

    if (isConnected) {
        m_cwKeyerStatusLabel->setText(QString("Connected to %1").arg(m_halikeyDevice->portName()));
        m_cwKeyerStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                                .arg(K4Styles::Colors::StatusGreen)
                                                .arg(K4Styles::Dimensions::FontSizePopup));
        m_cwKeyerConnectBtn->setText("Disconnect");
    } else {
        m_cwKeyerStatusLabel->setText("Not Connected");
        m_cwKeyerStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                                .arg(K4Styles::Colors::ErrorRed)
                                                .arg(K4Styles::Dimensions::FontSizePopup));
        m_cwKeyerConnectBtn->setText("Connect");
    }
}

void OptionsDialog::updateColorButton(QPushButton *btn, const QColor &color) {
    btn->setStyleSheet(
        QString("QPushButton { background-color: %1; border: 1px solid %2; border-radius: 4px; min-width: 40px; "
                "min-height: 24px; }"
                "QPushButton:hover { border-color: %3; }")
            .arg(color.name(), K4Styles::Colors::DialogBorder, K4Styles::Colors::BorderHover));
}

QWidget *OptionsDialog::createN1mmPage() {
    auto *page = new QWidget(this);
    page->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::Background));

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    // Title
    auto *titleLabel = new QLabel("N1MM Spots", page);
    titleLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                  .arg(K4Styles::Colors::AccentAmber)
                                  .arg(K4Styles::Dimensions::FontSizeTitle));
    layout->addWidget(titleLabel);

    // Description
    auto *descLabel =
        new QLabel("Display DX spots from N1MM Logger+ on the panadapter. N1MM broadcasts spot data via UDP.", page);
    descLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::FontSizeButton));
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    // Separator
    auto *line = new QFrame(page);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line);

    // Status indicator
    auto *statusLayout = new QHBoxLayout();
    auto *statusTitleLabel = new QLabel("Status:", page);
    statusTitleLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                        .arg(K4Styles::Colors::TextGray)
                                        .arg(K4Styles::Dimensions::FontSizePopup));
    statusTitleLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_n1mmStatusLabel = new QLabel("Not running", page);
    statusLayout->addWidget(statusTitleLabel);
    statusLayout->addWidget(m_n1mmStatusLabel);
    statusLayout->addStretch();
    layout->addLayout(statusLayout);

    // Separator
    auto *line2 = new QFrame(page);
    line2->setFrameShape(QFrame::HLine);
    line2->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line2->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line2);

    // Settings section
    auto *sectionLabel = new QLabel("Settings", page);
    sectionLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                    .arg(K4Styles::Colors::TextWhite)
                                    .arg(K4Styles::Dimensions::FontSizePopup));
    layout->addWidget(sectionLabel);

    QString spinBoxStyle =
        QString("QSpinBox { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: %4px; "
                "font-size: %5px; }"
                "QSpinBox::up-button, QSpinBox::down-button { width: 16px; border: none; }")
            .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextWhite, K4Styles::Colors::DialogBorder)
            .arg(K4Styles::Dimensions::PaddingSmall)
            .arg(K4Styles::Dimensions::FontSizePopup);

    // Port
    auto *portLayout = new QHBoxLayout();
    auto *portLabel = new QLabel("UDP Port:", page);
    portLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::FontSizePopup));
    portLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_n1mmPortSpin = new QSpinBox(page);
    m_n1mmPortSpin->setRange(1024, 65535);
    m_n1mmPortSpin->setValue(RadioSettings::instance()->n1mmPort());
    m_n1mmPortSpin->setFixedWidth(K4Styles::Dimensions::InputFieldWidthSmall);
    m_n1mmPortSpin->setStyleSheet(spinBoxStyle);

    auto *portHint = new QLabel("(default: 12060)", page);
    portHint->setStyleSheet(QString("color: %1; font-size: %2px;")
                                .arg(K4Styles::Colors::TextGray)
                                .arg(K4Styles::Dimensions::FontSizeLarge));

    portLayout->addWidget(portLabel);
    portLayout->addWidget(m_n1mmPortSpin);
    portLayout->addWidget(portHint);
    portLayout->addStretch();
    layout->addLayout(portLayout);

    // Expiry
    auto *expiryLayout = new QHBoxLayout();
    auto *expiryLabel = new QLabel("Spot Expiry:", page);
    expiryLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                   .arg(K4Styles::Colors::TextGray)
                                   .arg(K4Styles::Dimensions::FontSizePopup));
    expiryLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_n1mmExpirySpin = new QSpinBox(page);
    m_n1mmExpirySpin->setRange(1, 600);
    m_n1mmExpirySpin->setValue(RadioSettings::instance()->spotExpiryMinutes());
    m_n1mmExpirySpin->setSuffix(" min");
    m_n1mmExpirySpin->setFixedWidth(K4Styles::Dimensions::InputFieldWidthSmall);
    m_n1mmExpirySpin->setStyleSheet(spinBoxStyle);

    expiryLayout->addWidget(expiryLabel);
    expiryLayout->addWidget(m_n1mmExpirySpin);
    expiryLayout->addStretch();
    layout->addLayout(expiryLayout);

    // Font size
    auto *fontSizeLayout = new QHBoxLayout();
    auto *fontSizeLabel = new QLabel("Font Size:", page);
    fontSizeLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                     .arg(K4Styles::Colors::TextGray)
                                     .arg(K4Styles::Dimensions::FontSizePopup));
    fontSizeLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_n1mmFontSizeSpin = new QSpinBox(page);
    m_n1mmFontSizeSpin->setRange(8, 24);
    m_n1mmFontSizeSpin->setValue(RadioSettings::instance()->spotFontSize());
    m_n1mmFontSizeSpin->setSuffix(" px");
    m_n1mmFontSizeSpin->setFixedWidth(K4Styles::Dimensions::InputFieldWidthSmall);
    m_n1mmFontSizeSpin->setStyleSheet(spinBoxStyle);

    fontSizeLayout->addWidget(fontSizeLabel);
    fontSizeLayout->addWidget(m_n1mmFontSizeSpin);
    fontSizeLayout->addStretch();
    layout->addLayout(fontSizeLayout);

    // Separator
    auto *line3 = new QFrame(page);
    line3->setFrameShape(QFrame::HLine);
    line3->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line3->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line3);

    // Spot Colors section
    auto *colorsLabel = new QLabel("Spot Colors", page);
    colorsLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                   .arg(K4Styles::Colors::TextWhite)
                                   .arg(K4Styles::Dimensions::FontSizePopup));
    layout->addWidget(colorsLabel);

    auto *colorsGrid = new QGridLayout();
    colorsGrid->setHorizontalSpacing(K4Styles::Dimensions::PaddingMedium);
    colorsGrid->setVerticalSpacing(K4Styles::Dimensions::PopupButtonSpacing);

    QString colorLabelStyle =
        QString("color: %1; font-size: %2px;").arg(K4Styles::Colors::TextGray).arg(K4Styles::Dimensions::FontSizePopup);

    // Multiplier color
    auto *multLabel = new QLabel("Multiplier:", page);
    multLabel->setStyleSheet(colorLabelStyle);
    m_n1mmMultColorBtn = new QPushButton(page);
    updateColorButton(m_n1mmMultColorBtn, QColor(RadioSettings::instance()->spotMultColor()));
    colorsGrid->addWidget(multLabel, 0, 0);
    colorsGrid->addWidget(m_n1mmMultColorBtn, 0, 1);

    // Unworked color
    auto *newQsoLabel = new QLabel("Unworked:", page);
    newQsoLabel->setStyleSheet(colorLabelStyle);
    m_n1mmNewQsoColorBtn = new QPushButton(page);
    updateColorButton(m_n1mmNewQsoColorBtn, QColor(RadioSettings::instance()->spotNewQsoColor()));
    colorsGrid->addWidget(newQsoLabel, 1, 0);
    colorsGrid->addWidget(m_n1mmNewQsoColorBtn, 1, 1);

    // Worked/Dupe color
    auto *dupeLabel = new QLabel("Worked:", page);
    dupeLabel->setStyleSheet(colorLabelStyle);
    m_n1mmDupeColorBtn = new QPushButton(page);
    updateColorButton(m_n1mmDupeColorBtn, QColor(RadioSettings::instance()->spotDupeColor()));
    colorsGrid->addWidget(dupeLabel, 2, 0);
    colorsGrid->addWidget(m_n1mmDupeColorBtn, 2, 1);

    colorsGrid->setColumnStretch(2, 1);
    layout->addLayout(colorsGrid);

    // Separator
    auto *line4 = new QFrame(page);
    line4->setFrameShape(QFrame::HLine);
    line4->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line4->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line4);

    // Enable checkbox
    m_n1mmEnableCheckbox = new QCheckBox("Enable N1MM UDP Spots", page);
    m_n1mmEnableCheckbox->setStyleSheet(QString("QCheckBox { color: %1; font-size: %2px; spacing: %3px; }"
                                                "QCheckBox::indicator { width: %4px; height: %4px; }")
                                            .arg(K4Styles::Colors::TextWhite)
                                            .arg(K4Styles::Dimensions::FontSizePopup)
                                            .arg(K4Styles::Dimensions::BorderRadiusLarge)
                                            .arg(K4Styles::Dimensions::CheckboxSize));
    m_n1mmEnableCheckbox->setChecked(RadioSettings::instance()->n1mmEnabled());
    layout->addWidget(m_n1mmEnableCheckbox);

    // Help text
    auto *helpLabel = new QLabel("In N1MM Logger+, enable UDP broadcast on the port above. "
                                 "Spots will appear on the panadapter. Click a spot to tune to it.",
                                 page);
    helpLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-style: italic;")
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::FontSizeLarge));
    helpLabel->setWordWrap(true);
    layout->addWidget(helpLabel);

    layout->addStretch();

    // Connect signals
    connect(m_n1mmEnableCheckbox, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setN1mmEnabled(checked); });

    connect(m_n1mmPortSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [](int value) { RadioSettings::instance()->setN1mmPort(static_cast<quint16>(value)); });

    connect(m_n1mmExpirySpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [](int value) { RadioSettings::instance()->setSpotExpiryMinutes(value); });

    connect(m_n1mmFontSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [](int value) { RadioSettings::instance()->setSpotFontSize(value); });

    connect(m_n1mmMultColorBtn, &QPushButton::clicked, this, [this]() {
        QColor color =
            QColorDialog::getColor(QColor(RadioSettings::instance()->spotMultColor()), this, "Multiplier Color");
        if (color.isValid()) {
            RadioSettings::instance()->setSpotMultColor(color.name());
            updateColorButton(m_n1mmMultColorBtn, color);
        }
    });

    connect(m_n1mmNewQsoColorBtn, &QPushButton::clicked, this, [this]() {
        QColor color =
            QColorDialog::getColor(QColor(RadioSettings::instance()->spotNewQsoColor()), this, "Unworked Color");
        if (color.isValid()) {
            RadioSettings::instance()->setSpotNewQsoColor(color.name());
            updateColorButton(m_n1mmNewQsoColorBtn, color);
        }
    });

    connect(m_n1mmDupeColorBtn, &QPushButton::clicked, this, [this]() {
        QColor color = QColorDialog::getColor(QColor(RadioSettings::instance()->spotDupeColor()), this, "Worked Color");
        if (color.isValid()) {
            RadioSettings::instance()->setSpotDupeColor(color.name());
            updateColorButton(m_n1mmDupeColorBtn, color);
        }
    });

    // Listen for N1MM status changes
    connect(RadioSettings::instance(), &RadioSettings::n1mmEnabledChanged, this, [this]() { updateN1mmStatus(); });

    // Initialize status
    updateN1mmStatus();

    return page;
}

void OptionsDialog::updateN1mmStatus() {
    if (!m_n1mmStatusLabel)
        return;

    bool isListening = m_n1mmListener && m_n1mmListener->isListening();

    if (isListening) {
        m_n1mmStatusLabel->setText(QString("Listening on port %1").arg(RadioSettings::instance()->n1mmPort()));
        m_n1mmStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                             .arg(K4Styles::Colors::StatusGreen)
                                             .arg(K4Styles::Dimensions::FontSizePopup));
    } else {
        m_n1mmStatusLabel->setText("Not running");
        m_n1mmStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                             .arg(K4Styles::Colors::ErrorRed)
                                             .arg(K4Styles::Dimensions::FontSizePopup));
    }
}

QWidget *OptionsDialog::createKpa1500Page() {
    auto *page = new QWidget(this);
    page->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::Background));

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    // Title
    auto *titleLabel = new QLabel("KPA1500 Amplifier", page);
    titleLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                  .arg(K4Styles::Colors::AccentAmber)
                                  .arg(K4Styles::Dimensions::FontSizeTitle));
    layout->addWidget(titleLabel);

    // Description
    auto *descLabel = new QLabel("Connect to an Elecraft KPA1500 amplifier for monitoring and control. "
                                 "The amplifier panel will appear as a floating window when connected.",
                                 page);
    descLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::FontSizeButton));
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    // Separator
    auto *line = new QFrame(page);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line);

    // Status indicator
    auto *statusLayout = new QHBoxLayout();
    auto *statusTitleLabel = new QLabel("Status:", page);
    statusTitleLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                        .arg(K4Styles::Colors::TextGray)
                                        .arg(K4Styles::Dimensions::FontSizePopup));
    statusTitleLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_kpa1500SettingsStatusLabel = new QLabel("Not connected", page);
    statusLayout->addWidget(statusTitleLabel);
    statusLayout->addWidget(m_kpa1500SettingsStatusLabel);
    statusLayout->addStretch();
    layout->addLayout(statusLayout);

    // Separator
    auto *line2 = new QFrame(page);
    line2->setFrameShape(QFrame::HLine);
    line2->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line2->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line2);

    // Settings section
    auto *sectionLabel = new QLabel("Connection", page);
    sectionLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                    .arg(K4Styles::Colors::TextWhite)
                                    .arg(K4Styles::Dimensions::FontSizePopup));
    layout->addWidget(sectionLabel);

    QString lineEditStyle = QString("QLineEdit { background-color: %1; color: %2; border: 1px solid %3; "
                                    "           padding: %6px; font-size: %5px; border-radius: %7px; }"
                                    "QLineEdit:focus { border-color: %4; }")
                                .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextWhite,
                                     K4Styles::Colors::DialogBorder, K4Styles::Colors::AccentAmber)
                                .arg(K4Styles::Dimensions::FontSizePopup)
                                .arg(K4Styles::Dimensions::PaddingSmall)
                                .arg(K4Styles::Dimensions::SliderBorderRadius);

    QString spinBoxStyle =
        QString("QSpinBox { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: %4px; "
                "font-size: %5px; }"
                "QSpinBox::up-button, QSpinBox::down-button { width: 16px; border: none; }")
            .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextWhite, K4Styles::Colors::DialogBorder)
            .arg(K4Styles::Dimensions::PaddingSmall)
            .arg(K4Styles::Dimensions::FontSizePopup);

    // Host input
    auto *hostLayout = new QHBoxLayout();
    auto *hostLabel = new QLabel("Host:", page);
    hostLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::FontSizePopup));
    hostLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_kpa1500HostEdit = new QLineEdit(page);
    m_kpa1500HostEdit->setPlaceholderText("192.168.1.100");
    m_kpa1500HostEdit->setFixedWidth(160);
    m_kpa1500HostEdit->setStyleSheet(lineEditStyle);
    m_kpa1500HostEdit->setText(RadioSettings::instance()->kpa1500Host());

    hostLayout->addWidget(hostLabel);
    hostLayout->addWidget(m_kpa1500HostEdit);
    hostLayout->addStretch();
    layout->addLayout(hostLayout);

    // Port input
    auto *portLayout = new QHBoxLayout();
    auto *portLabel = new QLabel("Port:", page);
    portLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::FontSizePopup));
    portLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_kpa1500PortSpin = new QSpinBox(page);
    m_kpa1500PortSpin->setRange(1, 65535);
    m_kpa1500PortSpin->setValue(RadioSettings::instance()->kpa1500Port());
    m_kpa1500PortSpin->setFixedWidth(K4Styles::Dimensions::InputFieldWidthSmall);
    m_kpa1500PortSpin->setStyleSheet(spinBoxStyle);

    auto *portHint = new QLabel("(default: 1500)", page);
    portHint->setStyleSheet(QString("color: %1; font-size: %2px;")
                                .arg(K4Styles::Colors::TextGray)
                                .arg(K4Styles::Dimensions::FontSizeLarge));

    portLayout->addWidget(portLabel);
    portLayout->addWidget(m_kpa1500PortSpin);
    portLayout->addWidget(portHint);
    portLayout->addStretch();
    layout->addLayout(portLayout);

    // Separator
    auto *line3 = new QFrame(page);
    line3->setFrameShape(QFrame::HLine);
    line3->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line3->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line3);

    // Enable checkbox
    m_kpa1500EnableCheckbox = new QCheckBox("Enable KPA1500 Amplifier", page);
    m_kpa1500EnableCheckbox->setStyleSheet(QString("QCheckBox { color: %1; font-size: %2px; spacing: %3px; }"
                                                   "QCheckBox::indicator { width: %4px; height: %4px; }")
                                               .arg(K4Styles::Colors::TextWhite)
                                               .arg(K4Styles::Dimensions::FontSizePopup)
                                               .arg(K4Styles::Dimensions::BorderRadiusLarge)
                                               .arg(K4Styles::Dimensions::CheckboxSize));
    m_kpa1500EnableCheckbox->setChecked(RadioSettings::instance()->kpa1500Enabled());
    layout->addWidget(m_kpa1500EnableCheckbox);

    // Help text
    auto *helpLabel = new QLabel("Enter the KPA1500 IP address and port. The amplifier connects automatically "
                                 "when the K4 connects and shows a floating panel with power, SWR, and controls.",
                                 page);
    helpLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-style: italic;")
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::FontSizeLarge));
    helpLabel->setWordWrap(true);
    layout->addWidget(helpLabel);

    layout->addStretch();

    // Connect signals
    connect(m_kpa1500HostEdit, &QLineEdit::editingFinished, this,
            [this]() { RadioSettings::instance()->setKpa1500Host(m_kpa1500HostEdit->text().trimmed()); });

    connect(m_kpa1500PortSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [](int value) { RadioSettings::instance()->setKpa1500Port(static_cast<quint16>(value)); });

    connect(m_kpa1500EnableCheckbox, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setKpa1500Enabled(checked); });

    // Listen for status changes
    connect(RadioSettings::instance(), &RadioSettings::kpa1500EnabledChanged, this,
            [this]() { updateKpa1500SettingsStatus(); });

    // Initialize status
    updateKpa1500SettingsStatus();

    return page;
}

void OptionsDialog::updateKpa1500SettingsStatus() {
    if (!m_kpa1500SettingsStatusLabel)
        return;

    bool enabled = RadioSettings::instance()->kpa1500Enabled();
    bool connected = m_kpa1500Client && m_kpa1500Client->isConnected();

    if (!enabled) {
        m_kpa1500SettingsStatusLabel->setText("Disabled");
        m_kpa1500SettingsStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                                        .arg(K4Styles::Colors::InactiveGray)
                                                        .arg(K4Styles::Dimensions::FontSizePopup));
    } else if (connected) {
        m_kpa1500SettingsStatusLabel->setText("Connected");
        m_kpa1500SettingsStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                                        .arg(K4Styles::Colors::StatusGreen)
                                                        .arg(K4Styles::Dimensions::FontSizePopup));
    } else {
        m_kpa1500SettingsStatusLabel->setText("Not connected");
        m_kpa1500SettingsStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                                        .arg(K4Styles::Colors::ErrorRed)
                                                        .arg(K4Styles::Dimensions::FontSizePopup));
    }
}

QWidget *OptionsDialog::createRfkitPage() {
    auto *page = new QWidget(this);
    page->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::Background));

    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin,
                               K4Styles::Dimensions::DialogMargin, K4Styles::Dimensions::DialogMargin);
    layout->setSpacing(K4Styles::Dimensions::PaddingLarge);

    // Title
    auto *titleLabel = new QLabel("RFKit Amplifier", page);
    titleLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                  .arg(K4Styles::Colors::AccentAmber)
                                  .arg(K4Styles::Dimensions::FontSizeTitle));
    layout->addWidget(titleLabel);

    // Description
    auto *descLabel = new QLabel("Connect to an RFKit amplifier for monitoring and control. "
                                 "The amplifier panel will appear as a floating window when connected.",
                                 page);
    descLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::FontSizeButton));
    descLabel->setWordWrap(true);
    layout->addWidget(descLabel);

    // Separator
    auto *line = new QFrame(page);
    line->setFrameShape(QFrame::HLine);
    line->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line);

    // Status indicator
    auto *statusLayout = new QHBoxLayout();
    auto *statusTitleLabel = new QLabel("Status:", page);
    statusTitleLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                        .arg(K4Styles::Colors::TextGray)
                                        .arg(K4Styles::Dimensions::FontSizePopup));
    statusTitleLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_rfkitStatusLabel = new QLabel("Not connected", page);
    statusLayout->addWidget(statusTitleLabel);
    statusLayout->addWidget(m_rfkitStatusLabel);
    statusLayout->addStretch();
    layout->addLayout(statusLayout);

    // Separator
    auto *line2 = new QFrame(page);
    line2->setFrameShape(QFrame::HLine);
    line2->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line2->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line2);

    // Settings section
    auto *sectionLabel = new QLabel("Connection", page);
    sectionLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                    .arg(K4Styles::Colors::TextWhite)
                                    .arg(K4Styles::Dimensions::FontSizePopup));
    layout->addWidget(sectionLabel);

    QString lineEditStyle = QString("QLineEdit { background-color: %1; color: %2; border: 1px solid %3; "
                                    "           padding: %6px; font-size: %5px; border-radius: %7px; }"
                                    "QLineEdit:focus { border-color: %4; }")
                                .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextWhite,
                                     K4Styles::Colors::DialogBorder, K4Styles::Colors::AccentAmber)
                                .arg(K4Styles::Dimensions::FontSizePopup)
                                .arg(K4Styles::Dimensions::PaddingSmall)
                                .arg(K4Styles::Dimensions::SliderBorderRadius);

    QString spinBoxStyle =
        QString("QSpinBox { background-color: %1; color: %2; border: 1px solid %3; border-radius: 4px; padding: %4px; "
                "font-size: %5px; }"
                "QSpinBox::up-button, QSpinBox::down-button { width: 16px; border: none; }")
            .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextWhite, K4Styles::Colors::DialogBorder)
            .arg(K4Styles::Dimensions::PaddingSmall)
            .arg(K4Styles::Dimensions::FontSizePopup);

    // Host input
    auto *hostLayout = new QHBoxLayout();
    auto *hostLabel = new QLabel("Host:", page);
    hostLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::FontSizePopup));
    hostLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_rfkitHostEdit = new QLineEdit(page);
    m_rfkitHostEdit->setPlaceholderText("192.168.1.100");
    m_rfkitHostEdit->setFixedWidth(160);
    m_rfkitHostEdit->setStyleSheet(lineEditStyle);
    m_rfkitHostEdit->setText(RadioSettings::instance()->rfkitHost());

    hostLayout->addWidget(hostLabel);
    hostLayout->addWidget(m_rfkitHostEdit);
    hostLayout->addStretch();
    layout->addLayout(hostLayout);

    // Port input
    auto *portLayout = new QHBoxLayout();
    auto *portLabel = new QLabel("Port:", page);
    portLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::FontSizePopup));
    portLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_rfkitPortSpin = new QSpinBox(page);
    m_rfkitPortSpin->setRange(1, 65535);
    m_rfkitPortSpin->setValue(RadioSettings::instance()->rfkitPort());
    m_rfkitPortSpin->setFixedWidth(K4Styles::Dimensions::InputFieldWidthSmall);
    m_rfkitPortSpin->setStyleSheet(spinBoxStyle);

    auto *portHint = new QLabel("(default: 8080)", page);
    portHint->setStyleSheet(QString("color: %1; font-size: %2px;")
                                .arg(K4Styles::Colors::TextGray)
                                .arg(K4Styles::Dimensions::FontSizeLarge));

    portLayout->addWidget(portLabel);
    portLayout->addWidget(m_rfkitPortSpin);
    portLayout->addWidget(portHint);
    portLayout->addStretch();
    layout->addLayout(portLayout);

    // Separator
    auto *line3 = new QFrame(page);
    line3->setFrameShape(QFrame::HLine);
    line3->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line3->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line3);

    // Enable checkbox
    m_rfkitEnableCheckbox = new QCheckBox("Enable RFKit Amplifier", page);
    m_rfkitEnableCheckbox->setStyleSheet(QString("QCheckBox { color: %1; font-size: %2px; spacing: %3px; }"
                                                 "QCheckBox::indicator { width: %4px; height: %4px; }")
                                             .arg(K4Styles::Colors::TextWhite)
                                             .arg(K4Styles::Dimensions::FontSizePopup)
                                             .arg(K4Styles::Dimensions::BorderRadiusLarge)
                                             .arg(K4Styles::Dimensions::CheckboxSize));
    m_rfkitEnableCheckbox->setChecked(RadioSettings::instance()->rfkitEnabled());
    layout->addWidget(m_rfkitEnableCheckbox);

    // Separator
    auto *line4 = new QFrame(page);
    line4->setFrameShape(QFrame::HLine);
    line4->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line4->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line4);

    // Drive Power Protection section
    auto *driveLabel = new QLabel("Drive Power Protection", page);
    driveLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                  .arg(K4Styles::Colors::TextWhite)
                                  .arg(K4Styles::Dimensions::FontSizePopup));
    layout->addWidget(driveLabel);

    m_rfkitLowPowerCheckbox = new QCheckBox("Low Power Mode", page);
    m_rfkitLowPowerCheckbox->setStyleSheet(QString("QCheckBox { color: %1; font-size: %2px; spacing: %3px; }"
                                                   "QCheckBox::indicator { width: %4px; height: %4px; }")
                                               .arg(K4Styles::Colors::TextWhite)
                                               .arg(K4Styles::Dimensions::FontSizePopup)
                                               .arg(K4Styles::Dimensions::BorderRadiusLarge)
                                               .arg(K4Styles::Dimensions::CheckboxSize));
    m_rfkitLowPowerCheckbox->setChecked(RadioSettings::instance()->rfkitLowPowerEnabled());
    layout->addWidget(m_rfkitLowPowerCheckbox);

    auto *maxDriveLayout = new QHBoxLayout();
    auto *maxDriveLabel = new QLabel("Max Drive:", page);
    maxDriveLabel->setStyleSheet(QString("color: %1; font-size: %2px;")
                                     .arg(K4Styles::Colors::TextGray)
                                     .arg(K4Styles::Dimensions::FontSizePopup));
    maxDriveLabel->setFixedWidth(K4Styles::Dimensions::FormLabelWidth);

    m_rfkitMaxDriveSpin = new QDoubleSpinBox(page);
    m_rfkitMaxDriveSpin->setRange(0.1, 110.0);
    m_rfkitMaxDriveSpin->setSingleStep(0.1);
    m_rfkitMaxDriveSpin->setDecimals(1);
    m_rfkitMaxDriveSpin->setSuffix(" W");
    m_rfkitMaxDriveSpin->setValue(RadioSettings::instance()->rfkitMaxDrivePower());
    m_rfkitMaxDriveSpin->setFixedWidth(K4Styles::Dimensions::InputFieldWidthSmall);
    m_rfkitMaxDriveSpin->setStyleSheet(
        QString("QDoubleSpinBox { background-color: %1; color: %2; border: 1px solid %3; "
                "border-radius: 4px; padding: %4px; font-size: %5px; }"
                "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { "
                "  width: 16px; border: none; background-color: %6; }"
                "QDoubleSpinBox::up-arrow { image: none; border-left: 4px solid transparent; "
                "  border-right: 4px solid transparent; border-bottom: 5px solid %2; }"
                "QDoubleSpinBox::down-arrow { image: none; border-left: 4px solid transparent; "
                "  border-right: 4px solid transparent; border-top: 5px solid %2; }")
            .arg(K4Styles::Colors::DarkBackground, K4Styles::Colors::TextWhite, K4Styles::Colors::DialogBorder)
            .arg(K4Styles::Dimensions::PaddingSmall)
            .arg(K4Styles::Dimensions::FontSizePopup)
            .arg(K4Styles::Colors::Background));

    auto *driveHint = new QLabel("If K4 power exceeds this, amp goes to standby", page);
    driveHint->setStyleSheet(QString("color: %1; font-size: %2px;")
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::FontSizeLarge));

    maxDriveLayout->addWidget(maxDriveLabel);
    maxDriveLayout->addWidget(m_rfkitMaxDriveSpin);
    maxDriveLayout->addWidget(driveHint);
    maxDriveLayout->addStretch();
    layout->addLayout(maxDriveLayout);

    // Separator
    auto *line5 = new QFrame(page);
    line5->setFrameShape(QFrame::HLine);
    line5->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line5->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line5);

    // Display section
    auto *displayLabel = new QLabel("Display", page);
    displayLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                    .arg(K4Styles::Colors::TextWhite)
                                    .arg(K4Styles::Dimensions::FontSizePopup));
    layout->addWidget(displayLabel);

    m_rfkitTempFahrenheitCheckbox = new QCheckBox("Show temperature in Fahrenheit", page);
    m_rfkitTempFahrenheitCheckbox->setStyleSheet(QString("QCheckBox { color: %1; font-size: %2px; spacing: %3px; }"
                                                         "QCheckBox::indicator { width: %4px; height: %4px; }")
                                                     .arg(K4Styles::Colors::TextWhite)
                                                     .arg(K4Styles::Dimensions::FontSizePopup)
                                                     .arg(K4Styles::Dimensions::BorderRadiusLarge)
                                                     .arg(K4Styles::Dimensions::CheckboxSize));
    m_rfkitTempFahrenheitCheckbox->setChecked(RadioSettings::instance()->rfkitTempFahrenheit());
    layout->addWidget(m_rfkitTempFahrenheitCheckbox);

    // Separator
    auto *line6 = new QFrame(page);
    line6->setFrameShape(QFrame::HLine);
    line6->setStyleSheet(QString("background-color: %1;").arg(K4Styles::Colors::DialogBorder));
    line6->setFixedHeight(K4Styles::Dimensions::SeparatorHeight);
    layout->addWidget(line6);

    // Help text
    auto *helpLabel = new QLabel("Enter the RFKit amplifier IP address and port. The amplifier panel shows power, "
                                 "SWR, temperature, and allows operate/standby control.",
                                 page);
    helpLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-style: italic;")
                                 .arg(K4Styles::Colors::TextGray)
                                 .arg(K4Styles::Dimensions::FontSizeLarge));
    helpLabel->setWordWrap(true);
    layout->addWidget(helpLabel);

    layout->addStretch();

    // Connect signals
    connect(m_rfkitHostEdit, &QLineEdit::editingFinished, this,
            [this]() { RadioSettings::instance()->setRfkitHost(m_rfkitHostEdit->text().trimmed()); });

    connect(m_rfkitPortSpin, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [](int value) { RadioSettings::instance()->setRfkitPort(static_cast<quint16>(value)); });

    connect(m_rfkitEnableCheckbox, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setRfkitEnabled(checked); });

    connect(m_rfkitLowPowerCheckbox, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setRfkitLowPowerEnabled(checked); });

    connect(m_rfkitMaxDriveSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [](double value) { RadioSettings::instance()->setRfkitMaxDrivePower(value); });

    connect(m_rfkitTempFahrenheitCheckbox, &QCheckBox::toggled, this,
            [](bool checked) { RadioSettings::instance()->setRfkitTempFahrenheit(checked); });

    // Listen for status changes
    connect(RadioSettings::instance(), &RadioSettings::rfkitEnabledChanged, this, [this]() { updateRfkitStatus(); });

    // Initialize status
    updateRfkitStatus();

    return page;
}

void OptionsDialog::updateRfkitStatus() {
    if (!m_rfkitStatusLabel)
        return;

    bool enabled = RadioSettings::instance()->rfkitEnabled();
    bool connected = m_rfkitClient && m_rfkitClient->isConnected();

    if (!enabled) {
        m_rfkitStatusLabel->setText("Disabled");
        m_rfkitStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                              .arg(K4Styles::Colors::InactiveGray)
                                              .arg(K4Styles::Dimensions::FontSizePopup));
    } else if (connected) {
        QString name = m_rfkitClient->deviceName();
        QString text = name.isEmpty() ? "Connected" : QString("Connected: %1").arg(name);
        m_rfkitStatusLabel->setText(text);
        m_rfkitStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                              .arg(K4Styles::Colors::StatusGreen)
                                              .arg(K4Styles::Dimensions::FontSizePopup));
    } else {
        m_rfkitStatusLabel->setText("Not connected");
        m_rfkitStatusLabel->setStyleSheet(QString("color: %1; font-size: %2px; font-weight: bold;")
                                              .arg(K4Styles::Colors::ErrorRed)
                                              .arg(K4Styles::Dimensions::FontSizePopup));
    }
}
