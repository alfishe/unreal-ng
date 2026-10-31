#pragma once
#include "stdafx.h"

#include "emulator/io/sdcard/sdcardspi.h"
#include "emulator/io/spi/zcontrollerspi.h"
#include "emulator/media/mediaslot.h"
#include "emulator/ports/models/portdecoder_scorpion256.h"

/// Scorpion ZS-256 with the GMX card (Graphic Memory eXpander, MOA 1998) in its CPU socket.
///
/// The card brings its own Z80, 2 MB of RAM, a 512 KB flash of eight 64 KB ROM planes and a register file on top of
/// the host board: #xx00 (global configuration), #78FD (the #8000 page), #7AFD / #7CFD (the scroller), #7EFD (turbo,
/// ROM plane, the 640x200 mode, MagEnable) and #DFFD (RAM page bits 4-6). Everything else - keyboard, AY, Beta128,
/// SMUC, Kempston - is the host Scorpion, plus the Z-Controller SD card the 6.44 monitor drives (#57 / #77, media
/// slot "sd.zc"), so this decoder answers those ports and hands every other cycle to
/// PortDecoder_Scorpion256. The host Turbo+ logic is cut off by the fitting (its WAIT and CLOCK lines), so its IN
/// strobes and wait states do not apply here.
///
/// Design: docs/inprogress/2026-10-07-gmx/design.md sections 2.2-2.6 and 4; the 1998 manual in
/// docs/inprogress/2026-10-07-gmx/references/
class PortDecoder_ScorpionGMX : public PortDecoder_Scorpion256
{
    /// region <Constants>
public:
    /// #xx00 bits (manual section 2)
    static constexpr uint8_t P00_SCHEME_MASK = 0x07;  // CONF0-2: the FPGA scheme number
    static constexpr uint8_t P00_RECONF = 0x08;       // reload scheme CONF: the card resets the CPU
    static constexpr uint8_t P00_FIXROM = 0x10;       // freeze the ROM plane: RESET neither clears it nor reloads
    static constexpr uint8_t P00_BLKEXT = 0x20;       // the GMX register file off: a plain ZS-256 Turbo+
    static constexpr uint8_t P00_BLKEX2 = 0x40;       // with BLKEXT: all xxFD ports blocked (not built)
    static constexpr uint8_t P00_PENTAGON_INT = 0x80; // ST/PENT: 1 = INT at the start of frame sync, 320 lines

    /// #7EFD bits (manual section 2)
    static constexpr uint8_t P7EFD_EWR = 0x01;        // flash write enable (ignored: the flash is read-only)
    static constexpr uint8_t P7EFD_VPP = 0x02;        // flash Vpp (ignored)
    static constexpr uint8_t P7EFD_MAGIC_BLOCK = 0x04; // 1 = the Magic button ignored and the #7FFD lock off
    static constexpr uint8_t P7EFD_GFX = 0x08;        // the 640x200 extended video mode
    static constexpr uint8_t P7EFD_PLANE_MASK = 0x70; // ROM plane = flash A18-A16
    static constexpr uint8_t P7EFD_TURBO = 0x80;      // 7 MHz

    /// The magic byte a RECONF arms: #88 | scheme, read LSB first through IN #78FD bit 0
    static constexpr uint8_t MAGIC_BASE = 0x88;

    /// The frames #00 bit 7 selects (manual, Q13), on the same raster origin (paper at T 16152). ST: the Scorpion's
    /// 312 lines, INT at the end of frame sync, 14336 T before the paper. PENT: 320 lines, INT at the start of frame
    /// sync, 17988 T before the paper as on the Pentagon (its intstart 71635, paper at 17944)
    static constexpr uint32_t kScorpionFrame = 69888;      // 224 * 312
    static constexpr uint32_t kScorpionIntStart = 1815;    // INT fires at 1816
    static constexpr uint32_t kPentagonFrame = 71680;      // 224 * 320
    static constexpr uint32_t kPentagonIntStart = 69843;   // INT fires at 69844 = 16152 + 71680 - 17988
    static constexpr uint32_t kIntLength = 32;
    /// endregion </Constants>

    /// region <Constructors / Destructors>
public:
    PortDecoder_ScorpionGMX() = delete;
    explicit PortDecoder_ScorpionGMX(EmulatorContext* context);
    ~PortDecoder_ScorpionGMX() override;
    /// endregion </Constructors / Destructors>

    /// region <Interface methods>
public:
    /// RESET: every card latch to 0 except #00; the plane stays when #00 FIXROM holds it (manual section 2)
    void reset() override;
    /// Power-off clears #00 as well
    void PowerCycle() override;

    uint8_t DecodePortIn(uint16_t port, uint16_t pc) override;
    void DecodePortOut(uint16_t port, uint8_t value, uint16_t pc) override;

    void SetRAMPage(uint8_t page) override;

    /// The card's turbo: #7EFD bit 7 alone, no host Turbo+ waits or /INT stretches; the card's Even M1 wait in turbo
    /// (EVEN_M1_TURBO) and the frame #00 bit 7 selects. Called by reset and the TTD restores
    void SyncTurboWaits() override;
    /// Runs the CPU reset a RECONF write asked for, at the end of that instruction
    void OnMachineStep(uint32_t t) override;
    void OnMachineFrameRollover(uint32_t frameLength) override;

    /// A Spectrum / Scorpion state comes in: the Scorpion scheme the loader hands over to (ROM plane 4, #7EFD = #40)
    /// and the card's own pages off (#78FD, #DFFD), turbo off as after the hand-over
    void EnterSpectrum128Paging(uint16_t pc) override;

    /// The #7FFD lock (bit 5) does not act while #7EFD bit 2 is set (manual 3.1)
    bool IsPagingLocked() const override;

    /// The Magic button was pressed: false = blocked by #7EFD bit 2. Otherwise latches the read-backs until the
    /// next IN #FF (manual section 2)
    bool OnMagicButton();

    /// region <SD card (Z-Controller)>
    /// The media manager's slot "sd.zc": the manager owns the medium and applies the config ([MEDIA] sd.zc, legacy
    /// [ZC]). A Z80 reset keeps the card and its session writes; the controller deselects it
    SdCardSpi& GetSdCard() { return _sdCard; }
    ZControllerSpi& GetZController() { return _zc; }
    /// endregion </SD card (Z-Controller)>

    /// The host Scorpion's blobs plus GmxPaging (ttd::TTDGmxPaging) and the SD card's protocol state (EvoSdCard)
    std::vector<ttd::TTDDeviceType> GetTTDModelStateTypes() const override;
    std::vector<std::unique_ptr<ttd::TTDSerializable>> CreateTTDSerializers() const override;
    /// endregion </Interface methods>

    /// region <Register file>
public:
    /// The decode of the card's #xxFD file: Scorpion low-byte rule (A5 = 1, A2 = 1, A1 = 0, A0 = 1) plus the exact
    /// high byte. Returns the high byte for #78 / #7A / #7C / #7E / #DF, 0 otherwise
    static uint8_t GmxRegisterOf(uint16_t port);

    /// The values IN returns (manual section 2)
    uint8_t Read78FD();
    uint8_t Read7AFD() const;
    uint8_t Read7EFD() const;

    /// The ROM plane #7EFD selects (0-7)
    uint8_t Plane() const;
    /// RECONF requested and not run yet
    bool IsReconfigurationPending() const { return _reconfPending; }
    /// TTD restore: the pending RECONF and its step hook
    void RestoreReconfigurationPending(bool pending);
    /// endregion </Register file>

protected:
    void Out00(uint8_t value);
    void Out7EFD(uint8_t value);
    void ApplyTurbo();
    /// The frame length and INT position of #00 bit 7 (kScorpion* / kPentagon*)
    void SyncFrame();
    void RunReconfiguration();
    void RefreshStepHook();

    bool _reconfPending = false;

    // Z-Controller SD slot: the card outlives a reset
    class GmxSdSlot : public IMediaSlot
    {
    public:
        explicit GmxSdSlot(PortDecoder_ScorpionGMX& owner);
        const SlotDescriptor& Descriptor() const override { return _descriptor; }
        void Attach(Medium& medium) override;
        void Detach() override;
        bool IsBusy() const override;

    private:
        PortDecoder_ScorpionGMX& _owner;
        SlotDescriptor _descriptor;
    };

    // The bus analyzer's view of the Z-Controller bus: declared before the card and the controller, which point at it
    BusTap _spiTap{"zc.spi", BusProtocol::Spi, "Z-Controller SPI (#57 data, #77 chip select): SD card"};
    SdCardSpi _sdCard;
    ZControllerSpi _zc;
    GmxSdSlot _sdSlot{*this};
};
