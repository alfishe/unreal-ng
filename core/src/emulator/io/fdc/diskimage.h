#pragma once

#include "stdafx.h"

#include "common/dumphelper.h"
#include "emulator/io/fdc/fdc.h"
#include "trdos.h"
#include <algorithm>

// @see http://www.bitsavers.org/components/westernDigital/FD179X-01_Data_Sheet_Oct1979.pdf
// | Data in DR (Hex) | FD179X Interpretation in FM (DDEN = 1) | FD179X Interpretation in MFM (DDEN = 0) | Notes                                  |
// |------------------|-----------------------------------------|----------------------------------------|----------------------------------------|
// | 00-F4            | Write 00-F4 with clock = FF             | Write 00-F4 in MFM                     | Normal data bytes                      |
// | F5               | Not Allowed                             | Write A1* in MFM, preset CRC           | *Missing clock transition bits 4-5     |
// | F6               | Not Allowed                             | Write C2** in MFM                      | **Missing clock transition bits 3-4    |
// | F7               | Not Allowed                             | Generate 2 CRC bytes                   | Terminates CRC calculation             |
// | F8-FB            | Write F8-FB with clock = C7, preset CRC | Write F8-FB in MFM                     | FB=Data Mark, F8=Deleted Data Mark     |
// | FC               | Write FC with clock = D7                | Write FC in MFM                        | Index Address Mark (FM only)           |
// | FD               | Write FD with clock = FF                | Write FD in MFM                        | Unused in standard formats             |
// | FE               | Write FE with clock = C7, preset CRC    | Write FE in MFM                        | ID Address Mark (sector header)        |
// | FF               | Write FF with clock = FF                | Write FF in MFM                        | Filler/Gap byte                        |

class DiskImage
{
    /// region <Types>
public:
    // WD1793 supports only those sector sizes: 128, 256, 512, 1024
    enum SectorSizeEnum : uint8_t // Log2(<sector size>) - 7
    {
        SECTOR_SIZE_128 = 0,
        SECTOR_SIZE_256 = 1,
        SECTOR_SIZE_512 = 2,
        SECTOR_SIZE_1024 = 3
    };

#pragma pack(push, 1)
    /// This record is used by WD1793 to verify head positioning
    /// Used by READ_ADDRESS and READ_TRACK commands
    struct AddressMarkRecord // sizeof() = 7 bytes
    {
        uint8_t id_address_mark = 0xFE;
        uint8_t cylinder = 0x00;
        uint8_t head = 0x00;
        uint8_t sector = 0x00;
        uint8_t sector_size = SectorSizeEnum::SECTOR_SIZE_256;  // 0x01 - sector size 256 bytes. The only option for TR-DOS
        uint16_t id_crc = 0xFFFF;

        /// region <Methods>
    public:
        /// Resets AddressMarkRecord to it's default state
        void reset()
        {
            id_address_mark = 0xFE;
            cylinder = 0x00;
            head = 0x00;
            sector = 0x00;
            sector_size = SectorSizeEnum::SECTOR_SIZE_256;  // 0x01 - sector size 256 bytes. The only option for TR-DOS
            id_crc = 0xFFFF;

            // Total size verification (compile-time check)
            static_assert(sizeof(AddressMarkRecord) == 7, "AddressMarkRecord size mismatch! Check padding/alignment");
        }

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
    struct RawSectorBytes // sizeof() = 388 bytes
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
        void reset()
        {
            // 1. Sector start gap (22 bytes total)
            std::fill_n(gap0,   sizeof(gap0),   0x4E);              // 10 bytes
            std::fill_n(sync0,  sizeof(sync0),  0x00);              // 12 bytes

            // 2. Index / Address Mark Block (16 bytes total)
            std::fill_n(f5_token0, sizeof(f5_token0), 0xA1);        // 3 bytes
            address_record.reset();                                 // 7 bytes

            // 3. Gap between blocks (34 bytes total)
            std::fill_n(gap1,   sizeof(gap1),   0x4E);              // 22 bytes
            std::fill_n(sync1,  sizeof(sync1),  0x00);              // 12 bytes

            // 4. Data block (262 bytes total)
            std::fill_n(f5_token1, sizeof(f5_token1), 0xA1);        // 3 bytes
            data_address_mark = 0xFB;                               // 1 byte
            std::fill_n(data, sizeof(data), 0x00);                  // 256 bytes
            data_crc = 0xFFFF;                                      // 2 bytes

            // 5. Sector end gap (60 bytes)
            std::fill_n(gap2, sizeof(gap2), 0x4E);                  // 60 bytes

            // Total size verification (compile-time check)
            static_assert(sizeof(RawSectorBytes) == 388, "RawSectorBytes size mismatch! Check padding/alignment");
        }

        /// CRC is calculated for all sector data AND data_address_mark
        void recalculateDataCRC()
        {
            uint16_t crc = CRCHelper::crcWD1793(&data_address_mark, sizeof(data) + sizeof(data_address_mark));
            data_crc = crc;
        }

        /// Check if CRC valid
        bool isDataCRCValid()
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
        static constexpr const size_t SECTORS_PER_TRACK = 16;                              // TR-DOS uses 16 sectors layout
        static constexpr const size_t RAW_SECTOR_BYTES = sizeof(RawSectorBytes);           // 388 bytes expected
        static constexpr const size_t TRACK_BITMAP_SIZE_BYTES = (RAW_TRACK_SIZE + 7) / 8;  // 782 bytes expected
        static constexpr const size_t TRACK_END_GAP_BYTES = RAW_TRACK_SIZE - (RAW_SECTOR_BYTES * SECTORS_PER_TRACK); // 42 bytes expected
        /// endregion </Constants>

        // Fields
        RawSectorBytes sectors[SECTORS_PER_TRACK] = {};
        uint8_t endGap[TRACK_END_GAP_BYTES] = {};

        /// region <Constructors / destructors>
    public:
        RawTrack()
        {
            reset();
        }
        ~RawTrack() = default;  // Note: do not make it virtual since vtable will add overhead to sizeof()
        /// endregion </Constructors / destructors>

        /// region <Methods>
    public:
        void reset()
        {
            // 1. Reset all sectors (16 sectors × 388 bytes = 6208 bytes)
            for (auto& sector : sectors)
            {
                sector.reset(); // Calls RawSectorBytes::reset() we implemented earlier
            }

            // 2. Reset end gap (42 bytes)
            std::fill_n(endGap, sizeof(endGap), 0x4E);

            // Compile-time size verification
            static_assert(sizeof(sectors) == 16 * 388, "Sectors array size mismatch");
            static_assert(sizeof(endGap) == 42, "End gap size mismatch");
            static_assert(sizeof(RawTrack) == 6250, "RawTrack size mismatch");
        }


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

    /// Holds RawTrack data + meta information about disk imperfections
    struct FullTrack : public RawTrack
    {
        uint8_t clockMarksBitmap[TRACK_BITMAP_SIZE_BYTES] = {};
        uint8_t badBytesBitmap[TRACK_BITMAP_SIZE_BYTES] {};
    };

    /// Track information with all additional indexes
    struct Track : public FullTrack
    {
        /// region <Constants>

        // Default interleave table (1:1 mapping)
        static constexpr uint8_t DEFAULT_INTERLEAVE[SECTORS_PER_TRACK] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F };
        /// region </Constants>

        /// region <Fields>
    public:
        uint8_t sectorInterleaveTable[SECTORS_PER_TRACK] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F };
        RawSectorBytes* sectorsOrderedRef[SECTORS_PER_TRACK] = {};
        AddressMarkRecord* sectorIDsOrderedRef[SECTORS_PER_TRACK] = {};
        DiskImage* _diskImage = nullptr;

        /// region <Properties>
    public:
        DiskImage* getDiskImage() const
        {
            return _diskImage;
        }
        /// endregion </Properties>
        /// endregion </Fields>

        /// region <Properties>
    public:
        /// Gets a reference to the raw sector data
        /// 
        /// @param sector The sector number (0-15) to retrieve
        /// @return Pointer to RawSectorBytes structure containing the sector data
        /// 
        /// @note The sector number is masked with 0x0F to ensure it's in the range [0..15]
        /// @note The returned pointer points directly to the internal sector data array
        /// @warning The returned pointer should not be used to modify the sector structure directly
        /// 
        /// @see RawSectorBytes
        /// @see SECTORS_PER_TRACK
        RawSectorBytes* getRawSector(uint8_t sector)
        {
            // Ensure sector number is in range [0..15]
            sector &= 0x0F;

            RawSectorBytes* result = &sectors[sector];

            return result;
        }

        /// Gets a reference to the sector data
        /// 
        /// @param sectorNo The sector number (0-15) to retrieve
        /// @return Pointer to RawSectorBytes structure containing the sector data
        /// 
        /// @note The sector number is masked with 0x0F to ensure it's in the range [0..15]
        /// @note The returned pointer points directly to the internal sector data array
        /// @warning The returned pointer should not be used to modify the sector structure directly
        /// 
        /// @see RawSectorBytes
        /// @see SECTORS_PER_TRACK
        RawSectorBytes* getSector(uint8_t sectorNo)
        {
            sectorNo &= 0x0F;

            RawSectorBytes* result = sectorsOrderedRef[sectorNo];

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

        uint16_t getCRCForSector(uint8_t sectorNo)
        {
            uint16_t result = sectorsOrderedRef[sectorNo]->data_crc;

            return result;
        }

        void setCRCForSector(uint8_t sectorNo, uint16_t crc)
        {
            sectorsOrderedRef[sectorNo]->data_crc = crc;
        }

        void calculateDataCRCForSector(uint8_t sectorNo)
        {
            sectorsOrderedRef[sectorNo]->recalculateDataCRC();
        }

        uint8_t* getCRCForSectorAddress(uint8_t sectorNo)
        {
            uint8_t* result = (uint8_t*)&sectorsOrderedRef[sectorNo]->data_crc;

            return result;
        }

        /// Returns pointer to raw track data
        /// @param cylinder Cylinder number
        /// @param side Side number (0 or 1)
        /// @return Pointer to raw track data if track exists, nullptr otherwise
        /// @note The pointer points directly to the track's raw data buffer
        /// @warning The returned pointer should not be used to modify the track data directly
        uint8_t* getRawTrackData(uint8_t cylinder, uint8_t side)
        {
            if (!Track::isTrackValid(cylinder, side))
            {
                return nullptr;
            }

            size_t trackIndex = Track::calculateTrackIndex(cylinder, side);
            Track* track = getDiskImage()->getTrack(trackIndex);
            if (!track)
            {
                return nullptr;
            }
            return reinterpret_cast<uint8_t*>(track);
        }
        /// endregion </Properties>

        /// region <Constructors / destructors>
    public:
        Track() = default;
        Track(DiskImage* diskImage)
            : _diskImage(diskImage)
        {
            // Apply default interleaving and do re-index
            reset();
        }
        ~Track() = default;

        Track(const Track&) = delete;
        /*
        {
            std::cout << "Track COPY @" << this << " from @" << &other << "\n";
            // Check if sector buffers are being copied
        }
        */

        Track(Track&& other) = default;
        /*
        {
            std::cout << "Track MOVE @" << this << " from @" << &other << "\n";
        }
        */
        /// endregion </Constructors / destructors>

        /// region <Methods>
    public:
        /// Validates if track number is within valid range
        /// @param cylinder Cylinder number
        /// @param side Side number (0 or 1)
        /// @return True if track is valid, false otherwise
        static bool isTrackValid(uint8_t cylinder, uint8_t side)
        {
            return (cylinder <= MAX_CYLINDERS) && (side <= MAX_SIDES);
        }

        /// Calculates track index based on cylinder and side
        /// @param cylinder Cylinder number
        /// @param side Side number (0 or 1)
        /// @return Track index
        static size_t calculateTrackIndex(uint8_t cylinder, uint8_t side)
        {
            return cylinder * 2 + side;
        }

        void reset()
        {
            // Reset all sectors content
            for (RawSectorBytes& sector: sectors)
            {
                sector.reset();
            }

            // Re-apply default interleave (1:1)
            applyInterleaveTable(DEFAULT_INTERLEAVE);

            // Restore indexes
            reindexSectors();
        }

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

    /// Gets a reference to a specific track using physical cylinder and side coordinates
    /// 
    /// @param cylinder The physical cylinder number (0-79 for 80-track disks)
    /// @param side     The physical side number (0 or 1)
    /// @return Pointer to the Track object if it exists, nullptr otherwise
    /// 
    /// @note This method converts physical cylinder/side coordinates to a logical track number:
    ///       - Track number = (cylinder * sides) + side
    /// @note For example, on a double-sided disk:
    ///       - Cylinder 0, Side 0 = Track 0
    ///       - Cylinder 0, Side 1 = Track 1
    ///       - Cylinder 1, Side 0 = Track 2
    /// @note Maximum cylinder number is MAX_CYLINDERS - 1 (usually 79)
    /// @warning Returns nullptr if:
    ///         - Cylinder number is out of bounds (≥ MAX_CYLINDERS)
    ///         - Side number is invalid (≠ 0 or 1)
    ///         - Logical track number is out of bounds
    /// 
    /// @see getTrack
    /// @see Track
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

    /// Gets a reference to a specific track on the disk
    /// 
    /// @param track The track number to retrieve (logical track number)
    /// @return Pointer to the Track object if it exists, nullptr otherwise
    /// 
    /// @note Track numbers are logical and range from 0 to (cylinders * sides - 1)
    /// @note For example, on a double-sided disk with 80 cylinders:
    ///       - Track 0 = Head 0, Cylinder 0
    ///       - Track 1 = Head 1, Cylinder 0
    ///       - Track 2 = Head 0, Cylinder 1
    ///       - Track 159 = Head 1, Cylinder 79
    /// @warning Returns nullptr if the track number is out of bounds
    /// 
    /// @see getTrackForCylinderAndSide
    /// @see Track
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
        
        // Initialize tracks with DiskImage pointer
        for (Track& track : _tracks)
        {
            track._diskImage = this;
        }
        
        reset();
    }
    DiskImage() = delete;

    virtual ~DiskImage()
    {
        releaseMemory();
    }
    /// endregion </Constructors / destructors>

    /// region <Helper methods>
protected:
    void reset()
    {
        for (Track& track : _tracks)
        {
            track.reset();
        }

    }
    bool allocateMemory(uint8_t cylinders, uint8_t sides);
    void releaseMemory();
    /// endregion </Helper methods>

    /// region <Debug methods>
  public:
    std::string DumpSectorHex(uint8_t trackNo, uint8_t sectorNo)
    {
        Track* track = getTrack(trackNo);
        RawSectorBytes* sector = track->getRawSector(sectorNo);

        std::string result = DumpHelper::HexDumpBuffer(sector->data, TRD_SECTORS_SIZE_BYTES);

        return result;
    }
    /// endregion </Debug methods>
};
