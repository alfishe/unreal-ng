// drv-ours.cpp - our Saa1099 (core/src/emulator/sound/chips/saa1099) on a stimulus,
// clock by clock (host axis = chip clock, 1:1), generator state from Describe.
//   drv-ours <stimulus.saa> <events.txt|-> [--authentic] [--tone-start-low]
// --tone-start-low starts every tone generator low after power-on and after RST (the
// MiSTer RTL does; MAME at power-on; we follow SAASound's high start). It edits the
// level bytes of the TTD blob, so the comparison against those references can look
// past the polarity choice.
#include "cosim.h"

#include <vector>

#include "emulator/sound/chips/saa1099/saa1099.h"

namespace
{
struct Ours
{
    Saa1099 chip;
    uint64_t t = 0;
    bool toneStartLow = false;
    uint8_t address = 0;
    bool sync = false;

    void ToneLevelsLow()
    {
        // Blob layout (saa1099.cpp TTDSaveState): version, 32 registers, 4 flags,
        // then per tone generator u32 counter, level, tone, octave
        std::vector<uint8_t> blob(chip.TTDStateSize());
        chip.TTDSaveState(blob.data());
        for (int i = 0; i < 6; i++)
            blob[1 + 32 + 4 + i * 7 + 4] = 0;
        chip.TTDLoadState(blob.data());
    }

    Ours(bool authentic, bool startLow) : toneStartLow(startLow)
    {
        Saa1099Config cfg;
        cfg.hostTickRate = 8000000;
        cfg.chipClockHz = 8000000;
        cfg.renderMode = authentic ? Saa1099RenderMode::Authentic : Saa1099RenderMode::HiFi;
        chip.Configure(cfg);
        if (toneStartLow)
            ToneLevelsLow();
    }
    void Apply(const saacosim::Write& w)
    {
        if (w.address)
        {
            chip.WriteAddress(t, w.value);
            address = w.value & 0x1F;
            return;
        }
        chip.WriteData(t, w.value);
        if (address == 0x1C)
        {
            const bool rising = (w.value & 2) && !sync;
            sync = (w.value & 2) != 0;
            if (rising && toneStartLow)
                ToneLevelsLow();
        }
    }
    void Tick() { chip.Run(++t); }
    void Read(saacosim::GenState& s) const
    {
        Saa1099Report r;
        chip.Describe(r);
        for (int i = 0; i < 6; i++)
            s.tone[i] = r.tones[i].level;
        for (int n = 0; n < 2; n++)
        {
            s.noiseBit[n] = r.noise[n].output;
            s.noiseRaw[n] = r.noise[n].lfsr;
        }
        for (int e = 0; e < 2; e++)
        {
            s.envOn[e] = r.envelopes[e].enabled;
            s.envLeft[e] = r.envelopes[e].levelLeft;
            s.envRight[e] = r.envelopes[e].levelRight;
        }
        s.outLeft = r.outputLeft;
        s.outRight = r.outputRight;
    }
};
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: %s <stimulus.saa> <events.txt|-> [--authentic] [--tone-start-low]\n", argv[0]);
        return 2;
    }
    bool authentic = false;
    bool toneStartLow = false;
    for (int i = 3; i < argc; i++)
    {
        authentic |= strcmp(argv[i], "--authentic") == 0;
        toneStartLow |= strcmp(argv[i], "--tone-start-low") == 0;
    }
    const saacosim::Stimulus st = saacosim::LoadStimulus(argv[1]);
    saacosim::EventLog log(argv[2]);
    Ours ours(authentic, toneStartLow);
    saacosim::RunPerClock(ours, st, log);
    return 0;
}
