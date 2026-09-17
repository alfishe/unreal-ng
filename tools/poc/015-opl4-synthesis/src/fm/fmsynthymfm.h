// libopl4 — FM engine, ymfm verification backend (temporary).
//
// Opl4FmYmfm is the IFmSynth adapter over ymfm's YMF262 (OPL3) core
// (core/src/3rdparty/ymfm, pinned 81aec25) so the chip's FM synthesis can be
// A/B-verified against the battle-tested reference while our openMSX-audited
// bus layer and PCM half stay in place. Selected at build time via
// -DOPL4_FM_BACKEND=ymfm (CMake defines OPL4_FM_YMFM); the default build
// keeps the in-tree compact model.
//
// Division of labour (rearchitecture §4.3): synthesis only — phase,
// envelopes, connections, rhythm, 4-op, LFO — one generate() call per
// 684-clock FM boundary, the exact cadence ymfm's own ymf278b uses (clock/684
// FM grid, clock/768 output). Bank aliasing, NEW/NEW2, timers and status
// live in FmBus; ymfm's own timer/busy machinery is never advanced (stubs)
// and its status is not read.
//
// Known temporary-backend limitations (documented, not bugs):
//   - No per-channel taps: the bus passes ymfm's pre-mixed stereo pair
//     through, so mixer peak meters and per-channel FM mute have no effect
//     (Opl4Fm feeds both from its tap array).
//   - TTD: save/restore shuttles ymfm's vector-based save_restore into the
//     fixed POD region (state layout 2; sessions are per-backend).
#pragma once

#include "opl4/ifmsynth.h"

#include <ymfm_opl.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace opl4
{

// ymfm_interface with every callback stubbed: timers and busy live in our
// audited bus layer, and the IRQ pin is not connected.
class OplFmInterface final : public ymfm::ymfm_interface
{
public:
    void ymfm_set_timer(uint32_t, int32_t) override {}
    void ymfm_set_busy_end(uint32_t) override {}
    bool ymfm_is_busy() override { return false; }
    void ymfm_update_irq(bool) override {}
};

// ymf262 subclass exposing the protected pieces the adapter needs: the
// address latch (ymf262::reset() does not clear it — we do) and the raw
// register file reads (diagnostics).
class OplFmEngine final : public ymfm::ymf262
{
public:
    explicit OplFmEngine(ymfm::ymfm_interface& intf) : ymfm::ymf262(intf)
    {
    }

    OplFmEngine(const OplFmEngine&) = delete;
    OplFmEngine& operator=(const OplFmEngine&) = delete;

    void ResetAddress() { m_address = 0; }

    // ymfm exposes one non-const save_restore entry point for both
    // directions; the local TTD patch pins the saving direction pure
    // (PATCHES.md), so the const cast is safe — saving only reads.
    void SaveTo(ymfm::ymfm_saved_state& st) const
    {
        const_cast<OplFmEngine*>(this)->save_restore(st);
    }

    // ymfm exposes regs() mutable-only; opl_registers_base::read() is const.
    uint8_t RegByte(uint32_t regnum) const
    {
        return const_cast<OplFmEngine*>(this)->m_fm.regs().read(regnum);
    }
};

class Opl4FmYmfm final : public IFmSynth
{
public:
    // Length prefix (16) + ymfm payload cap.
    static constexpr size_t kYmfmStateCap = 4096;
    static constexpr size_t kStateSize = 16 + kYmfmStateCap;

    Opl4FmYmfm();

    // Not copyable or movable: ymfm holds a reference to the interface.
    Opl4FmYmfm(const Opl4FmYmfm&) = delete;
    Opl4FmYmfm& operator=(const Opl4FmYmfm&) = delete;

    void Reset() override;

    // De-aliased write (bank in bit 8): the byte lands in ymfm's own
    // register file for its synthesis paths; bus semantics are upstream.
    void WriteReg(uint16_t reg, uint8_t data) override;

    // One 49516.4 Hz step: ymfm generate() (clock + 4-output mix + clamp16);
    // L = out0 + out2, R = out1 + out3 (both stereo pairs, MAME convention).
    void Advance(FmOutput& out) override;

    FmCaps Caps() const override
    {
        return {false, true, "ymfm"}; // no taps; exact save via save_restore
    }

    size_t StateSize() const override { return kStateSize; }
    // POD save/load, side-effect-free save (ymfm engine purity is pinned by
    // the local TTD patch to fm_engine_base::save_restore).
    void SaveState(uint8_t* dst) const override;
    void LoadState(const uint8_t* src) override;
    uint32_t LayoutTag() const override { return 0x324D4659; } // "YFM2"

    // Live ymfm register file (diagnostics).
    uint8_t YmfmReg(uint32_t regnum) const { return _engine.RegByte(regnum); }

private:
    OplFmInterface _intf;
    OplFmEngine _engine;

    mutable std::vector<uint8_t> _io; // save/restore shuttle (never state)
};

} // namespace opl4
