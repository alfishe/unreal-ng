// VDAC4 Quake II support (docs/inprogress/2026-11-15-tsconf-vdac4/quake2.md): the firmware's FPGA core and data
// file queries, core_load through the Tang LINK, the bomzh-3D core's LINK registers and drawing, the ESP heap's
// realloc in place. Synthetic fixtures only (a fake bitstream with the marks the firmware checks).

#include "vdac4espfixture.h"

#include <string>

#include "emulator/platforms/tsconf/vdac4/esp/espmemory.h"
#include "emulator/platforms/tsconf/vdac4/vdac4bomzh.h"

using namespace Vdac4EspTest;

namespace
{
/// The marks core_cache::checkImage looks for: sync + IDCODE in the head, a multiboot jump to 0, the VDAC4
/// UserCode in the tail
std::vector<uint8_t> FakeBitstream(size_t size, uint8_t personality, uint32_t jumpAddress = 0)
{
    std::vector<uint8_t> image(size, 0xFF);
    const uint8_t head[] = {0xA5, 0xC3, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x1B};
    std::memcpy(image.data() + 8, head, sizeof(head));
    // i + 10 = 18, steps of 4: the jump at 22
    image[18] = 0x10;
    const uint8_t jump[] = {0xD2, 0x00, 0xFF, 0xFF, static_cast<uint8_t>(jumpAddress >> 24),
                            static_cast<uint8_t>(jumpAddress >> 16), static_cast<uint8_t>(jumpAddress >> 8),
                            static_cast<uint8_t>(jumpAddress)};
    std::memcpy(image.data() + 22, jump, sizeof(jump));
    const uint8_t mark[] = {0x0A, 0x00, 0x00, 0x00, 0x56, 0x44, 0x34, personality};
    std::memcpy(image.data() + size - 40, mark, sizeof(mark));
    return image;
}

class CoreTangLink : public FakeTangLink
{
public:
    int LoadCore(uint32_t usercode, uint32_t& version) override
    {
        loaded = usercode;
        if (usercode != Vdac4BomzhCore::kUserCode)
            return -1;
        version = Vdac4BomzhCore::kVersion;
        return kZmOk;
    }
    bool CoreBack() override
    {
        ++backs;
        return true;
    }
    uint32_t loaded = 0;
    int backs = 0;
};
}  // namespace

class Vdac4Quake2Firmware_Test : public Vdac4EspFixture
{
protected:
    void SetUp() override
    {
        Vdac4EspFixture::SetUp();
        _esp->SetTang(&_coreTang);
        _esp->Firmware().SetLog([this](const std::string& line) { _log += line + "\n"; });
    }

    /// MOD_QUERY with flag 2 (a core) or 4 (a data file: the app's name, then the file's number when >= 0)
    std::vector<uint8_t> Query(uint8_t flags, const std::string& name, uint32_t size, uint32_t stamp,
                               const std::string& app = "", int index = -1)
    {
        std::vector<uint8_t> data = QueryPayload(name, size, stamp);
        data[0] = flags;
        if (!app.empty())
        {
            data.insert(data.end(), app.begin(), app.end());
            data.push_back(0);
        }
        if (index >= 0)
            data.push_back(static_cast<uint8_t>(index));
        return data;
    }

    uint8_t DeliverCore(const std::string& name, const std::vector<uint8_t>& image, uint32_t stamp)
    {
        Frame frame = Exchange(0x69, Query(2, name, static_cast<uint32_t>(image.size()), stamp), 0xE9);
        if (frame.data.empty() || frame.data[0] != 1)
            return 0xFF;
        for (size_t at = 0; at < image.size(); at += 1000)
        {
            std::vector<uint8_t> put = Le32(static_cast<uint32_t>(at));
            const size_t piece = std::min<size_t>(1000, image.size() - at);
            put.insert(put.end(), image.begin() + static_cast<std::ptrdiff_t>(at),
                       image.begin() + static_cast<std::ptrdiff_t>(at + piece));
            if (Exchange(0x6A, put, 0xEA).data[0] != 1)
                return 0xFE;
        }
        frame = Exchange(0x6B, {}, 0xEB);
        return frame.data.empty() ? 0xFD : frame.data[0];
    }

    /// The firmware as an app sees it (zm_core_t): a fake app started for it
    IVdac4AppHost& Host()
    {
        if (_runtime.host == nullptr)
        {
            EXPECT_EQ(Deliver("APP.ELF", BuildElf(300), 1), 1);
            Exchange(0x6C, {'A', 'P', 'P', '.', 'E', 'L', 'F', 0}, 0xEC);
        }
        EXPECT_NE(_runtime.host, nullptr);
        return *_runtime.host;
    }

    CoreTangLink _coreTang;
    std::string _log;
};

/// A core from the Evo card: into the BL616 cache, the same file again is "already there", core_load runs it by
/// its UserCode mark
TEST_F(Vdac4Quake2Firmware_Test, CoreQueryCommitAndLoad)
{
    BootAndInit();
    const std::vector<uint8_t> image = FakeBitstream(4000, 0x51);
    ASSERT_EQ(DeliverCore("Q2.BIN", image, 7), 1);
    ASSERT_EQ(_esp->Firmware().HeldCores().size(), 1u);
    EXPECT_EQ(_esp->Firmware().HeldCores()[0].name, "Q2");
    EXPECT_EQ(_esp->Firmware().HeldCores()[0].usercode, Vdac4BomzhCore::kUserCode);
    EXPECT_NE(_log.find("core Q2: 4000 bytes put into the BL616 cache (UserCode 56443451)"), std::string::npos)
        << _log;
    EXPECT_EQ(Exchange(0x69, Query(2, "Q2.BIN", 4000, 7), 0xE9).data[0], 0) << "held: not sent again";
    EXPECT_EQ(Exchange(0x69, Query(2, "Q2.BIN", 4000, 8), 0xE9).data[0], 1) << "another stamp: sent again";

    uint32_t version = 0;
    EXPECT_EQ(Host().CoreLoad("q2", version), kZmOk);
    EXPECT_EQ(_coreTang.loaded, Vdac4BomzhCore::kUserCode);
    EXPECT_EQ(version, Vdac4BomzhCore::kVersion);
    EXPECT_EQ(Host().CoreLoad("NONE", version), kZmNotFound);
    EXPECT_EQ(Host().CoreBack(), kZmOk);
    EXPECT_EQ(_coreTang.backs, 1);
}

/// What checkImage refuses is not cached; a core query needs NAME.BIN up to 1 MB
TEST_F(Vdac4Quake2Firmware_Test, BadCoresAreRefused)
{
    BootAndInit();
    std::vector<uint8_t> image = FakeBitstream(2000, 0x51);
    image[17] = 0x1C;  // IDCODE of another chip
    EXPECT_EQ(DeliverCore("A.BIN", image, 1), 0);
    EXPECT_EQ(DeliverCore("B.BIN", FakeBitstream(2000, 0x51, 0x100000), 1), 0) << "jump not to slot 0";
    image = FakeBitstream(2000, 0x51);
    image[image.size() - 34] = 0x33;  // the mark's '4'
    EXPECT_EQ(DeliverCore("C.BIN", image, 1), 0);
    EXPECT_TRUE(_esp->Firmware().HeldCores().empty());
    EXPECT_NE(_log.find("no VDAC4 mark"), std::string::npos) << _log;
    EXPECT_EQ(Exchange(0x69, Query(2, "Q2.ELF", 2000, 1), 0xE9).data[0], 3);
    EXPECT_EQ(Exchange(0x69, Query(2, "Q2.BIN", Vdac4EspFirmware::kMaxCoreBytes + 1, 1), 0xE9).data[0], 3);
}

/// Data files (flag 4): with the Z80 program's file number they are read on request (5); without one the board
/// would need a USB drive (4); a bad app name (3). The app finds them by its name
TEST_F(Vdac4Quake2Firmware_Test, DataQueryReplies)
{
    BootAndInit();
    EXPECT_EQ(Exchange(0x69, Query(4, "PAK0.PAK", 123456, 3, "q2", 0), 0xE9).data[0], 5);
    EXPECT_EQ(_runtime.dataChanges, 1);
    EXPECT_EQ(Exchange(0x69, Query(4, "PAK1.PAK", 99, 3, "q2"), 0xE9).data[0], 4);
    EXPECT_EQ(Exchange(0x69, Query(4, "PAK1.PAK", 99, 3, "TOOLONGAPP", 1), 0xE9).data[0], 3);
    EXPECT_EQ(Exchange(0x69, Query(4, "PAK1.PAK", 99, 3), 0xE9).data[0], 3) << "no app name";
    ASSERT_EQ(_esp->Firmware().RemoteFiles().size(), 1u);
    uint8_t index = 0xFF;
    uint32_t size = 0;
    EXPECT_EQ(Host().DataFile("Q2", "PAK0.PAK", index, size), kZmOk);
    EXPECT_EQ(index, 0);
    EXPECT_EQ(size, 123456u);
    EXPECT_EQ(Host().DataFile("DOOM", "PAK0.PAK", index, size), kZmNotFound);
    // The same file again takes its own place
    EXPECT_EQ(Exchange(0x69, Query(4, "PAK0.PAK", 5000, 4, "Q2", 2), 0xE9).data[0], 5);
    ASSERT_EQ(_esp->Firmware().RemoteFiles().size(), 1u);
    EXPECT_EQ(_esp->Firmware().RemoteFiles()[0].index, 2);
}

/// A data piece (0x6F) reaches the app runtime with its prefix
TEST_F(Vdac4Quake2Firmware_Test, DataPieceFrameIsParsed)
{
    BootAndInit();
    std::vector<uint8_t> piece{9};
    for (uint8_t b : Le32(512))
        piece.push_back(b);
    piece.insert(piece.end(), 300, 0x5C);
    ASSERT_TRUE(_z80->SendFrame(0x6F, piece));
    for (int ms = 0; ms < 20 && _runtime.pieces.empty(); ++ms)
        AdvanceMs(1);
    ASSERT_EQ(_runtime.pieces.size(), 1u);
    EXPECT_EQ(_runtime.pieces[0], piece);
    EXPECT_EQ(_esp->Transport().Parser().BadChecksums(), 0u);
}

// --- the bomzh-3D core on the LINK ----------------------------------------------------------------------------

namespace
{
constexpr uint32_t kQueue = 0x40000;

void PushWords(Vdac4BomzhCore& core, const std::vector<uint32_t>& words)
{
    for (uint32_t word : words)
        core.LinkWrite(kQueue, word);
}

/// SETUP of a 16 x 8 picture: frames at 0x10000 / 0x20000, z at 0x30000, colormap 0x40000, palette 0x50000 with
/// color 1 = (0x0A, 0x14, 0x1E), turb 0x60000
void SetupPicture(Vdac4BomzhCore& core)
{
    PushWords(core, {0x0900'0002u, 0x50000, 0x0A00'0000u, 0x0000'1E14u});  // palette bytes 00 00 00 0A 14 1E
    PushWords(core, {0x0A00'0000u, 0x10000, 0x20000, 0x30000, (8u << 16) | 16u, 0x40000, 0, 0x50000, 0x60000});
}
}  // namespace

TEST(Vdac4BomzhCore_Test, RegistersAtReset)
{
    Vdac4BomzhCore core;
    ASSERT_TRUE(core.IsReady());
    EXPECT_EQ(core.LinkRead(0x00), 0x56444334u);
    EXPECT_EQ(core.LinkRead(0x04), 0x51010003u);
    EXPECT_EQ(core.LinkRead(0x08), 1u << 2) << "mem_ready only";
    EXPECT_EQ(core.LinkRead(0x0C), 2048u);
    EXPECT_EQ(core.LinkRead(0x1C), 0x4700'0C00u) << "tag + gray(2048)";
    EXPECT_EQ(core.LinkRead(0x14), 1u);
    EXPECT_EQ(core.LinkRead(0x84), 0x535A'0000u);
    core.LinkWrite(0x84, 0x535A'0002u);
    EXPECT_EQ(core.LinkRead(0x84), 0x535A'0002u);
    core.LinkWrite(0x84, 0x535A'0003u);  // size 3 is not taken
    core.LinkWrite(0x84, 0x1234'0001u);  // nor without the key
    EXPECT_EQ(core.LinkRead(0x84), 0x535A'0002u);
    core.LinkWrite(0x3C, 0xCAFEF00Du);
    EXPECT_EQ(core.LinkRead(0x3C), 0xCAFEF00Du);
    EXPECT_EQ(core.LinkRead(0xF0), 0xB000'0000u);
    core.LinkWrite(0xF0, 0x424F4F54u);
    EXPECT_EQ(core.LinkRead(0xF0), 0xB001'0000u);
    EXPECT_TRUE(core.BootRequested(0));
}

/// FILL, FRAME: the shown frame is the one drawn before the FRAME, through the palette; MARK reads back
TEST(Vdac4BomzhCore_Test, FillFrameAndMark)
{
    Vdac4BomzhCore core;
    SetupPicture(core);
    PushWords(core, {0x0600'0000u, 0, (8u << 16) | 16u, 1});  // FILL the frame with color 1
    PushWords(core, {0x0700'0000u, 0});                       // FRAME, no blend
    PushWords(core, {0x0F00'0000u, 0x1234});                  // MARK
    EXPECT_EQ(core.Frames(), 1u);
    EXPECT_EQ(core.LinkRead(0x10), 1u);
    EXPECT_EQ(core.LinkRead(0x90), 0x1234u);
    EXPECT_EQ(core.LinkRead(0x08) & 2u, 0u) << "no bad command";
    std::vector<uint8_t> rgba;
    uint16_t width = 0, height = 0;
    ASSERT_TRUE(core.ShownPicture(rgba, width, height));
    EXPECT_EQ(width, 16);
    EXPECT_EQ(height, 8);
    ASSERT_EQ(rgba.size(), 16u * 8u * 4u);
    EXPECT_EQ(rgba[0], 0x0A);
    EXPECT_EQ(rgba[1], 0x14);
    EXPECT_EQ(rgba[2], 0x1E);
    EXPECT_EQ(rgba[3], 0xFF);
    EXPECT_EQ(rgba[rgba.size() - 4], 0x0A);
}

/// WRITE data streams into SDRAM (longer than one piece), RD_ADDR / RD_LEN copy it into the window
TEST(Vdac4BomzhCore_Test, WriteThenReadWindow)
{
    Vdac4BomzhCore core;
    const uint32_t count = 1500;
    std::vector<uint32_t> words{0x0900'0000u | count, 0x100000};
    for (uint32_t i = 0; i < count; ++i)
        words.push_back(0xA0000000u + i);
    PushWords(core, words);
    core.LinkWrite(0x88, 0x100000 + 4 * 1400);  // a multiple of 32
    core.LinkWrite(0x8C, 2);                    // 64 bytes
    for (uint32_t i = 0; i < 16; ++i)
        EXPECT_EQ(core.LinkRead(0x1000 + 4 * i), 0xA0000000u + 1400 + i) << i;
    // The next command after the data is a command again
    PushWords(core, {0x0F00'0000u, 77});
    EXPECT_EQ(core.LinkRead(0x90), 77u);
    EXPECT_EQ(core.GetStats().badWords, 0u);
}

/// A word that starts no command sets bad_cmd (sticky) and is dropped; the next command still runs
TEST(Vdac4BomzhCore_Test, BadWordIsDropped)
{
    Vdac4BomzhCore core;
    PushWords(core, {0xEE00'0000u, 0x0300'0002u /* POLY of 2 points */, 0x0F00'0000u, 5});
    EXPECT_EQ(core.LinkRead(0x08) & 2u, 2u);
    EXPECT_EQ(core.GetStats().badWords, 2u);
    EXPECT_EQ(core.LinkRead(0x90), 5u);
}

/// Back to slot 0: the Evo's RST_PULSE (68 00 00), or M_SEL 1 then 0 and 0.5 s of quiet; other traffic does not
TEST(Vdac4BomzhCore_Test, BootRules)
{
    Vdac4BomzhCore pulse;
    for (uint8_t b : {0x68, 0x00, 0x01})
        pulse.EvoByte(b, 10);
    pulse.EvoDeselect(10);
    EXPECT_FALSE(pulse.BootRequested(10)) << "68 00 01 is not RST_PULSE";
    for (uint8_t b : {0x68, 0x00, 0x00})
        pulse.EvoByte(b, 11);
    pulse.EvoDeselect(11);
    EXPECT_TRUE(pulse.BootRequested(11));

    Vdac4BomzhCore idle;
    EXPECT_FALSE(idle.BootRequested(5000)) << "never armed";
    idle.SetMsel(true, 100);
    EXPECT_FALSE(idle.BootRequested(2000)) << "M_SEL holds it";
    idle.SetMsel(false, 2000);
    EXPECT_FALSE(idle.BootRequested(2400));
    idle.EvoByte(0x00, 2400);
    EXPECT_FALSE(idle.BootRequested(2850)) << "Evo traffic restarts the wait";
    EXPECT_TRUE(idle.BootRequested(2901));
}

/// heap_caps_realloc in place: shrink always, grow into free space after the block, not over the next block
TEST(Vdac4EspHeap_Test, ResizeInPlace)
{
    vdac4::EspHeap heap(0x1000, 0x1000);
    const uint32_t a = heap.Alloc(0x100);
    const uint32_t b = heap.Alloc(0x100);
    ASSERT_NE(a, 0u);
    ASSERT_NE(b, 0u);
    EXPECT_TRUE(heap.Resize(a, 0x40));
    EXPECT_EQ(heap.BlockSize(a), 0x40u);
    EXPECT_TRUE(heap.Resize(a, 0x100)) << "back into its own old space";
    EXPECT_FALSE(heap.Resize(a, 0x110)) << "the next block is in the way";
    EXPECT_TRUE(heap.Resize(b, 0x800)) << "free up to the heap end";
    EXPECT_FALSE(heap.Resize(b, 0x2000));
    EXPECT_FALSE(heap.Resize(a + 16, 0x10)) << "not a block start";
}
