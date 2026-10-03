#include "soundchip_gs.h"

#include "emulator/sound/chips/gs/gscpuregisters.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "3rdparty/message-center/messagecenter.h"
#include "common/filehelper.h"
#include "emulator/emulatorcontext.h"
#include "emulator/cpu/z80.h"
#include "emulator/cpu/core.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"

/// region <Constructors / destructors>

SoundChip_GeneralSound::SoundChip_GeneralSound(EmulatorContext* context, size_t ramKB, size_t sampleRate)
    : _context(context)
    , _sampleRate(sampleRate)
    , _audio(static_cast<double>(GS_CLOCK_HZ), sampleRate)
{
    _logger = _context ? _context->pModuleLogger : nullptr;

    // Mailbox overflow drops land in this card's activity counters
    _mb.counters = &_activityCounters;

    // RAM is banked in 32 KB pairs; the original GS card range is 128-512 KB
    // (NeoGS lifts the cap - P2). Configured sizes outside the range clamp to
    // the nearest original-GS size.
    size_t pairs = ramKB / (RAM_PAIR_SIZE / 1024);
    pairs = std::clamp<size_t>(pairs, 4, 16);
    _ramPairMask = static_cast<uint8_t>(pairs - 1);
    _ram.assign(pairs * RAM_PAIR_SIZE, 0x00);

    _rom.assign(ROM_SIZE, 0x00);

    // Dedicated coprocessor wired to the static trampolines below - never the
    // main emulator Z80 (design §4.3: context hardwiring, debug traps)
    _cpu = Z80CpuCreate();
    Z80CpuSetMemoryBus(_cpu, &SoundChip_GeneralSound::gsMemRead, this, &SoundChip_GeneralSound::gsMemWrite, this);
    Z80CpuSetPortBus(_cpu, &SoundChip_GeneralSound::gsPortRead, this, &SoundChip_GeneralSound::gsPortWrite, this);
    Z80CpuSetIntVectorFn(_cpu, &SoundChip_GeneralSound::gsIntRead, this);
    _runner.bind(_cpu, this);

    reset();
}

SoundChip_GeneralSound::~SoundChip_GeneralSound()
{
    if (_cpu)
    {
        Z80CpuDestroy(_cpu);
        _cpu = nullptr;
    }
}

void SoundChip_GeneralSound::setSampleRate(size_t sampleRate)
{
    _sampleRate = sampleRate;
    _audio.setRates(static_cast<double>(GS_CLOCK_HZ), _sampleRate);
}

void SoundChip_GeneralSound::setSynthesisSuppressed(bool suppressed)
{
    if (_synthesisSuppressed == suppressed)
        return;
    _synthesisSuppressed = suppressed;
    if (!suppressed)
        _audio.clear();
}

/// endregion </Constructors / destructors>

/// region <Reset / ROM>

void SoundChip_GeneralSound::reset()
{
    resetCard();

    // Host mailbox and the external DAC/volume latches flip back to their
    // power-on state (resetCard - the #33 pulse - leaves them alone: they are
    // flip-flops outside the GS CPU, Unreal gsz80.cpp:555 keeps them too)
    _mb.resetAll();
    for (int i = 0; i < 4; i++)
    {
        _channelData[i] = 0x80; // Midpoint = silence
        _channelVol[i] = 0;
    }
    makeVolumeTable();

    _audio.reset();
    memset(_buffer, 0, _audioDescriptor.memoryBufferSizeInBytes);
    _frameHadActivity = false;
    _wasActive = false;
}

void SoundChip_GeneralSound::resetCard()
{
    if (_cpu)
        Z80CpuReset(_cpu);

    _mpag = 0;
    applyBanking();

    // Timing restarts from zero but keeps the ZX frame base: the following
    // flush re-executes the elapsed frame time from the reset state, exactly
    // like Unreal's "reset(); flush_gs_z80();" pair (out_gs #33 handler)
    _runner.reset();
    _gsCyclesAbs = 0;
    scheduleNextPeriod();
    _frameStartGsCycles = 0;
    _nmiPending = false;
    _intPending = false;

    // #33 reboots the firmware (POST), which resets CNTMOD to 0 - any
    // previously uploaded module is functionally gone even though its old
    // bytes still sit in GS RAM, so the handoff capture must forget it too
    _upload.clear();
}

void SoundChip_GeneralSound::hostReset()
{
    // GSReset=1 couples the card to the ZX reset line (Unreal z80.cpp:79
    // "if (conf.sound.gsreset) reset_gs();" - matching the ini comment
    // "reinit GS on reset"). GSReset=0 leaves the card running across a ZX
    // reset: it is a separate subsystem with its own reset line (#33)
    if (_context && _context->config.sound.gsreset != 0)
        reset();
}

void SoundChip_GeneralSound::loadROM(const std::string& romPath)
{
    _romLoaded = false;

    if (romPath.empty())
    {
        MLOGWARNING("GS: no ROM file configured - running with zeroed ROM");
        return;
    }

    // Path resolution follows ROM::LoadROM: working dir, then executable
    // path, then the resources path (macOS app bundles)
    std::string resolvedPath = FileHelper::NormalizePath(romPath);
    if (!FileHelper::FileExists(resolvedPath))
    {
        std::string executablePath = FileHelper::GetExecutablePath();
        resolvedPath = FileHelper::PathCombine(executablePath, resolvedPath);
        if (!FileHelper::FileExists(resolvedPath))
        {
            std::string resourcesPath = FileHelper::GetResourcesPath();
            resolvedPath = FileHelper::PathCombine(resourcesPath, romPath);
            if (!FileHelper::FileExists(resolvedPath))
            {
                MLOGWARNING("GS: ROM '%s' not found (tried working dir, executable and resources paths) - running with zeroed ROM",
                            FileHelper::PrintablePath(resolvedPath).c_str());
                return;
            }
        }
    }

    FILE* file = FileHelper::OpenFile(resolvedPath, "rb");
    if (!file)
    {
        MLOGWARNING("GS: unable to open ROM '%s' - running with zeroed ROM",
                    FileHelper::PrintablePath(resolvedPath).c_str());
        return;
    }

    size_t size = fread(_rom.data(), 1, ROM_SIZE, file);
    bool larger = (size == ROM_SIZE) && (fgetc(file) != EOF);
    fclose(file);

    if (size == 0)
    {
        MLOGWARNING("GS: ROM '%s' is empty - running with zeroed ROM",
                    FileHelper::PrintablePath(resolvedPath).c_str());
        return;
    }
    if (size != ROM_SIZE)
    {
        MLOGWARNING("GS: ROM '%s' unexpected size %zu (expected %zu) - loading what fits",
                    FileHelper::PrintablePath(resolvedPath).c_str(), size, ROM_SIZE);
    }
    else if (larger)
    {
        // bootGS.rom is a 512 KB NeoGS flash image - the original card sees
        // only its first 32 KB (design §7.2)
        MLOGWARNING("GS: ROM '%s' is larger than 32 KB (NeoGS flash image?) - using the first 32 KB",
                    FileHelper::PrintablePath(resolvedPath).c_str());
    }
    _romLoaded = true;
}

/// endregion </Reset / ROM>

/// region <Volume curve>

void SoundChip_GeneralSound::makeVolumeTable()
{
    // [SOUND] GSVol uses the shared 0-8192 ini volume scale (shipped configs
    // carry GSVol=8000, "max sound volume is 8192" - the same domain as
    // BeeperVol). Unreal make_gs_volume (gs.cpp:14-20) computes
    // gs_vol*i*63/4096 and the channel level math then divides by 256;
    // pre-scaling x4 (256*4 instead of 256) lands the result directly in the
    // int16 blip domain: _vfx[i] == gs_vol * i * 63 / 1024. GSVol=8192 peaks
    // at ~24000 (73% FS) after the stereo mix
    int gsVol = _context ? _context->config.sound.gs_vol : 8000;
    gsVol = std::clamp(gsVol, 0, 8192);

    for (int i = 0; i <= 0x40; i++)
        _vfx[i] = static_cast<uint32_t>(gsVol) * static_cast<uint32_t>(i) * 63u / 1024u;
}

/// endregion </Volume curve>

/// region <Lazy sync core>

int64_t SoundChip_GeneralSound::frameGsLength() const
{
    // One ZX frame is config.frame * hostMultiplier multiplied tacts; the GS
    // card clocks 12 MHz against the ZX base clock, so the multiplier cancels
    // and the GS frame length is turbo-invariant (design §2.4)
    return GSHostClock::frameUnits(_context, static_cast<double>(GS_CLOCK_HZ));
}

void SoundChip_GeneralSound::flush()
{
    int64_t target = 0;
    if (!GSHostClock::targetUnits(_context, static_cast<double>(GS_CLOCK_HZ), _frameStartZxTacts, _frameStartGsCycles, target))
        return; // ZX reset rewound the clock; wait for the next frame base
    runTo(target);
}

// Catch-up loop: GSCardRunner (gscardrunner.h), Unreal z80loop order. The
// hooks below are the classic card's part of it.

// NMI from #33 bit6: latched, delivered at the first instruction boundary
// that takes it (the core refuses one while a prefix chain is pending and
// right after another NMI acknowledge - the latch then stays)
void SoundChip_GeneralSound::onNmiAccepted()
{
    _nmiPending = false;
    _activityCounters.nmisAccepted++;
    traceEvent(GSTraceSide::Interrupt, 0, 0, false, 0, GSTraceFlags::kNmi);
}

// Level-held periodic INT (design §2.4, Xpeccy intrq |= Z80_INT): a boundary
// request stays asserted until the CPU accepts it, i.e. until IFF1 comes back
// on after the firmware ISR / masked stretches. Dropping unaccepted requests
// modulated the 37.5 kHz sample clock (740-767 accepted per 768 boundaries)
// and made steady tones float in pitch - root cause of the 2026-09-20 drift
// investigation
void SoundChip_GeneralSound::onIntAccepted()
{
    _intPending = false;
    _activityCounters.interruptsAccepted++;
    traceEvent(GSTraceSide::Interrupt, 0, 0, false);
}

// 37.5 kHz period boundary: assert the request, the overshoot stays in the
// next period. One flip-flop: a second boundary while the previous request is
// still pending merges into it - real hardware loses that sample too (the
// handler is genuinely slower than the period), so the coalesced counter is
// the honest place for it
void SoundChip_GeneralSound::runEvents(int64_t /*now*/)
{
    _activityCounters.interruptPeriods++;
    if (_intPending)
        _activityCounters.interruptsCoalesced++;
    else
        _intPending = true;
    _gsCyclesAbs += GS_CYCLES_PER_INT;
    scheduleNextPeriod();
}

/// endregion </Lazy sync core>

/// region <Diagnostics>

uint32_t SoundChip_GeneralSound::currentFrameNumber() const
{
    return _context ? static_cast<uint32_t>(_context->emulatorState.frame_counter) : 0;
}

void SoundChip_GeneralSound::traceEvent(GSTraceSide side, uint16_t port, uint8_t value, bool isOut, uint8_t channel, uint8_t extraFlags)
{
    if (!_portTrace.isCapturing())
        return;

    GSTraceEvent event;
    event.timestamp = totalGsCycles();
    event.frameNumber = currentFrameNumber();
    event.port = port;
    event.pc = getCPUReg(GSCpuRegister::PC);
    event.value = value;
    event.channel = channel;
    event.side = side;
    event.flags = static_cast<uint8_t>((isOut ? GSTraceFlags::kDirectionOut : 0) | extraFlags);
    _portTrace.record(event);
}

/// endregion </Diagnostics>

/// region <Frame lifecycle>

void SoundChip_GeneralSound::handleFrameStart()
{
    _frameHadActivity = false;

    // Frame-relative bases (Unreal init_gs_frame): the ZX tact counter and
    // the GS cycle accumulator are snapshotted so host-multiplier changes
    // take effect cleanly from the next frame
    _frameStartZxTacts = GSHostClock::currentZxTacts(_context, _frameStartZxTacts);
    _frameStartGsCycles = totalGsCycles();
    _frameGsCycles = frameGsLength();
}

void SoundChip_GeneralSound::handleFrameEnd(size_t expectedSamples)
{
    if (_frameGsCycles <= 0)
        return;

    // Catch-up: run the GS CPU to the end of the ZX frame
    runTo(_frameStartGsCycles + _frameGsCycles);

    // Actual samples for this frame - must match what SoundManager mixes
    // (accumulator count preferred, local rounding as fallback)
    int samplesThisFrame;
    if (expectedSamples > 0)
    {
        samplesThisFrame = static_cast<int>(expectedSamples);
    }
    else
    {
        samplesThisFrame = static_cast<int>(std::llround(
            static_cast<double>(_frameGsCycles) * static_cast<double>(_sampleRate) / static_cast<double>(GS_CLOCK_HZ)));
    }
    _audio.endFrame(_frameGsCycles, samplesThisFrame, _buffer);

    // Activity of this frame: the audio-settings LED and, held for a second,
    // the HUD nudge (SoundManager / AudioActivityIndicators)
    _wasActive = _frameHadActivity;
}

void SoundChip_GeneralSound::onEmulatorPaused()
{
    memset(_buffer, 0, _audioDescriptor.memoryBufferSizeInBytes);

    _wasActive = false;
}

/// endregion </Frame lifecycle>

/// region <Audio pipeline>

void SoundChip_GeneralSound::computeStereo(int32_t& outL, int32_t& outR) const
{
    // Per-channel level: centered sample scaled by the volume curve
    // (Unreal gsz80.cpp:251 - the +gs_vfx[33] DC term is dropped, our blip
    // pipeline is signed and silence stays at 0)
    int32_t v[4];
    for (int ch = 0; ch < 4; ch++)
    {
        int32_t centered = static_cast<int8_t>(static_cast<uint8_t>(_channelData[ch] - 0x80));
        v[ch] = centered * static_cast<int32_t>(_vfx[_channelVol[ch]]) / 256;
    }

    // Channels 1,2 -> left; 3,4 -> right, with 50% cross-feed
    // (Unreal gsz80.cpp:148-154)
    int32_t l = v[0] + v[1];
    int32_t r = v[2] + v[3];
    outL = (l + r / 2) / 2;
    outR = (r + l / 2) / 2;
}

void SoundChip_GeneralSound::emitSample()
{
    int32_t newL, newR;
    computeStereo(newL, newR);

    // Position inside the current frame, 12 MHz blip clock domain
    if (_audio.set(totalGsCycles() - _frameStartGsCycles, _frameGsCycles, newL, newR, !_synthesisSuppressed))
        _frameHadActivity = true;
}

/// endregion </Audio pipeline>

/// region <Host port interface (ZX side)>

uint8_t SoundChip_GeneralSound::portDeviceInMethod(uint16_t port)
{
    switch (port & 0x00FF)
    {
        case 0xB3: // GSDAT: bit7 clears, the GS->ZX byte returns
            flush();
            _mb.status &= 0x7F;  // Original Unreal: clear bit7 directly
            _activityCounters.hostDataRead++;
            traceEvent(GSTraceSide::Host, PORT_DATA, _mb.dataToHost, false);
            return _mb.dataToHost;
        case 0xBB: // GSCOM read: status, bits 1-6 read as 1 (pull-ups)
            flush();
            traceEvent(GSTraceSide::Host, PORT_COMMAND, _mb.status | 0x7E, false);
            return _mb.status | 0x7E;
        default:
            return 0xFF;
    }
}

void SoundChip_GeneralSound::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    switch (port & 0x00FF)
    {
        case 0x33: // GSCTR: bit7 = reset, bit6 = NMI (both discard nothing
            //       else - Unreal out_gs applies them before any flush)
            traceEvent(GSTraceSide::Host, PORT_CONTROL, value, true);
            if (value & 0x80)
            {
                resetCard();
                flush(); // re-execute the elapsed frame time from reset state
                return;
            }
            if (value & 0x40)
            {
                _nmiPending = true;
                flush();
                return;
            }
            return;
        // Exact original semantics (Unreal gsz80.cpp out_gs / ZXMAK2
        // GeneralSoundDevice.writeB3/writeBB): single latch + shared status
        // flip-flops, no queues. bit7 = data flip-flop (either direction),
        // bit0 = command flip-flop, cleared only by the card's RSCOM (0x05).
        case 0xB3: // GSDAT write: latch byte, raise bit7
            flush();
            traceEvent(GSTraceSide::Host, PORT_DATA, value, true);
            onHostDataWrite(value);
            return;
        case 0xBB: // GSCOM write: latch command, raise bit0
            flush();
            traceEvent(GSTraceSide::Host, PORT_COMMAND, value, true);
            onHostCommandWrite(value);
            return;
        default:
            return;
    }
}

// Shared latch + v1 module handoff capture (host-port layer, personality-
// agnostic: mirrors the raw COM30..D2 payload stream as it goes by,
// independent of where the firmware parks it in GS RAM) - used by both the
// ZX-side ports and the automation sendData/sendCommand actions
void SoundChip_GeneralSound::onHostDataWrite(uint8_t value)
{
    _activityCounters.hostDataWritten++;
    _mb.dataFromHost = value;
    _mb.status |= 0x80;
    _upload.onData(value, _ram.size());
}

void SoundChip_GeneralSound::onHostCommandWrite(uint8_t value)
{
    _activityCounters.hostCommandsReceived++;
    _mb.commandFromHost = value;
    _mb.status |= 0x01;
    _upload.onCommand(value);
}

// Automation actions - same side effects as the ZX-side hardware ports

uint8_t SoundChip_GeneralSound::readStatus()
{
    flush();
    return _mb.status | 0x7E;
}

uint8_t SoundChip_GeneralSound::readData()
{
    flush();
    _mb.status &= 0x7F; // IN #B3: clear bit7
    return _mb.dataToHost;
}

void SoundChip_GeneralSound::sendCommand(uint8_t command)
{
    flush();
    onHostCommandWrite(command);
}

void SoundChip_GeneralSound::sendData(uint8_t data)
{
    flush();
    onHostDataWrite(data);
}

void SoundChip_GeneralSound::triggerNMI()
{
    _nmiPending = true;
    flush();
}

/// endregion </Host port interface>

/// region <Runtime personality switch (SoundManager::switchGeneralSoundCard)>

GSForwardMailbox SoundChip_GeneralSound::snapshotMailbox() const
{
    GSForwardMailbox snapshot = _mb;
    snapshot.counters = nullptr; // owner back-pointer, not protocol state
    return snapshot;
}

void SoundChip_GeneralSound::restoreMailbox(const GSForwardMailbox& snapshot)
{
    _mb = snapshot;
    _mb.counters = &_activityCounters; // rebind drop accounting to this card
}

void SoundChip_GeneralSound::accumulateActivityCounters(const GSActivityCounters& other)
{
    accumulateGSActivityCounters(_activityCounters, other);
}

bool SoundChip_GeneralSound::captureModuleUpload(std::vector<uint8_t>& bytes, bool& playing) const
{
    return _upload.capture(bytes, playing);
}

void SoundChip_GeneralSound::replayDrainReply()
{
    // Switch-internal consumption of a card->host byte (the COM30/COM31
    // acks the replay itself produces) - keeps bit7 clean for the mailbox
    // snapshot restored afterwards. Only call when no host->card byte is
    // in flight: bit7 is shared by both directions.
    if (_mb.status & 0x80)
        (void)readData();
}

void SoundChip_GeneralSound::replayAdvanceFrame()
{
    // One ZX frame of GS card time through the same lazy-sync core the
    // frame boundary uses: the firmware's COMINT/WTDTL loops drain the
    // FIFOs while the ZX clock stands still. The card ends up ahead of
    // the ZX clock - harmless: emitSample synthesizes nothing until the
    // first handleFrameStart sets the frame bases, and runTo targets
    // re-derive from those bases afterwards
    runTo(totalGsCycles() + frameGsLength());
}

void SoundChip_GeneralSound::replayModuleUpload(const std::vector<uint8_t>& bytes, bool startPlayback)
{
    size_t stalledAt = 0;
    switch (gsReplayModuleUpload(*this, bytes, startPlayback, &stalledAt))
    {
        case GSModuleReplayResult::NoFirmware:
            MLOGWARNING("GS: personality switch cannot replay the module upload - no firmware ROM");
            break;
        case GSModuleReplayResult::ByteStalled:
            MLOGWARNING("GS: personality switch module replay stalled at byte %zu of %zu - firmware not draining",
                        stalledAt + 1, bytes.size());
            break;
        default:
            break;
    }
}

/// endregion </Runtime personality switch>

/// region <GS-side ports (internal Z80, 0x00-0x0B)>

uint8_t SoundChip_GeneralSound::gsIn(uint16_t port)
{
    traceEvent(GSTraceSide::GsInternal, port & 0x00FF, 0, false);
    switch (port & 0x00FF)
    {
        // Exact original semantics (Unreal gsz80.cpp z80gs::in / ZXMAK2):
        case 0x01: return _mb.commandFromHost; // COMRG: no flag change (RSCOM 0x05 clears bit0)
        case 0x02: _mb.status &= 0x7F; return _mb.dataFromHost; // DATRG: clear bit7, return host byte
        case 0x03: _mb.status |= 0x80; _mb.dataToHost = 0xFF; return 0xFF; // OUTRG read: set bit7
        case 0x04: return _mb.status; // FLAGS
        case 0x05: _mb.status &= 0xFE; return 0xFF; // RSCOM: clear bit0
        // gspage in the original is rol8(MPAG,1), so (gspage<<7)&0x80 == raw MPAG bit7
        case 0x0A: applyPort0A(); return 0xFF;
        case 0x0B: _mb.status = (_mb.status & 0xFE) | ((_channelVol[0] >> 5) & 1); return 0xFF;
        default: return 0xFF; // NGS ports (P2) and unmapped read open bus
    }
}

void SoundChip_GeneralSound::gsOut(uint16_t port, uint8_t value)
{
    traceEvent(GSTraceSide::GsInternal, port & 0x00FF, value, true);
    switch (port & 0x00FF)
    {
        case 0x00: // MPAG - firmware/Xpeccy encoding (design §2.3)
            _mpag = value;
            applyBanking();
            return;
        case 0x02: _mb.status &= 0x7F; return; // DATRG write: clear bit7
        case 0x03: _mb.status |= 0x80; _mb.dataToHost = value; return; // OUTRG: set bit7, latch reply
        case 0x05: _mb.status &= 0xFE; return; // RSCOM: clear bit0
        case 0x06:
        case 0x07:
        case 0x08:
        case 0x09:
        {
            // Volume latches: 6-bit, channel = low nibble - 6 (Unreal gsz80.cpp:353)
            int channel = (port & 0x0F) - 6;
            _channelVol[channel] = value & 0x3F;
            _activityCounters.volumeLatchWrites++;
            emitSample(); // level change - emit at the current position
            return;
        }
        case 0x0A: applyPort0A(); return;
        case 0x0B: _mb.status = (_mb.status & 0xFE) | ((_channelVol[0] >> 5) & 1); return;
        default: return;
    }
}

/// endregion </GS-side ports>

/// Port 0x0A (read or write): status bit 7 <- NOT bit 0 of the page register.
/// GS port document (GS_PORTS.TXT): "port A sets status bit D7 not equal to
/// bit D0 of port 0"; Xpeccy gs.c agrees. Neither firmware uses the port.
void SoundChip_GeneralSound::applyPort0A()
{
    _mb.status = static_cast<uint8_t>((_mb.status & 0x7F) | ((~_mpag & 0x01) << 7));
}

/// region <Memory subsystem>

void SoundChip_GeneralSound::applyBanking()
{
    // Window 0: ROM page 0 (writes discarded); window 1: fixed RAM page
    // FIXED_WINDOW_RAM_PAGE = upper half of MPAG 1 (the DAC sample buffers
    // 0x6000-0x7FFF live in its upper half)
    _bankR[0] = _rom.data();
    _bankW[0] = nullptr;
    _bankR[1] = _ram.data() + FIXED_WINDOW_RAM_PAGE * PAGE_SIZE;
    _bankW[1] = _ram.data() + FIXED_WINDOW_RAM_PAGE * PAGE_SIZE;

    if (_mpag == 0)
    {
        // V == 0 -> ROM pair: window 2 = ROM page 0, window 3 = ROM page 1
        _bankR[2] = _rom.data();
        _bankW[2] = nullptr;
        _bankR[3] = _rom.data() + PAGE_SIZE;
        _bankW[3] = nullptr;
    }
    else
    {
        // V >= 1 -> RAM pair (V-1), masked to installed RAM (design §2.3)
        size_t pair = (_mpag - 1) & _ramPairMask;
        uint8_t* pairBase = _ram.data() + pair * RAM_PAIR_SIZE;
        _bankR[2] = pairBase;
        _bankW[2] = pairBase;
        _bankR[3] = pairBase + PAGE_SIZE;
        _bankW[3] = pairBase + PAGE_SIZE;
    }
}

uint8_t SoundChip_GeneralSound::readMem(uint16_t addr)
{
    const uint8_t* bank = _bankR[(addr >> 14) & 3];
    uint8_t value = bank[addr & (PAGE_SIZE - 1)];
    // The window test stays here, inline on every read; the latch itself is
    // out of line (rare: a few thousand reads per frame)
    if ((addr & 0xE000) == 0x6000)
        dacFetch(addr, value);
    return value;
}

void SoundChip_GeneralSound::writeMem(uint16_t addr, uint8_t value)
{
    uint8_t* bank = _bankW[(addr >> 14) & 3];
    if (bank) // nullptr = ROM window: write goes nowhere
    {
        bank[addr & (PAGE_SIZE - 1)] = value;
        if (_ramTrackerArmed)
            _ramTrackerArmed->Mark(static_cast<size_t>(bank - _ram.data()) + (addr & (PAGE_SIZE - 1)));
    }
}

void SoundChip_GeneralSound::dacFetch(uint16_t addr, uint8_t value)
{
    // Any read in 0x6000-0x7FFF latches the byte into the DAC selected by
    // address bits 9-8 (design §2.1) - including opcode fetches, matching
    // Unreal's rm() hook. readMem has already tested the window.
    int channel = (addr >> 8) & 3;
    _channelData[channel] = value;
    _activityCounters.dacFetches++;
    _activityCounters.lastDacFetchGsCycle = totalGsCycles();
    _activityCounters.lastDacFetchFrame = currentFrameNumber();
    traceEvent(GSTraceSide::DacFetch, addr, value, false, static_cast<uint8_t>(channel));
    emitSample();
}

/// endregion </Memory subsystem>

/// region <Z80 bus callbacks>

uint8_t SoundChip_GeneralSound::gsMemRead(Z80CPU* /*cpu*/, uint16_t addr, int /*m1State*/, void* userData)
{
    return static_cast<SoundChip_GeneralSound*>(userData)->readMem(addr);
}

void SoundChip_GeneralSound::gsMemWrite(Z80CPU* /*cpu*/, uint16_t addr, uint8_t value, void* userData)
{
    static_cast<SoundChip_GeneralSound*>(userData)->writeMem(addr, value);
}

uint8_t SoundChip_GeneralSound::gsPortRead(Z80CPU* /*cpu*/, uint16_t port, void* userData)
{
    return static_cast<SoundChip_GeneralSound*>(userData)->gsIn(port);
}

void SoundChip_GeneralSound::gsPortWrite(Z80CPU* /*cpu*/, uint16_t port, uint8_t value, void* userData)
{
    static_cast<SoundChip_GeneralSound*>(userData)->gsOut(port, value);
}

uint8_t SoundChip_GeneralSound::gsIntRead(Z80CPU* /*cpu*/, void* /*userData*/)
{
    return 0xFF; // IM2 vector: nothing drives the GS data bus during INTA
}

uint16_t SoundChip_GeneralSound::getCPUReg(GSCpuRegister reg) const
{
    return gsReadCpuRegister(_cpu, reg);
}

/// endregion </Z80 bus callbacks>

/// region <TTDSerializable (P1.5 - parent TDD 6.4)>

namespace
{
// Little-endian fixed-width helpers: the blob must be portable across
// platforms, so every field is written byte by byte
void gsTtdWrite16(uint8_t* dst, uint16_t value)
{
    dst[0] = static_cast<uint8_t>(value);
    dst[1] = static_cast<uint8_t>(value >> 8);
}

uint16_t gsTtdRead16(const uint8_t* src)
{
    return static_cast<uint16_t>(src[0] | (static_cast<uint16_t>(src[1]) << 8));
}

void gsTtdWrite64(uint8_t* dst, int64_t value)
{
    uint64_t raw = static_cast<uint64_t>(value);
    for (int i = 0; i < 8; i++)
        dst[i] = static_cast<uint8_t>(raw >> (8 * i));
}

int64_t gsTtdRead64(const uint8_t* src)
{
    uint64_t raw = 0;
    for (int i = 7; i >= 0; i--)
        raw = (raw << 8) | src[i];
    return static_cast<int64_t>(raw);
}

void gsTtdWrite32(uint8_t* dst, uint32_t value)
{
    for (int i = 0; i < 4; i++)
        dst[i] = static_cast<uint8_t>(value >> (8 * i));
}

uint32_t gsTtdRead32(const uint8_t* src)
{
    uint32_t raw = 0;
    for (int i = 3; i >= 0; i--)
        raw = (raw << 8) | src[i];
    return raw;
}
} // namespace

void SoundChip_GeneralSound::serializeFixedState(uint8_t* dst) const
{
    dst[0] = _mb.status;
    dst[1] = _mb.dataFromHost;
    dst[2] = _mb.dataToHost;
    dst[3] = _mb.commandFromHost;
    dst[4] = _mpag;
    memcpy(&dst[5], _channelVol, 4);
    memcpy(&dst[9], _channelData, 4);
    gsTtdWrite64(&dst[13], _gsCyclesAbs);
    gsTtdWrite16(&dst[21], static_cast<uint16_t>(static_cast<int16_t>(totalGsCycles() - _gsCyclesAbs)));
    // bits 2/3 were the queue-era pending flags; _mb.status (dst[0]) is
    // authoritative now, the slots stay reserved for layout compatibility
    dst[23] = static_cast<uint8_t>((_nmiPending ? 1 : 0) | (_intPending ? 2 : 0));

    // The complete CPU state at an instruction boundary: registers plus the
    // boundary state (EI shadow, a pending prefix after a redundant one, the
    // LD A,I/R quirk, a just-taken NMI) and the NMI session flag - a restore
    // anywhere continues exactly like the original (Z80CpuRegisters contract)
    Z80CpuRegisters regs{};
    Z80CpuGetRegisters(_cpu, &regs);
    uint8_t* z80 = &dst[24];
    gsTtdWrite16(z80 + 0, regs.af);
    gsTtdWrite16(z80 + 2, regs.bc);
    gsTtdWrite16(z80 + 4, regs.de);
    gsTtdWrite16(z80 + 6, regs.hl);
    gsTtdWrite16(z80 + 8, regs.afAlt);
    gsTtdWrite16(z80 + 10, regs.bcAlt);
    gsTtdWrite16(z80 + 12, regs.deAlt);
    gsTtdWrite16(z80 + 14, regs.hlAlt);
    gsTtdWrite16(z80 + 16, regs.ix);
    gsTtdWrite16(z80 + 18, regs.iy);
    gsTtdWrite16(z80 + 20, regs.sp);
    gsTtdWrite16(z80 + 22, regs.pc);
    gsTtdWrite16(z80 + 24, regs.memptr);
    z80[26] = regs.i;
    z80[27] = regs.r;  // with R7
    z80[28] = regs.q;
    z80[29] = regs.boundary;
    z80[30] = regs.iff1;
    z80[31] = regs.iff2;
    z80[32] = regs.im;
    z80[33] = regs.halted;
    z80[34] = regs.nmiInProgress;

    // dst[59..94]: queue-era slots, reserved (zero) for layout compatibility
    std::fill_n(&dst[59], 36, static_cast<uint8_t>(0));

    // dst[77..88]: the current frame's timeline (handleFrameStart bases). A
    // checkpoint is taken inside a started frame - at its first instruction
    // for per-frame checkpoints, anywhere for a recording's baseline - so the
    // bases are state, not derivable from the position alone
    gsTtdWrite32(&dst[77], static_cast<uint32_t>(totalGsCycles() - _frameStartGsCycles));
    gsTtdWrite32(&dst[81], static_cast<uint32_t>(_frameStartZxTacts));
    gsTtdWrite32(&dst[85], static_cast<uint32_t>(_frameGsCycles));
}

size_t SoundChip_GeneralSound::TTDStateSize() const
{
    return TTD_FIXED_STATE_SIZE + _ram.size();
}

void SoundChip_GeneralSound::TTDSaveState(uint8_t* dst) const
{
    serializeFixedState(dst);
    memcpy(dst + TTD_FIXED_STATE_SIZE, _ram.data(), _ram.size());
}

void SoundChip_GeneralSound::TTDLoadState(const uint8_t* src)
{
    loadFixedState(src);
    memcpy(_ram.data(), src + TTD_FIXED_STATE_SIZE, _ram.size());
    if (_ramTrackerArmed)
        _ramTrackerArmed->MarkAll();
    finishLoad(src);
}

bool SoundChip_GeneralSound::TTDLoadStateWithoutRegions(const uint8_t* state, size_t size)
{
    // The time-travel engine restored the RAM as its region already
    if (size != TTD_FIXED_STATE_SIZE)
        return false;
    loadFixedState(state);
    finishLoad(state);
    return true;
}

void SoundChip_GeneralSound::loadFixedState(const uint8_t* src)
{
    _mb.status = src[0];
    _mb.dataFromHost = src[1];
    _mb.dataToHost = src[2];
    _mb.commandFromHost = src[3];
    _mpag = src[4];
    memcpy(_channelVol, &src[5], 4);
    memcpy(_channelData, &src[9], 4);
    _gsCyclesAbs = gsTtdRead64(&src[13]);
    _runner.setNow(_gsCyclesAbs + static_cast<int16_t>(gsTtdRead16(&src[21])));
    scheduleNextPeriod();
    _nmiPending = (src[23] & 1) != 0; // bit1 = intPending (pre-level-hold captures: 0/1 only)
    _intPending = (src[23] & 2) != 0;
    // bits 2/3: queue-era pending flags, ignored - _mb.status is authoritative

    const uint8_t* z80 = &src[24];
    Z80CpuRegisters regs{};
    regs.af = gsTtdRead16(z80 + 0);
    regs.bc = gsTtdRead16(z80 + 2);
    regs.de = gsTtdRead16(z80 + 4);
    regs.hl = gsTtdRead16(z80 + 6);
    regs.afAlt = gsTtdRead16(z80 + 8);
    regs.bcAlt = gsTtdRead16(z80 + 10);
    regs.deAlt = gsTtdRead16(z80 + 12);
    regs.hlAlt = gsTtdRead16(z80 + 14);
    regs.ix = gsTtdRead16(z80 + 16);
    regs.iy = gsTtdRead16(z80 + 18);
    regs.sp = gsTtdRead16(z80 + 20);
    regs.pc = gsTtdRead16(z80 + 22);
    regs.memptr = gsTtdRead16(z80 + 24);
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

    // src[59..94]: queue-era slots, ignored
}

void SoundChip_GeneralSound::finishLoad(const uint8_t* src)
{
    applyBanking();

    // Host-side pipeline follows the restored levels without emitting a
    // step (the seek position already produced its own audio)
    int32_t levelL, levelR;
    computeStereo(levelL, levelR);
    _audio.setLevels(levelL, levelR);
    _audio.clear();
    _frameHadActivity = false;

    // The frame timeline the checkpoint was taken in (see serializeFixedState)
    _frameStartGsCycles = totalGsCycles() - static_cast<int64_t>(gsTtdRead32(&src[77]));
    _frameStartZxTacts = gsTtdRead32(&src[81]);
    _frameGsCycles = static_cast<int64_t>(gsTtdRead32(&src[85]));
}

uint64_t SoundChip_GeneralSound::TTDHashState() const
{
    // FNV-1a over the fixed part only: hashing up to 512 KB of RAM per frame
    // would dominate the TTD capture cost, while mailbox + banking + CPU
    // state already pin the deterministic trajectory (audio latches feed the
    // hash via _channelVol/_channelData)
    uint8_t blob[TTD_FIXED_STATE_SIZE];
    serializeFixedState(blob);

    uint64_t hash = 14695981039346656037ull; // FNV-1a offset basis
    for (size_t i = 0; i < TTD_FIXED_STATE_SIZE; i++)
    {
        hash ^= blob[i];
        hash *= 1099511628211ull; // FNV prime
    }
    return hash;
}

/// endregion </TTDSerializable>

/// region <Time-travel engine region>

void SoundChip_GeneralSound::TTDRegions(std::vector<ttd::TTDDeviceRegion>& out)
{
    _ramTracker.Bind(_ram.data(), _ram.size());
    ttd::TTDDeviceRegion ram;
    ram.desc.id = ttd::TTDRegionId::GeneralSoundRam;
    ram.desc.name = "gs.ram";
    ram.desc.ownerType = static_cast<uint16_t>(ttd::PeripheralId::GeneralSound);
    ram.desc.memory = _ram.data();
    ram.desc.bytes = static_cast<uint32_t>(_ram.size());
    ram.desc.pieces = _ramTracker.Pieces();
    ram.tracker = &_ramTracker;
    out.push_back(ram);
}

bool SoundChip_GeneralSound::TTDStateWithoutRegions(uint8_t& peripheralId, std::vector<uint8_t>& state) const
{
    peripheralId = static_cast<uint8_t>(ttd::PeripheralId::GeneralSound);
    state.resize(TTD_FIXED_STATE_SIZE);
    serializeFixedState(state.data());
    return true;
}

/// endregion </Time-travel engine region>
