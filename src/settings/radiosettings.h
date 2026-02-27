#ifndef RADIOSETTINGS_H
#define RADIOSETTINGS_H

#include <QMap>
#include <QObject>
#include <QPoint>
#include <QSettings>
#include <QString>
#include <QVector>

// Macro entry for programmable function keys
struct MacroEntry {
    QString functionId; // "PF1", "Fn.F1", "K-pod.1T", etc.
    QString label;      // Custom label or empty
    QString command;    // CAT command or empty

    bool isEmpty() const { return command.isEmpty(); }
    QString displayLabel() const {
        if (command.isEmpty())
            return "Unused";
        return label.isEmpty() ? "Mapped" : label;
    }
};

// RX EQ preset entry (8-band graphic equalizer)
struct EqPreset {
    QString name;       // User-defined name ("SSB", "CW", etc.)
    QVector<int> bands; // 8 values, -16 to +16 dB

    bool isEmpty() const { return bands.isEmpty() || name.isEmpty(); }
    QString displayName() const { return isEmpty() ? "---" : name; }
};

struct RadioEntry {
    QString name;
    QString host;
    QString password; // Password (used as PSK when TLS enabled)
    quint16 port;
    bool useTls = false;           // Use TLS/PSK encryption (port 9204)
    QString identity;              // TLS-PSK identity (optional, empty = default)
    int encodeMode = 3;            // Audio encode mode: 0=RAW32, 1=RAW16, 2=Opus Int, 3=Opus Float (default)
    int streamingLatency = 3;      // Remote streaming audio latency: 0-7 (default 3)
    int displayFps = 30;           // Display FPS: 12-30 (default 30)
    bool n1mmEnabled = false;      // Whether to listen for N1MM spot broadcasts
    quint16 n1mmPort = 12060;      // N1MM UDP listen port
    int spotExpiryMinutes = 10;    // Auto-expire spots after N minutes
    bool panadapterEnabled = true; // Persist panadapter on/off state

    bool operator==(const RadioEntry &other) const {
        return name == other.name && host == other.host && port == other.port;
    }
};

class RadioSettings : public QObject {
    Q_OBJECT

public:
    static RadioSettings *instance();

    QVector<RadioEntry> radios() const;
    void addRadio(const RadioEntry &radio);
    void removeRadio(int index);
    void updateRadio(int index, const RadioEntry &radio);

    int lastSelectedIndex() const;
    void setLastSelectedIndex(int index);

    bool kpodEnabled() const;
    void setKpodEnabled(bool enabled);

    // KPA1500 Amplifier settings
    QString kpa1500Host() const;
    void setKpa1500Host(const QString &host);
    quint16 kpa1500Port() const;
    void setKpa1500Port(quint16 port);
    bool kpa1500Enabled() const;
    void setKpa1500Enabled(bool enabled);
    int kpa1500PollInterval() const;
    void setKpa1500PollInterval(int intervalMs);
    QPoint kpa1500WindowPosition() const;
    void setKpa1500WindowPosition(const QPoint &pos);

    // Audio enable/disable (when disabled, AG commands pass through to K4)
    bool audioEnabled() const;
    void setAudioEnabled(bool enabled);

    // RX noise filter (3.5kHz low-pass)
    bool noiseFilterEnabled() const;
    void setNoiseFilterEnabled(bool enabled);

    // Audio output settings
    int volume() const;
    void setVolume(int value); // 0-100, default 45
    int subVolume() const;
    void setSubVolume(int value); // 0-100, default 45 (Sub RX / VFO B)

    // Audio input (microphone) settings
    int micGain() const;
    void setMicGain(int value); // 0-100, default 25
    QString micDevice() const;
    void setMicDevice(const QString &deviceId);

    // Audio output (speaker) settings
    QString speakerDevice() const;
    void setSpeakerDevice(const QString &deviceId);

    // CAT Server settings (local TCP server for external apps)
    bool catServerEnabled() const;
    void setCatServerEnabled(bool enabled);
    quint16 catServerPort() const;
    void setCatServerPort(quint16 port);

    // Macro settings
    QMap<QString, MacroEntry> macros() const;
    MacroEntry macro(const QString &functionId) const;
    void setMacro(const QString &functionId, const QString &label, const QString &command);
    void clearMacro(const QString &functionId);

    // HaliKey CW Keyer settings
    QString halikeyPortName() const;
    void setHalikeyPortName(const QString &portName);
    bool halikeyEnabled() const;
    void setHalikeyEnabled(bool enabled);
    int halikeyDeviceType() const;
    void setHalikeyDeviceType(int type); // 0=V14, 1=MiDi
    int sidetoneVolume() const;
    void setSidetoneVolume(int value); // 0-100, default 30

    // Audio latency settings
    int audioOutputBuffer() const;
    void setAudioOutputBuffer(int ms); // 50-1000ms, default 500
    int audioLatencyTarget() const;
    void setAudioLatencyTarget(int ms); // 20-500ms, default 200
    bool tcpNoDelay() const;
    void setTcpNoDelay(bool enabled); // default true

    // RX EQ Presets (4 slots)
    EqPreset rxEqPreset(int index) const;                  // Get preset 0-3
    void setRxEqPreset(int index, const EqPreset &preset); // Set preset 0-3
    void clearRxEqPreset(int index);                       // Clear preset 0-3

    // TX EQ Presets (4 slots)
    EqPreset txEqPreset(int index) const;                  // Get preset 0-3
    void setTxEqPreset(int index, const EqPreset &preset); // Set preset 0-3
    void clearTxEqPreset(int index);                       // Clear preset 0-3

    // RFKit Amplifier settings
    QString rfkitHost() const;
    void setRfkitHost(const QString &host);
    quint16 rfkitPort() const;
    void setRfkitPort(quint16 port);
    bool rfkitEnabled() const;
    void setRfkitEnabled(bool enabled);
    int rfkitPollInterval() const;
    void setRfkitPollInterval(int intervalMs);
    QPoint rfkitWindowPosition() const;
    void setRfkitWindowPosition(const QPoint &pos);
    bool rfkitLowPowerEnabled() const;
    void setRfkitLowPowerEnabled(bool enabled);
    double rfkitMaxDrivePower() const;
    void setRfkitMaxDrivePower(double watts);
    bool rfkitTempFahrenheit() const;
    void setRfkitTempFahrenheit(bool fahrenheit);

    // N1MM Spot settings (global)
    bool n1mmEnabled() const;
    void setN1mmEnabled(bool enabled);
    quint16 n1mmPort() const;
    void setN1mmPort(quint16 port);
    int spotExpiryMinutes() const;
    void setSpotExpiryMinutes(int minutes);
    QString spotMultColor() const;
    void setSpotMultColor(const QString &color);
    QString spotNewQsoColor() const;
    void setSpotNewQsoColor(const QString &color);
    QString spotDupeColor() const;
    void setSpotDupeColor(const QString &color);
    int spotFontSize() const;
    void setSpotFontSize(int size);

signals:
    void radiosChanged();
    void kpodEnabledChanged(bool enabled);
    void kpa1500EnabledChanged(bool enabled);
    void kpa1500SettingsChanged();
    void kpa1500PollIntervalChanged(int intervalMs);
    void audioEnabledChanged(bool enabled);
    void noiseFilterEnabledChanged(bool enabled);
    void micGainChanged(int value);
    void micDeviceChanged(const QString &deviceId);
    void speakerDeviceChanged(const QString &deviceId);
    void catServerEnabledChanged(bool enabled);
    void catServerPortChanged(quint16 port);
    void macrosChanged();
    void halikeyEnabledChanged(bool enabled);
    void halikeyPortNameChanged(const QString &portName);
    void halikeyDeviceTypeChanged(int type);
    void sidetoneVolumeChanged(int value);
    void rxEqPresetsChanged();
    void txEqPresetsChanged();
    void n1mmEnabledChanged(bool enabled);
    void n1mmPortChanged(quint16 port);
    void spotExpiryMinutesChanged(int minutes);
    void spotColorsChanged();
    void spotFontSizeChanged(int size);
    void rfkitEnabledChanged(bool enabled);
    void rfkitSettingsChanged();
    void rfkitPollIntervalChanged(int intervalMs);
    void rfkitLowPowerChanged();
    void rfkitTempUnitChanged(bool fahrenheit);
    void audioOutputBufferChanged(int ms);
    void audioLatencyTargetChanged(int ms);
    void tcpNoDelayChanged(bool enabled);

private:
    explicit RadioSettings(QObject *parent = nullptr);
    void load();
    void save();

    QVector<RadioEntry> m_radios;
    int m_lastSelectedIndex;
    bool m_kpodEnabled;

    // KPA1500 settings
    QString m_kpa1500Host;
    quint16 m_kpa1500Port = 1500;
    bool m_kpa1500Enabled = false;
    int m_kpa1500PollInterval = 300; // Default: 300ms for responsive meters

    // CAT Server settings
    bool m_catServerEnabled = false;
    quint16 m_catServerPort = 9299;

    // HaliKey settings
    QString m_halikeyPortName;
    bool m_halikeyEnabled = false;
    int m_halikeyDeviceType = 0; // 0=V14, 1=MiDi
    int m_sidetoneVolume = 30;   // Default 30%

    // RFKit Amplifier settings
    QString m_rfkitHost;
    quint16 m_rfkitPort = 8080;
    bool m_rfkitEnabled = false;
    int m_rfkitPollInterval = 1000; // Default: 1000ms for HTTP polling
    bool m_rfkitLowPowerEnabled = false;
    double m_rfkitMaxDrivePower = 1.5; // Default: 1.5W max K4 drive power
    bool m_rfkitTempFahrenheit = false;

    // N1MM Spot settings (global)
    bool m_n1mmEnabled = false;
    quint16 m_n1mmPort = 12060;
    int m_spotExpiryMinutes = 10;
    QString m_spotMultColor = "#FF0000";
    QString m_spotNewQsoColor = "#4DA6FF";
    QString m_spotDupeColor = "#555555";
    int m_spotFontSize = 10;

    // Macro settings
    QMap<QString, MacroEntry> m_macros;

    // RX EQ Presets (4 slots)
    EqPreset m_rxEqPresets[4];

    // TX EQ Presets (4 slots)
    EqPreset m_txEqPresets[4];

    QSettings m_settings;
};

#endif // RADIOSETTINGS_H
