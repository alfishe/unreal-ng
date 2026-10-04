#include "sprinterpldgame.h"

#include <cstring>

#include "emulator/ports/models/sprinter/sprinterpldstandard.h"
#include "emulator/ports/models/sprinter/sprinterpldstate.h"

SprinterPldGame::SprinterPldGame()
{
    _descriptor.name = "Game";
    _descriptor.fullHash = kFullHash;
    _descriptor.headHash = kHeadHash;
    _descriptor.streams = {{kFullHash, "GAME_00.ACX from #0100 = LDConf's GC.BIN = titd-k30.acx (\"Thunder in the Deep\")"}};
}

bool SprinterPldGame::InitialCells(uint8_t* cells) const
{
    // Standard's embedded RAM contents, except RET_PORT (MAME sprinter.cpp:1162; the BIOS return, see the header)
    std::memcpy(cells, SprinterPldStandard::kCells, sizeof(SprinterPldStandard::kCells));
    cells[kRetPortCell - 0xC0] = kRetPortValue;
    return true;
}

void SprinterPldGame::OnActivate(SprinterPldState& pld)
{
    (void)pld;
    // The decoder starts the picture at the frame and T the load ended (SprinterBeamVideo::Start)
    _video.Start(0, 0);
}

void SprinterPldGame::SaveState(uint8_t* dst) const
{
    // The frame-start state: the register as the frame began, beam position 0. How far the picture has been run
    // inside the frame is not machine state - it depends on when the screen last caught up (a seek's repaint, a
    // rendered or a skipped frame); running from the frame start gives the same register again. A checkpoint sits at
    // the frame start anyway, right after the frame start closed the frame before (offset = frame offset, beam 0)
    SprinterGameVideoState state = _video.State();
    state.offset = state.frameOffset;
    state.beamT = 0;
    std::memcpy(dst, &state, sizeof(state));
}

void SprinterPldGame::LoadState(const uint8_t* src)
{
    std::memcpy(&_video.State(), src, sizeof(SprinterGameVideoState));
}
