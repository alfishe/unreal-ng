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

    _time.position = Now();
}

Vdac2Card::~Vdac2Card()
{
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
        EveReset(_chip);
    _time.position = Now();
    _time.remainder = 0;
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

    uint64_t tacts = absoluteRaster - _time.position;
    _time.position = absoluteRaster;
    while (tacts > 0)
    {
        const uint64_t chunk = std::min(tacts, kMaxChunkTacts);
        tacts -= chunk;
        const uint64_t hz = EveSystemClockHz(_chip);
        if (hz == 0)
        {
            // Clock stopped (sleep, power-down): no edges, no phase to carry
            _time.remainder = 0;
            continue;
        }
        const uint64_t scaled = chunk * hz + _time.remainder;
        _time.remainder = scaled % kRasterHz;
        EveAdvance(_chip, scaled / kRasterHz);
    }
}

void Vdac2Card::select(bool selected)
{
    Synchronize();
    if (_chip)
        EveSelect(_chip, selected ? 1 : 0);
}

uint8_t Vdac2Card::exchange(uint8_t mosi)
{
    Synchronize();
    return _chip ? EveExchange(_chip, mosi) : 0xFF;
}

#endif // ENABLE_VDAC2
