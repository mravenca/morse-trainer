#pragma once
// Adaptive straight-key decoder. Feed it key edges with microsecond timestamps
// (any monotonic clock) and call tick() periodically so characters are flushed
// during silence. Plain C++, no Qt.
#include <cstdint>
#include <deque>
#include <string>

class Decoder
{
public:
    struct Stats {
        double ditMs = 0;      // running average of marks classified as dits
        double dahMs = 0;      // running average of marks classified as dahs
        double elemGapMs = 0;  // running average of gaps inside characters
        int dits = 0;
        int dahs = 0;
        double ratio() const { return ditMs > 0 ? dahMs / ditMs : 0; }      // ideal 3.0
        double weight() const { return elemGapMs > 0 ? ditMs / elemGapMs : 0; } // ideal 1.0
    };

    explicit Decoder(double initialWpm = 18.0, bool adaptive = true);

    // Each returns text produced by this event (a symbol, " ", or empty).
    std::string keyDown(int64_t tUs);
    std::string keyUp(int64_t tUs);
    std::string tick(int64_t nowUs);

    void reset();
    void setWpm(double wpm);          // re-seeds the timing estimate
    void setAdaptive(bool adaptive) { m_adaptive = adaptive; }

    double unitMs() const;            // current estimate of one Morse unit
    double wpm() const { return 1200.0 / unitMs(); }
    double dotDashThresholdMs() const;
    double wordGapThresholdMs() const; // silence that ends a word
    const std::string &pattern() const { return m_pattern; }
    const Stats &stats() const { return m_stats; }
    bool isDown() const { return m_down; }

    static constexpr int64_t kGlitchUs = 8000;  // marks shorter than this are ignored
    static constexpr size_t kWindow = 16;       // marks used for speed estimation

private:
    std::string flush(int64_t nowUs);
    void adapt(double markMs);

    bool m_adaptive;
    double m_estDit;      // ms
    double m_estDah;      // ms
    bool m_down = false;
    bool m_haveUp = false;
    int64_t m_downAt = 0;
    int64_t m_upAt = 0;
    std::string m_pattern;
    bool m_needWordSpace = false;  // a symbol was emitted since the last word space
    Stats m_stats;
    std::deque<double> m_recent;   // recent mark durations (ms)
    std::deque<double> m_charGaps; // recent gaps between characters/words (ms)
};
