#pragma once
#include "emulator/memory/memory.h"

struct TsConfState;

/// TS-Conf memory subsystem (TSConf technical-design §3.5, hardware-spec §2).
///
/// Owns the whole latch-to-bank translation (UpdateModelBanks override):
/// - window 0: normal mode (PAGE0 directly) or mapped mode
///   ({PAGE0[7:2], ~DOS, ROM128}), ROM (page[4:0]) or RAM, W0_WE write
///   protection, vdos -> RAM page 0xFF writable;
/// - windows 1-3: PAGE1-3, RAM only.
/// And the functional CPU cache (§2.5): 256 one-word entries indexed by
/// A[8:1], filled by CPU RAM reads, hit when CACHE_CONFIG enables the window,
/// invalidated by a CPU write to the entry. DMA and video writes do not
/// invalidate (stale reads after DMA are hardware-correct).
///
/// The state lives in TsConfState, owned by PortDecoder_TSConf, which attaches
/// it here (AttachState). Without a state (before the decoder exists) the
/// banks show the reset layout: ROM 0, RAM 5, 2, 0.
class TsConfMemory : public Memory
{
    /// region <Constructors / Destructors>
public:
    TsConfMemory() = delete;
    explicit TsConfMemory(EmulatorContext* context);
    ~TsConfMemory() override = default;
    /// endregion </Constructors / Destructors>

    /// region <Model overrides>
public:
    uint8_t MemoryReadFast(uint16_t addr, bool isExecution) override;
    uint8_t MemoryReadDebug(uint16_t addr, bool isExecution) override;

    void AttachState(TsConfState* state) { _ts = state; }
    TsConfState* GetState() const { return _ts; }

    /// Cache (§2.5): the read path fills / hits only while any window has the
    /// cache enabled (see CacheRead), so the plain path is one bool test
    void SetCacheActive(bool active) { _cacheActive = active; }
    /// A CPU write to addr: drop the entry it hits (the decoder's write overlay)
    void CacheInvalidate(uint16_t addr);
    /// Drop every entry (cache switched off: entries filled while disabled are
    /// not modeled, see CacheRead)
    void CacheClear();
    /// endregion </Model overrides>

    /// region <Latch-to-bank translation>
protected:
    bool UpdateModelBanks() override;
    /// endregion </Latch-to-bank translation>

private:
    /// The cached byte for a CPU RAM read at addr (fills the entry on a miss)
    uint8_t CacheRead(uint16_t addr, uint8_t normal);

    TsConfState* _ts = nullptr;
    bool _cacheActive = false;
};
