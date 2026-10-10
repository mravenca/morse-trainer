#pragma once
// Sidetone and playback. A pull-mode QIODevice feeding QAudioSink: sine with a
// raised-cosine envelope (no key clicks). Live keying and scheduled playback are
// both rendered sample-accurately inside the audio callback.
#include "morse.h"

#include <QAudioDevice>
#include <QAudioFormat>
#include <QIODevice>
#include <QTimer>

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>

class QAudioSink;

class ToneGenerator : public QIODevice
{
    Q_OBJECT
public:
    explicit ToneGenerator(QObject *parent = nullptr);
    ~ToneGenerator() override;

    bool startAudio(const QAudioDevice &device, int bufferMs, QString *error);
    void stopAudio();

    void setFrequency(double hz) { m_freq = hz; }
    void setVolume(double v) { m_volume = v; }
    void setSidetoneEnabled(bool on) { m_sidetone = on; }
    void setRampMs(double ms) { m_rampMs = ms; }

    // Live keying (sidetone).
    void setKeyed(bool down) { m_keyed = down; }

    // Scheduled playback of a keying sequence; replaces anything pending.
    void play(const std::vector<morse::Segment> &seq);
    void stopPlayback();
    bool isPlaying() const { return m_playing; }

    QString status() const;

    qint64 readData(char *data, qint64 maxlen) override;
    qint64 writeData(const char *, qint64) override { return -1; }
    qint64 bytesAvailable() const override { return 1 << 20; }
    bool isSequential() const override { return true; }

signals:
    void playbackFinished();

private:
    // Renders `frames` interleaved frames of sidetone/playback into out.
    template <typename T>
    void render(T *out, qint64 frames, int channels);

    QAudioFormat m_format;
    std::unique_ptr<QAudioSink> m_sink;
    QTimer m_finishPoll;

    std::atomic<double> m_freq{650.0};
    std::atomic<double> m_volume{0.5};
    std::atomic<double> m_rampMs{5.0};
    std::atomic<bool> m_sidetone{true};
    std::atomic<bool> m_keyed{false};
    std::atomic<bool> m_playing{false};

    // Audio-thread state.
    double m_phase = 0;
    double m_env = 0;          // 0..1 linear ramp position
    struct Pending { bool on; int64_t samples; };
    std::mutex m_mutex;
    std::deque<Pending> m_queue;
    bool m_playOn = false;
    int64_t m_playLeft = 0;
};
