#include "morse.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace morse {

namespace {

const std::map<std::string, std::string> &table()
{
    static const std::map<std::string, std::string> t = {
        {"A", ".-"},     {"B", "-..."},   {"C", "-.-."},   {"D", "-.."},    {"E", "."},
        {"F", "..-."},   {"G", "--."},    {"H", "...."},   {"I", ".."},     {"J", ".---"},
        {"K", "-.-"},    {"L", ".-.."},   {"M", "--"},     {"N", "-."},     {"O", "---"},
        {"P", ".--."},   {"Q", "--.-"},   {"R", ".-."},    {"S", "..."},    {"T", "-"},
        {"U", "..-"},    {"V", "...-"},   {"W", ".--"},    {"X", "-..-"},   {"Y", "-.--"},
        {"Z", "--.."},
        {"0", "-----"},  {"1", ".----"},  {"2", "..---"},  {"3", "...--"},  {"4", "....-"},
        {"5", "....."},  {"6", "-...."},  {"7", "--..."},  {"8", "---.."},  {"9", "----."},
        {".", ".-.-.-"}, {",", "--..--"}, {"?", "..--.."}, {"/", "-..-."},  {"=", "-...-"},
        {"+", ".-.-."},  {"-", "-....-"}, {"'", ".----."}, {"(", "-.--."},  {")", "-.--.-"},
        {":", "---..."}, {"@", ".--.-."}, {"\"", ".-..-."},
        {"<SK>", "...-.-"}, {"<KA>", "-.-.-"}, {"<AS>", ".-..."}, {"<SOS>", "...---..."},
        {"<HH>", "........"},
    };
    return t;
}

const std::map<std::string, std::string> &reverseTable()
{
    static const std::map<std::string, std::string> r = [] {
        std::map<std::string, std::string> m;
        for (const auto &[sym, pat] : table())
            m.emplace(pat, sym);
        return m;
    }();
    return r;
}

} // namespace

std::string encode(const std::string &symbol)
{
    std::string up = symbol;
    std::transform(up.begin(), up.end(), up.begin(), [](unsigned char c) { return std::toupper(c); });
    auto it = table().find(up);
    return it == table().end() ? std::string() : it->second;
}

std::string decode(const std::string &pattern)
{
    auto it = reverseTable().find(pattern);
    return it == reverseTable().end() ? std::string() : it->second;
}

std::vector<std::string> symbols(const std::string &text)
{
    std::vector<std::string> out;
    for (size_t i = 0; i < text.size(); ++i) {
        unsigned char c = text[i];
        if (std::isspace(c)) {
            if (!out.empty() && out.back() != " ")
                out.emplace_back(" ");
            continue;
        }
        if (c == '<') {
            size_t end = text.find('>', i);
            if (end != std::string::npos) {
                std::string ps = text.substr(i, end - i + 1);
                std::transform(ps.begin(), ps.end(), ps.begin(), [](unsigned char ch) { return std::toupper(ch); });
                out.push_back(ps);
                i = end;
                continue;
            }
        }
        out.emplace_back(1, static_cast<char>(std::toupper(c)));
    }
    while (!out.empty() && out.back() == " ")
        out.pop_back();
    return out;
}

const std::vector<std::string> &kochOrder()
{
    static const std::vector<std::string> order = {
        "K", "M", "U", "R", "E", "S", "N", "A", "P", "T", "L", "W", "I", ".", "J", "Z", "=", "F", "O", "Y",
        ",", "V", "G", "5", "/", "Q", "9", "2", "H", "3", "8", "B", "?", "4", "7", "C", "1", "D", "6", "0",
        "X",
    };
    return order;
}

std::vector<Segment> timing(const std::string &text, double charWpm, double effWpm)
{
    charWpm = std::max(charWpm, 1.0);
    effWpm = std::clamp(effWpm, 1.0, charWpm);
    const double unit = 1200.0 / charWpm;

    // ARRL Farnsworth: total extra delay per PARIS word spread over 19 gap units.
    double charGap = 3 * unit;
    double wordGap = 7 * unit;
    if (effWpm < charWpm) {
        const double ta = (60.0 * charWpm - 37.2 * effWpm) / (effWpm * charWpm) * 1000.0; // ms
        charGap = 3.0 * ta / 19.0;
        wordGap = 7.0 * ta / 19.0;
    }

    std::vector<Segment> seq;
    auto addGap = [&seq](double ms) {
        if (seq.empty())
            return;
        if (!seq.back().on)
            seq.back().ms = std::max(seq.back().ms, ms);
        else
            seq.push_back({false, ms});
    };

    for (const auto &sym : symbols(text)) {
        if (sym == " ") {
            addGap(wordGap);
            continue;
        }
        const std::string pat = encode(sym);
        if (pat.empty())
            continue;
        addGap(charGap);
        for (size_t i = 0; i < pat.size(); ++i) {
            if (i > 0)
                seq.push_back({false, unit});
            seq.push_back({true, pat[i] == '.' ? unit : 3 * unit});
        }
    }
    return seq;
}

int editDistance(const std::vector<std::string> &a, const std::vector<std::string> &b)
{
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j)
        prev[j] = static_cast<int>(j);
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = static_cast<int>(i);
        for (size_t j = 1; j <= b.size(); ++j) {
            const int sub = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, sub});
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

} // namespace morse
