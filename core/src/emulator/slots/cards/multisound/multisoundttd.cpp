#include "multisoundttd.h"

#include <vector>

#include "emulator/slots/cards/multisound/multisoundslotcard.h"
#include "sam2695/sam2695.h"

namespace
{

uint64_t Fnv1a(const uint8_t* data, size_t size)
{
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < size; i++)
    {
        hash ^= data[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

void PutU64(uint8_t*& p, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        *p++ = static_cast<uint8_t>(v >> (8 * i));
}

uint64_t GetU64(const uint8_t*& p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v |= static_cast<uint64_t>(*p++) << (8 * i);
    return v;
}

}  // namespace

/// region <MultiSoundCardTtd>

size_t MultiSoundCardTtd::TTDStateSize() const
{
    return kCardOffset + _card.Card().TtdStateSize();
}

void MultiSoundCardTtd::TTDSaveState(uint8_t* dst) const
{
    uint8_t* p = dst;
    *p++ = kVersion;
    PutU64(p, _card.Origin());
    PutU64(p, _card.LastTime());
    PutU64(p, _card.MixerSamplePhase());
    _card.Card().TtdSave(p);
}

void MultiSoundCardTtd::TTDLoadState(const uint8_t* src)
{
    const uint8_t* p = src;
    if (*p++ != kVersion)
        return;
    const uint64_t origin = GetU64(p);
    const uint64_t last = GetU64(p);
    const uint64_t samplePhase = GetU64(p);
    if (_card.Card().TtdLoad(p))
    {
        _card.RestoreTime(origin, last);
        // The mixer still counts samples from its live, pre-seek phase: the frames after the restore must have the
        // sample counts they were recorded with, or the card renders them to other lengths and its YM2203
        // decimator phase (TTD state) leaves the recording
        _card.AdoptMixerSamplePhase(samplePhase);
    }
}

uint64_t MultiSoundCardTtd::TTDHashState() const
{
    std::vector<uint8_t> blob(TTDStateSize());
    TTDSaveState(blob.data());
    return Fnv1a(blob.data(), blob.size());
}

ttd::TTDDeviceDescriptor MultiSoundCardTtd::TTDDescribe() const
{
    ttd::TTDDeviceDescriptor d = ttd::TTDSerializable::TTDDescribe();
    d.runsBehindCpu = true;
    d.timeFields.push_back({ 1, 8 });   // the adapter's origin
    d.timeFields.push_back({ 9, 8 });   // its last time
    _card.Card().TtdTimeFields(d.timeFields, kCardOffset);
    return d;
}

bool MultiSoundCardTtd::TTDSyncedTime(int64_t& offset) const
{
    return _card.Card().TtdSynced(_card.Position(), offset);
}

/// endregion </MultiSoundCardTtd>

/// region <Sam2695Ttd>

size_t Sam2695Ttd::TTDStateSize() const
{
    return _synth.StateSize();
}

void Sam2695Ttd::TTDSaveState(uint8_t* dst) const
{
    _synth.SaveState(dst);
}

void Sam2695Ttd::TTDLoadState(const uint8_t* src)
{
    if (!_synth.LoadState(src, _synth.StateSize()))
        _refused++;
}

uint64_t Sam2695Ttd::TTDHashState() const
{
    std::vector<uint8_t> blob(TTDStateSize());
    TTDSaveState(blob.data());
    return Fnv1a(blob.data(), blob.size());
}

ttd::TTDDeviceDescriptor Sam2695Ttd::TTDDescribe() const
{
    ttd::TTDDeviceDescriptor d = ttd::TTDSerializable::TTDDescribe();
    d.firmwareFingerprint = BankFingerprint(_synth);
    return d;
}

uint64_t Sam2695Ttd::BankFingerprint(const sam2695::Synth& synth)
{
    const sam2695::ISoundBank* bank = synth.Bank();
    if (bank == nullptr)
        return 0;
    const sam2695::BankDigest& digest = bank->Digest();
    return Fnv1a(digest.data(), digest.size());
}

/// endregion </Sam2695Ttd>
