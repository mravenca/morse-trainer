#include "gpiokeysource.h"

#include <QDir>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <unistd.h>

#if __has_include(<linux/gpio.h>)
#include <linux/gpio.h>
#endif

#ifdef GPIO_V2_GET_LINE_IOCTL
#define HAVE_GPIO_V2 1
#else
#define HAVE_GPIO_V2 0
#endif

GpioKeySource::GpioKeySource(const QString &chip, unsigned line, bool activeLow, bool pullUp,
                             int debounceMs, QObject *parent)
    : KeySource(parent), m_chip(chip), m_line(line), m_activeLow(activeLow), m_pullUp(pullUp),
      m_debounceMs(debounceMs)
{
}

GpioKeySource::~GpioKeySource()
{
    stop();
}

bool GpioKeySource::supported()
{
    return HAVE_GPIO_V2;
}

QStringList GpioKeySource::availableChips()
{
    QStringList out;
    const auto names = QDir("/dev").entryList({QStringLiteral("gpiochip*")}, QDir::System, QDir::Name);
    for (const QString &n : names)
        out << "/dev/" + n;
    return out;
}

QString GpioKeySource::description() const
{
    return QStringLiteral("%1 line %2%3").arg(m_chip).arg(m_line).arg(m_activeLow ? " (active low)" : "");
}

bool GpioKeySource::start(QString *error)
{
    stop();
#if HAVE_GPIO_V2
    const int chipFd = ::open(m_chip.toLocal8Bit().constData(), O_RDONLY | O_CLOEXEC);
    if (chipFd < 0) {
        if (error)
            *error = QStringLiteral("Cannot open %1: %2").arg(m_chip, strerror(errno));
        return false;
    }
    gpio_v2_line_request req{};
    req.offsets[0] = m_line;
    req.num_lines = 1;
    std::strncpy(req.consumer, "morse-trainer", sizeof(req.consumer) - 1);
    req.config.flags = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EDGE_RISING |
                       GPIO_V2_LINE_FLAG_EDGE_FALLING |
                       (m_activeLow ? GPIO_V2_LINE_FLAG_ACTIVE_LOW : 0) |
                       (m_pullUp ? GPIO_V2_LINE_FLAG_BIAS_PULL_UP : GPIO_V2_LINE_FLAG_BIAS_DISABLED);
    const int rc = ioctl(chipFd, GPIO_V2_GET_LINE_IOCTL, &req);
    const int err = errno;
    ::close(chipFd);
    if (rc != 0) {
        if (error) {
            *error = QStringLiteral("Cannot request line %1 on %2: %3").arg(m_line).arg(m_chip, strerror(err));
            if (err == EBUSY)
                *error += QStringLiteral("\nThe line is used by another driver or process (check the device tree / gpioinfo).");
        }
        return false;
    }
    m_lineFd = req.fd;
    m_wakeFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    m_stop = false;
    m_thread = std::thread([this] { run(); });
    return true;
#else
    if (error)
        *error = QStringLiteral("Built without GPIO uAPI v2 headers (needs kernel headers >= 5.10).");
    return false;
#endif
}

void GpioKeySource::stop()
{
    if (m_thread.joinable()) {
        m_stop = true;
        const uint64_t one = 1;
        [[maybe_unused]] auto n = ::write(m_wakeFd, &one, sizeof one);
        m_thread.join();
    }
    if (m_lineFd >= 0) {
        ::close(m_lineFd);
        m_lineFd = -1;
    }
    if (m_wakeFd >= 0) {
        ::close(m_wakeFd);
        m_wakeFd = -1;
    }
}

bool GpioKeySource::readValue(bool *active)
{
#if HAVE_GPIO_V2
    gpio_v2_line_values v{};
    v.mask = 1;
    if (ioctl(m_lineFd, GPIO_V2_LINE_GET_VALUES_IOCTL, &v) != 0)
        return false;
    *active = (v.bits & 1) != 0;   // already inverted by the kernel when active-low
    return true;
#else
    (void)active;
    return false;
#endif
}

void GpioKeySource::run()
{
#if HAVE_GPIO_V2
    bool state = false;
    if (readValue(&state) && state)
        emit keyChanged(true, monotonicUs());

    pollfd fds[2] = {{m_lineFd, POLLIN, 0}, {m_wakeFd, POLLIN, 0}};
    int64_t ignoreUntilUs = 0;

    while (!m_stop) {
        int timeoutMs = -1;
        if (ignoreUntilUs) {
            const int64_t left = ignoreUntilUs - monotonicUs();
            timeoutMs = left > 0 ? int((left + 999) / 1000) : 0;
        }
        const int n = poll(fds, 2, timeoutMs);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            emit failed(QStringLiteral("poll failed: %1").arg(strerror(errno)));
            return;
        }
        if (fds[1].revents)
            return;

        if (fds[0].revents & POLLIN) {
            gpio_v2_line_event events[16];
            const ssize_t r = ::read(m_lineFd, events, sizeof events);
            if (r <= 0) {
                emit failed(QStringLiteral("GPIO read failed"));
                return;
            }
            const int count = int(r / sizeof(gpio_v2_line_event));
            for (int i = 0; i < count; ++i) {
                const int64_t t = int64_t(events[i].timestamp_ns / 1000);
                const bool down = events[i].id == GPIO_V2_LINE_EVENT_RISING_EDGE;
                if (ignoreUntilUs && t < ignoreUntilUs)
                    continue;      // contact bounce
                if (down == state)
                    continue;
                state = down;
                emit keyChanged(state, t);
                ignoreUntilUs = m_debounceMs > 0 ? t + m_debounceMs * 1000 : 0;
            }
        }
        // Debounce window over: resync with the real level in case the final
        // bounce edge was swallowed.
        if (ignoreUntilUs && monotonicUs() >= ignoreUntilUs) {
            ignoreUntilUs = 0;
            bool now;
            if (readValue(&now) && now != state) {
                state = now;
                emit keyChanged(state, monotonicUs());
            }
        }
    }
#endif
}
