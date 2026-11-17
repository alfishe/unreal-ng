#include "stdafx.h"

#include "vdac2card.h"

#include "emulator/emulatorcontext.h"

namespace
{
EveHostCard::Params Vdac2Params(const EmulatorContext* context)
{
    EveHostCard::Params params;
    params.name = "VDAC2";
    params.historyId = "vdac2";
    params.historyLabel = "FT812 SPI transactions";
    params.crystalHz = Vdac2Card::kCrystalHz;
    if (context)
    {
        params.romPath = context->config.vdac2_rom_path;
        params.capturePath = context->config.vdac2_capture_path;
        params.lineBudgetMargin = context->config.vdac2_line_budget_margin;
    }
    return params;
}
}  // namespace

Vdac2Card::Vdac2Card(EmulatorContext* context, std::function<uint32_t()> rasterInFrame)
    : EveHostCard(context, std::move(rasterInFrame), Vdac2Params(context))
{
}
