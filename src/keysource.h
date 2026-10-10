#pragma once
// Base class for hardware key inputs. Backends run their own thread and emit
// keyChanged() with a CLOCK_MONOTONIC timestamp in microseconds, so edge timing
// is taken as close to the hardware as possible, not when the GUI gets to it.
#include <QObject>
#include <QString>
#include <cstdint>

int64_t monotonicUs();

class KeySource : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    ~KeySource() override = default;

    virtual bool start(QString *error) = 0;
    virtual void stop() = 0;
    virtual QString description() const = 0;

signals:
    void keyChanged(bool down, qint64 tUs);
    void failed(const QString &reason);   // emitted if the device goes away
};
