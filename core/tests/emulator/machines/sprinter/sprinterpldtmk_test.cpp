#include "sprinterfixture.h"

#include <cstring>
#include <string>
#include <vector>

#include "debugger/ttd/sprinter/ttdsprinter.h"
#include "emulator/memory/sprinter/sprinteraccelerator.h"
#include "emulator/ports/models/sprinter/sprinterpldconfig.h"
#include "emulator/ports/models/sprinter/sprinterpldstandard.h"
#include "emulator/ports/models/sprinter/sprinterpldtmk.h"
#include "emulator/state/devicestate.h"
#include "emulator/video/sprinter/sprintervideoram.h"

namespace
{
/// The inverse of the FNV-1a prime #01000193 modulo 2^32: h = (h' x inverse) xor byte undoes one step
constexpr uint32_t kFnvPrimeInverse = 0x359C449Bu;

const StateNode& Member(const StateNode& node, const std::string& key)
{
    static const StateNode kNone;
    const StateNode* member = node.find(key);
    return member ? *member : kNone;
}
}  // namespace

/// The TMK PLD configuration (tmk-accelerator.md): the standard accelerator with a 1 024-byte buffer, ACC_CTRL
/// (code #80: clip, rectangle, length x 4, address down) and ACC_RECT_W (code #81). The ports are mapped as
/// TmKonf.exe maps them (#7785 -> #80, #7786 -> #81). Code runs from window 2 (#8000), the data lives in window 1
class SprinterPldTmk_Test : public SprinterFixture
{
protected:
    void SetUp() override
    {
        SprinterFixture::SetUp();
        OpenDcp();
        Pld().allMode = 0x05;  // accelerator on, no original waits
        _decoder->ApplyOrigWaits();
        Pld().configModule = static_cast<uint8_t>(SprinterPldConfigurationRegistry::kTmkIndex);
        _decoder->OnTtdStateLoaded();  // the active module's accelerator as the bus agent
        ASSERT_NE(Acc(), nullptr);
        ASSERT_NE(Acc()->Tmk(), nullptr);
        SetCode(0x7785, false, SprinterPldTmk::kCodeCtrl);
        SetCode(0x7786, false, SprinterPldTmk::kCodeRectWidth);
        SetCode(0x7786, true, SprinterPldTmk::kCodeRectWidth);
        ASSERT_TRUE(IsRam(0x4000));
        ASSERT_TRUE(IsRam(0x8000));
    }

    SprinterAccelerator* Acc() { return _decoder->GetAccelerator(); }
    SprinterPldTmk& Tmk() { return dynamic_cast<SprinterPldTmk&>(_decoder->ActiveModule()); }

    void Ctrl(uint8_t value) { Out(0x7785, value); }
    void RectWidth(uint8_t value) { Out(0x7786, value); }

    uint8_t Page1() { return Pld().Cell(0xE9); }
    uint8_t& Data(uint16_t addr) { return Ram(Page1(), static_cast<uint16_t>(addr - 0x4000)); }
    void Graphics1()
    {
        Pld().Cell(0xE9) = 0x50;
        _decoder->UpdateBanks();
    }

    /// LD HL,addr : LD A,value : LD D,D : LD E,length : <mode> : <body> : LD B,B
    static std::vector<uint8_t> Program(uint16_t hl, uint8_t a, uint8_t length, uint8_t mode, std::vector<uint8_t> body)
    {
        std::vector<uint8_t> code = {0x21, static_cast<uint8_t>(hl), static_cast<uint8_t>(hl >> 8), 0x3E, a,
                                     0x52, 0x1E, length, mode};
        code.insert(code.end(), body.begin(), body.end());
        code.push_back(0x40);
        return code;
    }
    /// The buffer from `from` (copy mode, LD A,(HL)), then out to `to` (LD (HL),A)
    std::vector<uint8_t> LoadBuffer(uint16_t from, uint8_t length) { return Program(from, 0, length, 0x6D, {0x7E}); }
    std::vector<uint8_t> StoreBuffer(uint16_t to) { return {0x21, static_cast<uint8_t>(to), static_cast<uint8_t>(to >> 8), 0x6D, 0x77, 0x40}; }

    /// A load whose hashes come out `full` / `head`: the sink one write before the end, then the last write
    void LoadAs(uint32_t full, uint32_t head)
    {
        _decoder->BeginLoading();
        SprinterPldState& pld = Pld();
        constexpr uint8_t kLast = 0xFF;
        pld.bitstreamCount = SprinterPldConfig::kPldConfigurationWrites - 1;
        pld.bitstreamHashHead = head;
        pld.bitstreamHashFull = (full * kFnvPrimeInverse) ^ kLast;
        _decoder->OnConfigurationWrite(kLast);
        _decoder->OnMachineStep(0);  // the PLD's reset at the instruction boundary
    }
};

TEST_F(SprinterPldTmk_Test, Registry_KnowsTmkByItsHashesAndName)
{
    SprinterPldConfigurationRegistry registry;
    EXPECT_EQ(registry.FindByName("TMK"), static_cast<int>(SprinterPldConfigurationRegistry::kTmkIndex));
    EXPECT_EQ(registry.Find(0x84BA5A9A, 0), static_cast<int>(SprinterPldConfigurationRegistry::kTmkIndex))
        << "SP2_ACEX.ttf of the author's K30 build: the full hash";
    EXPECT_EQ(registry.Find(0x12345678, 0xE1BA76FB), static_cast<int>(SprinterPldConfigurationRegistry::kTmkIndex));
}

// The TMK bitstream selects the module, which attaches its part to the standard accelerator; ACC_CTRL starts at 0.
// Back to the standard bitstream: the plain accelerator again
TEST_F(SprinterPldTmk_Test, TmkBitstream_AttachesTheAdditions_StandardDetaches)
{
    Tmk().State().ctrl = 0x7F;
    LoadAs(SprinterPldTmk::kFullHash, SprinterPldTmk::kHeadHash);
    ASSERT_EQ(Pld().configState, SprinterConfigState::Configured);
    EXPECT_EQ(_decoder->ActiveModule().Descriptor().name, "TMK");
    EXPECT_EQ(Acc(), &_decoder->StandardAccelerator());
    EXPECT_EQ(Acc()->Tmk(), &Tmk().State());
    EXPECT_EQ(Tmk().State().ctrl, 0) << "/RESET clears ACC_CTRL";

    LoadAs(SprinterPldStandard::kFullHash304, SprinterPldStandard::kHeadHash304);
    EXPECT_EQ(_decoder->ActiveModule().Descriptor().name, "Standard");
    EXPECT_EQ(Acc()->Tmk(), nullptr);
}

// Codes #80 / #81: ACC_CTRL and ACC_RECT_W; an access to #81 (a read too) reloads the row counter
TEST_F(SprinterPldTmk_Test, Ports_CtrlAndRectWidth)
{
    Ctrl(0x35);
    RectWidth(31);
    EXPECT_EQ(Tmk().State().ctrl, 0x35);
    EXPECT_EQ(Tmk().State().rectWidth, 31);
    EXPECT_EQ(Tmk().State().rectCounter, 31);
    Tmk().State().rectCounter = 4;
    In(0x7786);
    EXPECT_EQ(Tmk().State().rectCounter, 31) << "ACC_RECT_W_ENA is not gated by the direction";
}

// ACC_CTRL = 0: the standard rules (length 0 = 256, address up)
TEST_F(SprinterPldTmk_Test, CtrlZero_FillIsStandard)
{
    for (uint32_t a = 0x4100; a < 0x4202; a++)
        Data(static_cast<uint16_t>(a)) = 0x11;
    RunCode(Program(0x4100, 0x5C, 0, 0x49, {0x77}));
    for (uint32_t a = 0x4100; a < 0x4200; a++)
        ASSERT_EQ(Data(static_cast<uint16_t>(a)), 0x5C) << std::hex << a;
    EXPECT_EQ(Data(0x4200), 0x11);
}

// Bit 5: length x 4 - RGACC 80 fills a 320-byte line (TmKonf.exe), RGACC 0 fills 1 024 bytes
TEST_F(SprinterPldTmk_Test, Length4_RgaccTimesFour_ZeroIs1024)
{
    Ctrl(SprinterAccelerator::kCtrlLength4);
    for (uint32_t a = 0x4000; a < 0x4402; a++)
        Data(static_cast<uint16_t>(a)) = 0x11;
    RunCode(Program(0x4000, 0x22, 80, 0x49, {0x77}));
    EXPECT_EQ(Data(0x413F), 0x22);
    EXPECT_EQ(Data(0x4140), 0x11) << "320 bytes";
    EXPECT_EQ(Acc()->OperationAccesses(), 320u);

    RunCode(Program(0x4000, 0x33, 0, 0x49, {0x77}));
    for (uint32_t a = 0x4000; a < 0x4400; a++)
        ASSERT_EQ(Data(static_cast<uint16_t>(a)), 0x33) << std::hex << a;
    EXPECT_EQ(Data(0x4400), 0x11);
    EXPECT_EQ(Acc()->State().lastExtraClocks, (1023u * 6u + 11u) / 12u) << "3.5 MHz: 1 023 extra accesses";
}

// The 1 024-byte buffer: a copy of 1 024 distinct bytes comes out whole (a 256-byte buffer would repeat)
TEST_F(SprinterPldTmk_Test, Buffer1024_CopiesAKilobyte)
{
    Ctrl(SprinterAccelerator::kCtrlLength4);
    for (uint32_t i = 0; i < 1024; i++)
    {
        Data(static_cast<uint16_t>(0x4000 + i)) = static_cast<uint8_t>(i ^ (i >> 8) * 0x35);
        Data(static_cast<uint16_t>(0x5000 + i)) = 0;
    }
    RunCode(LoadBuffer(0x4000, 0));
    RunCode(StoreBuffer(0x5000));
    for (uint32_t i = 0; i < 1024; i++)
        ASSERT_EQ(Data(static_cast<uint16_t>(0x5000 + i)), static_cast<uint8_t>(i ^ (i >> 8) * 0x35)) << i;
    EXPECT_NE(std::memcmp(Tmk().State().bufferHigh, Acc()->State().buffer, 256), 0) << "cells #100-#3FF in use";
}

// Bit 6: the address runs down - a fill ends below the start, a copy stored downwards mirrors the line
TEST_F(SprinterPldTmk_Test, AddressDown_FillAndMirror)
{
    for (uint16_t a = 0x40FF; a < 0x4108; a++)
        Data(a) = 0x11;
    Ctrl(SprinterAccelerator::kCtrlAddressDown);
    RunCode(Program(0x4105, 0x44, 5, 0x49, {0x77}));
    for (uint16_t a = 0x4101; a <= 0x4105; a++)
        EXPECT_EQ(Data(a), 0x44) << std::hex << a;
    EXPECT_EQ(Data(0x4100), 0x11);
    EXPECT_EQ(Data(0x4106), 0x11);

    for (uint16_t i = 0; i < 4; i++)
        Data(static_cast<uint16_t>(0x4200 + i)) = static_cast<uint8_t>(0xA0 + i);
    Ctrl(0);
    RunCode(LoadBuffer(0x4200, 4));
    Ctrl(SprinterAccelerator::kCtrlAddressDown);
    RunCode(StoreBuffer(0x4303));
    for (uint16_t i = 0; i < 4; i++)
        EXPECT_EQ(Data(static_cast<uint16_t>(0x4303 - i)), 0xA0 + i) << i;
}

// Bit 4: a copy stored as a rectangle - rows of ACC_RECT_W + 1 bytes, PORT_Y + 1 per row, the row counter carries
TEST_F(SprinterPldTmk_Test, Rectangle_CopyStoresRowsDownPortY)
{
    for (uint16_t i = 0; i < 12; i++)
        Data(static_cast<uint16_t>(0x4000 + i)) = static_cast<uint8_t>(0x10 + i);
    RunCode(LoadBuffer(0x4000, 12));

    Graphics1();
    SprinterVideoRam& vram = _decoder->GetVideoRam();
    RectWidth(3);
    Ctrl(SprinterAccelerator::kCtrlRect);
    Pld().portY = 20;
    RunCode(StoreBuffer(0x4010));
    for (uint32_t row = 0; row < 3; row++)
    {
        for (uint32_t col = 0; col < 4; col++)
            EXPECT_EQ(vram.Read((20 + row) * 1024 + 0x10 + col), 0x10 + row * 4 + col) << row << "," << col;
        EXPECT_EQ(vram.Read((20 + row) * 1024 + 0x14), 0x00) << "row " << row << " is 4 bytes";
    }
    EXPECT_EQ(vram.Read(23 * 1024 + 0x10), 0x00);
    EXPECT_EQ(Pld().portY, 23) << "AGR + 1 at every row end, the last one included";
    EXPECT_EQ(Tmk().State().rectCounter, 3) << "reloaded at the last row end";
}

// The rectangle needs the buffer (RECT_ENA = CTRL4 & ACC_DIR1): a fill stays one line, and a load (a read) too
TEST_F(SprinterPldTmk_Test, Rectangle_NotForFillsOrLoads)
{
    Graphics1();
    SprinterVideoRam& vram = _decoder->GetVideoRam();
    RectWidth(3);
    Ctrl(SprinterAccelerator::kCtrlRect);
    Pld().portY = 30;
    RunCode(Program(0x4000, 0x66, 8, 0x49, {0x77}));
    for (uint32_t x = 0; x < 8; x++)
        EXPECT_EQ(vram.Read(30 * 1024 + x), 0x66) << x;
    EXPECT_EQ(vram.Read(31 * 1024), 0x00);
    EXPECT_EQ(Pld().portY, 30);
    EXPECT_EQ(Tmk().State().rectCounter, 3) << "untouched";

    RunCode(LoadBuffer(0x4000, 8));
    EXPECT_EQ(Pld().portY, 30);
    EXPECT_EQ(Acc()->State().buffer[1], 0x66) << "the line, not a rectangle";
}

// Bits 0-1: clip the graphics stores by PORT_Y bit 7 - the CPU's own store and the accelerator's
TEST_F(SprinterPldTmk_Test, ClipY_BlocksStoresOnTheClippedHalf)
{
    Graphics1();
    SprinterVideoRam& vram = _decoder->GetVideoRam();
    Ctrl(SprinterAccelerator::kCtrlClipY | SprinterAccelerator::kCtrlClipYValue);  // PORT_Y bit 7 = 1 clipped
    Pld().portY = 5;
    RunCode({0x21, 0x05, 0x40, 0x3E, 0x77, 0x77});  // LD HL,#4005 : LD A,#77 : LD (HL),A
    EXPECT_EQ(vram.Read(5 * 1024 + 5), 0x77);
    Pld().portY = 130;
    const uint8_t ramBefore = _memory->RAMBase()[0x50 * PAGE_SIZE + 130 * 1024 + 5];
    RunCode({0x21, 0x05, 0x40, 0x3E, 0x77, 0x77});
    EXPECT_EQ(vram.Read(130 * 1024 + 5), 0x00) << "CPU store clipped";
    EXPECT_EQ(_memory->RAMBase()[0x50 * PAGE_SIZE + 130 * 1024 + 5], ramBefore) << "RAM store blocked too (BLK_MR)";

    Pld().portY = 125;
    RunCode(Program(0x4009, 0x5E, 6, 0x5B, {0x77}));  // vertical fill across line 128
    for (uint32_t y = 125; y < 128; y++)
        EXPECT_EQ(vram.Read(y * 1024 + 9), 0x5E) << y;
    for (uint32_t y = 128; y < 131; y++)
        EXPECT_EQ(vram.Read(y * 1024 + 9), 0x00) << y;
}

// Bits 2-3: clip by address bit 7; a RAM window is never clipped
TEST_F(SprinterPldTmk_Test, ClipX_BlocksByAddressBit7_GraphicsOnly)
{
    for (uint16_t a = 0x4000; a < 0x4100; a++)
        Data(a) = 0x11;
    Ctrl(SprinterAccelerator::kCtrlClipX);  // address bit 7 = 0 clipped
    RunCode(Program(0x4000, 0x22, 0, 0x49, {0x77}));
    EXPECT_EQ(Data(0x4000), 0x22) << "RAM: no clip";

    Graphics1();
    SprinterVideoRam& vram = _decoder->GetVideoRam();
    Pld().portY = 7;
    RunCode(Program(0x4040, 0x99, 128, 0x49, {0x77}));  // #4040-#40BF
    EXPECT_EQ(vram.Read(7 * 1024 + 0x7F), 0x00);
    EXPECT_EQ(vram.Read(7 * 1024 + 0x80), 0x99);
    EXPECT_EQ(vram.Read(7 * 1024 + 0xBF), 0x99);
}

// The TMK state travels in the PLD blob as the module's state (the room is the largest module's)
TEST_F(SprinterPldTmk_Test, Ttd_TheTmkStateTravelsInThePldBlob)
{
    Ctrl(0x31);
    RectWidth(9);
    Tmk().State().rectCounter = 4;
    Tmk().State().bufferHigh[700] = 0xC3;
    ttd::TTDSprinterPld serializer(*_decoder);
    std::vector<uint8_t> saved(serializer.TTDStateSize());
    serializer.TTDSaveState(saved.data());
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(saved.data()) + 139), "TMK");

    Tmk().State() = SprinterAccelTmkState{};
    serializer.TTDLoadState(saved.data());
    EXPECT_EQ(Tmk().State().ctrl, 0x31);
    EXPECT_EQ(Tmk().State().rectWidth, 9);
    EXPECT_EQ(Tmk().State().rectCounter, 4);
    EXPECT_EQ(Tmk().State().bufferHigh[700], 0xC3);
    EXPECT_EQ(Acc()->Tmk(), &Tmk().State());
}

// The automation view names the additions
TEST_F(SprinterPldTmk_Test, DeviceState_ReportsTheTmkBlock)
{
    Ctrl(SprinterAccelerator::kCtrlRect | SprinterAccelerator::kCtrlLength4 | SprinterAccelerator::kCtrlClipY);
    RectWidth(15);
    Acc()->State().length = 8;
    const StateNode report = DeviceState::Sprinter(_context);
    const StateNode& accel = Member(report, "accelerator");
    EXPECT_EQ(Member(accel, "length").i, 32);
    const StateNode& tmk = Member(accel, "tmk");
    EXPECT_EQ(Member(tmk, "buffer_size").i, 1024);
    EXPECT_TRUE(Member(tmk, "rectangle").b);
    EXPECT_TRUE(Member(tmk, "length_x4").b);
    EXPECT_FALSE(Member(tmk, "address_down").b);
    EXPECT_EQ(Member(tmk, "rect_width").i, 16);
    EXPECT_EQ(Member(tmk, "clip_y").s, "PORT_Y bit 7 = 0");
    EXPECT_EQ(Member(tmk, "clip_x").s, "off");
}

// The author's stream (testdata pld/tmk/TMK-K30.ACX, his SP2_ACEX.ttf) through the sink as the ROM loader writes it:
// each byte 8 times, rotated right between the writes. It ends at the count and selects TMK by the full hash
TEST_F(SprinterPldTmk_Test, AuthorsStream_SelectsTmkByTheFullHash)
{
    const std::string path =
        (TestPathHelper::FindProjectRoot() / "testdata" / "machines" / "sprinter" / "pld" / "tmk" / "TMK-K30.ACX").string();
    std::vector<uint8_t> stream;
    ASSERT_TRUE(FileHelper::FileExists(path)) << path;
    {
        FILE* file = fopen(path.c_str(), "rb");
        ASSERT_NE(file, nullptr);
        stream.resize(59215);
        ASSERT_EQ(fread(stream.data(), 1, stream.size(), file), stream.size());
        fclose(file);
    }

    _decoder->BeginLoading();
    for (uint8_t byte : stream)
    {
        uint8_t value = byte;
        for (int bit = 0; bit < 8; bit++)
        {
            _decoder->OnConfigurationWrite(value);
            value = static_cast<uint8_t>((value >> 1) | (value << 7));
        }
    }
    EXPECT_EQ(Pld().bitstreamCount, SprinterPldConfig::kPldConfigurationWrites);
    EXPECT_EQ(Pld().bitstreamHashFull, SprinterPldTmk::kFullHash);
    EXPECT_EQ(Pld().bitstreamHashHead, SprinterPldTmk::kHeadHash);
    _decoder->OnMachineStep(0);
    EXPECT_EQ(_decoder->ActiveModule().Descriptor().name, "TMK");
    EXPECT_EQ(Acc()->Tmk(), &Tmk().State());
}
