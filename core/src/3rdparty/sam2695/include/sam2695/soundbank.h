// libsam2695 - sound banks.
//
// ISoundBank is the read-only view the synthesizer plays from. It is shared between Synth instances
// (std::shared_ptr<const ISoundBank>) and never changes after loading. Sf2Bank reads SoundFont 2.04
// files; a DLS loader can fill the same model later.
//
// The model keeps the SF2 structure (presets -> preset zones -> instruments -> instrument zones ->
// samples, with generators and modulators per zone) because the SF2 generator arithmetic (instrument
// values absolute, preset values relative) happens per note.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sam2695
{

using BankDigest = std::array<uint8_t, 32>; // SHA-256 of the bank file

// SF2 2.04 generator numbers (section 8.1.2). Pitch (59) is the internal destination of the
// default pitch-wheel modulator (8.4.10), not a file generator.
enum class Gen : uint8_t
{
    StartAddrsOffset = 0,
    EndAddrsOffset = 1,
    StartloopAddrsOffset = 2,
    EndloopAddrsOffset = 3,
    StartAddrsCoarseOffset = 4,
    ModLfoToPitch = 5,
    VibLfoToPitch = 6,
    ModEnvToPitch = 7,
    InitialFilterFc = 8,
    InitialFilterQ = 9,
    ModLfoToFilterFc = 10,
    ModEnvToFilterFc = 11,
    EndAddrsCoarseOffset = 12,
    ModLfoToVolume = 13,
    ChorusEffectsSend = 15,
    ReverbEffectsSend = 16,
    Pan = 17,
    DelayModLfo = 21,
    FreqModLfo = 22,
    DelayVibLfo = 23,
    FreqVibLfo = 24,
    DelayModEnv = 25,
    AttackModEnv = 26,
    HoldModEnv = 27,
    DecayModEnv = 28,
    SustainModEnv = 29,
    ReleaseModEnv = 30,
    KeynumToModEnvHold = 31,
    KeynumToModEnvDecay = 32,
    DelayVolEnv = 33,
    AttackVolEnv = 34,
    HoldVolEnv = 35,
    DecayVolEnv = 36,
    SustainVolEnv = 37,
    ReleaseVolEnv = 38,
    KeynumToVolEnvHold = 39,
    KeynumToVolEnvDecay = 40,
    Instrument = 41,
    KeyRange = 43,
    VelRange = 44,
    StartloopAddrsCoarseOffset = 45,
    Keynum = 46,
    Velocity = 47,
    InitialAttenuation = 48,
    EndloopAddrsCoarseOffset = 50,
    CoarseTune = 51,
    FineTune = 52,
    SampleId = 53,
    SampleModes = 54,
    ScaleTuning = 56,
    ExclusiveClass = 57,
    OverridingRootKey = 58,
    Pitch = 59,
    EndOper = 60
};
constexpr int kGenCount = 61;

// One SF2 modulator record (sfModList / sfInstModList, section 7.4 / 7.8).
struct ModulatorDef
{
    uint16_t src = 0;
    uint16_t dest = 0;
    int16_t amount = 0;
    uint16_t amtSrc = 0;
    uint16_t transform = 0;
};

struct Zone
{
    std::array<int16_t, kGenCount> gens{}; // raw amounts; ranges are kept in keyLo..velHi
    uint64_t setMask = 0;                  // bit g set when the zone sets generator g
    uint8_t keyLo = 0, keyHi = 127, velLo = 0, velHi = 127;
    uint32_t modFirst = 0, modCount = 0;   // into BankModel::modulators
    int32_t link = -1;                     // instrument index (preset zone) or sample index (instrument zone)

    bool IsSet(Gen g) const { return (setMask >> static_cast<int>(g)) & 1u; }
};

struct Instrument
{
    std::string name;
    int32_t globalZone = -1;               // into BankModel::zones
    uint32_t zoneFirst = 0, zoneCount = 0; // local zones (global excluded)
};

struct Preset
{
    std::string name;
    uint16_t program = 0;
    uint16_t bank = 0;
    int32_t globalZone = -1;
    uint32_t zoneFirst = 0, zoneCount = 0;
};

enum class SampleLink : uint16_t
{
    Mono = 1,
    Right = 2,
    Left = 4,
    Linked = 8
};

struct SampleInfo
{
    std::string name;
    uint32_t start = 0, end = 0;           // frames in the sample data, end exclusive
    uint32_t loopStart = 0, loopEnd = 0;   // frames in the sample data, loopEnd exclusive
    uint32_t sampleRate = 44100;
    uint8_t originalPitch = 60;
    int8_t pitchCorrection = 0;
    uint16_t type = 1;                     // SampleLink bits
};

struct BankModel
{
    std::string name;                      // INAM
    uint16_t versionMajor = 2, versionMinor = 1; // ifil
    std::vector<Preset> presets;           // sorted by (bank, program)
    std::vector<Instrument> instruments;
    std::vector<Zone> zones;               // preset and instrument zones
    std::vector<ModulatorDef> modulators;
    std::vector<SampleInfo> samples;
    std::vector<int16_t> data16;           // smpl, every frame
    std::vector<uint8_t> data24;           // sm24 low bytes, empty when the bank has none
    std::vector<std::string> warnings;     // tolerated irregularities (fixed while loading)
};

class ISoundBank
{
public:
    virtual ~ISoundBank() = default;

    virtual const BankModel& Model() const = 0;
    virtual const BankDigest& Digest() const = 0;

    // The preset for (bank, program), or nullptr.
    const Preset* FindPreset(uint16_t bank, uint16_t program) const;
};

enum class BankError : uint8_t
{
    None,
    Io,                 // file cannot be read
    NotRiff,            // no RIFF header
    NotSfbk,            // RIFF form is not 'sfbk'
    Truncated,          // a chunk runs past its container
    MissingChunk,       // a required chunk is absent (reason names it)
    BadVersion,         // ifil major version is not 2
    Compressed,         // SF3 (Ogg Vorbis samples): not supported in v1
    BadRecordSize,      // a pdta chunk is not a whole number of records
    BadTerminal,        // a pdta list lacks its terminal record
    BadIndex,           // bag / generator / modulator / instrument / sample index out of range or not monotonic
    BadSample,          // a sample header points outside the sample data
    NoPresets           // nothing playable
};

const char* BankErrorName(BankError e);

class Sf2Bank final : public ISoundBank
{
public:
    struct LoadResult
    {
        std::shared_ptr<const Sf2Bank> bank; // null when refused
        BankError error = BankError::None;
        std::string reason;                  // precise, human-readable
    };

    static LoadResult LoadFile(const std::string& path);
    static LoadResult LoadMemory(const uint8_t* data, size_t size);

    const BankModel& Model() const override { return _model; }
    const BankDigest& Digest() const override { return _digest; }

private:
    BankModel _model;
    BankDigest _digest{};
};

// SHA-256 of a buffer (the bank identity).
BankDigest Sha256(const uint8_t* data, size_t size);
std::string DigestHex(const BankDigest& d);

} // namespace sam2695
