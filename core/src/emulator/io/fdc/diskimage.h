#pragma once

#include "stdafx.h"

#include <algorithm>
#include "emulator/io/fdc/fdc.h"

class DiskImage
{
    /// region <Types>
public:
#pragma pack(push, 1)
    /// This record is used by WD1793 to verify head positioning
    /// Used by READ_ADDRESS and READ_TRACK commands
    struct AddressMarkRecord
    {
        uint8_t id_address_mark = 0xFE;
        uint8_t cylinder = 0x00;
        uint8_t head = 0x00;
        uint8_t sector = 0x00;
        uint8_t sector_len = 0x01;  // 0x01 - sector size 256 bytes. The only option for TR-DOS
        uint16_t id_crc = 0xFFFF;

        /// region <Methods>
    public:
        /// CRC is calculated for all AddressMarkRecord fields starting from id_address_mark byte
        void recalculateCRC()
        {
            uint16_t crc = CRCHelper::crcWD1793(&id_address_mark, 5);
            id_crc = crc;
        }

        /// Check if CRC valid
        bool isCRCValid()
        {
            uint16_t crc = CRCHelper::crcWD1793(&id_address_mark, 5);

            bool result = crc == id_crc;

            return result;
        }
        /// endregion </Methods>
    };

    /// Each sector on disk represented by this structure.
    /// It represents modified IBM System 34 format layout from WD1793 datasheet
    struct RawSectorBytes
    {
        // Sector start gap
        uint8_t gap0[10] = { 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E };
        uint8_t sync0[12] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // Must be exactly 12 bytes of zeroes

        // Index block
        uint8_t f5_token0[3] = { 0xA1, 0xA1, 0xA1 }; // Clock transitions between bits 4 and 5 missing (Written by putting 0xF5 into Data Register during WRITE TRACK command by WD1793)
        AddressMarkRecord address_record;

        // Gap between blocks
        uint8_t gap1[22] = { 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E };
        uint8_t sync1[12] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }; // Must be exactly 12 bytes of zeroes

        // Data block
        uint8_t f5_token1[3] = { 0xA1, 0xA1, 0xA1 }; // Clock transitions between bits 4 and 5 missing (Written by putting 0xF5 into Data Register during WRITE TRACK command by WD1793)
        uint8_t data_address_mark = 0xFB;
        uint8_t data[256] = {};
        uint16_t data_crc = 0xFFFF;

        // Sector end gap
        uint8_t gap2[60] =
        {
            0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E,
            0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E,
            0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E,
            0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E,
            0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E,
            0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E, 0x4E
        };

        /// region <Methods>
    public:
        /// CRC is calculated for all sector data AND data_address_mark
        void recalculateDataCRC()
        {
            uint16_t crc = CRCHelper::crcWD1793(&data_address_mark, sizeof(data) + sizeof(data_address_mark));
            data_crc = crc;
        }

        /// Check if CRC valid
        bool isCRCValid()
        {
            uint16_t crc = CRCHelper::crcWD1793(&data_address_mark, sizeof(data) + sizeof(data_address_mark));

            bool result = crc == data_crc;

            return result;
        }

        /// endregion </Methods>
    };

    /// Contains only raw track information as on disk. No additional indexes
    struct RawTrack
    {
        /// region <Constants>
    public:
        /// 200ms per disk revolution, 4us per bit => 32 us per byte. So 200000 / 32 = 6250 bytes per track.
        /// TR-DOS allows track size in a range [6208...6464] bytes
        static constexpr const size_t RAW_TRACK_SIZE = 6250;
        static constexpr const size_t SECTORS_PER_TRACK = 16;                              // TR-DOS uses 16 sector layout
        static constexpr const size_t RAW_SECTOR_BYTES = sizeof(RawSectorBytes);           // 388 bytes expected
        static constexpr const size_t TRACK_BITMAP_SIZE_BYTES = (RAW_TRACK_SIZE + 7) / 8;  // 782 bytes expected
        static constexpr const size_t TRACK_END_GAP_BYTES = RAW_TRACK_SIZE - (RAW_SECTOR_BYTES * SECTORS_PER_TRACK); // 42 bytes expected
        /// endregion </Constants>

        // Fields
        RawSectorBytes sectors[SECTORS_PER_TRACK];
        uint8_t endGap[TRACK_END_GAP_BYTES];
        uint8_t clockMarksBitmap[TRACK_BITMAP_SIZE_BYTES];
        uint8_t badBytesBitmap[TRACK_BITMAP_SIZE_BYTES];

        /// region <Constructors / destructors>
    public:
        RawTrack()
        {
            std::fill(&endGap[0], &endGap[0] + sizeof(endGap), 0x4E);
        }
        ~RawTrack() = default;  // Note: do not make it virtual since vtable will add overhead to sizeof()
        /// endregion </Constructors / destructors>

        /// region <Methods>
    public:
        void formatTrack(uint8_t cylinder, uint8_t side)
        {
            for (uint8_t sector = 0; sector < SECTORS_PER_TRACK; sector++)
            {
                sectors[sector] = RawSectorBytes(); // Re-initialize raw sector data using it's default constructor

                // Set proper addressing
                AddressMarkRecord& markRecord = sectors[sector].address_record;
                markRecord.cylinder = cylinder;
                markRecord.head = side;
            }
        }
        /// endregion </Methods>
    };

    /// Track information with all additional indexes
    struct Track : public RawTrack
    {
        /// region <Fields>
    public:
        uint8_t sectorInterleaveTable[SECTORS_PER_TRACK] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F };
        RawSectorBytes* sectorsOrderedRef[SECTORS_PER_TRACK];
        AddressMarkRecord* sectorIDsOrderedRef[SECTORS_PER_TRACK];
        /// endregion </Fields>

        /// region <Properties>
    public:
        RawSectorBytes* getRawSector(uint8_t sector)
        {
            // Ensure sector number is in range [0..15]
            sector &= 0x0F;

            RawSectorBytes* result = &sectors[sector];

            return result;
        }

        AddressMarkRecord* getIDForSector(uint8_t sectorNo)
        {
            // Ensure sector number is in range [0..15]
            sectorNo &= 0x0F;

            AddressMarkRecord* result = sectorIDsOrderedRef[sectorNo];

            return result;
        }

        /// Returns pointer to sector data
        /// @param sectorNo (Sector numeration starts from 1)
        /// @return Sector
        uint8_t* getDataForSector(uint8_t sectorNo)
        {
            // Ensure sector number is in range [0..15]
            sectorNo &= 0x0F;

            uint8_t* result = sectorsOrderedRef[sectorNo]->data;

            return result;
        }
        /// endregion </Properties>

        /// region <Constructors / destructors>
        Track()
        {
            // Apply default interleaving and do re-index
            reindexSectors();
        }
        ~Track() = default;
        /// endregion </Constructors / destructors>

        /// region <Methods>
    public:
        void applyInterleaveTable(const uint8_t (&interleaveTable)[16])
        {
            // Copy interleave sector pattern used during formatting into track index to simplify sector lookups
            std::copy(std::begin(interleaveTable), std::end(interleaveTable), std::begin(sectorInterleaveTable));

            // Interleave table contains sector numerations starting from 1. We need numeration to be started from 0. So decreasing by 1.
            std::transform(std::begin(sectorInterleaveTable), std::end(sectorInterleaveTable), std::begin(sectorInterleaveTable), [](uint8_t element)
            {
                return element - 1;
            });

            // Trigger sector lookup table re-indexing
            reindexSectors();
        }

        /// Reindex sector access information using current interleave table
        void reindexSectors()
        {
            for (uint8_t i = 0; i < SECTORS_PER_TRACK; i++)
            {
                uint8_t sectorIdx = sectorInterleaveTable[i];
                RawSectorBytes* sectorRef = getRawSector(sectorIdx);

                sectorsOrderedRef[i] = sectorRef;                       // Store sector reference
                sectorIDsOrderedRef[i] = &sectorRef->address_record;    // Store ID record reference
            }
        }
        /// endregion </Methods>
    };

#pragma pack(pop)
    /// endregion </Types>

    /// region <Fields>
protected:
    bool _loaded = false;
    std::vector<Track> _tracks;


    uint8_t _cylinders;
    uint8_t _sides;
    /// endregion </Fields>

    /// region <Properties>
public:
    uint8_t getCylinders() { return _cylinders; }
    uint8_t getSides() { return _sides; }

    bool getLoaded() { return _loaded; }
    void setLoaded(bool loaded) { _loaded = loaded; }

    Track* getTrackForCylinderAndSide(uint8_t cylinder, uint8_t side)
    {
        Track* result = nullptr;

        if (cylinder < MAX_CYLINDERS && side < 2)
        {
            size_t trackNumber = cylinder * _sides + side;
            result = getTrack(trackNumber);
        }

        return result;
    }

    Track* getTrack(uint8_t track)
    {
        Track* result = nullptr;

        if (_tracks.size() > track)
        {
            result = &_tracks[track];
        }

        return result;
    }
    /// endregion </Properties>

    /// region <Constructors / destructors>
public:
    DiskImage(uint8_t cylinders, uint8_t sides)
    {
        _cylinders = cylinders > MAX_CYLINDERS ? MAX_CYLINDERS : cylinders;
        _sides = sides > 2 ? 2 : sides;

        // Allocate memory for disk image with selected characteristics
        allocateMemory(_cylinders, _sides);
    }
    DiskImage() = delete;

    virtual ~DiskImage()
    {
        releaseMemory();
    }
    /// endregion </Constructors / destructors>

    /// region <Helper methods>
protected:
    bool allocateMemory(uint8_t cylinders, uint8_t sides);
    void releaseMemory();
    /// endregion </Helper methods>
};


