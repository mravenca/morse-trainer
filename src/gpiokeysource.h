#pragma once
// Straight key on a GPIO line, via the Linux GPIO character device uAPI v2
// directly (no libgpiod dependency). Edge timestamps come from the
// kernel interrupt handler (CLOCK_MONOTONIC), so GUI load does not skew timing.
#include "keysource.h"

#include <atomic>
#include <thread>

class GpioKeySource : public KeySource
{
    Q_OBJECT
public:
    // activeLow: key shorts the line to GND (typical, with pullUp = true).
    GpioKeySource(const QString &chip, unsigned line, bool activeLow, bool pullUp, int debounceMs,
                  QObject *parent = nullptr);
    ~GpioKeySource() override;

    bool start(QString *error) override;
    void stop() override;
    QString description() const override;

    static QStringList availableChips();
    static bool supported();

private:
    void run();
    bool readValue(bool *active);

    QString m_chip;
    unsigned m_line;
    bool m_activeLow;
    bool m_pullUp;
    int m_debounceMs;
    int m_lineFd = -1;
    int m_wakeFd = -1;
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
};
