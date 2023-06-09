#pragma once

#include "stdafx.h"

#include <algorithm>
#include "vg93.h"

class DiskImage
{
/// region <Types>
public:
    enum WD93_REGISTERS : uint8_t
    {
        CMD = 0,    // COMMAND/STATUS register (port #1F)
        TRK,        // TRACK register (port #3F)
        SEC,        // SECTOR register (port #5F)
        DAT,        // DATA register (port #7F)
        SYS         // BETA128 register (port #FF)
    };

    /// WD92 / VG93 state machine states
    enum WDSTATE : uint8_t
    {
        S_IDLE = 0,
        S_WAIT,

        S_DELAY_BEFORE_CMD,
        S_CMD_RW,
        S_FOUND_NEXT_ID,
        S_RDSEC,
        S_READ,
        S_WRSEC,
        S_WRITE,
        S_WRTRACK,
        S_WR_TRACK_DATA,

        S_TYPE1_CMD,
        S_STEP,
        S_SEEKSTART,
        S_RESTORE,
        S_SEEK,
        S_VERIFY,
        S_VERIFY2,

        S_WAIT_HLT,
        S_WAIT_HLT_RW,

        S_EJECT1,
        S_EJECT2
    };

    /// FDC status (output signals)
    enum WD_STATUS : uint8_t
    {
        WDS_BUSY      = 0x01,
        WDS_INDEX     = 0x02,
        WDS_DRQ       = 0x02,
        WDS_TRK00     = 0x04,
        WDS_LOST      = 0x04,
        WDS_CRCERR    = 0x08,
        WDS_NOTFOUND  = 0x10,
        WDS_SEEKERR   = 0x10,
        WDS_RECORDT   = 0x20,
        WDS_HEADL     = 0x20,
        WDS_WRFAULT   = 0x20,
        WDS_WRITEP    = 0x40,   // Disk is write protected
        WDS_NOTRDY    = 0x80
    };

    enum WD93_CMD_BITS : uint8_t
    {
        CMD_SEEK_RATE     = 0x03,
        CMD_SEEK_VERIFY   = 0x04,
        CMD_SEEK_HEADLOAD = 0x08,
        CMD_SEEK_TRKUPD   = 0x10,
        CMD_SEEK_DIR      = 0x20,

        CMD_WRITE_DEL     = 0x01,
        CMD_SIDE_CMP_FLAG = 0x02,
        CMD_DELAY         = 0x04,
        CMD_SIDE          = 0x08,
        CMD_SIDE_SHIFT    = 3,
        CMD_MULTIPLE      = 0x10
    };

    enum BETA_STATUS : uint8_t
    {
        DRQ   = 0x40,
        INTRQ = 0x80
    };

    enum WD_SYS : uint8_t
    {
        SYS_HLT       = 0x08
    };

    enum SEEK_MODE : uint8_t
    {
        JUST_SEEK = 0,
        LOAD_SECTORS = 1
    };

    struct SectorHeader
    {
        uint8_t sector = 0;
        uint8_t cylinder = 0;
        uint8_t head = 0;
        uint8_t l = 0;

        uint16_t crc = 0;
        uint16_t crc1 = 0;
        uint16_t crc2 = 0;

        uint8_t* data = nullptr;              // Pointer to sector data (in full track image)
        uint8_t* synchro = nullptr;           // Pointer to sector synchro (in full track image)
        uint8_t* damagedSectorMap = nullptr;  // Pointer to sector bad bytes (within track). Used only on read for protection emulation
        uint16_t damageBitIndex = 0;          // Index of first bad bytes bit (offset from track bad bytes data for the current sector)

        size_t dataLen = 0;
        uint8_t flags = 0;

        // Used in .dsk format only
        static constexpr uint8_t FL_DDAM = 0b0000'0001;
    };

    class Disk;  // Forward declaration
    struct Track
    {
        Disk* _disk = nullptr;

        uint8_t _cylinder = 0;
        uint8_t _side = 0;

        size_t _trackLen = 0;                       // Track data size
        SEEK_MODE _seekMode;                        // flag: is sectors filled

        uint8_t* _trackData;                        // Track data (can be nullptr if no data on track)
        uint8_t* _trackSynchro;                     // Synchro pulses bitmap
        uint8_t* _trackBad;                         // Bad bytes bitmap

        SectorHeader _headers[MAX_SECTOR] = { 0 };  // Sectors on track
        size_t _sectorNum = 0;                      // Number of sectors on this track

        /// region <Helper methods>
        static inline bool test_bit(const uint8_t *data, uint8_t bit)
        {
            return (data[bit >> 3] & (1U << (bit & 7))) != 0;
        }

        static inline void set_bit(uint8_t *data, unsigned bit)
        {
            data[bit >> 3] |= (1U << (bit & 7));
        }

        static inline void clr_bit(uint8_t *data, unsigned bit)
        {
            data[bit >> 3] &= ~(1U << (bit & 7));
        }
        /// endregion </Helper methods>

        void seek(Disk* disk, uint8_t side, uint8_t cylinder, SEEK_MODE seekMode)
        {
            // We already positioned to this sector properly
            if ((_disk == disk) && (_seekMode == seekMode) && (_cylinder == cylinder) && (_side == side))
                return;

            _disk = disk;
            _seekMode = seekMode;
            _sectorNum = 0;
            _side = side;
            _cylinder = cylinder;

            if (cylinder >= MAX_CYLINDERS || side > disk->sides || disk->rawData == nullptr)
            {
                _trackData = nullptr;
                return;
            }

            // We can still increase disk size by formatting more tracks (up to MAX_CYLINDERS value)
            if (_cylinder >= disk->cylinders)
            {
                if (_seekMode != JUST_SEEK)
                {
                    _trackData = nullptr;
                    return;
                }

                disk->cylinders = cylinder + 1;
            }

            _trackData = disk->trackData[cylinder][side];
            _trackSynchro = disk->trackSynchro[cylinder][side];
            _trackBad = disk->trackBads[cylinder][side];
            _trackLen = disk->trackLengths[cylinder][side];

            if (_trackLen == 0)
            {
                _trackData = nullptr;
                return;
            }


            // ts_byte = Z80FQ / (trklen * FDD_RPS);
            if (_seekMode == JUST_SEEK)
                return;

            /// region <Unfinished>

            /*
            //auto SecSizeMask{ CalcSecSizeMask() };

            for (size_t i = 0; i < _trackLen - 8; i++)
            {
                if (_trackData[i] != 0xA1 || _trackData[i+1] != 0xFE || !test_i(i)) // Поиск idam
                    continue;

                if (_sectorNum == MAX_SECTOR)
                {
                    throw std::logic_error("too many sectors");
                }

                SectorHeader *h = &_headers[_sectorNum++]; // Заполнение заголовка
                h->id = _trackData + i + 2; // Указатель на заголовок сектора
                h->cylinder = h->id[0];
                h->sector = h->id[1];
                h->n = h->id[2];
                h->l = h->id[3];
                h->crc = *(unsigned short*)(trkd+i+6);
                h->crc1 = (CRCHelper::crcWD93(_trackData+i+1, 5) == h->crc);
                h->data = nullptr;
                h->dataLen = 0;
                h->damageBitIndex = 0;
                h->flags = 0;

                unsigned end = min(_trackLen - 8, i + 8 + 43); // 43-DD, 30-SD

                // Формирование указателя на зону данных сектора
                for (size_t j = i + 8; j < end; j++)
                {
                    if (_trackData[j] != 0xA1 || !test_i(j) || test_i(j+1))
                        continue;

                    if (_trackData[j+1] == 0xF8 || _trackData[j+1] == 0xFB) // Найден data am (0xFB), deleted data am (0xF8)
                    {
                        if (_trackData[j + 1] == 0xF8) // ddam
                        {
                            h->flags |= SectorHeader::FL_DDAM;
                        }

                        h->dataLen = min(128U << (h->l & SecSizeMask), MAX_SECTOR_DATA_LEN); // [vv] FD1793 use only 2 lsb of sector size code
                        h->data = _trackData + j + 2;
                        // Для больших секторов 8192 байта (не влезающих на дорожку)
                        // до проверки crc дело не доходит, команда прерывается по второму прохождению индекса
                        if((h->l & SecSizeMask) < 6 && CRCHelper::crcWD93(h->data - 1, h->dataLen + 1) != *(unsigned short*)(h->data + h->dataLen))
                        {
                            h->crc2 = 0;
                        }
                        else
                        {
                            h->crc2 = 1;
                        }

                        if (_trackBad)
                        {
                            for (size_t b = 0; b < h->dataLen; b++)
                            {
                                if (test_wp(j + 2 + b))
                                {
                                    h->wp_start = j + 2; // Есть хотябы один сбойный байт
                                    break;
                                }
                            }
                        }
                    }

                    break;
                }
            }
             */
            /// endregion </Unfinished>
        }

        void write(size_t pos, uint8_t byte, uint8_t index)
        {
            if (!_trackData)
                return;

            _trackData[pos] = byte;
            if (index)
            {
                set_bit(_trackSynchro, pos);
            }
            else
            {
                clr_bit(_trackSynchro, pos);
            }
        }

        void format()
        {
            if (_trackData == nullptr || _trackLen == 0)
            {
                // No data on track, formatting is impossible
                return;
            }

            // Sector size mask for FD1793 = 0x03
            // Sector size mask for uPD765 = 0x0F
            uint8_t sectorSizeMask = 0x03;

            // Wipe all existing data, synchro and bad bitmaps
            size_t bitmapSize = (_trackLen + 7) >> 3;
            memset(_trackData, 0, _trackLen);
            memset(_trackSynchro, 0, bitmapSize);
            memset(_trackBad, 0, bitmapSize);

            uint8_t* dst = _trackData;

            // 6250 - 6144 = 106
            // gap4a(80) + sync0(12) + iam(3) + 1 + s*(gap1(50) + sync1(12) + idam(3) + 1 + 4 + 2 + gap2(22) + sync2(12) + data_am(3) + 1 + 2)
            uint8_t gap4a = 80;
            uint8_t sync0 = 12;
            uint8_t i_am = 3;
            uint8_t gap1 = 40;
            uint8_t sync1 = 12;
            uint8_t id_am = 3;
            uint8_t gap2 = 22;
            uint8_t sync2 = 12;
            uint8_t data_am = 3;
            size_t synchroDataLen = gap4a + sync0 + i_am + 1 + _sectorNum * (gap1 + sync1 + id_am + 1 + 4 + 2 + gap2 + sync2 + data_am + 1 + 2);

            size_t dataSize = 0;
            for (size_t i = 0; i < _sectorNum; i++)
            {
                SectorHeader& sectorHeader = _headers[i];
                size_t defaultSectorLen = std::min((size_t)(128 << sectorHeader.l), MAX_SECTOR_DATA_LEN);
                dataSize += sectorHeader.dataLen != 0 ? sectorHeader.dataLen : defaultSectorLen;
            }


            // Calculate if full data length is not exceeding limit. If yes - reduce all synchro fields to minimally possible
            if (synchroDataLen + dataSize >= MAX_SECTOR_DATA_LEN)
            {
                gap4a = 1;
                sync0 = 1;
                i_am = 1;
                gap1 = 1;
                sync1 = 1;
                id_am = 1;
                gap2 = 1;
                sync2 = 1;
                data_am = 1;
            }
            synchroDataLen = gap4a + sync0 + i_am + 1 + _sectorNum * (gap1 + sync1 + id_am + 1 + 4 + 2 + gap2 + sync2 + data_am + 1 + 2);

            if (synchroDataLen + dataSize < MAX_SECTOR_DATA_LEN)
            {
                memset(dst, 0x4E, gap1);
                dst += gap1;

                memset(dst, 0, sync1);
                dst += sync1;

                for (size_t i = 0; i < id_am; i++)
                {
                    // TODO: write
                    //write(unsigned(dst++ - _trackData), 0xA1, 1);
                }

                *dst++ = 0xFE;

                // Access last sector on track
                SectorHeader* header = &_headers[_sectorNum - 1];
                *dst++ = header->cylinder;
                *dst++ = header->sector;
                *dst++ = header->head;
                *dst++ = header->l;

                // Calculate CRC for the sector geometry
                uint16_t crc = CRCHelper::crcWD93(dst - 5, 5);
                if (header->crc1 == 1)
                {
                    crc = header->crc;
                }
                if (header->crc1 == 2)
                {
                    crc ^= 0xFFFF;
                }
                dst += 2;

                if (header->data)
                {
                    memset(dst, 0x4E, gap2);
                    dst += gap2;

                    memset(dst, 0, sync2);
                    dst += sync2;

                    for (size_t i = 0; i < data_am; i++)
                    {
                        // TODO: write data
                        //write(unsigned(dst++ - _trackData), 0xA1, 1);
                    }

                    *dst++ = (header->flags & SectorHeader::FL_DDAM) ? 0xF8 : 0xFB;

                    // Populate data
                    size_t len = header->dataLen != 0 ? header->dataLen : std::min((size_t)(128 << (header->l & sectorSizeMask)), MAX_SECTOR_DATA_LEN);
                    if (header->data != (unsigned char*)1)
                    {
                        memcpy(dst, header->data, len);

                        // Copy bad sector bytes bitmap
                        if (header->damagedSectorMap)
                        {
                            uint16_t damageBitIndex = uint16_t(dst - _trackData);
                            header->damageBitIndex = damageBitIndex;

                            for (size_t b = 0; b < len; b++)
                            {
                                if (test_bit(header->damagedSectorMap, b))
                                {
                                    set_bit(_trackBad, damageBitIndex + b);
                                }
                            }
                        }
                    }
                    else
                        memset(dst, 0, len);

                    crc = CRCHelper::crcWD93(dst - 1, len + 1);
                    if (header->crc2 == 1)
                    {
                        // FDI image only
                        //crc = header->crcd;
                    }

                    if (header->crc2 == 2)
                        crc ^= 0xFFFF;
                    *(uint16_t*)(dst+len) = crc;
                    dst += len + 2;
                }
            }

            if (dst > _trackData + _trackLen)
            {
                printf("cyl=%u, h=%u, additional len=%zu\n", _cylinder, _side, size_t(dst - (_trackData + _trackLen)));
                throw std::logic_error("Track data is abnormally long");
            }

            // Fill the rest of the track with 0x4E pattern
            while (dst < _trackData + _trackLen)
                *dst++ = 0x4E;
        }

        SectorHeader* getSector(uint8_t sectorNum, size_t len)
        {
            SectorHeader* result = nullptr;

            // Find sector
            bool found = false;
            uint8_t sectorIndex = 0;
            for (size_t i = 0; i < _sectorNum; i++)
            {
                if (_headers[i].sector == sectorNum)
                {
                    found = true;
                    sectorIndex = i;
                    break;
                }
            }

            if (found)
            {
                result = &_headers[sectorIndex];
            }

            return result;
        }

        size_t updateSector(uint8_t sectorNum, uint8_t* sectorData, size_t len)
        {
            size_t result = 0;

            const SectorHeader* header = getSector(sectorNum, len);
            if (header && header->data)
            {
                size_t size = header->dataLen;
                if (header->data != sectorData)
                {
                    // Copy new sector content over
                    memcpy(header->data, sectorData, size);
                }

                // Calculate sector CRC
                uint16_t crc = CRCHelper::crcWD93(header->data - 1, size + 1);
                //TODO: write CRC after sector data
                //*(uint16_t*)(header->data + size) = crc;

                result = size;
            }

            return result;
        }
    };

    struct Disk
    {
        uint8_t id = 0;                             // Unique drive identifier
        size_t motorSpinCounter = 0;                // 0 - not spinning; >0 - time when it'll stop
        uint8_t currentTrack = 0;                   // Head position

        // Disk data
        uint8_t* rawData = nullptr;                 // Raw disk data (already unpacked by disk image loader)
        size_t rawDataSize = 0;                     // Size of raw data

        // Initial disk geometry
        uint8_t cylinders = 0;
        uint8_t sides = 2;
        uint8_t trackLengths[MAX_CYLINDERS][2];     // All track lengths on both disk sides

        uint8_t* trackData[MAX_CYLINDERS][2];       // Shortcuts to each track data
        uint8_t* trackSynchro[MAX_CYLINDERS][2];    // Shortcuts to each track synchro pulses
        uint8_t* trackBads[MAX_CYLINDERS][2];       // Shortcuts to each track bad byte info

        Track _track;                               // Buffer current track for fast sector access


    };
    /// endregion </Types>

    /// region <Fields>
protected:
    Disk* loadedDisk = nullptr;
    WDSTATE state;

    uint8_t cmd;
    uint8_t data;
    uint8_t track;
    uint8_t sector;
    uint8_t status;

    int8_t _stepDirection = 1;      // Positive values: from edge to center. Negative - from center to edge
    /// endregion </Fields>

    /// region <Methods>
public:

    /// endregion </Methods>
};


