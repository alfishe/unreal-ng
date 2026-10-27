#include "unrealasm/encoding.h"

#include <algorithm>
#include <cmath>

#include "encoding/utf8.h"

namespace unrealasm::encoding
{
namespace
{
/// Russian letter frequencies in percent, by the lower-case letter's offset from U+0430 (а...я), ё separately
constexpr double kRussianFrequency[32] = {
    8.01, 1.59, 4.54, 1.70, 2.98, 8.45, 0.94, 1.65,   // а б в г д е ж з
    7.35, 1.21, 3.49, 4.40, 3.21, 6.70, 10.97, 2.81,  // и й к л м н о п
    4.73, 5.47, 6.26, 2.62, 0.26, 0.97, 0.48, 1.44,   // р с т у ф х ц ч
    0.73, 0.36, 0.04, 1.90, 1.74, 0.32, 0.64, 2.01,   // ш щ ъ ы ь э ю я
};
constexpr double kYoFrequency = 0.04;

struct PageQuality
{
    CodePage codePage;
    double quality = 0.0;   // 0..1: letters of the high half x how Russian their frequencies look
};

/// Bits per letter a letter gains over a uniform choice of 32: log2(p * 32). Real Russian text averages about +0.5;
/// a wrong decoding (KOI8-R read as CP1251 and the like) averages below -0.3
double LetterBits(double percent)
{
    return std::log2(percent / 100.0 * 32.0);
}

PageQuality Evaluate(std::span<const uint8_t> bytes, CodePage codePage, size_t highCount)
{
    size_t letters = 0;
    double bitsSum = 0.0;
    for (uint8_t b : bytes)
    {
        if (b < 0x80)
            continue;
        char32_t cp = ByteToCodePoint(b, codePage);
        if (cp >= 0x0410 && cp <= 0x042F)
            cp += 0x20;   // upper -> lower
        if (cp == 0x0401)
            cp = 0x0451;
        if (cp >= 0x0430 && cp <= 0x044F)
        {
            ++letters;
            bitsSum += LetterBits(kRussianFrequency[cp - 0x0430]);
        }
        else if (cp == 0x0451)
        {
            ++letters;
            bitsSum += LetterBits(kYoFrequency);
        }
    }
    PageQuality result{codePage, 0.0};
    if (letters == 0 || highCount == 0)
        return result;
    const double letterRatio = static_cast<double>(letters) / static_cast<double>(highCount);
    const double meanBits = bitsSum / static_cast<double>(letters);
    result.quality = letterRatio * std::clamp((meanBits + 1.0) / 1.5, 0.0, 1.0);
    return result;
}
}  // namespace

std::vector<CodePageGuess> CodePageDetector::Rank(std::span<const uint8_t> bytes) const
{
    size_t highCount = 0;
    for (uint8_t b : bytes)
        highCount += b >= 0x80 ? 1 : 0;
    if (highCount == 0)
        return {{CodePage::Ascii, 100}, {CodePage::Utf8, 90}};

    // UTF-8: every high byte belongs to a valid multi-byte sequence
    size_t sequences = 0;
    size_t invalid = 0;
    for (size_t i = 0; i < bytes.size();)
    {
        char32_t cp = 0;
        const size_t length = utf8::DecodeOne(bytes.subspan(i), cp);
        if (length == 0)
        {
            ++invalid;
            ++i;
            continue;
        }
        sequences += length > 1 ? 1 : 0;
        i += length;
    }
    std::vector<CodePageGuess> guesses;
    if (invalid == 0 && sequences > 0)
    {
        guesses.push_back({CodePage::Utf8, sequences >= 3 ? 98 : 75});
        for (CodePage page : {CodePage::Cp1251, CodePage::Koi8r, CodePage::Cp866})
            guesses.push_back({page, 5});
        return guesses;
    }

    // Single-byte Cyrillic pages: the best Russian-looking decoding wins; the lead over the runner-up and the amount
    // of evidence set the confidence
    std::vector<PageQuality> qualities = {Evaluate(bytes, CodePage::Cp866, highCount),
                                          Evaluate(bytes, CodePage::Koi8r, highCount),
                                          Evaluate(bytes, CodePage::Cp1251, highCount)};
    std::sort(qualities.begin(), qualities.end(), [](const PageQuality& a, const PageQuality& b) { return a.quality > b.quality; });
    const double best = qualities[0].quality;
    const double second = qualities[1].quality;
    const double evidence = std::min(1.0, static_cast<double>(highCount) / 32.0);
    if (best <= 0.0)
    {
        // High bytes but no letters in any page: not Cyrillic text (pseudo-graphics, binary)
        for (const PageQuality& q : qualities)
            guesses.push_back({q.codePage, 10});
        guesses.push_back({CodePage::Utf8, 0});
        return guesses;
    }
    // The closer the runner-up, the lower the confidence: KOI8-R and CP1251 both decode to letters and differ only in
    // how Russian the letter frequencies look
    const double lead = (best - second) / best;
    const int bestConfidence = static_cast<int>(std::lround(100.0 * evidence * (0.4 + 0.6 * lead) * best));
    for (const PageQuality& q : qualities)
    {
        const int confidence = &q == &qualities[0] ? bestConfidence
                                                   : static_cast<int>(std::lround(bestConfidence * q.quality / best * (1.0 - lead)));
        guesses.push_back({q.codePage, std::clamp(confidence, 0, 100)});
    }
    guesses.push_back({CodePage::Utf8, 0});
    return guesses;
}

CodePageGuess CodePageDetector::Best(std::span<const uint8_t> bytes) const
{
    const std::vector<CodePageGuess> ranked = Rank(bytes);
    return ranked.empty() ? CodePageGuess{CodePage::Ascii, 100} : ranked.front();
}

LineEnd LineEndDetector::Detect(std::span<const uint8_t> bytes) const
{
    size_t lf = 0, crlf = 0, cr = 0;
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        if (bytes[i] == '\r')
        {
            if (i + 1 < bytes.size() && bytes[i + 1] == '\n')
            {
                ++crlf;
                ++i;
            }
            else
                ++cr;
        }
        else if (bytes[i] == '\n')
            ++lf;
    }
    const int kinds = (lf ? 1 : 0) + (crlf ? 1 : 0) + (cr ? 1 : 0);
    if (kinds == 0)
        return LineEnd::None;
    if (kinds > 1)
        return LineEnd::Mixed;
    return lf ? LineEnd::Lf : crlf ? LineEnd::CrLf : LineEnd::Cr;
}

int TextBinaryDetector::TextScore(std::span<const uint8_t> bytes) const
{
    if (bytes.empty())
        return 100;
    size_t bad = 0;
    for (uint8_t b : bytes)
    {
        if (b == 0)
            bad += 4;
        else if (b < 0x20 && b != '\t' && b != '\n' && b != '\r' && b != '\f' && b != 0x1A && b != 0x1B)
            ++bad;
    }
    const double ratio = static_cast<double>(bad) / static_cast<double>(bytes.size());
    return std::clamp(static_cast<int>(std::lround(100.0 - ratio * 600.0)), 0, 100);
}
}  // namespace unrealasm::encoding
