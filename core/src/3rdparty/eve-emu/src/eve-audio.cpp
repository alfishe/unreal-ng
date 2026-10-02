// eve-emu - audio engine timing (spec §3.3). The card has no audio output: the engine
// runs its timing (read pointer, flags, interrupts) and produces no samples.
#include "eve-internal.h"

namespace EveLib
{

namespace
{

constexpr uint32_t kIntSound = 0x08;
constexpr uint32_t kIntPlayback = 0x10;
constexpr uint32_t kFormatAdpcm = 2;
constexpr uint32_t kSoundSilence = 0x00;

uint64_t SystemHz(const EveChip& chip)
{
    return chip.state.power.systemClockHz;
}

// Bytes of sample data consumed after a number of samples.
uint64_t SampleBytes(const EveChip& chip, uint64_t samples)
{
    return (RegGet(chip, Reg::PlaybackFormat) == kFormatAdpcm) ? samples / 2 : samples;
}

uint64_t PlaybackSamples(const EveChip& chip)
{
    const uint64_t hz = SystemHz(chip);
    if (hz == 0)
        return 0;
    return chip.state.audio.playbackClocks * RegGet(chip, Reg::PlaybackFreq) / hz;
}

void EndEffect(EveChip& chip)
{
    chip.state.audio.effectPlaying = 0;
    chip.state.audio.effectEndsAt = 0;
    RegSet(chip, Reg::Play, 0);
    RaiseInterrupt(chip, kIntSound);
}

void EndPlayback(EveChip& chip)
{
    chip.state.audio.playbackPlaying = 0;
    RegSet(chip, Reg::PlaybackPlay, 0);
    RaiseInterrupt(chip, kIntPlayback);
}

} // namespace

void AudioReset(EveChip& chip)
{
    chip.state.audio = AudioState{};
    RegSet(chip, Reg::Play, 0);
    RegSet(chip, Reg::PlaybackPlay, 0);
}

void AudioStartEffect(EveChip& chip)
{
    AudioState& audio = chip.state.audio;
    if ((RegGet(chip, Reg::Sound) & 0xFF) == kSoundSilence)
    {
        // Selecting silence and playing it stops the effect [PG Register Definition 22].
        EndEffect(chip);
        return;
    }
    audio.effectPlaying = 1;
    audio.effectEndsAt = chip.state.scan.clocksSinceReset + SystemHz(chip) * kSoundEffectDurationMs / kMillisecondsPerSecond;
    RegSet(chip, Reg::Play, 1);
}

void AudioStartPlayback(EveChip& chip)
{
    AudioState& audio = chip.state.audio;
    audio.playbackClocks = 0;
    audio.playbackPlaying = 1;
    RegSet(chip, Reg::PlaybackPlay, 1);
    if (RegGet(chip, Reg::PlaybackLength) == 0)
        EndPlayback(chip);
}

void AudioAdvance(EveChip& chip, uint64_t clocks)
{
    AudioState& audio = chip.state.audio;
    if (audio.effectPlaying && chip.state.scan.clocksSinceReset >= audio.effectEndsAt)
        EndEffect(chip);
    if (!audio.playbackPlaying)
        return;
    audio.playbackClocks += clocks;
    const uint64_t length = RegGet(chip, Reg::PlaybackLength);
    if (SampleBytes(chip, PlaybackSamples(chip)) < length)
        return;
    if (RegGet(chip, Reg::PlaybackLoop) & 1)
    {
        // Keep the clock count below one pass so it never overflows.
        const uint64_t hz = SystemHz(chip);
        const uint64_t freq = RegGet(chip, Reg::PlaybackFreq);
        const uint64_t samplesPerPass = (RegGet(chip, Reg::PlaybackFormat) == kFormatAdpcm) ? length * 2 : length;
        if (freq != 0)
        {
            const uint64_t clocksPerPass = samplesPerPass * hz / freq;
            if (clocksPerPass != 0)
                audio.playbackClocks %= clocksPerPass;
        }
        return;
    }
    EndPlayback(chip);
}

uint64_t AudioClocksToNextEvent(const EveChip& chip)
{
    const AudioState& audio = chip.state.audio;
    uint64_t next = UINT64_MAX;
    const uint64_t now = chip.state.scan.clocksSinceReset;
    if (audio.effectPlaying)
        next = audio.effectEndsAt > now ? audio.effectEndsAt - now : 0;
    if (audio.playbackPlaying && !(RegGet(chip, Reg::PlaybackLoop) & 1))
    {
        const uint64_t freq = RegGet(chip, Reg::PlaybackFreq);
        const uint64_t hz = SystemHz(chip);
        if (freq != 0 && hz != 0)
        {
            const uint64_t length = RegGet(chip, Reg::PlaybackLength);
            const uint64_t samples = (RegGet(chip, Reg::PlaybackFormat) == kFormatAdpcm) ? length * 2 : length;
            // First clock count at which samples x hz / freq rounds up to the end.
            const uint64_t endClocks = (samples * hz + freq - 1) / freq;
            const uint64_t left = endClocks > audio.playbackClocks ? endClocks - audio.playbackClocks : 0;
            next = left < next ? left : next;
        }
    }
    return next;
}

uint32_t AudioReadPointer(const EveChip& chip)
{
    const uint32_t start = RegGet(chip, Reg::PlaybackStart);
    if (!chip.state.audio.playbackPlaying)
        return start;
    const uint64_t length = RegGet(chip, Reg::PlaybackLength);
    uint64_t bytes = SampleBytes(chip, PlaybackSamples(chip));
    if (length != 0)
        bytes %= length;
    return static_cast<uint32_t>((start + bytes) & 0xFFFFF);
}

} // namespace EveLib
