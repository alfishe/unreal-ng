#include "tsconfengine.h"

#include <algorithm>

#include "emulator/emulatorcontext.h"
#include "emulator/platforms/tsconf/tsconfgeometry.h"
#include "emulator/platforms/tsconf/tsconfinterrupts.h"
#include "emulator/platforms/tsconf/tsconfstate.h"

void TsConfEngine::Reset()
{
    // The latched copies reset with their registers ([V] video_ports.v:138-147);
    // the tile pages and offsets are not reset and keep their latches
    _ts.latVConfig = _ts.regs[TsConfReg::VConfig];
    _ts.latVPage = _ts.regs[TsConfReg::VPage];
    _ts.latPalSel = _ts.regs[TsConfReg::PalSel];
    _ts.latGXOffsL = _ts.regs[TsConfReg::GXOffsL];
    _ts.latGXOffsH = _ts.regs[TsConfReg::GXOffsH];
    _ts.yOffsPending = 0;
    _ts.cntRow = 0;
    _ts.engNextLine = 0;
    RebuildLineTable();
}

void TsConfEngine::RebuildLineTable()
{
    const TsConfLine set = LatchedSet();
    for (TsConfLine& line : _lines)
        line = set;
}

uint32_t TsConfEngine::RasterAt(uint32_t t) const
{
    const uint32_t multiplier = std::max<uint32_t>(_context->emulatorState.current_z80_frequency_multiplier, 1u);
    return std::min<uint32_t>(t / multiplier, kFrameTacts - 1);
}

uint32_t TsConfEngine::LineAt(uint32_t t) const
{
    return RasterAt(t) / kLineTacts;
}

TsConfLine TsConfEngine::LatchedSet() const
{
    TsConfLine set;
    set.vConfig = _ts.latVConfig;
    set.vPage = _ts.latVPage;
    set.palSel = _ts.latPalSel;
    set.t0GPage = _ts.latT0GPage;
    set.t1GPage = _ts.latT1GPage;
    set.gxOffs = static_cast<uint16_t>(_ts.latGXOffsL | ((_ts.latGXOffsH & 1u) << 8));
    set.t0XOffs = static_cast<uint16_t>(_ts.latT0XOffsL | ((_ts.latT0XOffsH & 1u) << 8));
    set.t1XOffs = static_cast<uint16_t>(_ts.latT1XOffsL | ((_ts.latT1XOffsH & 1u) << 8));
    set.cntRow = _ts.cntRow;
    return set;
}

void TsConfEngine::LineStart(uint32_t line)
{
    const uint32_t previous = line ? line - 1 : kLines - 1;

    // Row counter, with the geometry the previous line was shown with:
    // reload at the end of line 31 and after a G_Y_OFFS write, else step
    // after every line of the graphics window
    const uint16_t yOffs = static_cast<uint16_t>(_ts.regs[TsConfReg::GYOffsL] | ((_ts.regs[TsConfReg::GYOffsH] & 1u) << 8));
    if (previous == TsConfGeometry::kFirstVisibleLine - 1 || _ts.yOffsPending)
        _ts.cntRow = yOffs;
    else if (TsConfGeometry::LineInWindow(_ts.latVConfig, previous))
        _ts.cntRow = static_cast<uint16_t>((_ts.cntRow + 1) & 0x1FF);
    _ts.yOffsPending = 0;

    const uint8_t* r = _ts.regs;
    _ts.latVConfig = r[TsConfReg::VConfig];
    _ts.latVPage = r[TsConfReg::VPage];
    _ts.latPalSel = r[TsConfReg::PalSel];
    _ts.latGXOffsL = r[TsConfReg::GXOffsL];
    _ts.latGXOffsH = r[TsConfReg::GXOffsH];
    _ts.latT0GPage = r[TsConfReg::T0GPage];
    _ts.latT1GPage = r[TsConfReg::T1GPage];
    _ts.latT0XOffsL = r[TsConfReg::T0XOffsL];
    _ts.latT0XOffsH = r[TsConfReg::T0XOffsL + 1];
    _ts.latT1XOffsL = r[TsConfReg::T0XOffsL + 4];
    _ts.latT1XOffsH = r[TsConfReg::T0XOffsL + 5];

    _lines[line] = LatchedSet();
}

void TsConfEngine::CatchUp(uint32_t t)
{
    const uint32_t raster = RasterAt(t);
    while (_ts.engNextLine < kLines && _ts.engNextLine * kLineTacts <= raster)
        LineStart(_ts.engNextLine++);
}

void TsConfEngine::SetLiveVideoPage(uint8_t vPage)
{
    _ts.latVPage = vPage;
    const uint32_t current = _ts.engNextLine ? _ts.engNextLine - 1u : 0u;
    _lines[current].vPage = vPage;
}

void TsConfEngine::OnMachineStep(uint32_t t)
{
    _interrupts.OnMachineStep(t);
    CatchUp(t);
}

void TsConfEngine::OnMachineFrameRollover(uint32_t frameLength)
{
    // Every line of the old frame started; the next frame starts at line 0
    CatchUp(kFrameTacts * std::max<uint32_t>(_context->emulatorState.current_z80_frequency_multiplier, 1u));
    _ts.engNextLine = 0;
    _interrupts.OnMachineFrameRollover(frameLength);
}
