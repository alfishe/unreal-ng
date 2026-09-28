#include "gshostclock.h"

#include <cmath>

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"

double GSHostClock::unitsPerZxTact(const EmulatorContext* context, double unitsPerSecond)
{
    if (!context)
        return unitsPerSecond / static_cast<double>(CPU_CLOCK_RATE);

    const CONFIG& config = context->config;
    if (config.frame == 0 || config.frame_duration_us == 0)
        return unitsPerSecond / static_cast<double>(CPU_CLOCK_RATE);

    const double zxBaseHz = static_cast<double>(config.frame) / (static_cast<double>(config.frame_duration_us) * 1e-6);
    const double hostMultiplier = static_cast<double>(context->emulatorState.HostSpeedMultiplier());
    return unitsPerSecond / (zxBaseHz * hostMultiplier);
}

uint64_t GSHostClock::currentZxTacts(const EmulatorContext* context, uint64_t fallback)
{
    if (context && context->pCore && context->pCore->GetZ80())
        return static_cast<uint64_t>(context->emulatorState.AudioTstate(context->pCore->GetZ80()->t));
    return fallback;
}

int64_t GSHostClock::frameUnits(const EmulatorContext* context, double unitsPerSecond)
{
    if (!context)
        return 0;
    const double tacts = static_cast<double>(context->config.frame) *
                         static_cast<double>(context->emulatorState.HostSpeedMultiplier());
    return static_cast<int64_t>(std::llround(tacts * unitsPerZxTact(context, unitsPerSecond)));
}

bool GSHostClock::targetUnits(const EmulatorContext* context, double unitsPerSecond, uint64_t frameStartZxTacts,
                              int64_t frameStartUnits, int64_t& target)
{
    const uint64_t zxTacts = currentZxTacts(context, frameStartZxTacts);
    if (zxTacts < frameStartZxTacts)
        return false;
    const uint64_t relative = zxTacts - frameStartZxTacts;
    target = frameStartUnits +
             static_cast<int64_t>(std::llround(static_cast<double>(relative) * unitsPerZxTact(context, unitsPerSecond)));
    return true;
}
