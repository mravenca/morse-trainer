#include "keyscope.h"

#include <QPainter>
#include <QPainterPath>

KeyScope::KeyScope(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(70);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, qOverload<>(&QWidget::update));
    m_timer.start();
}

void KeyScope::addEdge(bool down, qint64 tUs)
{
    if (down) {
        if (m_marks.empty() || m_marks.back().end >= 0)
            m_marks.push_back({tUs, -1});
    } else if (!m_marks.empty() && m_marks.back().end < 0) {
        m_marks.back().end = tUs;
    }
    const qint64 horizon = tUs - qint64(m_windowS * 1.2e6);
    while (!m_marks.empty() && m_marks.front().end >= 0 && m_marks.front().end < horizon)
        m_marks.pop_front();
}

void KeyScope::setTiming(double unitMs, double thresholdMs)
{
    m_unitMs = unitMs;
    m_thresholdMs = thresholdMs;
}

void KeyScope::clear()
{
    m_marks.clear();
    update();
}

void KeyScope::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QPalette pal = palette();
    const QRectF r = rect().adjusted(1, 1, -1, -1);
    p.fillRect(r, pal.color(QPalette::Base));

    const qint64 now = m_now ? m_now() : 0;
    const double pxPerUs = r.width() / (m_windowS * 1e6);
    auto xOf = [&](qint64 t) { return r.right() - (now - t) * pxPerUs; };

    // Unit grid, anchored to absolute time so it scrolls with the tape.
    QColor grid = pal.color(QPalette::Text);
    grid.setAlphaF(0.08);
    p.setPen(QPen(grid, 1));
    const qint64 unitUs = qint64(m_unitMs * 1000);
    if (unitUs > 0 && r.width() / (m_windowS * 1000 / m_unitMs) > 3) {
        for (qint64 t = now - (now % unitUs); xOf(t) > r.left(); t -= unitUs)
            p.drawLine(QPointF(xOf(t), r.top()), QPointF(xOf(t), r.bottom()));
    }

    const QColor ditColor(0x2b, 0x8a, 0xd8);
    const QColor dahColor(0xe0, 0x8a, 0x1f);
    const QColor liveColor(0x3c, 0xb3, 0x71);
    const double top = r.top() + r.height() * 0.25;
    const double h = r.height() * 0.5;

    for (const Mark &m : m_marks) {
        const qint64 end = m.end < 0 ? now : m.end;
        const double x1 = xOf(m.start), x2 = xOf(end);
        if (x2 < r.left())
            continue;
        const double durMs = (end - m.start) / 1000.0;
        QColor c = m.end < 0 ? liveColor : (durMs >= m_thresholdMs ? dahColor : ditColor);
        QPainterPath path;
        path.addRoundedRect(QRectF(QPointF(std::max(x1, r.left()), top), QPointF(x2, top + h)), 3, 3);
        p.fillPath(path, c);
    }

    // Legend
    p.setPen(pal.color(QPalette::PlaceholderText));
    QFont f = font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    p.setFont(f);
    p.drawText(r.adjusted(6, 2, -6, -2), Qt::AlignLeft | Qt::AlignTop,
               QStringLiteral("blue = dit   orange = dah   grid = 1 unit (%1 ms)").arg(m_unitMs, 0, 'f', 0));

    p.setPen(QPen(pal.color(QPalette::Mid), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(r);
}
