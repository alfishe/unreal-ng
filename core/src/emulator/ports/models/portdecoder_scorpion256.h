#pragma once
#include "stdafx.h"

#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/rtc/smucnvram.h"
#include "emulator/memory/memory.h"
#include "emulator/memory/scorpion/scorpionturbooverlay.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/video/screen.h"

///
/// See: https://worldofspectrum.org/faq/reference/128kreference.htm
/// See: https://zx-pk.ru/threads/11490-paging-ports-of-zx-clones.html?langid=1
/// See: http://zx.clan.su/forum/11-46-1
class PortDecoder_Scorpion256 : public PortDecoder, public IMachineStepHook
{
    /// region <Fields>
protected:

    // SMUC (Scorpion & MOA Universal Controller) stub for the ProfROM boot
    // probes (profrom-smuc-not-found-and-driver-disassembly.md section 8):
    // 2 KB serial-link EEPROM with the DS1685 RTC behind the same NVRAM chip,
    // plus an IDE window register file
    SMUCNvram _smucNvram;
    uint8_t _smucIdeRegs[8] = {};

    // SMUC board presence. Absent by default: with no board physically on the
    // bus the whole #xxBA/#xxBE family floats (reads open-bus #FF, writes hit
    // no latch), the ProfROM presence polls fail fast and the boot skips the
    // NVRAM / RTC / IDE init chains. The stubs stay wired behind the flag for
    // verification tests (profrom-smuc-not-found-and-driver-disassembly.md, 8)
    bool _smucEnabled = false;

    // Saved #7FFD state prior to entering Shadow Monitor via #1FFD bit 1
    uint8_t _savedP7FFD = 0x00;
    bool _savedP7FFDValid = false;

    // Turbo+ wait states at 7 MHz (ScorpionTurboOverlay): created on the first switch to turbo, installed on the
    // host bus while turbo is on (SyncTurboWaits)
    std::unique_ptr<ScorpionTurboOverlay> _turboOverlay;
    bool _turboWaitsInstalled = false;
    /// endregion </Fields>

    /// region <Constructors / Destructors>
public:
    PortDecoder_Scorpion256() = delete;                 // Disable default constructor; C++ 11 feature
    PortDecoder_Scorpion256(EmulatorContext* context);
    virtual ~PortDecoder_Scorpion256();
    /// endregion </Constructors / Destructors>

    /// region <Interface methods>
public:
    void reset() override;
    uint8_t DecodePortIn(uint16_t port, uint16_t pc) override;
    void DecodePortOut(uint16_t port, uint8_t value, uint16_t pc) override;

    /// #FF1F answers Joystick::Read()
    bool HasKempstonJoystick() const override { return true; }

    void SetRAMPage(uint8_t oage) override;
    void SetROMPage(uint8_t page) override;

    /// Install the turbo wait-state overlay while turbo is on, remove it otherwise (the turbo switch, reset, a
    /// TTD restore, whose chipset copy sets the clock without the decoder)
    void SyncTurboWaits();

    /// Turbo+: while /INT is active the logic chip runs the CPU at 3.5 MHz (its TRB register only follows the
    /// turbo latch while INT1 is high), so the interrupt acknowledge and the start of the handler run at normal
    /// speed with the normal rules (Even M1 with SC15.1). Installed while the turbo latch is on; switches at
    /// instruction boundaries (research-scorpion-turbo.md section 2.3). It runs only from the frame start until
    /// the pulse is over (OnMachineFrameRollover turns it on, OnMachineStep off): the rest of the frame pays nothing
    void OnMachineStep(uint32_t t) override;
    void OnMachineFrameRollover(uint32_t frameLength) override;
    bool AreTurboWaitsInstalled() const { return _turboWaitsInstalled; }
    /// endregion </Interface methods>

    /// region <Helper methods>
public:
    bool IsPort_FE(uint16_t port);
    bool IsPort_7FFD(uint16_t port);
    bool IsPort_1FFD(uint16_t port);
    bool IsPort_7EFD(uint16_t port);  // ProfROM window latch (#7FFD pattern with A8 low)
    bool IsPort_SMUC(uint16_t port);  // SMUC board (#xxBA/#xxBE family)
    bool IsPort_KempstonJoystick(uint16_t port);  // Kempston Joystick (#FF1F)
    bool IsPort_KempstonMouse(uint16_t port, uint8_t& outRegister) const override;     // Kempston Mouse (standard decode, A5-A0=#1F A9=1)
    bool ScorpionTrDosSelected() const;                                 // TR-DOS session or armed DOS trigger

    /// region <TTD model-specific state>
    /// #1FFD, the DD50.1 trigger and (PROFSCORP) the ProfROM plane / #7EFD latch:
    /// paging inputs the model-agnostic TTDChipsetState does not carry, on both
    /// Scorpion variants. Declared here because this decoder owns
    /// those latches (see PortDecoder::GetTTDModelStateIds).
    std::vector<ttd::PeripheralId> GetTTDModelStateIds() const override;

    /// Turbo+ runs the CPU at 3.5 or 7 MHz
    uint8_t TtdClockUnits() const override { return 2; }
    std::vector<std::unique_ptr<ttd::TTDSerializable>> CreateTTDSerializers() const override;
    /// endregion </TTD model-specific state>

    /// SMUC EEPROM backing store (verification tests / debug UI)
    SMUCNvram& GetSMUCNvram() { return _smucNvram; }
    /// The clock chip (tests, debug UI; every RTC machine has GetRtc())
    Ds12887& GetRtc() { return _smucNvram.GetRtc(); }
    RtcBinding GetRtcBinding() override;

    /// SMUC board presence (absent by default - see _smucEnabled)
    void SetSmucEnabled(bool enabled) { _smucEnabled = enabled; }
    bool IsSmucEnabled() const { return _smucEnabled; }
    /// The board is on the bus: enabled by hand (tests), or the machine's IDE scheme is SMUC
    bool IsSmucFitted() const { return _smucEnabled || _ide.Scheme() == IDE_SMUC; }
    /// endregion </Helper methods>

protected:
    void Port_7FFD(uint8_t value, uint16_t pc);
    void Port_1FFD(uint8_t value, uint16_t pc);
    uint8_t ReadSMUCPort(uint16_t port);
    void WriteSMUCPort(uint16_t port, uint8_t value);
};
