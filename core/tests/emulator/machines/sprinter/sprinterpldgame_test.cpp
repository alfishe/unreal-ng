// The Game PLD configuration module (sprinterpldgame.h; Sprinter mame-gap-analysis V10, game-configuration.md):
// chosen by the loaded bitstream's identity, what it changes against Standard (cell #EE, the picture), the way back
// to Standard (a reload, the RESET button), why the reports say it runs, and its state in the TTD blob.
//
// The loads are synthetic: the test puts the sink one write before the end with the hashes of the stream it wants
// (FNV-1a runs backwards with the inverse of its prime), so no bitstream file is needed - the real GAME_00.ACX load
// runs in the hard-disk test (sprinter_boot_test.cpp, RealHdd_Game00ReloadsThePld).

#include "sprinterfixture.h"
#include "sprintermodetable.h"

#include <string>
#include <vector>

#include "debugger/ttd/sprinter/ttdsprinter.h"
#include "emulator/ports/models/sprinter/sprinterpldconfig.h"
#include "emulator/ports/models/sprinter/sprinterpldgame.h"
#include "emulator/ports/models/sprinter/sprinterpldstandard.h"
#include "emulator/state/devicestate.h"
#include "emulator/video/sprinter/screensprinter.h"
#include "emulator/video/sprinter/sprintergamevideo.h"
#include "emulator/video/sprinter/sprintervideoram.h"

namespace
{
const StateNode& Member(const StateNode& node, const std::string& key)
{
    static const StateNode kNone;
    const StateNode* member = node.find(key);
    return member ? *member : kNone;
}
std::string Str(const StateNode& node, const std::string& key) { return Member(node, key).s; }

/// The inverse of the FNV-1a prime #01000193 modulo 2^32: h = (h' x inverse) xor byte undoes one step
constexpr uint32_t kFnvPrimeInverse = 0x359C449Bu;
static_assert(static_cast<uint32_t>(SprinterPldConfig::kFnvPrime * kFnvPrimeInverse) == 1u, "FNV prime inverse");
}  // namespace

class SprinterPldGame_Test : public SprinterFixture
{
protected:
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
    void LoadGame() { LoadAs(SprinterPldGame::kFullHash, SprinterPldGame::kHeadHash); }
    void LoadStandard() { LoadAs(SprinterPldStandard::kFullHash304, SprinterPldStandard::kHeadHash304); }

    std::string Selection() const
    {
        std::string key, why;
        _decoder->ModuleSelection(key, why);
        return key;
    }
};

TEST_F(SprinterPldGame_Test, Registry_KnowsGameByBothHashesAndName)
{
    SprinterPldConfigurationRegistry registry;
    ASSERT_EQ(registry.Count(), 2u);
    EXPECT_EQ(registry.At(SprinterPldConfigurationRegistry::kGameIndex).Descriptor().name, "Game");
    EXPECT_EQ(registry.Find(0xC0FA3055, 0), 1) << "GAME_00.ACX = GC.BIN: the full hash";
    EXPECT_EQ(registry.Find(0x12345678, 0x3861CFA4), 1) << "MAME's constant: the head hash";
    EXPECT_EQ(registry.FindByName("Game"), 1);
}

// The Game bitstream: the Game module, its cells (RET_PORT #EE = #41: the BIOS returns to the program), its picture
TEST_F(SprinterPldGame_Test, GameBitstream_SelectsGameWithItsCells)
{
    Pld().Cell(0xE9) = 0x77;  // a program's page: the load sets the embedded RAM again
    LoadGame();
    ASSERT_EQ(Pld().configState, SprinterConfigState::Configured);
    EXPECT_EQ(_decoder->ActiveModule().Descriptor().name, "Game");
    EXPECT_EQ(Pld().Cell(0xEE), 0x41) << "RET_PORT: the BIOS maps page #41 and jumps to its #FFF4";
    EXPECT_EQ(Pld().Cell(0xE9), 0x05) << "the rest is the shipped bitstream's DCP.MIF";
    EXPECT_EQ(Pld().Cell(0xF3), 0x03);
    EXPECT_NE(_decoder->BeamVideo(), nullptr);
    EXPECT_EQ(&_decoder->VideoRenderer(), &dynamic_cast<SprinterPldGame&>(_decoder->ActiveModule()).Video());
    EXPECT_EQ(Selection(), "full_hash");
    EXPECT_EQ(_decoder->GetAccelerator(), &_decoder->StandardAccelerator()) << "the accelerator is Standard's";
}

TEST_F(SprinterPldGame_Test, HeadHashOnly_SelectsGameAndSaysSo)
{
    LoadAs(0x0BADF00D, SprinterPldGame::kHeadHash);
    EXPECT_EQ(_decoder->ActiveModule().Descriptor().name, "Game");
    EXPECT_EQ(Selection(), "head_hash");
}

TEST_F(SprinterPldGame_Test, UnknownBitstream_RunsStandardAndSaysSo)
{
    LoadAs(0xA65B49FC, 0xD0953276);  // LDConf's STREAM.300
    EXPECT_EQ(_decoder->ActiveModule().Descriptor().name, "Standard");
    EXPECT_EQ(Selection(), "unknown_bitstream");
    EXPECT_EQ(Pld().Cell(0xEE), 0x00);
    EXPECT_EQ(_decoder->BeamVideo(), nullptr);
}

// Back to Standard: a reload with the Standard stream (LDConf's way back, START.BAT) and the RESET button
TEST_F(SprinterPldGame_Test, BackToStandard_ByReloadAndByReset)
{
    LoadGame();
    ASSERT_EQ(_decoder->ActiveModule().Descriptor().name, "Game");
    LoadStandard();
    EXPECT_EQ(_decoder->ActiveModule().Descriptor().name, "Standard");
    EXPECT_EQ(Selection(), "full_hash");
    EXPECT_EQ(Pld().Cell(0xEE), 0x00) << "Standard's RET_PORT: the BIOS cold-boots";
    EXPECT_EQ(_decoder->BeamVideo(), nullptr);
    EXPECT_EQ(&_decoder->VideoRenderer(), &SprinterVideoRenderer::Standard());

    LoadGame();
    ASSERT_EQ(_decoder->ActiveModule().Descriptor().name, "Game");
    _core->Reset();  // the RESET button: the PLD loads the ROM's bitstream again (fast start here)
    EXPECT_EQ(_decoder->ActiveModule().Descriptor().name, "Standard");
    EXPECT_EQ(_decoder->BeamVideo(), nullptr);
    EXPECT_EQ(Pld().Cell(0xEE), 0x00);
}

// The cells come with a load that changes the configuration; a reload of the running one keeps them (the
// launchers' reset intercept, cell #EE = #41, survives the RESET button: SprinterZxResetFn_Test)
TEST_F(SprinterPldGame_Test, Cells_SetWhenTheConfigurationChanges)
{
    Pld().Cell(0xEE) = 0x41;  // a launcher's reset intercept on Standard
    LoadStandard();
    EXPECT_EQ(Pld().Cell(0xEE), 0x41) << "Standard reloaded: the cells stay";
    EXPECT_EQ(Pld().moduleBeforeLoad, SprinterPldConfigurationRegistry::kStandardIndex);

    LoadGame();
    EXPECT_EQ(Pld().moduleBeforeLoad, SprinterPldConfigurationRegistry::kStandardIndex);
    EXPECT_EQ(Pld().Cell(0xEE), 0x41) << "Game's own #EE";
    Pld().Cell(0xE9) = 0x77;
    LoadGame();
    EXPECT_EQ(Pld().Cell(0xE9), 0x77) << "Game reloaded: the cells stay";

    LoadStandard();
    EXPECT_EQ(Pld().moduleBeforeLoad, SprinterPldConfigurationRegistry::kGameIndex);
    EXPECT_EQ(Pld().Cell(0xEE), 0x00) << "back to Standard: Standard's cells";
    EXPECT_EQ(Pld().Cell(0xE9), 0x05);
}

// The reports: the machine state, the BIOS report and the ZX-mode report name the module and why
TEST_F(SprinterPldGame_Test, Reports_NameTheModuleAndWhy)
{
    LoadGame();
    const StateNode machine = DeviceState::Sprinter(_context);
    const StateNode& pld = Member(machine, "pld");
    EXPECT_EQ(Str(pld, "module"), "Game");
    EXPECT_EQ(Str(pld, "selected_by"), "full_hash");
    EXPECT_NE(Str(pld, "why").find("C0FA3055"), std::string::npos) << Str(pld, "why");
    EXPECT_EQ(Str(pld, "cell_EE"), "0x41");
    EXPECT_TRUE(Member(Member(pld, "game"), "active").b);
    EXPECT_EQ(Str(Member(machine, "video"), "renderer"), "Game");

    for (const StateNode& report : {DeviceState::SprinterBios(_context), DeviceState::SprinterZxMode(_context, false)})
    {
        const StateNode& module = Member(report, "pld");
        EXPECT_EQ(Str(module, "module"), "Game") << DeviceState::ToText(report);
        EXPECT_EQ(Str(module, "selected_by"), "full_hash");
    }
}

// The screen draws through the module's beam-ordered picture: a Game square (column = Mode0 bits 1-0 + Mode1)
// where Standard would read another column; the frame end closes the register's frame
TEST_F(SprinterPldGame_Test, Screen_DrawsTheGamePicture)
{
    auto* screen = dynamic_cast<ScreenSprinter*>(_context->pScreen);
    ASSERT_NE(screen, nullptr);
    SprinterVideoRam& vram = _decoder->GetVideoRam();
    for (uint32_t pen = 0; pen < 0x400; pen++)
    {
        const uint32_t address = SprinterVideoRam::PenAddress(pen);
        vram.Write(address, static_cast<uint8_t>(pen));
        vram.Write(address + 1, static_cast<uint8_t>(pen >> 8));
        vram.Write(address + 2, 0x33);
    }
    for (uint32_t column = 0; column < 0x100; column++)
        vram.Write(0x10 * 1024 + column, static_cast<uint8_t>(column ^ 0xA5));
    LoadGame();
    const uint32_t address = SprinterVideoRam::ModeAddress(0, 0, 0);
    vram.Write(address, 0x24);       // graphics, palette 0, grid offset after it
    vram.Write(address + 1, 0x05);   // column 5 (Standard: (Mode1 & 7) << 3 = 40)
    vram.Write(address + 2, 0x10);   // row #10
    vram.Write(address + 3, 0x12);

    screen->RenderFrameBatch();
    const FramebufferDescriptor& fb = screen->GetFramebufferDescriptor();
    const uint32_t* pixels = reinterpret_cast<const uint32_t*>(fb.memoryBuffer);
    EXPECT_EQ(pixels[16 * fb.width + 48 + 2], vram.Pen((0x05 + 1) ^ 0xA5)) << "column 5 + 1";

    // The GUI status bar and /state/screen name the configuration (DescribeScreenState)
    EXPECT_EQ(screen->DescribeScreenState().videoModeBrief, "PLD Game: 320x256 256c");

    std::vector<uint16_t> pens;
    uint16_t width = 0, height = 0;
    std::string encoding;
    ASSERT_TRUE(screen->IndexedFrame(pens, width, height, encoding));
    EXPECT_EQ(pens[16 * width + 48 + 2], (0x05 + 1) ^ 0xA5);

    auto& game = dynamic_cast<SprinterPldGame&>(_decoder->ActiveModule());
    _context->emulatorState.frame_counter++;
    screen->InitFrame();  // the frame start closes the frame the register ran in
    EXPECT_EQ(game.Video().State().beamT, 0u);
    EXPECT_EQ(game.Video().State().frame, _context->emulatorState.frame_counter);
    EXPECT_EQ(game.Video().State().frameOffset, 0x12) << "square (0, 0) set the register; nothing cleared it";
}

// TTD: the module travels by name with its 16-byte state (the grid offset at the frame start and the frame)
TEST_F(SprinterPldGame_Test, Ttd_TheGameStateTravelsInThePldBlob)
{
    LoadGame();
    auto& game = dynamic_cast<SprinterPldGame&>(_decoder->ActiveModule());
    game.Video().State().offset = 0x5A;
    game.Video().State().frameOffset = 0x21;
    game.Video().State().beamT = 1234;
    game.Video().State().frame = 0x123456789ULL;

    ttd::TTDSprinterPld serializer(*_decoder);
    EXPECT_EQ(serializer.TTDStateSize(),
              ttd::TTDSprinterPld::kFixedSize + sizeof(SprinterGameVideoState) + 2 + sizeof(SprinterAccelState));
    std::vector<uint8_t> saved(serializer.TTDStateSize());
    serializer.TTDSaveState(saved.data());
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(saved.data()) + 139), "Game");

    LoadStandard();
    game.Video().State() = SprinterGameVideoState{};
    ASSERT_EQ(_decoder->BeamVideo(), nullptr);
    serializer.TTDLoadState(saved.data());
    EXPECT_EQ(_decoder->ActiveModule().Descriptor().name, "Game");
    EXPECT_EQ(_decoder->BeamVideo(), &game.Video()) << "the restored module's picture runs";
    // The blob holds the frame-start state: the register as the frame began, beam position 0 (the screen runs the
    // frame from its start again; how far it had caught up is not machine state)
    EXPECT_EQ(game.Video().State().offset, 0x21);
    EXPECT_EQ(game.Video().State().frameOffset, 0x21);
    EXPECT_EQ(game.Video().State().beamT, 0u);
    EXPECT_EQ(game.Video().State().frame, 0x123456789ULL);
    std::vector<uint8_t> again(serializer.TTDStateSize());
    serializer.TTDSaveState(again.data());
    EXPECT_EQ(again, saved);
}
