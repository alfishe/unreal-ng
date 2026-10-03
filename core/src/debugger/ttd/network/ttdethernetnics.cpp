#include "debugger/ttd/network/ttdethernetnics.h"

#include <cstring>
#include <vector>

#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/network/networkmanager.h"
#include "emulator/io/network/vnet/ethernetgateway.h"

namespace ttd
{
namespace
{
const std::vector<NetworkManager::SlotCard>* Cards(EmulatorContext* context)
{
    NetworkManager* manager = context && context->pCore ? context->pCore->GetNetworkManager() : nullptr;
    return manager ? &manager->SlotCards() : nullptr;
}

size_t CardsSize(EmulatorContext* context)
{
    size_t size = 2;
    if (const auto* cards = Cards(context))
    {
        for (const auto& card : *cards)
        {
            if (card.ne2000)
                size += 1 + card.ne2000->PortKey().size() + card.ne2000->StateSize();
        }
    }
    return size;
}
}  // namespace

size_t TTDEthernetNics::TTDStateSize() const
{
    return CardsSize(_context) + 4 + 16u * 1024 * 1024;
}

void TTDEthernetNics::TTDSaveStateTo(std::vector<uint8_t>& out) const
{
    out.clear();
    out.reserve(CardsSize(_context) + 64);
    out.push_back(kVersion);
    out.push_back(0);
    if (const auto* cards = Cards(_context))
    {
        for (const auto& card : *cards)
        {
            if (!card.ne2000)
                continue;
            const std::string& key = card.ne2000->PortKey();
            out.push_back(static_cast<uint8_t>(key.size()));
            out.insert(out.end(), key.begin(), key.end());
            const size_t at = out.size();
            out.resize(at + card.ne2000->StateSize());
            card.ne2000->SaveState(out.data() + at);
            ++out[1];
        }
    }
    std::vector<uint8_t> gateway;
    if (_context && _context->pEthernetGateway)
        gateway = _context->pEthernetGateway->SaveState();
    const uint32_t length = static_cast<uint32_t>(gateway.size());
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<uint8_t>(length >> (8 * i)));
    out.insert(out.end(), gateway.begin(), gateway.end());
}

void TTDEthernetNics::TTDSaveState(uint8_t* dst) const
{
    if (!dst)
        return;
    std::vector<uint8_t> bytes;
    TTDSaveStateTo(bytes);
    std::memcpy(dst, bytes.data(), bytes.size());
}

void TTDEthernetNics::TTDLoadState(const uint8_t* src)
{
    const auto* cards = Cards(_context);
    if (!src || !cards || src[0] != kVersion)
        return;
    size_t at = 2;
    const uint8_t count = src[1];
    uint8_t index = 0;
    for (const auto& card : *cards)
    {
        if (!card.ne2000)
            continue;
        if (index++ >= count)
            return;
        const size_t keyLength = src[at++];
        const std::string key(reinterpret_cast<const char*>(src + at), keyLength);
        at += keyLength;
        if (key != card.ne2000->PortKey())
            return;   // another population: the ISA session guard refuses such a recording
        card.ne2000->LoadState(src + at, card.ne2000->StateSize());
        at += card.ne2000->StateSize();
    }
    const uint32_t length = static_cast<uint32_t>(src[at]) | (static_cast<uint32_t>(src[at + 1]) << 8) |
                            (static_cast<uint32_t>(src[at + 2]) << 16) | (static_cast<uint32_t>(src[at + 3]) << 24);
    at += 4;
    if (length && _context->pEthernetGateway)
        _context->pEthernetGateway->LoadState(src + at, length);
}

uint64_t TTDEthernetNics::TTDHashState() const
{
    std::vector<uint8_t> bytes;
    TTDSaveStateTo(bytes);
    uint64_t h = 1469598103934665603ull;
    for (uint8_t b : bytes)
    {
        h ^= b;
        h *= 1099511628211ull;
    }
    return h;
}

}  // namespace ttd
