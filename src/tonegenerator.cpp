#include "tonegenerator.h"

#include <QAudioSink>
#include <QMediaDevices>
#include <QtGlobal>

#include <cmath>
#include <type_traits>

ToneGenerator::ToneGenerator(QObject *parent)
    : QIODevice(parent)
{
    m_finishPoll.setInterval(20);
    connect(&m_finishPoll, &QTimer::timeout, this, [this] {
        if (!m_playing) {
            m_finishPoll.stop();
            emit playbackFinished();
        }
    });
}

ToneGenerator::~ToneGenerator()
{
    stopAudio();
}

bool ToneGenerator::startAudio(const QAudioDevice &requested, int bufferMs, QString *error)
{
    stopAudio();
    QAudioDevice device = requested.isNull() ? QMediaDevices::defaultAudioOutput() : requested;
    if (device.isNull()) {
        if (error)
            *error = QStringLiteral("No audio output device");
        return false;
    }
    QAudioFormat fmt;
    fmt.setSampleRate(48000);
    fmt.setChannelCount(1);
    fmt.setSampleFormat(QAudioFormat::Float);
    if (!device.isFormatSupported(fmt)) {
        fmt = device.preferredFormat();
        fmt.setSampleFormat(QAudioFormat::Float);
        if (!device.isFormatSupported(fmt))
            fmt.setSampleFormat(QAudioFormat::Int16);
    }
    m_format = fmt;
    m_sink = std::make_unique<QAudioSink>(device, fmt);
    // Small buffer = low sidetone latency. Too small may crackle; tune in settings.
    m_sink->setBufferSize(fmt.bytesForDuration(qint64(bufferMs) * 1000));
#if QT_VERSION >= QT_VERSION_CHECK(6, 11, 0)
    // Real-time callback on the audio thread: renders exactly what the device
    // needs, so the buffer size really is the latency and nothing underruns.
    const int ch = fmt.channelCount();
    switch (fmt.sampleFormat()) {
    case QAudioFormat::Float:
        m_sink->start([this, ch](QSpan<float> b) { render(b.data(), b.size() / ch, ch); });
        break;
    case QAudioFormat::Int16:
        m_sink->start([this, ch](QSpan<int16_t> b) { render(b.data(), b.size() / ch, ch); });
        break;
    case QAudioFormat::Int32:
        m_sink->start([this, ch](QSpan<int32_t> b) { render(b.data(), b.size() / ch, ch); });
        break;
    default:
        m_sink->start([this, ch](QSpan<uint8_t> b) { render(b.data(), b.size() / ch, ch); });
        break;
    }
#else
    open(QIODevice::ReadOnly);
    m_sink->start(this);
#endif
    if (m_sink->error() != QAudio::NoError) {
        if (error)
            *error = QStringLiteral("Audio output failed to start (error %1)").arg(int(m_sink->error()));
        stopAudio();
        return false;
    }
    return true;
}

void ToneGenerator::stopAudio()
{
    if (m_sink) {
        m_sink->stop();
        m_sink.reset();
    }
    if (isOpen())
        close();
}

QString ToneGenerator::status() const
{
    if (!m_sink)
        return QStringLiteral("audio off");
    return QStringLiteral("%1 Hz, %2 ch, buffer %3 ms")
        .arg(m_format.sampleRate())
        .arg(m_format.channelCount())
        .arg(m_format.durationForBytes(m_sink->bufferSize()) / 1000);
}

void ToneGenerator::play(const std::vector<morse::Segment> &seq)
{
    const double rate = m_format.sampleRate() > 0 ? m_format.sampleRate() : 48000;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.clear();
    m_playLeft = 0;
    m_playOn = false;
    m_queue.push_back({false, int64_t(0.15 * rate)});   // short lead-in
    for (const auto &s : seq)
        m_queue.push_back({s.on, int64_t(s.ms * rate / 1000.0)});
    m_playing = true;
    m_finishPoll.start();
}

void ToneGenerator::stopPlayback()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.clear();
    m_playLeft = 0;
    m_playOn = false;
    m_playing = false;
}

qint64 ToneGenerator::readData(char *data, qint64 maxlen)
{
    const int channels = m_format.channelCount();
    const int frameBytes = channels * m_format.bytesPerSample();
    if (frameBytes <= 0)
        return 0;
    const qint64 frames = maxlen / frameBytes;
    switch (m_format.sampleFormat()) {
    case QAudioFormat::Float: render(reinterpret_cast<float *>(data), frames, channels); break;
    case QAudioFormat::Int16: render(reinterpret_cast<int16_t *>(data), frames, channels); break;
    case QAudioFormat::Int32: render(reinterpret_cast<int32_t *>(data), frames, channels); break;
    default: render(reinterpret_cast<uint8_t *>(data), frames, channels); break;
    }
    return frames * frameBytes;
}

template <typename T>
void ToneGenerator::render(T *out, qint64 frames, int channels)
{
    const double rate = m_format.sampleRate();
    const double step = 2.0 * M_PI * m_freq / rate;
    const double rampStep = 1.0 / std::max(1.0, m_rampMs * rate / 1000.0);
    const double vol = m_volume;
    const bool sidetone = m_sidetone;

    std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
    for (qint64 i = 0; i < frames; ++i) {
        bool playOn = false;
        if (lock.owns_lock() && m_playing) {
            while (m_playLeft <= 0 && !m_queue.empty()) {
                m_playOn = m_queue.front().on;
                m_playLeft = m_queue.front().samples;
                m_queue.pop_front();
            }
            if (m_playLeft > 0) {
                --m_playLeft;
                playOn = m_playOn;
            } else {
                m_playing = false;
            }
        }
        // Key state is read per sample so edges aren't quantised to the buffer.
        const bool on = (sidetone && m_keyed.load(std::memory_order_relaxed)) || playOn;
        m_env = on ? std::min(1.0, m_env + rampStep) : std::max(0.0, m_env - rampStep);
        const double shaped = 0.5 - 0.5 * std::cos(M_PI * m_env);
        const double s = shaped > 0 ? std::sin(m_phase) * shaped * vol : 0.0;
        m_phase += step;
        if (m_phase > 2.0 * M_PI)
            m_phase -= 2.0 * M_PI;
        T v;
        if constexpr (std::is_same_v<T, float>)
            v = float(s);
        else if constexpr (std::is_same_v<T, int16_t>)
            v = int16_t(s * 32767.0);
        else if constexpr (std::is_same_v<T, int32_t>)
            v = int32_t(s * 2147483647.0);
        else
            v = uint8_t(128 + s * 127.0);
        for (int c = 0; c < channels; ++c)
            *out++ = v;
    }
}
