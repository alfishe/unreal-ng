// drv-saasound.cpp - SAASound (Dave Hooper, refs/saasound) on a stimulus, clock by
// clock: the device runs at a "sample rate" equal to its clock with no oversampling,
// so one _TickAndOutputStereo is one chip clock. Built with -Dprivate=public to read
// the generators (instrumentation only; the reference code is not changed).
// SAASound keeps 12-bit fractional periods, so its tone edges can land one clock
// late per half period (compare.py allows for it). Noise is seeded with our seed
// (18 bits all ones) so the bit streams line up.
//   drv-saasound <stimulus.saa> <events.txt|->
#include "cosim.h"

#include "SAASound.h"
#include "types.h"
#include "SAADevice.h"

namespace
{
struct Ref
{
    CSAADevice dev;
    unsigned lastLeft = 0, lastRight = 0;

    Ref()
    {
        dev._SetClockRate(8000000);
        dev._SetSampleRate(8000000);
        dev._SetOversample(0);
        dev.m_Noise0.Seed(0x3FFFF);
        dev.m_Noise1.Seed(0x3FFFF);
    }
    void Apply(const saacosim::Write& w)
    {
        if (w.address)
            dev._WriteAddress(w.value);
        else
            dev._WriteData(w.value);
    }
    void Tick() { dev._TickAndOutputStereo(lastLeft, lastRight, 0x3F); }
    void Read(saacosim::GenState& s) const
    {
        const CSAAFreq* osc[6] = {&dev.m_Osc0, &dev.m_Osc1, &dev.m_Osc2, &dev.m_Osc3, &dev.m_Osc4, &dev.m_Osc5};
        for (int i = 0; i < 6; i++)
            s.tone[i] = static_cast<uint8_t>(osc[i]->m_nLevel);
        const CSAANoise* noise[2] = {&dev.m_Noise0, &dev.m_Noise1};
        for (int n = 0; n < 2; n++)
        {
            s.noiseBit[n] = static_cast<uint8_t>(noise[n]->Level());
            s.noiseRaw[n] = static_cast<uint32_t>(noise[n]->m_nRand);
        }
        const CSAAEnv* env[2] = {&dev.m_Env0, &dev.m_Env1};
        for (int e = 0; e < 2; e++)
        {
            s.envOn[e] = env[e]->IsActive() ? 1 : 0;
            s.envLeft[e] = static_cast<uint8_t>(env[e]->LeftLevel());
            s.envRight[e] = static_cast<uint8_t>(env[e]->RightLevel());
        }
        // SAASound units are 4x ours (popcount x 8 per voice vs popcount x 2)
        s.outLeft = static_cast<int32_t>(lastLeft / 4);
        s.outRight = static_cast<int32_t>(lastRight / 4);
    }
};
} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: %s <stimulus.saa> <events.txt|->\n", argv[0]);
        return 2;
    }
    const saacosim::Stimulus st = saacosim::LoadStimulus(argv[1]);
    saacosim::EventLog log(argv[2]);
    static Ref ref;
    saacosim::RunPerClock(ref, st, log);
    return 0;
}
