#pragma once

#include "emulator/ports/models/sprinter/sprinterpldconfiguration.h"
#include "emulator/video/sprinter/sprintergamevideo.h"

/// The "Game" PLD configuration (Sprinter mame-gap-analysis V10; game-configuration.md): the bitstream
/// "Thunder in the Deep" was made for, shipped as `GAME_00.ACX` (C:\DEMOS\GAME_00 of the MAME pack's disk:
/// GAME_00.EXE, TEST_005.EXE, TEST_010.EXE load it) and as LDConf's `GC.BIN` (the same 59 215 bytes).
///
/// No sources exist; what it changes against Standard is taken from the bitstream's own behavior (the author's
/// RELOAD.ASZ next to it), MAME (`m_conf`, sprinter.cpp:399-400, 499-545, 1156-1163) and the BIOS:
///   1. the cells' initial contents: cell #EE (RET_PORT) = #41 instead of 0. Every BIOS reads cell #EE right
///      after a load (3.04 page 8 #02ED, 3.07 EXP.asm Reset_Handler: SET_PORTS #EE <- 0, the old value back);
///      non-zero means "a program waits in page #41": the BIOS maps that page, takes the windows from
///      #FFF0-#FFF3 and jumps to (#FFF4) - the program that loaded the configuration goes on (RELOAD_RET).
///      With Standard's 0 the BIOS cold-boots (the demos' old symptom). MAME: m_ram_pages[#2E] = #41;
///   2. the picture: every square graphics 320 x 256 colors from any byte corner of a 1024 x 256 virtual
///      screen, with the per-square grid offset (SprinterGameVideo). No text: DSS and the BIOS run, their
///      screen output does not show ("all DOS / BIOS functions work except the screen ones", RELOAD.ASZ).
/// Everything else - port codes, memory windows, the accelerator, sound, input, the INT from the mode table -
/// is Standard's (the programs use them exactly so: PAGE3 = #50 for the video RAM, the accelerator fills).
///
/// Worked example: GAME_00.EXE copies GAME_00.ACX into fast RAM (+ "ACEX_30K_LOADING" at #FEF0), writes
/// RELOAD_RET to page #41 #FFF4 and "ZX" to #FFFE, then reloads the PLD (code #2E). The loader streams
/// the 473 720 writes from fast RAM, the hashes come out #C0FA3055 / #3861CFA4: this module. The BIOS finds cell
/// #EE = #41 and returns to RELOAD_RET, which draws the grid and scrolls it with the Mode3 offsets.
class SprinterPldGame : public SprinterPldConfiguration
{
public:
    /// GAME_00.ACX from #0100 (as the reload copies it to fast RAM #1000) = GC.BIN: hashes over the loader's writes
    static constexpr uint32_t kFullHash = 0xC0FA3055;
    static constexpr uint32_t kHeadHash = 0x3861CFA4;  ///< MAME's Game constant (sprinter.cpp:1161)
    /// Cell #EE after a load (RET_PORT: the special page #41 holds the program to return to)
    static constexpr uint8_t kRetPortCell = 0xEE;
    static constexpr uint8_t kRetPortValue = 0x41;

    SprinterPldGame();

    const SprinterPldModuleDescriptor& Descriptor() const override { return _descriptor; }

    const SprinterVideoRenderer* VideoRenderer() const override { return &_video; }
    SprinterBeamVideo* BeamVideo() override { return &_video; }
    bool InitialCells(uint8_t* cells) const override;

    void OnActivate(SprinterPldState& pld) override;
    size_t StateSize() const override { return sizeof(SprinterGameVideoState); }
    void SaveState(uint8_t* dst) const override;
    void LoadState(const uint8_t* src) override;

    SprinterGameVideo& Video() { return _video; }
    const SprinterGameVideo& Video() const { return _video; }

private:
    SprinterPldModuleDescriptor _descriptor;
    SprinterGameVideo _video;
};
