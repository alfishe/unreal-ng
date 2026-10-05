#include "debugger/ttd/network/ttdzifi.h"

#include <cstring>

#include "common/modulelogger.h"
#include "debugger/ttd/timetravelhooks.h"
#include "debugger/ttd/ttdinputjournal.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/network/zifi.h"
#include "emulator/io/serial/esp/zifinativemodule.h"

namespace ttd
{
namespace
{
constexpr size_t kMaxBridge = 16u << 20;   // rings, queued uploads and raw test bytes fit easily

ZiFiNativeModule* NativePeer(EmulatorContext* context)
{
    if (!context || !context->pZiFi)
        return nullptr;
    return dynamic_cast<ZiFiNativeModule*>(context->pZiFi->Line().Peer());
}
}  // namespace

size_t TTDZiFi::TTDStateSize() const
{
    return sizeof(ZiFi::State) + 4 + kMaxBridge;
}

void TTDZiFi::TTDSaveStateTo(std::vector<uint8_t>& out) const
{
    ZiFi::State state{};
    if (_context && _context->pZiFi)
        _context->pZiFi->SaveState(state);
    const ZiFiNativeModule* native = NativePeer(_context);
    // reserved[0] = 1: a bridge section follows ([length LE32][SaveBridge]); blobs before Z3b have 0 there
    state.reserved[0] = native ? 1 : 0;
    out.assign(reinterpret_cast<const uint8_t*>(&state), reinterpret_cast<const uint8_t*>(&state) + sizeof(state));
    if (native)
    {
        out.resize(out.size() + 4);
        native->SaveBridge(out);
        const uint32_t length = static_cast<uint32_t>(out.size() - sizeof(state) - 4);
        for (int b = 0; b < 4; ++b)
            out[sizeof(state) + static_cast<size_t>(b)] = static_cast<uint8_t>(length >> (8 * b));
    }
}

void TTDZiFi::TTDSaveState(uint8_t* dst) const
{
    std::vector<uint8_t> out;
    TTDSaveStateTo(out);
    std::memset(dst, 0, TTDStateSize());
    std::memcpy(dst, out.data(), std::min(out.size(), TTDStateSize()));
}

void TTDZiFi::TTDLoadState(const uint8_t* src)
{
    // The registry hands a variable blob of the size it was saved with; the bridge part is self-delimiting and a
    // 32-byte blob (before Z3b) has none
    if (!_context || !_context->pZiFi)
        return;
    ZiFi::State state{};
    std::memcpy(&state, src, sizeof(state));
    _context->pZiFi->LoadState(state);
    ZiFiNativeModule* native = NativePeer(_context);
    if (!native)
        return;
    const ttd::TTDInputJournal* journal =
        _context->pTimeTravelHooks ? &_context->pTimeTravelHooks->InputJournal() : nullptr;
    EspStack::ByteSource bytes = [journal](uint32_t source, uint32_t offset, uint32_t length, std::vector<uint8_t>& out) {
        if (!journal)
            return false;
        const TTDNetInput* net = journal->NetAt(source);
        if (!net || static_cast<uint64_t>(offset) + length > net->payloadLength)
            return false;
        const uint8_t* payload = journal->PayloadOf(*net);
        if (!payload)
            return false;
        out.assign(payload + offset, payload + offset + length);
        return true;
    };
    // reserved[0] says whether a bridge section follows (a blob from before Z3b ends at the state)
    const uint8_t* bridge = state.reserved[0] == 1 ? src + sizeof(state) : nullptr;
    const uint32_t length = bridge ? static_cast<uint32_t>(bridge[0] | (bridge[1] << 8) | (bridge[2] << 16) |
                                                           (static_cast<uint32_t>(bridge[3]) << 24))
                                   : 0;
    if (length > kMaxBridge)
        return;
    if (!native->LoadBridge(bridge ? bridge + 4 : nullptr, length, bytes) && _context->pModuleLogger)
    {
        ModuleLogger* _logger = _context->pModuleLogger;
        const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_DEBUGGER;
        const uint16_t _SUBMODULE = 0x0000;
        MLOGWARNING("TTD ZiFi: the ZIFI-NATIVE file bridge was restored incompletely (received bytes missing from the journal)");
    }
}

uint64_t TTDZiFi::TTDHashState() const
{
    if (!_context || !_context->pZiFi)
        return 0;
    std::vector<uint8_t> out;
    TTDSaveStateTo(out);
    uint64_t hash = 1469598103934665603ull;
    for (uint8_t b : out)
        hash = (hash ^ b) * 1099511628211ull;
    return hash;
}

}  // namespace ttd
