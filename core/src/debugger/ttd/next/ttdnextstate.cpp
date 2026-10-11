#include "ttdnextstate.h"

#include <cstring>

#include "emulator/io/z80n/nextstatecodec.h"
#include "emulator/ports/models/portdecoder_next.h"

namespace ttd {

TTDNextState::TTDNextState(PortDecoder_Next& decoder) : _decoder(decoder), _size(NextStateCodec::Save(decoder).size()) {}

void TTDNextState::TTDSaveState(uint8_t* dst) const
{
    if (!dst)
        return;
    const std::vector<uint8_t> image = NextStateCodec::Save(_decoder);
    std::memcpy(dst, image.data(), image.size() < _size ? image.size() : _size);
}

void TTDNextState::TTDLoadState(const uint8_t* src)
{
    if (src)
        NextStateCodec::Load(_decoder, src, _size);
}

uint64_t TTDNextState::TTDHashState() const
{
    uint64_t h = 0xcbf29ce484222325ULL;  // FNV-1a offset basis
    for (uint8_t byte : NextStateCodec::Save(_decoder))
    {
        h ^= byte;
        h *= 0x100000001b3ULL;  // FNV-1a prime
    }
    return h;
}

}  // namespace ttd
