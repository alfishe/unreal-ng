// drv-mame.cpp - MAME's saa1099_device (refs/mame) on a stimulus. MAME runs one
// sample per 256 chip clocks (clock_divider), so it is driven sample by sample and
// its state is read at 256-clock boundaries; writes land at the next boundary (the
// device updates its stream to the write time, which is sample-granular). Built with
// -Dprivate=public -Dprotected=public to read the generators.
//   drv-mame <stimulus.saa> <events.txt|->
#include "cosim.h"

#include "emu.h"
#include "saa1099.h"

namespace
{
constexpr int kDivider = 256;
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: %s <stimulus.saa> <events.txt|->\n", argv[0]);
        return 2;
    }
    const saacosim::Stimulus st = saacosim::LoadStimulus(argv[1]);
    saacosim::EventLog log(argv[2]);

    machine_config config;
    static saa1099_device dev(config, "saa", nullptr, 8000000);
    dev.device_start();

    size_t w = 0;
    saacosim::GenState s;
    for (uint64_t j = 0; j * kDivider < st.end; j++)
    {
        // Writes up to this sample boundary act before the sample is generated
        while (w < st.writes.size() && st.writes[w].clock <= j * kDivider)
        {
            const saacosim::Write& wr = st.writes[w++];
            dev.write(wr.address ? 1 : 0, wr.value);
        }
        sound_stream& stream = dev.Stream();
        stream.Begin(1);
        dev.sound_stream_update(stream);

        for (int i = 0; i < 6; i++)
            s.tone[i] = dev.m_channels[i].level & 1;
        for (int n = 0; n < 2; n++)
        {
            s.noiseRaw[n] = dev.m_noise[n].level & 0x3FFFF;
            s.noiseBit[n] = dev.m_noise[n].level & 1;
        }
        for (int e = 0; e < 2; e++)
        {
            s.envOn[e] = dev.m_env_enable[e] ? 1 : 0;
            // MAME applies the envelope factor to all three voices of a group; report voice 2 / 5
            s.envLeft[e] = static_cast<uint8_t>(dev.m_channels[e * 3 + 2].envelope[0] & 0x1F);
            s.envRight[e] = static_cast<uint8_t>(dev.m_channels[e * 3 + 2].envelope[1] & 0x1F);
        }
        // MAME units: amplitude a -> a * 32767 / 16 per voice (half when tone and
        // noise are both high); ours: 8a. Scale to ours, rounded
        const double k = 8.0 / (32767.0 / 16.0);
        s.outLeft = static_cast<int32_t>(stream.Out(0, 0) * k + 0.5);
        s.outRight = static_cast<int32_t>(stream.Out(1, 0) * k + 0.5);
        log.Sample((j + 1) * kDivider, s);
    }
    return 0;
}
