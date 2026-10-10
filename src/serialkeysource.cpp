#include "serialkeysource.h"

#include <QDir>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace {

int lineBit(SerialKeySource::Line l)
{
    switch (l) {
    case SerialKeySource::Line::CTS: return TIOCM_CTS;
    case SerialKeySource::Line::DSR: return TIOCM_DSR;
    case SerialKeySource::Line::DCD: return TIOCM_CAR;
    case SerialKeySource::Line::RI:  return TIOCM_RNG;
    }
    return TIOCM_CTS;
}

const char *lineName(SerialKeySource::Line l)
{
    switch (l) {
    case SerialKeySource::Line::CTS: return "CTS";
    case SerialKeySource::Line::DSR: return "DSR";
    case SerialKeySource::Line::DCD: return "DCD";
    case SerialKeySource::Line::RI:  return "RI";
    }
    return "?";
}

// SIGUSR1 is used only to interrupt a blocking TIOCMIWAIT when stopping.
void noopHandler(int) {}

void installWakeSignal()
{
    static bool done = false;
    if (done)
        return;
    struct sigaction sa{};
    sa.sa_handler = noopHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // no SA_RESTART: the ioctl must return EINTR
    sigaction(SIGUSR1, &sa, nullptr);
    done = true;
}

void sleepMs(int ms)
{
    timespec ts{ms / 1000, (ms % 1000) * 1'000'000L};
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {}
}

} // namespace

SerialKeySource::SerialKeySource(const QString &device, Line line, bool invert, int debounceMs,
                                 QObject *parent)
    : KeySource(parent), m_device(device), m_line(line), m_invert(invert), m_debounceMs(debounceMs)
{
}

SerialKeySource::~SerialKeySource()
{
    stop();
}

QStringList SerialKeySource::availablePorts()
{
    QStringList out;
    QDir dev("/dev");
    for (const QString &pattern : {QStringLiteral("ttyUSB*"), QStringLiteral("ttyACM*"),
                                   QStringLiteral("ttyLP*"), QStringLiteral("ttymxc*"),
                                   QStringLiteral("ttyS*")}) {
        const auto names = dev.entryList({pattern}, QDir::System, QDir::Name);
        for (const QString &n : names) {
            // Skip the dozens of non-existent legacy ttyS ports.
            if (n.startsWith("ttyS") && !QFile::exists("/sys/class/tty/" + n + "/device/driver"))
                continue;
            out << "/dev/" + n;
        }
    }
    return out;
}

QString SerialKeySource::description() const
{
    return QStringLiteral("%1 (DTR → %2%3)").arg(m_device, lineName(m_line),
                                               m_invert ? ", inverted" : "");
}

bool SerialKeySource::start(QString *error)
{
    stop();
    m_fd = ::open(m_device.toLocal8Bit().constData(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (m_fd < 0) {
        if (error) {
            *error = QStringLiteral("Cannot open %1: %2").arg(m_device, strerror(errno));
            if (errno == EACCES)
                *error += QStringLiteral("\nAdd yourself to the 'dialout' group and log in again.");
        }
        return false;
    }
    // Raw mode, no flow control, so the driver does not touch RTS/CTS itself.
    termios tio{};
    if (tcgetattr(m_fd, &tio) == 0) {
        cfmakeraw(&tio);
        tio.c_cflag &= ~CRTSCTS;
        tio.c_cflag |= CLOCAL | CREAD;
        tcsetattr(m_fd, TCSANOW, &tio);
    }
    int out = TIOCM_DTR | TIOCM_RTS;
    if (ioctl(m_fd, TIOCMBIS, &out) != 0) {
        if (error)
            *error = QStringLiteral("Cannot raise DTR/RTS on %1: %2").arg(m_device, strerror(errno));
        ::close(m_fd);
        m_fd = -1;
        return false;
    }
    bool dummy;
    if (!readState(&dummy)) {
        if (error)
            *error = QStringLiteral("%1 does not report modem status lines").arg(m_device);
        ::close(m_fd);
        m_fd = -1;
        return false;
    }
    installWakeSignal();
    m_stop = false;
    m_waitSupported = true;
    m_done = false;
    m_thread = std::thread([this] {
        run();
        m_done = true;
    });
    return true;
}

void SerialKeySource::stop()
{
    if (m_thread.joinable()) {
        m_stop = true;
        // Keep poking until the thread notices: a signal that lands just
        // before it enters TIOCMIWAIT would otherwise be lost.
        while (!m_done) {
            pthread_kill(m_thread.native_handle(), SIGUSR1);
            sleepMs(5);
        }
        m_thread.join();
    }
    if (m_fd >= 0) {
        int out = TIOCM_DTR | TIOCM_RTS;
        ioctl(m_fd, TIOCMBIC, &out);
        ::close(m_fd);
        m_fd = -1;
    }
}

bool SerialKeySource::readState(bool *down)
{
    int status = 0;
    if (ioctl(m_fd, TIOCMGET, &status) != 0)
        return false;
    *down = ((status & lineBit(m_line)) != 0) != m_invert;
    return true;
}

void SerialKeySource::run()
{
    bool state = false;
    if (!readState(&state)) {
        emit failed(QStringLiteral("Lost %1").arg(m_device));
        return;
    }
    if (state)
        emit keyChanged(true, monotonicUs());

    const int bit = lineBit(m_line);
    while (!m_stop) {
        if (m_waitSupported) {
            if (ioctl(m_fd, TIOCMIWAIT, bit) != 0) {
                if (errno == EINTR)
                    continue;
                if (errno == EIO || errno == ENODEV || errno == ENXIO) {
                    emit failed(QStringLiteral("%1 disconnected").arg(m_device));
                    return;
                }
                m_waitSupported = false;   // driver lacks TIOCMIWAIT -> poll
                continue;
            }
        } else {
            sleepMs(1);
        }
        const int64_t t = monotonicUs();
        bool now;
        if (!readState(&now)) {
            emit failed(QStringLiteral("%1 disconnected").arg(m_device));
            return;
        }
        if (now == state)
            continue;
        state = now;
        emit keyChanged(state, t);

        // Leading-edge debounce: report the first edge immediately, ignore
        // contact bounce for a few ms, then resync with the real line state.
        if (m_debounceMs > 0) {
            sleepMs(m_debounceMs);
            if (readState(&now) && now != state) {
                state = now;
                emit keyChanged(state, monotonicUs());
            }
        }
    }
}
