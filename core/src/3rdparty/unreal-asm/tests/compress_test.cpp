// rasm's crunchers (unrealasm/compress.h): byte-exact against rasm 3.3.x on the fixtures of testdata/compress
// (manifest.tsv: input, algorithm, section or include, what rasm emitted), written by
// tools/verification/unreal-asm/checks/crunchcheck.py from rasm itself. UNREAL_ASM_CRUNCH_FIXTURES points the
// CrunchFixtures tests at another set of the same layout (crunchcheck.py --fuzz). The long inputs (the 3K text, 4K of
// zeros, the 20K ROM, the 29K lzDataTest.bin, the 100K generated input) run with UNREAL_LONG_TESTS=1: the optimal
// parsers take up to seconds on them.

#include "unrealasm/compress.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace unrealasm::compress;

namespace
{
std::string FixtureDir()
{
    if (const char* dir = std::getenv("UNREAL_ASM_CRUNCH_FIXTURES"); dir != nullptr && *dir != '\0')
    {
        return dir;
    }
    return std::string(UNREAL_ASM_TESTDATA_DIR) + "/compress";
}

bool LongTests()
{
    const char* value = std::getenv("UNREAL_LONG_TESTS");
    return value != nullptr && std::string(value) == "1";
}

std::vector<uint8_t> ReadFile(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// crunchcheck.py GENERATED: the inputs the fixtures do not store
std::vector<uint8_t> GeneratedInput(const std::string& name)
{
    if (name == "@big")
    {
        const std::string unit = "Hello big world 0123456789 ";
        std::vector<uint8_t> out;
        while (out.size() < 100000)
        {
            out.insert(out.end(), unit.begin(), unit.end());
        }
        out.resize(100000);
        return out;
    }
    return {};
}

std::vector<uint8_t> Input(const std::string& name)
{
    return name[0] == '@' ? GeneratedInput(name) : ReadFile(FixtureDir() + "/inputs/" + name);
}

// zlib's CRC-32 (crunchcheck.py stores the outputs of the long inputs as "crc32:<hex>:<size>")
uint32_t Crc32(const std::vector<uint8_t>& data)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint8_t byte : data)
    {
        crc ^= byte;
        for (int bit = 0; bit < 8; bit++)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

std::string Fingerprint(const std::vector<uint8_t>& data)
{
    char text[48];
    std::snprintf(text, sizeof(text), "crc32:%08x:%zu", Crc32(data), data.size());
    return text;
}

bool IsHashed(const std::string& file)
{
    return file.rfind("crc32:", 0) == 0;
}

bool IsLong(const std::string& name)
{
    // text.bin and zeros4k.bin cost salvador, apultra, exomizer and ZX7 up to a second each (optimal parsers, long
    // runs); the short tier keeps empty, one, seventeen and random.bin, which reach every outcome
    return name == "@big" || name == "rom20k.bin" || name == "lzdatatest.bin" || name == "text.bin" ||
           name == "zeros4k.bin";
}

struct Case
{
    std::string input;
    std::string algorithm;
    std::string origin;   // section, incbin, section/minmatch=N
    std::string outcome;  // crunched, raw, reversed, empty, error, crash
    std::string file;
};

std::vector<Case> Manifest()
{
    std::vector<Case> cases;
    std::ifstream in(FixtureDir() + "/manifest.tsv");
    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }
        std::istringstream fields(line);
        Case c;
        std::getline(fields, c.input, '\t');
        std::getline(fields, c.algorithm, '\t');
        std::getline(fields, c.origin, '\t');
        std::getline(fields, c.outcome, '\t');
        std::getline(fields, c.file, '\t');
        cases.push_back(c);
    }
    return cases;
}

const char* Name(Algorithm algorithm)
{
    switch (algorithm)
    {
        case Algorithm::Lz4:
            return "Lz4";
        case Algorithm::Lz48:
            return "Lz48";
        case Algorithm::Lz49:
            return "Lz49";
        case Algorithm::Zx7:
            return "Zx7";
        case Algorithm::Zx0:
            return "Zx0";
        case Algorithm::Zx0Backward:
            return "Zx0Backward";
        case Algorithm::Exomizer:
            return "Exomizer";
        case Algorithm::ApUltra:
            return "ApUltra";
        case Algorithm::Lzsa1:
            return "Lzsa1";
        case Algorithm::Lzsa2:
            return "Lzsa2";
    }
    return "";
}

Options OptionsOf(const Case& c)
{
    Options options;
    options.origin = c.origin.rfind("incbin", 0) == 0 ? Origin::Incbin : Origin::Section;
    const size_t at = c.origin.find("minmatch=");
    if (at != std::string::npos)
    {
        options.lzsaMinMatch = std::atoi(c.origin.c_str() + at + 9);
    }
    return options;
}

// Every manifest row of an algorithm: what Crunch gives against what rasm emitted
void CheckAgainstRasm(Algorithm algorithm)
{
    const std::vector<Case> cases = Manifest();
    ASSERT_FALSE(cases.empty()) << "no manifest in " << FixtureDir();
    int checked = 0;
    for (const Case& c : cases)
    {
        if (c.algorithm != Name(algorithm) || (IsLong(c.input) && !LongTests()))
        {
            continue;
        }
        SCOPED_TRACE(c.input + " " + c.algorithm + " " + c.origin + " (rasm: " + c.outcome + ")");
        const std::vector<uint8_t> input = Input(c.input);
        const Result result = Crunch(algorithm, input.data(), input.size(), OptionsOf(c));
        checked++;
        if (c.outcome == "crunched" && IsHashed(c.file))
        {
            EXPECT_TRUE(result.error.empty()) << result.error;
            EXPECT_TRUE(result.crunched);
            EXPECT_EQ(Fingerprint(result.bytes), c.file) << "the bytes differ from rasm's";
        }
        else if (c.outcome == "crunched")
        {
            const std::vector<uint8_t> expected = ReadFile(FixtureDir() + "/expected/" + c.file);
            ASSERT_FALSE(expected.empty()) << c.file;
            EXPECT_TRUE(result.error.empty()) << result.error;
            EXPECT_TRUE(result.crunched);
            EXPECT_EQ(result.bytes.size(), expected.size());
            EXPECT_TRUE(result.bytes == expected) << "the bytes differ from rasm's";
        }
        else if (c.outcome == "raw" || c.outcome == "reversed")
        {
            std::vector<uint8_t> expected = input;
            if (c.outcome == "reversed")
            {
                std::reverse(expected.begin(), expected.end());
            }
            EXPECT_TRUE(result.error.empty()) << result.error;
            EXPECT_FALSE(result.crunched);
            EXPECT_FALSE(result.warning.empty());
            EXPECT_TRUE(result.bytes == expected);
        }
        else if (c.outcome == "empty")
        {
            EXPECT_TRUE(result.error.empty()) << result.error;
            EXPECT_FALSE(result.crunched);
            EXPECT_TRUE(result.bytes.empty());
            EXPECT_FALSE(result.warning.empty());
        }
        else
        {
            EXPECT_FALSE(result.error.empty()) << "rasm fails here (" << c.outcome << ")";
            EXPECT_TRUE(result.bytes.empty());
        }
    }
    EXPECT_GT(checked, 0);
}

// Every crunched fixture decrunched gives its input back (the decoders checked on rasm's own bytes)
void CheckRoundTrip(Algorithm algorithm)
{
    int checked = 0;
    for (const Case& c : Manifest())
    {
        if (c.algorithm != Name(algorithm) || c.outcome != "crunched" || (IsLong(c.input) && !LongTests()))
        {
            continue;
        }
        SCOPED_TRACE(c.input + " " + c.algorithm + " " + c.origin);
        // A hashed output is rebuilt (CrunchFixtures checks it against rasm's fingerprint)
        const std::vector<uint8_t> input = Input(c.input);
        const std::vector<uint8_t> crunched = IsHashed(c.file)
                                                  ? Crunch(algorithm, input.data(), input.size(), OptionsOf(c)).bytes
                                                  : ReadFile(FixtureDir() + "/expected/" + c.file);
        const Result result = Decrunch(algorithm, crunched.data(), crunched.size());
        ASSERT_TRUE(result.error.empty()) << result.error;
        EXPECT_TRUE(result.bytes == input) << "decrunched " << result.bytes.size() << " bytes";
        checked++;
    }
    // The cruncher's own bytes of every short input, crunched or not by rasm (Exomizer crunches none of them)
    for (const char* name : {"one.bin", "seventeen.bin", "random.bin"})
    {
        SCOPED_TRACE(std::string(name) + " " + Name(algorithm) + " raw");
        const std::vector<uint8_t> input = Input(name);
        if (input.empty())
        {
            continue;  // another fixture set (UNREAL_ASM_CRUNCH_FIXTURES)
        }
        const Result raw = CrunchRaw(algorithm, input.data(), input.size());
        ASSERT_TRUE(raw.error.empty()) << raw.error;
        const Result back = Decrunch(algorithm, raw.bytes.data(), raw.bytes.size());
        ASSERT_TRUE(back.error.empty()) << back.error;
        EXPECT_TRUE(back.bytes == input) << "decrunched " << back.bytes.size() << " bytes";
        checked++;
    }
    EXPECT_GT(checked, 0);
}
}  // namespace

#define UNREAL_ASM_CRUNCH_TESTS(algorithm)      \
    TEST(CrunchFixtures, algorithm)             \
    {                                           \
        CheckAgainstRasm(Algorithm::algorithm); \
    }                                           \
    TEST(CrunchRoundTrip, algorithm)            \
    {                                           \
        CheckRoundTrip(Algorithm::algorithm);   \
    }

UNREAL_ASM_CRUNCH_TESTS(Lz4)
UNREAL_ASM_CRUNCH_TESTS(Lz48)
UNREAL_ASM_CRUNCH_TESTS(Lz49)
UNREAL_ASM_CRUNCH_TESTS(Zx7)
UNREAL_ASM_CRUNCH_TESTS(Zx0)
UNREAL_ASM_CRUNCH_TESTS(Zx0Backward)
UNREAL_ASM_CRUNCH_TESTS(Exomizer)
UNREAL_ASM_CRUNCH_TESTS(ApUltra)
UNREAL_ASM_CRUNCH_TESTS(Lzsa1)
UNREAL_ASM_CRUNCH_TESTS(Lzsa2)

TEST(Crunch, DirectiveNames)
{
    EXPECT_EQ(AlgorithmFromRasmDirective("LZ4"), Algorithm::Lz4);
    EXPECT_EQ(AlgorithmFromRasmDirective("inclz4"), Algorithm::Lz4);
    EXPECT_EQ(AlgorithmFromRasmDirective("INCL48"), Algorithm::Lz48);
    EXPECT_EQ(AlgorithmFromRasmDirective("lz49"), Algorithm::Lz49);
    EXPECT_EQ(AlgorithmFromRasmDirective("LZX7"), Algorithm::Zx7);
    EXPECT_EQ(AlgorithmFromRasmDirective("INCZX0"), Algorithm::Zx0);
    EXPECT_EQ(AlgorithmFromRasmDirective("LZX0B"), Algorithm::Zx0Backward);
    EXPECT_EQ(AlgorithmFromRasmDirective("IncZx0B"), Algorithm::Zx0Backward);
    EXPECT_EQ(AlgorithmFromRasmDirective("INCEXO"), Algorithm::Exomizer);
    EXPECT_EQ(AlgorithmFromRasmDirective("LZAPU"), Algorithm::ApUltra);
    EXPECT_EQ(AlgorithmFromRasmDirective("INCLZSA1"), Algorithm::Lzsa1);
    EXPECT_EQ(AlgorithmFromRasmDirective("LZSA2"), Algorithm::Lzsa2);
    EXPECT_FALSE(AlgorithmFromRasmDirective("INCBIN").has_value());
    EXPECT_FALSE(AlgorithmFromRasmDirective("LZCLOSE").has_value());
    EXPECT_FALSE(AlgorithmFromRasmDirective("INCL4").has_value());
    EXPECT_FALSE(AlgorithmFromRasmDirective("").has_value());
    EXPECT_EQ(RasmSectionDirective(Algorithm::Zx0Backward), "LZX0B");
    EXPECT_EQ(RasmIncludeDirective(Algorithm::Lz48), "INCL48");
    EXPECT_EQ(RasmIncludeDirective(Algorithm::Exomizer), "INCEXO");
}

TEST(Crunch, RawCruncherOutputOfTinyInput)
{
    // rasm never emits these (a crunch that grows is dropped), CrunchRaw gives the cruncher's own bytes
    const uint8_t one = 'A';
    EXPECT_EQ(CrunchRaw(Algorithm::Lz48, &one, 1).bytes, (std::vector<uint8_t>{'A', 0x00, 0xFF}));
    EXPECT_EQ(CrunchRaw(Algorithm::Lz49, &one, 1).bytes, (std::vector<uint8_t>{'A', 0x00, 0xFF}));
    const Result section = Crunch(Algorithm::Lz48, &one, 1);
    EXPECT_FALSE(section.crunched);
    EXPECT_EQ(section.bytes, (std::vector<uint8_t>{'A'}));
    EXPECT_FALSE(section.warning.empty());
    Options incbin;
    incbin.origin = Origin::Incbin;
    EXPECT_FALSE(Crunch(Algorithm::Lz48, &one, 1, incbin).error.empty()) << "INCL48 of 1 byte: Cannot crunch";
    EXPECT_FALSE(CrunchRaw(Algorithm::Zx0, nullptr, 0).error.empty());
    for (Algorithm algorithm :
         {Algorithm::Lz4, Algorithm::Lz48, Algorithm::Lz49, Algorithm::Zx7, Algorithm::Zx0, Algorithm::Zx0Backward,
          Algorithm::Exomizer, Algorithm::ApUltra, Algorithm::Lzsa1, Algorithm::Lzsa2})
    {
        SCOPED_TRACE(Name(algorithm));
        const Result raw = CrunchRaw(algorithm, &one, 1);
        ASSERT_TRUE(raw.error.empty()) << raw.error;
        const Result back = Decrunch(algorithm, raw.bytes.data(), raw.bytes.size());
        ASSERT_TRUE(back.error.empty()) << back.error;
        EXPECT_EQ(back.bytes, (std::vector<uint8_t>{'A'}));
    }
}

TEST(Crunch, Lz49OffsetsAroundTheHighBit)
{
    // 0..255 once, then matches at offsets 256 (stored 0xFF with the token's bit 7: (0 - 1) in 8 bits) and 257
    std::vector<uint8_t> input(256);
    for (size_t i = 0; i < input.size(); i++)
    {
        input[i] = static_cast<uint8_t>(i);
    }
    input.insert(input.end(), {0, 1, 2});
    input.insert(input.end(), {2, 3, 4, 5});
    for (Algorithm algorithm : {Algorithm::Lz48, Algorithm::Lz49})
    {
        SCOPED_TRACE(Name(algorithm));
        const Result raw = CrunchRaw(algorithm, input.data(), input.size());
        ASSERT_TRUE(raw.error.empty()) << raw.error;
        const Result back = Decrunch(algorithm, raw.bytes.data(), raw.bytes.size());
        ASSERT_TRUE(back.error.empty()) << back.error;
        EXPECT_TRUE(back.bytes == input);
    }
}

TEST(Crunch, DamagedDataIsAnErrorNotACrash)
{
    // Every cut of a crunched block and a byte of it changed: the decoders stop inside their buffers (apultra's own
    // size probe never ended on a cut block, lzsa refused its own LZSA1 end marker below 256 bytes out)
    std::vector<uint8_t> text = ReadFile(FixtureDir() + "/inputs/text.bin");
    text.resize(std::min<size_t>(text.size(), 512));
    for (Algorithm algorithm :
         {Algorithm::Lz4, Algorithm::Lz48, Algorithm::Lz49, Algorithm::Zx7, Algorithm::Zx0, Algorithm::Zx0Backward,
          Algorithm::Exomizer, Algorithm::ApUltra, Algorithm::Lzsa1, Algorithm::Lzsa2})
    {
        SCOPED_TRACE(Name(algorithm));
        const Result raw = CrunchRaw(algorithm, text.data(), text.size());
        ASSERT_TRUE(raw.error.empty()) << raw.error;
        const Result whole = Decrunch(algorithm, raw.bytes.data(), raw.bytes.size());
        EXPECT_EQ(whole.bytes, text);
        // A cut may still decode (an LZSA block without its end marker ends where the data ends): it must only end
        for (size_t length = 1; length < raw.bytes.size(); length++)
        {
            const Result cut = Decrunch(algorithm, raw.bytes.data(), length);
            EXPECT_TRUE(cut.error.empty() || cut.bytes.empty());
        }
        for (size_t at = 0; at < raw.bytes.size(); at += 7)
        {
            std::vector<uint8_t> damaged = raw.bytes;
            damaged[at] ^= 0x5A;
            const Result result = Decrunch(algorithm, damaged.data(), damaged.size());
            EXPECT_TRUE(result.error.empty() || result.bytes.empty()) << "an error carries no bytes";
        }
    }
}

TEST(Crunch, ThreadSafe)
{
    // Exomizer keeps static state (serialised inside); the others are reentrant
    std::vector<uint8_t> text = ReadFile(FixtureDir() + "/inputs/text.bin");
    text.resize(std::min<size_t>(text.size(), 768));
    const std::vector<Algorithm> algorithms{Algorithm::Exomizer, Algorithm::Zx7,   Algorithm::Zx0,
                                            Algorithm::ApUltra,  Algorithm::Lzsa2, Algorithm::Lz4};
    std::vector<std::vector<uint8_t>> expected;
    for (Algorithm algorithm : algorithms)
    {
        expected.push_back(CrunchRaw(algorithm, text.data(), text.size()).bytes);
    }
    std::vector<int> mismatches(algorithms.size() * 2, 0);
    std::vector<std::thread> threads;
    for (size_t t = 0; t < mismatches.size(); t++)
    {
        threads.emplace_back([&, t] {
            const size_t k = t % algorithms.size();
            for (int round = 0; round < 2; round++)
            {
                if (CrunchRaw(algorithms[k], text.data(), text.size()).bytes != expected[k])
                {
                    mismatches[t]++;
                }
            }
        });
    }
    for (std::thread& thread : threads)
    {
        thread.join();
    }
    for (size_t t = 0; t < mismatches.size(); t++)
    {
        EXPECT_EQ(mismatches[t], 0) << Name(algorithms[t % algorithms.size()]);
    }
}
