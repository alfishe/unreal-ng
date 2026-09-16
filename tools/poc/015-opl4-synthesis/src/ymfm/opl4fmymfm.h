// libopl4 — FM engine, ymfm verification backend (temporary).
//
// Opl4FmYmfm presents the Opl4Fm surface backed by ymfm's YMF262 (OPL3)
// core (core/src/3rdparty/ymfm, pinned 81aec25) so the chip's FM synthesis
// can be A/B-verified against the battle-tested reference while our openMSX-
// audited PCM half stays in place. Selected at build time via
// -DOPL4_FM_BACKEND=ymfm (CMake defines OPL4_FM_YMFM); the default build
// keeps the in-tree compact model.
//
// Division of labour (deliberate):
//   - Synthesis (phase, envelopes, connections, rhythm, 4-op, LFO): ymfm.
//     One generate() call per 684-clock FM boundary — the exact cadence
//     ymfm's own ymf278b uses (clock/684 FM grid, clock/768 output).
//   - Bus-visible semantics (bank-1 aliasing, 0x105 NEW/NEW2, timers T1/T2
//     with their status bits): this class, replicating Opl4Fm field-for-
//     field — the openMSX-audited behaviour must not depend on which
//     synthesis core is plugged in. ymfm's own timer/busy machinery is
//     never advanced (stubs); its status is not read.
//
// Known temporary-backend limitations (documented, not bugs):
//   - Per-channel taps are zeroed: mixer peak meters and per-channel mute
//     have no effect on FM output (Opl4Fm feeds both from the tap array).
//   - Channels() returns a static default array — it exists only for the
//     mute-subtraction routing read in Opl4::AdvanceFmToOutput.
//   - TTD: save/restore shuttles ymfm's vector-based save_restore into the
//     fixed POD region (state layout version 2; sessions are per-backend).
#pragma once

#include "../opl4fm.h"

#include <ymfm_opl.h>

#include <array>
#include <cstdint>
#include <vector>

namespace opl4
{

// ymfm_interface with every callback stubbed: timers and busy live in our
// audited model at the top level, and the IRQ pin is not connected.
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
// register file reads (NEW/NEW2 shadowing, comparator use).
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

class Opl4FmYmfm
{
public:
    static constexpr int kChannelCount = Opl4Fm::kChannelCount; // 18
    static constexpr uint8_t kStatusBusy = Opl4Fm::kStatusBusy;
    static constexpr uint8_t kStatusT1 = Opl4Fm::kStatusT1;
    static constexpr uint8_t kStatusT2 = Opl4Fm::kStatusT2;

    // Timer/status block (12) + length prefix (4) + ymfm payload cap.
    static constexpr size_t kYmfmStateCap = 4096;
    static constexpr size_t kStateSize = 16 + kYmfmStateCap;

    Opl4FmYmfm();

    // Not copyable or movable: ymfm holds a reference to the interface.
    Opl4FmYmfm(const Opl4FmYmfm&) = delete;
    Opl4FmYmfm& operator=(const Opl4FmYmfm&) = delete;

    void Reset();

    // Bank 0/1 register write — same guest-visible semantics as Opl4Fm.
    void WriteReg(uint8_t bank, uint8_t reg, uint8_t data);
    uint8_t ReadStatus() const;

    // One 49516.4 Hz step: ymfm generate() (clock + 4-output mix + clamp16);
    // L = out0 + out2, R = out1 + out3 (both stereo pairs, MAME convention).
    void Advance(int32_t& outL, int32_t& outR,
                 std::array<int32_t, kChannelCount>& channelTaps);

    // Timers (guest-visible): call once per FM step. Identical model to
    // Opl4Fm — the audited T1/T2 semantics must survive the backend swap.
    void AdvanceTimers();

    // POD save/load, side-effect-free save (ymfm engine purity is pinned by
    // the local TTD patch to fm_engine_base::save_restore).
    void SaveState(uint8_t* dst) const;
    void LoadState(const uint8_t* src);

    uint8_t Status() const { return _status; }
    bool NewMode() const { return _newMode; }
    bool New2() const { return _new2; }

    // Static defaults: taps are zeroed, so the mute-subtraction routing in
    // Opl4::AdvanceFmToOutput never reads meaningful values here.
    const std::array<FmChannel, kChannelCount>& Channels() const
    {
        return _chStub;
    }

    // Live ymfm register file (comparator + diagnostics).
    uint8_t YmfmReg(uint32_t regnum) const { return _engine.RegByte(regnum); }

private:
    // Audited timer/status model — field-for-field the Opl4Fm set.
    uint8_t _status = 0;
    uint16_t _timer1 = 0, _timer2 = 0;
    uint16_t _timer1Load = 0, _timer2Load = 0;
    bool _timer1Enable = false, _timer2Enable = false;
    bool _timer1Mask = false, _timer2Mask = false;
    bool _newMode = false;
    bool _new2 = false;

    OplFmInterface _intf;
    OplFmEngine _engine;

    std::array<FmChannel, kChannelCount> _chStub{};
    mutable std::vector<uint8_t> _io; // save/restore shuttle (never state)
};

} // namespace opl4
