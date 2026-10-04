// libsam2695 tests - SoundFont 2 banks generated in code, so no unit test needs an external file.
//
// Sf2Builder writes a spec-conforming file into memory: samples (16-bit, optional sm24), one
// instrument per preset with any number of zones, optional global zones and modulators. Hooks let a
// test corrupt the result to exercise each refusal reason.
#ifndef SAM2695_SF2BUILDER_H
#define SAM2695_SF2BUILDER_H

#include "sam2695/soundbank.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace sam2695test
{

using sam2695::Gen;
using sam2695::ModulatorDef;

struct BSample
{
    std::string name;
    std::vector<int32_t> frames; // 16-bit values, or 24-bit when the bank has sm24
    uint32_t rate = 37500;
    uint8_t root = 69;
    int8_t correction = 0;
    uint32_t loopStart = 0, loopEnd = 0; // relative to the sample, end exclusive; 0/0 = whole sample
};

struct BZone
{
    int sample = 0;                    // instrument zone: sample index; preset zone: unused
    std::map<int, int> gens;           // generator -> amount
    int keyLo = -1, keyHi = -1, velLo = -1, velHi = -1;
    std::vector<ModulatorDef> mods;
};

struct BPreset
{
    std::string name;
    uint16_t bank = 0, program = 0;
    std::vector<BZone> zones;          // instrument zones
    bool hasInstGlobal = false;
    BZone instGlobal;
    BZone presetZone;                  // the single preset zone (gens are relative)
    bool hasPresetGlobal = false;
    BZone presetGlobal;
};

class Sf2Builder
{
public:
    std::vector<BSample> samples;
    std::vector<BPreset> presets;
    bool use24 = false;
    uint16_t versionMajor = 2, versionMinor = 4;

    int AddSample(const BSample& s)
    {
        samples.push_back(s);
        return static_cast<int>(samples.size() - 1);
    }

    std::vector<uint8_t> Build() const
    {
        std::vector<uint8_t> smpl, sm24, shdr;
        uint32_t frame = 0;
        for (const BSample& s : samples)
        {
            const uint32_t start = frame;
            for (int32_t v : s.frames)
            {
                const int32_t hi = use24 ? (v >> 8) : v;
                Put16(smpl, static_cast<uint16_t>(static_cast<int16_t>(hi)));
                if (use24)
                    sm24.push_back(static_cast<uint8_t>(v & 0xFF));
                frame++;
            }
            const uint32_t end = frame;
            for (int i = 0; i < 46; i++)
            {
                Put16(smpl, 0);
                if (use24)
                    sm24.push_back(0);
                frame++;
            }
            const uint32_t ls = s.loopEnd > s.loopStart ? start + s.loopStart : start;
            const uint32_t le = s.loopEnd > s.loopStart ? start + s.loopEnd : end;
            PutName(shdr, s.name);
            Put32(shdr, start);
            Put32(shdr, end);
            Put32(shdr, ls);
            Put32(shdr, le);
            Put32(shdr, s.rate);
            shdr.push_back(s.root);
            shdr.push_back(static_cast<uint8_t>(s.correction));
            Put16(shdr, 0);
            Put16(shdr, 1);
        }
        PutName(shdr, "EOS");
        for (int i = 0; i < 26; i++)
            shdr.push_back(0);
        if (use24 && (sm24.size() & 1))
            sm24.push_back(0);

        std::vector<uint8_t> phdr, pbag, pmod, pgen, inst, ibag, imod, igen;
        uint16_t pbagIndex = 0, ibagIndex = 0, pgenIndex = 0, igenIndex = 0, pmodIndex = 0, imodIndex = 0;
        for (size_t pi = 0; pi < presets.size(); pi++)
        {
            const BPreset& p = presets[pi];
            PutName(inst, p.name);
            Put16(inst, ibagIndex);
            auto emitInstZone = [&](const BZone& z, bool global) {
                Put16(ibag, igenIndex);
                Put16(ibag, imodIndex);
                ibagIndex++;
                EmitGens(igen, z, igenIndex);
                if (!global)
                {
                    Put16(igen, static_cast<uint16_t>(Gen::SampleId));
                    Put16(igen, static_cast<uint16_t>(z.sample));
                    igenIndex++;
                }
                EmitMods(imod, z, imodIndex);
            };
            if (p.hasInstGlobal)
                emitInstZone(p.instGlobal, true);
            for (const BZone& z : p.zones)
                emitInstZone(z, false);

            PutName(phdr, p.name);
            Put16(phdr, p.program);
            Put16(phdr, p.bank);
            Put16(phdr, pbagIndex);
            Put32(phdr, 0);
            Put32(phdr, 0);
            Put32(phdr, 0);
            auto emitPresetZone = [&](const BZone& z, bool global) {
                Put16(pbag, pgenIndex);
                Put16(pbag, pmodIndex);
                pbagIndex++;
                EmitGens(pgen, z, pgenIndex);
                if (!global)
                {
                    Put16(pgen, static_cast<uint16_t>(Gen::Instrument));
                    Put16(pgen, static_cast<uint16_t>(pi));
                    pgenIndex++;
                }
                EmitMods(pmod, z, pmodIndex);
            };
            if (p.hasPresetGlobal)
                emitPresetZone(p.presetGlobal, true);
            emitPresetZone(p.presetZone, false);
        }
        PutName(phdr, "EOP");
        Put16(phdr, 0);
        Put16(phdr, 0);
        Put16(phdr, pbagIndex);
        Put32(phdr, 0);
        Put32(phdr, 0);
        Put32(phdr, 0);
        Put16(pbag, pgenIndex);
        Put16(pbag, pmodIndex);
        Put32(pgen, 0);
        for (int i = 0; i < 10; i++)
            pmod.push_back(0);
        PutName(inst, "EOI");
        Put16(inst, ibagIndex);
        Put16(ibag, igenIndex);
        Put16(ibag, imodIndex);
        Put32(igen, 0);
        for (int i = 0; i < 10; i++)
            imod.push_back(0);

        std::vector<uint8_t> ifil;
        Put16(ifil, versionMajor);
        Put16(ifil, versionMinor);
        std::vector<uint8_t> info = List("INFO", {Chunk("ifil", ifil), Chunk("isng", Str("EMU8000")),
                                                  Chunk("INAM", Str("libsam2695 test bank"))});
        std::vector<std::vector<uint8_t>> sd = {Chunk("smpl", smpl)};
        if (use24)
            sd.push_back(Chunk("sm24", sm24));
        std::vector<uint8_t> sdta = List("sdta", sd);
        std::vector<uint8_t> pdta =
            List("pdta", {Chunk("phdr", phdr), Chunk("pbag", pbag), Chunk("pmod", pmod), Chunk("pgen", pgen),
                          Chunk("inst", inst), Chunk("ibag", ibag), Chunk("imod", imod), Chunk("igen", igen),
                          Chunk("shdr", shdr)});
        std::vector<uint8_t> body = {'s', 'f', 'b', 'k'};
        body.insert(body.end(), info.begin(), info.end());
        body.insert(body.end(), sdta.begin(), sdta.end());
        body.insert(body.end(), pdta.begin(), pdta.end());
        std::vector<uint8_t> file = {'R', 'I', 'F', 'F'};
        Put32(file, static_cast<uint32_t>(body.size()));
        file.insert(file.end(), body.begin(), body.end());
        return file;
    }

    std::shared_ptr<const sam2695::Sf2Bank> Load() const
    {
        const std::vector<uint8_t> bytes = Build();
        return sam2695::Sf2Bank::LoadMemory(bytes.data(), bytes.size()).bank;
    }

    // Byte offset of a chunk's 4-byte id inside a built file (for corruption tests), or SIZE_MAX.
    static size_t FindChunk(const std::vector<uint8_t>& f, const char* id)
    {
        for (size_t i = 12; i + 4 <= f.size(); i++)
            if (std::memcmp(&f[i], id, 4) == 0)
                return i;
        return SIZE_MAX;
    }

    static void Put16(std::vector<uint8_t>& v, uint16_t x)
    {
        v.push_back(static_cast<uint8_t>(x));
        v.push_back(static_cast<uint8_t>(x >> 8));
    }
    static void Put32(std::vector<uint8_t>& v, uint32_t x)
    {
        Put16(v, static_cast<uint16_t>(x));
        Put16(v, static_cast<uint16_t>(x >> 16));
    }

private:
    static void PutName(std::vector<uint8_t>& v, const std::string& n)
    {
        for (size_t i = 0; i < 20; i++)
            v.push_back(i < n.size() && i < 19 ? static_cast<uint8_t>(n[i]) : 0);
    }
    static std::vector<uint8_t> Str(const char* s)
    {
        std::vector<uint8_t> v(s, s + std::strlen(s) + 1);
        return v;
    }
    static std::vector<uint8_t> Chunk(const char* id, const std::vector<uint8_t>& payload)
    {
        std::vector<uint8_t> c(id, id + 4);
        Put32(c, static_cast<uint32_t>(payload.size()));
        c.insert(c.end(), payload.begin(), payload.end());
        if (payload.size() & 1)
            c.push_back(0);
        return c;
    }
    static std::vector<uint8_t> List(const char* id, const std::vector<std::vector<uint8_t>>& chunks)
    {
        std::vector<uint8_t> body(id, id + 4);
        for (const auto& c : chunks)
            body.insert(body.end(), c.begin(), c.end());
        std::vector<uint8_t> l = {'L', 'I', 'S', 'T'};
        Put32(l, static_cast<uint32_t>(body.size()));
        l.insert(l.end(), body.begin(), body.end());
        return l;
    }
    static void EmitGens(std::vector<uint8_t>& out, const BZone& z, uint16_t& index)
    {
        if (z.keyLo >= 0)
        {
            Put16(out, static_cast<uint16_t>(Gen::KeyRange));
            out.push_back(static_cast<uint8_t>(z.keyLo));
            out.push_back(static_cast<uint8_t>(z.keyHi));
            index++;
        }
        if (z.velLo >= 0)
        {
            Put16(out, static_cast<uint16_t>(Gen::VelRange));
            out.push_back(static_cast<uint8_t>(z.velLo));
            out.push_back(static_cast<uint8_t>(z.velHi));
            index++;
        }
        for (const auto& [g, amount] : z.gens)
        {
            Put16(out, static_cast<uint16_t>(g));
            Put16(out, static_cast<uint16_t>(static_cast<int16_t>(amount)));
            index++;
        }
    }
    static void EmitMods(std::vector<uint8_t>& out, const BZone& z, uint16_t& index)
    {
        for (const ModulatorDef& m : z.mods)
        {
            Put16(out, m.src);
            Put16(out, m.dest);
            Put16(out, static_cast<uint16_t>(m.amount));
            Put16(out, m.amtSrc);
            Put16(out, m.transform);
            index++;
        }
    }
};

// A looped sine: `period` frames per cycle at 37 500 Hz, root key 69: 37500 / period Hz at key 69.
inline BSample SineSample(const std::string& name, int period = 100, int cycles = 8, int amplitude = 16384)
{
    BSample s;
    s.name = name;
    for (int i = 0; i < period * cycles; i++)
        s.frames.push_back(static_cast<int32_t>(std::lround(amplitude * std::sin(2.0 * 3.14159265358979323846 * i / period))));
    s.loopStart = 0;
    s.loopEnd = static_cast<uint32_t>(period * cycles);
    return s;
}

// A constant level (useful for timing: no zero crossings)
inline BSample DcSample(const std::string& name, int frames = 400, int level = 16384)
{
    BSample s;
    s.name = name;
    s.frames.assign(frames, level);
    s.loopStart = 0;
    s.loopEnd = static_cast<uint32_t>(frames);
    return s;
}

// Deterministic white noise (a 32-bit LCG), looped
inline BSample NoiseSample(const std::string& name, int frames = 8192, int amplitude = 16384)
{
    BSample s;
    s.name = name;
    uint32_t x = 12345;
    for (int i = 0; i < frames; i++)
    {
        x = x * 1664525u + 1013904223u;
        s.frames.push_back(static_cast<int32_t>((static_cast<int64_t>(x >> 16) - 32768) * amplitude / 32768));
    }
    s.loopStart = 0;
    s.loopEnd = static_cast<uint32_t>(frames);
    return s;
}

// A simple instrument: one zone over the whole keyboard playing `sample`.
inline BPreset SimplePreset(const std::string& name, uint16_t bank, uint16_t program, int sample,
                            std::map<int, int> gens = {})
{
    BPreset p;
    p.name = name;
    p.bank = bank;
    p.program = program;
    BZone z;
    z.sample = sample;
    z.gens = std::move(gens);
    p.zones.push_back(z);
    return p;
}

constexpr int G(Gen g) { return static_cast<int>(g); }

} // namespace sam2695test

#endif // SAM2695_SF2BUILDER_H
