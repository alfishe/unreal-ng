#pragma once

/// @file evehostcard.h
/// @brief An FT812-compatible chip (the eve-emu library) on the TS-Conf
/// Z-Controller SPI hub: time conversion, event stepping, INT_N edges, the
/// picture latched at the chip's own frame rate, bus capture and TTD.
///
/// The boards differ only in where the chip comes from and how it is wired:
/// Vdac2Card (a real FT812 on the VDAC2 card) and the VDAC4 Tang FPGA core.
/// Design: docs/inprogress/2026-10-01-tsconf-vdac2/vdac2-integration-design.md
/// ("D-C") §5-§9; docs/inprogress/2026-11-15-tsconf-vdac4/design.md §5.1.
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

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "debugger/ttd/ttddisplayparticipant.h"
#include "emulator/io/spi/spidevice.h"
#include "emulator/platforms/tsconf/tsconfhistory.h"
#include "emulator/platforms/tsconf/vdac2capture.h"

namespace ttd
{
struct TTDTimeField;
}
#include "emulator/platforms/tsconf/vdac2control.h"
#include "emulator/platform.h"

class EmulatorContext;
class ModuleLogger;
struct EveChip;

class EveHostCard : public SpiDevice, public ttd::ITTDDisplayParticipant
{
    /// region <ModuleLogger definitions for Module/Submodule>
protected:
    const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_IO;
    const uint16_t _SUBMODULE = PlatformIOSubmodulesEnum::SUBMODULE_IO_GENERIC;
    ModuleLogger* _logger = nullptr;
    /// endregion </ModuleLogger definitions for Module/Submodule>

public:
    /// TS-Conf raster tacts per second (tsconfinterrupts.h)
    static constexpr uint64_t kRasterHz = 3'500'000;
    /// TS-Conf raster tacts per frame (tsconfinterrupts.h: 224 x 320)
    static constexpr uint32_t kFrameTacts = 71'680;

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

    /// What differs between the boards that host an FT812-compatible chip
    struct Params
    {
        const char* name = "EVE";                ///< log prefix ("VDAC2")
        const char* historyId = "eve";           ///< device history id (tsconfhistory.h)
        const char* historyLabel = "FT812 SPI transactions";
        uint32_t crystalHz = 8'000'000;          ///< the clock on the chip's X1 / X2 (CLKEXT)
        std::string romPath;                     ///< FT81x ROM image ("" = ROM reads as zeros)
        std::string capturePath;                 ///< .evr capture from power-on ("" = none)
        uint32_t lineBudgetMargin = 10;          ///< percent, 0..50
        uint32_t model = 0;                      ///< EveModel (0 = EVE_MODEL_FT812)
    };

    /// `rasterInFrame` returns the raster tact inside the current frame
    /// (0..kFrameTacts) the owner has reached
    EveHostCard(EmulatorContext* context, std::function<uint32_t()> rasterInFrame, Params params);
    ~EveHostCard() override;

    EveHostCard(const EveHostCard&) = delete;
    EveHostCard& operator=(const EveHostCard&) = delete;

    /// The library created the chip (it fails only without a decoder the
    /// build requires, which ENABLE_VDAC2 configures)
    bool IsReady() const { return _chip != nullptr; }
    /// The ROM image was found and loaded
    bool HasRom() const { return !_rom.empty(); }

    /// Power-on of the card: the FT812 comes up as after power is applied
    /// (sleep, internal oscillator), the time base restarts at the owner's
    /// current position
    virtual void PowerOn();

    /// The Evo's reset button (nothing by default: the chip has its own reset)
    virtual void Reset() {}

    /// The engine closed its frame: advance to the frame's end, then move the
    /// frame base on
    virtual void OnFrameEnd();

    /// Bring the chip up to the owner's current raster position
    void Synchronize();

    /// The monitor shows the FT812 (msel latched) or the Evo. The owner
    /// calls it at each machine frame end; a change switches the Screen's
    /// external picture on or off
    virtual void SetShowing(bool showing);
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
    /// The bus capture writes a host file: not while time travel replays
    /// history (FR-20; that traffic was captured when it happened)
    bool CaptureLive() const;
    /// endregion

    /// region <Line budget metrics (line-budget-metrics.md, line-budget-model.md)>
    /// The soft budget: percent below the line period (0..50; [VDAC2] LineBudgetMargin).
    /// Any thread; frames finished from the next one on use it
    void SetLineBudgetMargin(uint32_t percent);
    uint32_t LineBudgetMargin() const { return _lineBudgetMargin.load(); }
    /// Draw (and so measure) every FT812 frame, also while the monitor shows the
    /// Evo or turbo skips frames. Off: only shown, drawn frames have metrics.
    /// Nothing the guest sees depends on it; it costs the drawing time. Any
    /// thread; takes effect at the card's next advance
    void SetMeasureAlways(bool on);
    bool MeasureAlways() const { return _measureAlways.load(); }
    /// The metrics now: the last finished frame's block, its lines if asked, and
    /// the frame in flight if asked (that one brings the chip up to the machine's
    /// position: emulation thread or a stopped machine only)
    void ReadFrameMetrics(Vdac2Control::FrameMetrics& out, bool withLines, bool inFlight);
    /// The metrics of the picture the monitor shows now (line-budget-metrics.md
    /// §3.3): kept when the card publishes a picture - an FT812 frame latched to
    /// the Screen (while measure-always is on), a TTD-composed picture (a frame
    /// target: the frame that finished last; a moment inside a frame: plus the
    /// lines of the frame in flight drawn so far). False while the monitor shows
    /// the Evo or before the first one. Any thread
    bool PresentedFrameMetrics(Vdac2Control::FrameMetrics& out) const;
    /// endregion

    /// Falling edges of INT_N up to raster tact `rasterInFrame` of the
    /// current frame, oldest first, as tacts of the current frame (an edge
    /// carried over from the frame before reads as 0). Advances the chip
    /// first when its next event is due. Taken edges are forgotten
    virtual size_t TakeIntEdges(uint32_t rasterInFrame, uint32_t* out, size_t max);

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
    /// The blob's absolute raster times (frameBase, position, nextEvent): time fields of the card's TTD state
    static std::vector<ttd::TTDTimeField> TtdTimeFields();
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
    /// The memory blob's 8-byte header with no tokens: the blob when the TTD
    /// engine keeps the regions themselves (TtdLoadMemory refuses it, leaving
    /// the memory to the engine's restore)
    static void TtdSaveMemoryHeader(uint8_t* dst);
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

protected:
    /// The log prefix and the board parameters
    const Params& GetParams() const { return _params; }
    /// The owner's current position, absolute raster tacts
    uint64_t Now() const { return _time.frameBase + _rasterInFrame(); }
    /// PowerOn as of an earlier tact `at` (<= Now()): the chip runs from there on the next access or Synchronize
    void PowerOnAt(uint64_t at);
    EmulatorContext* Context() const { return _context; }

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

    EmulatorContext* _context;
    Params _params;
    std::function<uint32_t()> _rasterInFrame;
    /// The FT812 SPI transactions (tsconfhistory.h): the one in progress goes in at its deselect
    Vdac2History _history;
    Vdac2Record _transaction;
    uint32_t _transactionBytes = 0;   ///< bytes of the open transaction (0: none open)
    void HistoryByte(uint8_t mosi, uint8_t miso);
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

    // Set from any thread (automation, the debugger window); the emulation thread
    // applies them: the margin at the next FT812 frame end, the drawing switch at
    // the next advance
    std::atomic<uint32_t> _lineBudgetMargin{10};
    std::atomic<bool> _measureAlways{false};

    /// What PresentedFrameMetrics answers; written on the emulation thread
    void KeepPresentedMetrics(bool withInFlight);
    mutable std::mutex _presentedLock;
    Vdac2Control::FrameMetrics _presented;
    bool _presentedKnown = false;
};
