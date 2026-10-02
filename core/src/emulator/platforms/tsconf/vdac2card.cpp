#include "stdafx.h"

#include "vdac2card.h"

// The card exists only in builds with the FT812 library (CMake ENABLE_VDAC2);
// without it a VDAC2 configuration is refused at load (config.cpp)
#ifdef ENABLE_VDAC2

#include <eve/eve.h>

#include <algorithm>
#include <cstdio>

#include "common/filehelper.h"
#include "common/modulelogger.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/screen.h"

namespace
{

/// Longest interval converted in one multiplication: a minute of raster
/// tacts x the fastest FT812 clock (80 MHz) stays far inside 64 bits
constexpr uint64_t kMaxChunkTacts = Vdac2Card::kRasterHz * 60;

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

Vdac2Card::Vdac2Card(EmulatorContext* context, std::function<uint32_t()> rasterInFrame)
    : _context(context), _rasterInFrame(std::move(rasterInFrame))
{
    _logger = _context ? _context->pModuleLogger : nullptr;

    LoadRom();

    EveConfig config{};
    config.structSize = sizeof(EveConfig);
    config.model = EVE_MODEL_FT812;
    config.externalClockHz = kCrystalHz;
    config.romImage = _rom.empty() ? nullptr : _rom.data();
    config.romImageSize = _rom.size();
    config.decoders = nullptr;  // the library's built-in decoders (architecture A3)
    _chip = EveCreate(&config);
    if (!_chip)
        MLOGERROR("VDAC2: the FT812 could not be created: %s", EveLastError());
    else
    {
        _chipFrames = EveCompletedFrames(_chip);
        ConfigureOutput();
    }

    _time.position = Now();
    PlanNextEvent();
}

Vdac2Card::~Vdac2Card()
{
    // The Screen must not keep a pointer into _picture (at machine teardown
    // the Screen is gone first and pScreen is already null)
    if (_showing && _context && _context->pScreen)
        _context->pScreen->ClearExternalPicture();
    if (_chip)
        EveDestroy(_chip);
}

void Vdac2Card::LoadRom()
{
    // The ROM holds the FT812's fonts 16-34 (CMD_TEXT, the TEXT8X8 / TEXTVGA
    // formats). It is Bridgetek's and never shipped: the user extracts it
    // (tools/machines/tsconf/vdac2/). Without it the chip works and the ROM
    // reads as zeros, i.e. ROM text is blank (D-C §10)
    const char* configured = _context ? _context->config.vdac2_rom_path : "";
    if (configured[0] == '\0')
        return;

    const std::string path = ResolveRomPath(configured);
    if (path.empty())
    {
        MLOGINFO("VDAC2: no FT81x ROM image '%s' ([VDAC2] RomImage) - the FT812's ROM fonts read as zeros", configured);
        return;
    }

    FILE* file = FileHelper::OpenFile(path, "rb");
    if (!file)
    {
        MLOGWARNING("VDAC2: cannot open the FT81x ROM image '%s' - the ROM fonts read as zeros",
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
        MLOGWARNING("VDAC2: '%s' is not an FT81x ROM image (1..%zu bytes expected) - the ROM fonts read as zeros",
                    FileHelper::PrintablePath(path).c_str(), kRomImageSize);
        return;
    }
    image.resize(size);
    _rom = std::move(image);
}

void Vdac2Card::PowerOn()
{
    if (_chip)
    {
        EveReset(_chip);
        _chipFrames = EveCompletedFrames(_chip);
        if (ConfigureOutput())
            PublishPicture();
    }
    _time.position = Now();
    _time.remainder = 0;
    _time.intAsserted = 0;
    _edgeCount = 0;
    PlanNextEvent();
}

void Vdac2Card::OnFrameEnd()
{
    AdvanceTo(_time.frameBase + kFrameTacts);
    _time.frameBase += kFrameTacts;
}

void Vdac2Card::Synchronize()
{
    AdvanceTo(Now());
}

void Vdac2Card::AdvanceTo(uint64_t absoluteRaster)
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
                OnChipFrame();
            const uint64_t need = done * kRasterHz;
            const uint64_t tacts = need > startRemainder ? (need - startRemainder + hz - 1) / hz : 0;
            SampleInt(start + tacts);
        }
    }
    PlanNextEvent();
}

void Vdac2Card::SampleInt(uint64_t at)
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

void Vdac2Card::PlanNextEvent()
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

size_t Vdac2Card::TakeIntEdges(uint32_t rasterInFrame, uint32_t* out, size_t max)
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

void Vdac2Card::SetShowing(bool showing)
{
    if (showing == _showing || !_chip)
        return;
    _showing = showing;
    ConfigureOutput();
    PublishPicture();
}

void Vdac2Card::PublishPicture()
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

bool Vdac2Card::ConfigureOutput()
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
        _pictureWidth = width;
        _pictureHeight = height;
        const size_t pixels = static_cast<size_t>(width) * height;
        _chipFrame.assign(pixels, 0xFF000000u);
        _picture.assign(pixels * 4, 0);
        for (size_t i = 0; i < pixels; i++)
            _picture[i * 4 + 3] = 0xFF;  // opaque black
    }
    _drawing = Drawing();
    EveSetOutput(_chip, _chipFrame.data(), width, width, height, _drawing ? 1 : 0);
    return resized;
}

bool Vdac2Card::Drawing() const
{
    // Only a shown picture is drawn, and not while turbo skips the frames
    // (all timing runs either way: nothing the guest sees depends on drawing)
    const Screen* screen = _context ? _context->pScreen : nullptr;
    return _showing && !(screen && screen->IsTurboRenderSkip());
}

void Vdac2Card::OnChipFrame()
{
    _chipFrames = EveCompletedFrames(_chip);
    if (!_showing)
        return;

    // Set up the next frame first: a new mode resizes the buffers (the frame
    // just finished was drawn for the old size and is not presented), and the
    // drawing switch follows turbo decimation
    const bool drew = _drawing;
    if (ConfigureOutput())
    {
        PublishPicture();
        return;
    }
    if (!drew)
        return;  // nothing new was drawn: the Screen keeps its last frame

    // ARGB8888 (0xAARRGGBB) -> the framebuffer's RGBA8888 (bytes R, G, B, A)
    // SIMD-CANDIDATE(VDAC2-1): a byte shuffle per pixel, 786 432 pixels at 1024 x 768
    const size_t pixels = _chipFrame.size();
    uint8_t* out = _picture.data();
    for (size_t i = 0; i < pixels; i++)
    {
        const uint32_t argb = _chipFrame[i];
        out[i * 4 + 0] = static_cast<uint8_t>(argb >> 16);
        out[i * 4 + 1] = static_cast<uint8_t>(argb >> 8);
        out[i * 4 + 2] = static_cast<uint8_t>(argb);
        out[i * 4 + 3] = 0xFF;
    }
    if (Screen* screen = _context ? _context->pScreen : nullptr)
        screen->LatchExternalFrame();
    _latchedFrames++;
}

void Vdac2Card::select(bool selected)
{
    Synchronize();
    if (!_chip)
        return;
    EveSelect(_chip, selected ? 1 : 0);
    SampleInt(_time.position);
    PlanNextEvent();
}

uint8_t Vdac2Card::exchange(uint8_t mosi)
{
    Synchronize();
    if (!_chip)
        return 0xFF;
    const uint8_t miso = EveExchange(_chip, mosi);
    // A register write can raise INT_N at once (INT_EN with flags pending)
    // or release it (the REG_INT_FLAGS read)
    SampleInt(_time.position);
    PlanNextEvent();
    return miso;
}

#endif // ENABLE_VDAC2
