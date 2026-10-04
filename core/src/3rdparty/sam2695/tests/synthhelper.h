// libsam2695 tests - a Synth on the internal-rate axis: hostTickRate = kInternalRate and outputRate =
// kInternalRate, so one host tick is one internal sample and Render() is the bit-exact internal stream.
#ifndef SAM2695_SYNTHHELPER_H
#define SAM2695_SYNTHHELPER_H

#include "sam2695/sam2695.h"
#include "sf2builder.h"

#include <cmath>
#include <initializer_list>
#include <vector>

namespace sam2695test
{

using namespace sam2695;

struct TestSynth
{
    Synth synth;
    SynthConfig cfg;
    std::vector<float> out; // everything rendered so far, interleaved stereo

    explicit TestSynth(std::shared_ptr<const ISoundBank> bank, SynthConfig c = SynthConfig{})
    {
        cfg = c;
        cfg.hostTickRate = kInternalRate;
        cfg.outputRate = kInternalRate;
        cfg.outputGain = 1.0f;
        synth.Configure(cfg);
        synth.LoadBank(std::move(bank));
    }

    void Send(uint64_t t, std::initializer_list<int> bytes)
    {
        for (int b : bytes)
            synth.WriteByte(t, static_cast<uint8_t>(b));
    }

    // Synthesize up to sample `until` (whole blocks) and append what Render() returns.
    void RunTo(uint64_t until)
    {
        synth.Run(until);
        float buf[512 * 2];
        for (;;)
        {
            const size_t n = synth.Render(buf, 512);
            out.insert(out.end(), buf, buf + n * 2);
            if (n < 512)
                break;
        }
    }

    size_t Frames() const { return out.size() / 2; }
    // NaN past the rendered frames, so a test reading beyond them fails instead of reading garbage
    float L(size_t i) const { return i < Frames() ? out[i * 2] : std::nanf(""); }
    float R(size_t i) const { return i < Frames() ? out[i * 2 + 1] : std::nanf(""); }

    double PeakL(size_t from, size_t to) const
    {
        double p = 0.0;
        for (size_t i = from; i < to && i < Frames(); i++)
            p = std::fmax(p, std::fabs(L(i)));
        return p;
    }
    double PeakR(size_t from, size_t to) const
    {
        double p = 0.0;
        for (size_t i = from; i < to && i < Frames(); i++)
            p = std::fmax(p, std::fabs(R(i)));
        return p;
    }
    double RmsL(size_t from, size_t to) const
    {
        double s = 0.0;
        size_t n = 0;
        for (size_t i = from; i < to && i < Frames(); i++, n++)
            s += static_cast<double>(L(i)) * L(i);
        return n ? std::sqrt(s / n) : 0.0;
    }
    // Frequency from rising zero crossings of the left channel (sub-sample interpolated)
    double FrequencyL(size_t from, size_t to) const
    {
        double first = -1.0, last = -1.0;
        int crossings = 0;
        for (size_t i = from + 1; i < to && i < Frames(); i++)
            if (L(i - 1) < 0.0f && L(i) >= 0.0f)
            {
                const double t = static_cast<double>(i - 1) + L(i - 1) / static_cast<double>(L(i - 1) - L(i));
                if (first < 0.0)
                    first = t;
                else
                    crossings++;
                last = t;
            }
        return crossings > 0 ? crossings * static_cast<double>(kInternalRate) / (last - first) : 0.0;
    }
};

// Bank with: 0:0 looped sine (period 100 -> 375 Hz at key 69), 0:1 looped DC with the shortest attack
// (1 ms, SF2's minimum), 0:2 the same with a 1 s release, 128:0 drum kit (key 42 closed hi-hat / 46 open hi-hat in exclusive class 1, 36 kick).
inline std::shared_ptr<const Sf2Bank> BasicBank()
{
    Sf2Builder b;
    const int sine = b.AddSample(SineSample("sine"));
    const int dc = b.AddSample(DcSample("dc"));
    b.presets.push_back(SimplePreset("Sine", 0, 0, sine, {{G(Gen::SampleModes), 1}}));
    b.presets.push_back(
        SimplePreset("Dc", 0, 1, dc, {{G(Gen::SampleModes), 1}, {G(Gen::AttackVolEnv), -32768}}));
    b.presets.push_back(SimplePreset(
        "Pad", 0, 2, dc, {{G(Gen::SampleModes), 1}, {G(Gen::AttackVolEnv), -32768}, {G(Gen::ReleaseVolEnv), 0}}));
    BPreset kit;
    kit.name = "Kit";
    kit.bank = 128;
    kit.program = 0;
    for (int key : {36, 42, 46})
    {
        BZone z;
        z.sample = sine;
        z.keyLo = z.keyHi = key;
        z.gens = {{G(Gen::SampleModes), 1}, {G(Gen::OverridingRootKey), 69}};
        if (key != 36)
            z.gens[G(Gen::ExclusiveClass)] = 1;
        kit.zones.push_back(z);
    }
    b.presets.push_back(kit);
    return b.Load();
}

} // namespace sam2695test

#endif // SAM2695_SYNTHHELPER_H
