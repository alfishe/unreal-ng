#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/mfm_parser.h"

/// Tests for the size-agnostic MFM parser / validator
/// (docs/inprogress/2026-09-02-universal-track-model/test-plan.md, section 2)

class MFMParser_Test : public ::testing::Test
{
};

/// M1: parsing a TR-DOS formatted track reports 16 valid sectors whose offsets equal the track index
TEST_F(MFMParser_Test, ParseTrack_Trdos_MatchesTrackIndex)
{
    DiskImage::Track track;
    track.formatTrack(3, 1);

    TrackParseResult result = MFMParser::parseTrack(track.rawData(), track.rawSize());

    EXPECT_EQ(result.expectedSectors, 16u);
    EXPECT_EQ(result.sectorsFound, 16u);
    EXPECT_EQ(result.validSectors, 16u);
    EXPECT_TRUE(result.isCompliant());
    EXPECT_TRUE(result.errors.empty()) << result.dump();
    ASSERT_EQ(result.allSectors.size(), 16u);

    for (size_t i = 0; i < 16; i++)
    {
        const SectorParseResult& parsed = result.sectors[i];
        const DiskImage::Sector* indexed = track.getSector(static_cast<uint8_t>(i));
        ASSERT_NE(indexed, nullptr);

        EXPECT_TRUE(parsed.found);
        EXPECT_EQ(parsed.idamOffset, indexed->idamOffset);
        EXPECT_EQ(parsed.dataOffset, indexed->dataOffset);
        EXPECT_EQ(parsed.cylinder, 3);
        EXPECT_EQ(parsed.head, 1);
        EXPECT_EQ(parsed.sectorNo, i + 1);
        EXPECT_EQ(parsed.getSectorSize(), 256u);
        EXPECT_TRUE(parsed.idamCrcValid);
        EXPECT_TRUE(parsed.dataCrcValid);
        EXPECT_FALSE(parsed.deletedData);
    }
}

/// M2: non-nominal track lengths are honoured and never read past the buffer
TEST_F(MFMParser_Test, ParseTrack_VariableLength_NoOverrun)
{
    DiskImage::Track fm;
    fm.resizeRaw(3125);
    TrackParseResult fmResult = MFMParser::parseTrack(fm.rawData(), fm.rawSize(), 0);
    EXPECT_EQ(fmResult.sectorsFound, 0u);
    EXPECT_TRUE(fmResult.isCompliant()) << "no sectors expected on a gap-only 3125-byte track";

    DiskImage::TrackFormatSpec spec = DiskImage::TrackFormatSpec::trdos();
    spec.trackLength = 6464;
    DiskImage::Track fast;
    fast.formatTrack(0, 0, spec);
    TrackParseResult fastResult = MFMParser::parseTrack(fast.rawData(), fast.rawSize());
    EXPECT_EQ(fastResult.validSectors, 16u);

    // A truncated buffer (ID field cut off at the end) must not be read beyond its size
    std::vector<uint8_t> truncated(fast.rawData(), fast.rawData() + 30);
    TrackParseResult shortResult = MFMParser::parseTrack(truncated.data(), truncated.size(), 16);
    EXPECT_EQ(shortResult.validSectors, 0u);
}

/// M3: 9 x 512 layout is parsed with 512-byte CRC coverage
TEST_F(MFMParser_Test, ParseTrack_9x512)
{
    DiskImage::Track track;
    track.formatTrack(0, 0, DiskImage::TrackFormatSpec::plus3());

    TrackParseResult result = MFMParser::parseTrack(track.rawData(), track.rawSize(), 9);

    EXPECT_EQ(result.expectedSectors, 9u);
    EXPECT_EQ(result.sectorsFound, 9u);
    EXPECT_EQ(result.validSectors, 9u);
    EXPECT_TRUE(result.isCompliant()) << result.dump();

    for (size_t i = 0; i < 9; i++)
    {
        EXPECT_EQ(result.sectors[i].getSectorSize(), 512u);
        EXPECT_TRUE(result.sectors[i].dataCrcValid);
    }

    // Corrupt the last data byte of sector 4: only that sector's data CRC fails
    track.getSector(3)->data[511] ^= 0xFF;
    result = MFMParser::parseTrack(track.rawData(), track.rawSize(), 9);
    EXPECT_EQ(result.validSectors, 8u);
    EXPECT_FALSE(result.sectors[3].dataCrcValid);
}

/// M4: the validator keeps its TR-DOS diagnostics (interleave check, missing sectors) for expectedSectors = 16
TEST_F(MFMParser_Test, Validate_TrdosDiagnostics)
{
    const uint8_t interleave[16] = {1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15, 8, 16};
    DiskImage::Track track;
    track.formatTrack(0, 0, DiskImage::TrackFormatSpec::trdos(interleave, 16));

    MFMValidator::ValidationResult ok = MFMValidator::validate(track.rawData(), track.rawSize());
    EXPECT_TRUE(ok.passed) << ok.report();

    bool nonStandardInterleave = false;
    for (const auto& issue : ok.issues)
    {
        if (issue.code == "NON_STANDARD_INTERLEAVE") nonStandardInterleave = true;
    }
    EXPECT_FALSE(nonStandardInterleave) << "1:2 interleave is the TR-DOS standard";

    // 1:1 layout is flagged as non-standard interleave (info), still passes
    track.formatTrack(0, 0);
    MFMValidator::ValidationResult sequential = MFMValidator::validate(track.rawData(), track.rawSize());
    EXPECT_TRUE(sequential.passed) << sequential.report();
    nonStandardInterleave = false;
    for (const auto& issue : sequential.issues)
    {
        if (issue.code == "NON_STANDARD_INTERLEAVE") nonStandardInterleave = true;
    }
    EXPECT_TRUE(nonStandardInterleave);

    // 9 x 512 validated against the TR-DOS expectation fails with 7 missing sectors
    track.formatTrack(0, 0, DiskImage::TrackFormatSpec::plus3());
    MFMValidator::ValidationResult plus3 = MFMValidator::validate(track.rawData(), track.rawSize());
    EXPECT_FALSE(plus3.passed);
    EXPECT_EQ(plus3.parseResult.sectorsFound, 9u);
    EXPECT_EQ(plus3.getErrors().size(), 7u) << plus3.report();

    // ... and passes when the expectation is 9 sectors
    MFMValidator::ValidationResult plus3ok = MFMValidator::validate(track.rawData(), track.rawSize(), 9);
    EXPECT_TRUE(plus3ok.passed) << plus3ok.report();
}
