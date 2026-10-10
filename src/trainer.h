#pragma once
// Koch-method lesson generator and scoring. Plain C++, no Qt.
#include <deque>
#include <map>
#include <random>
#include <string>
#include <vector>

class Trainer
{
public:
    Trainer();

    void setLevel(int chars);                // number of Koch characters in play (2..41)
    int level() const { return m_level; }
    std::vector<std::string> charset() const;
    std::string newestChar() const;

    // Random group of symbols from the current charset; the newest character
    // is drawn twice as often so it gets extra practice.
    std::string group(int length);
    std::string text(int groups, int length);

    struct Result {
        std::vector<std::string> expected;   // spaces removed
        std::vector<bool> ok;                // per expected symbol
        int distance = 0;                    // edit distance to what was given
        int correct() const;
        double accuracy() const;             // 0..1, penalises insertions too
    };
    // Aligns `given` against `expected` (spaces ignored) and records the result.
    Result grade(const std::string &expected, const std::string &given);

    double recentAccuracy() const;           // over last ~100 symbols, -1 if too few
    int recentCount() const { return int(m_recent.size()); }
    bool readyToAdvance() const;             // >= 90 % over >= 50 symbols
    // Characters with most misses (symbol, error rate), worst first.
    std::vector<std::pair<std::string, double>> weakest(int n) const;
    void resetStats();

private:
    int m_level = 2;
    std::mt19937 m_rng;
    std::deque<bool> m_recent;
    std::map<std::string, std::pair<int, int>> m_perChar;   // symbol -> (seen, missed)
};
