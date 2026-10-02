#pragma once

/// @file vdac2card.h
/// @brief The TS-Labs VDAC2 card on the TS-Conf IDE connector: an FT812
/// graphics controller (Bridgetek EVE2) behind the board's SPI master.
///
/// Design: docs/inprogress/2026-10-01-tsconf-vdac2/vdac2-integration-design.md
/// ("D-C") §5; hardware: vdac2-tdd.md §2; the chip itself is the eve-emu
/// library (eve-emu-architecture.md), built only with ENABLE_VDAC2.
///
/// What the card is, electrically (vdac2-tdd.md §2.1-§2.2):
///   - The FT812's SPI port hangs on the Z-Controller SPI bus of the Evo
///     together with the SD card. Its chip select is config bit D2 of #77,
///     active high in the register (the FPGA drives ftcs_n = ~D2,
///     [V] zports.v:296-311); the FPGA muxes MISO to the FT812 while it is
///     selected ([V] top.v:1173-1178). So the card is slot 1 of the hub,
///     mask #04, active high (ZControllerSpi::AttachDevice).
///   - The FT812 runs from its own 8 MHz crystal (CLKEXT); its system clock
///     is that times the multiplier the guest programs (CLKSEL), 48-80 MHz in
///     the TS-Labs mode table. It is not tied to the Evo's clocks.
///   - The card's CPLD switches the whole monitor signal between the Evo
///     and the FT812 by msel = V_CONFIG bit 2 ([C] cpld/top.v, vdac2-tdd.md
///     §2.1), and routes INT_N to the Evo's line interrupt while msel = 1.
///
/// Picture (D-C §7). While the monitor shows the FT812 the card gives the chip
/// a frame buffer, converts each finished FT812 frame (ARGB8888) into the
/// emulator's RGBA8888 and hands it to the Screen as the external picture,
/// latched at the FT812's own frame end (its rate, not the machine's). While
/// the Evo is shown the chip draws nothing (all its timing still runs).
///
/// Time (D-C §5.2). TS-Conf time is the raster tact: 3.5 MHz, 71 680 per
/// frame, whatever the CPU turbo. The card keeps an absolute raster-tact
/// position (frame base + the tact inside the frame) and converts each
/// elapsed interval to FT812 system clocks exactly, carrying the remainder:
///   clocks = floor((tacts * f_sys + remainder) / 3 500 000)
/// so nothing drifts over a session. f_sys changes only through a bus access
/// (CLKSEL is accepted in SLEEP, behavior spec §2.2) and the card brings the
/// chip up to "now" before every access, so each interval is converted at
/// the frequency in force during it. While the chip's clock is stopped
/// (EveSystemClockHz = 0) time passes without clocks and the remainder is
/// dropped: the next clock edge after a restart has no relation to the old
/// phase.
///
/// The time inside the frame comes from the owner (PortDecoder_TSConf): the
/// TS-Conf engine's accounted position, which is the CPU's raster tact at a
/// port access and the DMA's position while a DMA_RAM_SPI transfer is being
/// accounted, so DMA bytes reach the chip at their own time. The frame base
/// advances in OnFrameEnd, which the engine calls once the old frame is
/// fully accounted.
///
/// Stepping. Every bus access first brings the chip to "now", which is exact
/// for everything the guest reads. The interrupt pin needs more: INT_N can
/// fall between two accesses (a swap, the coprocessor finishing). So the card
/// steps the chip from event to event (EveClocksToNextEvent) whenever it
/// advances, records each falling edge of INT_N with its raster tact, and
/// keeps the tact of the chip's next event: the TS-Conf interrupt controller
/// asks for edges after every instruction (TakeIntEdges), which costs one
/// comparison until that tact is reached (D-C §5.2, §6).
///
/// INT_N reaches the Evo only while the card shows the FT812 (msel = 1),
/// where it replaces the line interrupt; deciding that is the interrupt
/// controller's job (ITsConfLineSource). The card reports every edge.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "debugger/ttd/ttddisplayparticipant.h"
#include "emulator/io/spi/spidevice.h"
#include "emulator/platforms/tsconf/vdac2capture.h"
#include "emulator/platform.h"

class EmulatorContext;
class ModuleLogger;
struct EveChip;

class Vdac2Card : public SpiDevice, public ttd::ITTDDisplayParticipant
{
    /// region <ModuleLogger definitions for Module/Submodule>
protected:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_IO;
    const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_GENERIC;
    ModuleLogger* _logger = nullptr;
    /// endregion </ModuleLogger definitions for Module/Submodule>

public:
    /// The card's crystal on the FT812's X1 / X2 (vdac2-tdd.md §2.2)
    static constexpr uint32_t kCrystalHz = 8'000'000;
    /// TS-Conf raster tacts per second (tsconfinterrupts.h)
    static constexpr uint64_t kRasterHz = 3'500'000;
    /// TS-Conf raster tacts per frame (tsconfinterrupts.h: 224 x 320)
    static constexpr uint32_t kFrameTacts = 71'680;

    /// Where the card sits on the Z-Controller hub (zports.v:296-311)
    static constexpr uint8_t kSpiSlot = 1;
    static constexpr uint8_t kSpiSelectMask = 0x04;  // #77 D2
    static constexpr bool kSpiSelectActiveHigh = true;

    /// The FT81x ROM: 0x1E0000..0x2FFFFF (behavior spec §1)
    static constexpr size_t kRomImageSize = 0x300000 - 0x1E0000;

    /// The card's own time, plain integers for snapshots / TTD (D-C §9.1)
    struct Time
    {
        uint64_t frameBase = 0;   // absolute raster tact of the current frame's tact 0
        uint64_t position = 0;    // absolute raster tact the chip has been advanced to
        uint64_t remainder = 0;   // tacts x f_sys not yet converted (< kRasterHz)
        uint64_t nextEvent = 0;   // absolute raster tact of the chip's next event (UINT64_MAX: none)
        uint8_t intAsserted = 0;  // INT_N low at `position`
    };

    /// Falling INT_N edges not yet taken by the interrupt controller
    static constexpr size_t kMaxPendingEdges = 16;

    /// `rasterInFrame` returns the raster tact inside the current frame
    /// (0..kFrameTacts) the owner has reached
    Vdac2Card(EmulatorContext* context, std::function<uint32_t()> rasterInFrame);
    ~Vdac2Card() override;

    Vdac2Card(const Vdac2Card&) = delete;
    Vdac2Card& operator=(const Vdac2Card&) = delete;

    /// The library created the chip (it fails only without a decoder the
    /// build requires, which ENABLE_VDAC2 configures)
    bool IsReady() const { return _chip != nullptr; }
    /// The ROM image was found and loaded
    bool HasRom() const { return !_rom.empty(); }

    /// Power-on of the card: the FT812 comes up as after power is applied
    /// (sleep, internal oscillator), the time base restarts at the owner's
    /// current position
    void PowerOn();

    /// The Evo's reset button. The card's FT812 has its own power-on reset
    /// and the TS-Conf reset line does not reach the IDE connector's chip
    /// (TO VERIFY on the card's schematic: PD_N wiring), so the chip keeps
    /// running; the SDK's ft_init always starts with PWRDOWN / ACTIVE /
    /// RST_PULSE anyway. The bus side is deselected by the hub's reset
    void Reset() {}

    /// The engine closed its frame: advance to the frame's end, then move the
    /// frame base on
    void OnFrameEnd();

    /// Bring the chip up to the owner's current raster position
    void Synchronize();

    /// The monitor shows the FT812 (msel latched) or the Evo. The owner
    /// calls it at each machine frame end; a change switches the Screen's
    /// external picture on or off
    void SetShowing(bool showing);
    bool IsShowing() const { return _showing; }
    /// The latest FT812 picture (RGBA8888), its size; empty before the first frame
    const std::vector<uint8_t>& Picture() const { return _picture; }
    uint16_t PictureWidth() const { return _pictureWidth; }
    uint16_t PictureHeight() const { return _pictureHeight; }
    /// FT812 frames latched to the Screen since the card was fitted
    uint64_t LatchedFrames() const { return _latchedFrames; }

    /// region <Bus capture (.evr replay stream, vdac2-test-corpus.md §4)>
    /// Start writing everything on the FT812's bus to `path`. On a running
    /// chip the stream begins with the chip's whole state, so it replays
    /// from that point. Replaces a capture in progress
    bool StartCapture(const std::string& path, std::string* error = nullptr);
    /// Finish the stream (the end record, the file closed); false when none ran
    bool StopCapture();
    bool IsCapturing() const { return _capture.IsOpen(); }
    /// What the current or the last capture wrote
    Vdac2Capture::Stats GetCaptureStats() const { return _capture.GetStats(); }
    /// endregion

    /// Falling edges of INT_N up to raster tact `rasterInFrame` of the
    /// current frame, oldest first, as tacts of the current frame (an edge
    /// carried over from the frame before reads as 0). Advances the chip
    /// first when its next event is due. Taken edges are forgotten
    size_t TakeIntEdges(uint32_t rasterInFrame, uint32_t* out, size_t max);

    /// region <SpiDevice>
    void select(bool selected) override;
    uint8_t exchange(uint8_t mosi) override;
    /// endregion

    /// region <TTD (line-budget-metrics.md §3.4)>
    /// Card state: the card's time, the INT edges not yet taken, what the monitor
    /// shows, and the chip's control state (EveSaveState, the metrics block in it).
    /// Fixed size for the card's lifetime
    size_t TtdStateSize() const;
    void TtdSaveState(uint8_t* dst) const;
    /// False when the blob is not this card's (version, or the chip refused its state)
    bool TtdLoadState(const uint8_t* src);
    /// The chip's memory regions (RAM_G, both display lists, REG, CMD, SPECIAL,
    /// INFLIGHT) in region order, zero runs of 64+ bytes dropped (format in
    /// vdac2card.cpp, vdac2-integration-design.md §9.1). TtdMemorySize: the
    /// worst case; TtdSaveMemory returns the bytes written (a variable-size TTD
    /// blob). Restored before the card state
    size_t TtdMemorySize() const;
    size_t TtdSaveMemory(uint8_t* dst) const;
    /// False when the blob is not a valid memory blob (the regions may be partly written)
    bool TtdLoadMemory(const uint8_t* src);
    /// Hashes of what each blob holds (divergence checks)
    uint64_t TtdStateHash() const;
    uint64_t TtdMemoryHash() const;

    /// ITTDDisplayParticipant: one machine frame before a frame target (an FT812
    /// frame, ~16.9 ms, may have started before the target frame's checkpoint);
    /// inside a frame the picture is the FT812 frame drawn up to the position
    unsigned TTDLeadInFrames() const override { return 1; }
    void TTDPrepareComposedPicture(bool frameTarget) override;
    /// endregion

    const Time& GetTime() const { return _time; }
    /// The chip, for the inspection API of eve/eve.h (nullptr if not ready)
    EveChip* Chip() const { return _chip; }

private:
    void LoadRom();
    void AdvanceTo(uint64_t absoluteRaster);
    /// Look at INT_N after the chip moved or was accessed at tact `at`
    void SampleInt(uint64_t at);
    /// The tact of the chip's next event, from the current position
    void PlanNextEvent();
    /// The chip finished a frame: present it while showing
    void OnChipFrame();
    /// Size the buffers for the chip's current mode and give it the output
    /// (drawing only while showing); true when the picture size changed
    bool ConfigureOutput();
    /// Tell the Screen what the monitor shows now (the FT812 picture or the Evo)
    void PublishPicture();
    /// The chip should draw its next frame
    bool Drawing() const;
    /// The chip's frame buffer (ARGB8888) into the presented picture (RGBA8888)
    void ConvertFrameToPicture();
    uint64_t Now() const { return _time.frameBase + _rasterInFrame(); }

    EmulatorContext* _context;
    std::function<uint32_t()> _rasterInFrame;
    std::vector<uint8_t> _rom;
    EveChip* _chip = nullptr;
    Time _time;
    uint64_t _edges[kMaxPendingEdges] = {};
    size_t _edgeCount = 0;

    // Picture: the chip draws ARGB8888 into _chipFrame; _picture is the RGBA
    // copy the Screen presents
    bool _showing = false;
    bool _drawing = false;         // the chip was told to draw (EveSetOutput)
    uint64_t _chipFrames = 0;      // EveCompletedFrames seen last
    uint64_t _latchedFrames = 0;
    std::vector<uint32_t> _chipFrame;
    std::vector<uint8_t> _picture;
    uint16_t _pictureWidth = 0;
    uint16_t _pictureHeight = 0;

    // [VDAC2] CaptureFile: the bus traffic as an .evr replay stream; while
    // capturing, the chip draws every frame so the stream carries frame hashes
    Vdac2Capture _capture;
};
