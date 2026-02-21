#ifndef RFKITPANEL_H
#define RFKITPANEL_H

#include <QPushButton>
#include <QTimer>
#include <QWidget>

/**
 * RFKitPanel - Compact control panel for RFKit amplifier.
 *
 * Layout (270×270px):
 * - Header: device name + status labels (OPER/STBY, antenna, fault)
 * - Hero meter: FWD power bar (0-maxW) with scale
 * - Mini meters: REF, SWR, TMP
 * - Info row: Voltage + Current readouts
 * - Button row: OPER/STBY, ANT cycle, CLR (error reset)
 */
class RFKitPanel : public QWidget {
    Q_OBJECT

public:
    explicit RFKitPanel(QWidget *parent = nullptr);

    // State setters (called from RFKitClient responses)
    void setMode(bool operate);
    void setAntenna(int number, const QString &name);
    void setForwardPower(float watts, float maxWatts);
    void setReflectedPower(float watts);
    void setSWR(float swr);
    void setTemperature(float celsius);
    void setVoltage(float volts);
    void setCurrent(float amps);
    void setFault(bool fault);
    void setConnected(bool connected);
    void setDeviceName(const QString &name);
    void setStatus(const QString &status);
    void setAntennaCount(int count);

signals:
    void modeToggled(bool operate);
    void antennaChanged(int number);
    void errorResetRequested();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void setupUi();
    void updateButtonStates();

    // Drawing helpers
    void drawMeter(QPainter &painter, int y, const QString &label, const QString &valueStr, float displayRatio,
                   float peakRatio, const QStringList &scaleLabels, bool large = true);
    void drawStatusLabels(QPainter &painter, int y, int height);
    void drawInfoRow(QPainter &painter, int y);
    void drawPeakMarker(QPainter &painter, int barX, int barY, int barWidth, int barHeight, float peakRatio);

    // Peak decay
    void startDecayTimer();
    void onDecayTimer();

    // State
    bool m_operate = false;
    int m_antennaNumber = 0;
    QString m_antennaName;
    int m_antennaCount = 1;
    float m_forwardPower = 0.0f;
    float m_maxForwardPower = 1500.0f;
    float m_reflectedPower = 0.0f;
    float m_swr = 1.0f;
    float m_temperature = 0.0f;
    float m_voltage = 0.0f;
    float m_current = 0.0f;
    bool m_fault = false;
    bool m_connected = false;
    QString m_deviceName = "RFKit";
    QString m_status;

    // Display values (smoothed for animation)
    float m_displayForwardPower = 0.0f;
    float m_displayReflectedPower = 0.0f;
    float m_displaySwr = 1.0f;
    float m_displayTemperature = 0.0f;

    // Peak hold values
    float m_peakForwardPower = 0.0f;
    float m_peakReflectedPower = 0.0f;
    float m_peakSwr = 1.0f;

    // Animation timer
    QTimer *m_decayTimer = nullptr;
    static constexpr int DECAY_INTERVAL_MS = 33;
    static constexpr float ATTACK_RATE = 0.35f;
    static constexpr float DECAY_RATE = 0.06f;
    static constexpr float PEAK_DECAY_RATE = 0.04f;

    // Buttons
    QPushButton *m_modeBtn = nullptr;
    QPushButton *m_antBtn = nullptr;
    QPushButton *m_clrBtn = nullptr;
};

#endif // RFKITPANEL_H
