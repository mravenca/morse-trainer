#pragma once
// Morse code table, text <-> timing helpers and scoring. Plain C++, no Qt.
#include <string>
#include <vector>

namespace morse {

// Pattern for one character (".-" for A), empty if not encodable.
// Prosigns are written in angle brackets: "<SK>", "<KA>", "<AS>".
std::string encode(const std::string &symbol);

// Symbol for a pattern ("A", "<SK>"), empty if unknown.
std::string decode(const std::string &pattern);

// Splits text into symbols (upper-cased; "<SK>" kept as one symbol, spaces kept as " ").
std::vector<std::string> symbols(const std::string &text);

// Koch method character order (LCWO order).
const std::vector<std::string> &kochOrder();

// One keying segment of a played sequence.
struct Segment {
    bool on;
    double ms;
};

// Builds the on/off keying sequence for text.
// charWpm: speed of the characters themselves (PARIS timing, dit = 1200/wpm ms).
// effWpm:  overall speed; if lower than charWpm, gaps are stretched (ARRL Farnsworth).
std::vector<Segment> timing(const std::string &text, double charWpm, double effWpm);

// Levenshtein distance between two symbol sequences.
int editDistance(const std::vector<std::string> &a, const std::vector<std::string> &b);

} // namespace morse
