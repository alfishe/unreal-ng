// The Next's esxDOS mode (nextesxdos.h, requirements.md Q11): RST $08 calls served against the FAT volume of SD card 0 on the
// bare machine. Source: D ("NextZXOS and esxDOS APIs", pages 37-58); the card's contents are checked with the independent reader
// (FatVolumeReader) on an image built by FatImageBuilder
#include "stdafx.h"
#include "pch.h"

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/fatimagebuilder.h"
#include "base/featuremanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/storage/fat/fatvolumereader.h"
#include "emulator/io/storage/rawimage.h"
#include "emulator/io/z80n/nextesxdos.h"
#include "emulator/io/z80n/nextstatecodec.h"
#include "emulator/memory/next/nextmemory.h"
#include "emulator/ports/models/portdecoder_next.h"

namespace
{
std::vector<uint8_t> Pattern(size_t size, uint8_t seed)
{
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < size; i++)
        data[i] = static_cast<uint8_t>(i * 7 + seed + (i >> 9));
    return data;
}

constexpr uint16_t kCode = 0xC000, kName = 0xA000, kName2 = 0xA100, kBuffer = 0x8000, kStack = 0xFF00;
constexpr uint8_t kOpen = 0x9A, kClose = 0x9B, kRead = 0x9D, kWrite = 0x9E, kSeek = 0x9F, kGetPos = 0xA0, kFstat = 0xA1,
                  kOpenDir = 0xA3, kReadDir = 0xA4, kTellDir = 0xA5, kSeekDir = 0xA6, kGetCwd = 0xA8, kChdir = 0xA9,
                  kMkdir = 0xAA, kRmdir = 0xAB, kStat = 0xAC, kUnlink = 0xAD, kChmod = 0xAF, kRename = 0xB0;
}  // namespace

class NextEsxDos_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Z80* _z80 = nullptr;
    PortDecoder_Next* _ports = nullptr;
    std::unique_ptr<ScratchFatImage> _image;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("NEXT", LoggerLevel::LogError, RamPowerOn::Zero);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _z80 = _context->pCore->GetZ80();
        _ports = dynamic_cast<PortDecoder_Next*>(_context->pPortDecoder);
        ASSERT_NE(_ports, nullptr);
        ASSERT_TRUE(_context->pFeatureManager->setFeature(Features::kEsxDos, true));
        ASSERT_TRUE(_ports->EsxDosServing()) << "the bare machine with the feature on";

        FatImageSpec spec;
        spec.files = {{"README.TXT", Pattern(1500, 1)}};
        _image = std::make_unique<ScratchFatImage>("esxdos.img", spec);
        ASSERT_TRUE(_image->ok()) << _image->error();
        std::string error;
        std::unique_ptr<RawImage> disk = RawImage::Open(_image->path(), RawImage::Access::ReadWrite, &error);
        ASSERT_NE(disk, nullptr) << error;
        ASSERT_TRUE(_ports->InsertSdCard(0, std::move(disk), SdCardSpi::WriteMode::Persist));
        _ports->I2c().Rtc().SetTime(2026, 10, 26, 15, 30, 20);
    }
    void TearDown() override
    {
        if (_emulator)
        {
            _context->pFeatureManager->setFeature(Features::kEsxDos, false);
            EmulatorTestHelper::CleanupEmulator(_emulator);
        }
    }

    NextMemory& Mem() { return *static_cast<NextMemory*>(_context->pMemory); }
    void Poke(uint16_t address, const std::string& text)
    {
        for (size_t i = 0; i <= text.size(); i++)
            Mem().PokeSlot(static_cast<uint16_t>(address + i), i < text.size() ? static_cast<uint8_t>(text[i]) : 0);
    }
    std::vector<uint8_t> Peek(uint16_t address, size_t count)
    {
        std::vector<uint8_t> out(count);
        for (size_t i = 0; i < count; i++)
            out[i] = Mem().PeekSlot(static_cast<uint16_t>(address + i));
        return out;
    }
    std::string PeekString(uint16_t address)
    {
        std::string out;
        for (uint8_t c; (c = Mem().PeekSlot(address)) != 0; address++)
            out.push_back(static_cast<char>(c));
        return out;
    }

    /// RST $08 : DEFB id from `code`, the registers set before; true when the call returned carry (an error)
    bool Call(uint8_t id, uint16_t code = kCode)
    {
        Mem().PokeSlot(code, 0xCF);
        Mem().PokeSlot(static_cast<uint16_t>(code + 1), id);
        _z80->pc = code;
        _z80->sp = kStack;
        _z80->EngineStep();  // the RST
        EXPECT_EQ(_z80->pc, 0x0008);
        _z80->EngineStep();  // served
        EXPECT_EQ(_z80->pc, code + 2) << "back past the call byte";
        EXPECT_EQ(_z80->sp, kStack);
        return (_z80->f & 0x01) != 0;
    }
    /// F_OPEN of the name at kName; the handle
    uint8_t Open(const std::string& name, uint8_t mode, uint16_t header = 0)
    {
        Poke(kName, name);
        _z80->a = '*';
        _z80->ix = kName;
        _z80->b = mode;
        _z80->de = header;
        EXPECT_FALSE(Call(kOpen)) << name << " error " << int(_z80->a);
        return _z80->a;
    }
    uint16_t Read(uint8_t handle, uint16_t count, uint16_t to = kBuffer)
    {
        _z80->a = handle;
        _z80->ix = to;
        _z80->bc = count;
        EXPECT_FALSE(Call(kRead));
        return _z80->bc;
    }
    uint16_t Write(uint8_t handle, const std::vector<uint8_t>& data)
    {
        for (size_t i = 0; i < data.size(); i++)
            Mem().PokeSlot(static_cast<uint16_t>(kBuffer + i), data[i]);
        _z80->a = handle;
        _z80->ix = kBuffer;
        _z80->bc = static_cast<uint16_t>(data.size());
        EXPECT_FALSE(Call(kWrite));
        return _z80->bc;
    }
    void Close(uint8_t handle)
    {
        _z80->a = handle;
        EXPECT_FALSE(Call(kClose));
    }
    /// A path call (A = '*', IX = the path); its error code, 0 when it succeeded
    uint8_t PathCall(uint8_t id, const std::string& path)
    {
        Poke(kName, path);
        _z80->a = '*';
        _z80->ix = kName;
        return Call(id) ? _z80->a : 0;
    }
    /// The card as the independent reader sees it
    std::vector<uint8_t> ReadBack(const std::string& path)
    {
        std::string error;
        std::unique_ptr<RawImage> disk = RawImage::Open(_image->path(), RawImage::Access::ReadOnly, &error);
        FatVolumeReader reader;
        std::vector<uint8_t> data;
        EXPECT_TRUE(disk && reader.Open(*disk)) << error;
        EXPECT_TRUE(reader.ReadFile(path, data)) << path;
        return data;
    }
    std::vector<FatDirEntryInfo> ListBack(const std::string& path)
    {
        std::string error;
        std::unique_ptr<RawImage> disk = RawImage::Open(_image->path(), RawImage::Access::ReadOnly, &error);
        FatVolumeReader reader;
        std::vector<FatDirEntryInfo> entries;
        EXPECT_TRUE(disk && reader.Open(*disk)) << error;
        EXPECT_TRUE(reader.List(path, entries)) << path;
        return entries;
    }
    /// F_READDIR's name(s) of the next entry; empty at the end
    std::vector<std::string> ReadDir(uint8_t handle)
    {
        _z80->a = handle;
        _z80->ix = kBuffer;
        EXPECT_FALSE(Call(kReadDir));
        if (_z80->a == 0)
            return {};
        std::vector<std::string> names;
        uint16_t p = kBuffer + 1;
        names.push_back(PeekString(p));
        return names;
    }
};

TEST_F(NextEsxDos_Test, TheMachineReportsNextZxosOnDriveC)
{
    EXPECT_FALSE(Call(0x88));  // M_DOSVERSION
    EXPECT_EQ(_z80->b, 'N');
    EXPECT_EQ(_z80->c, 'X');
    EXPECT_EQ(_z80->de, 0x0207);
    EXPECT_EQ(_z80->l, 'e');
    EXPECT_EQ(_z80->h, 'n');
    EXPECT_EQ(_z80->a, 0) << "the NextZXOS mode";
    EXPECT_NE(_z80->f & 0x40, 0) << "Z set";

    _z80->a = 0;
    EXPECT_FALSE(Call(0x89));  // M_GETSETDRV: get
    EXPECT_EQ(_z80->a, 2 << 3) << "C:";
    _z80->a = (3 << 3) | 1;
    EXPECT_TRUE(Call(0x89)) << "D: is not there";
    EXPECT_EQ(_z80->a, NextEsxDos::kNoDrive);

    EXPECT_FALSE(Call(0x8E));  // M_GETDATE from the RTC
    EXPECT_EQ(_z80->bc, ((2026 - 1980) << 9) | (10 << 5) | 26);
    EXPECT_EQ(_z80->de, (15 << 11) | (30 << 5) | (20 / 2));
    EXPECT_EQ(_z80->h, 20) << "the seconds";
    _ports->I2c().Rtc().SetReg(5, 0x13);  // month 13
    EXPECT_TRUE(Call(0x8E)) << "no valid date";
    EXPECT_EQ(_z80->bc, 0);

    EXPECT_TRUE(Call(0x8B)) << "M_TAPEIN is NextZXOS's own";
    EXPECT_EQ(_z80->a, NextEsxDos::kNoCommand);
}

TEST_F(NextEsxDos_Test, ACallBelow80IsTheRomsErrorReport)
{
    Mem().PokeSlot(kCode, 0xCF);
    Mem().PokeSlot(kCode + 1, 0x0B);  // RST 8 : DEFB 11 (error C)
    _z80->pc = kCode;
    _z80->sp = kStack;
    _z80->EngineStep();
    _z80->EngineStep();
    EXPECT_NE(_z80->pc, kCode + 2) << "the ROM's code at #0008 ran";
    EXPECT_EQ(_z80->sp, kStack - 2) << "the return address stays for the ROM";
}

TEST_F(NextEsxDos_Test, AFileIsReadSeekedAndStatedAsOnTheBoard)
{
    const std::vector<uint8_t> expected = Pattern(1500, 1);
    const uint8_t handle = Open("readme.txt", 0x01);
    EXPECT_EQ(Read(handle, 1000), 1000);
    EXPECT_EQ(_z80->de, 1000) << "DE has the count too";
    EXPECT_EQ(_z80->hl, kBuffer + 1000) << "HL past the bytes";
    EXPECT_EQ(Peek(kBuffer, 1000), std::vector<uint8_t>(expected.begin(), expected.begin() + 1000));

    _z80->a = handle;
    _z80->bc = 0;
    _z80->de = 1400;
    _z80->xl = 0;  // seek set
    EXPECT_FALSE(Call(kSeek));
    EXPECT_EQ(Read(handle, 1000), 100) << "short at the end, no error";
    _z80->a = handle;
    EXPECT_FALSE(Call(kGetPos));
    EXPECT_EQ(_z80->de, 1500);
    _z80->a = handle;
    _z80->bc = 0;
    _z80->de = 2000;
    _z80->xl = 2;  // back past the start stops at 0
    EXPECT_FALSE(Call(kSeek));
    EXPECT_EQ(_z80->de, 0);

    _z80->a = handle;
    _z80->ix = kBuffer;
    EXPECT_FALSE(Call(kFstat));
    const std::vector<uint8_t> stat = Peek(kBuffer, 11);
    EXPECT_EQ(stat[0], '*');
    EXPECT_EQ(stat[1], 0x81);
    EXPECT_EQ(stat[7] | (stat[8] << 8), 1500);
    Close(handle);
    _z80->a = handle;
    _z80->ix = kBuffer;
    _z80->bc = 10;
    EXPECT_TRUE(Call(kRead));
    EXPECT_EQ(_z80->a, NextEsxDos::kBadHandle);

    // From a dot command's page the pointer is HL
    Poke(kName, "README.TXT");
    _z80->a = '*';
    _z80->hl = kName;
    _z80->ix = 0;
    _z80->de = kBuffer;
    EXPECT_FALSE(Call(kStat, 0x2100));
    EXPECT_EQ(Peek(kBuffer + 7, 2), (std::vector<uint8_t>{1500 & 0xFF, 1500 >> 8}));
    EXPECT_EQ(PathCall(kStat, "NOPE.TXT"), NextEsxDos::kNoEntry);
}

TEST_F(NextEsxDos_Test, WritesReachTheCardWithTheRtcDate)
{
    EXPECT_EQ(PathCall(kMkdir, "C:/GAMES"), 0);
    const uint8_t handle = Open("/games/save.dat", 0x02 | 0x08);
    const std::vector<uint8_t> data = Pattern(3000, 5);
    EXPECT_EQ(Write(handle, std::vector<uint8_t>(data.begin(), data.begin() + 2000)), 2000);
    EXPECT_EQ(Write(handle, std::vector<uint8_t>(data.begin() + 2000, data.end())), 1000);
    Close(handle);
    EXPECT_EQ(ReadBack("/GAMES/SAVE.DAT"), data);
    for (const FatDirEntryInfo& e : ListBack("/GAMES"))
    {
        if (e.shortName == "SAVE.DAT")
        {
            EXPECT_EQ(e.date, ((2026 - 1980) << 9) | (10 << 5) | 26) << "the board's RTC dates the write";
        }
    }

    // Create-new refuses an existing file; read-only refuses a write open
    Poke(kName, "/GAMES/SAVE.DAT");
    _z80->a = '*';
    _z80->ix = kName;
    _z80->b = 0x02 | 0x04;
    EXPECT_TRUE(Call(kOpen));
    EXPECT_EQ(_z80->a, NextEsxDos::kExists);
    Poke(kName, "/GAMES/SAVE.DAT");
    _z80->a = '*';
    _z80->ix = kName;
    _z80->b = 0x00;  // A_WRITE off
    _z80->c = 0x01;
    EXPECT_FALSE(Call(kChmod));
    Poke(kName, "/GAMES/SAVE.DAT");
    _z80->ix = kName;
    _z80->a = '*';
    _z80->b = 0x02;
    EXPECT_TRUE(Call(kOpen));
    EXPECT_EQ(_z80->a, NextEsxDos::kAccess);

    // Truncate on open
    const uint8_t again = Open("/GAMES/NEW.BIN", 0x02 | 0x0C);
    EXPECT_EQ(Write(again, {1, 2, 3}), 3);
    Close(again);
    const uint8_t trunc = Open("/GAMES/NEW.BIN", 0x02 | 0x0C);
    Close(trunc);
    EXPECT_TRUE(ReadBack("/GAMES/NEW.BIN").empty());
}

TEST_F(NextEsxDos_Test, APlus3DosHeaderIsWrittenAndHidden)
{
    const uint8_t basic[8] = {3, 0x00, 0x1B, 0x00, 0x40, 0, 0, 0};  // CODE, 6912 bytes at 16384
    for (unsigned i = 0; i < 8; i++)
        Mem().PokeSlot(static_cast<uint16_t>(0xA200 + i), basic[i]);
    const uint8_t handle = Open("SCREEN.BIN", 0x02 | 0x0C | 0x40, 0xA200);
    const std::vector<uint8_t> data = Pattern(6912, 9);
    EXPECT_EQ(Write(handle, data), 6912);
    Close(handle);

    const std::vector<uint8_t> file = ReadBack("/SCREEN.BIN");
    ASSERT_EQ(file.size(), 128u + 6912u);
    EXPECT_EQ(std::memcmp(file.data(), "PLUS3DOS\x1A", 9), 0);
    EXPECT_EQ(file[11] | (file[12] << 8) | (file[13] << 16), 128 + 6912) << "the length with the header";
    EXPECT_EQ(std::vector<uint8_t>(file.begin() + 15, file.begin() + 23), std::vector<uint8_t>(basic, basic + 8));
    uint8_t sum = 0;
    for (size_t i = 0; i < 127; i++)
        sum = static_cast<uint8_t>(sum + file[i]);
    EXPECT_EQ(file[127], sum);
    EXPECT_EQ(std::vector<uint8_t>(file.begin() + 128, file.end()), data);

    for (unsigned i = 0; i < 8; i++)
        Mem().PokeSlot(static_cast<uint16_t>(0xA200 + i), 0);
    const uint8_t reader = Open("SCREEN.BIN", 0x01 | 0x40, 0xA200);
    EXPECT_EQ(Peek(0xA200, 8), std::vector<uint8_t>(basic, basic + 8));
    EXPECT_EQ(Read(reader, 16), 16);
    EXPECT_EQ(Peek(kBuffer, 16), std::vector<uint8_t>(data.begin(), data.begin() + 16)) << "the data after the header";
    Close(reader);
    const uint8_t plain = Open("README.TXT", 0x01 | 0x40, 0xA200);
    EXPECT_EQ(Peek(0xA200, 1)[0], 0xFF) << "a headerless file";
    Close(plain);
}

TEST_F(NextEsxDos_Test, DirectoriesAreListedEnteredAndRemoved)
{
    ASSERT_EQ(PathCall(kMkdir, "GAMES"), 0);
    ASSERT_EQ(PathCall(kChdir, "games"), 0);
    _z80->a = '*';
    _z80->ix = kBuffer;
    EXPECT_FALSE(Call(kGetCwd));
    EXPECT_EQ(PeekString(kBuffer), "/GAMES/");
    EXPECT_EQ(_ports->EsxDos().WorkingDirectory(), "/GAMES");
    const uint8_t file = Open("Long Name File.txt", 0x02 | 0x08);
    Close(file);

    Poke(kName, ".");
    _z80->a = '*';
    _z80->ix = kName;
    _z80->b = 0x10;  // long names
    ASSERT_FALSE(Call(kOpenDir));
    const uint8_t dir = _z80->a;
    EXPECT_EQ(ReadDir(dir), std::vector<std::string>{"."});
    EXPECT_EQ(ReadDir(dir), std::vector<std::string>{".."});
    _z80->a = dir;
    EXPECT_FALSE(Call(kTellDir));
    const uint16_t place = _z80->de;
    EXPECT_EQ(ReadDir(dir), std::vector<std::string>{"Long Name File.txt"});
    EXPECT_EQ(Peek(kBuffer, 1)[0] & 0x10, 0) << "a file";
    EXPECT_TRUE(ReadDir(dir).empty()) << "the end";
    _z80->a = dir;
    _z80->bc = 0;
    _z80->de = place;
    EXPECT_FALSE(Call(kSeekDir));
    EXPECT_EQ(ReadDir(dir), std::vector<std::string>{"Long Name File.txt"});
    Close(dir);

    Poke(kName, "/GAMES");
    _z80->a = '*';
    _z80->ix = kName;
    _z80->b = 0x00;  // short names
    ASSERT_FALSE(Call(kOpenDir));
    const uint8_t shortDir = _z80->a;
    ReadDir(shortDir);
    ReadDir(shortDir);
    EXPECT_EQ(ReadDir(shortDir), std::vector<std::string>{"LONGNA~1.TXT"});
    Close(shortDir);

    EXPECT_EQ(PathCall(kRmdir, "/GAMES"), NextEsxDos::kDirInUse) << "the working directory";
    ASSERT_EQ(PathCall(kChdir, ".."), 0);
    EXPECT_EQ(PathCall(kRmdir, "/GAMES"), NextEsxDos::kAccess) << "not empty";
    EXPECT_EQ(PathCall(kUnlink, "GAMES/longna~1.txt"), 0);
    EXPECT_EQ(PathCall(kRmdir, "/GAMES"), 0);
    EXPECT_EQ(ListBack("/").size(), 1u);
}

TEST_F(NextEsxDos_Test, ListingsSortFilterAndMatch)
{
    for (const char* name : {"b.txt", "A.TXT", "c.bin"})
        Close(Open(name, 0x02 | 0x08));
    ASSERT_EQ(PathCall(kMkdir, "ZDIR"), 0);

    auto list = [&](uint8_t mode, uint8_t filter, const std::string& wildcard) {
        Poke(kName, "/");
        Poke(kName2, wildcard);
        _z80->a = '*';
        _z80->ix = kName;
        _z80->b = mode;
        _z80->c = filter;
        _z80->de = kName2;
        EXPECT_FALSE(Call(kOpenDir));
        const uint8_t dir = _z80->a;
        std::vector<std::string> names;
        for (;;)
        {
            _z80->de = kName2;
            const std::vector<std::string> one = ReadDir(dir);
            if (one.empty())
                break;
            names.push_back(one[0]);
        }
        Close(dir);
        return names;
    };
    EXPECT_EQ(list(0x10 | 0x80, 0x08, ""), (std::vector<std::string>{"A.TXT", "b.txt", "c.bin", "README.TXT", "ZDIR"}))
        << "sorted by long name, no case";
    EXPECT_EQ(list(0x10 | 0x80, 0x08 | 0x04 | 0x40, ""), (std::vector<std::string>{"README.TXT", "c.bin", "b.txt", "A.TXT"}))
        << "reversed, no directories";
    EXPECT_EQ(list(0x10 | 0x80, 0x80, ""), (std::vector<std::string>{"ZDIR"})) << "no files";
    const std::vector<std::string> txt = list(0x10 | 0x20, 0, "*.TXT");
    EXPECT_EQ(txt.size(), 3u) << "the wildcard keeps README.TXT, b.txt, A.TXT";
}

TEST_F(NextEsxDos_Test, ARenameCarriesAnOpenFileAlong)
{
    ASSERT_EQ(PathCall(kMkdir, "KEEP"), 0);
    const uint8_t handle = Open("README.TXT", 0x01);
    EXPECT_EQ(Read(handle, 10), 10);
    Poke(kName, "README.TXT");
    Poke(kName2, "/KEEP/Old Readme.txt");
    _z80->a = '*';
    _z80->ix = kName;
    _z80->de = kName2;
    EXPECT_FALSE(Call(kRename));
    EXPECT_EQ(Read(handle, 10), 10) << "the handle follows its entry";
    const std::vector<uint8_t> expected = Pattern(1500, 1);
    EXPECT_EQ(Peek(kBuffer, 10), std::vector<uint8_t>(expected.begin() + 10, expected.begin() + 20));
    EXPECT_EQ(PathCall(kUnlink, "/KEEP/old readme.txt"), NextEsxDos::kInUse);
    Close(handle);
    EXPECT_EQ(ReadBack("/KEEP/Old Readme.txt").size(), 1500u);
}

TEST_F(NextEsxDos_Test, TheFileMapStreamsTheFileThroughThePort)
{
    const uint8_t handle = Open("README.TXT", 0x01);
    _z80->a = handle;
    _z80->ix = kBuffer;
    _z80->de = 4;
    ASSERT_FALSE(Call(0x85));  // DISK_FILEMAP
    EXPECT_EQ(_z80->a, 0x02) << "card 0, block addressing";
    ASSERT_EQ(_z80->de, 3) << "one entry";
    EXPECT_EQ(_z80->hl, kBuffer + 6);
    const std::vector<uint8_t> entry = Peek(kBuffer, 6);
    const uint32_t block = entry[0] | (entry[1] << 8) | (entry[2] << 16) | (static_cast<uint32_t>(entry[3]) << 24);
    EXPECT_GE(entry[4] | (entry[5] << 8), 3) << "1500 bytes: at least three sectors";

    _z80->a = 0x02;
    _z80->ix = static_cast<uint16_t>(block >> 16);
    _z80->de = static_cast<uint16_t>(block);
    ASSERT_FALSE(Call(0x86));  // DISK_STRMSTART
    EXPECT_EQ(_z80->b, 0);
    EXPECT_EQ(_z80->c, 0xEB);
    const std::vector<uint8_t> expected = Pattern(1500, 1);
    std::vector<uint8_t> got;
    for (int i = 0; i < 512; i++)
    {
        _z80->t += 20;  // a byte time between two reads
        got.push_back(_ports->DecodePortIn(0xEB, 0));
    }
    EXPECT_EQ(got, std::vector<uint8_t>(expected.begin(), expected.begin() + 512)) << "the first sector";
    _z80->a = 0x02;
    EXPECT_FALSE(Call(0x87));  // DISK_STRMEND
    EXPECT_EQ(_ports->SpiSelectedLine(), -1) << "deselected";
    Close(handle);
}

TEST_F(NextEsxDos_Test, TheStateImageKeepsTheHandles)
{
    const uint8_t handle = Open("README.TXT", 0x01);
    EXPECT_EQ(Read(handle, 100), 100);
    ASSERT_EQ(PathCall(kMkdir, "SUB"), 0);
    ASSERT_EQ(PathCall(kChdir, "SUB"), 0);
    const std::vector<uint8_t> image = NextStateCodec::Save(*_ports);
    _ports->EsxDos().Reset();
    EXPECT_EQ(_ports->EsxDos().OpenHandles(), 0u);
    ASSERT_TRUE(NextStateCodec::Load(*_ports, image.data(), image.size()));
    EXPECT_EQ(_ports->EsxDos().OpenHandles(), 1u);
    EXPECT_EQ(_ports->EsxDos().WorkingDirectory(), "/SUB");
    _z80->a = handle;
    EXPECT_FALSE(Call(kGetPos));
    EXPECT_EQ(_z80->de, 100);
}

TEST_F(NextEsxDos_Test, OnlyTheBareMachineWithTheFeatureServes)
{
    _context->pFeatureManager->setFeature(Features::kEsxDos, false);
    EXPECT_FALSE(_ports->EsxDosServing());
    EXPECT_EQ(_context->pRst8Trap, nullptr);
    _context->pFeatureManager->setFeature(Features::kEsxDos, true);
    EXPECT_TRUE(_ports->EsxDosServing());
    std::strncpy(_context->config.next_boot_rom_path, "boot.rom", sizeof(_context->config.next_boot_rom_path));
    _ports->UpdateModelFeatureCache();
    EXPECT_FALSE(_ports->EsxDosServing()) << "the firmware answers RST $08 through the DivMMC";
    _context->config.next_boot_rom_path[0] = '\0';
}
