#include "soundcardscope.h"

#include <atomic>

#include "emulator/config.h"
#include "emulator/platform.h"

namespace
{
/// One counter per TestSound bit (scopes nest and overlap)
std::atomic<int> g_activeScopes[4]{};

/// GeneralSoundFitScope's kind (GSTypeKind::NONE = 0: as configured)
std::atomic<uint8_t> g_fitGeneralSound{0};

template <typename F>
void ForEachDevice(TestSound devices, F&& f)
{
    for (int bit = 0; bit < 4; bit++)
    {
        if (static_cast<uint8_t>(devices) & (1u << bit))
            f(g_activeScopes[bit]);
    }
}

int BitIndex(TestSound device)
{
    const auto value = static_cast<uint8_t>(device);
    for (int bit = 0; bit < 4; bit++)
    {
        if (value == (1u << bit))
            return bit;
    }
    return -1;
}
}  // namespace

SoundCardScope::SoundCardScope() : SoundCardScope(TestSound::All)
{
}

SoundCardScope::SoundCardScope(TestSound devices) : _devices(devices)
{
    ForEachDevice(_devices, [](std::atomic<int>& count) { count.fetch_add(1, std::memory_order_acq_rel); });
}

SoundCardScope::~SoundCardScope()
{
    ForEachDevice(_devices, [](std::atomic<int>& count) { count.fetch_sub(1, std::memory_order_acq_rel); });
}

bool SoundCardScope::Active(TestSound device)
{
    const int bit = BitIndex(device);
    return bit >= 0 && g_activeScopes[bit].load(std::memory_order_acquire) > 0;
}

void SoundCardScope::InstallPolicy()
{
    Config::SetConfigLoadedHook([](CONFIG& config)
    {
        // Devices a test asked for keep the configured value
        if (!Active(TestSound::GeneralSound))
            config.sound.gsTypeKind = GSTypeKind::NONE;
        if (GeneralSoundFitScope::Kind() != GSTypeKind::NONE)
            config.sound.gsTypeKind = GeneralSoundFitScope::Kind();
        if (!Active(TestSound::MoonSound))
            config.sound.moonsound = 0;
        if (!Active(TestSound::TurboSound))
            config.sound.turboSoundKind = TurboSoundKind::None;
    });
    Config::SetMidiBankHook([](std::string& bank)
    {
        if (bank.empty() && !Active(TestSound::DefaultMidiBank))
            bank = "NONE";
    });
}

GeneralSoundFitScope::GeneralSoundFitScope(GSTypeKind kind)
    : _previous(g_fitGeneralSound.exchange(static_cast<uint8_t>(kind), std::memory_order_acq_rel))
{
}

GeneralSoundFitScope::~GeneralSoundFitScope()
{
    g_fitGeneralSound.store(_previous, std::memory_order_release);
}

GSTypeKind GeneralSoundFitScope::Kind()
{
    return static_cast<GSTypeKind>(g_fitGeneralSound.load(std::memory_order_acquire));
}
