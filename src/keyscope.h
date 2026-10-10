#pragma once
// Scrolling "paper tape" of the last few seconds of keying. Marks are coloured
// by how the decoder classified them, with a dit-unit grid underneath, so a
// straight-key operator can see uneven dits, short dahs and cramped spacing.
#include <QWidget>
#include <QTimer>

#include <deque>
#include <functional>

class KeyScope : public QWidget
{
    Q_OBJECT
public:
    explicit KeyScope(QWidget *parent = nullptr);

    void addEdge(bool down, qint64 tUs);
    void setTiming(double unitMs, double thresholdMs);
    void setClock(std::function<qint64()> nowUs) { m_now = std::move(nowUs); }
    void clear();

    QSize sizeHint() const override { return {600, 90}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    struct Mark { qint64 start; qint64 end; };   // end < 0 while key is down
    std::deque<Mark> m_marks;
    double m_unitMs = 66.7;
    double m_thresholdMs = 115;
    double m_windowS = 6.0;
    std::function<qint64()> m_now;
    QTimer m_timer;
};
