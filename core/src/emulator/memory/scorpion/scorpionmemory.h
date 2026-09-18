#pragma once
#include "emulator/memory/memory.h"
#include "emulator/memory/scorpion/scorpionromwindow.h"

/// Scorpion ZS 256 memory subsystem - the derived model-specific class for
/// MM_SCORP / MM_PROFSCORP (design §3-§5). It owns everything that makes a
/// Scorpion a Scorpion, so the generic Memory base stays model-agnostic:
/// - the whole latch-to-bank translation (UpdateModelBanks override of the
///   UpdateZ80Banks dispatch),
/// - the ProfROM quadrant window and its ROM loader geometry hook,
/// - the two bus-cycle silicon effects that have no port/latch event to hang
///   on (the firmware switches ProfROM planes purely by READING addresses):
///   GAL DD41 read strobe + DD50.1 magic-button release, run by the
///   MemoryReadFast/Debug overrides BEFORE the base class serves the byte.
///
/// The class is created by Core for Scorpion models and reached through the
/// standard Memory*/MemoryInterface plumbing; virtual dispatch goes through
/// the member-pointer calls the Z80 core already makes.
class ScorpionMemory : public Memory
{
    /// region <Constructors / Destructors>
public:
    ScorpionMemory() = delete;
    ScorpionMemory(EmulatorContext* context);
    ~ScorpionMemory() override = default;
    /// endregion </Constructors / Destructors>

    /// region <Model overrides>
public:
    uint8_t MemoryReadFast(uint16_t addr, bool isExecution) override;
    uint8_t MemoryReadDebug(uint16_t addr, bool isExecution) override;

    /// ROM loader completion hook (design §4.2): derives the ProfROM image
    /// geometry masks from the validated bank count
    void OnRomLoaded(uint16_t imageBanks) override;

    /// ProfROM quadrant window (owned policy object; state itself lives in
    /// EmulatorState / TEMP). Null in the generic base - models without the
    /// silicon have no window
    ScorpionRomWindow* GetScorpionRomWindow() override
    {
        return &_scorpionRomWindow;
    };

    /// Repoint the four ROM role pointers at a ProfROM quadrant (design §4.2)
    void ResolveScorpionRomBases(uint8_t quadrant);
    /// endregion </Model overrides>

    /// region <Latch-to-bank translation>
protected:
    /// Full MM_SCORP / MM_PROFSCORP translation; UpdateZ80Banks() dispatches
    /// here instead of running its generic body (design §3)
    bool UpdateModelBanks() override;
    /// endregion </Latch-to-bank translation>

    /// region <Fields>
private:
    // ProfROM quadrant window (design §4.2): stateless policy object, quadrant
    // state itself lives in EmulatorState / TEMP. The cached gates below feed
    // the read-path override so the silicon effects cost one bool test
    ScorpionRomWindow _scorpionRomWindow;
    bool _scorpProfromActive = false;  // MM_PROFSCORP && service ROM paged at #0000

    // Magic-button DOS trigger gate (hardware-reference §9): cached from
    // UpdateModelBanks() so the release hook costs one bool test per read
    bool _scorpionDosTriggerActive = false;

    /// Bus-cycle side effects of the ProfROM silicon, applied before the read
    /// byte is served - see the definition in scorpionmemory.cpp
    void ApplyScorpionReadCycle(uint16_t addr, bool isExecution);
    /// endregion </Fields>
};
