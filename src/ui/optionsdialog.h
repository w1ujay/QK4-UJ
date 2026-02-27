#ifndef OPTIONSDIALOG_H
#define OPTIONSDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QCheckBox>
#include <QLineEdit>
#include <QComboBox>
#include <QSlider>
#include <QPushButton>
#include <QShowEvent>
#include <QHideEvent>
#include <QSpinBox>
#include <QDoubleSpinBox>

class RadioState;
class AudioEngine;
class MicMeterWidget;
class KpodDevice;
class CatServer;
class HalikeyDevice;
class N1mmListener;
class RFKitClient;
class KPA1500Client;

class OptionsDialog : public QDialog {
    Q_OBJECT

public:
    enum Page {
        PageAbout = 0,
        PageAudioInput,
        PageAudioOutput,
        PageRigControl,
        PageCwKeyer,
        PageKpod,
        PageN1mm,
        PageKpa1500,
        PageRfkit,
        PageCount
    };

    explicit OptionsDialog(RadioState *radioState, AudioEngine *audioEngine, KpodDevice *kpodDevice,
                           CatServer *catServer, HalikeyDevice *halikeyDevice, N1mmListener *n1mmListener,
                           RFKitClient *rfkitClient, KPA1500Client *kpa1500Client, QWidget *parent = nullptr);
    ~OptionsDialog();

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private slots:
    void onMicTestToggled(bool checked);
    void onMicLevelChanged(float level);
    void onMicDeviceChanged(int index);
    void onMicGainChanged(int value);
    void updateKpodStatus();
    void updateCwKeyerStatus();
    void onCwKeyerConnectClicked();
    void onCwKeyerRefreshClicked();

private:
    void setupUi();
    void refreshCurrentPage();
    void refreshPage(int index);
    QWidget *createAboutPage();
    QWidget *createKpodPage();
    QWidget *createAudioInputPage();
    QWidget *createAudioOutputPage();
    QWidget *createRigControlPage();
    QWidget *createCwKeyerPage();
    QWidget *createN1mmPage();
    QWidget *createRfkitPage();
    QWidget *createKpa1500Page();
    void updateCatServerStatus();
    void updateN1mmStatus();
    void updateRfkitStatus();
    void updateKpa1500SettingsStatus();
    void populateMicDevices();
    void populateSpeakerDevices();
    void populateCwKeyerPorts();

    RadioState *m_radioState;
    AudioEngine *m_audioEngine;
    KpodDevice *m_kpodDevice;
    CatServer *m_catServer;
    HalikeyDevice *m_halikeyDevice;
    N1mmListener *m_n1mmListener;
    RFKitClient *m_rfkitClient;
    KPA1500Client *m_kpa1500Client;
    QListWidget *m_tabList;
    QStackedWidget *m_pageStack;
    bool m_pageCreated[PageCount] = {};

    // KPOD page elements (for real-time updates)
    QCheckBox *m_kpodEnableCheckbox = nullptr;
    QLabel *m_kpodStatusLabel = nullptr;
    QLabel *m_kpodProductLabel = nullptr;
    QLabel *m_kpodManufacturerLabel = nullptr;
    QLabel *m_kpodVendorIdLabel = nullptr;
    QLabel *m_kpodProductIdLabel = nullptr;
    QLabel *m_kpodDeviceTypeLabel = nullptr;
    QLabel *m_kpodFirmwareLabel = nullptr;
    QLabel *m_kpodDeviceIdLabel = nullptr;
    QLabel *m_kpodHelpLabel = nullptr;

    // Audio Input settings
    QComboBox *m_micDeviceCombo = nullptr;
    QSlider *m_micGainSlider = nullptr;
    QLabel *m_micGainValueLabel = nullptr;
    QPushButton *m_micTestBtn = nullptr;
    MicMeterWidget *m_micMeter = nullptr;
    bool m_micTestActive = false;

    // Audio Output settings
    QComboBox *m_speakerDeviceCombo = nullptr;
    QSpinBox *m_outputBufferSpin = nullptr;
    QSpinBox *m_latencyTargetSpin = nullptr;
    QCheckBox *m_tcpNoDelayCheckbox = nullptr;

    // CAT Server page elements
    QCheckBox *m_catServerEnableCheckbox = nullptr;
    QLineEdit *m_catServerPortEdit = nullptr;
    QLabel *m_catServerStatusLabel = nullptr;
    QLabel *m_catServerClientsLabel = nullptr;

    void onSpeakerDeviceChanged(int index);

    // CW Keyer page elements
    QComboBox *m_cwKeyerDeviceTypeCombo = nullptr;
    QLabel *m_cwKeyerDescLabel = nullptr;
    QComboBox *m_cwKeyerPortCombo = nullptr;
    QPushButton *m_cwKeyerRefreshBtn = nullptr;
    QPushButton *m_cwKeyerConnectBtn = nullptr;
    QLabel *m_cwKeyerStatusLabel = nullptr;
    QSlider *m_sidetoneVolumeSlider = nullptr;
    QLabel *m_sidetoneVolumeValueLabel = nullptr;
    void updateCwKeyerDescription();

    // N1MM Spots page elements
    QCheckBox *m_n1mmEnableCheckbox = nullptr;
    QSpinBox *m_n1mmPortSpin = nullptr;
    QSpinBox *m_n1mmExpirySpin = nullptr;
    QSpinBox *m_n1mmFontSizeSpin = nullptr;
    QPushButton *m_n1mmMultColorBtn = nullptr;
    QPushButton *m_n1mmNewQsoColorBtn = nullptr;
    QPushButton *m_n1mmDupeColorBtn = nullptr;
    QLabel *m_n1mmStatusLabel = nullptr;
    void updateColorButton(QPushButton *btn, const QColor &color);

    // RFKit Amp page elements
    QCheckBox *m_rfkitEnableCheckbox = nullptr;
    QLineEdit *m_rfkitHostEdit = nullptr;
    QSpinBox *m_rfkitPortSpin = nullptr;
    QLabel *m_rfkitStatusLabel = nullptr;
    QCheckBox *m_rfkitLowPowerCheckbox = nullptr;
    QDoubleSpinBox *m_rfkitMaxDriveSpin = nullptr;
    QCheckBox *m_rfkitTempFahrenheitCheckbox = nullptr;

    // KPA1500 page elements
    QCheckBox *m_kpa1500EnableCheckbox = nullptr;
    QLineEdit *m_kpa1500HostEdit = nullptr;
    QSpinBox *m_kpa1500PortSpin = nullptr;
    QLabel *m_kpa1500SettingsStatusLabel = nullptr;
};

#endif // OPTIONSDIALOG_H
