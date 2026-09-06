#include "loaders/disk/loader_td0.h"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <vector>

#include <common/filehelper.h>
#include <common/stringhelper.h>

#include "_helpers/testpathhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/fdc.h"
#include "emulator/io/fdc/wd1793.h"
#include "loaders/disk/loader_trd.h"

/// TD0 loader tests (docs/inprogress/2026-09-02-universal-track-model/loader-td0.md, section 4).
/// Fixtures are produced by core/tests/emulator/io/fdc/tools/td0_image_generator.py; the expected sector
/// contents of trdos-sample.td0 are recomputed here with the generator's rule (sectorContent()).

class LoaderTD0_Test : public ::testing::Test
{
protected:
    EmulatorContext* _context = nullptr;
    CoreCUT* _core = nullptr;
    Z80* _z80 = nullptr;

protected:
    void SetUp() override
    {
        _context = new EmulatorContext(LoggerLevel::LogError);
        _context->pModuleLogger->TurnOffLoggingForAll();

        _core = new CoreCUT(_context);
        _z80 = new Z80(_context);
        _core->_z80 = _z80;
        _context->pCore = _core;
    }

    void TearDown() override
    {
        if (_context)
        {
            if (_context->pCore)
            {
                _core->_z80 = nullptr;
                delete _z80;
                _context->pCore = nullptr;
                delete _core;
            }
            delete _context;
        }
    }

    static std::vector<uint8_t> readFile(const std::string& path)
    {
        std::vector<uint8_t> result;
        if (FileHelper::FileExists(path))
        {
            size_t size = FileHelper::GetFileSize(path);
            result.resize(size);
            if (size > 0) FileHelper::ReadFileToBuffer(path, result.data(), size);
        }
        return result;
    }

    static void removeFile(const std::string& path)
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    static std::string scratch(const char* name) { return TestPathHelper::GetTestScratchPath(name); }
    static std::string fixture(const char* name) { return TestPathHelper::GetTestDataPath(name); }

    static std::string joinWarnings(const std::vector<std::string>& warnings)
    {
        std::string s;
        for (const std::string& w : warnings) s += w + "\n";
        return s;
    }

    static bool hasWarning(const std::vector<std::string>& warnings, const char* needle)
    {
        for (const std::string& w : warnings) if (w.find(needle) != std::string::npos) return true;
        return false;
    }

    /// region <Generator rule for trdos-sample.td0>

    static constexpr uint8_t TRDOS_INTERLEAVE[16] = {1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15, 8, 16};

    static std::vector<uint8_t> lcgBytes(uint32_t seed, size_t count)
    {
        std::vector<uint8_t> out(count);
        uint32_t x = seed;
        for (size_t i = 0; i < count; i++)
        {
            x = (x * 1103515245u + 12345u) & 0x7FFFFFFFu;
            out[i] = static_cast<uint8_t>((x >> 16) & 0xFF);
        }
        return out;
    }

    static std::vector<uint8_t> trdosVolumeSector()
    {
        std::vector<uint8_t> d(256, 0);
        d[0xE1] = 0;
        d[0xE2] = 1;
        d[0xE3] = 0x16;
        d[0xE4] = 0;
        const uint16_t free = (80 * 2 - 1) * 16;
        d[0xE5] = free & 0xFF;
        d[0xE6] = free >> 8;
        d[0xE7] = 0x10;
        std::memcpy(&d[0xF5], "TD0TEST ", 8);
        return d;
    }

    /// Mirrors sector_content() in td0_image_generator.py. Returns the data and the encoding the generator used.
    static std::vector<uint8_t> sectorContent(uint8_t c, uint8_t h, uint8_t r, int* encoding = nullptr)
    {
        const int t = c * 2 + h;
        if (t == 0 && r >= 1 && r <= 8)
        {
            if (encoding) *encoding = 1;
            return std::vector<uint8_t>(256, 0);
        }
        if (t == 0 && r == 9)
        {
            if (encoding) *encoding = 0;
            return trdosVolumeSector();
        }
        const int kind = (t + r) % 3;
        if (encoding) *encoding = kind;
        if (kind == 0)
        {
            return lcgBytes((static_cast<uint32_t>(c) << 16) | (static_cast<uint32_t>(h) << 8) | r, 256);
        }
        if (kind == 1)
        {
            std::vector<uint8_t> d(256);
            for (size_t i = 0; i < 256; i += 2) { d[i] = c; d[i + 1] = r; }
            return d;
        }
        std::vector<uint8_t> d(256);
        for (size_t i = 0; i < 64; i++) d[i] = static_cast<uint8_t>((i * 3 + r + c) & 0xFF);
        for (size_t i = 64; i < 256; i += 4) { d[i] = c; d[i + 1] = h; d[i + 2] = r; d[i + 3] = 0xAA; }
        return d;
    }

    /// endregion </Generator rule for trdos-sample.td0>

    /// Compare two images track by track: same stream bytes, clock bitmap, encoding, sector index
    static void expectSameModel(DiskImage* a, DiskImage* b, const char* what)
    {
        ASSERT_EQ(a->getCylinders(), b->getCylinders()) << what;
        ASSERT_EQ(a->getSides(), b->getSides()) << what;
        const size_t trackCount = static_cast<size_t>(a->getCylinders()) * a->getSides();
        for (size_t t = 0; t < trackCount; t++)
        {
            DiskImage::Track* p = a->getTrack(static_cast<uint8_t>(t));
            DiskImage::Track* q = b->getTrack(static_cast<uint8_t>(t));
            ASSERT_EQ(p->rawSize(), q->rawSize()) << what << " track " << t;
            EXPECT_EQ(p->encoding(), q->encoding()) << what << " track " << t;
            EXPECT_EQ(std::memcmp(p->rawData(), q->rawData(), p->rawSize()), 0) << what << " track " << t;
            EXPECT_EQ(p->clockBitmap(), q->clockBitmap()) << what << " track " << t;
            ASSERT_EQ(p->sectorCount(), q->sectorCount()) << what << " track " << t;
            for (size_t s = 0; s < p->sectorCount(); s++)
            {
                const DiskImage::Sector* x = p->getRawSector(s);
                const DiskImage::Sector* y = q->getRawSector(s);
                EXPECT_EQ(x->number(), y->number());
                EXPECT_EQ(x->hasData, y->hasData);
                EXPECT_EQ(x->deleted, y->deleted);
                EXPECT_EQ(x->dataCrcValid, y->dataCrcValid);
            }
        }
    }
};

constexpr uint8_t LoaderTD0_Test::TRDOS_INTERLEAVE[16];

/// region <Detection / CRC>

TEST_F(LoaderTD0_Test, Detect_TD_td)
{
    const uint8_t normal[] = {'T', 'D', 0, 0};
    const uint8_t advanced[] = {'t', 'd', 0, 0};
    const uint8_t mixed[] = {'T', 'd', 0, 0};
    const uint8_t fdi[] = {'F', 'D', 'I', 0};
    EXPECT_TRUE(LoaderTD0::detect(normal, sizeof(normal)));
    EXPECT_TRUE(LoaderTD0::detect(advanced, sizeof(advanced)));
    EXPECT_FALSE(LoaderTD0::detect(mixed, sizeof(mixed)));
    EXPECT_FALSE(LoaderTD0::detect(fdi, sizeof(fdi)));
    EXPECT_FALSE(LoaderTD0::detect(normal, 1));
    EXPECT_FALSE(LoaderTD0::detect(nullptr, 0));
}

TEST_F(LoaderTD0_Test, Crc16_MatchesFdcTable_And_Fixtures)
{
    // The loader's bitwise CRC agrees with the table in fdc.h (CRCHelper::crc16, "used for TD0")
    std::vector<uint8_t> sample = lcgBytes(42, 1000);
    for (size_t len : {size_t(0), size_t(1), size_t(9), size_t(10), size_t(255), size_t(1000)})
    {
        EXPECT_EQ(LoaderTD0::crc16(sample.data(), len), CRCHelper::crc16(sample.data(), static_cast<uint16_t>(len))) << len;
    }

    // ... and with the generator's independent implementation (header CRC of every fixture)
    for (const char* name : {"loaders/td0/trdos-sample.td0", "loaders/td0/trdos-sample-adv.td0", "loaders/td0/protected-sample.td0"})
    {
        std::vector<uint8_t> d = readFile(fixture(name));
        ASSERT_GE(d.size(), 12u) << name;
        const uint16_t stored = static_cast<uint16_t>(d[10] | (d[11] << 8));
        EXPECT_EQ(LoaderTD0::crc16(d.data(), 10), stored) << name;
    }
}

/// endregion </Detection / CRC>

/// region <Fixtures>

TEST_F(LoaderTD0_Test, Load_Normal_Geometry)
{
    LoaderTD0 loader(_context, fixture("loaders/td0/trdos-sample.td0"));
    ASSERT_TRUE(loader.loadImage()) << joinWarnings(loader.lastWarnings());
    EXPECT_TRUE(loader.lastWarnings().empty()) << joinWarnings(loader.lastWarnings());
    DiskImage* image = loader.getImage();
    ASSERT_NE(image, nullptr);
    EXPECT_TRUE(image->getLoaded());
    EXPECT_FALSE(image->isDirty());
    EXPECT_FALSE(loader.isAdvancedCompression());
    EXPECT_FALSE(loader.hasComment());
    EXPECT_EQ(loader.getVersion(), 0x15);
    EXPECT_EQ(loader.getDriveType(), 3);
    EXPECT_EQ(loader.getDataRate(), 0);

    ASSERT_EQ(image->getCylinders(), 80);
    ASSERT_EQ(image->getSides(), 2);
    EXPECT_TRUE(LoaderTRD::isTrdosGeometry(image));

    for (uint8_t c = 0; c < 80; c++)
    {
        for (uint8_t h = 0; h < 2; h++)
        {
            DiskImage::Track* track = image->getTrackForCylinderAndSide(c, h);
            ASSERT_NE(track, nullptr);
            ASSERT_EQ(track->sectorCount(), 16u) << "c" << (int)c << " h" << (int)h;
            EXPECT_EQ(track->rawSize(), 6250u);
            EXPECT_EQ(track->encoding(), DiskImage::Encoding::MFM);

            for (size_t s = 0; s < 16; s++)
            {
                const DiskImage::Sector* sector = track->getRawSector(s);
                EXPECT_EQ(sector->number(), TRDOS_INTERLEAVE[s]) << "physical order is the TR-DOS interleave";
                EXPECT_EQ(sector->cylinder(), c);
                EXPECT_EQ(sector->head(), h);
                EXPECT_EQ(sector->sizeCode(), 1);
                EXPECT_EQ(sector->dataSize, 256);
                ASSERT_TRUE(sector->hasData);
                EXPECT_TRUE(sector->idCrcValid);
                EXPECT_TRUE(sector->dataCrcValid);
                EXPECT_FALSE(sector->deleted);

                std::vector<uint8_t> expected = sectorContent(c, h, sector->number());
                EXPECT_EQ(std::memcmp(sector->data, expected.data(), 256), 0) << "c" << (int)c << " h" << (int)h << " s" << (int)sector->number();
            }
        }
    }

    // TR-DOS volume sector (cylinder 0, sector 9)
    const DiskImage::Sector* volume = image->getTrack(0)->findSector(9);
    ASSERT_NE(volume, nullptr);
    EXPECT_EQ(volume->data[0xE7], 0x10);
    EXPECT_EQ(volume->data[0xE3], 0x16);
    EXPECT_EQ(std::memcmp(volume->data + 0xF5, "TD0TEST ", 8), 0);

    // The image validates as a TR-DOS disk
    LoaderTRD trd(_context, "");
    TRDValidationReport report;
    EXPECT_TRUE(trd.validateTRDOSImage(image, report));

    delete image;
}

TEST_F(LoaderTD0_Test, Load_Advanced_Decompresses)
{
    LoaderTD0 normal(_context, fixture("loaders/td0/trdos-sample.td0"));
    ASSERT_TRUE(normal.loadImage()) << joinWarnings(normal.lastWarnings());

    LoaderTD0 advanced(_context, fixture("loaders/td0/trdos-sample-adv.td0"));
    ASSERT_TRUE(advanced.loadImage()) << joinWarnings(advanced.lastWarnings());
    EXPECT_TRUE(advanced.lastWarnings().empty()) << joinWarnings(advanced.lastWarnings());
    EXPECT_TRUE(advanced.isAdvancedCompression());

    DiskImage* a = normal.getImage();
    DiskImage* b = advanced.getImage();
    expectSameModel(a, b, "TD vs td");

    // The decompressed stream is byte-identical to the "TD" body (the encoder is an independent implementation)
    std::vector<uint8_t> plain = readFile(fixture("loaders/td0/trdos-sample.td0"));
    std::vector<uint8_t> packed = readFile(fixture("loaders/td0/trdos-sample-adv.td0"));
    ASSERT_GT(plain.size(), 12u);
    ASSERT_GT(packed.size(), 12u);
    EXPECT_LT(packed.size(), plain.size());
    std::vector<uint8_t> out;
    ASSERT_TRUE(LoaderTD0::decompressLzhuf(packed.data() + 12, packed.size() - 12, out));
    ASSERT_GE(out.size(), plain.size() - 12);
    EXPECT_EQ(std::memcmp(out.data(), plain.data() + 12, plain.size() - 12), 0);

    delete a;
    delete b;
}

TEST_F(LoaderTD0_Test, Decode_Rle_Patterns)
{
    std::vector<uint8_t> out;

    // Encoding 0: raw bytes
    const uint8_t raw[] = {1, 2, 3, 4, 5, 6, 7, 8};
    EXPECT_TRUE(LoaderTD0::decodeDataBlock(0, raw, sizeof(raw), 8, out));
    EXPECT_EQ(out, std::vector<uint8_t>(raw, raw + 8));

    // Raw block shorter than the sector: zero padded, reported
    EXPECT_FALSE(LoaderTD0::decodeDataBlock(0, raw, 4, 8, out));
    EXPECT_EQ(out, std::vector<uint8_t>({1, 2, 3, 4, 0, 0, 0, 0}));

    // Encoding 1: count(2) + 2-byte pattern
    const uint8_t pattern[] = {0x04, 0x00, 0xAB, 0xCD};
    EXPECT_TRUE(LoaderTD0::decodeDataBlock(1, pattern, sizeof(pattern), 8, out));
    EXPECT_EQ(out, std::vector<uint8_t>({0xAB, 0xCD, 0xAB, 0xCD, 0xAB, 0xCD, 0xAB, 0xCD}));

    // Pattern count larger than the sector: clipped
    const uint8_t longPattern[] = {0xFF, 0x00, 0x11, 0x22};
    EXPECT_TRUE(LoaderTD0::decodeDataBlock(1, longPattern, sizeof(longPattern), 6, out));
    EXPECT_EQ(out, std::vector<uint8_t>({0x11, 0x22, 0x11, 0x22, 0x11, 0x22}));

    // Encoding 2: literal block, 2-byte pattern block, 4-byte pattern block
    const uint8_t rle[] = {
        0x00, 0x03, 0xA0, 0xA1, 0xA2,          // 3 raw bytes
        0x01, 0x03, 0x55, 0xAA,                // (55 AA) x 3
        0x02, 0x02, 0x01, 0x02, 0x03, 0x04,    // (01 02 03 04) x 2
    };
    EXPECT_TRUE(LoaderTD0::decodeDataBlock(2, rle, sizeof(rle), 17, out));
    EXPECT_EQ(out, std::vector<uint8_t>({0xA0, 0xA1, 0xA2, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x01, 0x02, 0x03, 0x04, 0x01, 0x02, 0x03, 0x04}));

    // RLE output longer than the sector: clipped in the middle of a pattern
    EXPECT_TRUE(LoaderTD0::decodeDataBlock(2, rle, sizeof(rle), 12, out));
    EXPECT_EQ(out.size(), 12u);
    EXPECT_EQ(out[11], 0x03);

    // Truncated RLE pattern block: padded, reported
    const uint8_t rleBad[] = {0x02, 0x02, 0x01, 0x02};
    EXPECT_FALSE(LoaderTD0::decodeDataBlock(2, rleBad, sizeof(rleBad), 8, out));
    EXPECT_EQ(out.size(), 8u);

    // Unknown encoding
    EXPECT_FALSE(LoaderTD0::decodeDataBlock(3, raw, sizeof(raw), 8, out));
    EXPECT_EQ(out, std::vector<uint8_t>(8, 0));
}

TEST_F(LoaderTD0_Test, Flags_To_Model)
{
    LoaderTD0 loader(_context, fixture("loaders/td0/protected-sample.td0"));
    ASSERT_TRUE(loader.loadImage()) << joinWarnings(loader.lastWarnings());
    DiskImage* image = loader.getImage();
    ASSERT_EQ(image->getCylinders(), 4);
    ASSERT_EQ(image->getSides(), 2);

    // Cylinder 0 head 0: plain 16 x 256 with every data encoding
    DiskImage::Track* t00 = image->getTrackForCylinderAndSide(0, 0);
    ASSERT_EQ(t00->sectorCount(), 16u);
    for (size_t s = 0; s < 16; s++)
    {
        const DiskImage::Sector* sector = t00->getRawSector(s);
        EXPECT_EQ(sector->number(), s + 1);
        ASSERT_TRUE(sector->hasData && sector->dataCrcValid && sector->idCrcValid && !sector->deleted);
        for (size_t i = 0; i < 256; i++) ASSERT_EQ(sector->data[i], s + 1) << "sector " << s + 1 << " byte " << i;
    }

    // Cylinder 0 head 1: 9 x 512; 3 deleted, 5 CRC error, 9 deleted + CRC error, data always kept
    DiskImage::Track* t01 = image->getTrackForCylinderAndSide(0, 1);
    ASSERT_EQ(t01->sectorCount(), 9u);
    for (size_t s = 0; s < 9; s++)
    {
        DiskImage::Sector* sector = t01->getRawSector(s);
        const uint8_t r = static_cast<uint8_t>(s + 1);
        EXPECT_EQ(sector->number(), r);
        EXPECT_EQ(sector->dataSize, 512);
        ASSERT_TRUE(sector->hasData);
        EXPECT_EQ(sector->deleted, r == 3 || r == 9) << "sector " << (int)r;
        EXPECT_EQ(sector->dataAddressMark(), (r == 3 || r == 9) ? 0xF8 : 0xFB);
        EXPECT_EQ(sector->dataCrcValid, !(r == 5 || r == 9)) << "sector " << (int)r;
        EXPECT_EQ(sector->isDataCRCValid(), !(r == 5 || r == 9));
        std::vector<uint8_t> expected = lcgBytes(0x0100u | r, 512);
        EXPECT_EQ(std::memcmp(sector->data, expected.data(), 512), 0) << "sector " << (int)r;
    }

    // Cylinder 1 head 0: 4 = ID only, 6 = DOS-unallocated (ID only), 7 duplicated
    DiskImage::Track* t10 = image->getTrackForCylinderAndSide(1, 0);
    ASSERT_EQ(t10->sectorCount(), 11u);
    EXPECT_EQ(t10->getRawSector(3)->number(), 4);
    EXPECT_FALSE(t10->getRawSector(3)->hasData);
    EXPECT_TRUE(t10->getRawSector(3)->idCrcValid);
    EXPECT_EQ(t10->getRawSector(5)->number(), 6);
    EXPECT_FALSE(t10->getRawSector(5)->hasData);
    EXPECT_EQ(t10->getDataForSector(3), nullptr);
    EXPECT_EQ(t10->getRawSector(6)->number(), 7);
    EXPECT_EQ(t10->getRawSector(7)->number(), 7);
    EXPECT_EQ(t10->getRawSector(6)->data[0], 0x17);
    EXPECT_EQ(t10->getRawSector(6)->data[1], 0x27);
    EXPECT_EQ(t10->getRawSector(7)->data[0], 0x77);
    EXPECT_EQ(t10->getRawSector(8)->number(), 8);
    size_t sevens = 0;
    for (const DiskImage::Sector& s : t10->sectors()) if (s.number() == 7) sevens++;
    EXPECT_EQ(sevens, 2u);

    // Cylinder 2 head 0: the sector without an ID field is skipped, 5 x 1024 remain
    DiskImage::Track* t20 = image->getTrackForCylinderAndSide(2, 0);
    ASSERT_EQ(t20->sectorCount(), 5u);
    for (size_t s = 0; s < 5; s++)
    {
        const DiskImage::Sector* sector = t20->getRawSector(s);
        EXPECT_EQ(sector->number(), s + 1);
        EXPECT_EQ(sector->dataSize, 1024);
        std::vector<uint8_t> expected = lcgBytes(0x2000u | static_cast<uint32_t>(s + 1), 1024);
        EXPECT_EQ(std::memcmp(sector->data, expected.data(), 1024), 0);
    }
    EXPECT_TRUE(hasWarning(loader.lastWarnings(), "no ID field")) << joinWarnings(loader.lastWarnings());

    // Cylinder 2 head 1: mixed sizes and a foreign C/H in one ID
    DiskImage::Track* t21 = image->getTrackForCylinderAndSide(2, 1);
    ASSERT_EQ(t21->sectorCount(), 4u);
    EXPECT_EQ(t21->getRawSector(0)->dataSize, 128);
    EXPECT_EQ(t21->getRawSector(1)->dataSize, 256);
    EXPECT_EQ(t21->getRawSector(2)->dataSize, 512);
    EXPECT_EQ(t21->getRawSector(3)->dataSize, 1024);
    EXPECT_EQ(t21->getRawSector(2)->cylinder(), 40);
    EXPECT_EQ(t21->getRawSector(2)->head(), 1);
    EXPECT_EQ(t21->getRawSector(2)->number(), 7);
    for (size_t i = 0; i < 4; i++)
    {
        EXPECT_TRUE(t21->getRawSector(i)->idCrcValid && t21->getRawSector(i)->dataCrcValid) << i;
        EXPECT_EQ(t21->getRawSector(i)->data[0], 0xA0 + i);
    }
    EXPECT_EQ(t21->rawSize(), 6250u);

    // Cylinder 3 head 0: unformatted
    EXPECT_EQ(image->getTrackForCylinderAndSide(3, 0)->sectorCount(), 0u);
    EXPECT_EQ(image->getTrackForCylinderAndSide(3, 0)->rawSize(), 6250u);

    // Cylinder 3 head 1: stale sector CRC is a warning only, data is loaded
    DiskImage::Track* t31 = image->getTrackForCylinderAndSide(3, 1);
    ASSERT_EQ(t31->sectorCount(), 16u);
    EXPECT_TRUE(t31->getRawSector(1)->dataCrcValid);
    std::vector<uint8_t> expected = lcgBytes(0x3102u, 256);
    EXPECT_EQ(std::memcmp(t31->getRawSector(1)->data, expected.data(), 256), 0);
    EXPECT_TRUE(hasWarning(loader.lastWarnings(), "sector 2: data CRC mismatch")) << joinWarnings(loader.lastWarnings());

    // Exactly those two warnings
    EXPECT_EQ(loader.lastWarnings().size(), 2u) << joinWarnings(loader.lastWarnings());

    delete image;
}

TEST_F(LoaderTD0_Test, Fm_Track_Flag)
{
    LoaderTD0 loader(_context, fixture("loaders/td0/protected-sample.td0"));
    ASSERT_TRUE(loader.loadImage()) << joinWarnings(loader.lastWarnings());
    DiskImage* image = loader.getImage();

    DiskImage::Track* fm = image->getTrackForCylinderAndSide(1, 1);
    EXPECT_EQ(fm->encoding(), DiskImage::Encoding::FM);
    EXPECT_EQ(fm->rawSize(), 3125u);
    EXPECT_TRUE(fm->hasClockMarks());
    ASSERT_EQ(fm->sectorCount(), 16u);
    for (size_t s = 0; s < 16; s++)
    {
        const DiskImage::Sector* sector = fm->getRawSector(s);
        EXPECT_EQ(sector->number(), s + 1);
        EXPECT_EQ(sector->dataSize, 128);
        EXPECT_TRUE(sector->idCrcValid);
        EXPECT_TRUE(sector->dataCrcValid);
        std::vector<uint8_t> expected = lcgBytes(0x0F00u | static_cast<uint32_t>(s + 1), 128);
        EXPECT_EQ(std::memcmp(sector->data, expected.data(), 128), 0) << "sector " << s + 1;
    }

    // The neighbouring tracks stay MFM
    EXPECT_EQ(image->getTrackForCylinderAndSide(1, 0)->encoding(), DiskImage::Encoding::MFM);
    EXPECT_EQ(image->getTrackForCylinderAndSide(2, 0)->encoding(), DiskImage::Encoding::MFM);

    delete image;
}

TEST_F(LoaderTD0_Test, Comment_Preserved)
{
    LoaderTD0 loader(_context, fixture("loaders/td0/protected-sample.td0"));
    ASSERT_TRUE(loader.loadImage()) << joinWarnings(loader.lastWarnings());
    EXPECT_TRUE(loader.hasComment());
    EXPECT_EQ(loader.getDescription(), "Protected sample\nsecond line");
    EXPECT_EQ(loader.getCommentDate().year, 2026);
    EXPECT_EQ(loader.getCommentDate().month, 9);
    EXPECT_EQ(loader.getCommentDate().day, 2);
    EXPECT_EQ(loader.getCommentDate().hour, 12);
    EXPECT_EQ(loader.getCommentDate().minute, 34);
    EXPECT_EQ(loader.getCommentDate().second, 56);
    delete loader.getImage();

    LoaderTD0 plain(_context, fixture("loaders/td0/trdos-sample.td0"));
    ASSERT_TRUE(plain.loadImage());
    EXPECT_FALSE(plain.hasComment());
    EXPECT_TRUE(plain.getDescription().empty());
    delete plain.getImage();
}

TEST_F(LoaderTD0_Test, Load_TrdosSample_ThroughController)
{
    LoaderTD0 loader(_context, fixture("loaders/td0/trdos-sample.td0"));
    ASSERT_TRUE(loader.loadImage()) << joinWarnings(loader.lastWarnings());
    DiskImage* image = loader.getImage();

    auto readSector = [&](uint8_t cyl, uint8_t side, uint8_t number) -> std::vector<uint8_t>
    {
        WD1793CUT fdc(_context);
        fdc.getDrive()->insertDisk(image);
        fdc._beta128Register = WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_RESET |
                               (side ? WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_HEAD : 0);   // MFM: no DENSITY bit
        fdc._drive = 0;
        fdc.wakeUp();
        fdc._time = 1000;
        fdc.prolongFDDMotorRotation();
        fdc._trackRegister = cyl;
        fdc._selectedDrive->setTrack(cyl);
        fdc._sideUp = side != 0;
        fdc._sectorRegister = number;
        fdc._commandRegister = 0x80;
        fdc._lastDecodedCmd = WD1793::WD_CMD_READ_SECTOR;
        fdc.cmdReadSector(0x00);

        std::vector<uint8_t> read;
        for (size_t clk = 1000; clk < 1000 + 3'500'000 * 3 && fdc._state != WD1793::S_IDLE; clk += 100)
        {
            fdc._time = clk;
            fdc.process();
            if (fdc._beta128status & WD1793::DRQ) read.push_back(fdc.readDataRegister());
        }
        EXPECT_EQ(fdc._state, WD1793::S_IDLE) << "command did not finish";
        EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_NOTFOUND);
        EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_CRCERR);
        fdc.getDrive()->ejectDisk();
        return read;
    };

    // Volume sector (raw encoding), a pattern sector and an RLE sector on side 1
    std::vector<uint8_t> data = readSector(0, 0, 9);
    ASSERT_EQ(data.size(), 256u);
    EXPECT_EQ(data[0xE7], 0x10);
    EXPECT_EQ(std::memcmp(data.data(), trdosVolumeSector().data(), 256), 0);

    for (uint8_t r = 1; r <= 3; r++)
    {
        int encoding = -1;
        std::vector<uint8_t> expected = sectorContent(37, 1, r, &encoding);
        data = readSector(37, 1, r);
        ASSERT_EQ(data.size(), 256u) << "sector " << (int)r;
        EXPECT_EQ(std::memcmp(data.data(), expected.data(), 256), 0) << "sector " << (int)r << " encoding " << encoding;
    }

    delete image;
}

/// endregion </Fixtures>

/// region <Save>

TEST_F(LoaderTD0_Test, Save_RoundTrip)
{
    const char* fixtures[] = {"loaders/td0/protected-sample.td0", "loaders/td0/trdos-sample-adv.td0"};

    for (const char* name : fixtures)
    {
        std::string target = scratch("roundtrip.td0");
        removeFile(target);

        LoaderTD0 loader(_context, fixture(name));
        ASSERT_TRUE(loader.loadImage()) << name << ": " << joinWarnings(loader.lastWarnings());
        DiskImage* image = loader.getImage();

        ASSERT_TRUE(loader.writeImage(target)) << name << ": " << joinWarnings(loader.lastWarnings());
        EXPECT_TRUE(loader.lastWarnings().empty()) << joinWarnings(loader.lastWarnings());
        EXPECT_EQ(image->getFilePath(), target);
        EXPECT_FALSE(image->isDirty());

        // Always the uncompressed variant with a valid header CRC
        std::vector<uint8_t> out = readFile(target);
        ASSERT_GT(out.size(), 12u);
        EXPECT_EQ(out[0], 'T');
        EXPECT_EQ(out[1], 'D');
        EXPECT_EQ(out.back(), 0xFF);
        EXPECT_EQ(static_cast<uint16_t>(out[10] | (out[11] << 8)), LoaderTD0::crc16(out.data(), 10));

        // Reloading gives the same sector lists, data and flags: identical model
        LoaderTD0 back(_context, target);
        ASSERT_TRUE(back.loadImage()) << name << ": " << joinWarnings(back.lastWarnings());
        EXPECT_TRUE(back.lastWarnings().empty()) << joinWarnings(back.lastWarnings());
        EXPECT_FALSE(back.isAdvancedCompression());
        DiskImage* reloaded = back.getImage();
        EXPECT_EQ(back.getDescription(), loader.getDescription());
        EXPECT_EQ(back.hasComment(), loader.hasComment());
        EXPECT_EQ(back.getCommentDate().year, loader.getCommentDate().year);
        EXPECT_EQ(back.getCommentDate().second, loader.getCommentDate().second);
        EXPECT_EQ(back.getDriveType(), loader.getDriveType());
        expectSameModel(image, reloaded, name);

        removeFile(target);
        delete reloaded;
        delete image;
    }
}

TEST_F(LoaderTD0_Test, Save_Serialize_ByteLayout)
{
    // One track: a pattern sector (encoding 1), a raw sector, an ID-only sector, a deleted sector, a CRC-error sector
    DiskImage image(1, 1);
    DiskImage::TrackFormatSpec spec = DiskImage::TrackFormatSpec::trdos(nullptr, 5);
    spec.idOnly = {0, 0, 1, 0, 0};
    DiskImage::Track* track = image.getTrack(0);
    track->formatTrack(0, 0, spec);
    for (size_t i = 0; i < 256; i += 2) { track->getRawSector(0)->data[i] = 0xDE; track->getRawSector(0)->data[i + 1] = 0xAD; }
    track->getRawSector(0)->recalculateDataCRC();
    std::vector<uint8_t> raw = lcgBytes(99, 256);
    std::memcpy(track->getRawSector(1)->data, raw.data(), 256);
    track->getRawSector(1)->recalculateDataCRC();
    track->getRawSector(3)->setDataAddressMark(0xF8);
    track->getRawSector(3)->recalculateDataCRC();
    track->getRawSector(4)->setDataCRC(static_cast<uint16_t>(track->getRawSector(4)->dataCRC() ^ 0xFFFF));
    track->reindex();

    LoaderTD0CUT loader(_context, "");
    loader._description = "abc\ndef";
    LoaderTD0::CommentDate date;
    date.year = 2000; date.month = 1; date.day = 2; date.hour = 3; date.minute = 4; date.second = 5;
    loader.setCommentDate(date);

    std::vector<uint8_t> out;
    std::vector<std::string> warnings;
    ASSERT_TRUE(loader.serialize(&image, out, warnings));
    EXPECT_TRUE(warnings.empty()) << joinWarnings(warnings);

    // Header
    ASSERT_GT(out.size(), 12u + 10u + 7u);
    EXPECT_EQ(out[0], 'T');
    EXPECT_EQ(out[4], 0x15);
    EXPECT_EQ(out[5], 0);          // 250 kbps MFM
    EXPECT_EQ(out[7], 0x80);       // comment present
    EXPECT_EQ(out[9], 1);          // single sided
    EXPECT_EQ(static_cast<uint16_t>(out[10] | (out[11] << 8)), CRCHelper::crc16(out.data(), 10));

    // Comment block: CRC, length 7, date, "abc\0def"
    size_t p = 12;
    const uint16_t commentCrc = static_cast<uint16_t>(out[p] | (out[p + 1] << 8));
    EXPECT_EQ(commentCrc, CRCHelper::crc16(out.data() + p + 2, 8 + 7));
    EXPECT_EQ(out[p + 2], 7);
    EXPECT_EQ(out[p + 3], 0);
    EXPECT_EQ(out[p + 4], 100);    // 2000 - 1900
    EXPECT_EQ(out[p + 5], 0);      // January
    EXPECT_EQ(out[p + 6], 2);
    EXPECT_EQ(out[p + 7], 3);
    EXPECT_EQ(out[p + 8], 4);
    EXPECT_EQ(out[p + 9], 5);
    EXPECT_EQ(std::memcmp(out.data() + p + 10, "abc\0def", 7), 0);
    p += 10 + 7;

    // Track header: 5 sectors, cylinder 0, head 0, CRC-8
    EXPECT_EQ(out[p], 5);
    EXPECT_EQ(out[p + 1], 0);
    EXPECT_EQ(out[p + 2], 0);
    EXPECT_EQ(out[p + 3], CRCHelper::crc16(out.data() + p, 3) & 0xFF);
    p += 4;

    // Sector 1: pattern DE AD -> encoding 1, block length 5
    EXPECT_EQ(out[p + 2], 1);
    EXPECT_EQ(out[p + 3], 1);
    EXPECT_EQ(out[p + 4], 0);
    std::vector<uint8_t> pat(256);
    for (size_t i = 0; i < 256; i += 2) { pat[i] = 0xDE; pat[i + 1] = 0xAD; }
    EXPECT_EQ(out[p + 5], CRCHelper::crc16(pat.data(), 256) & 0xFF);
    EXPECT_EQ(out[p + 6], 5);
    EXPECT_EQ(out[p + 7], 0);
    EXPECT_EQ(out[p + 8], 1);
    EXPECT_EQ(out[p + 9], 128);
    EXPECT_EQ(out[p + 10], 0);
    EXPECT_EQ(out[p + 11], 0xDE);
    EXPECT_EQ(out[p + 12], 0xAD);
    p += 6 + 2 + 5;

    // Sector 2: raw, block length 257
    EXPECT_EQ(out[p + 2], 2);
    EXPECT_EQ(out[p + 4], 0);
    EXPECT_EQ(out[p + 5], CRCHelper::crc16(raw.data(), 256) & 0xFF);
    EXPECT_EQ(out[p + 6], 0x01);
    EXPECT_EQ(out[p + 7], 0x01);
    EXPECT_EQ(out[p + 8], 0);
    EXPECT_EQ(std::memcmp(out.data() + p + 9, raw.data(), 256), 0);
    p += 6 + 2 + 257;

    // Sector 3: ID only, flag 0x20, no data block
    EXPECT_EQ(out[p + 2], 3);
    EXPECT_EQ(out[p + 4], 0x20);
    EXPECT_EQ(out[p + 5], 0);
    p += 6;

    // Sector 4: deleted (data all zero -> pattern encoding)
    EXPECT_EQ(out[p + 2], 4);
    EXPECT_EQ(out[p + 4], 0x04);
    p += 6 + 2 + 5;

    // Sector 5: CRC error
    EXPECT_EQ(out[p + 2], 5);
    EXPECT_EQ(out[p + 4], 0x02);
    p += 6 + 2 + 5;

    EXPECT_EQ(out[p], 0xFF);
    EXPECT_EQ(out.size(), p + 1);

    // ... and it parses back
    LoaderTD0 back(_context, "");
    std::vector<uint8_t> in = out;
    DiskImage* reloaded = back.parse(in.data(), in.size(), warnings);
    ASSERT_NE(reloaded, nullptr) << joinWarnings(warnings);
    EXPECT_TRUE(warnings.empty()) << joinWarnings(warnings);
    EXPECT_EQ(back.getDescription(), "abc\ndef");
    EXPECT_EQ(back.getCommentDate().year, 2000);
    EXPECT_EQ(back.getCommentDate().month, 1);
    expectSameModel(&image, reloaded, "serialize/parse");
    delete reloaded;
}

TEST_F(LoaderTD0_Test, Save_FromTrd_ValidatesAsTrdos)
{
    std::string target = scratch("from-trd.td0");
    removeFile(target);

    LoaderTRD trd(_context, fixture("loaders/trd/EyeAche.trd"));
    ASSERT_TRUE(trd.loadImage());
    DiskImage* image = trd.getImage();

    LoaderTD0 td0(_context, target);
    td0.setImage(image);
    td0.setDescription("EyeAche via TD0");
    ASSERT_TRUE(td0.writeImage());
    EXPECT_TRUE(td0.lastWarnings().empty()) << joinWarnings(td0.lastWarnings());
    delete image;

    LoaderTD0 back(_context, target);
    ASSERT_TRUE(back.loadImage()) << joinWarnings(back.lastWarnings());
    DiskImage* reloaded = back.getImage();
    EXPECT_EQ(back.getDescription(), "EyeAche via TD0");
    EXPECT_TRUE(LoaderTRD::isTrdosGeometry(reloaded));
    TRDValidationReport report;
    EXPECT_TRUE(trd.validateTRDOSImage(reloaded, report));

    std::string trdPath = scratch("from-td0.trd");
    LoaderTRD trdOut(_context, trdPath);
    trdOut.setImage(reloaded);
    ASSERT_TRUE(trdOut.writeImage(trdPath));
    std::vector<uint8_t> a = readFile(fixture("loaders/trd/EyeAche.trd"));
    std::vector<uint8_t> b = readFile(trdPath);
    ASSERT_EQ(a.size(), b.size());
    EXPECT_EQ(std::memcmp(a.data(), b.data(), a.size()), 0);

    removeFile(target);
    removeFile(trdPath);
    delete reloaded;
}

/// endregion </Save>

/// region <Negative>

TEST_F(LoaderTD0_Test, Negative_BadHeaderCrc)
{
    std::vector<uint8_t> d = readFile(fixture("loaders/td0/protected-sample.td0"));
    ASSERT_GT(d.size(), 12u);
    LoaderTD0 loader(_context, "");
    std::vector<std::string> warnings;

    // Corrupt a header field
    std::vector<uint8_t> bad = d;
    bad[6] ^= 0x01;
    EXPECT_EQ(loader.parse(bad.data(), bad.size(), warnings), nullptr);
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings[0].find("header CRC"), std::string::npos) << warnings[0];

    // Corrupt the stored CRC itself
    warnings.clear();
    bad = d;
    bad[11] ^= 0x80;
    EXPECT_EQ(loader.parse(bad.data(), bad.size(), warnings), nullptr);
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings[0].find("header CRC"), std::string::npos);

    // Wrong signature
    warnings.clear();
    bad = d;
    bad[0] = 'X';
    EXPECT_EQ(loader.parse(bad.data(), bad.size(), warnings), nullptr);
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings[0].find("signature"), std::string::npos);
}

TEST_F(LoaderTD0_Test, Negative_Truncated)
{
    std::vector<uint8_t> d = readFile(fixture("loaders/td0/protected-sample.td0"));
    ASSERT_GT(d.size(), 2000u);
    LoaderTD0 loader(_context, "");
    std::vector<std::string> warnings;

    // Shorter than the header
    EXPECT_EQ(loader.parse(d.data(), 8, warnings), nullptr);
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings.back().find("truncated"), std::string::npos) << warnings.back();

    // Inside the comment block
    warnings.clear();
    EXPECT_EQ(loader.parse(d.data(), 20, warnings), nullptr);
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings.back().find("truncated"), std::string::npos) << warnings.back();

    // Inside a sector data block
    warnings.clear();
    EXPECT_EQ(loader.parse(d.data(), 2000, warnings), nullptr);
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings.back().find("truncated"), std::string::npos) << warnings.back();

    // Inside a sector header list
    warnings.clear();
    EXPECT_EQ(loader.parse(d.data(), d.size() - 3, warnings), nullptr);
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings.back().find("truncated"), std::string::npos) << warnings.back();

    // Missing end-of-image marker only: loads with a warning
    warnings.clear();
    DiskImage* image = loader.parse(d.data(), d.size() - 1, warnings);
    ASSERT_NE(image, nullptr) << joinWarnings(warnings);
    EXPECT_TRUE(hasWarning(warnings, "end-of-image")) << joinWarnings(warnings);
    delete image;

    // Truncated "td" stream: the decoder stops at the end of the input and the parser reports the cut
    std::vector<uint8_t> adv = readFile(fixture("loaders/td0/trdos-sample-adv.td0"));
    ASSERT_GT(adv.size(), 5000u);
    warnings.clear();
    EXPECT_EQ(loader.parse(adv.data(), 5000, warnings), nullptr);
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings.back().find("truncated"), std::string::npos) << warnings.back();
}

TEST_F(LoaderTD0_Test, Negative_Geometry)
{
    // Build a minimal image with a track header claiming cylinder 90 / head 2
    auto build = [](uint8_t cylinder, uint8_t head)
    {
        std::vector<uint8_t> v = {'T', 'D', 0, 0, 0x15, 0, 3, 0, 0, 2};
        const uint16_t crc = LoaderTD0::crc16(v.data(), 10);
        v.push_back(crc & 0xFF);
        v.push_back(crc >> 8);
        const uint8_t hdr[3] = {0, cylinder, head};
        v.insert(v.end(), hdr, hdr + 3);
        v.push_back(LoaderTD0::crc16(hdr, 3) & 0xFF);
        v.push_back(0xFF);
        return v;
    };

    LoaderTD0 loader(_context, "");
    std::vector<std::string> warnings;

    std::vector<uint8_t> tooManyCylinders = build(90, 0);
    EXPECT_EQ(loader.parse(tooManyCylinders.data(), tooManyCylinders.size(), warnings), nullptr);
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings.back().find("cylinder"), std::string::npos);

    warnings.clear();
    std::vector<uint8_t> threeHeads = build(0, 2);
    EXPECT_EQ(loader.parse(threeHeads.data(), threeHeads.size(), warnings), nullptr);
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings.back().find("head"), std::string::npos);

    // A valid single empty track loads as a 1 x 2 unformatted image
    warnings.clear();
    std::vector<uint8_t> ok = build(0, 0);
    DiskImage* image = loader.parse(ok.data(), ok.size(), warnings);
    ASSERT_NE(image, nullptr) << joinWarnings(warnings);
    EXPECT_EQ(image->getCylinders(), 1);
    EXPECT_EQ(image->getSides(), 2);
    EXPECT_EQ(image->getTrack(0)->sectorCount(), 0u);
    delete image;
}

/// endregion </Negative>
