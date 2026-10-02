// eve-emu - create / destroy / reset, the C API entry points (spec §2.2, arch §4).
#include "eve-copro.h"

#include <initializer_list>
#include <new>

namespace EveLib
{

namespace
{

thread_local char lastError[256] = "";

constexpr uint32_t kMaxRomImageSize = kRomEnd - kRomFontBase;

} // namespace

void SetLastError(const char* text)
{
    size_t i = 0;
    for (; text[i] != '\0' && i + 1 < sizeof(lastError); ++i)
        lastError[i] = text[i];
    lastError[i] = '\0';
}

void CoreReset(EveChip& chip, bool clearRamG)
{
    ControlState& state = chip.state;
    state.bus = BusState{};
    Region& ramG = chip.regions[RegionRamG];
    if (clearRamG)
    {
        std::memset(ramG.base, 0, ramG.size);
        ramG.MarkDirtyRange(0, ramG.size);
    }
    for (uint32_t i = 0; i < 4; ++i)
        ramG.base[kChipIdAddress + i] = chip.table->chipId[i];
    ramG.MarkDirty(kChipIdAddress);
    for (RegionId id : {RegionDl0, RegionDl1, RegionReg, RegionCmd, RegionSpecial})
    {
        Region& region = chip.regions[id];
        std::memset(region.base, 0, region.size);
        region.MarkDirtyRange(0, region.size);
    }

    ScanState& scan = state.scan;
    const uint64_t completedFrames = scan.completedFrames;
    scan = ScanState{};
    scan.completedFrames = completedFrames; // monotonic for the host
    scan.idReadyAt = kIdReadyDelayClocks;

    ResetRegisters(chip);
    AudioReset(chip);
    CoproReset(chip);
    DrawingReset(chip);
}

void PowerOnReset(EveChip& chip)
{
    ControlState& state = chip.state;
    const uint64_t completedFrames = state.scan.completedFrames;
    const uint64_t totalClocks = state.totalClocks;
    std::memset(&state, 0, sizeof(state));
    state.totalClocks = totalClocks;
    state.magic = kStateMagic;
    state.version = kStateVersion;
    state.size = static_cast<uint32_t>(sizeof(ControlState));
    state.model = static_cast<uint32_t>(chip.table->model);
    state.scan.completedFrames = completedFrames;
    // At power-on the chip is in SLEEP with the internal oscillator selected (spec §2.2).
    state.power.mode = PowerMode::Sleep;
    state.power.externalClock = 0;
    InitClock(chip);
    ClearRegions(chip);
    CoreReset(chip, true);
}

} // namespace EveLib

using namespace EveLib;

extern "C" {

void EveGetBuildInfo(EveBuildInfo* out)
{
    if (out == nullptr)
        return;
    out->version = EVE_VERSION;
    out->builtinInflate = EVE_HAS_BUILTIN_INFLATE ? 1 : 0;
    out->builtinPng = EVE_HAS_BUILTIN_PNG ? 1 : 0;
    out->builtinJpeg = EVE_HAS_BUILTIN_JPEG ? 1 : 0;
}

EveChip* EveCreate(const EveConfig* config)
{
    SetLastError("");
    if (config == nullptr)
    {
        SetLastError("EveCreate: config is NULL");
        return nullptr;
    }
    if (config->structSize < sizeof(EveConfig))
    {
        SetLastError("EveCreate: config->structSize is smaller than sizeof(EveConfig)");
        return nullptr;
    }
    if (config->model != EVE_MODEL_FT812)
    {
        SetLastError("EveCreate: unsupported model (only EVE_MODEL_FT812)");
        return nullptr;
    }
    if (config->externalClockHz == 0)
    {
        SetLastError("EveCreate: externalClockHz must not be 0");
        return nullptr;
    }
    if (config->romImage != nullptr && (config->romImageSize == 0 || config->romImageSize > kMaxRomImageSize))
    {
        SetLastError("EveCreate: romImageSize must be 1...1179648 bytes (0x1E0000..0x2FFFFF)");
        return nullptr;
    }

    EveChip* chip = new (std::nothrow) EveChip();
    if (chip == nullptr)
    {
        SetLastError("EveCreate: out of memory");
        return nullptr;
    }
    chip->table = &GetChipTable(config->model);
    chip->externalClockHz = config->externalClockHz;
    chip->romImage = config->romImage;
    chip->romSize = config->romImage != nullptr ? static_cast<uint32_t>(config->romImageSize) : 0;
    chip->romBase = kRomEnd - chip->romSize;
    if (!ResolveDecoders(*chip, config->decoders))
    {
        delete chip;
        return nullptr;
    }
    chip->costs = {kCostCommand, kCostDisplayListWord, kCostMemoryPerByte, kCostMemcrcPerByte,
                   kCostInflatePerOutputByte, kCostLoadImagePerPixel};
    chip->decoderState.reset(new (std::nothrow) uint8_t[chip->inflate->stateSize]());
    chip->workBuffer.reset(new (std::nothrow) uint8_t[kWorkBufferSize]());
    chip->inputBuffer.reset(new (std::nothrow) uint8_t[kRamCmdSize]());
    constexpr size_t kRgbaBytes = 4;
    const size_t imagePixels = kJpegMaxPixels > kPngMaxPixels ? kJpegMaxPixels : kPngMaxPixels;
    chip->imageBuffer.reset(new (std::nothrow) uint8_t[imagePixels * kRgbaBytes]);
    if (!chip->decoderState || !chip->workBuffer || !chip->inputBuffer || !chip->imageBuffer || !InitRegions(*chip) || !InitDrawing(*chip))
    {
        SetLastError("EveCreate: out of memory");
        delete chip;
        return nullptr;
    }
    PowerOnReset(*chip);
    return chip;
}

void EveDestroy(EveChip* chip)
{
    delete chip;
}

void EveReset(EveChip* chip)
{
    if (chip != nullptr)
        PowerOnReset(*chip);
}

const char* EveLastError(void)
{
    return lastError;
}

void EveSelect(EveChip* chip, int selected)
{
    SpiSelect(*chip, selected != 0);
}

uint8_t EveExchange(EveChip* chip, uint8_t mosi)
{
    return SpiExchange(*chip, mosi);
}

} // extern "C"
