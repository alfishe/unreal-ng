#pragma once

#include "stdafx.h"

// FDC-specific constants and functionality defined

/// region <Constants>

// FDD rotation speed
// 300 revolutions per minute => 5 revolutions per second => 200 ms per revolution
static constexpr const size_t FDD_RPM = 300;
static constexpr const size_t FDD_RPS = 5;

// FDD index pulse duration (in ms)
static constexpr const size_t FDD_INDEX_PULSE_DURATION_MS = 4;
// FDD index pulse duration (4ms) in T-states @3.5MHz
static constexpr const size_t FDD_INDEX_PULSE_DURATION_T_STATES = 14;

// Maximum theoreticaltrack length in bytes (bound to timings)
// 250 Kbps max transfer speed (31250 bytes per second) in MFM mode
// Within 200 ms of one revolution, 6250 bytes can be transferred
static constexpr size_t MAX_TRACK_LEN = 6250;

// Maximum useful track length in bytes (bound to index pulse duration and related data loss)
//+-------------+---------------------+-------------+--------------------+-----------------------------+
//| Index Pulse | Lost Bytes          | Total Track | Usable Track Bytes | Notes                       |
//| (ms)        | (During idx pulse)  |   Bytes     |                    |                             |
//+-------------+----------------- ---+-------------+--------------------+-----------------------------+
//| 2           | 62                  | 6,250       | 6,188              | Minimal loss, tight timing. |
//| 3           | 94                  | 6,250       | 6,156              | Common in some 3.5" drives. |
//| 4 (Typical) | 125                 | 6,250       | 6,125              | Most FDDs use this          |
//| 8           | 250                 | 6,250       | 6,000              | Rare; older 8" drives.      |
//+-------------+---------------------+-------------+--------------------+-----------------------------+
static constexpr const size_t MAX_TRACK_DATA_LEN = 6144;      // Aligned to sector size (works for 128 and 256 byte sectors)

static constexpr const uint8_t MAX_CYLINDERS = 86;            // Don't load images with track number exceeding this threshold
static constexpr const uint8_t MAX_SIDES = 2;                 // 2 sides is max allowed. There can be single side disks as well
static constexpr const uint8_t MAX_PHYSICAL_CYLINDER = 86;    // FDC won't perform seek commands beyond this cylinder
static constexpr const uint8_t MIN_SECTORS = 1;               // Track should have at least one sector (unless track is written via WRITE TRACK)
static constexpr const uint8_t MAX_SECTORS = 26;              // Max sectors per track (Assuming smallest 128 byte sector)

/// @brief Maximum allowable delay for host CPU to service a Data Request (DRQ) from the Floppy Disk Controller
/// For 250Kbps (MFM) modes
/// This timeout is calculated based on the 250 Kbps (31,250 bytes/sec) transfer rate used by
/// standard double-density floppy drives. The host system must provide the next data byte to the FDC
/// before the previous byte has been completely shifted out to the drive.
///
/// Calculation:
///   250,000 bits/sec ÷ 8 bits/byte = 31,250 bytes/sec
///   1,000,000 μs/sec ÷ 31,250 bytes/sec = 32 μs/byte
///
/// This is the absolute maximum time available between DRQ assertion and data transfer.
/// The actual safe window is smaller (typically 13.5μs) due to controller processing overhead.
static constexpr const size_t MAX_DRQ_SERVICE_TIME_MFM_US = 32;

// CPU at 3.5MHz has 114 t-states until timeout when DRQ is raised
static constexpr const size_t MAX_DRQ_SERVICE_TIME_MFM_T_STATES = 114;

/// @brief Maximum allowable delay for host CPU to service a Data Request (DRQ) from the Floppy Disk Controller
/// For 125Kbps (FM) modes
/// Calculation:
///   125,000 bits/sec ÷ 8 bits/byte = 15,625 bytes/sec
///   1,000,000 μs/sec ÷ 15,625 bytes/sec = 64 μs/byte
static constexpr const size_t MAX_DRQ_SERVICE_TIME_FM_US = 64;
static constexpr const size_t MAX_DRQ_SERVICE_TIME_FM_T_STATES = 228;

/// @brief Practical safe DRQ service time including FDC processing margin
/// While theoretically 32μs is available at 250Kbps, the WD1793 datasheet specifies
/// a tighter 13.5μs window (MFM) to account for:
/// - Signal propagation delays
/// - Controller state machine overhead
/// - Data bus settling time
///
/// This is the actual deadline the host system must meet to avoid DATA LOST errors.
static constexpr const size_t SAFE_DRQ_SERVICE_TIME_MFM_US = 13;

/// For 125Kbps (FM) modes
static constexpr const size_t SAFE_DRQ_SERVICE_TIME_FM_US = 27;

/// @brief Controller internal timing constants
/// These represent the WD1793's internal processing delays between critical events,
/// measured in byte times (32μs units at 250Kbps)
namespace FDC_Delays
{
    /// 96μs (3*32) delay after IDAM
    static constexpr const size_t IDAM_TO_DATA_MARK_MFM = 3;

    /// 384μs of 0x00 before DAM
    static constexpr const size_t PREAMBLE_ZEROS = 12;
};


/// endregion </Constants>

/// region <Types>
class CRCHelper
{
protected:
    /// CRC-16 CCITT with 0x1021 polynomial
    /// CRC Name : CRC-16
    /// Width : 16 Bits
    /// Polynomial Used : 0x1021 (hex)
    /// @details http://www.sunshine2k.de/coding/javascript/crc/crc_js.html calculator was used (CRC-16, CRC16_CCIT_ZERO, 0x1021)
    static constexpr const uint16_t CRC16_CCITT_LOOKUP_TABLE[256] =
    {
        0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
        0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
        0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
        0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
        0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
        0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
        0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
        0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
        0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
        0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
        0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
        0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
        0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
        0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
        0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
        0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
        0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
        0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
        0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
        0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
        0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
        0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
        0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
        0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
        0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
        0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
        0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
        0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
        0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
        0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
        0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
        0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
    };

    static constexpr const uint16_t crcTable[256] =
    {
        0x0000, 0x97A0, 0xB9E1, 0x2E41, 0xE563, 0x72C3, 0x5C82, 0xCB22,
        0xCAC7, 0x5D67, 0x7326, 0xE486, 0x2FA4, 0xB804, 0x9645, 0x01E5,
        0x032F, 0x948F, 0xBACE, 0x2D6E, 0xE64C, 0x71EC, 0x5FAD, 0xC80D,
        0xC9E8, 0x5E48, 0x7009, 0xE7A9, 0x2C8B, 0xBB2B, 0x956A, 0x02CA,
        0x065E, 0x91FE, 0xBFBF, 0x281F, 0xE33D, 0x749D, 0x5ADC, 0xCD7C,
        0xCC99, 0x5B39, 0x7578, 0xE2D8, 0x29FA, 0xBE5A, 0x901B, 0x07BB,
        0x0571, 0x92D1, 0xBC90, 0x2B30, 0xE012, 0x77B2, 0x59F3, 0xCE53,
        0xCFB6, 0x5816, 0x7657, 0xE1F7, 0x2AD5, 0xBD75, 0x9334, 0x0494,
        0x0CBC, 0x9B1C, 0xB55D, 0x22FD, 0xE9DF, 0x7E7F, 0x503E, 0xC79E,
        0xC67B, 0x51DB, 0x7F9A, 0xE83A, 0x2318, 0xB4B8, 0x9AF9, 0x0D59,
        0x0F93, 0x9833, 0xB672, 0x21D2, 0xEAF0, 0x7D50, 0x5311, 0xC4B1,
        0xC554, 0x52F4, 0x7CB5, 0xEB15, 0x2037, 0xB797, 0x99D6, 0x0E76,
        0x0AE2, 0x9D42, 0xB303, 0x24A3, 0xEF81, 0x7821, 0x5660, 0xC1C0,
        0xC025, 0x5785, 0x79C4, 0xEE64, 0x2546, 0xB2E6, 0x9CA7, 0x0B07,
        0x09CD, 0x9E6D, 0xB02C, 0x278C, 0xECAE, 0x7B0E, 0x554F, 0xC2EF,
        0xC30A, 0x54AA, 0x7AEB, 0xED4B, 0x2669, 0xB1C9, 0x9F88, 0x0828,
        0x8FD8, 0x1878, 0x3639, 0xA199, 0x6ABB, 0xFD1B, 0xD35A, 0x44FA,
        0x451F, 0xD2BF, 0xFCFE, 0x6B5E, 0xA07C, 0x37DC, 0x199D, 0x8E3D,
        0x8CF7, 0x1B57, 0x3516, 0xA2B6, 0x6994, 0xFE34, 0xD075, 0x47D5,
        0x4630, 0xD190, 0xFFD1, 0x6871, 0xA353, 0x34F3, 0x1AB2, 0x8D12,
        0x8986, 0x1E26, 0x3067, 0xA7C7, 0x6CE5, 0xFB45, 0xD504, 0x42A4,
        0x4341, 0xD4E1, 0xFAA0, 0x6D00, 0xA622, 0x3182, 0x1FC3, 0x8863,
        0x8AA9, 0x1D09, 0x3348, 0xA4E8, 0x6FCA, 0xF86A, 0xD62B, 0x418B,
        0x406E, 0xD7CE, 0xF98F, 0x6E2F, 0xA50D, 0x32AD, 0x1CEC, 0x8B4C,
        0x8364, 0x14C4, 0x3A85, 0xAD25, 0x6607, 0xF1A7, 0xDFE6, 0x4846,
        0x49A3, 0xDE03, 0xF042, 0x67E2, 0xACC0, 0x3B60, 0x1521, 0x8281,
        0x804B, 0x17EB, 0x39AA, 0xAE0A, 0x6528, 0xF288, 0xDCC9, 0x4B69,
        0x4A8C, 0xDD2C, 0xF36D, 0x64CD, 0xAFEF, 0x384F, 0x160E, 0x81AE,
        0x853A, 0x129A, 0x3CDB, 0xAB7B, 0x6059, 0xF7F9, 0xD9B8, 0x4E18,
        0x4FFD, 0xD85D, 0xF61C, 0x61BC, 0xAA9E, 0x3D3E, 0x137F, 0x84DF,
        0x8615, 0x11B5, 0x3FF4, 0xA854, 0x6376, 0xF4D6, 0xDA97, 0x4D37,
        0x4CD2, 0xDB72, 0xF533, 0x6293, 0xA9B1, 0x3E11, 0x1050, 0x87F0
    };

public:
    /// Inline helper to swap bytes in parameter provided
    /// @param i 16-bit input value
    /// @return 16-bit output value with bytes swapped
    static inline uint16_t _byteswap_ushort(uint16_t i)
    {
        return (i >> 8) | (i << 8);
    }

    /// Used in WD1793 emulation for sector CRC
    /// @param ptr Pointer to byte buffer
    /// @param size Buffer size
    /// @return crc16-CCITT calculated value
    /// @details It's crc16-CCITT. Polynomial is: x^16 + x^12 + x^5 + 1. CRC init value: 0xCDB4
    static uint16_t crcWD1793(uint8_t *ptr, uint16_t size)
    {
        uint16_t crc = 0xCDB4;

        // Table lookup implementation
        for (size_t i = 0; i < size; i++)
        {
            crc = (crc << 8) ^ CRC16_CCITT_LOOKUP_TABLE[((crc >> 8) ^ (*ptr++)) & 0xFF];
        }

        /// region <Algorithm>
        /*
        // crc-16-CCITT calculation algorithm
        while (size--)
        {
            crc ^= (*ptr++) << 8;

            for (int j = 0; j < 8; j++)
            {
                if ((crc & 0x8000) != 0) // Overflow bit is used to determine if XOR with polynomial is required
                {
                    crc = (crc << 1) ^ 0x1021; // Bit-encoded representation of polynomial taps: x^12 + x^5 + 1
                }
                else
                {
                    crc <<= 1;
                }
            }
        }
        */
        /// endregion </Algorithm>

        uint16_t result = _byteswap_ushort(crc);

        return result;
    }

    /// Used for UDI disk format
    /// @details In fact it is CRC-32 algorithm with one mistake introduced:
    ///
    int crcUDI(int &crc, unsigned char *buf, unsigned len)
    {
        while (len--)
        {
            crc ^= -1 ^ *buf++;
            for (int k = 8; k--; )
            {
                int temp = -(crc & 1);
                crc >>= 1;
                crc ^= int(0xEDB88320) & temp;
            }

            crc ^= -1;
        }

        return crc;
    }

    /// Used for TD0 image format
    static uint16_t crc16(uint8_t *buf, uint16_t size)
    {
        uint16_t crc = 0;

        while (size--)
        {
            crc = (crc >> 8) ^ crcTable[(crc & 0xff) ^ *buf++];
        }

        return _byteswap_ushort(crc);
    }

    static void crc32(int &crc, uint8_t *buf, unsigned len);
};

/// endregion </Types>
