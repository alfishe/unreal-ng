#include "pch.h"

#include "common/inifile.h"

#include "_helpers/testpathhelper.h"

#include <cstdint>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

/// Fuzzing coverage for IniFile, mirroring this project's established style
/// (see LoaderZ80_Fuzzing_Test): a fixed seed for reproducible failures, a
/// spread of input sizes, and "must not crash / must not throw" as the
/// baseline assertion. IniFile::LoadData() never fails a call (malformed
/// input is simply skipped per its documented contract), so there is no
/// return-code to check here - the point is exclusively to rule out crashes,
/// hangs, and UB (out-of-bounds reads, signed-char UB, etc.) on adversarial
/// or malformed byte streams, plus one property check (round-trip
/// idempotency) that goes beyond "didn't crash".

namespace
{
    /// Fixed seed for all fuzz input generation - keeps failures reproducible
    /// across runs and shards instead of depending on an unseeded random_device.
    constexpr uint32_t kFuzzSeed = 0xC0FFEE;

    std::string RandomBytes(size_t size, uint32_t seed = kFuzzSeed)
    {
        std::mt19937 gen(seed);
        std::uniform_int_distribution<int> dis(0, 255);

        std::string data;
        data.resize(size);
        for (size_t i = 0; i < size; i++)
        {
            data[i] = static_cast<char>(static_cast<uint8_t>(dis(gen)));
        }
        return data;
    }

    /// Printable ASCII plus the characters IniFile's grammar actually keys
    /// off ('[', ']', '=', ';', '#', '/', space, CR, LF) weighted in - pure
    /// random bytes rarely contain these, so they mostly exercise the "not a
    /// valid header/assignment, skip the line" early-out. This corpus drives
    /// far more of the actual parsing logic (header/assignment/comment paths).
    std::string RandomAlmostIni(size_t size, uint32_t seed = kFuzzSeed)
    {
        static constexpr char kAlphabet[] =
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
            "[]=;#/ \t\r\n\"'.-_:";
        constexpr size_t kAlphabetLen = sizeof(kAlphabet) - 1;

        std::mt19937 gen(seed);
        std::uniform_int_distribution<size_t> dis(0, kAlphabetLen - 1);

        std::string data;
        data.resize(size);
        for (size_t i = 0; i < size; i++)
        {
            data[i] = kAlphabet[dis(gen)];
        }
        return data;
    }

    /// Same idea as RandomAlmostIni, but the alphabet excludes ';', '#' and
    /// '/' - the three inline-comment trigger characters. Values built from
    /// this alphabet can never be additionally truncated by
    /// StripInlineComment on a second pass, which is what makes single-round
    /// parse/serialize idempotency a property that actually holds (see
    /// RoundTripCanLoseDataWhenSavedValueContainsACommentMarker below for the
    /// documented case where it does not).
    std::string RandomAlmostIniNoCommentMarkers(size_t size, uint32_t seed = kFuzzSeed)
    {
        static constexpr char kAlphabet[] =
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
            "[]= \t\r\n\"'.-_:";
        constexpr size_t kAlphabetLen = sizeof(kAlphabet) - 1;

        std::mt19937 gen(seed);
        std::uniform_int_distribution<size_t> dis(0, kAlphabetLen - 1);

        std::string data;
        data.resize(size);
        for (size_t i = 0; i < size; i++)
        {
            data[i] = kAlphabet[dis(gen)];
        }
        return data;
    }

    void CorruptBytes(std::string& data, size_t corruptionCount, uint32_t seed = kFuzzSeed)
    {
        if (data.empty())
        {
            return;
        }

        std::mt19937 gen(seed);
        std::uniform_int_distribution<size_t> posDis(0, data.size() - 1);
        std::uniform_int_distribution<int> byteDis(0, 255);

        for (size_t i = 0; i < corruptionCount; i++)
        {
            data[posDis(gen)] = static_cast<char>(static_cast<uint8_t>(byteDis(gen)));
        }
    }
}  // namespace

class IniFile_Fuzzing_Test : public ::testing::Test
{
};

/// region <Random byte streams: must not crash>

TEST_F(IniFile_Fuzzing_Test, RandomBytes_SmallSizes)
{
    for (size_t size : {size_t{0}, size_t{1}, size_t{2}, size_t{7}, size_t{16}, size_t{31}, size_t{100}})
    {
        IniFile ini;
        const std::string data = RandomBytes(size);

        EXPECT_NO_THROW(ini.LoadData(data)) << "size=" << size;
        // Must remain in a self-consistent, further-usable state afterward
        EXPECT_NO_THROW((void)ini.GetAllSections()) << "size=" << size;
        EXPECT_NO_THROW((void)ini.SaveData()) << "size=" << size;
    }
}

TEST_F(IniFile_Fuzzing_Test, RandomBytes_MediumSizes)
{
    for (size_t size : {size_t{500}, size_t{2000}, size_t{20000}})
    {
        IniFile ini;
        const std::string data = RandomBytes(size);

        EXPECT_NO_THROW(ini.LoadData(data)) << "size=" << size;
        EXPECT_NO_THROW((void)ini.SaveData()) << "size=" << size;
    }
}

TEST_F(IniFile_Fuzzing_Test, RandomBytes_LargeSize)
{
    // INI files are inherently tiny in this project; this is well beyond any
    // real config and is here only to rule out quadratic blowup or a crash
    // that only manifests at scale.
    IniFile ini;
    const std::string data = RandomBytes(2 * 1024 * 1024);

    EXPECT_NO_THROW(ini.LoadData(data));
}

TEST_F(IniFile_Fuzzing_Test, RandomAlmostIniText_VariousSizes)
{
    // Random bytes drawn from an alphabet weighted toward the characters
    // IniFile's grammar actually parses on - exercises header/assignment/
    // comment-stripping logic far more than pure binary garbage does.
    for (size_t size : {size_t{16}, size_t{64}, size_t{256}, size_t{4096}, size_t{50000}})
    {
        IniFile ini;
        const std::string data = RandomAlmostIni(size);

        EXPECT_NO_THROW(ini.LoadData(data)) << "size=" << size;
        EXPECT_NO_THROW((void)ini.SaveData()) << "size=" << size;
    }
}

TEST_F(IniFile_Fuzzing_Test, UnterminatedGiantSingleLine)
{
    // A large buffer with no '\n' at all is one giant "line" all the way to
    // LoadData's std::string::npos path - checks that path doesn't hang or
    // blow up on a single huge token.
    std::string data(500000, 'a');
    data += "=1";  // one assignment character buried in the middle, no newline anywhere

    IniFile ini;
    EXPECT_NO_THROW(ini.LoadData(data));
}

/// endregion </Random byte streams: must not crash>

/// region <Structural extremes>

TEST_F(IniFile_Fuzzing_Test, AllZeroBytes)
{
    // 0x00 is not '\n', not a space char, not any grammar character - the
    // whole buffer becomes one line copied into a single std::string
    // (embedded NULs and all). Must not crash.
    const std::string data(20000, '\0');

    IniFile ini;
    EXPECT_NO_THROW(ini.LoadData(data));
}

TEST_F(IniFile_Fuzzing_Test, AllOnesBytes)
{
    // 0xFF as a plain `char` is negative on platforms with signed char -
    // exercises FoldAscii/tolower's unsigned-char cast under adversarial input.
    const std::string data(20000, static_cast<char>(0xFF));

    IniFile ini;
    EXPECT_NO_THROW(ini.LoadData(data));
}

TEST_F(IniFile_Fuzzing_Test, RepeatingGrammarCharacterPatterns)
{
    // Patterns built entirely from characters the grammar treats specially -
    // adversarial in a different way than random noise: maximizes header/
    // comment/assignment state transitions per byte.
    for (const std::string& pattern : {std::string("[="), std::string("=;"), std::string("[]="),
                                        std::string("//;#"), std::string("\r\n")})
    {
        std::string data;
        data.reserve(20000);
        while (data.size() < 20000)
        {
            data += pattern;
        }

        IniFile ini;
        EXPECT_NO_THROW(ini.LoadData(data)) << "pattern=" << pattern;
    }
}

TEST_F(IniFile_Fuzzing_Test, DeeplyNestedBracketsInOneHeader)
{
    std::string data = "[";
    for (int i = 0; i < 10000; i++)
    {
        data += "[";
    }
    data += "]\nkey=value\n";

    IniFile ini;
    EXPECT_NO_THROW(ini.LoadData(data));
}

/// endregion </Structural extremes>

/// region <Corrupted real-world input>

TEST_F(IniFile_Fuzzing_Test, CorruptedShippedConfig_LightCorruption)
{
    // Take a real heritage config and flip a handful of random bytes -
    // mirrors LoaderZ80_Fuzzing_Test's corruptedValidFiles approach.
    const std::string path =
        (TestPathHelper::FindProjectRoot() / "data" / "configs" / "pentagon128k" / "unreal.ini").string();

    std::ifstream file(path, std::ios::binary);
    ASSERT_TRUE(file.is_open()) << path;
    std::string original((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    ASSERT_FALSE(original.empty());

    for (size_t corruptCount : {size_t{1}, size_t{5}, size_t{20}})
    {
        std::string corrupted = original;
        CorruptBytes(corrupted, corruptCount);

        IniFile ini;
        EXPECT_NO_THROW(ini.LoadData(corrupted)) << "corruptCount=" << corruptCount;
    }
}

TEST_F(IniFile_Fuzzing_Test, CorruptedShippedConfig_HeavyCorruption)
{
    const std::string path =
        (TestPathHelper::FindProjectRoot() / "data" / "configs" / "spectrum128" / "unreal.ini").string();

    std::ifstream file(path, std::ios::binary);
    ASSERT_TRUE(file.is_open()) << path;
    std::string original((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    ASSERT_FALSE(original.empty());

    for (double fraction : {0.1, 0.25, 0.5, 1.0})
    {
        std::string corrupted = original;
        CorruptBytes(corrupted, static_cast<size_t>(static_cast<double>(corrupted.size()) * fraction));

        IniFile ini;
        EXPECT_NO_THROW(ini.LoadData(corrupted)) << "fraction=" << fraction;
    }
}

/// endregion </Corrupted real-world input>

/// region <Property: round-trip idempotency>

TEST_F(IniFile_Fuzzing_Test, ParseSerializeIsIdempotentOnArbitraryInput)
{
    // Whatever LoadData makes of a garbage input, feeding SaveData's output
    // back through LoadData and serializing again must reproduce the exact
    // same bytes: once garbage has been folded into IniFile's canonical
    // model, that model is a fixed point of parse-then-serialize. This is a
    // stronger check than "didn't crash" - it catches asymmetries between
    // what LoadData accepts and what SaveData emits.
    //
    // Corpus is drawn from an alphabet WITHOUT ';', '#', '/' on purpose: this
    // property genuinely does not hold when a value contains one of those
    // characters (StripInlineComment re-applies on every parse, so a value
    // that survives one strip can be truncated further by the next one) -
    // that is a real, separate finding, pinned deliberately by
    // RoundTripCanLoseDataWhenSavedValueContainsACommentMarker below rather
    // than papered over here. This test verifies the much larger surface
    // that ISN'T affected by that: idempotency holds for every value made of
    // any other byte, however adversarial otherwise.
    const std::vector<std::string> corpora = {
        RandomAlmostIniNoCommentMarkers(5000),
        RandomAlmostIniNoCommentMarkers(5000, kFuzzSeed + 1),
        RandomAlmostIniNoCommentMarkers(5000, kFuzzSeed + 2),
        std::string(2000, '\0'),
        std::string(2000, static_cast<char>(0xFF)),
    };

    for (size_t i = 0; i < corpora.size(); i++)
    {
        IniFile first;
        first.LoadData(corpora[i]);
        const std::string firstSave = first.SaveData();

        IniFile second;
        second.LoadData(firstSave);
        const std::string secondSave = second.SaveData();

        EXPECT_EQ(firstSave, secondSave) << "corpus index=" << i;
    }
}

TEST_F(IniFile_Fuzzing_Test, RoundTripCanLoseDataWhenSavedValueContainsACommentMarker)
{
    // Found by ParseSerializeIsIdempotentOnArbitraryInput's original
    // unfiltered corpus (see the comment above): "last marker wins" (the
    // documented, correct behavior for a single parse) can compound across a
    // save+reload cycle. Minimal repro:
    //
    //   parse "k=abc#def;ghi"     -> backward scan hits ';' first -> value
    //                                becomes "abc#def" (correct: the
    //                                embedded '#' is legitimately kept,
    //                                per InlineCommentBackwardScanFindsLastMarker)
    //   save that value           -> file now contains "k = abc#def"
    //   parse it again            -> '#' is now the ONLY marker present ->
    //                                truncated AGAIN -> value becomes "abc"
    //
    // "def" is lost on the second pass even though nothing about it was ever
    // an actual comment - SaveData has no escaping for values that contain a
    // comment-trigger character, so re-loading a saved file is not always
    // equivalent to the in-memory state that produced it. Pinned here as a
    // known, current characteristic rather than an assumption nobody
    // verified; a fix (e.g. quoting/escaping such values on save) is a
    // deliberate design decision this test does not make on its own.
    IniFile first;
    first.LoadData("[s]\nk=abc#def;ghi\n");
    ASSERT_STREQ(first.GetValue("s", "k"), "abc#def");

    IniFile second;
    second.LoadData(first.SaveData());
    EXPECT_STREQ(second.GetValue("s", "k"), "abc");  // "def" did not survive the round trip
}

/// endregion </Property: round-trip idempotency>
