#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

#include <QObject>
#include <QAudioSink>
#include <QAudioSource>
#include <QAudioFormat>
#include <QIODevice>
#include <QTimer>
#include <QQueue>
#include <QMutex>
#include <atomic>

class AudioEngine : public QObject {
    Q_OBJECT

public:
    enum MixSource { MixA = 0, MixB = 1, MixAB = 2, MixNegA = 3 };

    // RX volume gain ceiling (slider 100% / AG 060 = unity, no amplification)
    static constexpr float VOLUME_GAIN_MAX = 1.0f;

    explicit AudioEngine(QObject *parent = nullptr);
    ~AudioEngine();

    Q_INVOKABLE bool start();
    Q_INVOKABLE void stop();
    void enqueueAudio(const QByteArray &pcmData);
    Q_INVOKABLE void flushQueue();

    Q_INVOKABLE void setMicEnabled(bool enabled);
    bool isMicEnabled() const { return m_micEnabled.load(std::memory_order_relaxed); }

    void setVolume(float volume); // 0.0 to 1.0
    float volume() const { return m_volume.load(std::memory_order_relaxed); }

    // Channel volume controls (applied at playback time for instant response)
    void setMainVolume(float volume); // 0.0 to VOLUME_GAIN_MAX
    void setSubVolume(float volume);  // 0.0 to VOLUME_GAIN_MAX
    float mainVolume() const { return m_mainVolume.load(std::memory_order_relaxed); }
    float subVolume() const { return m_subVolume.load(std::memory_order_relaxed); }

    // SUB RX mute control (when sub receiver is off, sub channel is silent)
    void setSubMuted(bool muted);

    // Audio mix routing (MX command - how main/sub maps to L/R when SUB is on)
    void setAudioMix(int left, int right); // MixSource values

    // Balance mode control (0=NOR, 1=BAL)
    void setBalanceMode(int mode);
    void setBalanceOffset(int offset); // -50 to +50

    // RX noise filter (3.5kHz low-pass to remove out-of-band noise)
    void setNoiseFilterEnabled(bool enabled);
    bool noiseFilterEnabled() const { return m_noiseFilterEnabled.load(std::memory_order_relaxed); }

    // Microphone settings
    void setMicGain(float gain); // 0.0 to 1.0
    float micGain() const { return m_micGain.load(std::memory_order_relaxed); }

    Q_INVOKABLE void setMicDevice(const QString &deviceId);
    QString micDeviceId() const;

    // Get list of available input devices (for settings UI)
    static QList<QPair<QString, QString>> availableInputDevices(); // (id, description)

    // Output device settings
    Q_INVOKABLE void setOutputDevice(const QString &deviceId);
    QString outputDeviceId() const;

    // Latency tuning (call BEFORE start(), or use setters that re-create the sink)
    void setOutputBufferMs(int ms);
    void setLatencyTargetMs(int ms);

    // Get list of available output devices (for settings UI)
    static QList<QPair<QString, QString>> availableOutputDevices(); // (id, description)

signals:
    void microphoneData(const QByteArray &pcmData);    // Raw Float32 mic data (variable size)
    void microphoneFrame(const QByteArray &s16leData); // Complete frame (240 samples, S16LE @ 12kHz)
    void micLevelChanged(float level);                 // RMS level 0.0-1.0 for meter display

private slots:
    void onMicDataReady();
    void feedAudioDevice();

private:
    bool setupAudioOutput();
    bool setupAudioInput();

    // Resample 48kHz Float32 samples to 12kHz (4:1 decimation with averaging)
    QByteArray resample48kTo12k(const QByteArray &input48k);

    // Upsample 12kHz stereo Float32 to 48kHz (4x linear interpolation)
    QByteArray upsample12kTo48k(const QByteArray &input12k);

    // Apply MX routing + volume + balance to a raw [main, sub] interleaved packet
    void applyMixAndVolume(QByteArray &packet);

    // 2nd-order Butterworth low-pass filter (biquad) state per channel
    struct BiquadState {
        float z1 = 0.0f; // delay line
        float z2 = 0.0f;
    };
    BiquadState m_lpfLeft;
    BiquadState m_lpfRight;

    // Biquad coefficients for 2nd-order Butterworth LPF: fc=3.5kHz, fs=12kHz
    // -3dB at 3.5kHz, -18dB at 5kHz — removes out-of-band receiver/codec noise
    static constexpr float LPF_B0 = 0.373978f;
    static constexpr float LPF_B1 = 0.747956f;
    static constexpr float LPF_B2 = 0.373978f;
    static constexpr float LPF_A1 = 0.307566f;
    static constexpr float LPF_A2 = 0.188345f;

    static inline float biquadProcess(BiquadState &s, float in) {
        float out = LPF_B0 * in + s.z1;
        s.z1 = LPF_B1 * in - LPF_A1 * out + s.z2;
        s.z2 = LPF_B2 * in - LPF_A2 * out;
        return out;
    }

    // Audio output format: 12kHz stereo Float32 (K4 RX audio, L=Main R=Sub)
    QAudioFormat m_outputFormat;

    // Audio input format: 48kHz mono Float32 (native macOS rate, resampled to 12kHz)
    QAudioFormat m_inputFormat;

    // Audio output (speaker)
    QAudioSink *m_audioSink;
    QIODevice *m_audioSinkDevice;

    // Audio input (microphone)
    QAudioSource *m_audioSource;
    QIODevice *m_audioSourceDevice;
    std::atomic<bool> m_micEnabled{false};
    QString m_selectedMicDeviceId;    // Empty = use system default
    QString m_selectedOutputDeviceId; // Empty = use system default

    // Volume control (QAudioSink system volume)
    std::atomic<float> m_volume{1.0f};

    // Channel volume controls (0.0 to VOLUME_GAIN_MAX)
    std::atomic<float> m_mainVolume{1.0f};
    std::atomic<float> m_subVolume{1.0f};

    // SUB RX mute state (true = sub muted, sub channel is silent)
    std::atomic<bool> m_subMuted{true}; // Starts muted (SUB RX is off at startup)

    // RX noise filter (low-pass biquad to remove out-of-band noise)
    std::atomic<bool> m_noiseFilterEnabled{true};

    // Audio mix routing (MX command) - default A.B (main left, sub right)
    MixSource m_mixLeft = MixA;
    MixSource m_mixRight = MixB;
    QMutex m_mixMutex; // Protects m_mixLeft and m_mixRight (always set together)

    // Balance mode (0=NOR: independent volume, 1=BAL: L/R balance)
    std::atomic<int> m_balanceMode{0};
    std::atomic<int> m_balanceOffset{0}; // -50 to +50

    // Microphone gain control
    std::atomic<float> m_micGain{0.25f}; // Default 25% (macOS mic input is typically hot)

    // Audio throughput at 12kHz input: 12kHz × 2ch × sizeof(float) = 96,000 bytes/sec = 96 bytes/ms
    static constexpr int BYTES_PER_MS = 96;

    // Audio buffer sizes — defaults (overridable via setOutputBufferMs / setLatencyTargetMs)
    static constexpr int DEFAULT_OUTPUT_BUFFER_MS = 500;
    static constexpr int DEFAULT_OUTPUT_BUFFER_SIZE = DEFAULT_OUTPUT_BUFFER_MS * BYTES_PER_MS; // 48,000 bytes
    // Input: 48kHz * 4 bytes/sample * 0.1 sec = 19200 bytes
    static constexpr int INPUT_BUFFER_SIZE = 19200;

    // Runtime-configurable buffer sizes
    int m_outputBufferMs = DEFAULT_OUTPUT_BUFFER_MS;

    // Microphone gain scaling factor (gain slider 0-1 maps to 0-2x, so 0.5 = unity)
    static constexpr float MIC_GAIN_SCALE = 2.0f;

    // Microphone frame buffering for Opus encoding
    // Buffer accumulates S16LE samples at 12kHz until we have a complete frame
    static constexpr int FRAME_SAMPLES = 240;                                // 20ms at 12kHz
    static constexpr int FRAME_BYTES_S16LE = FRAME_SAMPLES * sizeof(qint16); // 480 bytes
    QByteArray m_micBuffer;

    // Timer for polling microphone data (more reliable than readyRead signal)
    QTimer *m_micPollTimer;

    // Jitter buffer for RX audio playback
    QQueue<QByteArray> m_audioQueue;
    int m_queueBytes = 0; // Total decoded bytes in m_audioQueue (tracked for time-based thresholds)
    QMutex m_queueMutex;  // Protects m_audioQueue, m_queueBytes, m_prebuffering
    QTimer *m_feedTimer;
    bool m_prebuffering = true;

    // Write staging buffer: holds processed PCM that couldn't be written in one feed cycle
    // Audio-thread-only (no mutex needed) — safety net for partial QIODevice::write()
    QByteArray m_writeBuffer;

    // Jitter buffer constants (adapt to any SL level automatically)
    // Prebuffer: start playback as soon as the first packet arrives.
    // The SL level already provides jitter tolerance (larger packets = more runway),
    // so additional prebuffering just adds latency without benefit.
    static constexpr int PREBUFFER_PACKETS = 1;
    static constexpr int MAX_QUEUE_BYTES = 1000 * BYTES_PER_MS; // 96,000 bytes (1s overflow cap)
    static constexpr int DEFAULT_LATENCY_TARGET_MS = 200;
    std::atomic<int> m_latencyTargetBytes{DEFAULT_LATENCY_TARGET_MS * BYTES_PER_MS}; // ~200ms default
    static constexpr int FEED_INTERVAL_MS = 10;
};

#endif // AUDIOENGINE_H
