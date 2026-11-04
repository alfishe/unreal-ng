#pragma once
#include "stdafx.h"

#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/pic/smucinterrupts.h"
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
    // The SMUC's 8259 and its interrupt path (RTC /IRQ on IR0, INT through #FFBA D3); on the CPU's daisy chain
    // while the board is fitted
    std::unique_ptr<SmucInterrupts> _smucInterrupts;
    // The card in the SMUC's ISA-8 slot ([SMUC] Isa; NetworkManager builds it and plugs it through DescribeNetwork)
    IIoBusDevice* _smucIsaCard = nullptr;
    bool _nvramLoaded = false;   // [SMUC] NvramFile read once, on the first reset with the board fitted
    static bool s_nvramPersistence;

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

    // The host Turbo+ logic drives the CPU: its IN strobes switch turbo, its waits and /INT stretch apply. False on
    // the GMX, whose fitting cuts the host WAIT and CLOCK lines (PortDecoder_ScorpionGMX)
    bool _turboPlusHost = true;
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
    bool BoardHasKempstonJoystick() const override { return true; }

    /// The Turbo+ flip-flop (scorpion.turbo) is the clock select a disk autostart or a snapshot load keeps over its
    /// quick reset; 0 on the GMX, whose fitting cuts the host clock (its turbo is #7EFD bit 7, set by its loader)
    uint16_t GetClockSelect() const override;
    void RestoreClockSelect(uint16_t select) override;

    void SetRAMPage(uint8_t page) override;
    void SetROMPage(uint8_t page) override;

    /// Install the turbo wait-state overlay while turbo is on, remove it otherwise (the turbo switch, reset, a
    /// TTD restore, whose chipset copy sets the clock without the decoder)
    virtual void SyncTurboWaits();

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
    bool IsPort_PrinterCovox(uint16_t port) const;  // the Covox on the printer port (#DD, CovoxDD=1; Covox::PrinterDecodeOf)
    bool IsPort_KempstonMouse(uint16_t port, uint8_t& outRegister) const override;     // Kempston Mouse (standard decode, A5-A0=#1F A9=1)
    bool ScorpionTrDosSelected() const;                                 // TR-DOS session or armed DOS trigger
    /// The SMUC's ISA window (#18E6-#7FFE, SMUC hardware reference §5.1): the ISA address a port reaches when the
    /// board is fitted, a card is in the slot, the DOS ports are on and the card decodes it; false otherwise
    bool SmucIsaAddress(uint16_t port, uint16_t& offset) const;
    /// The machine's expansion slots: the SMUC's ISA slot while the board is fitted
    NetworkCapabilities DescribeNetwork() override;

    /// region <TTD model-specific state>
    /// #1FFD, the DD50.1 trigger and (PROFSCORP) the ProfROM plane / #7EFD latch:
    /// paging inputs the model-agnostic TTDChipsetState does not carry, on both
    /// Scorpion variants. Declared here because this decoder owns
    /// those latches (see PortDecoder::GetTTDModelStateTypes).
    std::vector<ttd::TTDDeviceType> GetTTDModelStateTypes() const override;

    /// Turbo+ runs the CPU at 3.5 or 7 MHz
    uint8_t TtdClockUnits() const override { return 2; }
    std::vector<std::unique_ptr<ttd::TTDSerializable>> CreateTTDSerializers() const override;
    void CollectTTDRegionSources(std::vector<ttd::ITTDRegionSource*>& out) override { out.push_back(&_smucNvram); }
    /// endregion </TTD model-specific state>

    /// [SMUC] NvramFile as a path, one per firmware: the ROM file's name joins the stem ("gmx-smuc-nvram-gmx644.bin").
    /// A bare name is in the settings folder (FileHelper::GetWritablePath); empty = none or persistence off
    std::string SmucNvramPath() const;
    /// [SMUC] NvramFile as written, without the ROM name (the image all firmwares shared before)
    std::string SmucNvramBasePath() const;
    /// The model's ROM file name without folder and extension
    std::string SmucNvramRomName() const;
    /// [SMUC] NvramFile into the board, once, while it is fitted (the first reset or the first SMUC access)
    void LoadSmucNvram();
    /// Tests: a GMX boot must not depend on what an earlier test's machine saved (core-tests main turns it off)
    static void SetNvramPersistenceAllowed(bool allowed) { s_nvramPersistence = allowed; }

    /// SMUC EEPROM backing store (verification tests / debug UI)
    SMUCNvram& GetSMUCNvram() { return _smucNvram; }
    /// The SMUC IDE window register file (8 registers; TTD: TTDSmuc)
    uint8_t* GetSmucIdeRegs() { return _smucIdeRegs; }
    /// The clock chip (tests, debug UI; every RTC machine has GetRtc())
    Ds12887& GetRtc() { return _smucNvram.GetRtc(); }
    RtcBinding GetRtcBinding() override;

    /// SMUC board presence (absent by default - see _smucEnabled)
    void SetSmucEnabled(bool enabled) { _smucEnabled = enabled; }
    bool IsSmucEnabled() const { return _smucEnabled; }
    /// The board is on the bus: enabled by hand (tests), or the machine's IDE scheme is SMUC
    bool IsSmucFitted() const { return _smucEnabled || _ide.Scheme() == IDE_SMUC; }
    SmucInterrupts& GetSmucInterrupts() { return *_smucInterrupts; }
    const SmucInterrupts& GetSmucInterrupts() const { return *_smucInterrupts; }
    /// endregion </Helper methods>

protected:
    void Port_7FFD(uint8_t value, uint16_t pc);
    void Port_1FFD(uint8_t value, uint16_t pc);
    uint8_t ReadSMUCPort(uint16_t port);
    void WriteSMUCPort(uint16_t port, uint8_t value);
};
