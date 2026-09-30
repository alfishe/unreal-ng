#include "soundchip_neogs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "3rdparty/digestpp/digestpp.hpp"
#include "common/filehelper.h"
#include "common/statebytes.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/media/mediamanager.h"
#include "emulator/media/medium.h"
#include "emulator/sound/chips/gs/gshostclock.h"
#include "emulator/sound/chips/neogs/neogsflashimages.h"

namespace
{
constexpr int64_t kNever = std::numeric_limits<int64_t>::max();

// Card-side port numbers (A5..A0, FPGA ports.v / ports.inc)
constexpr uint8_t P_MPAG = 0x00;
constexpr uint8_t P_ZXCMD = 0x01;   // R: command from host; W: LED
constexpr uint8_t P_ZXDATRD = 0x02;
constexpr uint8_t P_ZXDATWR = 0x03;
constexpr uint8_t P_ZXSTAT = 0x04;
constexpr uint8_t P_CLRCBIT = 0x05;
constexpr uint8_t P_VOL1 = 0x06;
constexpr uint8_t P_VOL4 = 0x09;
constexpr uint8_t P_DAMNPORT1 = 0x0A;
constexpr uint8_t P_DAMNPORT2 = 0x0B;
constexpr uint8_t P_INTENA = 0x0C;
constexpr uint8_t P_INTREQ = 0x0D;
constexpr uint8_t P_TIM_FREQ = 0x0E;
constexpr uint8_t P_GSCFG0 = 0x0F;
constexpr uint8_t P_MPAGEX = 0x10;
constexpr uint8_t P_SCTRL = 0x11;
constexpr uint8_t P_SSTAT = 0x12;
constexpr uint8_t P_SD_SEND = 0x13; // W: SD_SEND, R: SD_READ
constexpr uint8_t P_SD_RSTR = 0x14; // R: SD_RSTR, W: MD_SEND
constexpr uint8_t P_MC_SEND = 0x15; // W: MC_SEND, R: MC_READ
constexpr uint8_t P_VOL5 = 0x16;
constexpr uint8_t P_VOL8 = 0x19;
constexpr uint8_t P_DMA_MOD = 0x1B;
constexpr uint8_t P_DMA_HAD = 0x1C;
constexpr uint8_t P_DMA_CST = 0x1F;
constexpr uint8_t P_PG0 = 0x20;
constexpr uint8_t P_PG3 = 0x23;
} // namespace

using namespace statebytes;

/// region <Construction>

SoundChip_NeoGS::SoundChip_NeoGS(EmulatorContext* context, const NeoGSConfig& config, size_t sampleRate)
    : _context(context)
    , _config(config)
    , _sampleRate(sampleRate)
    , _flash(TICKS_PER_SECOND, config.flashId == NeoGSConfig::FlashId::AMD ? Flash29F040B::Vendor::AMD
                                                                            : Flash29F040B::Vendor::ST)
    , _mem(config.ramKB, &_flash)
    , _dma(*this, _mem, _irq)
    , _audio(static_cast<double>(TICKS_PER_SECOND / TICKS_PER_CRYSTAL), sampleRate)
{
    _logger = _context ? _context->pModuleLogger : nullptr;
    _mb.counters = &_activityCounters;
    _stereoMode = _config.stereoMode;
    _couplingL.configure(static_cast<double>(sampleRate), OUTPUT_HIGHPASS_HZ);
    _couplingR.configure(static_cast<double>(sampleRate), OUTPUT_HIGHPASS_HZ);
    _flash.setWritable(_config.flashWrite != NeoGSConfig::WriteMode::Off);

    _cpu = Z80CpuCreate();
    Z80CpuSetMemoryBus(_cpu, &SoundChip_NeoGS::memReadCb, this, &SoundChip_NeoGS::memWriteCb, this);
    Z80CpuSetPortBus(_cpu, &SoundChip_NeoGS::portReadCb, this, &SoundChip_NeoGS::portWriteCb, this);
    Z80CpuSetIntVectorFn(_cpu, &SoundChip_NeoGS::intVectorCb, this);
    _runner.bind(_cpu, this);

    _sd = std::make_unique<SdCardSpi>();
    // TTD: the card's protocol state is in the blob, its sectors are not; a
    // guest write changes the medium, so it is a replay barrier (the media
    // manager's rule: at most one a frame)
    _sd->setWriteListener([this](uint64_t) {
        if (_context && _context->pMediaManager)
            _context->pMediaManager->NoteWrite(SD_SLOT_ID);
    });
    _spi.attach(NeoGSSpi::SD, _sd.get());
    if (_config.mp3Support != NGSMP3SupportKind::None)
    {
        _mp3 = std::make_unique<Vs10xxDecoder>(
            _config.mp3Chip == NeoGSConfig::Mp3Chip::VS1011 ? Vs10xxDecoder::Chip::VS1011 : Vs10xxDecoder::Chip::VS1001,
            _config.mp3Support == NGSMP3SupportKind::Software ? Vs10xxDecoder::Level::Software : Vs10xxDecoder::Level::Stub,
            TICKS_PER_SECOND);
        _spi.attach(NeoGSSpi::MC, _mp3->sci());
        _spi.attach(NeoGSSpi::MD, _mp3->sdi());
        _dma.attachMp3(_mp3.get());
    }
    _dma.attachSd(_sd.get());
    _zx.setAvailable(_config.fpga == NeoGSConfig::Fpga::Current); // fpgaD has no DMA
    _zx.setWatchFrames(_config.zxDmaWatchFrames);
    _zx.setWatchAlways(_config.zxDmaWatch == NeoGSConfig::ZxDmaWatch::Always);
    _sdWriteProtect = _config.sdWriteProtect;

    coldBoot();

    // The slot goes to the media manager last: registering may attach a
    // medium at once (one parked when the card was last removed, or the
    // configured one when the card is fitted at run time), and the card must
    // be complete by then. At machine creation the configured media follow
    // (MediaManager::ApplyConfiguredMedia)
    if (_context && _context->pMediaManager)
    {
        _context->pMediaManager->RegisterSlot(_sdSlot);
        _sdSlotRegistered = true;
    }
    else if (_config.sdCardPath[0])
        openSdImage(_config.sdCardPath);
}

bool SoundChip_NeoGS::ttdRecording() const
{
    return _context && _context->pTimeTravelManager && _context->pTimeTravelManager->IsRecording();
}

bool SoundChip_NeoGS::insertSdCard(const std::string& path)
{
    if (MediaManager* manager = _sdSlotRegistered ? _context->pMediaManager : nullptr)
    {
        MediaSource source;
        source.path = path;
        source.type = FileHelper::IsFolder(path) ? MediaSourceType::Folder : MediaSourceType::File;
        InsertOptions options;
        options.access = configuredSdAccess(_config);
        options.writeProtect = _sdWriteProtect;
        options.disposition = Disposition::Discard; // the card's own call always replaced the card
        const MediaResult result = manager->Insert(SD_SLOT_ID, source, options);
        if (!result.Ok())
            MLOGWARNING("NeoGS: SD card '%s' not inserted: %s", FileHelper::PrintablePath(path).c_str(), result.message.c_str());
        return result.Ok();
    }
    // The machine's configuration is fixed while a TTD recording runs
    if (ttdRecording())
    {
        MLOGWARNING("NeoGS: SD card insert refused - a TTD recording is running");
        return false;
    }
    return openSdImage(path);
}

AccessMode SoundChip_NeoGS::configuredSdAccess(const NeoGSConfig& config)
{
    switch (config.sdWrite)
    {
        case NeoGSConfig::WriteMode::Persist: return AccessMode::WriteThrough;
        case NeoGSConfig::WriteMode::Off: return AccessMode::ReadOnly;
        default: return AccessMode::Session;
    }
}

SdCardSpi::Type SoundChip_NeoGS::sdType() const
{
    return _config.sdType == NeoGSConfig::SDType::SDHC   ? SdCardSpi::Type::SDHC
         : _config.sdType == NeoGSConfig::SDType::SDSC ? SdCardSpi::Type::SDSC
                                                       : SdCardSpi::Type::Auto;
}

void SoundChip_NeoGS::reselectSd()
{
    _sd->select((_spi.sctrlRaw() & NeoGSSpi::SCTRL_SD_NCS) == 0);
}

bool SoundChip_NeoGS::openSdImage(const std::string& path)
{
    const SdCardSpi::WriteMode mode = _config.sdWrite == NeoGSConfig::WriteMode::Persist ? SdCardSpi::WriteMode::Persist
                                    : _config.sdWrite == NeoGSConfig::WriteMode::Off     ? SdCardSpi::WriteMode::Off
                                                                                         : SdCardSpi::WriteMode::Session;
    if (!_sd->open(path, mode, sdType()))
    {
        MLOGWARNING("NeoGS: SD card image '%s' cannot be opened - slot left empty", FileHelper::PrintablePath(path).c_str());
        return false;
    }
    reselectSd();
    return true;
}

bool SoundChip_NeoGS::ejectSdCard()
{
    if (MediaManager* manager = _sdSlotRegistered ? _context->pMediaManager : nullptr)
    {
        EjectOptions options;
        options.disposition = Disposition::Discard; // the programmatic eject of tests and automation wrappers
        const MediaResult result = manager->Eject(SD_SLOT_ID, options);
        if (!result.Ok())
            MLOGWARNING("NeoGS: SD card not ejected: %s", result.message.c_str());
        return result.Ok();
    }
    if (ttdRecording())
    {
        MLOGWARNING("NeoGS: SD card eject refused - a TTD recording is running");
        return false;
    }
    _sd->close();
    return true;
}

void SoundChip_NeoGS::markReplayBarrier(ttd::TTDExternalEventKind kind, const char* reason)
{
    // No-op unless a TTD session is recording
    if (_context && _context->pTimeTravelManager)
        _context->pTimeTravelManager->RecordExternalEvent(kind, reason);
}

/// region <SD slot>

SoundChip_NeoGS::SdSlot::SdSlot(SoundChip_NeoGS& owner) : _owner(owner)
{
    _descriptor.id = SD_SLOT_ID;
    _descriptor.kind = MediaKind::Block;
    _descriptor.label = "SD card (NeoGS)";
    _descriptor.removable = true;
    _descriptor.swapDelayMs = 500; // the players poll SD_DET and re-initialize the card
    _descriptor.acceptsFolder = true;
    _descriptor.defaultAccess = AccessMode::Session;
    _descriptor.defaultFs = FatType::Fat16; // the loader's bare-boot-sector path
    _descriptor.hasCardDetect = true;          // SSTAT bit 1
    _descriptor.hasWriteProtectSwitch = true;  // SSTAT bit 2
    _descriptor.tags = {"sd", "neogs", "addon"};
    _descriptor.guestName = "the NeoGS card's SD slot (NEOGS.ROM, MP3 and module players)";
}

void SoundChip_NeoGS::SdSlot::Attach(Medium& medium)
{
    _owner._sd->attach(*medium.Block(), _owner.sdType());
    _owner._sdSource = medium.Source().path;
    _owner.reselectSd();
}

void SoundChip_NeoGS::SdSlot::Detach()
{
    _owner._sd->detach();
    _owner._sdSource.clear();
}

bool SoundChip_NeoGS::SdSlot::IsBusy() const
{
    return _owner._sd->busy();
}

void SoundChip_NeoGS::SdSlot::SetWriteProtectSwitch(bool on)
{
    _owner._sdWriteProtect = on;
}

void SoundChip_NeoGS::SdSlot::SourceChanged(Medium& medium)
{
    _owner._sdSource = medium.Source().path;
}

/// endregion </SD slot>

SoundChip_NeoGS::~SoundChip_NeoGS()
{
    // The host must not call into a card that is going away
    _zx.shutdown();

    // The medium is parked with its session writes until a NeoGS card returns
    if (_sdSlotRegistered && _context->pMediaManager)
        _context->pMediaManager->UnregisterSlot(SD_SLOT_ID);

    if (_config.flashWrite == NeoGSConfig::WriteMode::Persist && _flash.modified())
        saveFlash();

    if (_cpu)
    {
        Z80CpuDestroy(_cpu);
        _cpu = nullptr;
    }
}

std::string SoundChip_NeoGS::deviceDescription() const
{
    char text[128];
    snprintf(text, sizeof text, "NeoGS (Z80 @ %u MHz, 8 x 8-bit DAC, %zu KB RAM)", cardClockHz() / 1000000,
             getRamSizeKB());
    return text;
}

void SoundChip_NeoGS::setSampleRate(size_t sampleRate)
{
    _sampleRate = sampleRate;
    _audio.setRates(static_cast<double>(TICKS_PER_SECOND / TICKS_PER_CRYSTAL), _sampleRate);
    _couplingL.configure(static_cast<double>(_sampleRate), OUTPUT_HIGHPASS_HZ);
    _couplingR.configure(static_cast<double>(_sampleRate), OUTPUT_HIGHPASS_HZ);
}

void SoundChip_NeoGS::setSynthesisSuppressed(bool suppressed)
{
    if (_synthesisSuppressed == suppressed)
        return;
    _synthesisSuppressed = suppressed;
    if (!suppressed)
        _audio.clear();
}

/// endregion </Construction>

/// region <Reset and boot>

void SoundChip_NeoGS::reset()
{
    coldBoot();
}

void SoundChip_NeoGS::hostReset()
{
    // Decided 2026-09-27 (neogs-tdd.md §3.4): any emulator reset is a cold
    // boot of the whole card. The J1 warm-reset link is not modelled.
    coldBoot();
}

void SoundChip_NeoGS::resetCard()
{
    // Host #33 = 100: the FPGA registers and the CPU reset; the loader runs
    // again from flash page 0. Latches, volumes, PG2/PG3 and the dividers stay.
    fpgaReset();
}

void SoundChip_NeoGS::coldBoot()
{
    Z80CpuReset(_cpu);

    // The crystal dividers start from zero at configuration
    _phaseOrigin = _runner.now();
    _runner.setStallUntil(0);

    _flash.reset();
    _sd->powerOn();
    _mem.powerOn();
    _snd.powerOn();
    _mb.resetAll();
    _gscfg0 = GSCFG0_RESET;
    _port09Bit5 = 0;
    _led = 0;
    _dma.reset();
    memset(_dma.rawRegisters(), 0, sizeof _dma.rawRegisters());
    _zx.reset();
    _irq.reset();
    _spi.reset();
    _sd->select(false);
    if (_mp3)
        _mp3->setReset(false, _runner.now()); // SCTRL #0B: XRESET low
    memset(_mp3Buffer, 0, _mp3Descriptor.memoryBufferSizeInBytes);
    _mp3WasActive = false;
    _nmiPending = false;
    _resetRequest = ResetKind::None;
    _ready = false;
    _mainRomStartTicks = -1;
    _upload.clear();

    _ticksPerCycle = _nextTicksPerCycle = ticksPerCycleFor(_gscfg0);
    _mem.setConfig(_gscfg0);

    _nextDacCrystal = CRYSTAL_PER_DAC_SIDE;
    scheduleTimer(NeoGSInterrupts::nextTickCrystal(_irq.timFreq(), 0));
    _extraStrobeAt = kNever;

    _outL = 0;
    _outR = 0;
    _audio.reset();
    memset(_buffer, 0, _audioDescriptor.memoryBufferSizeInBytes);
    _frameHadActivity = false;
    _wasActive = false;

    applyBootMode();
    reschedule();
}

void SoundChip_NeoGS::fpgaReset()
{
    Z80CpuReset(_cpu);
    _mem.resetRegisters();
    writeGscfg0(GSCFG0_RESET);
    _ticksPerCycle = _nextTicksPerCycle;
    _irq.reset();
    _spi.reset();
    if (_mp3)
        _mp3->setReset(false, _runner.now());
    _led = 0;
    _dma.reset(); // only the run bits reset
    _zx.reset();
    _nmiPending = false;
    _resetRequest = ResetKind::None;
    _ready = false;
    _mainRomStartTicks = -1;
    _runner.setStallUntil(0);

    // The firmware reboots and forgets any uploaded module
    _upload.clear();

    // TIM_FREQ is back to 0: the next regular tick follows the new rate (the
    // dividers themselves run on)
    scheduleTimer(NeoGSInterrupts::nextTickCrystal(0, crystalNow()));
    _extraStrobeAt = kNever;
    reschedule();
}

void SoundChip_NeoGS::applyBootMode()
{
    if (_config.boot == NeoGSConfig::Boot::Direct && _flashLoaded)
    {
        // What the loader's GS105 path leaves (neogs-tdd.md §4.2): the main
        // ROM copied into RAM pages 0-1, RAM mode + RAMRO at 20 MHz, MPAG 0,
        // SP = #4080, IM 0, DI, and the SD chip select still asserted
        memcpy(_mem.ram(), _flash.data() + FLASH_MAIN_ROM, MAIN_ROM_SIZE);
        writeGscfg0(GSCFG0_MAIN_ROM);
        _ticksPerCycle = _nextTicksPerCycle;
        _mem.writeMpag(0);
        writeSctrl(NeoGSSpi::SCTRL_SD_NCS); // d7 = 0: SD selected -> SCTRL #0A
        Z80CpuSetReg(_cpu, Z80CpuRegSp, 0x4080);
        Z80CpuSetReg(_cpu, Z80CpuRegPc, 0x0000);
    }

    if (_config.bootDelayMs > 0)
        _runner.stall(static_cast<int64_t>(_config.bootDelayMs) * static_cast<int64_t>(TICKS_PER_SECOND / 1000));
}

void SoundChip_NeoGS::requestReset(ResetKind kind)
{
    // Called from inside a CPU step (a port write): applied at the next
    // round of the runner, between instructions
    _resetRequest = kind;
    reschedule();
}

void SoundChip_NeoGS::writeGscfg0(uint8_t value)
{
    _gscfg0 = value;
    _mem.setConfig(value);
    _nextTicksPerCycle = ticksPerCycleFor(value);
    if (value == GSCFG0_MAIN_ROM && _mainRomStartTicks < 0)
        _mainRomStartTicks = _runner.now();
}

void SoundChip_NeoGS::applyClockChange()
{
    const int64_t now = _runner.now();
    _spi.onClockChange(now, _ticksPerCycle, _nextTicksPerCycle);
    _dma.onClockChange(now, _ticksPerCycle, _nextTicksPerCycle);
    _ticksPerCycle = _nextTicksPerCycle;
    reschedule();
}

void SoundChip_NeoGS::writeSctrl(uint8_t value)
{
    const int64_t now = _runner.now();
    _spi.writeSctrl(value, now);
    if (_mp3)
    {
        _mp3->advance(now);
        _mp3->setReset((_spi.sctrlRaw() & NeoGSSpi::SCTRL_MC_XRESET) != 0, now);
    }
}

void SoundChip_NeoGS::loadROM(const std::string& flashPath)
{
    _flashLoaded = false;
    _flashTitle.clear();
    _mainRomV111 = false;

    std::string path = flashPath.empty() ? std::string(_config.flashPath) : flashPath;
    if (path.empty())
    {
        MLOGWARNING("NeoGS: no flash image configured - the card has nothing to run");
        coldBoot();
        return;
    }

    std::string resolved = FileHelper::NormalizePath(path);
    if (!FileHelper::FileExists(resolved))
    {
        resolved = FileHelper::PathCombine(FileHelper::GetExecutablePath(), path);
        if (!FileHelper::FileExists(resolved))
            resolved = FileHelper::PathCombine(FileHelper::GetResourcesPath(), path);
    }

    FILE* file = FileHelper::FileExists(resolved) ? FileHelper::OpenFile(resolved, "rb") : nullptr;
    if (!file)
    {
        MLOGWARNING("NeoGS: flash image '%s' not found (tried working dir, executable and resources paths)",
                    FileHelper::PrintablePath(path).c_str());
        coldBoot();
        return;
    }

    std::vector<uint8_t> image(Flash29F040B::SIZE, 0xFF);
    const size_t size = fread(image.data(), 1, image.size(), file);
    fclose(file);
    if (size == 0)
    {
        MLOGWARNING("NeoGS: flash image '%s' is empty", FileHelper::PrintablePath(resolved).c_str());
        coldBoot();
        return;
    }
    if (size != Flash29F040B::SIZE)
        MLOGWARNING("NeoGS: flash image '%s' is %zu bytes (expected 512 KB) - the rest reads as erased",
                    FileHelper::PrintablePath(resolved).c_str(), size);

    const std::string digest = digestpp::sha256().absorb(image.data(), image.size()).hexdigest();

    // FlashWrite=persist: a previously saved copy of this image replaces it
    const std::string folder = _flashPersistFolder.empty() ? FileHelper::GetWritablePath() : _flashPersistFolder;
    _flashPersistPath = FileHelper::PathCombine(folder, "neogs-flash-" + digest + ".rom");
    bool persisted = false;
    if (_config.flashWrite == NeoGSConfig::WriteMode::Persist && FileHelper::FileExists(_flashPersistPath))
    {
        std::vector<uint8_t> saved(Flash29F040B::SIZE, 0xFF);
        if (FileHelper::ReadFileToBuffer(_flashPersistPath, saved.data(), saved.size()) == saved.size())
        {
            MLOGINFO("NeoGS: using the reprogrammed flash '%s'", FileHelper::PrintablePath(_flashPersistPath).c_str());
            image.swap(saved);
            persisted = true;
        }
    }
    if (const NeoGSFlashImage* known = findNeoGSFlashImage(digest.c_str()))
    {
        _flashTitle = known->title;
        _mainRomV111 = known->mainRomV111;
        if (known->fpgaD && _config.fpga != NeoGSConfig::Fpga::D)
            MLOGWARNING("NeoGS: '%s' was made for the fpgaD board revision ([NGS] Fpga=D)", known->title);
    }
    else
    {
        _flashTitle = "unknown NeoGS flash image";
        MLOGINFO("NeoGS: flash image SHA-256 %s is not a known release", digest.c_str());
    }

    _flash.load(image.data(), image.size());
    _flashLoaded = true;
    if (persisted)
        _flashTitle += " (reprogrammed)";
    coldBoot();
}

bool SoundChip_NeoGS::neogsState(NeoGSStateInfo& out) const
{
    out = NeoGSStateInfo{};
    out.flashTitle = _flashTitle;
    out.flashModified = _flash.modified();
    out.gscfg0 = _gscfg0;
    out.clockHz = cardClockHz();
    for (int w = 0; w < 4; w++)
    {
        out.pages[w] = _mem.page(w);
        out.windowFlash[w] = _mem.isFlash(w);
    }
    out.mpag = _mem.lastMpag();
    out.ledOn = ledOn();
    out.readyForCommands = isReadyForCommands();
    out.intEnable = _irq.enable();
    out.intRequest = _irq.readRequest();
    out.timFreq = _irq.timFreq();
    out.sctrl = _spi.readSctrl();
    out.sdPresent = sdCardPresent();
    if (out.sdPresent)
    {
        out.sdPath = sdCardImage();
        out.sdSdhc = _sd->isSdhc();
        out.sdSizeBytes = _sd->sizeBytes();
        out.sdBlocksRead = _sd->blocksRead();
        out.sdBlocksWritten = _sd->blocksWritten();
    }
    out.mp3Fitted = _mp3 != nullptr;
    if (_mp3)
    {
        out.mp3Chip = _mp3->chip() == Vs10xxDecoder::Chip::VS1011 ? "VS1011" : "VS1001";
        out.mp3Dreq = _mp3->running() && _mp3->inputFill() + Vs10xxDecoder::DREQ_FREE <= Vs10xxDecoder::INPUT_FIFO;
        out.mp3Rate = _mp3->streamRate();
        out.mp3Channels = _mp3->streamChannels();
        out.mp3Frames = _mp3->framesDecoded();
        out.mp3DecodeSeconds = _mp3->streamRate() ? static_cast<uint32_t>(_mp3->samplesPlayed() / _mp3->streamRate()) : 0;
        out.mp3InputFill = _mp3->inputFill();
    }
    out.dmaSelect = _dma.moduleSelect();
    for (int m = 0; m < 3; m++)
    {
        out.dmaRunning[m] = _dma.running(static_cast<NeoGSDma::Module>(m));
        out.dmaAddress[m] = _dma.address(static_cast<NeoGSDma::Module>(m));
    }
    {
        static const char* kModes[] = {"off", "watch", "divert"};
        static const char* kPending[] = {"none", "read", "write"};
        out.zxMode = kModes[static_cast<int>(_zx.mode())];
        out.zxOverlayInstalled = _zx.installed();
        out.zxReadLatch = _zx.readLatch();
        out.zxPending = kPending[static_cast<int>(_zx.pending())];
        out.zxPendingAddress = _zx.pendingAddress();
        out.zxBytesRead = _zx.bytesRead();
        out.zxBytesWritten = _zx.bytesWritten();
        out.zxBytesDropped = _zx.bytesDropped();
        out.zxWaitTStates = _zx.waitTStates();
        out.zxLateStarts = _zx.lateStarts();
        out.zxLateStartUnits = _zx.lateStartUnits();
        out.zxWatchSetting = _zx.watchAlways() ? "always" : "selected";
        out.zxWatchFrames = _zx.watchFrames();
        out.zxWatchFramesLeft = _zx.watchFramesLeft();
    }
    return true;
}

bool SoundChip_NeoGS::saveFlash()
{
    if (!_flashLoaded || _flashPersistPath.empty())
        return false;
    if (!FileHelper::SaveBufferToFile(_flashPersistPath, _flash.data(), Flash29F040B::SIZE))
    {
        MLOGWARNING("NeoGS: cannot save the flash to '%s'", FileHelper::PrintablePath(_flashPersistPath).c_str());
        return false;
    }
    _flash.clearModified();
    MLOGINFO("NeoGS: flash saved to '%s'", FileHelper::PrintablePath(_flashPersistPath).c_str());
    return true;
}

/// endregion </Reset and boot>

/// region <Runner hooks and events>

void SoundChip_NeoGS::scheduleTimer(int64_t tickCrystal)
{
    // The tick reaches the controller through a synchroniser on the card clock
    _nextTimerCrystal = tickCrystal;
    _timerStrobeAt = crystalToTicks(tickCrystal) + TIMER_SYNC_CYCLES * _ticksPerCycle;
}

void SoundChip_NeoGS::reschedule()
{
    int64_t next = std::min({_timerStrobeAt, _extraStrobeAt, crystalToTicks(_nextDacCrystal), _dma.nextEvent(), _zx.nextEvent()});
    if (_resetRequest != ResetKind::None)
        next = _runner.now();
    _runner.setNextEvent(next);
}

void SoundChip_NeoGS::runEvents(int64_t now)
{
    if (_resetRequest != ResetKind::None)
    {
        const ResetKind kind = _resetRequest;
        _resetRequest = ResetKind::None;
        if (kind == ResetKind::Cold)
            coldBoot();
        else
            fpgaReset();
        return;
    }

    auto timerStrobe = [this]()
    {
        _activityCounters.interruptPeriods++;
        if (_irq.readRequest() & NeoGSInterrupts::REQ_TIMER)
            _activityCounters.interruptsCoalesced++; // a pending request absorbs the tick
        _irq.raise(NeoGSInterrupts::REQ_TIMER);
    };

    if (now >= _extraStrobeAt)
    {
        timerStrobe();
        _extraStrobeAt = kNever;
    }

    while (now >= _timerStrobeAt)
    {
        timerStrobe();
        scheduleTimer(NeoGSInterrupts::nextTickCrystal(_irq.timFreq(), _nextTimerCrystal));
    }

    while (now >= crystalToTicks(_nextDacCrystal))
    {
        dacSideEvent(_nextDacCrystal);
        _nextDacCrystal += CRYSTAL_PER_DAC_SIDE;
    }

    if (now >= _dma.nextEvent())
        _dma.run(now);
    if (now >= _zx.nextEvent())
        _zx.run(now);

    reschedule();
}

void SoundChip_NeoGS::onNmiAccepted()
{
    _nmiPending = false;
    _activityCounters.nmisAccepted++;
    traceEvent(GSTraceSide::Interrupt, 0, 0, false, 0, GSTraceFlags::kNmi);
}

void SoundChip_NeoGS::onIntAccepted()
{
    // The acknowledge clears the request it served (priority at its M1)
    const uint8_t vector = _irq.vector();
    _irq.acknowledge();
    _activityCounters.interruptsAccepted++;
    traceEvent(GSTraceSide::Interrupt, 0, vector, false);
}

/// endregion </Runner hooks and events>

/// region <Audio>

void SoundChip_NeoGS::dacSideEvent(int64_t crystal)
{
    // The mixer computes one side per 320 crystal clocks, alternately; the
    // side whose turn starts on a timer boundary (multiple of 640) is right
    const bool right = ((crystal / CRYSTAL_PER_DAC_SIDE) & 1) == 0;
    const int32_t raw = _snd.mix(right, _gscfg0);
    const int32_t level = raw * static_cast<int32_t>(_config.volume) / 16384;
    if (right)
        _outR = level;
    else
        _outL = level;

    if (_frameTicks <= 0)
        return;
    const int64_t eventTicks = crystalToTicks(crystal);
    const int64_t position = eventTicks / TICKS_PER_CRYSTAL - _frameStartTicks / TICKS_PER_CRYSTAL;
    // The listening choice: as on the board, the classic GS's 50% cross-feed
    // (the opposite side at half level, scaled so both sides full stays full)
    // or mono
    int32_t left = _outL;
    int32_t right_ = _outR;
    if (_stereoMode == NeoGSConfig::StereoMode::GS)
    {
        left = (2 * _outL + _outR) / 3;
        right_ = (2 * _outR + _outL) / 3;
    }
    else if (_stereoMode == NeoGSConfig::StereoMode::Mono)
    {
        left = right_ = (_outL + _outR) / 2;
    }
    if (_audio.set(position, blipFrameLength(), left, right_, !_synthesisSuppressed))
        _frameHadActivity = true;
}

void SoundChip_NeoGS::dacCapture(uint16_t addr, uint8_t value)
{
    const int channel = _snd.capture(addr, value, _gscfg0);
    _activityCounters.dacFetches++;
    _activityCounters.lastDacFetchGsCycle = _runner.now();
    _activityCounters.lastDacFetchFrame = currentFrameNumber();
    traceEvent(GSTraceSide::DacFetch, addr, value, false, static_cast<uint8_t>(channel));
}

/// endregion </Audio>

/// region <Frame lifecycle and host sync>

bool SoundChip_NeoGS::zxHostNowUnits(int64_t& now) const
{
    return GSHostClock::targetUnits(_context, TICKS_PER_SECOND, _frameStartZxTacts, _frameStartTicks, now);
}

double SoundChip_NeoGS::zxUnitsPerHostT() const
{
    // Host T-states count at the CPU rate; the ZX tact domain has hardware
    // turbo descaled (EmulatorState::AudioTstate)
    const unsigned ratio = _context ? _context->emulatorState.hw_turbo_ratio_applied : 1u;
    return GSHostClock::unitsPerZxTact(_context, TICKS_PER_SECOND) / static_cast<double>(ratio ? ratio : 1u);
}

void SoundChip_NeoGS::zxAddHostWait(uint32_t tStates)
{
    if (_context && _context->pCore && _context->pCore->GetZ80())
        _context->pCore->GetZ80()->AddWaitStates(tStates);
}

bool SoundChip_NeoGS::zxInstall(bool installed)
{
    if (!_context || !_context->pCore)
        return false;
    if (!installed)
    {
        _context->pCore->RemoveBusOverlay(&_zx);
        return true;
    }
    return _context->pCore->AddBusOverlay(&_zx);
}

void SoundChip_NeoGS::zxLateStart(int64_t units)
{
    if (_zxLateStartLogged)
        return;
    _zxLateStartLogged = true;
    MLOGWARNING("NeoGS: ZX-DMA started while not watched; seen %.1f us late (raise [NGS] ZxDmaWatchFrames or set "
                "ZxDmaWatch=always). Further late starts are counted, not logged.",
                static_cast<double>(units) / (TICKS_PER_SECOND / 1e6));
}

void SoundChip_NeoGS::zxTrace(bool write, uint32_t address, uint8_t value)
{
    if (!_portTrace.isCapturing())
        return;
    GSTraceEvent event;
    event.timestamp = _runner.now();
    event.frameNumber = currentFrameNumber();
    event.port = static_cast<uint16_t>(address);
    event.channel = static_cast<uint8_t>((address >> 16) & 0x1F);
    event.pc = (_context && _context->pCore && _context->pCore->GetZ80()) ? _context->pCore->GetZ80()->m1_pc : 0;
    event.value = value;
    event.side = GSTraceSide::ZxDma;
    event.flags = write ? GSTraceFlags::kDirectionOut : 0;
    _portTrace.record(event);
}

void SoundChip_NeoGS::flush()
{
    int64_t target = 0;
    if (!GSHostClock::targetUnits(_context, TICKS_PER_SECOND, _frameStartZxTacts, _frameStartTicks, target))
        return;
    _runner.runTo(target);
}

void SoundChip_NeoGS::handleFrameStart()
{
    _frameHadActivity = false;
    _frameStartZxTacts = GSHostClock::currentZxTacts(_context, _frameStartZxTacts);
    _frameStartTicks = _runner.now();
    _frameTicks = GSHostClock::frameUnits(_context, TICKS_PER_SECOND);
}

void SoundChip_NeoGS::handleFrameEnd(size_t expectedSamples)
{
    if (_frameTicks <= 0)
        return;

    _runner.runTo(_frameStartTicks + _frameTicks);
    _zx.onFrameEnd();

    int samples = expectedSamples > 0
                      ? static_cast<int>(expectedSamples)
                      : static_cast<int>(std::llround(static_cast<double>(_frameTicks) * static_cast<double>(_sampleRate) / TICKS_PER_SECOND));
    _audio.endFrame(blipFrameLength(), samples, _buffer);
    for (int i = 0; i < samples; i++)
    {
        _buffer[2 * i] = static_cast<int16_t>(std::clamp(std::lround(_couplingL.filter(_buffer[2 * i])), -32768L, 32767L));
        _buffer[2 * i + 1] = static_cast<int16_t>(std::clamp(std::lround(_couplingR.filter(_buffer[2 * i + 1])), -32768L, 32767L));
    }
    _wasActive = _frameHadActivity;
    const uint64_t dmaBytes = _dma.bytesMoved();
    const uint64_t zxDmaBytes = _zx.bytesRead() + _zx.bytesWritten();
    _dmaWasActive = dmaBytes != _dmaBytesSeen;
    _zxDmaWasActive = zxDmaBytes != _zxDmaBytesSeen;
    _dmaBytesSeen = dmaBytes;
    _zxDmaBytesSeen = zxDmaBytes;

    if (_mp3)
    {
        _spi.sync(_runner.now());
        _mp3->advance(_runner.now());
        _mp3WasActive = _mp3->renderFrame(_mp3Buffer, std::clamp(samples, 0, static_cast<int>(MAX_SAMPLES_PER_FRAME)),
                                          _synthesisSuppressed ? 0.0 : _config.mp3Gain);
    }
}

void SoundChip_NeoGS::onEmulatorPaused()
{
    memset(_buffer, 0, _audioDescriptor.memoryBufferSizeInBytes);
    memset(_mp3Buffer, 0, _mp3Descriptor.memoryBufferSizeInBytes);
    _wasActive = false;
    _mp3WasActive = false;
}

uint32_t SoundChip_NeoGS::currentFrameNumber() const
{
    return _context ? static_cast<uint32_t>(_context->emulatorState.frame_counter) : 0;
}

void SoundChip_NeoGS::traceEvent(GSTraceSide side, uint16_t port, uint8_t value, bool isOut, uint8_t channel, uint8_t extraFlags)
{
    if (!_portTrace.isCapturing())
        return;

    GSTraceEvent event;
    event.timestamp = _runner.now();
    event.frameNumber = currentFrameNumber();
    event.port = port;
    event.pc = Z80CpuGetReg(_cpu, Z80CpuRegPc);
    event.value = value;
    event.channel = channel;
    event.side = side;
    event.flags = static_cast<uint8_t>((isOut ? GSTraceFlags::kDirectionOut : 0) | extraFlags);
    _portTrace.record(event);
}

/// endregion </Frame lifecycle and host sync>

/// region <Host ports>

uint8_t SoundChip_NeoGS::portDeviceInMethod(uint16_t port)
{
    switch (port & 0x00FF)
    {
        case 0xB3: // reply byte; the data bit clears after the cycle
            hostPortSync();
            _mb.status &= 0x7F;
            _activityCounters.hostDataRead++;
            traceEvent(GSTraceSide::Host, PORT_DATA, _mb.dataToHost, false);
            return _mb.dataToHost;
        case 0xBB: // status: bit 7 data, bit 0 command; bits 6:1 read as 1
            hostPortSync();
            traceEvent(GSTraceSide::Host, PORT_COMMAND, _mb.status | 0x7E, false);
            return _mb.status | 0x7E;
        default:
            return 0xFF; // #33 reads are not driven
    }
}

void SoundChip_NeoGS::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    switch (port & 0x00FF)
    {
        case 0x33:
            // Decoded on d7..d5 exactly (zxbus.v): 100 reset, 010 NMI, 001 LED
            traceEvent(GSTraceSide::Host, PORT_CONTROL, value, true);
            switch (value & 0xE0)
            {
                case 0x80:
                    hostPortSync();
                    resetCard();
                    return;
                case 0x40:
                    hostPortSync();
                    _nmiPending = true;
                    return;
                case 0x20:
                    hostPortSync();
                    _led ^= 1;
                    return;
                default:
                    return;
            }
        case 0xB3:
            hostPortSync();
            traceEvent(GSTraceSide::Host, PORT_DATA, value, true);
            onHostDataWrite(value);
            return;
        case 0xBB:
            hostPortSync();
            traceEvent(GSTraceSide::Host, PORT_COMMAND, value, true);
            onHostCommandWrite(value);
            return;
        default:
            return;
    }
}

void SoundChip_NeoGS::onHostDataWrite(uint8_t value)
{
    _activityCounters.hostDataWritten++;
    _mb.dataFromHost = value;
    _mb.status |= 0x80;
    _upload.onData(value, _mem.ramSize());
}

void SoundChip_NeoGS::onHostCommandWrite(uint8_t value)
{
    _activityCounters.hostCommandsReceived++;
    _mb.commandFromHost = value;
    _mb.status |= 0x01;
    _upload.onCommand(value);
}

uint8_t SoundChip_NeoGS::readStatus()
{
    hostPortSync();
    return _mb.status | 0x7E;
}

uint8_t SoundChip_NeoGS::readData()
{
    hostPortSync();
    _mb.status &= 0x7F;
    return _mb.dataToHost;
}

void SoundChip_NeoGS::sendCommand(uint8_t command)
{
    hostPortSync();
    onHostCommandWrite(command);
}

void SoundChip_NeoGS::sendData(uint8_t data)
{
    hostPortSync();
    onHostDataWrite(data);
}

void SoundChip_NeoGS::triggerNMI()
{
    hostPortSync();
    _nmiPending = true;
}

/// endregion </Host ports>

/// region <Card-side ports>

uint8_t SoundChip_NeoGS::cardIn(uint16_t port)
{
    const uint8_t low = static_cast<uint8_t>(port);
    if (low & 0xC0)
        return 0xFF; // CPLD ports: the bus floats after configuration

    const uint8_t p = low & 0x3F;
    const int64_t now = _runner.now();
    uint8_t value = 0xFF;

    switch (p)
    {
        case P_ZXCMD:
            value = _mb.commandFromHost;
            break;
        case P_ZXDATRD:
            _mb.status &= 0x7F;
            value = _mb.dataFromHost;
            break;
        case P_ZXSTAT:
            value = static_cast<uint8_t>(_mb.status | 0x7E);
            // The v1.11 main ROM's command poll loop (neogs-tdd.md §7.1)
            if (!_ready && _mainRomV111 && Z80CpuInstructionPc(_cpu) == MAIN_ROM_COMINT && _gscfg0 == GSCFG0_MAIN_ROM &&
                _mem.page(0) == 0)
                _ready = true;
            break;
        case P_CLRCBIT:
            _mb.status &= 0xFE;
            break;
        case P_DAMNPORT1:
            _mb.status = static_cast<uint8_t>((_mb.status & 0x7F) | ((~_mem.page(2) & 0x01) << 7));
            break;
        case P_DAMNPORT2:
            _mb.status = static_cast<uint8_t>((_mb.status & 0xFE) | _port09Bit5);
            break;
        case P_INTREQ:
            value = _irq.readRequest();
            break;
        case P_GSCFG0:
            value = _gscfg0;
            break;
        case P_SCTRL:
            _spi.sync(now);
            value = _spi.readSctrl();
            break;
        case P_SSTAT:
        {
            _spi.sync(now);
            const bool mcReady = !_spi.busy(NeoGSSpi::MC, now);
            const bool sdPresent = sdCardPresent();
            const bool dreq = _mp3 && _mp3->dreq(now);
            // {0000, MCRDY, SD_WP, SD_DET, DREQ}; WP and DET are low-active switches
            value = static_cast<uint8_t>((mcReady ? 0x08 : 0) | (_sdWriteProtect ? 0 : 0x04) |
                                         (sdPresent ? 0 : 0x02) | (dreq ? 0x01 : 0));
            break;
        }
        case P_SD_SEND: // SD_READ
            _spi.sync(now);
            value = _spi.received(NeoGSSpi::SD);
            break;
        case P_SD_RSTR: // last byte, and a new exchange sending #FF
            _spi.sync(now);
            value = _spi.received(NeoGSSpi::SD);
            _spi.start(NeoGSSpi::SD, 0xFF, now, _ticksPerCycle);
            break;
        case P_MC_SEND: // MC_READ
            _spi.sync(now);
            value = _spi.received(NeoGSSpi::MC);
            break;
        case P_DMA_MOD:
            value = _dma.moduleSelect();
            break;
        default:
            if (p >= P_DMA_HAD && p <= P_DMA_CST)
                value = _dma.readRegister(p - P_DMA_HAD);
            break;
    }

    traceEvent(GSTraceSide::GsInternal, p, value, false);
    return value;
}

void SoundChip_NeoGS::cardOut(uint16_t port, uint8_t value)
{
    const uint8_t low = static_cast<uint8_t>(port);
    if (low & 0xC0)
    {
        // CPLD: #80 is its FPGA reconfiguration port - the card restarts from
        // scratch (the flasher's last instruction)
        if (low == 0x80)
            requestReset(ResetKind::Cold);
        return;
    }

    const uint8_t p = low & 0x3F;
    const int64_t now = _runner.now();
    traceEvent(GSTraceSide::GsInternal, p, value, true);

    switch (p)
    {
        case P_MPAG:
            _mem.writeMpag(value);
            return;
        case P_MPAGEX:
            _mem.writeMpagEx(value);
            return;
        case P_ZXCMD: // LED
            _led = static_cast<uint8_t>(value & 1);
            return;
        case P_ZXDATWR:
            _mb.dataToHost = value;
            _mb.status |= 0x80;
            return;
        case P_CLRCBIT:
            _mb.status &= 0xFE;
            return;
        case P_DAMNPORT1:
            _mb.status = static_cast<uint8_t>((_mb.status & 0x7F) | ((~_mem.page(2) & 0x01) << 7));
            return;
        case P_DAMNPORT2:
            _mb.status = static_cast<uint8_t>((_mb.status & 0xFE) | _port09Bit5);
            return;
        case P_INTENA:
            _irq.writeEnable(value);
            return;
        case P_INTREQ:
            _irq.writeRequest(value);
            return;
        case P_TIM_FREQ:
        {
            const int64_t crystal = crystalNow();
            if (_irq.writeTimFreq(value, crystal))
                _extraStrobeAt = now + TIMER_SYNC_CYCLES * _ticksPerCycle;
            scheduleTimer(NeoGSInterrupts::nextTickCrystal(_irq.timFreq(), crystal));
            reschedule();
            return;
        }
        case P_GSCFG0:
            writeGscfg0(value);
            return;
        case P_SCTRL:
            writeSctrl(value);
            return;
        case P_SD_SEND:
            _spi.start(NeoGSSpi::SD, value, now, _ticksPerCycle);
            return;
        case P_SD_RSTR: // MD_SEND
            _spi.start(NeoGSSpi::MD, value, now, _ticksPerCycle);
            return;
        case P_MC_SEND:
            _spi.start(NeoGSSpi::MC, value, now, _ticksPerCycle);
            return;
        case P_DMA_MOD:
            _dma.writeModuleSelect(value);
            _zx.onModuleSelectWritten();
            return;
        default:
            break;
    }

    if ((p >= P_VOL1 && p <= P_VOL4) || (p >= P_VOL5 && p <= P_VOL8))
    {
        // VOL1-VOL8: always stored, bits 5:0
        const int channel = (p >= P_VOL5) ? 4 + (p - P_VOL5) : (p - P_VOL1);
        _snd.setVolume(channel, value);
        if (p == P_VOL4)
            _port09Bit5 = static_cast<uint8_t>((value >> 5) & 1);
        _activityCounters.volumeLatchWrites++;
        return;
    }

    if (p >= P_PG0 && p <= P_PG3)
    {
        _mem.writePage(p - P_PG0, value);
        return;
    }

    if (p >= P_DMA_HAD && p <= P_DMA_CST)
    {
        _spi.sync(now); // the SD module shares the SD master
        const bool zxWasRunning = _dma.running(NeoGSDma::ZX);
        _dma.writeRegister(p - P_DMA_HAD, value, now);
        if (p == P_DMA_CST && _dma.moduleSelect() == 1)
            _zx.onControlWritten(zxWasRunning, now);
        reschedule();
    }
}

/// endregion </Card-side ports>

/// region <Z80 bus callbacks>

uint8_t SoundChip_NeoGS::memReadCb(Z80CPU* /*cpu*/, uint16_t addr, int /*m1State*/, void* userData)
{
    return static_cast<SoundChip_NeoGS*>(userData)->readMem(addr);
}

void SoundChip_NeoGS::memWriteCb(Z80CPU* /*cpu*/, uint16_t addr, uint8_t value, void* userData)
{
    auto* self = static_cast<SoundChip_NeoGS*>(userData);
    self->_mem.write(addr, value, self->_runner.now());
}

uint8_t SoundChip_NeoGS::portReadCb(Z80CPU* /*cpu*/, uint16_t port, void* userData)
{
    return static_cast<SoundChip_NeoGS*>(userData)->cardIn(port);
}

void SoundChip_NeoGS::portWriteCb(Z80CPU* /*cpu*/, uint16_t port, uint8_t value, void* userData)
{
    static_cast<SoundChip_NeoGS*>(userData)->cardOut(port, value);
}

uint8_t SoundChip_NeoGS::intVectorCb(Z80CPU* /*cpu*/, void* userData)
{
    return static_cast<SoundChip_NeoGS*>(userData)->_irq.vector();
}

uint16_t SoundChip_NeoGS::getCPUReg(GSCpuRegister reg) const
{
    return gsReadCpuRegister(_cpu, reg);
}

/// endregion </Z80 bus callbacks>

/// region <Personality switch>

bool SoundChip_NeoGS::isReadyForCommands() const
{
    if (_ready)
        return true;
    // Unknown firmware: 200 ms after it switched to the main ROM configuration
    return !_mainRomV111 && _mainRomStartTicks >= 0 &&
           _runner.now() - _mainRomStartTicks >= static_cast<int64_t>(0.2 * TICKS_PER_SECOND);
}

GSForwardMailbox SoundChip_NeoGS::snapshotMailbox() const
{
    GSForwardMailbox snapshot = _mb;
    snapshot.counters = nullptr;
    return snapshot;
}

void SoundChip_NeoGS::restoreMailbox(const GSForwardMailbox& snapshot)
{
    _mb = snapshot;
    _mb.counters = &_activityCounters;
}

void SoundChip_NeoGS::accumulateActivityCounters(const GSActivityCounters& other)
{
    accumulateGSActivityCounters(_activityCounters, other);
}

bool SoundChip_NeoGS::captureModuleUpload(std::vector<uint8_t>& bytes, bool& playing) const
{
    return _upload.capture(bytes, playing);
}

void SoundChip_NeoGS::replayAdvanceFrame()
{
    runFor(GSHostClock::frameUnits(_context, TICKS_PER_SECOND));
}

void SoundChip_NeoGS::replayDrainReply()
{
    if (_mb.status & 0x80)
        (void)readData();
}

void SoundChip_NeoGS::replayModuleUpload(const std::vector<uint8_t>& bytes, bool startPlayback)
{
    size_t stalledAt = 0;
    switch (gsReplayModuleUpload(*this, bytes, startPlayback, &stalledAt))
    {
        case GSModuleReplayResult::NoFirmware:
            MLOGWARNING("NeoGS: personality switch cannot replay the module upload - no flash image");
            break;
        case GSModuleReplayResult::ByteStalled:
            MLOGWARNING("NeoGS: personality switch module replay stalled at byte %zu of %zu - firmware not draining",
                        stalledAt + 1, bytes.size());
            break;
        default:
            break;
    }
}

/// endregion </Personality switch>

/// region <TTD>

void SoundChip_NeoGS::serializeFixedState(uint8_t* dst) const
{
    memset(dst, 0, TTD_FIXED_STATE_SIZE);
    dst[0] = TTD_LAYOUT;
    dst[1] = _mb.status;
    dst[2] = _mb.dataFromHost;
    dst[3] = _mb.dataToHost;
    dst[4] = _mb.commandFromHost;
    dst[5] = _gscfg0;
    for (int i = 0; i < 4; i++)
        dst[6 + i] = _mem.page(i);
    dst[10] = _mem.lastMpag();
    dst[11] = _port09Bit5;
    dst[12] = _led;
    dst[13] = _irq.enable();
    dst[14] = _irq.readRequest();
    dst[15] = _irq.timFreq();
    for (int i = 0; i < 8; i++)
    {
        dst[16 + i] = _snd.sample(i);
        dst[24 + i] = _snd.volume(i);
    }
    dst[32] = _spi.sctrlRaw();
    for (int m = 0; m < 3; m++)
    {
        const NeoGSSpi::MasterState& s = _spi.state(static_cast<NeoGSSpi::Master>(m));
        uint8_t* at = dst + 33 + m * 11;
        put64(at, s.end);
        at[8] = s.tx;
        at[9] = s.rx;
        at[10] = s.pending ? 1 : 0;
    }
    // 66..78: DMA registers until layout 2; now in the DMA block
    dst[79] = static_cast<uint8_t>(_ticksPerCycle);
    dst[80] = static_cast<uint8_t>(_nextTicksPerCycle);
    put64(dst + 81, _phaseOrigin);
    put64(dst + 89, _timerStrobeAt);
    put64(dst + 97, _extraStrobeAt);
    put64(dst + 105, _nextDacCrystal);
    dst[113] = static_cast<uint8_t>((_nmiPending ? 1 : 0) | (_ready ? 2 : 0));
    dst[114] = static_cast<uint8_t>(_resetRequest);
    put64(dst + 115, _mainRomStartTicks);
    put64(dst + 123, _runner.now());
    put64(dst + 131, _runner.stallUntil());
    put64(dst + 139, _runner.now() - _frameStartTicks);
    put32(dst + 147, static_cast<uint32_t>(_frameStartZxTacts));
    put32(dst + 151, static_cast<uint32_t>(_frameTicks));
    put32(dst + 155, static_cast<uint32_t>(_outL));
    put32(dst + 159, static_cast<uint32_t>(_outR));

    Z80CpuRegisters regs{};
    Z80CpuGetRegisters(_cpu, &regs);
    uint8_t* z80 = dst + 163;
    put16(z80 + 0, regs.af);
    put16(z80 + 2, regs.bc);
    put16(z80 + 4, regs.de);
    put16(z80 + 6, regs.hl);
    put16(z80 + 8, regs.afAlt);
    put16(z80 + 10, regs.bcAlt);
    put16(z80 + 12, regs.deAlt);
    put16(z80 + 14, regs.hlAlt);
    put16(z80 + 16, regs.ix);
    put16(z80 + 18, regs.iy);
    put16(z80 + 20, regs.sp);
    put16(z80 + 22, regs.pc);
    put16(z80 + 24, regs.memptr);
    z80[26] = regs.i;
    z80[27] = regs.r;
    z80[28] = regs.q;
    z80[29] = regs.boundary;
    z80[30] = regs.iff1;
    z80[31] = regs.iff2;
    z80[32] = regs.im;
    z80[33] = regs.halted;
    z80[34] = regs.nmiInProgress; // 163..197

    _flash.saveState(dst + 200); // 200..231
    put64(dst + 232, _nextTimerCrystal);
}

void SoundChip_NeoGS::serializeDeviceState(uint8_t* dst, bool machineVisibleOnly) const
{
    // dst covers [TTD_SD_OFFSET, TTD_DEVICE_STATE_END)
    memset(dst, 0, TTD_DEVICE_STATE_END - TTD_SD_OFFSET);
    _sd->saveState(dst);
    if (_mp3)
        _mp3->saveState(dst + (TTD_MP3_OFFSET - TTD_SD_OFFSET), machineVisibleOnly);
    _dma.saveState(dst + (TTD_DMA_OFFSET - TTD_SD_OFFSET));
    _zx.saveState(dst + (TTD_ZX_OFFSET - TTD_SD_OFFSET));
}

size_t SoundChip_NeoGS::TTDStateSize() const
{
    // Registers and device state only. The card RAM (2-4 MB) and the flash
    // (512 KB) are not in TTD v1 checkpoints: large memories wait for TTD v2
    // memory regions. A restore therefore keeps the live card memory
    return TTD_DEVICE_STATE_END;
}

void SoundChip_NeoGS::TTDSaveState(uint8_t* dst) const
{
    serializeFixedState(dst);
    serializeDeviceState(dst + TTD_SD_OFFSET, false);
}

void SoundChip_NeoGS::TTDLoadState(const uint8_t* src)
{
    if (src[0] != TTD_LAYOUT)
    {
        MLOGWARNING("NeoGS: TTD state layout %u is not supported - card state left as is", src[0]);
        return;
    }
    _mb.status = src[1];
    _mb.dataFromHost = src[2];
    _mb.dataToHost = src[3];
    _mb.commandFromHost = src[4];
    _gscfg0 = src[5];
    _port09Bit5 = src[11];
    _led = src[12];
    _irq.setRaw(src[13], src[14]);
    _irq.setTimFreqRaw(src[15]);
    for (int i = 0; i < 8; i++)
    {
        _snd.setSampleRaw(i, src[16 + i]);
        _snd.setVolume(i, src[24 + i]);
    }
    _spi.setSctrlRaw(src[32]);
    for (int m = 0; m < 3; m++)
    {
        NeoGSSpi::MasterState& s = _spi.state(static_cast<NeoGSSpi::Master>(m));
        const uint8_t* at = src + 33 + m * 11;
        s.end = get64(at);
        s.tx = at[8];
        s.rx = at[9];
        s.pending = at[10] != 0;
    }
    _ticksPerCycle = src[79];
    _nextTicksPerCycle = src[80];
    _phaseOrigin = get64(src + 81);
    _timerStrobeAt = get64(src + 89);
    _extraStrobeAt = get64(src + 97);
    _nextDacCrystal = get64(src + 105);
    _nmiPending = (src[113] & 1) != 0;
    _ready = (src[113] & 2) != 0;
    _resetRequest = static_cast<ResetKind>(src[114]);
    _mainRomStartTicks = get64(src + 115);
    _runner.setNow(get64(src + 123));
    _runner.setStallUntil(get64(src + 131));
    _frameStartTicks = _runner.now() - get64(src + 139);
    _frameStartZxTacts = get32(src + 147);
    _frameTicks = static_cast<int64_t>(get32(src + 151));
    _outL = static_cast<int32_t>(get32(src + 155));
    _outR = static_cast<int32_t>(get32(src + 159));

    const uint8_t* z80 = src + 163;
    Z80CpuRegisters regs{};
    regs.af = get16(z80 + 0);
    regs.bc = get16(z80 + 2);
    regs.de = get16(z80 + 4);
    regs.hl = get16(z80 + 6);
    regs.afAlt = get16(z80 + 8);
    regs.bcAlt = get16(z80 + 10);
    regs.deAlt = get16(z80 + 12);
    regs.hlAlt = get16(z80 + 14);
    regs.ix = get16(z80 + 16);
    regs.iy = get16(z80 + 18);
    regs.sp = get16(z80 + 20);
    regs.pc = get16(z80 + 22);
    regs.memptr = get16(z80 + 24);
    regs.i = z80[26];
    regs.r = z80[27];
    regs.q = z80[28];
    regs.boundary = z80[29];
    regs.iff1 = z80[30];
    regs.iff2 = z80[31];
    regs.im = z80[32];
    regs.halted = z80[33];
    regs.nmiInProgress = z80[34];
    Z80CpuSetRegisters(_cpu, &regs);

    _flash.loadState(src + 200);
    _nextTimerCrystal = get64(src + 232);
    _sd->loadState(src + TTD_SD_OFFSET);
    if (_mp3)
        _mp3->loadState(src + TTD_MP3_OFFSET);
    _dma.loadState(src + TTD_DMA_OFFSET);

    const uint8_t pages[4] = {src[6], src[7], src[8], src[9]};
    _mem.setPagesRaw(pages, src[10], _gscfg0);

    // After the DMA registers: the ZX mode follows from them, and loading
    // re-selects the host memory interface
    _zx.loadState(src + TTD_ZX_OFFSET);

    _audio.setLevels(_outL, _outR);
    _audio.clear();
    _frameHadActivity = false;
    reschedule();
}

uint64_t SoundChip_NeoGS::TTDHashState() const
{
    std::vector<uint8_t> blob(TTD_DEVICE_STATE_END);
    serializeFixedState(blob.data());
    serializeDeviceState(blob.data() + TTD_SD_OFFSET, true);
    uint64_t hash = 14695981039346656037ull;
    for (const uint8_t byte : blob)
    {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

/// endregion </TTD>
