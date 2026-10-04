// libsam2695 - SoundFont 2.04 loader (RIFF 'sfbk').
//
// Structure errors refuse the bank with a precise reason: a bank is never half-loaded. Irregularities
// the specification itself says to tolerate (a misplaced key range, a zone without a terminal
// generator, an invalid modulator) and common authoring slips that have one obvious repair (a loop
// outside its sample, a zero sample rate, a padded sm24) are fixed and listed in BankModel::warnings.
#include "bank/generators.h"
#include "bank/sha256.h"
#include "sam2695/soundbank.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <fstream>
#include <istream>

namespace sam2695
{

namespace
{

struct Span
{
    const uint8_t* p = nullptr;
    size_t size = 0;
    uint64_t offset = 0; // file offset of p (for messages)
};

uint16_t Le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t Le32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

bool Is(const uint8_t* p, const char* fourcc) { return std::memcmp(p, fourcc, 4) == 0; }

// A chunk id for messages (non-printable bytes as '?')
std::string Fourcc(const uint8_t* p)
{
    std::string s(reinterpret_cast<const char*>(p), 4);
    for (char& c : s)
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E)
            c = '?';
    return s;
}

// The refusal for a chunk that runs past its container; a preceding odd-sized chunk without its pad
// byte (a common writer bug that FluidSynth rejects too) is the usual cause and is named.
std::string PastContainer(const uint8_t* id, const char* where, uint64_t offset, uint64_t len, const std::string& oddBefore)
{
    std::string r = "chunk '" + Fourcc(id) + "' in " + where + " at offset " + std::to_string(offset) +
                    " runs past its container (" + std::to_string(len) + " bytes declared)";
    if (!oddBefore.empty())
        r += "; the chunk before it, '" + oddBefore + "', has an odd size and no RIFF pad byte";
    return r;
}

std::string Name20(const uint8_t* p, size_t n)
{
    size_t len = 0;
    while (len < n && p[len] != 0)
        len++;
    std::string s(reinterpret_cast<const char*>(p), len);
    for (char& c : s)
        if (static_cast<unsigned char>(c) < 0x20 || static_cast<unsigned char>(c) > 0x7E)
            c = '?';
    return s;
}

// Where the sample data lives: a span of a memory image, or offsets in a file read after the lists
struct SampleSource
{
    Span smpl, sm24;                    // memory image
    uint64_t smplOffset = 0, sm24Offset = 0;
    uint64_t smplBytes = 0, sm24Bytes = 0;
    bool haveSmpl = false, haveSm24 = false;
};

class Loader
{
public:
    explicit Loader(BankModel& model) : _model(model) {}

    BankError RunMemory(const uint8_t* data, size_t size, std::string& reason);
    BankError RunFile(std::istream& f, uint64_t size, std::string& reason);

private:
    BankError Fail(BankError e, std::string reason)
    {
        _reason = std::move(reason);
        return e;
    }
    void Warn(std::string w)
    {
        if (_model.warnings.size() < 64)
            _model.warnings.push_back(std::move(w));
        else if (_model.warnings.size() == 64)
            _model.warnings.push_back("further warnings suppressed");
    }

    // Walks the chunks of a container; false when a chunk runs past it.
    template <class F>
    bool Chunks(Span container, const char* where, F&& visit, BankError& err)
    {
        size_t pos = 0;
        std::string oddBefore;
        while (pos + 8 <= container.size)
        {
            const uint8_t* hdr = container.p + pos;
            const uint32_t len = Le32(hdr + 4);
            if (len > container.size - pos - 8)
            {
                err = Fail(BankError::Truncated, PastContainer(hdr, where, container.offset + pos, len, oddBefore));
                return false;
            }
            visit(hdr, Span{hdr + 8, len, container.offset + pos + 8});
            if (err != BankError::None)
                return false;
            oddBefore = (len & 1u) ? Fourcc(hdr) : std::string();
            pos += 8 + len + (len & 1u);
        }
        return true;
    }

    BankError ParseInfo(Span s);
    BankError ParsePdta(Span s);
    BankError CheckHeader(const uint8_t* head12, uint64_t size, uint64_t& riffEnd);
    BankError ParseLists(bool haveInfo, Span info, bool haveSdta, bool havePdta, Span pdta, const SampleSource& src);
    bool Sm24Usable(uint64_t bytes, uint64_t frames);
    BankError BuildZones(const char* list, Span bags, Span gens, Span mods, uint32_t firstBag, uint32_t endBag,
                         bool preset, uint32_t linkCount, int32_t& globalZone, uint32_t& zoneFirst,
                         uint32_t& zoneCount, const std::string& owner);

    BankModel& _model;
    std::string _reason;
    bool _haveIfil = false;
    uint32_t _smplFrames = 0;
};

BankError Loader::ParseInfo(Span s)
{
    BankError err = BankError::None;
    Chunks(s, "INFO", [&](const uint8_t* hdr, Span c) {
        if (Is(hdr, "ifil"))
        {
            if (c.size < 4)
            {
                err = Fail(BankError::Truncated, "INFO/ifil is shorter than 4 bytes");
                return;
            }
            _haveIfil = true;
            _model.versionMajor = Le16(c.p);
            _model.versionMinor = Le16(c.p + 2);
        }
        else if (Is(hdr, "INAM"))
            _model.name = Name20(c.p, c.size);
    }, err);
    if (err != BankError::None)
        return err;
    if (!_haveIfil)
        return Fail(BankError::MissingChunk, "INFO/ifil (version) is missing");
    if (_model.versionMajor == 3)
        return Fail(BankError::Compressed, "SF3 bank (ifil 3.x, Ogg Vorbis samples): not supported, use the SF2 "
                                           "(uncompressed) version of the bank");
    if (_model.versionMajor != 2)
        return Fail(BankError::BadVersion, "unsupported SoundFont version " + std::to_string(_model.versionMajor) +
                                               "." + std::to_string(_model.versionMinor) + " (only 2.x)");
    if (_model.name.empty())
        Warn("INFO/INAM (bank name) is missing");
    return BankError::None;
}

BankError Loader::BuildZones(const char* list, Span bags, Span gens, Span mods, uint32_t firstBag, uint32_t endBag,
                             bool preset, uint32_t linkCount, int32_t& globalZone, uint32_t& zoneFirst,
                             uint32_t& zoneCount, const std::string& owner)
{
    const uint32_t genCount = static_cast<uint32_t>(gens.size / 4);
    const uint32_t modCount = static_cast<uint32_t>(mods.size / 10);
    const int terminal = preset ? static_cast<int>(Gen::Instrument) : static_cast<int>(Gen::SampleId);
    globalZone = -1;
    zoneFirst = static_cast<uint32_t>(_model.zones.size());
    zoneCount = 0;
    std::vector<Zone> local;
    for (uint32_t b = firstBag; b < endBag; b++)
    {
        const uint8_t* bag = bags.p + b * 4;
        const uint32_t g0 = Le16(bag), g1 = Le16(bag + 4);
        const uint32_t m0 = Le16(bag + 2), m1 = Le16(bag + 6);
        if (g1 < g0 || g1 > genCount)
            return Fail(BankError::BadIndex, std::string(list) + " bag " + std::to_string(b) + " of '" + owner +
                                                 "': generator index " + std::to_string(g1) +
                                                 " is out of range or decreasing");
        if (m1 < m0 || m1 > modCount)
            return Fail(BankError::BadIndex, std::string(list) + " bag " + std::to_string(b) + " of '" + owner +
                                                 "': modulator index " + std::to_string(m1) +
                                                 " is out of range or decreasing");
        Zone z;
        for (int g = 0; g < kGenCount; g++)
            z.gens[g] = 0;
        bool hasTerminal = false;
        int position = 0;
        for (uint32_t i = g0; i < g1; i++, position++)
        {
            const uint8_t* rec = gens.p + i * 4;
            const uint16_t oper = Le16(rec);
            const uint16_t amount = Le16(rec + 2);
            if (oper == static_cast<uint16_t>(Gen::KeyRange) || oper == static_cast<uint16_t>(Gen::VelRange))
            {
                // keyRange must be first, velRange first or right after keyRange (8.1.2); else ignored
                const bool isKey = oper == static_cast<uint16_t>(Gen::KeyRange);
                const bool okPos = isKey ? position == 0
                                         : position == 0 || (position == 1 && z.IsSet(Gen::KeyRange));
                if (!okPos)
                {
                    Warn(owner + ": " + (isKey ? "keyRange" : "velRange") + " not first in its zone, ignored");
                    continue;
                }
                uint8_t lo = static_cast<uint8_t>(amount & 0xFF), hi = static_cast<uint8_t>(amount >> 8);
                if (lo > 127 || hi > 127 || lo > hi)
                {
                    Warn(owner + ": range " + std::to_string(lo) + "-" + std::to_string(hi) + " clamped");
                    lo = std::min<uint8_t>(lo, 127);
                    hi = std::min<uint8_t>(hi, 127);
                    if (lo > hi)
                        std::swap(lo, hi);
                }
                if (isKey)
                {
                    z.keyLo = lo;
                    z.keyHi = hi;
                }
                else
                {
                    z.velLo = lo;
                    z.velHi = hi;
                }
                z.setMask |= uint64_t(1) << oper;
                continue;
            }
            if (oper == terminal)
            {
                if (amount >= linkCount)
                    return Fail(BankError::BadIndex, owner + ": " + (preset ? "instrument" : "sample") + " index " +
                                                         std::to_string(amount) + " is out of range (" +
                                                         std::to_string(linkCount) + " defined)");
                z.link = amount;
                hasTerminal = true;
                if (i + 1 < g1)
                    Warn(owner + ": generators after the zone's " + (preset ? "instrument" : "sampleID") +
                         " generator ignored");
                break;
            }
            const GenInfo& info = GenInfoOf(oper);
            if (!info.valid || (preset && !info.presetAllowed) ||
                oper == static_cast<uint16_t>(preset ? Gen::SampleId : Gen::Instrument))
                continue; // unused, reserved or not allowed at this level: ignored (8.1.2, 8.5)
            z.gens[oper] = static_cast<int16_t>(amount);
            z.setMask |= uint64_t(1) << oper;
        }
        const bool first = b == firstBag;
        if (!hasTerminal && !first)
        {
            Warn(owner + ": zone " + std::to_string(b - firstBag) + " has no " + (preset ? "instrument" : "sampleID") +
                 " generator, ignored");
            continue;
        }
        // Modulators: drop invalid ones, a later identical one replaces an earlier one (9.5.1)
        z.modFirst = static_cast<uint32_t>(_model.modulators.size());
        for (uint32_t i = m0; i < m1; i++)
        {
            const uint8_t* rec = mods.p + i * 10;
            ModulatorDef m;
            m.src = Le16(rec);
            m.dest = Le16(rec + 2);
            m.amount = static_cast<int16_t>(Le16(rec + 4));
            m.amtSrc = Le16(rec + 6);
            m.transform = Le16(rec + 8);
            const bool linked = (m.dest & 0x8000) != 0;
            if (!IsValidModSource(m.src) || !IsValidModSource(m.amtSrc) || (m.transform != 0 && m.transform != 2) ||
                linked || (m.src & modsrc::kIndexMask) == modsrc::kLink ||
                !GenInfoOf(m.dest).valid || m.dest == static_cast<uint16_t>(Gen::Instrument) ||
                m.dest == static_cast<uint16_t>(Gen::SampleId))
            {
                if (m.src != 0 || m.dest != 0 || m.amount != 0)
                    Warn(owner + ": modulator " + std::to_string(i) + (linked ? " (linked)" : "") +
                         " not supported or invalid, ignored");
                continue;
            }
            bool replaced = false;
            for (uint32_t k = z.modFirst; k < _model.modulators.size(); k++)
                if (SameModulator(_model.modulators[k], m))
                {
                    _model.modulators[k] = m;
                    replaced = true;
                }
            if (!replaced)
                _model.modulators.push_back(m);
        }
        z.modCount = static_cast<uint32_t>(_model.modulators.size()) - z.modFirst;
        if (!hasTerminal)
        {
            // the first zone without a terminal generator is the global zone
            _model.zones.push_back(z);
            globalZone = static_cast<int32_t>(_model.zones.size() - 1);
            zoneFirst = static_cast<uint32_t>(_model.zones.size());
            continue;
        }
        local.push_back(z);
    }
    zoneFirst = static_cast<uint32_t>(_model.zones.size());
    for (const Zone& z : local)
        _model.zones.push_back(z);
    zoneCount = static_cast<uint32_t>(local.size());
    return BankError::None;
}

BankError Loader::ParsePdta(Span s)
{
    struct Need
    {
        const char* id;
        size_t record;
        Span span;
        bool found;
    };
    Need need[9] = {{"phdr", 38, {}, false}, {"pbag", 4, {}, false}, {"pmod", 10, {}, false},
                    {"pgen", 4, {}, false},  {"inst", 22, {}, false}, {"ibag", 4, {}, false},
                    {"imod", 10, {}, false}, {"igen", 4, {}, false},  {"shdr", 46, {}, false}};
    BankError err = BankError::None;
    if (!Chunks(s, "pdta", [&](const uint8_t* hdr, Span c) {
            for (Need& n : need)
                if (Is(hdr, n.id) && !n.found)
                {
                    n.span = c;
                    n.found = true;
                }
        }, err))
        return err;
    for (Need& n : need)
    {
        if (!n.found)
            return Fail(BankError::MissingChunk, std::string("pdta/") + n.id + " is missing");
        if (n.span.size % n.record != 0)
            return Fail(BankError::BadRecordSize, std::string("pdta/") + n.id + " is " + std::to_string(n.span.size) +
                                                      " bytes, not a multiple of its " + std::to_string(n.record) +
                                                      "-byte record");
        if (n.span.size / n.record < 1)
            return Fail(BankError::BadTerminal, std::string("pdta/") + n.id + " has no terminal record");
    }
    const Span phdr = need[0].span, pbag = need[1].span, pmod = need[2].span, pgen = need[3].span;
    const Span inst = need[4].span, ibag = need[5].span, imod = need[6].span, igen = need[7].span;
    const Span shdr = need[8].span;
    const uint32_t presetCount = static_cast<uint32_t>(phdr.size / 38) - 1;
    const uint32_t instCount = static_cast<uint32_t>(inst.size / 22) - 1;
    const uint32_t sampleCount = static_cast<uint32_t>(shdr.size / 46) - 1;
    const uint32_t pbagCount = static_cast<uint32_t>(pbag.size / 4);
    const uint32_t ibagCount = static_cast<uint32_t>(ibag.size / 4);
    if (presetCount == 0)
        return Fail(BankError::NoPresets, "pdta/phdr holds only its terminal record: no presets");

    // Samples
    const uint32_t frames = _smplFrames;
    _model.samples.resize(sampleCount);
    for (uint32_t i = 0; i < sampleCount; i++)
    {
        const uint8_t* r = shdr.p + i * 46;
        SampleInfo& si = _model.samples[i];
        si.name = Name20(r, 20);
        si.start = Le32(r + 20);
        si.end = Le32(r + 24);
        si.loopStart = Le32(r + 28);
        si.loopEnd = Le32(r + 32);
        si.sampleRate = Le32(r + 36);
        si.originalPitch = r[40];
        si.pitchCorrection = static_cast<int8_t>(r[41]);
        si.type = Le16(r + 44);
        if (si.type & 0x10)
            return Fail(BankError::Compressed, "sample '" + si.name + "' is compressed (SF3): not supported");
        if (si.type & 0x8000)
        {
            Warn("sample '" + si.name + "' lives in a ROM this bank does not include: zones using it are silent");
            si.start = si.end = si.loopStart = si.loopEnd = 0;
            continue;
        }
        if (si.start > si.end || si.start >= frames)
        {
            if (si.start == si.end)
            {
                Warn("sample '" + si.name + "' is empty");
                si.start = si.end = si.loopStart = si.loopEnd = 0;
                continue;
            }
            return Fail(BankError::BadSample, "sample '" + si.name + "' (" + std::to_string(i) + ") spans " +
                                                  std::to_string(si.start) + ".." + std::to_string(si.end) +
                                                  ", outside the " + std::to_string(frames) + " frames of smpl");
        }
        if (si.end > frames)
        {
            Warn("sample '" + si.name + "' ends past smpl, clamped");
            si.end = frames;
        }
        if (si.loopStart < si.start || si.loopEnd > si.end || si.loopStart >= si.loopEnd)
        {
            const uint32_t ls = std::clamp(si.loopStart, si.start, si.end);
            const uint32_t le = std::clamp(si.loopEnd, si.start, si.end);
            if (ls < le)
            {
                si.loopStart = ls;
                si.loopEnd = le;
            }
            else
            {
                si.loopStart = si.start;
                si.loopEnd = si.end;
            }
            if (!(si.loopStart == 0 && si.loopEnd == 0))
                Warn("sample '" + si.name + "': loop outside the sample, clamped");
        }
        if (si.sampleRate == 0)
        {
            Warn("sample '" + si.name + "' has sample rate 0, using 44100");
            si.sampleRate = 44100;
        }
        if (si.originalPitch > 127)
            si.originalPitch = 60; // 255: unpitched (7.10)
    }

    // Instruments
    _model.instruments.resize(instCount);
    for (uint32_t i = 0; i < instCount; i++)
    {
        const uint8_t* r = inst.p + i * 22;
        const uint32_t b0 = Le16(r + 20), b1 = Le16(r + 22 + 20);
        Instrument& in = _model.instruments[i];
        in.name = Name20(r, 20);
        if (b1 < b0 || b1 > ibagCount - 1)
            return Fail(BankError::BadIndex, "instrument '" + in.name + "': bag index " + std::to_string(b1) +
                                                 " is out of range or decreasing");
        const BankError e = BuildZones("ibag", ibag, igen, imod, b0, b1, false, sampleCount, in.globalZone,
                                       in.zoneFirst, in.zoneCount, "instrument '" + in.name + "'");
        if (e != BankError::None)
            return e;
    }

    // Presets
    std::vector<Preset> presets(presetCount);
    for (uint32_t i = 0; i < presetCount; i++)
    {
        const uint8_t* r = phdr.p + i * 38;
        const uint32_t b0 = Le16(r + 24), b1 = Le16(r + 38 + 24);
        Preset& p = presets[i];
        p.name = Name20(r, 20);
        p.program = Le16(r + 20);
        p.bank = Le16(r + 22);
        if (b1 < b0 || b1 > pbagCount - 1)
            return Fail(BankError::BadIndex, "preset '" + p.name + "': bag index " + std::to_string(b1) +
                                                 " is out of range or decreasing");
        const BankError e = BuildZones("pbag", pbag, pgen, pmod, b0, b1, true, instCount, p.globalZone, p.zoneFirst,
                                       p.zoneCount, "preset '" + p.name + "'");
        if (e != BankError::None)
            return e;
    }
    std::stable_sort(presets.begin(), presets.end(), [](const Preset& a, const Preset& b) {
        return a.bank != b.bank ? a.bank < b.bank : a.program < b.program;
    });
    for (const Preset& p : presets)
    {
        if (!_model.presets.empty() && _model.presets.back().bank == p.bank && _model.presets.back().program == p.program)
        {
            Warn("preset " + std::to_string(p.bank) + ":" + std::to_string(p.program) + " '" + p.name +
                 "' duplicates an earlier one, ignored");
            continue;
        }
        _model.presets.push_back(p);
    }
    return BankError::None;
}

BankError Loader::CheckHeader(const uint8_t* head, uint64_t size, uint64_t& riffEnd)
{
    if (size < 12 || !Is(head, "RIFF"))
        return Fail(BankError::NotRiff, "no RIFF header");
    if (!Is(head + 8, "sfbk"))
        return Fail(BankError::NotSfbk, "RIFF form is '" + Fourcc(head + 8) + "', not 'sfbk'");
    uint64_t riffSize = Le32(head + 4);
    if (riffSize > size - 8)
    {
        Warn("RIFF size " + std::to_string(riffSize) + " exceeds the file, using the file size");
        riffSize = size - 8;
    }
    riffEnd = 8 + riffSize;
    return BankError::None;
}

bool Loader::Sm24Usable(uint64_t bytes, uint64_t frames)
{
    // sm24 holds one byte per smpl frame, padded to an even size (7.2), and exists from version 2.04
    if (_model.versionMajor == 2 && _model.versionMinor < 4)
    {
        Warn("sdta/sm24 present in a version 2." + std::to_string(_model.versionMinor) + " bank, ignored");
        return false;
    }
    if (bytes != frames && bytes != frames + (frames & 1u))
    {
        Warn("sdta/sm24 is " + std::to_string(bytes) + " bytes for " + std::to_string(frames) + " frames, ignored");
        return false;
    }
    return true;
}

BankError Loader::ParseLists(bool haveInfo, Span info, bool haveSdta, bool havePdta, Span pdta,
                             const SampleSource& src)
{
    if (!haveInfo)
        return Fail(BankError::MissingChunk, "LIST INFO is missing");
    BankError err = ParseInfo(info);
    if (err != BankError::None)
        return err;
    if (!haveSdta)
        return Fail(BankError::MissingChunk, "LIST sdta is missing");
    if (!src.haveSmpl)
        return Fail(BankError::MissingChunk, "sdta/smpl is missing");
    if (!havePdta)
        return Fail(BankError::MissingChunk, "LIST pdta is missing");
    if (src.smplBytes & 1u)
        Warn("sdta/smpl has an odd size, last byte ignored");
    if (src.smplBytes / 2 > 0xFFFFFFFFu)
        return Fail(BankError::BadSample, "sdta/smpl holds more than 2^32 frames");
    _smplFrames = static_cast<uint32_t>(src.smplBytes / 2);
    return ParsePdta(pdta);
}

BankError Loader::RunMemory(const uint8_t* data, size_t size, std::string& reason)
{
    uint64_t riffEnd = 0;
    BankError err = CheckHeader(data, size, riffEnd);
    Span info, pdta;
    SampleSource src;
    bool haveInfo = false, haveSdta = false, havePdta = false;
    if (err == BankError::None)
        Chunks(Span{data + 12, static_cast<size_t>(riffEnd - 12), 12}, "RIFF", [&](const uint8_t* hdr, Span c) {
            if (!Is(hdr, "LIST") || c.size < 4)
                return;
            const Span body{c.p + 4, c.size - 4, c.offset + 4};
            if (Is(c.p, "INFO") && !haveInfo)
            {
                info = body;
                haveInfo = true;
            }
            else if (Is(c.p, "pdta") && !havePdta)
            {
                pdta = body;
                havePdta = true;
            }
            else if (Is(c.p, "sdta") && !haveSdta)
            {
                haveSdta = true;
                Chunks(body, "sdta", [&](const uint8_t* h, Span sc) {
                    if (Is(h, "smpl") && !src.haveSmpl)
                    {
                        src.smpl = sc;
                        src.smplBytes = sc.size;
                        src.haveSmpl = true;
                    }
                    else if (Is(h, "sm24") && !src.haveSm24)
                    {
                        src.sm24 = sc;
                        src.sm24Bytes = sc.size;
                        src.haveSm24 = true;
                    }
                }, err);
            }
        }, err);
    if (err == BankError::None)
        err = ParseLists(haveInfo, info, haveSdta, havePdta, pdta, src);
    if (err != BankError::None)
    {
        reason = _reason;
        return err;
    }
    _model.data16.resize(_smplFrames);
    for (size_t i = 0; i < _smplFrames; i++)
        _model.data16[i] = static_cast<int16_t>(Le16(src.smpl.p + i * 2));
    if (src.haveSm24 && Sm24Usable(src.sm24Bytes, _smplFrames))
        _model.data24.assign(src.sm24.p, src.sm24.p + _smplFrames);
    reason.clear();
    return BankError::None;
}

BankError Loader::RunFile(std::istream& f, uint64_t size, std::string& reason)
{
    // The lists (INFO, pdta) are read into memory; the sample data goes straight into the model, so a
    // bank of several gigabytes needs its own size once, not twice.
    auto readAt = [&f](uint64_t at, uint8_t* out, size_t n) {
        f.clear();
        f.seekg(static_cast<std::streamoff>(at));
        return static_cast<bool>(f.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(n)));
    };
    uint8_t head[12] = {};
    uint64_t riffEnd = 0;
    BankError err = size >= 12 && readAt(0, head, 12) ? CheckHeader(head, size, riffEnd)
                                                       : Fail(BankError::NotRiff, "no RIFF header");
    std::vector<uint8_t> infoBuf, pdtaBuf;
    uint64_t infoAt = 0, pdtaAt = 0;
    SampleSource src;
    bool haveInfo = false, haveSdta = false, havePdta = false;
    auto walk = [&](uint64_t from, uint64_t to, const char* where, auto&& visit) {
        uint64_t pos = from;
        std::string oddBefore;
        while (err == BankError::None && pos + 8 <= to)
        {
            uint8_t hdr[8];
            if (!readAt(pos, hdr, 8))
            {
                err = Fail(BankError::Io, std::string("read error in ") + where);
                return;
            }
            const uint64_t len = Le32(hdr + 4);
            if (len > to - pos - 8)
            {
                err = Fail(BankError::Truncated, PastContainer(hdr, where, pos, len, oddBefore));
                return;
            }
            visit(hdr, pos + 8, len);
            oddBefore = (len & 1u) ? Fourcc(hdr) : std::string();
            pos += 8 + len + (len & 1u);
        }
    };
    if (err == BankError::None)
        walk(12, riffEnd, "RIFF", [&](const uint8_t* hdr, uint64_t body, uint64_t len) {
            uint8_t type[4] = {};
            if (!Is(hdr, "LIST") || len < 4 || !readAt(body, type, 4))
                return;
            auto load = [&](std::vector<uint8_t>& buf, bool& have, uint64_t& at) {
                at = body + 4;
                buf.resize(static_cast<size_t>(len - 4));
                have = buf.empty() || readAt(body + 4, buf.data(), buf.size());
                if (!have)
                    err = Fail(BankError::Io, "read error in LIST " + Fourcc(type));
            };
            if (Is(type, "INFO") && !haveInfo)
                load(infoBuf, haveInfo, infoAt);
            else if (Is(type, "pdta") && !havePdta)
                load(pdtaBuf, havePdta, pdtaAt);
            else if (Is(type, "sdta") && !haveSdta)
            {
                haveSdta = true;
                walk(body + 4, body + len, "sdta", [&](const uint8_t* h, uint64_t at, uint64_t n) {
                    if (Is(h, "smpl") && !src.haveSmpl)
                    {
                        src.smplOffset = at;
                        src.smplBytes = n;
                        src.haveSmpl = true;
                    }
                    else if (Is(h, "sm24") && !src.haveSm24)
                    {
                        src.sm24Offset = at;
                        src.sm24Bytes = n;
                        src.haveSm24 = true;
                    }
                });
            }
        });
    if (err == BankError::None)
        err = ParseLists(haveInfo, Span{infoBuf.data(), infoBuf.size(), infoAt}, haveSdta, havePdta,
                         Span{pdtaBuf.data(), pdtaBuf.size(), pdtaAt}, src);
    if (err == BankError::None)
    {
        _model.data16.resize(_smplFrames);
        if (_smplFrames > 0 &&
            !readAt(src.smplOffset, reinterpret_cast<uint8_t*>(_model.data16.data()), size_t(_smplFrames) * 2))
            err = Fail(BankError::Io, "read error in sdta/smpl");
        if constexpr (std::endian::native == std::endian::big)
            for (int16_t& v : _model.data16)
                v = static_cast<int16_t>(Le16(reinterpret_cast<const uint8_t*>(&v)));
    }
    if (err == BankError::None && src.haveSm24 && Sm24Usable(src.sm24Bytes, _smplFrames))
    {
        _model.data24.resize(_smplFrames);
        if (_smplFrames > 0 && !readAt(src.sm24Offset, _model.data24.data(), _smplFrames))
            err = Fail(BankError::Io, "read error in sdta/sm24");
    }
    if (err != BankError::None)
    {
        _model.data16.clear();
        _model.data24.clear();
        reason = _reason;
        return err;
    }
    reason.clear();
    return BankError::None;
}

} // namespace

const Preset* ISoundBank::FindPreset(uint16_t bank, uint16_t program) const
{
    const std::vector<Preset>& p = Model().presets;
    auto it = std::lower_bound(p.begin(), p.end(), std::make_pair(bank, program),
                               [](const Preset& a, const std::pair<uint16_t, uint16_t>& key) {
                                   return a.bank != key.first ? a.bank < key.first : a.program < key.second;
                               });
    if (it != p.end() && it->bank == bank && it->program == program)
        return &*it;
    return nullptr;
}

const char* BankErrorName(BankError e)
{
    switch (e)
    {
        case BankError::None: return "None";
        case BankError::Io: return "Io";
        case BankError::NotRiff: return "NotRiff";
        case BankError::NotSfbk: return "NotSfbk";
        case BankError::Truncated: return "Truncated";
        case BankError::MissingChunk: return "MissingChunk";
        case BankError::BadVersion: return "BadVersion";
        case BankError::Compressed: return "Compressed";
        case BankError::BadRecordSize: return "BadRecordSize";
        case BankError::BadTerminal: return "BadTerminal";
        case BankError::BadIndex: return "BadIndex";
        case BankError::BadSample: return "BadSample";
        case BankError::NoPresets: return "NoPresets";
    }
    return "?";
}

Sf2Bank::LoadResult Sf2Bank::LoadMemory(const uint8_t* data, size_t size)
{
    LoadResult result;
    auto bank = std::shared_ptr<Sf2Bank>(new Sf2Bank());
    Loader loader(bank->_model);
    result.error = loader.RunMemory(data, size, result.reason);
    if (result.error != BankError::None)
        return result;
    bank->_digest = Sha256(data, size);
    result.bank = std::move(bank);
    return result;
}

Sf2Bank::LoadResult Sf2Bank::LoadFile(const std::string& path)
{
    LoadResult result;
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
    {
        result.error = BankError::Io;
        result.reason = "cannot open '" + path + "'";
        return result;
    }
    const std::streamoff size = f.tellg();
    if (size <= 0)
    {
        result.error = BankError::Io;
        result.reason = "'" + path + "' is empty";
        return result;
    }
    // identity: SHA-256 of the whole file, streamed
    Sha256Stream hash;
    std::vector<uint8_t> chunk(1u << 20);
    f.seekg(0);
    for (std::streamoff left = size; left > 0;)
    {
        const std::streamsize n = static_cast<std::streamsize>(std::min<std::streamoff>(left, static_cast<std::streamoff>(chunk.size())));
        if (!f.read(reinterpret_cast<char*>(chunk.data()), n))
        {
            result.error = BankError::Io;
            result.reason = "cannot read '" + path + "'";
            return result;
        }
        hash.Update(chunk.data(), static_cast<size_t>(n));
        left -= n;
    }
    auto bank = std::shared_ptr<Sf2Bank>(new Sf2Bank());
    Loader loader(bank->_model);
    result.error = loader.RunFile(f, static_cast<uint64_t>(size), result.reason);
    if (result.error != BankError::None)
        return result;
    bank->_digest = hash.Final();
    result.bank = std::move(bank);
    return result;
}

} // namespace sam2695
