#pragma once
// Straight key on a serial port's modem-status input (the classic ham wiring).
// DTR and RTS are driven high as a supply; the key closes DTR -> CTS (or DSR /
// DCD / RI). Works with built-in ports and USB adapters (FTDI, CP210x, PL2303,
// CH340...). Uses TIOCMIWAIT where the driver supports it, otherwise polls 1 kHz.
#include "keysource.h"

#include <atomic>
#include <thread>

class SerialKeySource : public KeySource
{
    Q_OBJECT
public:
    enum class Line { CTS, DSR, DCD, RI };

    SerialKeySource(const QString &device, Line line, bool invert, int debounceMs,
                    QObject *parent = nullptr);
    ~SerialKeySource() override;

    bool start(QString *error) override;
    void stop() override;
    QString description() const override;

    static QStringList availablePorts();

private:
    void run();
    bool readState(bool *down);

    QString m_device;
    Line m_line;
    bool m_invert;
    int m_debounceMs;
    int m_fd = -1;
    bool m_waitSupported = true;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_done{true};
    std::thread m_thread;
};
