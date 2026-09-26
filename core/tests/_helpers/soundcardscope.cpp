#include "soundcardscope.h"

#include <atomic>

#include "emulator/config.h"
#include "emulator/platform.h"

namespace
{
std::atomic<int> g_activeScopes{0};
}  // namespace

SoundCardScope::SoundCardScope()
{
    g_activeScopes.fetch_add(1, std::memory_order_acq_rel);
}

SoundCardScope::~SoundCardScope()
{
    g_activeScopes.fetch_sub(1, std::memory_order_acq_rel);
}

bool SoundCardScope::Active()
{
    return g_activeScopes.load(std::memory_order_acquire) > 0;
}

void SoundCardScope::InstallPolicy()
{
    Config::SetConfigLoadedHook([](CONFIG& config)
    {
        if (Active())
            return;  // the test asked for the machine as configured

        config.sound.gsTypeKind = GSTypeKind::NONE;
        config.sound.moonsound = 0;
    });
}
