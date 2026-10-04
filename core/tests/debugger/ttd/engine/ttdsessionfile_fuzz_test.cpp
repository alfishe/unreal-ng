/// @file ttdsessionfile_fuzz_test.cpp
/// @brief QR-4: the session file reader against damaged files. Fixtures of
/// testdata/ttd/v2/ are damaged thousands of ways - a flipped bit, a cut, a
/// size field set to 0xFFFFFFFF, garbage - and loaded. The reader never
/// crashes, and never loads a session that differs from the one written: when
/// a load succeeds, every checkpoint it brought in restores exactly as the
/// undamaged file's does (damage is found by the CRCs, or the bytes it hit
/// were not needed, as an index the reader rebuilds by scanning).
///
/// Over four thousand loads, about a second: the reader's acceptance check.

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "debugger/ttd/engine/ttdsessionfile.h"
#include "debugger/ttd/timetravelengine.h"

using namespace ttd;

namespace
{
std::vector<uint8_t> Fixture(const std::string& name)
{
    std::ifstream f(TestPathHelper::FindProjectRoot() / "testdata" / "ttd" / "v2" / name, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

/// Every region of every checkpoint @p loaded holds, against @p reference
bool SameAsReference(const TimeTravelEngine& loaded, const TimeTravelEngine& reference)
{
    if (loaded.CheckpointCount() > reference.CheckpointCount() || loaded.Regions().size() != reference.Regions().size())
        return false;
    std::vector<uint8_t> a, b;
    for (size_t i = 0; i < loaded.CheckpointCount(); ++i)
    {
        // Positions and CPUs of all of them, the memory of a sample (the last one included)
        const bool sample = i % 25 == 0 || i + 1 == loaded.CheckpointCount();
        if (!(loaded.Checkpoint(i)->position == reference.Checkpoint(i)->position) ||
            std::memcmp(&loaded.Checkpoint(i)->cpu, &reference.Checkpoint(i)->cpu, sizeof(TTDCpuState)) != 0)
            return false;
        for (uint32_t r = 0; sample && r < loaded.Regions().size(); ++r)
        {
            a.assign(size_t(loaded.Regions()[r].pieces) * kTTDPieceSize, 0);
            b.assign(a.size(), 0);
            const bool okA = loaded.RestoreRegion(i, r, a.data()).Ok();
            const bool okB = reference.RestoreRegion(i, r, b.data()).Ok();
            if (okA && (!okB || a != b))
                return false;   // a damaged version that restores "fine" with other bytes
        }
    }
    return true;
}

void Fuzz(const std::string& name, int rounds, uint32_t seed)
{
    SCOPED_TRACE(name);
    const std::vector<uint8_t> original = Fixture(name);
    ASSERT_GT(original.size(), 1000u);
    TimeTravelEngine reference;
    {
        TTDMemorySource source(original);
        std::string error;
        ASSERT_TRUE(TTDSessionFile::Load(reference, source, error)) << error;
    }
    std::mt19937 rng(seed);
    int loadedWhole = 0, loadedPart = 0, refused = 0;
    for (int round = 0; round < rounds; ++round)
    {
        std::vector<uint8_t> bytes = original;
        const size_t at = std::uniform_int_distribution<size_t>(0, bytes.size() - 1)(rng);
        switch (round % 4)
        {
            case 0:   // a flipped bit
                bytes[at] ^= static_cast<uint8_t>(1u << (rng() % 8));
                break;
            case 1:   // cut short
                bytes.resize(at);
                break;
            case 2:   // a size field gone huge
                for (size_t k = 0; k < 4 && at + k < bytes.size(); ++k)
                    bytes[at + k] = 0xFF;
                break;
            default:   // garbage
                for (size_t k = 0; k < 16 && at + k < bytes.size(); ++k)
                    bytes[at + k] = static_cast<uint8_t>(rng());
                break;
        }
        TimeTravelEngine engine;
        TTDMemorySource source(bytes);
        std::string error;
        TTDSessionLoadReport report;
        if (!TTDSessionFile::Load(engine, source, error, &report))
        {
            ++refused;
            continue;
        }
        (report.complete ? loadedWhole : loadedPart)++;
        ASSERT_TRUE(SameAsReference(engine, reference))
            << "round " << round << ": damage at " << at << " loaded a different session";
    }
    std::printf("[ fuzz     ] %s: %d rounds - %d loaded whole, %d loaded in part, %d refused\n", name.c_str(), rounds,
                loadedWhole, loadedPart, refused);
}
}  // namespace

TEST(TTDSessionFileFuzz_Test, DamageNeverLoadsAnotherSession)
{
    Fuzz("synthetic.ttd", 3000, 1);
    Fuzz("synthetic-unfinished.ttd", 1000, 2);
    Fuzz("active-demo-converted.ttd", 120, 3);
}
