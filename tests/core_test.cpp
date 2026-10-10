// Unit tests for the Morse core: table, timing, decoder, scoring.
#include "decoder.h"
#include "morse.h"
#include "trainer.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <string>

static int failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
            ++failures;                                                          \
        }                                                                        \
    } while (0)
#define CHECK_EQ(a, b)                                                           \
    do {                                                                         \
        auto _a = (a);                                                           \
        auto _b = (b);                                                           \
        if (!(_a == _b)) {                                                       \
            std::printf("FAIL %s:%d  %s == %s  ('%s' vs '%s')\n", __FILE__,      \
                        __LINE__, #a, #b, std::string(_a).c_str(),               \
                        std::string(_b).c_str());                                \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

// Plays text into the decoder using morse::timing, optional jitter (fraction of
// each segment) and a speed factor applied to the human's keying.
static std::string keyText(Decoder &d, const std::string &text, double wpm, double effWpm,
                           double jitter = 0.0, unsigned seed = 1)
{
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> j(1.0 - jitter, 1.0 + jitter);
    std::string out;
    int64_t t = 1'000'000;
    for (const auto &seg : morse::timing(text, wpm, effWpm)) {
        const int64_t dur = static_cast<int64_t>(seg.ms * 1000.0 * j(rng));
        if (seg.on) {
            out += d.keyDown(t);
            t += dur;
            out += d.keyUp(t);
        } else {
            // Tick through the silence like the UI timer does (every 5 ms).
            for (int64_t s = 5000; s < dur; s += 5000)
                out += d.tick(t + s);
            t += dur;
        }
    }
    for (int i = 1; i < 400; ++i)
        out += d.tick(t + i * 10000);
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

int main()
{
    // Table
    CHECK_EQ(morse::encode("a"), std::string(".-"));
    CHECK_EQ(morse::encode("<sk>"), std::string("...-.-"));
    CHECK_EQ(morse::decode("--.."), std::string("Z"));
    CHECK_EQ(morse::decode("......."), std::string(""));
    CHECK(morse::kochOrder().size() == 41);
    for (const auto &s : morse::kochOrder())
        CHECK(!morse::encode(s).empty());

    // Symbols
    auto syms = morse::symbols(" cq  de <SK> ");
    CHECK(syms.size() == 7);
    CHECK_EQ(syms[5], std::string(" "));
    CHECK_EQ(syms[6], std::string("<SK>"));

    // Timing: PARIS at 20 WPM is exactly 50 units = 3000 ms incl. trailing word gap.
    {
        double total = 0;
        for (auto &s : morse::timing("PARIS", 20, 20))
            total += s.ms;
        CHECK(std::fabs(total + 7 * 60 - 3000) < 1e-6);
        // Farnsworth: 20/10 -> one PARIS word incl. word gap takes 6 s.
        double fw = 0;
        auto seq = morse::timing("PARIS PARIS", 20, 10);
        int word = 0;
        for (auto &s : seq) {
            if (!s.on && s.ms > 1000) { word = 1; fw += s.ms; break; }
            fw += s.ms;
        }
        CHECK(word == 1);
        CHECK(std::fabs(fw - 6000) < 1.0);
    }

    // Decoder: perfect timing at the seeded speed.
    {
        Decoder d(20);
        CHECK_EQ(keyText(d, "CQ CQ DE OK2ABC", 20, 20), std::string("CQ CQ DE OK2ABC"));
    }
    // Decoder must adapt when the operator is much faster than the seed...
    {
        Decoder d(12);
        std::string got = keyText(d, "MMM PARIS PARIS TEST", 30, 30);
        CHECK_EQ(got.substr(got.size() - 10), std::string("PARIS TEST"));
        CHECK(std::fabs(d.wpm() - 30) < 3);
    }
    // ...and much slower.
    {
        Decoder d(25);
        std::string got = keyText(d, "TTT PARIS PARIS TEST", 8, 8);
        CHECK_EQ(got.substr(got.size() - 10), std::string("PARIS TEST"));
        CHECK(std::fabs(d.wpm() - 8) < 1);
    }
    // Human-ish jitter (+-20 %), normal spacing.
    for (unsigned seed = 1; seed <= 20; ++seed) {
        Decoder d(18);
        CHECK_EQ(keyText(d, "THE QUICK BROWN FOX 73", 18, 18, 0.20, seed),
                 std::string("THE QUICK BROWN FOX 73"));
    }
    // Farnsworth spacing (letters at 18, overall 10): once a few letter gaps are
    // known, long letter gaps must not be mistaken for word spaces.
    for (unsigned seed = 1; seed <= 20; ++seed) {
        Decoder d(18);
        std::string got = keyText(d, "VVV VVV THE QUICK BROWN FOX 73", 18, 10, 0.20, seed);
        const std::string want = "THE QUICK BROWN FOX 73";
        CHECK_EQ(got.substr(got.size() - want.size()), want);
    }
    // Contact bounce is ignored.
    {
        Decoder d(20);
        std::string out;
        out += d.keyDown(0);
        out += d.keyUp(3000);   // 3 ms glitch
        out += d.keyDown(100000);
        out += d.keyUp(160000); // one dit
        for (int i = 1; i < 100; ++i)
            out += d.tick(160000 + i * 10000);
        CHECK_EQ(out, std::string("E "));
    }
    // Stats
    {
        Decoder d(20);
        keyText(d, "PARIS", 20, 20);
        CHECK(std::fabs(d.stats().ratio() - 3.0) < 0.05);
        CHECK(std::fabs(d.stats().weight() - 1.0) < 0.05);
    }

    // Scoring
    CHECK(morse::editDistance(morse::symbols("KMURE"), morse::symbols("KMURE")) == 0);
    CHECK(morse::editDistance(morse::symbols("KMURE"), morse::symbols("KMRE")) == 1);
    CHECK(morse::editDistance(morse::symbols("KMURE"), morse::symbols("KNURS")) == 2);

    // Trainer
    {
        Trainer tr;
        tr.setLevel(2);
        for (int i = 0; i < 50; ++i)
            for (const auto &s : morse::symbols(tr.group(5)))
                CHECK(s == "K" || s == "M");
        tr.setLevel(99);
        CHECK(tr.level() == 41);
        tr.setLevel(5);
        CHECK_EQ(tr.newestChar(), std::string("E"));

        auto r = tr.grade("KMURE", "KMURE");
        CHECK(r.correct() == 5 && r.distance == 0);
        r = tr.grade("KMU RE", "KMRE");          // U missing, spaces ignored
        CHECK(r.correct() == 4 && !r.ok[2] && r.distance == 1);
        r = tr.grade("KMURE", "KMXURE");         // insertion: all matched, still penalised
        CHECK(r.correct() == 5 && r.distance == 1);
        CHECK(r.accuracy() < 1.0);
        r = tr.grade("KK", "");
        CHECK(r.correct() == 0);
        auto weak = tr.weakest(3);
        CHECK(!weak.empty());

        Trainer t2;
        for (int i = 0; i < 12; ++i)
            t2.grade("KMKMK", "KMKMK");
        CHECK(t2.readyToAdvance());
        t2.grade("KMKMKKMKMK", "");
        CHECK(!t2.readyToAdvance());
    }

    if (failures == 0)
        std::printf("All core tests passed\n");
    return failures == 0 ? 0 : 1;
}
