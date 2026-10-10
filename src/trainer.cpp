#include "trainer.h"
#include "morse.h"

#include <algorithm>

namespace {
constexpr size_t kRecentWindow = 100;

std::vector<std::string> noSpaces(const std::string &s)
{
    std::vector<std::string> out;
    for (auto &sym : morse::symbols(s))
        if (sym != " ")
            out.push_back(sym);
    return out;
}
} // namespace

Trainer::Trainer()
    : m_rng(std::random_device{}())
{
}

void Trainer::setLevel(int chars)
{
    m_level = std::clamp(chars, 2, int(morse::kochOrder().size()));
}

std::vector<std::string> Trainer::charset() const
{
    const auto &k = morse::kochOrder();
    return {k.begin(), k.begin() + m_level};
}

std::string Trainer::newestChar() const
{
    return morse::kochOrder()[m_level - 1];
}

std::string Trainer::group(int length)
{
    const auto set = charset();
    // Weights: 1 for each char, 2 for the newest.
    std::vector<double> w(set.size(), 1.0);
    w.back() = 2.0;
    std::discrete_distribution<size_t> pick(w.begin(), w.end());
    std::string g;
    for (int i = 0; i < length; ++i)
        g += set[pick(m_rng)];
    return g;
}

std::string Trainer::text(int groups, int length)
{
    std::string t;
    for (int i = 0; i < groups; ++i) {
        if (i)
            t += ' ';
        t += group(length);
    }
    return t;
}

int Trainer::Result::correct() const
{
    return int(std::count(ok.begin(), ok.end(), true));
}

double Trainer::Result::accuracy() const
{
    if (expected.empty())
        return 1.0;
    return std::max(0.0, 1.0 - double(distance) / double(expected.size()));
}

Trainer::Result Trainer::grade(const std::string &expectedText, const std::string &givenText)
{
    Result r;
    r.expected = noSpaces(expectedText);
    const auto given = noSpaces(givenText);
    const size_t n = r.expected.size(), m = given.size();

    // Full DP so we can back-trace which expected symbols were matched.
    std::vector<std::vector<int>> d(n + 1, std::vector<int>(m + 1));
    for (size_t i = 0; i <= n; ++i) d[i][0] = int(i);
    for (size_t j = 0; j <= m; ++j) d[0][j] = int(j);
    for (size_t i = 1; i <= n; ++i)
        for (size_t j = 1; j <= m; ++j)
            d[i][j] = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1,
                                d[i - 1][j - 1] + (r.expected[i - 1] == given[j - 1] ? 0 : 1)});
    r.distance = d[n][m];
    r.ok.assign(n, false);
    size_t i = n, j = m;
    while (i > 0 && j > 0) {
        if (r.expected[i - 1] == given[j - 1] && d[i][j] == d[i - 1][j - 1]) {
            r.ok[i - 1] = true; --i; --j;
        } else if (d[i][j] == d[i - 1][j - 1] + 1) {
            --i; --j;                      // substitution
        } else if (d[i][j] == d[i - 1][j] + 1) {
            --i;                           // expected symbol missing
        } else {
            --j;                           // extra symbol given
        }
    }

    for (size_t k = 0; k < n; ++k) {
        auto &pc = m_perChar[r.expected[k]];
        ++pc.first;
        if (!r.ok[k])
            ++pc.second;
        m_recent.push_back(r.ok[k]);
    }
    // Insertions count as errors in the rolling score too.
    for (int extra = r.distance - (int(n) - r.correct()); extra > 0; --extra)
        m_recent.push_back(false);
    while (m_recent.size() > kRecentWindow)
        m_recent.pop_front();
    return r;
}

double Trainer::recentAccuracy() const
{
    if (m_recent.size() < 10)
        return -1;
    return double(std::count(m_recent.begin(), m_recent.end(), true)) / double(m_recent.size());
}

bool Trainer::readyToAdvance() const
{
    return m_recent.size() >= 50 && recentAccuracy() >= 0.9;
}

std::vector<std::pair<std::string, double>> Trainer::weakest(int n) const
{
    std::vector<std::pair<std::string, double>> v;
    for (const auto &[sym, pc] : m_perChar)
        if (pc.first >= 3 && pc.second > 0)
            v.emplace_back(sym, double(pc.second) / pc.first);
    std::sort(v.begin(), v.end(), [](auto &a, auto &b) { return a.second > b.second; });
    if (int(v.size()) > n)
        v.resize(n);
    return v;
}

void Trainer::resetStats()
{
    m_recent.clear();
    m_perChar.clear();
}
