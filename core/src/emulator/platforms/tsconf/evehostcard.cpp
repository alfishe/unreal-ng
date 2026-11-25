#include "stdafx.h"

#include "evehostcard.h"

#include "debugger/ttd/ttdserializable.h"

// The chip exists only in builds with the FT812 library (CMake ENABLE_VDAC2);
// without it a configuration that needs one is refused at load (config.cpp)
#ifdef ENABLE_VDAC2

#include <eve/eve.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <type_traits>

#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "emulator/cpu/core.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/screen.h"

namespace
{

/// Longest interval converted in one multiplication: a minute of raster
/// tacts x the fastest FT812 clock (80 MHz) stays far inside 64 bits
constexpr uint64_t kMaxChunkTacts = EveHostCard::kRasterHz * 60;

/// Resolve a ROM path the way ROM::LoadROM and the GS firmware do: as given
/// (working directory), then next to the executable, then in the resources
/// folder (macOS bundles). Empty when none exists
std::string ResolveRomPath(const std::string& path)
{
    const std::string normalized = FileHelper::NormalizePath(path);
    if (FileHelper::FileExists(normalized))
        return normalized;
    const std::string nextToExecutable = FileHelper::PathCombine(FileHelper::GetExecutablePath(), normalized);
    if (FileHelper::FileExists(nextToExecutable))
        return nextToExecutable;
    const std::string inResources = FileHelper::PathCombine(FileHelper::GetResourcesPath(), path);
    if (FileHelper::FileExists(inResources))
        return inResources;
    return {};
}

} // namespace

EveHostCard::EveHostCard(EmulatorContext* context, std::function<uint32_t()> rasterInFrame, Params params)
    : _context(context), _params(std::move(params)), _rasterInFrame(std::move(rasterInFrame)),
      _history(context ? &context->histories : nullptr, std::string(_params.historyId), _params.historyLabel, 4096)
{
    _logger = _context ? _context->pModuleLogger : nullptr;

    LoadRom();

    EveConfig config{};
    config.structSize = sizeof(EveConfig);
    config.model = static_cast<EveModel>(_params.model);
    config.externalClockHz = _params.crystalHz;
    config.romImage = _rom.empty() ? nullptr : _rom.data();
    config.romImageSize = _rom.size();
    config.decoders = nullptr;  // the library's built-in decoders (architecture A3)
    _chip = EveCreate(&config);
    if (!_chip)
        MLOGERROR("%s: the FT812 could not be created: %s", _params.name, EveLastError());
    else
    {
        _chipFrames = EveCompletedFrames(_chip);
        SetLineBudgetMargin(_params.lineBudgetMargin);
        EveSetLineBudgetMargin(_chip, _lineBudgetMargin.load());
        if (!_params.capturePath.empty())
        {
            if (_capture.Open(_params.capturePath, _params.crystalHz, _rom))
                MLOGINFO("%s: capturing the FT812 bus to '%s'", _params.name, _params.capturePath.c_str());
            else
                MLOGWARNING("%s: cannot write the capture file '%s'", _params.name, _params.capturePath.c_str());
        }
        ConfigureOutput();
    }

    _time.position = Now();
    PlanNextEvent();

    if (_context && _chip)
        _context->pTtdDisplayParticipant = this;
}

EveHostCard::~EveHostCard()
{
    if (_context && _context->pTtdDisplayParticipant == this)
        _context->pTtdDisplayParticipant = nullptr;
    // The Screen must not keep a pointer into _picture (at machine teardown
    // the Screen is gone first and pScreen is already null)
    if (_showing && _context && _context->pScreen)
        _context->pScreen->ClearExternalPicture();
    if (_chip)
    {
        EveSetAsyncDrawing(_chip, 0, nullptr, nullptr);  // the renderer presents into this card
        _capture.Close(EveTotalClocks(_chip));
        EveDestroy(_chip);
    }
}

void EveHostCard::LoadRom()
{
    // The ROM holds the FT812's fonts 16-34 (CMD_TEXT, the TEXT8X8 / TEXTVGA
    // formats). It is Bridgetek's and never shipped: the user extracts it
    // (tools/machines/tsconf/vdac2/). Without it the chip works and the ROM
    // reads as zeros, i.e. ROM text is blank (D-C §10)
    const char* configured = _params.romPath.c_str();
    if (configured[0] == '\0')
        return;

    const std::string path = ResolveRomPath(configured);
    if (path.empty())
    {
        MLOGINFO("%s: no FT81x ROM image '%s' (RomImage) - the FT812's ROM fonts read as zeros", _params.name, configured);
        return;
    }

    FILE* file = FileHelper::OpenFile(path, "rb");
    if (!file)
    {
        MLOGWARNING("%s: cannot open the FT81x ROM image '%s' - the ROM fonts read as zeros", _params.name,
                    FileHelper::PrintablePath(path).c_str());
        return;
    }
    std::vector<uint8_t> image(kRomImageSize);
    const size_t size = std::fread(image.data(), 1, image.size(), file);
    const bool larger = size == image.size() && std::fgetc(file) != EOF;
    std::fclose(file);

    // A shorter image is the end of the ROM range (eve.h: EveConfig.romImage);
    // a longer one is not an FT81x ROM
    if (size == 0 || larger)
    {
        MLOGWARNING("%s: '%s' is not an FT81x ROM image (1..%zu bytes expected) - the ROM fonts read as zeros",
                    _params.name, FileHelper::PrintablePath(path).c_str(), kRomImageSize);
        return;
    }
    image.resize(size);
    _rom = std::move(image);
}

void EveHostCard::PowerOn()
{
    PowerOnAt(Now());
}

void EveHostCard::PowerOnAt(uint64_t at)
{
    if (_chip)
    {
        EveReset(_chip);
        _capture.PowerOn(EveTotalClocks(_chip));
        _chipFrames = EveCompletedFrames(_chip);
        if (ConfigureOutput())
            PublishPicture();
    }
    _time.position = at;
    _time.remainder = 0;
    _time.intAsserted = 0;
    _edgeCount = 0;
    PlanNextEvent();
}

void EveHostCard::OnFrameEnd()
{
    AdvanceTo(_time.frameBase + kFrameTacts);
    _time.frameBase += kFrameTacts;
    // The FT812 frames finished by now are on the Screen at the machine's frame end, as with the drawing on
    // this thread (a screenshot after a pause, a test's picture). The lines of the FT812 frame in flight are
    // drawn on meanwhile: the next machine frame runs while the renderer finishes them
    if (_async)
        EveFramesBarrier(_chip);
}

void EveHostCard::Synchronize()
{
    AdvanceTo(Now());
}

void EveHostCard::AdvanceTo(uint64_t absoluteRaster)
{
    if (absoluteRaster <= _time.position)
    {
        // Never backwards: the owner's position only moves on (a replayed
        // catch-up asks for the same tact again)
        return;
    }
    if (!_chip)
    {
        _time.position = absoluteRaster;
        return;
    }
    // Drawing() changed: a TTD replay started or ended, a capture, measure-always
    if (_drawing != Drawing())
        ConfigureOutput();
    if (_async != AsyncWanted())
        ConfigureAsync();
    if (_async)
        StampNextFrame();

    while (_time.position < absoluteRaster)
    {
        const uint64_t start = _time.position;
        const uint64_t chunk = std::min(absoluteRaster - start, kMaxChunkTacts);
        const uint64_t hz = EveSystemClockHz(_chip);
        _time.position = start + chunk;
        if (hz == 0)
        {
            // Clock stopped (sleep, power-down): no edges, no phase to carry
            _time.remainder = 0;
            continue;
        }

        // The clocks this interval holds, and the remainder it leaves
        const uint64_t startRemainder = _time.remainder;
        const uint64_t scaled = chunk * hz + startRemainder;
        _time.remainder = scaled % kRasterHz;
        const uint64_t due = scaled / kRasterHz;

        // Step from chip event to chip event so every INT_N change gets its
        // own tact: the first tact by which `done` clocks have elapsed is
        // ceil((done x 3.5 MHz - startRemainder) / f_sys) after `start`
        uint64_t done = 0;
        while (done < due)
        {
            // An event due "now" (0) still moves one clock: the loop always progresses
            const uint64_t step = std::max<uint64_t>(1, std::min(EveClocksToNextEvent(_chip), due - done));
            EveAdvance(_chip, step);
            done += step;
            if (EveCompletedFrames(_chip) != _chipFrames)
            {
                OnChipFrame();
                if (_async)
                    StampNextFrame();  // a later step of this advance may end the next one
            }
            const uint64_t need = done * kRasterHz;
            const uint64_t tacts = need > startRemainder ? (need - startRemainder + hz - 1) / hz : 0;
            SampleInt(start + tacts);
        }
    }
    PlanNextEvent();
}

void EveHostCard::SampleInt(uint64_t at)
{
    const uint8_t asserted = EveIntAsserted(_chip) ? 1 : 0;
    if (asserted && !_time.intAsserted)
    {
        // A falling edge of INT_N. The FPGA samples it through two fclk
        // flip-flops ([V] top.v:468-471): seen within the same raster tact
        if (_edgeCount < kMaxPendingEdges)
            _edges[_edgeCount++] = at;
        else
            _edges[kMaxPendingEdges - 1] = at;  // the latch is one bit: the newest edge is the one that counts
    }
    _time.intAsserted = asserted;
}

void EveHostCard::PlanNextEvent()
{
    const uint64_t hz = _chip ? EveSystemClockHz(_chip) : 0;
    if (hz == 0)
    {
        _time.nextEvent = UINT64_MAX;
        return;
    }
    // Further than a minute away: look again in a minute (keeps the
    // multiplication inside 64 bits)
    const uint64_t clocks = std::min(EveClocksToNextEvent(_chip), hz * 60);
    const uint64_t need = clocks * kRasterHz;
    const uint64_t tacts = need > _time.remainder ? (need - _time.remainder + hz - 1) / hz : 0;
    _time.nextEvent = _time.position + tacts;
}

size_t EveHostCard::TakeIntEdges(uint32_t rasterInFrame, uint32_t* out, size_t max)
{
    const uint64_t target = _time.frameBase + rasterInFrame;
    if (target >= _time.nextEvent)
        AdvanceTo(target);
    if (_edgeCount == 0)
        return 0;

    size_t taken = 0;
    size_t kept = 0;
    for (size_t i = 0; i < _edgeCount; i++)
    {
        const uint64_t edge = _edges[i];
        if (edge <= target && taken < max)
            out[taken++] = edge > _time.frameBase ? static_cast<uint32_t>(edge - _time.frameBase) : 0;
        else
            _edges[kept++] = edge;
    }
    _edgeCount = kept;
    return taken;
}

void EveHostCard::SetShowing(bool showing)
{
    if (showing == _showing || !_chip)
        return;
    _showing = showing;
    ConfigureOutput();
    PublishPicture();
}

void EveHostCard::PublishPicture()
{
    Screen* screen = _context ? _context->pScreen : nullptr;
    if (!screen)
        return;
    if (!_showing)
    {
        screen->ClearExternalPicture();
        return;
    }
    EveTiming timing{};
    EveGetTiming(_chip, &timing);
    const uint64_t hz = EveSystemClockHz(_chip);
    const uint32_t periodUs = hz ? static_cast<uint32_t>(timing.framePeriodClocks * 1'000'000 / hz) : 0;
    screen->SetExternalPicture(_picture.data(), _pictureWidth, _pictureHeight, periodUs);
}

bool EveHostCard::ConfigureOutput()
{
    EveTiming timing{};
    EveGetTiming(_chip, &timing);
    // The visible picture is HSIZE x VSIZE (behavior spec §4); an unset mode
    // still gets a 1 x 1 buffer so the Screen always has a picture
    const uint16_t width = std::max<uint16_t>(1, timing.hsize);
    const uint16_t height = std::max<uint16_t>(1, timing.vsize);
    const bool resized = width != _pictureWidth || height != _pictureHeight || _picture.empty();
    if (resized)
    {
        EveDrawingBarrier(_chip);  // the renderer may still draw into the old buffers
        _pictureWidth = width;
        _pictureHeight = height;
        const size_t pixels = static_cast<size_t>(width) * height;
        _chipFrame.assign(pixels, 0xFF000000u);
        _picture.assign(pixels * 4, 0);
        for (size_t i = 0; i < pixels; i++)
            _picture[i * 4 + 3] = 0xFF;  // opaque black
    }
    _drawing = Drawing();
    _asyncPresent.store(_showing);
    EveSetOutput(_chip, _chipFrame.data(), width, width, height, _drawing ? 1 : 0);
    return resized;
}

bool EveHostCard::CaptureLive() const
{
    return !(_context && _context->ttdReplayActive);
}

bool EveHostCard::Drawing() const
{
    // Only a shown picture is drawn, and not while turbo skips the frames
    // (all timing runs either way: nothing the guest sees depends on drawing)
    if (_capture.IsOpen())
        return true;  // a capture hashes every frame
    if (_measureAlways)
        return true;  // line metrics for every frame
    if (_context && _context->ttdReplayActive)
        return true;  // a TTD replay composes the position's picture from what is drawn
    const Screen* screen = _context ? _context->pScreen : nullptr;
    return _showing && !(screen && screen->IsTurboRenderSkip());
}

void EveHostCard::OnChipFrame()
{
    _chipFrames = EveCompletedFrames(_chip);
    EveSetLineBudgetMargin(_chip, _lineBudgetMargin.load());  // for the frame now starting
    if (_capture.IsOpen() && CaptureLive())
        _capture.Frame(EveTotalClocks(_chip), _chipFrames, _drawing, _pictureWidth, _pictureHeight,
                       _drawing ? Vdac2Capture::HashPicture(_chipFrame.data(), _chipFrame.size()) : 0);
    if (!_showing)
    {
        if (_capture.IsOpen())
            ConfigureOutput();  // follow mode changes: the next frame is drawn at the right size
        return;
    }

    // Set up the next frame first: a new mode resizes the buffers (the frame
    // just finished was drawn for the old size and is not presented), and the
    // drawing switch follows turbo decimation
    const bool drew = _drawing;
    if (ConfigureOutput())
    {
        PublishPicture();
        return;
    }
    if (!drew || _async)
        return;  // nothing new was drawn: the Screen keeps its last frame; or the renderer presents it

    // Not from a throwaway TTD replay (it composes a position's picture and publishes it itself): a latch there
    // would push frames nobody asked to see into the present queue, as MainLoop's native latch gate explains
    PresentPicture();
    _latchedFrames++;
    if (_measureAlways.load())
        KeepPresentedMetrics(false);  // the FT812 Debug window shows what the monitor shows
}

void EveHostCard::StampNextFrame()
{
    // The CPU does not move while the chip advances: a frame this advance ends ends at this position
    const Z80* z80 = _context && _context->pCore ? _context->pCore->GetZ80() : nullptr;
    const uint64_t length = _context ? _context->config.frame : 0;
    FrameStamp& slot = _frameStamps[(_chipFrames + 1) % kFrameStamps];
    slot.frameNumber.store(_context ? _context->emulatorState.frame_counter : 0, std::memory_order_relaxed);
    slot.position.store(z80 ? (length << 32) | static_cast<uint32_t>(z80->t) : 0, std::memory_order_relaxed);
}

void EveHostCard::BeforeRelease()
{
    _params.drawThread = false;  // AdvanceTo does not turn it on again
    if (_async)
    {
        EveSetAsyncDrawing(_chip, 0, nullptr, nullptr);
        _async = false;
    }
}

bool EveHostCard::AsyncWanted() const
{
    return _params.drawThread && _chip && !_capture.IsOpen() && !(_context && _context->ttdReplayActive);
}

void EveHostCard::ConfigureAsync()
{
    const bool want = AsyncWanted();
    if (EveSetAsyncDrawing(_chip, want ? 1 : 0, &EveHostCard::OnRendererFrame, this) != 0)
    {
        MLOGWARNING("%s: the FT812 draws on the emulation thread: %s", _params.name, EveLastError());
        _params.drawThread = false;
        return;
    }
    _async = want;
}

void EveHostCard::OnRendererFrame(void* user, uint64_t frame, int drawn)
{
    // OnChipFrame's presentation, on the renderer thread once the frame's last line is drawn. The buffers are
    // not resized meanwhile (ConfigureOutput waits for the renderer first)
    auto* card = static_cast<EveHostCard*>(user);
    if (!drawn || !card->_asyncPresent.load())
        return;
    const FrameStamp& slot = card->_frameStamps[frame % kFrameStamps];
    const uint64_t position = slot.position.load(std::memory_order_relaxed);
    const uint32_t length = static_cast<uint32_t>(position >> 32);
    const float fraction = length ? std::min(static_cast<float>(static_cast<uint32_t>(position)) / length, 1.0f) : 1.0f;
    card->PresentPicture(slot.frameNumber.load(std::memory_order_relaxed), fraction);
    card->_latchedFrames++;
    if (card->_measureAlways.load())
        card->KeepPresentedMetrics(false);  // the library answers the renderer without waiting
}

void EveHostCard::ConvertFrameToPicture()
{
    ConvertFrame(_picture.data(), nullptr, _chipFrame.size());
}

void EveHostCard::ConvertFrame(uint8_t* out, uint8_t* copy, size_t pixels) const
{
    // ARGB8888 (0xAARRGGBB) -> the framebuffer's RGBA8888 (bytes R, G, B, A), whole words so the compiler
    // vectorises it; into a second buffer in the same pass (the present slot: one read of the frame, not two)
    const uint32_t* in = _chipFrame.data();
    for (size_t i = 0; i < pixels; i++)
    {
        const uint32_t argb = in[i];
        uint32_t rgba;
        if constexpr (std::endian::native == std::endian::little)
            rgba = ((argb >> 16) & 0xFFu) | (argb & 0xFF00u) | ((argb & 0xFFu) << 16) | 0xFF000000u;
        else
            rgba = ((argb << 8) & 0xFFFFFF00u) | 0xFFu;
        std::memcpy(out + i * 4, &rgba, 4);
        if (copy)
            std::memcpy(copy + i * 4, &rgba, 4);
    }
}

void EveHostCard::PresentPicture()
{
    PresentPicture(nullptr);
}

void EveHostCard::PresentPicture(uint64_t frameNumber, float fraction)
{
    Screen::ExternalFrameStamp stamp;
    stamp.frameNumber = frameNumber;
    stamp.fraction = fraction;
    PresentPicture(&stamp);
}

void EveHostCard::PresentPicture(const void* stampPointer)
{
    const auto* stamp = static_cast<const Screen::ExternalFrameStamp*>(stampPointer);
    Screen* screen = _context ? _context->pScreen : nullptr;
    if (!screen || (_context && _context->ttdReplayActive))
    {
        ConvertFrameToPicture();
        return;
    }
    // The picture and the present slot in one pass; no slot to latch: the picture alone
    auto fill = [](void* self, uint8_t* slot, size_t bytes) {
        auto* card = static_cast<EveHostCard*>(self);
        const size_t pixels = card->_chipFrame.size();
        if (bytes == pixels * 4 && card->_picture.size() == bytes)
        {
            card->ConvertFrame(card->_picture.data(), slot, pixels);
            return;
        }
        // Not the picture's size (the Screen latches only its own size): the copy the latch makes
        card->ConvertFrameToPicture();
        std::memcpy(slot, card->_picture.data(), std::min(bytes, card->_picture.size()));
    };
    if (!screen->LatchExternalFrame(fill, this, stamp))
        ConvertFrameToPicture();
}

void EveHostCard::SetLineBudgetMargin(uint32_t percent)
{
    _lineBudgetMargin.store(std::min<uint32_t>(percent, 50));
}

void EveHostCard::SetMeasureAlways(bool on)
{
    _measureAlways.store(on);  // AdvanceTo reconfigures the output when Drawing() changes
}

void EveHostCard::ReadFrameMetrics(Vdac2Control::FrameMetrics& out, bool withLines, bool inFlight)
{
    out = Vdac2Control::FrameMetrics{};
    out.margin = _lineBudgetMargin.load();
    out.measureAlways = _measureAlways.load();
    if (!_chip)
        return;
    EveFrameMetrics block{};
    std::vector<uint16_t> lines;
    if (withLines)
        lines.resize(4096);
    lines.resize(EveGetFrameMetrics(_chip, &block, withLines ? lines.data() : nullptr, lines.size()));
    out.valid = block.valid != 0;
    out.frame = block.frame;
    out.lines = block.lines;
    out.hardBudget = block.hardBudget;
    out.softBudget = block.softBudget;
    out.worstLine = block.worstLine;
    out.worstClocks = block.worstClocks;
    out.totalClocks = block.totalClocks;
    out.linesOverSoft = block.linesOverSoft;
    out.linesOverHard = block.linesOverHard;
    out.lineClocks = std::move(lines);
    if (!inFlight)
        return;
    Synchronize();
    ConfigureOutput();  // EveSetOutput catches the drawing up to the chip's position
    out.inFlightKnown = true;
    out.inFlightLinesPassed = EveFrameLinesPassed(_chip);
    if (withLines)
    {
        const uint32_t passed = std::min<uint32_t>(out.inFlightLinesPassed, 4096);
        out.inFlightLineClocks.resize(passed);
        for (uint32_t line = 0; line < passed; ++line)
        {
            EveLineCost cost{};
            EveGetLineCost(_chip, line, &cost);
            out.inFlightLineClocks[line] = cost.valid ? static_cast<int32_t>(cost.totalClocks) : -1;
        }
    }
}

void EveHostCard::KeepPresentedMetrics(bool withInFlight)
{
    Vdac2Control::FrameMetrics metrics;
    ReadFrameMetrics(metrics, true, withInFlight);
    std::lock_guard<std::mutex> lock(_presentedLock);
    _presented = std::move(metrics);
    _presentedKnown = true;
}

bool EveHostCard::PresentedFrameMetrics(Vdac2Control::FrameMetrics& out) const
{
    if (!_showing)
        return false;
    std::lock_guard<std::mutex> lock(_presentedLock);
    if (!_presentedKnown)
        return false;
    out = _presented;
    out.margin = _lineBudgetMargin.load();
    out.measureAlways = _measureAlways.load();
    return true;
}

bool EveHostCard::StartCapture(const std::string& path, std::string* error)
{
    if (!_chip)
    {
        if (error)
            *error = "the FT812 is not available";
        return false;
    }
    Synchronize();
    StopCapture();

    // The chip as it is now: the state blob and every memory region
    Vdac2Capture::ChipState state;
    state.state.resize(EveStateSize(_chip));
    EveSaveState(_chip, state.state.data());
    for (size_t i = 0; i < EveRegionCount(_chip); i++)
    {
        EveRegion region{};
        EveGetRegion(_chip, i, &region);
        state.regions.push_back({region.name ? region.name : "", std::vector<uint8_t>(region.base, region.base + region.size)});
    }
    if (!_capture.Open(path, _params.crystalHz, _rom, &state, EveTotalClocks(_chip)))
    {
        if (error)
            *error = "cannot write '" + path + "'";
        return false;
    }
    ConfigureOutput();  // a capture draws every frame (Drawing)
    MLOGINFO("%s: capturing the FT812 bus to '%s'", _params.name, path.c_str());
    return true;
}

/// region <TTD>

namespace
{
// Card blob layout (version 1): padding-free, followed by the chip's EveSaveState bytes
struct CardBlob
{
    uint8_t version;
    uint8_t showing;
    uint8_t intAsserted;
    uint8_t reserved;
    uint32_t edgeCount;
    uint64_t frameBase;
    uint64_t position;
    uint64_t remainder;
    uint64_t nextEvent;
    uint64_t edges[EveHostCard::kMaxPendingEdges];
};
static_assert(std::is_trivially_copyable_v<CardBlob>, "the card blob is a plain byte image");

static_assert(sizeof(CardBlob) == 8 + 8 * 4 + 8 * EveHostCard::kMaxPendingEdges, "card blob layout drift");
constexpr uint8_t kCardBlobVersion = 1;

uint64_t HashBytes(uint64_t h, const uint8_t* data, size_t size)
{
    // 64-bit multiply-xor over 8-byte words, then the tail: deterministic, ~1 MB in a fraction of a ms
    size_t i = 0;
    for (; i + 8 <= size; i += 8)
    {
        uint64_t word;
        std::memcpy(&word, data + i, 8);
        h = (h ^ word) * 0x100000001b3ull;
        h ^= h >> 29;
    }
    for (; i < size; i++)
        h = (h ^ data[i]) * 0x100000001b3ull;
    return h;
}
}  // namespace

std::vector<ttd::TTDTimeField> EveHostCard::TtdTimeFields()
{
    // Measured 2026-10-09: frameBase and position advance one frame's tacts every frame at an idle machine
    return {{static_cast<uint16_t>(offsetof(CardBlob, frameBase)), 8},
            {static_cast<uint16_t>(offsetof(CardBlob, position)), 8},
            {static_cast<uint16_t>(offsetof(CardBlob, nextEvent)), 8}};
}

size_t EveHostCard::TtdStateSize() const
{
    return _chip ? sizeof(CardBlob) + EveStateSize(_chip) : 0;
}

void EveHostCard::TtdSaveState(uint8_t* dst) const
{
    if (!_chip)
        return;
    CardBlob blob{};
    blob.version = kCardBlobVersion;
    blob.showing = _showing ? 1 : 0;
    blob.intAsserted = _time.intAsserted;
    blob.edgeCount = static_cast<uint32_t>(_edgeCount);
    blob.frameBase = _time.frameBase;
    blob.position = _time.position;
    blob.remainder = _time.remainder;
    blob.nextEvent = _time.nextEvent;
    std::memcpy(blob.edges, _edges, sizeof(blob.edges));
    std::memcpy(dst, &blob, sizeof(blob));
    EveSaveState(_chip, dst + sizeof(blob));
}

bool EveHostCard::TtdLoadState(const uint8_t* src)
{
    if (!_chip)
        return false;
    CardBlob blob{};
    std::memcpy(&blob, src, sizeof(blob));
    if (blob.version != kCardBlobVersion || blob.edgeCount > kMaxPendingEdges)
        return false;
    if (EveLoadState(_chip, src + sizeof(blob), EveStateSize(_chip)) != 0)
    {
        MLOGWARNING("%s: TTD state not loaded: %s", _params.name, EveLastError());
        return false;
    }
    _time.frameBase = blob.frameBase;
    _time.position = blob.position;
    _time.remainder = blob.remainder;
    _time.nextEvent = blob.nextEvent;
    _time.intAsserted = blob.intAsserted;
    _edgeCount = blob.edgeCount;
    std::memcpy(_edges, blob.edges, sizeof(_edges));
    _chipFrames = EveCompletedFrames(_chip);

    // The output follows the restored mode; the FT812 picture is not state: what a
    // position shows is composed by the TTD replay (TTDPrepareComposedPicture)
    const bool showingChanged = (blob.showing != 0) != _showing;
    _showing = blob.showing != 0;
    const bool resized = ConfigureOutput();
    if (showingChanged || resized)
        PublishPicture();
    return true;
}

// Memory blob (zero runs dropped): u32 magic "VZR1", u32 encoded bytes after the
// header, then tokens per region in region order. A token is a u32: bit 31 set =
// a run of (token & 0x7FFFFFFF) zero bytes; clear = that many bytes follow as they
// are. Zero runs shorter than kZeroRunMin stay in the data (a run costs a token and
// splits the data around it: 8 bytes; zstd shrinks short zero stretches anyway).
// No token crosses a region boundary. Worst case (no long zero run): the regions'
// size + 12 bytes; the fixed-size blob's tail is zero, which zstd stores in a few bytes
constexpr uint32_t kZeroRunMagic = 0x3152'5A56;  // "VZR1"
constexpr size_t kZeroRunMin = 64;
constexpr size_t kZeroRunHeader = 8;

size_t EveHostCard::TtdMemorySize() const
{
    if (!_chip)
        return 0;
    size_t size = kZeroRunHeader;
    for (size_t i = 0; i < EveRegionCount(_chip); i++)
    {
        EveRegion region{};
        EveGetRegion(_chip, i, &region);
        size += region.size + 4;  // worst case: one data token per region
    }
    return size;
}

namespace
{
/// Length of the zero run starting at `p`, at most `max` bytes (8 bytes at a time)
size_t ZeroRun(const uint8_t* p, size_t max)
{
    size_t n = 0;
    while (n + 8 <= max)
    {
        uint64_t word;
        std::memcpy(&word, p + n, 8);
        if (word != 0)
            break;
        n += 8;
    }
    while (n < max && p[n] == 0)
        ++n;
    return n;
}

void PutToken(uint8_t*& out, uint32_t token)
{
    std::memcpy(out, &token, 4);
    out += 4;
}
}  // namespace

uint8_t* EveHostCard::TtdEncodeZeroRuns(const uint8_t* p, size_t size, uint8_t* out)
{
    // The tokens are the region's maximal zero runs of kZeroRunMin+ bytes and the data between them. Such a run
    // holds a whole 8-byte word at an offset that is a multiple of 8: only those words are looked at, then the run
    // through a zero one is measured both ways (it cannot reach back past the last run measured: a nonzero byte
    // or the region's start ends that one)
    size_t dataStart = 0;
    size_t measured = 0;  // offsets below are inside a run already measured
    auto flushData = [&](size_t end) {
        if (end > dataStart)
        {
            PutToken(out, static_cast<uint32_t>(end - dataStart));
            std::memcpy(out, p + dataStart, end - dataStart);
            out += end - dataStart;
        }
    };
    for (size_t at = 0; at + 8 <= size; at += 8)
    {
        if (at < measured)
            continue;
        uint64_t word;
        std::memcpy(&word, p + at, 8);
        if (word != 0)
            continue;
        size_t start = at;
        while (start > measured && p[start - 1] == 0)
            --start;
        const size_t end = at + ZeroRun(p + at, size - at);
        measured = end;
        if (end - start >= kZeroRunMin)
        {
            flushData(start);
            PutToken(out, 0x8000'0000u | static_cast<uint32_t>(end - start));
            dataStart = end;
        }
    }
    flushData(size);
    return out;
}

size_t EveHostCard::TtdSaveMemory(uint8_t* dst) const
{
    if (!_chip)
        return 0;
    uint8_t* out = dst + kZeroRunHeader;
    for (size_t i = 0; i < EveRegionCount(_chip); i++)
    {
        EveRegion region{};
        EveGetRegion(_chip, i, &region);
        out = TtdEncodeZeroRuns(region.base, region.size, out);
    }
    const uint32_t encoded = static_cast<uint32_t>(out - dst - kZeroRunHeader);
    std::memcpy(dst, &kZeroRunMagic, 4);
    std::memcpy(dst + 4, &encoded, 4);
    return static_cast<size_t>(out - dst);
}

void EveHostCard::TtdSaveMemoryHeader(uint8_t* dst)
{
    const uint32_t encoded = 0;
    std::memcpy(dst, &kZeroRunMagic, 4);
    std::memcpy(dst + 4, &encoded, 4);
}

bool EveHostCard::TtdLoadMemory(const uint8_t* src)
{
    if (!_chip)
        return false;
    uint32_t magic = 0;
    uint32_t encoded = 0;
    std::memcpy(&magic, src, 4);
    std::memcpy(&encoded, src + 4, 4);
    if (magic != kZeroRunMagic || encoded > TtdMemorySize() - kZeroRunHeader)
        return false;
    const uint8_t* in = src + kZeroRunHeader;
    const uint8_t* end = in + encoded;
    for (size_t i = 0; i < EveRegionCount(_chip); i++)
    {
        EveRegion region{};
        EveGetRegion(_chip, i, &region);
        size_t pos = 0;
        while (pos < region.size)
        {
            if (end - in < 4)
                return false;
            uint32_t token;
            std::memcpy(&token, in, 4);
            in += 4;
            const size_t length = token & 0x7FFF'FFFFu;
            if (length == 0 || length > region.size - pos)
                return false;
            if (token & 0x8000'0000u)
                std::memset(region.base + pos, 0, length);
            else
            {
                if (static_cast<size_t>(end - in) < length)
                    return false;
                std::memcpy(region.base + pos, in, length);
                in += length;
            }
            pos += length;
        }
    }
    EveMemoryRestored(_chip);
    return in == end;
}

uint64_t EveHostCard::TtdStateHash() const
{
    std::vector<uint8_t> state(TtdStateSize());
    if (state.empty())
        return 0;
    TtdSaveState(state.data());
    return HashBytes(0xcbf29ce484222325ull, state.data(), state.size());
}

uint64_t EveHostCard::TtdMemoryHash() const
{
    uint64_t h = 0xcbf29ce484222325ull;
    if (!_chip)
        return h;
    for (size_t i = 0; i < EveRegionCount(_chip); i++)
    {
        EveRegion region{};
        EveGetRegion(_chip, i, &region);
        h = HashBytes(h, region.base, region.size);
    }
    return h;
}

void EveHostCard::TTDPrepareComposedPicture(bool frameTarget)
{
    if (!_chip)
        return;
    // The chip brought up to the position (it runs behind the CPU until it is asked)
    Synchronize();
    if (!_showing)
        return;
    if (frameTarget)
    {
        KeepPresentedMetrics(false);  // the FT812 frame that finished last: _picture already
        return;
    }

    // Inside a frame: the FT812 frame in flight, drawn up to the position over its
    // previous frame. The library draws lines lazily (at a frame end or before a
    // memory write); handing it the output again draws every line due by now
    ConfigureOutput();
    EveDrawingBarrier(_chip);  // the lines due drawn by the renderer
    if (_chipFrame.size() * 4 == _picture.size())
        ConvertFrameToPicture();
    KeepPresentedMetrics(true);  // with the lines of the frame in flight drawn so far
}

/// endregion

bool EveHostCard::StopCapture()
{
    if (!_capture.IsOpen())
        return false;
    Synchronize();
    _capture.Close(EveTotalClocks(_chip));
    ConfigureOutput();
    MLOGINFO("%s: capture '%s' finished, %llu bytes", _params.name, _capture.GetStats().path.c_str(),
             static_cast<unsigned long long>(_capture.GetStats().bytesWritten));
    return true;
}

/// A transaction's byte: the header, then the data (a read's data after its dummy byte, as MISO)
void EveHostCard::HistoryByte(uint8_t mosi, uint8_t miso)
{
    const uint32_t i = _transactionBytes++;
    if (i < 3)
    {
        _transaction.header[i] = mosi;
        return;
    }
    const bool read = (_transaction.header[0] >> 6) == 0;
    if (read && i == 3)
        return;  // the dummy byte
    const uint32_t at = i - (read ? 4 : 3);
    if (at < sizeof(_transaction.data))
        _transaction.data[at] = read ? miso : mosi;
    _transaction.length = at + 1;
}

void EveHostCard::select(bool selected)
{
    Synchronize();
    if (!_chip)
        return;
    EveSelect(_chip, selected ? 1 : 0);
    if (CaptureLive())
        _capture.Select(EveTotalClocks(_chip), selected);
    if (_history.On() && !(_context && _context->ttdReplayActive)) [[unlikely]]
    {
        if (!selected && _transactionBytes != 0)
            _history->Push(_transaction);
        _transaction = {};
        _transactionBytes = 0;
        if (selected)
        {
            _transaction.frame = _context ? _context->emulatorState.frame_counter : 0;
            _transaction.clock = EveTotalClocks(_chip);
        }
    }
    SampleInt(_time.position);
    PlanNextEvent();
}

uint8_t EveHostCard::exchange(uint8_t mosi)
{
    Synchronize();
    if (!_chip)
        return 0xFF;
    const uint8_t miso = EveExchange(_chip, mosi);
    if (CaptureLive())
        _capture.Byte(EveTotalClocks(_chip), mosi, miso);
    if (_history.On()) [[unlikely]]
        HistoryByte(mosi, miso);
    // A register write can raise INT_N at once (INT_EN with flags pending)
    // or release it (the REG_INT_FLAGS read)
    SampleInt(_time.position);
    PlanNextEvent();
    return miso;
}

void EveHostCard::Transaction(const uint8_t* mosi, uint8_t* miso, size_t count)
{
    if (!_chip || (_capture.IsOpen() && CaptureLive()) || _history.On())
    {
        // Not the virtual calls: a subclass's select / exchange is its own bus, not this chip's
        EveHostCard::select(true);
        for (size_t i = 0; i < count; i++)
        {
            const uint8_t value = EveHostCard::exchange(mosi[i]);
            if (miso)
                miso[i] = value;
        }
        EveHostCard::select(false);
        return;
    }
    Synchronize();
    EveSelect(_chip, 1);
    SampleInt(_time.position);
    for (size_t i = 0; i < count; i++)
    {
        const uint8_t value = EveExchange(_chip, mosi[i]);
        if (miso)
            miso[i] = value;
        // A register byte can raise or release INT_N: sampled per byte as the byte path does
        SampleInt(_time.position);
    }
    EveSelect(_chip, 0);
    SampleInt(_time.position);
    PlanNextEvent();
}

void EveHostCard::WriteWords(uint32_t address, const uint8_t* words, size_t count)
{
    // A capture or a history records the bytes of each transaction: those go as transactions
    const bool pass = _chip && !(_capture.IsOpen() && CaptureLive()) && !_history.On();
    size_t done = 0;
    while (done < count)
    {
        const uint32_t at = address + 4 * static_cast<uint32_t>(done);
        if (pass)
        {
            Synchronize();
            const size_t stored = EveWriteRamGWords(_chip, at & 0x3FFFFF, words + 4 * done, count - done);
            if (stored != 0)
            {
                // RAM_G stores change neither INT_N nor the next event: once, as the last transaction did
                SampleInt(_time.position);
                PlanNextEvent();
                done += stored;
                continue;
            }
        }
        const uint8_t* word = words + 4 * done;
        const uint8_t mosi[7] = {static_cast<uint8_t>(0x80 | ((at >> 16) & 0x3F)), static_cast<uint8_t>(at >> 8),
                                 static_cast<uint8_t>(at), word[0], word[1], word[2], word[3]};
        EveHostCard::Transaction(mosi, nullptr, sizeof(mosi));
        ++done;
    }
}

#else // ENABLE_VDAC2

// A build without the FT812 library: the decoder never fits a card (a
// configuration that needs one is refused at load), but the decoder's calls
// still link against these. Nothing here runs
EveHostCard::EveHostCard(EmulatorContext* context, std::function<uint32_t()> rasterInFrame, Params params)
    : _context(context), _params(std::move(params)), _rasterInFrame(std::move(rasterInFrame)),
      _history(context ? &context->histories : nullptr, std::string(_params.historyId), _params.historyLabel, 4096)
{
}
void EveHostCard::HistoryByte(uint8_t, uint8_t) {}
EveHostCard::~EveHostCard() = default;
void EveHostCard::PowerOn() {}
void EveHostCard::PowerOnAt(uint64_t) {}
void EveHostCard::OnFrameEnd() {}
void EveHostCard::Synchronize() {}
void EveHostCard::SetShowing(bool) {}
size_t EveHostCard::TakeIntEdges(uint32_t, uint32_t*, size_t) { return 0; }
bool EveHostCard::StartCapture(const std::string&, std::string* error)
{
    if (error)
        *error = "this build has no FT812 support";
    return false;
}
bool EveHostCard::StopCapture() { return false; }
void EveHostCard::SetLineBudgetMargin(uint32_t percent) { _lineBudgetMargin.store(percent > 50 ? 50 : percent); }
void EveHostCard::SetMeasureAlways(bool on) { _measureAlways.store(on); }
void EveHostCard::ReadFrameMetrics(Vdac2Control::FrameMetrics& out, bool, bool) { out = Vdac2Control::FrameMetrics{}; }
bool EveHostCard::PresentedFrameMetrics(Vdac2Control::FrameMetrics&) const { return false; }
void EveHostCard::select(bool) {}
uint8_t EveHostCard::exchange(uint8_t) { return 0xFF; }
void EveHostCard::Transaction(const uint8_t*, uint8_t* miso, size_t count)
{
    if (miso)
        for (size_t i = 0; i < count; i++)
            miso[i] = 0xFF;
}
void EveHostCard::WriteWords(uint32_t, const uint8_t*, size_t) {}
std::vector<ttd::TTDTimeField> EveHostCard::TtdTimeFields() { return {}; }
size_t EveHostCard::TtdStateSize() const { return 0; }
void EveHostCard::TtdSaveState(uint8_t*) const {}
bool EveHostCard::TtdLoadState(const uint8_t*) { return false; }
size_t EveHostCard::TtdMemorySize() const { return 0; }
size_t EveHostCard::TtdSaveMemory(uint8_t*) const { return 0; }
void EveHostCard::TtdSaveMemoryHeader(uint8_t*) {}
uint8_t* EveHostCard::TtdEncodeZeroRuns(const uint8_t*, size_t, uint8_t* out) { return out; }
bool EveHostCard::TtdLoadMemory(const uint8_t*) { return false; }
uint64_t EveHostCard::TtdStateHash() const { return 0; }
uint64_t EveHostCard::TtdMemoryHash() const { return 0; }
void EveHostCard::TTDPrepareComposedPicture(bool) {}

#endif // ENABLE_VDAC2
