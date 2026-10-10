#include "decoder.h"
#include "morse.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
constexpr double kAlpha = 0.25;      // stats smoothing
constexpr double kMinUnit = 20.0;    // 60 WPM
constexpr double kMaxUnit = 300.0;   // 4 WPM

double ema(double avg, double v, int count)
{
    return count <= 1 ? v : avg + kAlpha * (v - avg);
}
} // namespace

Decoder::Decoder(double initialWpm, bool adaptive)
    : m_adaptive(adaptive)
{
    setWpm(initialWpm);
}

void Decoder::setWpm(double wpm)
{
    m_estDit = std::clamp(1200.0 / std::max(wpm, 1.0), kMinUnit, kMaxUnit);
    m_estDah = 3.0 * m_estDit;
}

void Decoder::reset()
{
    m_down = false;
    m_haveUp = false;
    m_pattern.clear();
    m_needWordSpace = false;
    m_stats = Stats{};
    m_recent.clear();
    m_charGaps.clear();
}

double Decoder::unitMs() const
{
    return std::clamp((m_estDit + m_estDah / 3.0) / 2.0, kMinUnit, kMaxUnit);
}

double Decoder::dotDashThresholdMs() const
{
    // Geometric mean sits between the two clusters even when the ratio drifts.
    return std::sqrt(m_estDit * m_estDah);
}

double Decoder::wordGapThresholdMs() const
{
    // Nominally between 3 (char) and 7 (word) units. Learners and Farnsworth
    // practice use much longer letter gaps, so follow the median of the
    // operator's recent letter gaps once there are a few of them.
    const double nominal = 5.0 * unitMs();
    if (m_charGaps.size() < 4)
        return nominal;
    std::vector<double> g(m_charGaps.begin(), m_charGaps.end());
    std::nth_element(g.begin(), g.begin() + g.size() / 2, g.end());
    return std::max(nominal, 1.5 * g[g.size() / 2]);
}

std::string Decoder::flush(int64_t nowUs)
{
    std::string out;
    if (m_down || !m_haveUp)
        return out;
    const double gapMs = (nowUs - m_upAt) / 1000.0;
    const double u = unitMs();

    if (!m_pattern.empty() && gapMs >= 2.0 * u) {      // between 1 (element) and 3 (char)
        std::string sym = morse::decode(m_pattern);
        out += sym.empty() ? "*" : sym;
        m_pattern.clear();
        m_needWordSpace = true;
    }
    if (m_needWordSpace && gapMs >= wordGapThresholdMs()) {
        out += " ";
        m_needWordSpace = false;
    }
    return out;
}

std::string Decoder::keyDown(int64_t tUs)
{
    if (m_down)
        return {};
    std::string out = flush(tUs);
    if (m_haveUp) {
        const double gapMs = (tUs - m_upAt) / 1000.0;
        if (!m_pattern.empty()) {
            m_stats.elemGapMs = ema(m_stats.elemGapMs, gapMs, m_stats.dits + m_stats.dahs);
        } else if (gapMs < 60000.0) {   // ignore long pauses (operator stopped)
            m_charGaps.push_back(gapMs);
            while (m_charGaps.size() > kWindow)
                m_charGaps.pop_front();
        }
    }
    m_down = true;
    m_downAt = tUs;
    return out;
}

std::string Decoder::keyUp(int64_t tUs)
{
    if (!m_down)
        return {};
    m_down = false;
    const int64_t durUs = tUs - m_downAt;
    if (durUs < kGlitchUs) {
        // Contact bounce / noise: pretend it never happened.
        return {};
    }
    const double ms = durUs / 1000.0;
    const bool isDah = ms >= dotDashThresholdMs();
    m_pattern += isDah ? '-' : '.';
    if (m_pattern.size() > 10) // runaway (e.g. key held/ chattering) -> give up on this char
        m_pattern = "........";

    if (isDah) {
        ++m_stats.dahs;
        m_stats.dahMs = ema(m_stats.dahMs, ms, m_stats.dahs);
    } else {
        ++m_stats.dits;
        m_stats.ditMs = ema(m_stats.ditMs, ms, m_stats.dits);
    }
    if (m_adaptive)
        adapt(ms);
    m_haveUp = true;
    m_upAt = tUs;
    return {};
}

// Re-estimates dit/dah length from the last marks with a 1-D two-means split in
// the log domain. If the window holds only one cluster (e.g. "TTT" or "EEE"),
// it is assigned to dit or dah by the current threshold and the other cluster
// is placed at the ideal 1:3 ratio.
void Decoder::adapt(double markMs)
{
    m_recent.push_back(markMs);
    while (m_recent.size() > kWindow)
        m_recent.pop_front();

    std::vector<double> v(m_recent.begin(), m_recent.end());
    std::sort(v.begin(), v.end());
    std::vector<double> lg(v.size());
    for (size_t i = 0; i < v.size(); ++i)
        lg[i] = std::log(v[i]);

    double bestCost = 1e300;
    size_t bestSplit = 0;
    for (size_t k = 1; k < v.size(); ++k) {
        double m1 = 0, m2 = 0;
        for (size_t i = 0; i < k; ++i) m1 += lg[i];
        for (size_t i = k; i < v.size(); ++i) m2 += lg[i];
        m1 /= k;
        m2 /= (v.size() - k);
        double cost = 0;
        for (size_t i = 0; i < k; ++i) cost += (lg[i] - m1) * (lg[i] - m1);
        for (size_t i = k; i < v.size(); ++i) cost += (lg[i] - m2) * (lg[i] - m2);
        if (cost < bestCost) { bestCost = cost; bestSplit = k; }
    }

    auto mean = [&v](size_t a, size_t b) {
        double s = 0;
        for (size_t i = a; i < b; ++i) s += v[i];
        return s / double(b - a);
    };

    bool split = false;
    if (bestSplit > 0) {
        const double lo = mean(0, bestSplit);
        const double hi = mean(bestSplit, v.size());
        if (hi / lo >= 1.8) {
            m_estDit = lo;
            m_estDah = hi;
            split = true;
        }
    }
    if (!split) {
        const double all = mean(0, v.size());
        if (all >= dotDashThresholdMs()) {
            m_estDah = all;
            m_estDit = all / 3.0;
        } else {
            m_estDit = all;
            m_estDah = all * 3.0;
        }
    }
    m_estDit = std::clamp(m_estDit, kMinUnit, kMaxUnit);
    m_estDah = std::clamp(m_estDah, 1.8 * m_estDit, 3.0 * kMaxUnit);
}

std::string Decoder::tick(int64_t nowUs)
{
    return flush(nowUs);
}
