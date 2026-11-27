#include "stdafx.h"

#include "vdac2card.h"

#include <cstdlib>
#include <cstring>

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
        params.drawThread = context->config.vdac2_ft_draw_thread;
        params.drawThreads = context->config.vdac2_ft_draw_threads;
    }
    // UNREAL_VDAC2_FT_DRAW_THREAD=0/1 overrides [VDAC2] FtDrawThread (A/B runs of one build)
    if (const char* env = std::getenv("UNREAL_VDAC2_FT_DRAW_THREAD"); env && *env)
        params.drawThread = std::strcmp(env, "0") != 0;
    // UNREAL_VDAC2_FT_DRAW_THREADS=N overrides [VDAC2] FtDrawThreads
    if (const char* env = std::getenv("UNREAL_VDAC2_FT_DRAW_THREADS"); env && *env)
        params.drawThreads = static_cast<uint32_t>(std::strtoul(env, nullptr, 10));
    return params;
}
}  // namespace

Vdac2Card::Vdac2Card(EmulatorContext* context, std::function<uint32_t()> rasterInFrame)
    : EveHostCard(context, std::move(rasterInFrame), Vdac2Params(context))
{
}
