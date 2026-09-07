#pragma once

#include "stdafx.h"

#include "common/dumphelper.h"
#include "emulator/io/fdc/fdc.h"
#include "trdos.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

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

/// Universal track model.
///
/// A track is a variable-length byte stream (what READ TRACK returns / WRITE TRACK accepts, offset 0 == index pulse)
/// plus one bit per byte telling whether that byte was written with a missing-clock pattern (A1 / C2 in MFM,
/// C7 / D7 clocks in FM). Sector structure is never stored - it is derived from the stream by Track::reindex(),
/// which produces a list of Sector index entries (views) in physical order.
///
/// This allows any layout the WD1793 can produce: any sector count, 128..1024 byte sectors, mixed sizes,
/// duplicate / missing sector numbers, ID-only sectors, wrong CRCs, deleted data marks, non-standard gaps,
/// track lengths from 3125 (FM) to 6464 (fast drive) bytes and above.
///
/// See docs/inprogress/2026-09-02-universal-track-model/track-model-design.md
class DiskImage
{
    /// region <Types>
public:
    /// Recording method of a track
    enum class Encoding : uint8_t
    {
        MFM = 0,  // Double density, 250 Kbps, A1/C2 sync marks
        FM = 1    // Single density, 125 Kbps, C7/D7 clock marks
    };

    // WD1793 supports only those sector sizes: 128, 256, 512, 1024
    enum SectorSizeEnum : uint8_t // Log2(<sector size>) - 7
    {
        SECTOR_SIZE_128 = 0,
        SECTOR_SIZE_256 = 1,
        SECTOR_SIZE_512 = 2,
        SECTOR_SIZE_1024 = 3
    };

#pragma pack(push, 1)
    /// ID field as it lies in the track stream: FE C H R N CRC(2). Overlay type - never allocated on its own
    /// except by tests. Used by READ_ADDRESS and READ_TRACK commands and by the sector index.
    struct AddressMarkRecord // sizeof() = 7 bytes
    {
        uint8_t id_address_mark = 0xFE;
        uint8_t cylinder = 0x00;
        uint8_t head = 0x00;
        uint8_t sector = 0x00;
        uint8_t sector_size = SectorSizeEnum::SECTOR_SIZE_256;  // 0x01 - sector size 256 bytes (TR-DOS default)
        uint16_t id_crc = 0xFFFF;                                // Stored in on-disk byte order (high byte first)

        /// region <Methods>
    public:
        /// Resets AddressMarkRecord to its default state
        void reset()
        {
            id_address_mark = 0xFE;
            cylinder = 0x00;
            head = 0x00;
            sector = 0x00;
            sector_size = SectorSizeEnum::SECTOR_SIZE_256;
            id_crc = 0xFFFF;

            static_assert(sizeof(AddressMarkRecord) == 7, "AddressMarkRecord size mismatch! Check padding/alignment");
        }

        /// Sector size in bytes encoded by this ID (WD1793 masks N to 2 bits)
        uint16_t getSectorSize() const { return static_cast<uint16_t>(128u << (sector_size & 0x03)); }

        /// CRC is calculated for all AddressMarkRecord fields starting from id_address_mark byte (MFM: preset after A1 A1 A1)
        void recalculateCRC()
        {
            id_crc = CRCHelper::crcWD1793(&id_address_mark, 5);
        }

        /// FM variant: CRC preset to 0xFFFF, no A1 preamble
        void recalculateCRCFM()
        {
            id_crc = CRCHelper::crcWD1793FM(&id_address_mark, 5);
        }

        /// Check if CRC valid (MFM)
        bool isCRCValid()
        {
            return CRCHelper::crcWD1793(&id_address_mark, 5) == id_crc;
        }

        /// Check if CRC valid (FM)
        bool isCRCValidFM()
        {
            return CRCHelper::crcWD1793FM(&id_address_mark, 5) == id_crc;
        }
        /// endregion </Methods>
    };
#pragma pack(pop)

    /// Data-driven description of a track layout used by Track::formatTrack()
    struct TrackFormatSpec
    {
        Encoding encoding = Encoding::MFM;
        size_t trackLength = MAX_TRACK_LEN;          // Bytes per revolution (6250 MFM / 3125 FM nominal)
        uint8_t sizeCode = SectorSizeEnum::SECTOR_SIZE_256;  // Applies to every sector unless sectorSizeCodes is set
        std::vector<uint8_t> sectorNumbers;          // Physical order == interleave. Empty => 1..16
        std::vector<uint8_t> sectorSizeCodes;        // Optional per-sector N override (mixed sizes)
        std::vector<uint8_t> sectorCylinders;        // Optional per-sector C override (copy protection)
        std::vector<uint8_t> sectorHeads;            // Optional per-sector H override
        std::vector<uint8_t> idOnly;                 // Optional per-sector flag: 1 => ID field without data field
        bool indexMark = false;                      // Write C2 C2 C2 FC (MFM) / FC (FM) at the index
        uint8_t gapIndex = 80;                       // Gap 4a before the index mark (only when indexMark)
        uint8_t gapPostIndex = 50;                   // Gap 1 after the index mark (only when indexMark)
        uint8_t gapPreID = 10;                       // 4E bytes before each ID sync (legacy gap0)
        uint8_t syncLength = 12;                     // 00 bytes before A1 A1 A1 / before FM marks (legacy sync0/sync1)
        uint8_t gapPostID = 22;                      // 4E bytes between ID CRC and data sync (legacy gap1)
        uint8_t gapPostData = 60;                    // 4E bytes after data CRC (legacy gap2)
        uint8_t gapFill = 0x4E;                      // FM formats use 0xFF
        uint8_t dataFill = 0x00;
        uint8_t dataMark = 0xFB;

        /// Exact legacy TR-DOS layout: 16 x (10+12+3+7+22+12+3+1+256+2+60 = 388) = 6208 + 42 bytes end gap = 6250
        /// @param order Sector numbers in physical order (1-based, e.g. TR-DOS 1:2 interleave). nullptr => 1..16
        /// @param count Number of entries in order
        static TrackFormatSpec trdos(const uint8_t* order = nullptr, size_t count = 16)
        {
            TrackFormatSpec spec;
            spec.sectorNumbers.resize(count);
            for (size_t i = 0; i < count; i++)
            {
                spec.sectorNumbers[i] = order ? order[i] : static_cast<uint8_t>(i + 1);
            }
            return spec;
        }

        /// Generic IBM System 34 style MFM layout with index mark
        static TrackFormatSpec ibm(uint8_t sectors, uint8_t sizeCode, uint8_t firstSector = 1, uint8_t gap3 = 0x2A,
                                   uint8_t filler = 0xE5)
        {
            TrackFormatSpec spec;
            spec.sizeCode = sizeCode;
            spec.indexMark = true;
            spec.gapPreID = 0;         // gap3 already precedes the next sector
            spec.gapPostID = 22;
            spec.gapPostData = gap3;
            spec.dataFill = filler;
            spec.sectorNumbers.resize(sectors);
            for (uint8_t i = 0; i < sectors; i++)
            {
                spec.sectorNumbers[i] = static_cast<uint8_t>(firstSector + i);
            }
            return spec;
        }

        /// ZX Spectrum +3 / +3DOS: 9 x 512, sectors 1..9, gap3 42, filler E5
        static TrackFormatSpec plus3() { return ibm(9, SectorSizeEnum::SECTOR_SIZE_512, 1, 0x2A, 0xE5); }

        /// DISCiPLE / +D (MGT): 10 x 512, sectors 1..10
        static TrackFormatSpec plusD() { return ibm(10, SectorSizeEnum::SECTOR_SIZE_512, 1, 0x18, 0x00); }

        /// IBM 3740 style single density for 5.25"/3.5" drives at 125 Kbps: 16 x 128, FM, 3125 bytes
        static TrackFormatSpec ibm3740()
        {
            TrackFormatSpec spec = ibm(16, SectorSizeEnum::SECTOR_SIZE_128, 1, 27, 0xE5);
            spec.encoding = Encoding::FM;
            spec.trackLength = NOMINAL_TRACK_LEN_FM;
            spec.gapFill = 0xFF;
            spec.syncLength = 6;
            spec.gapIndex = 40;
            spec.gapPostIndex = 26;
            spec.gapPostID = 11;
            return spec;
        }

        size_t sectorCount() const { return sectorNumbers.size(); }

        uint8_t sizeCodeFor(size_t i) const
        {
            return (i < sectorSizeCodes.size()) ? sectorSizeCodes[i] : sizeCode;
        }

        bool isIdOnly(size_t i) const { return i < idOnly.size() && idOnly[i] != 0; }

        /// Number of stream bytes one sector occupies with this spec
        size_t bytesPerSector(size_t i) const
        {
            const size_t marks = (encoding == Encoding::MFM) ? 3 : 0;  // A1 A1 A1 preamble
            size_t bytes = gapPreID + syncLength + marks + 7;           // ID field
            if (!isIdOnly(i))
            {
                bytes += gapPostID + syncLength + marks + 1 + (128u << (sizeCodeFor(i) & 0x03)) + 2;
            }
            bytes += gapPostData;
            return bytes;
        }

        size_t indexMarkBytes() const
        {
            if (!indexMark) return 0;
            const size_t marks = (encoding == Encoding::MFM) ? 3 : 0;
            return gapIndex + syncLength + marks + 1 + gapPostIndex;
        }

        size_t totalBytes() const
        {
            size_t bytes = indexMarkBytes();
            for (size_t i = 0; i < sectorNumbers.size(); i++)
            {
                bytes += bytesPerSector(i);
            }
            return bytes;
        }

        /// True when the whole layout fits into trackLength
        bool fits() const { return totalBytes() <= trackLength; }
    };

    /// Index entry + view over one sector inside the raw track stream.
    /// Never owns data: pointers reference the RawTrack buffer and stay valid until the track is
    /// resized / re-formatted / re-loaded.
    struct Sector
    {
        static constexpr uint32_t NO_OFFSET = 0xFFFFFFFFu;

        // Position in the stream
        uint32_t idamOffset = NO_OFFSET;  // Offset of the 0xFE byte (A1 A1 A1 precede it in MFM)
        uint32_t damOffset = NO_OFFSET;   // Offset of the DAM byte (F8..FB) or NO_OFFSET
        uint32_t dataOffset = NO_OFFSET;  // Offset of the first data byte
        uint16_t dataSize = 0;            // 128 << (N & 3)

        // Views
        AddressMarkRecord* id = nullptr;  // Overlay at idamOffset
        uint8_t* data = nullptr;          // nullptr when there is no data field

        // Derived status
        bool hasData = false;
        bool deleted = false;             // DAM == 0xF8
        bool idCrcValid = false;
        bool dataCrcValid = false;
        bool dirty = false;               // Set by Track::writeSectorData and WD1793 WRITE SECTOR
        Encoding encoding = Encoding::MFM;

        /// region <ID field accessors>
        uint8_t cylinder() const { return id ? id->cylinder : 0; }
        uint8_t head() const { return id ? id->head : 0; }
        uint8_t number() const { return id ? id->sector : 0; }
        uint8_t sizeCode() const { return id ? id->sector_size : 0; }
        /// endregion </ID field accessors>

        /// region <Data field accessors>
        uint8_t dataAddressMark() const { return hasData ? *(data - 1) : 0; }

        void setDataAddressMark(uint8_t dam)
        {
            if (hasData)
            {
                *(data - 1) = dam;
                deleted = (dam == 0xF8);
            }
        }

        /// Data CRC as stored in the stream (same in-memory representation as CRCHelper::crcWD1793 returns)
        uint16_t dataCRC() const
        {
            uint16_t crc = 0xFFFF;
            if (hasData)
            {
                std::memcpy(&crc, data + dataSize, sizeof(crc));
            }
            return crc;
        }

        void setDataCRC(uint16_t crc)
        {
            if (hasData)
            {
                std::memcpy(data + dataSize, &crc, sizeof(crc));
                dataCrcValid = isDataCRCValid();
            }
        }

        uint16_t computeDataCRC() const
        {
            if (!hasData) return 0xFFFF;
            uint8_t* start = data - 1;  // CRC covers DAM + data
            const uint16_t len = static_cast<uint16_t>(dataSize + 1);
            return (encoding == Encoding::MFM) ? CRCHelper::crcWD1793(start, len) : CRCHelper::crcWD1793FM(start, len);
        }

        /// CRC is calculated for data_address_mark AND all sector data
        void recalculateDataCRC()
        {
            if (!hasData) return;
            uint16_t crc = computeDataCRC();
            std::memcpy(data + dataSize, &crc, sizeof(crc));
            dataCrcValid = true;
        }

        /// Check if data CRC valid
        bool isDataCRCValid()
        {
            if (!hasData) return false;
            bool result = computeDataCRC() == dataCRC();
            dataCrcValid = result;
            return result;
        }

        void recalculateIDCRC()
        {
            if (!id) return;
            if (encoding == Encoding::MFM) id->recalculateCRC(); else id->recalculateCRCFM();
            idCrcValid = true;
        }

        bool isIDCRCValid()
        {
            if (!id) return false;
            bool result = (encoding == Encoding::MFM) ? id->isCRCValid() : id->isCRCValidFM();
            idCrcValid = result;
            return result;
        }
        /// endregion </Data field accessors>
    };

    /// Raw track storage: variable-length byte stream + clock-mark bitmap + optional weak-bit bitmap.
    /// Contains no structural information about sectors.
    struct RawTrack
    {
        /// region <Constants>
    public:
        /// 200ms per disk revolution, 4us per bit => 32 us per byte. So 200000 / 32 = 6250 bytes per track (MFM nominal).
        /// Real drives / images use 6208...6464 bytes; FM tracks are 3125 bytes nominal.
        static constexpr const size_t RAW_TRACK_SIZE = MAX_TRACK_LEN;              // Nominal MFM length (compat name)
        static constexpr const size_t DEFAULT_TRACK_SIZE_MFM = MAX_TRACK_LEN;
        static constexpr const size_t DEFAULT_TRACK_SIZE_FM = NOMINAL_TRACK_LEN_FM;
        static constexpr const size_t MIN_TRACK_SIZE = 64;
        static constexpr const size_t MAX_TRACK_SIZE = 12500;
        static constexpr const size_t SECTORS_PER_TRACK = 16;                       // TR-DOS default (formatting only)
        static constexpr const size_t MAX_SECTORS_PER_TRACK = 255;
        static constexpr const size_t DAM_SEARCH_WINDOW_MFM = 43;                   // Bytes after ID CRC (datasheet)
        static constexpr const size_t DAM_SEARCH_WINDOW_FM = 30;
        static constexpr const size_t TRACK_BITMAP_SIZE_BYTES = (RAW_TRACK_SIZE + 7) / 8;  // Bitmap size for the nominal track
        /// endregion </Constants>

        /// region <Fields>
    protected:
        std::vector<uint8_t> _raw;        // Byte stream, offset 0 == index pulse
        std::vector<uint8_t> _clock;      // (rawSize + 7) / 8 bytes; bit i = byte i carries a missing-clock mark
        std::vector<uint8_t> _weak;       // Empty, or (rawSize + 7) / 8 bytes; bit i = byte i is weak / unreliable
        Encoding _encoding = Encoding::MFM;
        /// endregion </Fields>

        /// region <Constructors / destructors>
    public:
        RawTrack() { allocateRaw(DEFAULT_TRACK_SIZE_MFM, Encoding::MFM, 0x4E); }

        // No user-declared destructor: the implicit move operations must stay available so that moving a Track
        // moves the heap buffers (and keeps Sector pointers valid) instead of copying them.
        RawTrack(const RawTrack&) = default;
        RawTrack& operator=(const RawTrack&) = default;
        RawTrack(RawTrack&&) noexcept = default;
        RawTrack& operator=(RawTrack&&) noexcept = default;
        /// endregion </Constructors / destructors>

        /// region <Properties>
    public:
        Encoding encoding() const { return _encoding; }
        size_t rawSize() const { return _raw.size(); }
        uint8_t* rawData() { return _raw.data(); }
        const uint8_t* rawData() const { return _raw.data(); }

        const std::vector<uint8_t>& clockBitmap() const { return _clock; }
        std::vector<uint8_t>& clockBitmap() { return _clock; }

        bool clockMark(size_t offset) const
        {
            return offset < _raw.size() && (_clock[offset >> 3] & (1u << (offset & 7))) != 0;
        }

        void setClockMark(size_t offset, bool on)
        {
            if (offset >= _raw.size()) return;
            uint8_t& byte = _clock[offset >> 3];
            const uint8_t mask = static_cast<uint8_t>(1u << (offset & 7));
            if (on) byte |= mask; else byte &= static_cast<uint8_t>(~mask);
        }

        bool hasClockMarks() const
        {
            for (uint8_t b : _clock) if (b) return true;
            return false;
        }

        bool hasWeakBits() const { return !_weak.empty(); }

        bool weakByte(size_t offset) const
        {
            return !_weak.empty() && offset < _raw.size() && (_weak[offset >> 3] & (1u << (offset & 7))) != 0;
        }

        void setWeakByte(size_t offset, bool on)
        {
            if (offset >= _raw.size()) return;
            if (_weak.empty()) _weak.assign(_clock.size(), 0);
            uint8_t& byte = _weak[offset >> 3];
            const uint8_t mask = static_cast<uint8_t>(1u << (offset & 7));
            if (on) byte |= mask; else byte &= static_cast<uint8_t>(~mask);
        }

        const std::vector<uint8_t>& weakBitmap() const { return _weak; }
        /// endregion </Properties>

        /// region <Helper methods>
    protected:
        static size_t clampTrackSize(size_t size)
        {
            return std::min(std::max(size, MIN_TRACK_SIZE), MAX_TRACK_SIZE);
        }

        /// (Re)allocate the stream. Clears bitmaps. Does not touch the index (RawTrack has none).
        void allocateRaw(size_t size, Encoding enc, uint8_t fill)
        {
            size = clampTrackSize(size);
            _raw.assign(size, fill);
            _clock.assign((size + 7) / 8, 0);
            _weak.clear();
            _encoding = enc;
        }
        /// endregion </Helper methods>
    };

    /// Kept for source compatibility: the clock/weak bitmaps now live in RawTrack
    using FullTrack = RawTrack;

    /// Track = raw stream + sector index + change tracking
    struct Track : public RawTrack
    {
        /// region <Fields>
    public:
        DiskImage* _diskImage = nullptr;

    protected:
        std::vector<Sector> _sectors;         // Physical (stream) order
        uint32_t _indexMarkOffset = Sector::NO_OFFSET;
        uint8_t _cylinder = 0;                // Physical position of this track inside the image
        uint8_t _side = 0;

        friend class DiskImage;
        /// endregion </Fields>

        /// region <Properties>
    public:
        DiskImage* getDiskImage() const { return _diskImage; }
        uint8_t cylinder() const { return _cylinder; }
        uint8_t side() const { return _side; }
        /// endregion </Properties>

        /// region <Change Tracking>
    protected:
        bool _dirty = false;
        bool _rawTrackDirty = false;  // For WRITE_TRACK operations (MFM layout changes)

        /// Marks track as dirty - protected, called by sector write accessors
        void markDirty();

        /// Marks track as dirty due to raw MFM write - called by WD1793 WRITE_TRACK
        void markRawTrackDirty();

        // Grant WD1793 friend access for WRITE_TRACK / WRITE_SECTOR
        friend class WD1793;

    public:
        /// Check if track has been modified (sector or raw level)
        bool isDirty() const { return _dirty; }

        /// Check if raw MFM data was written (WRITE_TRACK operation)
        bool isRawTrackDirty() const { return _rawTrackDirty; }

        /// Check if specific sector is dirty
        /// @param sectorNo Logical sector index (0-based, ID number - 1)
        bool isSectorDirty(uint8_t sectorNo) const
        {
            const Sector* sector = findSector(static_cast<uint8_t>(sectorNo + 1));
            return sector != nullptr && sector->dirty;
        }

        /// Check if any sector in track is marked dirty
        bool hasAnySectorDirty() const
        {
            for (const Sector& sector : _sectors) if (sector.dirty) return true;
            return false;
        }

        /// Mark specific sector as dirty (with content-change detection)
        /// @param sectorNo Logical sector index (0-based, ID number - 1)
        /// @param newData New data to compare against current sector data
        /// @param len Length of data to compare
        void markSectorDirtyIfChanged(uint8_t sectorNo, const uint8_t* newData, size_t len)
        {
            Sector* sector = getSector(sectorNo);
            if (sector && sector->hasData && len <= sector->dataSize)
            {
                if (std::memcmp(sector->data, newData, len) != 0)
                {
                    sector->dirty = true;
                    markDirty();
                }
            }
        }

        /// Write sector data with content-change detection. Recalculates the data CRC.
        /// @param sectorNo Logical sector index (0-based, ID number - 1)
        /// @param src Source data buffer
        /// @param len Length of data to copy (clamped to the sector size)
        void writeSectorData(uint8_t sectorNo, const uint8_t* src, size_t len)
        {
            Sector* sector = getSector(sectorNo);
            if (sector && sector->hasData)
            {
                if (len > sector->dataSize) len = sector->dataSize;

                if (std::memcmp(sector->data, src, len) != 0)
                {
                    std::memcpy(sector->data, src, len);
                    sector->recalculateDataCRC();
                    sector->dirty = true;
                    markDirty();
                }
            }
        }

        /// Clear dirty flags for track and all sectors (called after save)
        void markClean()
        {
            _dirty = false;
            _rawTrackDirty = false;
            for (Sector& sector : _sectors) sector.dirty = false;
        }
        /// endregion </Change Tracking>

        /// region <Sector index>
    public:
        size_t sectorCount() const { return _sectors.size(); }
        const std::vector<Sector>& sectors() const { return _sectors; }
        std::vector<Sector>& sectors() { return _sectors; }
        uint32_t indexMarkOffset() const { return _indexMarkOffset; }

        /// Sector by physical (stream) index
        Sector* getRawSector(size_t physicalIndex)
        {
            return physicalIndex < _sectors.size() ? &_sectors[physicalIndex] : nullptr;
        }

        const Sector* getRawSector(size_t physicalIndex) const
        {
            return physicalIndex < _sectors.size() ? &_sectors[physicalIndex] : nullptr;
        }

        /// First sector (in stream order) whose ID field carries the given sector number
        Sector* findSector(uint8_t number)
        {
            for (Sector& sector : _sectors) if (sector.number() == number) return &sector;
            return nullptr;
        }

        const Sector* findSector(uint8_t number) const
        {
            for (const Sector& sector : _sectors) if (sector.number() == number) return &sector;
            return nullptr;
        }

        /// Next sector with the given number after stream offset fromOffset (wrapping around the index)
        Sector* findSector(uint8_t number, size_t fromOffset)
        {
            return findSector(-1, -1, number, fromOffset);
        }

        /// Rotational search as performed by a WD1793 Type II command:
        /// the first ID field after fromOffset (wrapping) whose C / H / R fields match.
        /// @param cyl  Cylinder to match, or -1 for any
        /// @param side Head to match, or -1 for any (side compare flag not set)
        /// @param number Sector number to match
        /// @param fromOffset Current head position (stream offset)
        Sector* findSector(int cyl, int side, uint8_t number, size_t fromOffset)
        {
            const size_t count = _sectors.size();
            if (count == 0) return nullptr;

            size_t start = 0;
            while (start < count && _sectors[start].idamOffset < fromOffset) start++;

            for (size_t n = 0; n < count; n++)
            {
                Sector& sector = _sectors[(start + n) % count];
                if (sector.number() != number) continue;
                if (cyl >= 0 && sector.cylinder() != static_cast<uint8_t>(cyl)) continue;
                if (side >= 0 && sector.head() != static_cast<uint8_t>(side)) continue;
                return &sector;
            }

            return nullptr;
        }

        /// Next ID field physically under the head after fromOffset (wrapping). Used by READ ADDRESS.
        Sector* nextSector(size_t fromOffset)
        {
            const size_t count = _sectors.size();
            if (count == 0) return nullptr;

            for (size_t i = 0; i < count; i++)
            {
                if (_sectors[i].idamOffset >= fromOffset) return &_sectors[i];
            }
            return &_sectors[0];
        }

        /// Number of stream bytes from fromOffset to the ID field of sector (wrapping)
        size_t bytesUntil(const Sector& sector, size_t fromOffset) const
        {
            const size_t len = _raw.size();
            if (len == 0) return 0;
            fromOffset %= len;
            return (sector.idamOffset >= fromOffset) ? sector.idamOffset - fromOffset
                                                     : len - fromOffset + sector.idamOffset;
        }

        /// region <Compatibility accessors (0-based logical index == ID number - 1)>

        /// Sector whose ID field number is sectorNo + 1, nullptr when absent
        Sector* getSector(uint8_t sectorNo) { return findSector(static_cast<uint8_t>(sectorNo + 1)); }

        AddressMarkRecord* getIDForSector(uint8_t sectorNo)
        {
            Sector* sector = getSector(sectorNo);
            return sector ? sector->id : nullptr;
        }

        /// Returns pointer to sector data, nullptr when the sector is absent or has no data field
        uint8_t* getDataForSector(uint8_t sectorNo)
        {
            Sector* sector = getSector(sectorNo);
            return (sector && sector->hasData) ? sector->data : nullptr;
        }

        /// Deprecated aliases - the index is always rebuilt from the stream
        void reindexSectors() { reindex(); }
        void reindexFromIDAM() { reindex(); }
        /// endregion </Compatibility accessors>
        /// endregion </Sector index>

        /// region <Constructors / destructors>
    public:
        Track() = default;
        Track(DiskImage* diskImage) : _diskImage(diskImage) { reset(); }

        Track(const Track&) = delete;
        Track& operator=(const Track&) = delete;

        // Moving keeps the heap buffers, so Sector pointers into _raw stay valid
        Track(Track&& other) noexcept = default;
        Track& operator=(Track&& other) noexcept = default;
        /// endregion </Constructors / destructors>

        /// region <Methods>
    public:
        /// Validates if track number is within valid range
        static bool isTrackValid(uint8_t cylinder, uint8_t side)
        {
            return (cylinder <= MAX_CYLINDERS) && (side <= MAX_SIDES);
        }

        /// Calculates track index based on cylinder and side (double sided layout)
        static size_t calculateTrackIndex(uint8_t cylinder, uint8_t side)
        {
            return cylinder * 2 + side;
        }

        /// Blank TR-DOS formatted track (sectors 1..16 in physical order) at this track's physical position
        void reset()
        {
            formatTrack(_cylinder, _side);
            markClean();
        }

        /// region <Storage>

        /// Reallocate the stream (gap filled), clear bitmaps and index
        void resizeRaw(size_t newSize, Encoding enc = Encoding::MFM, uint8_t fill = 0x4E)
        {
            allocateRaw(newSize, enc, fill);
            reindex();
        }

        /// Replace the stream with a copy of data (clock bitmap cleared - call setClockBitmap afterwards)
        void setRaw(const uint8_t* data, size_t len, Encoding enc = Encoding::MFM)
        {
            allocateRaw(len, enc, 0x4E);
            std::memcpy(_raw.data(), data, std::min(len, _raw.size()));
            reindex();
        }

        /// Replace the clock bitmap (bit i == byte i carries a missing-clock mark) and rebuild the index
        void setClockBitmap(const uint8_t* bitmap, size_t len)
        {
            std::fill(_clock.begin(), _clock.end(), 0);
            std::memcpy(_clock.data(), bitmap, std::min(len, _clock.size()));
            reindex();
        }
        /// endregion </Storage>

        /// region <Formatting>

        /// Low-level format with the default TR-DOS layout (16 x 256, sector numbers 1..16 in physical order)
        void formatTrack(uint8_t cylinder, uint8_t side)
        {
            formatTrack(cylinder, side, TrackFormatSpec::trdos());
        }

        /// Low-level format according to spec. All sector data is filled with spec.dataFill, all CRCs are valid,
        /// every sync / address mark byte gets its clock mark, index is rebuilt.
        void formatTrack(uint8_t cylinder, uint8_t side, const TrackFormatSpec& spec)
        {
            const bool mfm = (spec.encoding == Encoding::MFM);
            allocateRaw(spec.trackLength, spec.encoding, spec.gapFill);
            const size_t len = _raw.size();
            size_t pos = 0;

            auto put = [&](uint8_t value, bool clock)
            {
                if (pos < len)
                {
                    _raw[pos] = value;
                    setClockMark(pos, clock);
                    pos++;
                }
            };
            auto fill = [&](size_t count, uint8_t value)
            {
                for (size_t i = 0; i < count; i++) put(value, false);
            };
            auto putCRC = [&](size_t start)
            {
                const uint16_t crcLen = static_cast<uint16_t>(pos - start);
                // crcWD1793 returns the CRC byte-swapped, so that storing it as a little-endian uint16_t yields the
                // on-disk order (true high byte first). Emit the same order byte by byte.
                uint16_t crc = mfm ? CRCHelper::crcWD1793(&_raw[start], crcLen) : CRCHelper::crcWD1793FM(&_raw[start], crcLen);
                put(static_cast<uint8_t>(crc & 0xFF), false);  // True CRC high byte
                put(static_cast<uint8_t>(crc >> 8), false);    // True CRC low byte
            };
            auto putSyncAndMarks = [&](uint8_t mark)
            {
                fill(spec.syncLength, 0x00);
                if (mfm)
                {
                    put(0xA1, true); put(0xA1, true); put(0xA1, true);
                    put(mark, false);
                }
                else
                {
                    put(mark, true);  // FM marks are recognised by their clock pattern
                }
            };

            if (spec.indexMark)
            {
                fill(spec.gapIndex, spec.gapFill);
                fill(spec.syncLength, 0x00);
                if (mfm)
                {
                    put(0xC2, true); put(0xC2, true); put(0xC2, true);
                    put(0xFC, false);
                }
                else
                {
                    put(0xFC, true);
                }
                fill(spec.gapPostIndex, spec.gapFill);
            }

            for (size_t i = 0; i < spec.sectorNumbers.size(); i++)
            {
                const uint8_t sizeCode = spec.sizeCodeFor(i);
                const uint8_t c = (i < spec.sectorCylinders.size()) ? spec.sectorCylinders[i] : cylinder;
                const uint8_t h = (i < spec.sectorHeads.size()) ? spec.sectorHeads[i] : side;

                fill(spec.gapPreID, spec.gapFill);
                putSyncAndMarks(0xFE);
                const size_t idStart = pos - 1;
                put(c, false);
                put(h, false);
                put(spec.sectorNumbers[i], false);
                put(sizeCode, false);
                putCRC(idStart);

                if (!spec.isIdOnly(i))
                {
                    fill(spec.gapPostID, spec.gapFill);
                    putSyncAndMarks(spec.dataMark);
                    const size_t damStart = pos - 1;
                    fill(128u << (sizeCode & 0x03), spec.dataFill);
                    putCRC(damStart);
                }

                fill(spec.gapPostData, spec.gapFill);
            }

            reindex();
        }

        /// Re-format as a blank TR-DOS track with the given physical sector order (1-based sector numbers)
        void applyInterleaveTable(const uint8_t* order, size_t count)
        {
            formatTrack(_cylinder, _side, TrackFormatSpec::trdos(order, count));
        }

        template <size_t N>
        void applyInterleaveTable(const uint8_t (&order)[N])
        {
            applyInterleaveTable(order, N);
        }
        /// endregion </Formatting>

        /// region <Scanner>

        /// Rebuild the sector index from the stream.
        /// MFM: sync = A1 A1 A1 carrying clock marks (plain byte match when the track has no clock information),
        ///      mark byte follows: FE = ID field, F8..FB = data field, C2 C2 C2 FC = index mark.
        /// FM:  mark = FE / F8..FB / FC byte carrying a clock mark (fallback: preceded by two 0x00 bytes).
        /// Duplicates are kept, nothing is sorted. Data fields must start within the datasheet window after the ID CRC
        /// and must fit entirely before the end of the stream, otherwise the sector is indexed as ID-only.
        void reindex()
        {
            _sectors.clear();
            _indexMarkOffset = Sector::NO_OFFSET;

            const size_t len = _raw.size();
            const bool mfm = (_encoding == Encoding::MFM);
            const bool useClock = hasClockMarks();
            const size_t window = mfm ? DAM_SEARCH_WINDOW_MFM : DAM_SEARCH_WINDOW_FM;

            // Sync detector: returns true when a mark byte sits at markPos
            auto isMarkAt = [&](size_t markPos) -> bool
            {
                if (markPos >= len) return false;
                if (mfm)
                {
                    if (markPos < 3) return false;
                    const size_t s = markPos - 3;
                    const bool a1 = _raw[s] == 0xA1 && _raw[s + 1] == 0xA1 && _raw[s + 2] == 0xA1;
                    if (!a1) return false;
                    return !useClock || (clockMark(s) && clockMark(s + 1) && clockMark(s + 2));
                }
                else
                {
                    if (useClock) return clockMark(markPos);
                    return markPos >= 2 && _raw[markPos - 1] == 0x00 && _raw[markPos - 2] == 0x00;
                }
            };
            auto isIndexMarkAt = [&](size_t markPos) -> bool
            {
                if (markPos >= len || _raw[markPos] != 0xFC) return false;
                if (mfm)
                {
                    if (markPos < 3) return false;
                    const size_t s = markPos - 3;
                    const bool c2 = _raw[s] == 0xC2 && _raw[s + 1] == 0xC2 && _raw[s + 2] == 0xC2;
                    if (!c2) return false;
                    return !useClock || (clockMark(s) && clockMark(s + 1) && clockMark(s + 2));
                }
                return isMarkAt(markPos);
            };

            size_t pos = mfm ? 3 : 0;
            while (pos < len && _sectors.size() < MAX_SECTORS_PER_TRACK)
            {
                const uint8_t byte = _raw[pos];

                if (byte == 0xFC && _indexMarkOffset == Sector::NO_OFFSET && isIndexMarkAt(pos))
                {
                    _indexMarkOffset = static_cast<uint32_t>(pos);
                    pos++;
                    continue;
                }

                if (byte != 0xFE || !isMarkAt(pos))
                {
                    pos++;
                    continue;
                }

                // ID field: FE C H R N CRC(2) must be complete
                if (pos + 7 > len) break;

                Sector sector;
                sector.encoding = _encoding;
                sector.idamOffset = static_cast<uint32_t>(pos);
                sector.id = reinterpret_cast<AddressMarkRecord*>(&_raw[pos]);
                sector.idCrcValid = mfm ? sector.id->isCRCValid() : sector.id->isCRCValidFM();
                sector.dataSize = sector.id->getSectorSize();

                // Data field: DAM must be found within the datasheet window after the ID CRC
                const size_t idEnd = pos + 7;
                const size_t lastDamPos = idEnd + window;
                size_t resume = idEnd;

                for (size_t dam = idEnd; dam <= lastDamPos && dam < len; dam++)
                {
                    const uint8_t mark = _raw[dam];
                    if (mark < 0xF8 || mark > 0xFB || !isMarkAt(dam)) continue;

                    const size_t dataStart = dam + 1;
                    if (dataStart + sector.dataSize + 2 <= len)
                    {
                        sector.hasData = true;
                        sector.damOffset = static_cast<uint32_t>(dam);
                        sector.dataOffset = static_cast<uint32_t>(dataStart);
                        sector.data = &_raw[dataStart];
                        sector.deleted = (mark == 0xF8);
                        sector.dataCrcValid = sector.isDataCRCValid();
                        if (!useClock) resume = dataStart + sector.dataSize + 2;  // Do not scan inside data without clock info
                    }
                    break;
                }

                _sectors.push_back(sector);
                pos = resume;
            }
        }
        /// endregion </Scanner>
        /// endregion </Methods>
    };
    /// endregion </Types>

    /// region <Fields>
protected:
    bool _loaded = false;
    bool _dirty = false;  // Change tracking - set when any track is modified
    std::vector<Track> _tracks;
    std::string _filePath;  // Source file path (set during load, used for tracking)

    uint8_t _cylinders;
    uint8_t _sides;

    /// Marks disk image as dirty - protected, called by Track
    void markDirty() { _dirty = true; }

    // Grant Track friend access for dirty propagation
    friend struct Track;
    /// endregion </Fields>

    /// region <Change Tracking>
public:
    /// Check if disk image has been modified
    bool isDirty() const { return _dirty; }

    /// Recompute dirty state from all tracks
    bool computeDirtyState() const
    {
        for (const Track& track : _tracks)
        {
            if (track.isDirty()) return true;
        }
        return false;
    }

    /// Clear dirty flags for disk and all tracks (called after save)
    void markClean()
    {
        _dirty = false;
        for (Track& track : _tracks)
        {
            track.markClean();
        }
    }
    /// endregion </Change Tracking>

    /// region <Properties>
public:
    const std::string& getFilePath() const { return _filePath; }
    void setFilePath(const std::string& path) { _filePath = path; }
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
    /// @note Track number = (cylinder * sides) + side
    Track* getTrackForCylinderAndSide(uint8_t cylinder, uint8_t side)
    {
        Track* result = nullptr;

        if (cylinder < _cylinders && side < _sides)
        {
            size_t trackNumber = cylinder * _sides + side;
            result = getTrack(trackNumber);
        }

        return result;
    }

    /// Gets a reference to a specific track on the disk
    ///
    /// @param track The logical track number (0 .. cylinders * sides - 1)
    /// @return Pointer to the Track object if it exists, nullptr otherwise
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
    /// Blank image with every track formatted as an empty TR-DOS track (16 x 256, sectors 1..16)
    DiskImage(uint8_t cylinders, uint8_t sides)
    {
        _cylinders = cylinders > MAX_CYLINDERS ? MAX_CYLINDERS : cylinders;
        _sides = sides > 2 ? 2 : sides;

        allocateMemory(_cylinders, _sides);
    }

    /// Blank image with every track formatted according to spec
    DiskImage(uint8_t cylinders, uint8_t sides, const TrackFormatSpec& spec)
    {
        _cylinders = cylinders > MAX_CYLINDERS ? MAX_CYLINDERS : cylinders;
        _sides = sides > 2 ? 2 : sides;

        allocateMemory(_cylinders, _sides);
        formatAll(spec);
    }

    DiskImage() = delete;

    virtual ~DiskImage()
    {
        releaseMemory();
    }
    /// endregion </Constructors / destructors>

    /// region <Helper methods>
public:
    /// Low-level format of every track with the given layout (data cleared, dirty flags cleared)
    void formatAll(const TrackFormatSpec& spec)
    {
        for (Track& track : _tracks)
        {
            track.formatTrack(track._cylinder, track._side, spec);
            track.markClean();
        }
        _dirty = false;
    }

protected:
    void reset()
    {
        for (Track& track : _tracks)
        {
            track.reset();
        }
        _dirty = false;
    }
    bool allocateMemory(uint8_t cylinders, uint8_t sides);
    void releaseMemory();
    /// endregion </Helper methods>

    /// region <Debug methods>
  public:
    std::string DumpSectorHex(uint8_t trackNo, uint8_t sectorNo)
    {
        std::string result;

        Track* track = getTrack(trackNo);
        Sector* sector = track ? track->getSector(sectorNo) : nullptr;

        if (sector && sector->hasData)
        {
            result = DumpHelper::HexDumpBuffer(sector->data, sector->dataSize);
        }

        return result;
    }
    /// endregion </Debug methods>
};
