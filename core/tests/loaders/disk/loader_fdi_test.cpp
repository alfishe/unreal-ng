#include "loaders/disk/loader_fdi.h"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <map>
#include <vector>

#include <common/filehelper.h>
#include <common/stringhelper.h>

#include "_helpers/testpathhelper.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/fdc/diskimage.h"
#include "emulator/io/fdc/wd1793.h"
#include "loaders/disk/loader_trd.h"
#include "loaders/disk/loader_udi.h"
#include "3rdparty/message-center/messagecenter.h"

/// FDI loader tests (docs/inprogress/2026-09-02-universal-track-model/loader-fdi.md, section 4)

using Spec = DiskImage::TrackFormatSpec;

class LoaderFDI_Test : public ::testing::Test
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
        // Clean up MessageCenter FIRST to dispose pending messages and observers
        // before destroying the objects they reference
        MessageCenter::DisposeDefaultMessageCenter();

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

    /// Header of an FDI fixture as parsed by the test itself (independent of the loader)
    struct RawSector { uint8_t c, h, r, n, flags; uint16_t off; };
    struct RawTrack { uint32_t dataOffset; std::vector<RawSector> sectors; };
    struct RawFdi { uint16_t cylinders, heads, dataOffset; std::vector<RawTrack> tracks; };

    static RawFdi parseRaw(const std::vector<uint8_t>& d)
    {
        RawFdi f;
        f.cylinders = d[4] | (d[5] << 8);
        f.heads = d[6] | (d[7] << 8);
        f.dataOffset = d[10] | (d[11] << 8);
        size_t off = 14 + (d[12] | (d[13] << 8));
        for (size_t t = 0; t < static_cast<size_t>(f.cylinders) * f.heads; t++)
        {
            RawTrack tr;
            tr.dataOffset = d[off] | (d[off + 1] << 8) | (d[off + 2] << 16) | (d[off + 3] << 24);
            uint8_t n = d[off + 6];
            off += 7;
            for (uint8_t s = 0; s < n; s++, off += 7)
            {
                tr.sectors.push_back({d[off], d[off + 1], d[off + 2], d[off + 3], d[off + 4],
                                      static_cast<uint16_t>(d[off + 5] | (d[off + 6] << 8))});
            }
            f.tracks.push_back(tr);
        }
        return f;
    }

    /// Compare a loaded image against the fixture's own sector lists and data
    static void expectMatchesFixture(DiskImage* image, const std::vector<uint8_t>& d, const char* name)
    {
        RawFdi f = parseRaw(d);
        ASSERT_EQ(image->getCylinders(), f.cylinders) << name;
        ASSERT_EQ(image->getSides(), f.heads) << name;

        for (uint16_t c = 0; c < f.cylinders; c++)
        {
            for (uint16_t h = 0; h < f.heads; h++)
            {
                const RawTrack& rt = f.tracks[c * f.heads + h];
                DiskImage::Track* track = image->getTrackForCylinderAndSide(static_cast<uint8_t>(c), static_cast<uint8_t>(h));
                ASSERT_NE(track, nullptr);
                ASSERT_EQ(track->sectorCount(), rt.sectors.size()) << name << " cylinder " << c << " head " << h;

                for (size_t s = 0; s < rt.sectors.size(); s++)
                {
                    const RawSector& rs = rt.sectors[s];
                    DiskImage::Sector* sector = track->getRawSector(s);
                    EXPECT_EQ(sector->cylinder(), rs.c);
                    EXPECT_EQ(sector->head(), rs.h);
                    EXPECT_EQ(sector->number(), rs.r);
                    EXPECT_EQ(sector->sizeCode(), rs.n);
                    EXPECT_TRUE(sector->idCrcValid);

                    const bool fixtureHasData = !(rs.flags & 0x80);
                    EXPECT_EQ(sector->hasData, fixtureHasData) << name << " c" << c << " h" << h << " s" << (int)rs.r;
                    if (!sector->hasData) continue;

                    EXPECT_EQ(sector->deleted, (rs.flags & 0x40) != 0);
                    const bool crcOk = (rs.flags & (1u << (rs.n & 3))) != 0;
                    EXPECT_EQ(sector->dataCrcValid, crcOk) << name << " c" << c << " h" << h << " s" << (int)rs.r;

                    const uint8_t* src = d.data() + f.dataOffset + rt.dataOffset + rs.off;
                    EXPECT_EQ(std::memcmp(sector->data, src, sector->dataSize), 0) << name << " c" << c << " h" << h << " s" << (int)rs.r;
                }
            }
        }
    }
};

/// region <Detection / geometry>

TEST_F(LoaderFDI_Test, Detect_Signature)
{
    const uint8_t good[] = {'F', 'D', 'I', 0};
    const uint8_t bad[] = {'U', 'D', 'I', '!'};
    EXPECT_TRUE(LoaderFDI::detect(good, sizeof(good)));
    EXPECT_FALSE(LoaderFDI::detect(bad, sizeof(bad)));
    EXPECT_FALSE(LoaderFDI::detect(good, 2));
    EXPECT_FALSE(LoaderFDI::detect(nullptr, 0));
}

TEST_F(LoaderFDI_Test, Load_Voron_Geometry_And_Data)
{
    const char* fixtures[] = {"loaders/fdi/VORON1.FDI", "loaders/fdi/VORON2.FDI"};

    for (const char* name : fixtures)
    {
        std::vector<uint8_t> d = readFile(fixture(name));
        ASSERT_FALSE(d.empty()) << name;

        LoaderFDI loader(_context, fixture(name));
        ASSERT_TRUE(loader.loadImage()) << name << ": " << (loader.lastWarnings().empty() ? "" : loader.lastWarnings()[0]);
        DiskImage* image = loader.getImage();
        ASSERT_NE(image, nullptr);
        EXPECT_TRUE(image->getLoaded());
        EXPECT_FALSE(image->isDirty());
        EXPECT_FALSE(loader.isWriteProtected());
        EXPECT_NE(loader.getDescription().find("fdi image by PGC/BDA"), std::string::npos);

        EXPECT_EQ(image->getCylinders(), 81);
        EXPECT_EQ(image->getSides(), 2);

        expectMatchesFixture(image, d, name);

        // Every track still fits the nominal revolution (5 x 1024 = 5780 bytes with TR-DOS gaps)
        std::map<size_t, size_t> geometries;
        for (size_t t = 0; t < 162; t++)
        {
            DiskImage::Track* track = image->getTrack(static_cast<uint8_t>(t));
            EXPECT_EQ(track->rawSize(), 6250u);
            geometries[track->sectorCount()]++;
        }
        EXPECT_GT(geometries[5], 100u) << "5 x 1024 tracks";
        EXPECT_GT(geometries[16], 10u) << "16 x 256 TR-DOS tracks";

        delete image;
    }
}

TEST_F(LoaderFDI_Test, Load_Voron_MixedSectorSizes_ThroughController)
{
    LoaderFDI loader(_context, fixture("loaders/fdi/VORON2.FDI"));
    ASSERT_TRUE(loader.loadImage());
    DiskImage* image = loader.getImage();

    // Find one 5 x 1024 track and one 9 x 512 track
    DiskImage::Track* big = nullptr;
    DiskImage::Track* mid = nullptr;
    uint8_t bigCyl = 0, bigSide = 0, midCyl = 0, midSide = 0;
    for (uint8_t c = 0; c < 81 && (!big || !mid); c++)
    {
        for (uint8_t h = 0; h < 2; h++)
        {
            DiskImage::Track* t = image->getTrackForCylinderAndSide(c, h);
            if (!big && t->sectorCount() == 5 && t->getRawSector(0)->dataSize == 1024) { big = t; bigCyl = c; bigSide = h; }
            if (!mid && t->sectorCount() == 9 && t->getRawSector(0)->dataSize == 512) { mid = t; midCyl = c; midSide = h; }
        }
    }
    ASSERT_NE(big, nullptr);
    ASSERT_NE(mid, nullptr);

    auto readSector = [&](uint8_t cyl, uint8_t side, uint8_t number) -> std::vector<uint8_t>
    {
        WD1793CUT fdc(_context);
        fdc.getDrive()->insertDisk(image);
        fdc._beta128Register = WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_RESET |
                               (side ? WD1793CUT::BETA128_COMMAND_BITS::BETA_CMD_HEAD : 0);
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
        EXPECT_FALSE(fdc._statusRegister & WD1793::WDS_NOTFOUND);
        fdc.getDrive()->ejectDisk();
        return read;
    };

    DiskImage::Sector* s = big->getRawSector(2);
    std::vector<uint8_t> data = readSector(bigCyl, bigSide, s->number());
    ASSERT_EQ(data.size(), 1024u);
    EXPECT_EQ(std::memcmp(data.data(), s->data, 1024), 0);

    s = mid->getRawSector(4);
    data = readSector(midCyl, midSide, s->number());
    ASSERT_EQ(data.size(), 512u);
    EXPECT_EQ(std::memcmp(data.data(), s->data, 512), 0);

    delete image;
}

/// endregion </Detection / geometry>

/// region <Flags and synthetic layouts>

namespace
{
    /// Minimal FDI builder for synthetic tests
    struct FdiBuilder
    {
        struct S { uint8_t c, h, r, n, flags; std::vector<uint8_t> data; };
        std::vector<std::vector<S>> tracks;  // cylinder-major, one entry per (cyl, head)
        uint16_t cylinders = 1, heads = 1;
        std::string description;
        bool writeProtect = false;

        std::vector<uint8_t> build() const
        {
            std::vector<uint8_t> hdr, data;
            auto p16 = [](std::vector<uint8_t>& v, uint16_t x) { v.push_back(x & 0xFF); v.push_back(x >> 8); };
            auto p32 = [](std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; i++) v.push_back((x >> (8 * i)) & 0xFF); };

            hdr.insert(hdr.end(), {'F', 'D', 'I', static_cast<uint8_t>(writeProtect ? 1 : 0)});
            p16(hdr, cylinders); p16(hdr, heads); p16(hdr, 0); p16(hdr, 0); p16(hdr, 0);

            for (const auto& t : tracks)
            {
                p32(hdr, static_cast<uint32_t>(data.size()));
                p16(hdr, 0);
                hdr.push_back(static_cast<uint8_t>(t.size()));
                uint16_t off = 0;
                for (const S& s : t)
                {
                    hdr.insert(hdr.end(), {s.c, s.h, s.r, s.n, s.flags});
                    p16(hdr, off);
                    if (!(s.flags & 0x80))
                    {
                        data.insert(data.end(), s.data.begin(), s.data.end());
                        off = static_cast<uint16_t>(off + s.data.size());
                    }
                }
            }
            if (!description.empty())
            {
                hdr[8] = hdr.size() & 0xFF; hdr[9] = hdr.size() >> 8;
                hdr.insert(hdr.end(), description.begin(), description.end());
                hdr.push_back(0);
            }
            hdr[10] = hdr.size() & 0xFF; hdr[11] = hdr.size() >> 8;
            hdr.insert(hdr.end(), data.begin(), data.end());
            return hdr;
        }
    };
}

TEST_F(LoaderFDI_Test, Load_Flags)
{
    FdiBuilder b;
    b.description = "flags test";
    b.writeProtect = true;
    b.tracks.push_back({
        {0, 0, 1, 1, 0x02, std::vector<uint8_t>(256, 0x11)},   // ok
        {0, 0, 2, 1, 0x00, std::vector<uint8_t>(256, 0x22)},   // data present, CRC bad
        {0, 0, 3, 1, 0x42, std::vector<uint8_t>(256, 0x33)},   // deleted, CRC ok
        {0, 0, 4, 1, 0x80, {}},                                // ID only
        {0, 0, 5, 1, 0x40, std::vector<uint8_t>(256, 0x55)},   // deleted, CRC bad
    });
    std::vector<uint8_t> data = b.build();

    LoaderFDI loader(_context, "");
    std::vector<std::string> warnings;
    DiskImage* image = loader.parse(data.data(), data.size(), warnings);
    ASSERT_NE(image, nullptr) << (warnings.empty() ? "" : warnings[0]);
    EXPECT_TRUE(loader.isWriteProtected());
    EXPECT_EQ(loader.getDescription(), "flags test");

    DiskImage::Track* track = image->getTrack(0);
    ASSERT_EQ(track->sectorCount(), 5u);

    DiskImage::Sector* s1 = track->getSector(0);
    EXPECT_TRUE(s1->hasData && s1->dataCrcValid && !s1->deleted);
    EXPECT_EQ(s1->data[0], 0x11);

    DiskImage::Sector* s2 = track->getSector(1);
    EXPECT_TRUE(s2->hasData);
    EXPECT_FALSE(s2->dataCrcValid);
    EXPECT_FALSE(s2->isDataCRCValid());
    EXPECT_EQ(s2->data[0], 0x22) << "data is kept even with a bad CRC";

    DiskImage::Sector* s3 = track->getSector(2);
    EXPECT_TRUE(s3->hasData && s3->deleted && s3->dataCrcValid);
    EXPECT_EQ(s3->dataAddressMark(), 0xF8);

    DiskImage::Sector* s4 = track->getSector(3);
    EXPECT_FALSE(s4->hasData);
    EXPECT_EQ(s4->data, nullptr);
    EXPECT_EQ(track->getDataForSector(3), nullptr);

    DiskImage::Sector* s5 = track->getSector(4);
    EXPECT_TRUE(s5->hasData && s5->deleted);
    EXPECT_FALSE(s5->dataCrcValid);

    delete image;
}

TEST_F(LoaderFDI_Test, Load_MixedSizes_And_CustomIds)
{
    FdiBuilder b;
    b.tracks.push_back({
        {0, 0, 1, 0, 0x01, std::vector<uint8_t>(128, 0xA0)},
        {0, 0, 2, 1, 0x02, std::vector<uint8_t>(256, 0xA1)},
        {40, 1, 7, 2, 0x04, std::vector<uint8_t>(512, 0xA2)},   // foreign C/H in the ID (copy protection style)
        {0, 0, 4, 3, 0x08, std::vector<uint8_t>(1024, 0xA3)},
    });
    std::vector<uint8_t> data = b.build();

    LoaderFDI loader(_context, "");
    std::vector<std::string> warnings;
    DiskImage* image = loader.parse(data.data(), data.size(), warnings);
    ASSERT_NE(image, nullptr);
    EXPECT_TRUE(warnings.empty());

    DiskImage::Track* track = image->getTrack(0);
    ASSERT_EQ(track->sectorCount(), 4u);
    EXPECT_EQ(track->rawSize(), 6250u);
    EXPECT_EQ(track->getRawSector(0)->dataSize, 128);
    EXPECT_EQ(track->getRawSector(1)->dataSize, 256);
    EXPECT_EQ(track->getRawSector(2)->dataSize, 512);
    EXPECT_EQ(track->getRawSector(3)->dataSize, 1024);
    EXPECT_EQ(track->getRawSector(2)->cylinder(), 40);
    EXPECT_EQ(track->getRawSector(2)->head(), 1);
    EXPECT_EQ(track->getRawSector(2)->number(), 7);
    for (size_t i = 0; i < 4; i++)
    {
        EXPECT_TRUE(track->getRawSector(i)->idCrcValid && track->getRawSector(i)->dataCrcValid) << i;
        EXPECT_EQ(track->getRawSector(i)->data[0], 0xA0 + i);
    }

    delete image;
}

TEST_F(LoaderFDI_Test, Load_TooManySectors_GrowsTrack_Or_Refuses)
{
    // 10 x 512 with TR-DOS gaps = 10 x 644 = 6440 > 6250: gap3 shrinks
    FdiBuilder b;
    std::vector<FdiBuilder::S> ten;
    for (uint8_t i = 1; i <= 10; i++) ten.push_back({0, 0, i, 2, 0x04, std::vector<uint8_t>(512, i)});
    b.tracks.push_back(ten);
    std::vector<uint8_t> data = b.build();

    LoaderFDI loader(_context, "");
    std::vector<std::string> warnings;
    DiskImage* image = loader.parse(data.data(), data.size(), warnings);
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->getTrack(0)->sectorCount(), 10u);
    EXPECT_EQ(image->getTrack(0)->rawSize(), 6250u);
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings[0].find("gap3 reduced"), std::string::npos);
    delete image;

    // 8 x 1024 needs > 6250 even with minimum gaps (8 x 1112 = 8896): track grows
    FdiBuilder big;
    std::vector<FdiBuilder::S> twelve;
    for (uint8_t i = 1; i <= 8; i++) twelve.push_back({0, 0, i, 3, 0x08, std::vector<uint8_t>(1024, i)});
    big.tracks.push_back(twelve);
    data = big.build();
    warnings.clear();
    image = loader.parse(data.data(), data.size(), warnings);
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->getTrack(0)->sectorCount(), 8u);
    EXPECT_GT(image->getTrack(0)->rawSize(), 6250u);
    EXPECT_LE(image->getTrack(0)->rawSize(), DiskImage::RawTrack::MAX_TRACK_SIZE);
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings[0].find("track length grown"), std::string::npos);
    delete image;

    // 30 x 1024 cannot fit at all: refused
    FdiBuilder huge;
    std::vector<FdiBuilder::S> thirty;
    for (uint8_t i = 1; i <= 30; i++) thirty.push_back({0, 0, i, 3, 0x08, std::vector<uint8_t>(1024, i)});
    huge.tracks.push_back(thirty);
    data = huge.build();
    warnings.clear();
    EXPECT_EQ(loader.parse(data.data(), data.size(), warnings), nullptr);
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings[0].find("exceed"), std::string::npos);
}

TEST_F(LoaderFDI_Test, Load_EmptyTrack_And_ZeroSectors)
{
    FdiBuilder b;
    b.cylinders = 2;
    b.tracks.push_back({{0, 0, 1, 1, 0x02, std::vector<uint8_t>(256, 0x01)}});
    b.tracks.push_back({});  // unformatted track
    std::vector<uint8_t> data = b.build();

    LoaderFDI loader(_context, "");
    std::vector<std::string> warnings;
    DiskImage* image = loader.parse(data.data(), data.size(), warnings);
    ASSERT_NE(image, nullptr);
    EXPECT_EQ(image->getTrack(1)->sectorCount(), 0u);
    EXPECT_EQ(image->getTrack(1)->rawSize(), 6250u);
    delete image;
}

/// endregion </Flags and synthetic layouts>

/// region <Save>

TEST_F(LoaderFDI_Test, Save_RoundTrip_SectorLists)
{
    const char* fixtures[] = {"loaders/fdi/VORON1.FDI", "loaders/fdi/VORON2.FDI"};

    for (const char* name : fixtures)
    {
        std::string target = scratch("roundtrip.fdi");
        removeFile(target);

        LoaderFDI loader(_context, fixture(name));
        ASSERT_TRUE(loader.loadImage()) << name;
        DiskImage* image = loader.getImage();

        ASSERT_TRUE(loader.writeImage(target)) << name;
        EXPECT_TRUE(loader.lastWarnings().empty()) << (loader.lastWarnings().empty() ? "" : loader.lastWarnings()[0]);
        EXPECT_EQ(image->getFilePath(), target);

        // The written file describes exactly the same sectors and data as the original
        std::vector<uint8_t> in = readFile(fixture(name));
        std::vector<uint8_t> out = readFile(target);
        RawFdi a = parseRaw(in);
        RawFdi bb = parseRaw(out);
        ASSERT_EQ(bb.tracks.size(), a.tracks.size());
        for (size_t t = 0; t < a.tracks.size(); t++)
        {
            ASSERT_EQ(bb.tracks[t].sectors.size(), a.tracks[t].sectors.size()) << name << " track " << t;
            for (size_t s = 0; s < a.tracks[t].sectors.size(); s++)
            {
                const RawSector& x = a.tracks[t].sectors[s];
                const RawSector& y = bb.tracks[t].sectors[s];
                EXPECT_EQ(x.c, y.c); EXPECT_EQ(x.h, y.h); EXPECT_EQ(x.r, y.r); EXPECT_EQ(x.n, y.n);
                EXPECT_EQ(x.flags, y.flags) << name << " track " << t << " sector " << (int)x.r;
                if (!(x.flags & 0x80))
                {
                    size_t size = 128u << (x.n & 3);
                    EXPECT_EQ(std::memcmp(in.data() + a.dataOffset + a.tracks[t].dataOffset + x.off,
                                          out.data() + bb.dataOffset + bb.tracks[t].dataOffset + y.off, size), 0);
                }
            }
        }

        // Reloading the written file gives an identical model
        LoaderFDI back(_context, target);
        ASSERT_TRUE(back.loadImage());
        DiskImage* reloaded = back.getImage();
        EXPECT_EQ(back.getDescription(), loader.getDescription());
        for (size_t t = 0; t < 162; t++)
        {
            DiskImage::Track* p = image->getTrack(static_cast<uint8_t>(t));
            DiskImage::Track* q = reloaded->getTrack(static_cast<uint8_t>(t));
            ASSERT_EQ(p->rawSize(), q->rawSize());
            EXPECT_EQ(std::memcmp(p->rawData(), q->rawData(), p->rawSize()), 0) << name << " track " << t;
        }

        removeFile(target);
        delete reloaded;
        delete image;
    }
}

TEST_F(LoaderFDI_Test, Save_FromTrd_ValidatesAsTrdos)
{
    std::string target = scratch("from-trd.fdi");
    removeFile(target);

    LoaderTRD trd(_context, fixture("loaders/trd/EyeAche.trd"));
    ASSERT_TRUE(trd.loadImage());
    DiskImage* image = trd.getImage();

    LoaderFDI fdi(_context, target);
    fdi.setImage(image);
    fdi.setDescription("EyeAche via FDI");
    ASSERT_TRUE(fdi.writeImage());
    EXPECT_TRUE(fdi.lastWarnings().empty());
    delete image;

    LoaderFDI back(_context, target);
    ASSERT_TRUE(back.loadImage());
    DiskImage* reloaded = back.getImage();
    EXPECT_EQ(back.getDescription(), "EyeAche via FDI");
    EXPECT_TRUE(LoaderTRD::isTrdosGeometry(reloaded));
    TRDValidationReport report;
    EXPECT_TRUE(trd.validateTRDOSImage(reloaded, report));

    // TR-DOS content survives: FDI -> TRD equals the original TRD
    std::string trdPath = scratch("from-fdi.trd");
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

TEST_F(LoaderFDI_Test, Save_WarnsOnContentFdiCannotStore)
{
    DiskImage image(2, 1);
    image.getTrack(1)->formatTrack(1, 0, Spec::ibm3740());  // FM track

    LoaderFDI fdi(_context, "");
    std::vector<uint8_t> out;
    std::vector<std::string> warnings;
    ASSERT_TRUE(fdi.serialize(&image, out, warnings));
    ASSERT_FALSE(warnings.empty());
    EXPECT_NE(warnings[0].find("FM track"), std::string::npos);

    // ... and UDI keeps the same image without complaint
    LoaderUDI udi(_context, "");
    std::vector<uint8_t> udiOut;
    std::vector<std::string> udiWarnings;
    ASSERT_TRUE(udi.serialize(&image, udiOut, udiWarnings));
    EXPECT_TRUE(udiWarnings.empty());
}

TEST_F(LoaderFDI_Test, Negative_Truncated_And_BadOffsets)
{
    std::vector<uint8_t> d = readFile(fixture("loaders/fdi/VORON1.FDI"));
    LoaderFDI loader(_context, "");
    std::vector<std::string> warnings;

    // Header only
    EXPECT_EQ(loader.parse(d.data(), 10, warnings), nullptr);
    warnings.clear();

    // Cut in the middle of the track headers (the data offset points outside the file)
    EXPECT_EQ(loader.parse(d.data(), 3000, warnings), nullptr);
    ASSERT_FALSE(warnings.empty());
    EXPECT_TRUE(warnings.back().find("truncated") != std::string::npos || warnings.back().find("outside") != std::string::npos)
        << warnings.back();
    warnings.clear();

    // Data area cut: affected sectors become ID-only, the rest loads
    DiskImage* image = loader.parse(d.data(), d.size() - 5000, warnings);
    ASSERT_NE(image, nullptr);
    EXPECT_FALSE(warnings.empty());
    size_t idOnly = 0;
    for (size_t t = 0; t < 162; t++)
    {
        for (const DiskImage::Sector& s : image->getTrack(static_cast<uint8_t>(t))->sectors()) if (!s.hasData) idOnly++;
    }
    EXPECT_GT(idOnly, 1u) << "the fixture has one ID-only sector of its own; the cut must add more";
    delete image;

    // Bad geometry
    std::vector<uint8_t> geom(d.begin(), d.begin() + 100);
    geom[6] = 3;  // 3 heads
    warnings.clear();
    EXPECT_EQ(loader.parse(geom.data(), geom.size(), warnings), nullptr);
}

/// endregion </Save>
