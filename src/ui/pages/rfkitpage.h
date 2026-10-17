#ifndef RFKITPAGE_H
#define RFKITPAGE_H

#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QWidget>

class RFKitClient;

/**
 * @brief OptionsDialog "RFKit" tab. Enables/disables the RF-Kit amplifier integration,
 *        captures the IP/port of the amplifier's HTTP REST endpoint, configures the
 *        drive-power lockout and temperature unit, and shows live device/status data
 *        from RFKitClient.
 */
class RfkitPage : public QWidget {
    Q_OBJECT

public:
    explicit RfkitPage(RFKitClient *rfkitClient, QWidget *parent = nullptr);

    void refresh();

private:
    void updateRfkitStatus();

    RFKitClient *m_rfkitClient;
    QCheckBox *m_enableCheckbox = nullptr;
    QLineEdit *m_hostEdit = nullptr;
    QLineEdit *m_portEdit = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_connectBtn = nullptr;
    QLabel *m_pollLabel = nullptr;
    QCheckBox *m_lowPowerCheckbox = nullptr;
    QLineEdit *m_maxDriveEdit = nullptr;
    QCheckBox *m_fahrenheitCheckbox = nullptr;
    QLabel *m_deviceLabel = nullptr;
    QLabel *m_bandLabel = nullptr;
    QLabel *m_antennaLabel = nullptr;
};

#endif // RFKITPAGE_H
