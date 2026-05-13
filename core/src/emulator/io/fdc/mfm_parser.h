#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "emulator/io/fdc/fdc.h"
#include "stdafx.h"

/// @file mfm_parser.h
/// @brief MFM track parsing and validation for WD1793 FDC
///
/// Provides classes for parsing raw MFM track data, validating sector structure,
/// and reporting compliance. Designed to support multiple disk formats:
/// - TRD/SCL (TR-DOS)
/// - SCP (SuperCard Pro)
/// - UDI (Ultra Disk Image)
/// - FDI (Formatted Disk Image)

/// MFM format constants (per WD1793 datasheet)
namespace MFM
{
// Sync and address mark bytes
constexpr uint8_t SYNC_BYTE = 0xA1;  ///< Sync byte with missing clock (written as F5)
constexpr uint8_t IDAM = 0xFE;       ///< ID Address Mark
constexpr uint8_t DAM = 0xFB;        ///< Data Address Mark (normal)
constexpr uint8_t DDAM = 0xF8;       ///< Deleted Data Address Mark
constexpr uint8_t INDEX_AM = 0xFC;   ///< Index Address Mark

// Gap fill byte
constexpr uint8_t GAP_BYTE = 0x4E;   ///< Gap fill byte
constexpr uint8_t SYNC_ZERO = 0x00;  ///< Sync zero byte

// Track geometry
constexpr size_t RAW_TRACK_SIZE = 6250;   ///< Bytes per track at 250kbps
constexpr size_t SECTORS_PER_TRACK = 16;  ///< TR-DOS sectors per track

// IDAM record size (excluding sync bytes)
constexpr size_t IDAM_SIZE = 7;        ///< FE + C + H + S + N + CRC(2)
constexpr size_t DAM_HEADER_SIZE = 4;  ///< A1 A1 A1 FB
}  // namespace MFM

/// Result of parsing a single sector from MFM data
struct SectorParseResult
{
    bool found = false;     ///< IDAM (0xFE) found
    size_t idamOffset = 0;  ///< Offset of IDAM in raw track data
    size_t dataOffset = 0;  ///< Offset of data block in raw track data

    // IDAM fields
    uint8_t cylinder = 0;  ///< Cylinder from IDAM
    uint8_t head = 0;      ///< Head/side from IDAM
    uint8_t sectorNo = 0;  ///< Sector number from IDAM (1-16 for TR-DOS)
    uint8_t sizeCode = 0;  ///< Size code (0=128, 1=256, 2=512, 3=1024)

    // CRC validation
    bool idamCrcValid = false;       ///< IDAM CRC valid
    uint16_t idamCrcExpected = 0;    ///< CRC from disk
    uint16_t idamCrcCalculated = 0;  ///< CRC we calculated

    // Data block
    bool dataBlockFound = false;  ///< Data Address Mark (0xFB/0xF8) found
    bool deletedData = false;     ///< True if DDAM (0xF8) instead of DAM (0xFB)
    bool dataCrcValid = false;    ///< Data block CRC valid
    uint16_t dataCrcExpected = 0;
    uint16_t dataCrcCalculated = 0;

    // Error info
    std::string error;  ///< Error description if validation failed

    /// Get sector size in bytes
    size_t getSectorSize() const
    {
        return 128 << (sizeCode & 0x03);
    }

    /// Check if sector is fully valid
    bool isValid() const
    {
        return found && idamCrcValid && dataBlockFound && dataCrcValid;
    }

    /// Get status string
    std::string getStatus() const
    {
        if (!found)
            return "NOT_FOUND";
        if (!idamCrcValid)
            return "IDAM_CRC_ERROR";
        if (!dataBlockFound)
            return "NO_DATA_BLOCK";
        if (!dataCrcValid)
            return "DATA_CRC_ERROR";
        return "OK";
    }
};

/// Result of parsing an entire track from MFM data
struct TrackParseResult
{
    size_t sectorsFound = 0;             ///< Number of IDAMs found
    size_t validSectors = 0;             ///< Number of fully valid sectors
    SectorParseResult sectors[16] = {};  ///< Per-sector results (index = sector-1)
    std::vector<std::string> errors;     ///< Critical errors
    std::vector<std::string> warnings;   ///< Non-critical issues

    /// Check if track is fully compliant (all 16 sectors valid)
    bool isCompliant() const
    {
        return validSectors == 16;
    }

    /// Generate human-readable summary
    std::string dump() const
    {
        std::stringstream ss;
        ss << "Track Parse Result: " << sectorsFound << "/16 sectors found, " << validSectors << " valid\n";

        for (size_t i = 0; i < 16; i++)
        {
            const auto& s = sectors[i];
            ss << "  Sector " << (i + 1) << ": ";
            if (!s.found)
            {
                ss << "NOT FOUND\n";
            }
            else
            {
                ss << "C" << (int)s.cylinder << " H" << (int)s.head << " S" << (int)s.sectorNo << " ("
                   << s.getSectorSize() << "B) "
                   << "@" << s.idamOffset << " [" << s.getStatus() << "]";
                if (!s.error.empty())
                    ss << " - " << s.error;
                ss << "\n";
            }
        }

        for (const auto& e : errors)
            ss << "ERROR: " << e << "\n";
        for (const auto& w : warnings)
            ss << "WARNING: " << w << "\n";

        return ss.str();
    }
};

/// MFM Track Parser
/// Scans raw track data for sector structure and validates MFM compliance
class MFMParser
{
public:
    /// Parse raw track data and validate all sectors
    /// @param rawData Pointer to raw track data (6250 bytes)
    /// @param trackSize Size of track data
    /// @return Parse result with per-sector details
    static TrackParseResult parseTrack(const uint8_t* rawData, size_t trackSize = MFM::RAW_TRACK_SIZE)
    {
        TrackParseResult result;

        if (!rawData || trackSize < 100)
        {
            result.errors.push_back("Invalid track data pointer or size");
            return result;
        }

        // Scan for ID Address Marks (A1 A1 A1 FE pattern)
        for (size_t pos = 0; pos < trackSize - 20; pos++)
        {
            if (findSyncPattern(rawData, pos, trackSize))
            {
                uint8_t mark = rawData[pos + 3];

                if (mark == MFM::IDAM)
                {
                    // Found IDAM - parse sector
                    SectorParseResult sector = parseSector(rawData, pos, trackSize);

                    if (sector.found && sector.sectorNo >= 1 && sector.sectorNo <= 16)
                    {
                        uint8_t idx = sector.sectorNo - 1;

                        // Check for duplicate sector
                        if (result.sectors[idx].found)
                        {
                            result.warnings.push_back("Duplicate sector " + std::to_string(sector.sectorNo));
                        }

                        result.sectors[idx] = sector;
                        result.sectorsFound++;
                        if (sector.isValid())
                            result.validSectors++;

                        // Skip past this sector to avoid re-parsing
                        pos += 50;
                    }
                }
            }
        }

        // Check for missing sectors
        for (size_t i = 0; i < 16; i++)
        {
            if (!result.sectors[i].found)
            {
                result.errors.push_back("Sector " + std::to_string(i + 1) + " not found");
            }
        }

        return result;
    }

private:
    /// Check for sync pattern (A1 A1 A1)
    static bool findSyncPattern(const uint8_t* data, size_t pos, size_t maxPos)
    {
        if (pos + 3 >= maxPos)
            return false;
        return data[pos] == MFM::SYNC_BYTE && data[pos + 1] == MFM::SYNC_BYTE && data[pos + 2] == MFM::SYNC_BYTE;
    }

    /// Parse single sector starting at IDAM sync position
    static SectorParseResult parseSector(const uint8_t* data, size_t syncPos, size_t trackSize)
    {
        SectorParseResult result;

        size_t idamPos = syncPos + 3;  // Skip A1 A1 A1
        if (idamPos + MFM::IDAM_SIZE > trackSize)
        {
            result.error = "IDAM truncated";
            return result;
        }

        result.found = true;
        result.idamOffset = idamPos;

        // Parse IDAM fields: FE C H S N CRC(2)
        result.cylinder = data[idamPos + 1];
        result.head = data[idamPos + 2];
        result.sectorNo = data[idamPos + 3];
        result.sizeCode = data[idamPos + 4];
        result.idamCrcExpected = (data[idamPos + 5] << 8) | data[idamPos + 6];

        // Validate IDAM CRC (includes FE byte)
        result.idamCrcCalculated = CRCHelper::crcWD1793(const_cast<uint8_t*>(&data[idamPos]), 5);
        result.idamCrcValid = (result.idamCrcExpected == result.idamCrcCalculated);

        if (!result.idamCrcValid)
        {
            result.error = "IDAM CRC mismatch";
        }

        // Look for Data Address Mark (skip gap, ~22 bytes gap1 + 12 bytes sync)
        // Search range: IDAM + 7 + 22..50 bytes
        size_t searchStart = idamPos + 7 + 20;
        size_t searchEnd = (std::min)(idamPos + 7 + 60, trackSize - 4);

        for (size_t dpos = searchStart; dpos < searchEnd; dpos++)
        {
            if (findSyncPattern(data, dpos, trackSize))
            {
                uint8_t dam = data[dpos + 3];
                if (dam == MFM::DAM || dam == MFM::DDAM)
                {
                    result.dataBlockFound = true;
                    result.deletedData = (dam == MFM::DDAM);
                    result.dataOffset = dpos + 4;  // After A1 A1 A1 FB

                    // Validate data CRC
                    size_t dataSize = result.getSectorSize();
                    if (result.dataOffset + dataSize + 2 <= trackSize)
                    {
                        // CRC covers DAM byte + data
                        result.dataCrcCalculated =
                            CRCHelper::crcWD1793(const_cast<uint8_t*>(&data[dpos + 3]), 1 + dataSize);
                        size_t crcPos = result.dataOffset + dataSize;
                        result.dataCrcExpected = (data[crcPos] << 8) | data[crcPos + 1];
                        result.dataCrcValid = (result.dataCrcExpected == result.dataCrcCalculated);

                        if (!result.dataCrcValid && result.error.empty())
                        {
                            result.error = "Data CRC mismatch";
                        }
                    }
                    break;
                }
            }
        }

        if (!result.dataBlockFound && result.error.empty())
        {
            result.error = "Data block not found";
        }

        return result;
    }
};

/// MFM Track Validator
/// High-level validation with reasoning, issue triaging, and fix hints
class MFMValidator
{
public:
    /// Diagnostic severity levels
    enum class Severity
    {
        Info,
        Warning,
        Error,
        Critical
    };

    /// Diagnostic issue with reasoning
    struct Issue
    {
        Severity severity;
        std::string code;         ///< Issue code (e.g., "IDAM_CRC_MISMATCH")
        std::string description;  ///< Human-readable description
        std::string reason;       ///< Why this might have happened
        std::string hint;         ///< How to fix or investigate
        int sectorNo = -1;        ///< Affected sector (-1 = track-level)
        size_t offset = 0;        ///< Byte offset in track data
    };

    /// Validation result with full diagnostics
    struct ValidationResult
    {
        bool passed = false;
        TrackParseResult parseResult;
        std::vector<Issue> issues;

        /// Get issues by severity
        std::vector<Issue> getErrors() const
        {
            std::vector<Issue> result;
            for (const auto& i : issues)
            {
                if (i.severity == Severity::Error || i.severity == Severity::Critical)
                    result.push_back(i);
            }
            return result;
        }

        /// Generate detailed diagnostic report
        std::string report() const
        {
            std::stringstream ss;
            ss << "=== MFM Track Validation Report ===\n";
            ss << "Status: " << (passed ? "PASSED" : "FAILED") << "\n";
            ss << "Sectors: " << parseResult.validSectors << "/16 valid\n\n";

            if (issues.empty())
            {
                ss << "No issues found.\n";
            }
            else
            {
                ss << "Issues (" << issues.size() << "):\n";
                for (const auto& issue : issues)
                {
                    ss << "\n[" << severityToString(issue.severity) << "] " << issue.code;
                    if (issue.sectorNo >= 0)
                        ss << " (Sector " << issue.sectorNo << ")";
                    ss << "\n";
                    ss << "  Description: " << issue.description << "\n";
                    ss << "  Reason: " << issue.reason << "\n";
                    ss << "  Hint: " << issue.hint << "\n";
                }
            }

            return ss.str();
        }

    private:
        static const char* severityToString(Severity s)
        {
            switch (s)
            {
                case Severity::Info:
                    return "INFO";
                case Severity::Warning:
                    return "WARNING";
                case Severity::Error:
                    return "ERROR";
                case Severity::Critical:
                    return "CRITICAL";
            }
            return "UNKNOWN";
        }
    };

    /// Validate a track with full diagnostics
    static ValidationResult validate(const uint8_t* rawData, size_t trackSize = MFM::RAW_TRACK_SIZE)
    {
        ValidationResult result;
        result.parseResult = MFMParser::parseTrack(rawData, trackSize);

        // Triage issues from parse result
        triageParseResult(result);

        // Additional validation checks
        checkTrackStructure(rawData, trackSize, result);
        checkSectorOrder(result);
        checkGapPatterns(rawData, trackSize, result);

        result.passed = result.parseResult.isCompliant() && result.getErrors().empty();
        return result;
    }

private:
    /// Triage issues from parse result into detailed diagnostics
    static void triageParseResult(ValidationResult& result)
    {
        const auto& pr = result.parseResult;

        for (size_t i = 0; i < 16; i++)
        {
            const auto& s = pr.sectors[i];
            int sectorNo = i + 1;

            if (!s.found)
            {
                result.issues.push_back(
                    {Severity::Error, "SECTOR_NOT_FOUND",
                     "Sector " + std::to_string(sectorNo) + " not found in track data",
                     "The ID Address Mark (A1 A1 A1 FE) sequence for this sector was not detected",
                     "Check if Write Track wrote all 16 sectors. Verify sector number field in format routine."});
            }
            else
            {
                if (!s.idamCrcValid)
                {
                    result.issues.push_back({Severity::Error, "IDAM_CRC_MISMATCH",
                                             "Sector " + std::to_string(sectorNo) + " IDAM CRC invalid",
                                             "CRC expected: 0x" + toHex(s.idamCrcExpected) + ", calculated: 0x" +
                                                 toHex(s.idamCrcCalculated),
                                             "Verify F7 (CRC write) command was sent after IDAM fields. Check CRC "
                                             "preset (F5) sent before FE.",
                                             sectorNo, s.idamOffset});
                }

                if (!s.dataBlockFound)
                {
                    result.issues.push_back(
                        {Severity::Error, "DATA_BLOCK_MISSING",
                         "Sector " + std::to_string(sectorNo) + " data block not found",
                         "No Data Address Mark (A1 A1 A1 FB) found after IDAM",
                         "Verify gap1/sync1 bytes written correctly. Check F5 F5 F5 FB sequence after gap.", sectorNo,
                         s.idamOffset});
                }
                else if (!s.dataCrcValid)
                {
                    result.issues.push_back({Severity::Warning, "DATA_CRC_MISMATCH",
                                             "Sector " + std::to_string(sectorNo) + " data CRC invalid",
                                             "CRC expected: 0x" + toHex(s.dataCrcExpected) + ", calculated: 0x" +
                                                 toHex(s.dataCrcCalculated),
                                             "Verify F7 sent after all data bytes. Check data byte count matches "
                                             "sector size (256 for TR-DOS).",
                                             sectorNo, s.dataOffset});
                }

                // Validate sector addressing
                if (s.sectorNo != sectorNo)
                {
                    result.issues.push_back(
                        {Severity::Warning, "SECTOR_NUMBER_MISMATCH",
                         "Sector at position " + std::to_string(i) + " has number " + std::to_string(s.sectorNo),
                         "Sector number in IDAM doesn't match expected position",
                         "Check interleave table and sector numbering in format routine. TR-DOS uses 1-16.", sectorNo,
                         s.idamOffset});
                }
            }
        }
    }

    /// Check overall track structure (gaps, sync patterns)
    static void checkTrackStructure(const uint8_t* data, size_t size, ValidationResult& result)
    {
        // Check track starts with gap bytes
        size_t gapCount = 0;
        for (size_t i = 0; i < (std::min)(size, (size_t)50) && data[i] == MFM::GAP_BYTE; i++)
        {
            gapCount++;
        }
        if (gapCount < 10)
        {
            result.issues.push_back({Severity::Warning, "SMALL_PREAMBLE_GAP",
                                     "Track preamble gap is only " + std::to_string(gapCount) + " bytes",
                                     "Standard format expects 10+ bytes of 0x4E before first sector",
                                     "May cause read timing issues on real hardware", -1, 0});
        }
    }

    /// Check sector ordering and interleave
    static void checkSectorOrder(ValidationResult& result)
    {
        const auto& pr = result.parseResult;
        std::vector<std::pair<size_t, int>> sectorOffsets;  // (offset, sectorNo)

        for (size_t i = 0; i < 16; i++)
        {
            if (pr.sectors[i].found)
            {
                sectorOffsets.push_back({pr.sectors[i].idamOffset, static_cast<int>(i + 1)});
            }
        }

        // Sort by offset to check physical order
        std::sort(sectorOffsets.begin(), sectorOffsets.end());

        // Check if interleave matches TR-DOS standard (1, 9, 2, 10, 3, 11...)
        static const int TRDOS_INTERLEAVE[] = {1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15, 8, 16};
        bool standardInterleave = true;

        if (sectorOffsets.size() == 16)
        {
            for (size_t i = 0; i < 16; i++)
            {
                if (sectorOffsets[i].second != TRDOS_INTERLEAVE[i])
                {
                    standardInterleave = false;
                    break;
                }
            }

            if (!standardInterleave)
            {
                result.issues.push_back(
                    {Severity::Info, "NON_STANDARD_INTERLEAVE",
                     "Sector interleave does not match TR-DOS standard (1:2)",
                     "Physical sector order differs from expected 1,9,2,10,3,11...",
                     "May be intentional for copy protection or different DOS. Will work but non-standard."});
            }
        }
    }

    /// Check gap patterns between sectors
    static void checkGapPatterns([[maybe_unused]] const uint8_t* data, [[maybe_unused]] size_t size,
                                 [[maybe_unused]] ValidationResult& result)
    {
        // Could add checks for:
        // - Gap2 size consistency
        // - Proper sync byte sequences (12x 0x00)
        // - Gap byte fill values
    }

    static std::string toHex(uint16_t v)
    {
        char buf[8];
        snprintf(buf, sizeof(buf), "%04X", v);
        return buf;
    }
};
