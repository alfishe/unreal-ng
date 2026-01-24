/// @file mfm_parser_test.cpp
/// @brief Unit tests for MFM track parsing and reindexing
/// Tests specifically designed to debug TR-DOS FORMAT verification issues

#include <gtest/gtest.h>
#include <iomanip>
#include "emulator/io/fdc/mfm_parser.h"
#include "emulator/io/fdc/diskimage.h"
#include "trdos_track_reference.h"

class MFMParserTest : public ::testing::Test
{
protected:
    const uint8_t* trackData = TRDOSReference::kFormattedTrack;
    size_t trackSize = TRDOSReference::kFormattedTrackLen;
};

/// Test that MFM parser correctly finds all 16 sectors in formatted track
TEST_F(MFMParserTest, ParseTrack_FindsAll16Sectors)
{
    ASSERT_EQ(trackSize, 6250) << "Track should be 6250 bytes";
    
    // Parse the track
    TrackParseResult result = MFMParser::parseTrack(trackData, trackSize);
    
    std::cout << "=== MFM Parser Results ===" << std::endl;
    std::cout << "Sectors found: " << result.sectorsFound << "/16" << std::endl;
    std::cout << "Valid sectors: " << result.validSectors << "/16" << std::endl;
    
    // Print each sector's details
    for (size_t i = 0; i < 16; i++)
    {
        const auto& s = result.sectors[i];
        std::cout << "Sector " << std::setw(2) << (i + 1) << ": ";
        if (s.found)
        {
            std::cout << "C" << (int)s.cylinder << " H" << (int)s.head 
                      << " S" << std::setw(2) << (int)s.sectorNo << " N" << (int)s.sizeCode
                      << " @0x" << std::hex << std::setw(4) << std::setfill('0') << s.idamOffset 
                      << std::dec << std::setfill(' ')
                      << " IDAM:" << (s.idamCrcValid ? "OK" : "BAD")
                      << " DATA:" << (s.dataBlockFound ? (s.dataCrcValid ? "OK" : "CRC") : "MISSING")
                      << std::endl;
        }
        else
        {
            std::cout << "NOT FOUND" << std::endl;
        }
    }
    
    // Print any errors
    for (const auto& err : result.errors)
    {
        std::cout << "ERROR: " << err << std::endl;
    }
    
    // Verify ALL 16 sectors were found
    EXPECT_EQ(result.sectorsFound, 16) << "Expected 16 sectors to be found";
    
    // Check specific sectors - sector 9 is at index 8
    EXPECT_TRUE(result.sectors[8].found) << "Sector 9 should be found";
    if (result.sectors[8].found)
    {
        EXPECT_EQ(result.sectors[8].sectorNo, 9) << "Sector at index 8 should be sector 9";
    }
    
    // Check sector 1
    EXPECT_TRUE(result.sectors[0].found) << "Sector 1 should be found";
    if (result.sectors[0].found)
    {
        EXPECT_EQ(result.sectors[0].sectorNo, 1) << "First sector should be sector 1";
    }
}

/// Test manually scanning for sync patterns (debug helper)
TEST_F(MFMParserTest, DebugSyncPatterns)
{
    std::cout << "\n=== Sync Pattern Search ===" << std::endl;
    
    // Manually scan for A1 A1 A1 patterns
    int syncCount = 0;
    int idamCount = 0;
    int damCount = 0;
    
    for (size_t i = 0; i < trackSize - 4; i++)
    {
        if (trackData[i] == 0xA1 && trackData[i+1] == 0xA1 && trackData[i+2] == 0xA1)
        {
            uint8_t mark = trackData[i+3];
            std::cout << "Sync @0x" << std::hex << std::setw(4) << std::setfill('0') << i 
                      << ": A1 A1 A1 " << std::setw(2) << (int)mark;
            
            if (mark == 0xFE)
            {
                // IDAM - print sector info
                std::cout << " (IDAM) C=" << (int)trackData[i+4] 
                          << " H=" << (int)trackData[i+5]
                          << " S=" << std::dec << (int)trackData[i+6] << std::hex
                          << " N=" << (int)trackData[i+7];
                idamCount++;
            }
            else if (mark == 0xFB)
            {
                std::cout << " (DAM)";
                damCount++;
            }
            std::cout << std::dec << std::endl;
            syncCount++;
            i += 3; // Skip past this pattern
        }
    }
    
    std::cout << "\nTotal sync patterns: " << syncCount << std::endl;
    std::cout << "IDAM patterns: " << idamCount << std::endl;
    std::cout << "DAM patterns: " << damCount << std::endl;
    
    EXPECT_EQ(idamCount, 16) << "Expected 16 IDAM patterns";
    EXPECT_EQ(damCount, 16) << "Expected 16 DAM patterns";
}

/// Test reindexing from MFM data - verify sector pointers are correct
TEST_F(MFMParserTest, ReindexFromMFM_CorrectlyMapsSectors)
{
    // Create a track and copy in the raw data
    DiskImage::Track track;
    
    // Copy raw data into the track's sectors array
    uint8_t* rawTrack = reinterpret_cast<uint8_t*>(&track.sectors[0]);
    std::memcpy(rawTrack, trackData, std::min(trackSize, (size_t)6250));
    
    // Run reindexFromMFM
    auto result = track.reindexFromMFM();
    
    std::cout << "\n=== Reindex Results ===" << std::endl;
    std::cout << "Sectors found: " << result.parseResult.sectorsFound << std::endl;
    std::cout << "Valid sectors: " << result.parseResult.validSectors << std::endl;
    
    // Check each sector can be accessed
    int accessibleCount = 0;
    for (uint8_t s = 1; s <= 16; s++)
    {
        uint8_t* data = track.getDataForSector(s);
        if (data != nullptr)
        {
            accessibleCount++;
            std::cout << "Sector " << std::setw(2) << (int)s << ": accessible at " 
                      << (void*)data << std::endl;
        }
        else
        {
            std::cout << "Sector " << std::setw(2) << (int)s << ": NULL (not accessible!)" << std::endl;
        }
    }
    
    std::cout << "\nAccessible sectors: " << accessibleCount << "/16" << std::endl;
    
    // Verify sector 9 specifically (the failing one in FORMAT verification)
    uint8_t* sector9Data = track.getDataForSector(9);
    EXPECT_NE(sector9Data, nullptr) << "Sector 9 data should be accessible after reindex";
    
    // All sectors should be accessible
    EXPECT_EQ(accessibleCount, 16) << "All 16 sectors should be accessible";
}

/// Test that getDataForSector returns data at correct offsets in raw track
TEST_F(MFMParserTest, ReindexFromMFM_VerifySectorDataOffset)
{
    DiskImage::Track track;
    uint8_t* rawTrack = reinterpret_cast<uint8_t*>(&track.sectors[0]);
    std::memcpy(rawTrack, trackData, std::min(trackSize, (size_t)6250));
    
    // Run reindexFromMFM
    track.reindexFromMFM();
    
    std::cout << "\n=== Sector Data Offset Verification ===" << std::endl;
    
    // For each sector, verify that getDataForSector() returns a pointer
    // that is within the raw track data range
    for (uint8_t s = 1; s <= 16; s++)
    {
        uint8_t* data = track.getDataForSector(s);
        ASSERT_NE(data, nullptr) << "Sector " << (int)s << " data should be accessible";
        
        // Calculate offset from start of track
        ptrdiff_t offset = data - rawTrack;
        
        std::cout << "Sector " << std::setw(2) << (int)s 
                  << ": data at offset 0x" << std::hex << std::setw(4) << std::setfill('0') << offset
                  << std::dec << std::setfill(' ') << std::endl;
        
        // Offset should be within track data
        EXPECT_GE(offset, 0) << "Sector " << (int)s << " offset should be >= 0";
        EXPECT_LT(offset, (ptrdiff_t)trackSize) << "Sector " << (int)s << " offset should be < track size";
    }
}

/// Test that sector data content is correct (all zeros for formatted track)
TEST_F(MFMParserTest, ReindexFromMFM_VerifySectorDataContent)
{
    DiskImage::Track track;
    uint8_t* rawTrack = reinterpret_cast<uint8_t*>(&track.sectors[0]);
    std::memcpy(rawTrack, trackData, std::min(trackSize, (size_t)6250));
    
    track.reindexFromMFM();
    
    std::cout << "\n=== Sector Data Content Verification ===" << std::endl;
    
    // All sectors in a freshly formatted track should contain zeros
    for (uint8_t s = 1; s <= 16; s++)
    {
        uint8_t* data = track.getDataForSector(s);
        ASSERT_NE(data, nullptr) << "Sector " << (int)s << " data should be accessible";
        
        // Check first and last bytes are zero (formatted track)
        bool allZero = true;
        for (int i = 0; i < 256; i++)
        {
            if (data[i] != 0x00)
            {
                allZero = false;
                std::cout << "Sector " << std::setw(2) << (int)s 
                          << ": byte[" << i << "] = 0x" << std::hex << (int)data[i] 
                          << std::dec << " (expected 0x00)" << std::endl;
                break;
            }
        }
        
        EXPECT_TRUE(allZero) << "Sector " << (int)s << " should contain all zeros";
    }
    
    std::cout << "All 16 sectors contain zeros (correct for formatted track)" << std::endl;
}

/// Test that getIDForSector returns correct IDAM data
TEST_F(MFMParserTest, ReindexFromMFM_VerifyIDAMContent)
{
    DiskImage::Track track;
    uint8_t* rawTrack = reinterpret_cast<uint8_t*>(&track.sectors[0]);
    std::memcpy(rawTrack, trackData, std::min(trackSize, (size_t)6250));
    
    track.reindexFromMFM();
    
    std::cout << "\n=== IDAM Content Verification ===" << std::endl;
    
    for (uint8_t s = 1; s <= 16; s++)
    {
        DiskImage::AddressMarkRecord* idam = track.getIDForSector(s);
        ASSERT_NE(idam, nullptr) << "Sector " << (int)s << " IDAM should be accessible";
        
        std::cout << "Sector " << std::setw(2) << (int)s 
                  << ": IDAM FE=" << std::hex << (int)idam->id_address_mark
                  << " C=" << (int)idam->cylinder
                  << " H=" << (int)idam->head
                  << " S=" << std::dec << (int)idam->sector
                  << " N=" << (int)idam->sector_size
                  << std::endl;
        
        // Verify IDAM fields are correct
        EXPECT_EQ(idam->id_address_mark, 0xFE) << "IDAM marker should be 0xFE";
        EXPECT_EQ(idam->cylinder, 1) << "Cylinder should be 1 (first formatted track)";
        EXPECT_EQ(idam->head, 0) << "Head should be 0";
        EXPECT_EQ(idam->sector, s) << "Sector number should match request";
        EXPECT_EQ(idam->sector_size, 1) << "Size code should be 1 (256 bytes)";
    }
}

/// Test MFM validator with full diagnostics
TEST_F(MFMParserTest, Validate_FullDiagnostics)
{
    auto result = MFMValidator::validate(trackData, trackSize);
    
    std::cout << "\n=== MFM Validator Report ===" << std::endl;
    std::cout << "Passed: " << (result.passed ? "YES" : "NO") << std::endl;
    std::cout << "Issues: " << result.issues.size() << std::endl;
    
    // Print issues
    for (const auto& issue : result.issues)
    {
        const char* severity = "";
        switch (issue.severity)
        {
            case MFMValidator::Severity::Info: severity = "INFO"; break;
            case MFMValidator::Severity::Warning: severity = "WARN"; break;
            case MFMValidator::Severity::Error: severity = "ERROR"; break;
            case MFMValidator::Severity::Critical: severity = "CRIT"; break;
        }
        
        std::cout << "[" << severity << "] " << issue.code;
        if (issue.sectorNo >= 0)
            std::cout << " (sec " << issue.sectorNo << ")";
        std::cout << "\n  " << issue.description << std::endl;
    }
    
    // Even if not passed, we should have found all sectors
    EXPECT_EQ(result.parseResult.sectorsFound, 16);
}
