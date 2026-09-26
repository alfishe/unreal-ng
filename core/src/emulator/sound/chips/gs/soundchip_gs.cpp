#include "soundchip_gs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "3rdparty/blip_buf/blip_buf.h"
#include "3rdparty/message-center/messagecenter.h"
#include "3rdparty/z80ex/typedefs.h" // full Z80EX_CONTEXT layout for TTD field access
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

    _blipL = blip_new(MAX_SAMPLES_PER_FRAME + 64);
    _blipR = blip_new(MAX_SAMPLES_PER_FRAME + 64);
    blip_set_rates(_blipL, static_cast<double>(GS_CLOCK_HZ), static_cast<double>(_sampleRate));
    blip_set_rates(_blipR, static_cast<double>(GS_CLOCK_HZ), static_cast<double>(_sampleRate));

    // Dedicated coprocessor wired to the static trampolines below - never the
    // main emulator Z80 (design §4.3: context hardwiring, debug traps)
    _cpu = z80ex_create(&SoundChip_GeneralSound::gsMemRead, this,
                        &SoundChip_GeneralSound::gsMemWrite, this,
                        &SoundChip_GeneralSound::gsPortRead, this,
                        &SoundChip_GeneralSound::gsPortWrite, this,
                        &SoundChip_GeneralSound::gsIntRead, this);

    reset();
}

SoundChip_GeneralSound::~SoundChip_GeneralSound()
{
    if (_cpu)
    {
        z80ex_destroy(_cpu);
        _cpu = nullptr;
    }
    blip_delete(_blipL);
    blip_delete(_blipR);
    _blipL = nullptr;
    _blipR = nullptr;
}

void SoundChip_GeneralSound::setSampleRate(size_t sampleRate)
{
    _sampleRate = sampleRate;
    blip_set_rates(_blipL, static_cast<double>(GS_CLOCK_HZ), static_cast<double>(_sampleRate));
    blip_set_rates(_blipR, static_cast<double>(GS_CLOCK_HZ), static_cast<double>(_sampleRate));
    if (_blipL) blip_clear(_blipL);
    if (_blipR) blip_clear(_blipR);
}

void SoundChip_GeneralSound::setSynthesisSuppressed(bool suppressed)
{
    if (_synthesisSuppressed == suppressed)
        return;
    _synthesisSuppressed = suppressed;
    if (!suppressed)
    {
        if (_blipL) blip_clear(_blipL);
        if (_blipR) blip_clear(_blipR);
    }
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

    _lastL = 0;
    _lastR = 0;
    if (_blipL) blip_clear(_blipL);
    if (_blipR) blip_clear(_blipR);
    memset(_buffer, 0, _audioDescriptor.memoryBufferSizeInBytes);
    _frameHadActivity = false;
    _wasActive = false;
}

void SoundChip_GeneralSound::resetCard()
{
    if (_cpu)
        z80ex_reset(_cpu);

    _mpag = 0;
    applyBanking();

    // Timing restarts from zero but keeps the ZX frame base: the following
    // flush re-executes the elapsed frame time from the reset state, exactly
    // like Unreal's "reset(); flush_gs_z80();" pair (out_gs #33 handler)
    _gsCyclesAbs = 0;
    _intQuantum = 0;
    _frameStartGsCycles = 0;
    _nmiPending = false;
    _intPending = false;

    // #33 reboots the firmware (POST), which resets CNTMOD to 0 - any
    // previously uploaded module is functionally gone even though its old
    // bytes still sit in GS RAM, so the handoff capture must forget it too
    _uploadStore.clear();
    _uploadLive = false;
    _uploadHadModule = false;
    _uploadPlaying = false;
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

double SoundChip_GeneralSound::gsCyclesPerZxTact() const
{
    if (!_context)
        return static_cast<double>(GS_CLOCK_HZ) / static_cast<double>(CPU_CLOCK_RATE);

    const CONFIG& config = _context->config;
    if (config.frame == 0 || config.frame_duration_us == 0)
        return static_cast<double>(GS_CLOCK_HZ) / static_cast<double>(CPU_CLOCK_RATE);

    // ZX base clock from the configured frame geometry (design §2.4); the GS
    // card keeps its own 12 MHz clock, so only the HOST speed multiplier
    // stretches the ZX tact domain (hardware turbo is already descaled by
    // AudioTstate - both multipliers cancel, see frameGsLength)
    double zxBaseHz = static_cast<double>(config.frame) / (static_cast<double>(config.frame_duration_us) * 1e-6);
    double hostMultiplier = static_cast<double>(_context->emulatorState.HostSpeedMultiplier());

    return static_cast<double>(GS_CLOCK_HZ) / (zxBaseHz * hostMultiplier);
}

uint64_t SoundChip_GeneralSound::currentZxTacts() const
{
    if (_context && _context->pCore && _context->pCore->GetZ80())
        return static_cast<uint64_t>(_context->emulatorState.AudioTstate(_context->pCore->GetZ80()->t));

    return _frameStartZxTacts;
}

int64_t SoundChip_GeneralSound::frameGsLength() const
{
    if (!_context)
        return 0;

    // One ZX frame is config.frame * hostMultiplier multiplied tacts; the GS
    // card clocks 12 MHz against the ZX base clock, so the multiplier cancels
    // and the GS frame length is turbo-invariant (design §2.4)
    double tacts = static_cast<double>(_context->config.frame) * static_cast<double>(_context->emulatorState.HostSpeedMultiplier());
    return static_cast<int64_t>(std::llround(tacts * gsCyclesPerZxTact()));
}

void SoundChip_GeneralSound::flush()
{
    uint64_t zxTacts = currentZxTacts();
    if (zxTacts < _frameStartZxTacts)
        return; // ZX reset rewound the clock; wait for the next frame base

    uint64_t relative = zxTacts - _frameStartZxTacts;
    int64_t target = _frameStartGsCycles + static_cast<int64_t>(std::llround(static_cast<double>(relative) * gsCyclesPerZxTact()));
    runTo(target);
}

void SoundChip_GeneralSound::runTo(int64_t target)
{
    // Unreal z80loop model (gsz80.inl:39-82): step instructions while inside
    // the 320-cycle quantum; at each boundary assert the periodic interrupt
    // and carry the overshoot into the next quantum
    while (totalGsCycles() < target)
    {
        // NMI from #33 bit6: latched, delivered at the first instruction
        // boundary (z80ex refuses it mid-prefix - fall through and step)
        if (_nmiPending)
        {
            int t = z80ex_nmi(_cpu);
            if (t > 0)
            {
                _nmiPending = false;
                _intQuantum = static_cast<int16_t>(_intQuantum + t);
                _activityCounters.nmisAccepted++;
                traceEvent(GSTraceSide::Interrupt, 0, 0, false, 0, GSTraceFlags::kNmi);
                continue;
            }
        }

        // Level-held periodic INT (design §2.4, Xpeccy intrq |= Z80_INT): a
        // boundary request stays asserted until the CPU accepts it, i.e.
        // until IFF1 comes back on after the firmware ISR / masked stretches.
        // Dropping unaccepted requests modulated the 37.5 kHz sample clock
        // (740-767 accepted per 768 boundaries) and made steady tones float
        // in pitch - root cause of the 2026-09-20 drift investigation
        if (_intPending)
        {
            int t = z80ex_int(_cpu);
            if (t > 0)
            {
                _intPending = false;
                _intQuantum = static_cast<int16_t>(_intQuantum + t);
                _activityCounters.interruptsAccepted++;
                traceEvent(GSTraceSide::Interrupt, 0, 0, false);
            }
        }

        if (_intQuantum >= GS_CYCLES_PER_INT)
        {
            // 37.5 kHz quantum boundary: assert the request, carry the
            // overshoot into the next quantum. One flip-flop: a second
            // boundary while the previous request is still pending merges
            // into it - real hardware loses that sample too (the handler is
            // genuinely slower than the period), so the coalesced counter
            // is the honest place for it
            _activityCounters.interruptPeriods++;
            if (_intPending)
                _activityCounters.interruptsCoalesced++;
            else
                _intPending = true;
            _intQuantum = static_cast<int16_t>(_intQuantum - GS_CYCLES_PER_INT);
            _gsCyclesAbs += GS_CYCLES_PER_INT;
            continue;
        }

        int t = z80ex_step(_cpu);
        if (t <= 0)
            break; // Defensive: a stuck core must not hang the emulator
        _intQuantum = static_cast<int16_t>(_intQuantum + t);
        _activityCounters.cpuSteps++;
    }
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
    event.pc = getCPUReg(regPC);
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
    _frameStartZxTacts = currentZxTacts();
    _frameStartGsCycles = totalGsCycles();
    _frameGsCycles = frameGsLength();
}

void SoundChip_GeneralSound::handleFrameEnd(size_t expectedSamples)
{
    if (_frameGsCycles <= 0)
        return;

    // Catch-up: run the GS CPU to the end of the ZX frame
    runTo(_frameStartGsCycles + _frameGsCycles);

    blip_end_frame(_blipL, static_cast<unsigned>(_frameGsCycles));
    blip_end_frame(_blipR, static_cast<unsigned>(_frameGsCycles));

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
    samplesThisFrame = std::clamp(samplesThisFrame, 0, static_cast<int>(MAX_SAMPLES_PER_FRAME));

    int samplesL = blip_read_samples(_blipL, &_buffer[0], samplesThisFrame, 1 /* stereo stride */);
    int samplesR = blip_read_samples(_blipR, &_buffer[1], samplesThisFrame, 1 /* stereo stride */);

    for (int i = samplesL; i < samplesThisFrame; i++)
        _buffer[i * 2] = 0;
    for (int i = samplesR; i < samplesThisFrame; i++)
        _buffer[i * 2 + 1] = 0;

    // Post notification while active (to refresh HUD TTL) or on state change
    if (_frameHadActivity || _frameHadActivity != _wasActive)
    {
        _wasActive = _frameHadActivity;
        MessageCenter::DefaultMessageCenter().Post(
            NC_AUDIO_ACTIVITY, new AudioActivityPayload(_context->emulatorId, AudioSource::GeneralSound, _wasActive));
    }
}

void SoundChip_GeneralSound::onEmulatorPaused()
{
    memset(_buffer, 0, _audioDescriptor.memoryBufferSizeInBytes);

    if (_wasActive)
    {
        _wasActive = false;
        MessageCenter::DefaultMessageCenter().Post(
            NC_AUDIO_ACTIVITY, new AudioActivityPayload(_context->emulatorId, AudioSource::GeneralSound, false));
    }
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

    if (!_synthesisSuppressed && _frameGsCycles > 0)
    {
        int32_t deltaL = newL - _lastL;
        int32_t deltaR = newR - _lastR;

        if (deltaL != 0 || deltaR != 0)
        {
            // Position inside the current frame, 12 MHz blip clock domain;
            // clamped to the frame length (turbo switches mid-frame)
            int64_t position = totalGsCycles() - _frameStartGsCycles;
            int64_t clamped = std::clamp<int64_t>(position, 0, _frameGsCycles - 1);

            if (deltaL != 0)
                blip_add_delta(_blipL, static_cast<unsigned>(clamped), deltaL);
            if (deltaR != 0)
                blip_add_delta(_blipR, static_cast<unsigned>(clamped), deltaR);
            _frameHadActivity = true;
        }
    }

    _lastL = newL;
    _lastR = newR;
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
    // The dummy slot byte precedes the OUT #BB,0x30 that opens capture, so
    // it never enters the store. Capped at the card's actual RAM size: the
    // firmware has nowhere else to put more bytes than that either, and
    // without the cap a host stream that never sends the terminating D2
    // would grow this vector without bound.
    if (_uploadLive && _uploadStore.size() < _ram.size())
        _uploadStore.push_back(value);
}

void SoundChip_GeneralSound::onHostCommandWrite(uint8_t value)
{
    _activityCounters.hostCommandsReceived++;
    _mb.commandFromHost = value;
    _mb.status |= 0x01;

    if (value == 0x30)
    {
        _uploadStore.clear();
        _uploadLive = true;
    }
    else if (value == 0xD2 && _uploadLive)
    {
        _uploadLive = false;
        _uploadHadModule = !_uploadStore.empty();
    }
    else if (value == 0x31 || value == 0x33)
    {
        _uploadPlaying = true;
    }
    else if (value == 0x32)
    {
        _uploadPlaying = false;
    }
    else if (value == 0xF3 || value == 0xF4 || value == 0x00)
    {
        // Reset wipes the firmware's module RAM: nothing left to hand off
        _uploadStore.clear();
        _uploadLive = false;
        _uploadHadModule = false;
        _uploadPlaying = false;
    }
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
    // Only a load that reached its D2 terminator is replayable (a switch
    // mid-upload keeps the mailbox but loses the partial stream, same
    // documented limit as the LW side)
    if (_uploadLive || !_uploadHadModule || _uploadStore.empty())
    {
        bytes.clear();
        playing = false;
        return false;
    }

    bytes = _uploadStore;
    playing = _uploadPlaying;
    return true;
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
    if (bytes.empty())
        return;

    if (!isROMLoaded())
    {
        MLOGWARNING("GS: personality switch cannot replay the module upload - no firmware ROM");
        return;
    }

    // A healthy firmware drains a paced upload at ~700+ bytes/frame, but
    // POST eats 15-55 frames before the COMINT loop consumes anything.
    // The stall bound turns a wedged firmware into a warning instead of a
    // hang (the queues simply stop draining).
    constexpr size_t kMaxStallFrames = 1000;
    size_t stall = 0;

    // The factory hands over a constructed-but-unbooted card: the COM30
    // stream must not interleave with POST. Advance frames until the volume
    // latches report the past-INITVAR signature (the INITVAR tail's own
    // DATRG read has already consumed the boot-reply flag by then - the
    // NUMPG value merely sits in the #B3 latch), then settle a few frames.
    // An already booted card skips straight to the drain
    if (_activityCounters.volumeLatchWrites < 4)
    {
        size_t boot = 0;
        while (_activityCounters.volumeLatchWrites < 4 && boot < kMaxStallFrames)
        {
            replayAdvanceFrame();
            boot++;
        }
        for (int i = 0; i < 4; i++)
            replayAdvanceFrame();
    }
    replayDrainReply();

    // COM30 open, the way a real loader sequences it: param first, then the
    // command; bit0 falls when the firmware dispatches. The slot reply lands
    // in the #B3 latch (its bit7 flag is consumed by the handler's own
    // DATRG param read - shared flip-flop), so drain the latch, not the flag
    sendData(0x01);
    sendCommand(0x30);
    stall = 0;
    while ((_mb.status & 0x01) != 0 && stall < kMaxStallFrames)
    {
        replayAdvanceFrame();
        stall++;
    }
    replayAdvanceFrame();
    (void)readData(); // consume the slot reply latch (clears bit7 if held)

    // Paced stream, one byte per firmware drain: a #B3 write raises the
    // pending flag and the loader clears it by reading DATRG - exactly
    // how real loaders pace #B3 writes between FLAGS polls. Batching into
    // the 16-deep FIFO instead wedges the gs105a load machine (verified
    // empirically: the tail stalls with the firmware polling FLAGS
    // forever and the module never parses)
    // Per-byte pacing at loader granularity, NOT frame granularity: the
    // firmware's LOADWT consumes a byte within a few hundred GS cycles, so
    // waiting a whole 239602-cycle frame per byte turned a 381 KB module
    // into ~6 minutes of frozen emulation (live-verified 2026-09-22: the
    // "demo stopped" report on every LW->LLE switch). Step 2 interrupt
    // periods at a time and bound the wait in cycles (200 frames' worth).
    constexpr int64_t kByteStepCycles = 2 * GS_CYCLES_PER_INT;
    constexpr int64_t kMaxByteWaitCycles = 200 * 239602; // one HSEND timeout is ~73 frames
    for (size_t pushed = 0; pushed < bytes.size(); pushed++)
    {
        sendData(bytes[pushed]);
        int64_t waited = 0;
        // bit7 falls when the loader's DATRG read consumes the byte; no
        // host #B3 read here (it would clear bit7 under the firmware)
        while ((_mb.status & 0x80) != 0 && waited < kMaxByteWaitCycles)
        {
            runTo(totalGsCycles() + kByteStepCycles);
            waited += kByteStepCycles;
        }
        if (waited >= kMaxByteWaitCycles)
        {
            MLOGWARNING("GS: personality switch module replay stalled at byte %zu of %zu - firmware not draining",
                        pushed + 1, bytes.size());
            return;
        }
    }

    // D2 terminator, then a settle margin for the parse (LOAD3 posts no
    // completion reply - the command popping plus the margin is the done
    // signal)
    sendCommand(0xD2);
    stall = 0;
    while ((_mb.status & 0x01) != 0 && stall < kMaxStallFrames)
    {
        replayAdvanceFrame();
        stall++;
    }
    for (int i = 0; i < 8; i++)
    {
        replayDrainReply();
        replayAdvanceFrame();
    }

    // Resume playback the way the host would (DATRG 1 + COM31 starts module 1;
    // the param must be queued first - an empty DATRG read latches the last
    // uploaded module byte into the selector)
    if (startPlayback)
    {
        sendData(0x01);
        sendCommand(0x31);
        stall = 0;
        while ((_mb.status & 0x01) != 0 && stall < kMaxStallFrames)
        {
            replayAdvanceFrame();
            stall++;
        }
        for (int i = 0; i < 3; i++)
            replayAdvanceFrame();
        (void)readData(); // COM31 status reply: latch drain (flag may be gone)
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
        case 0x0A: _mb.status = (_mb.status & 0x7F) | (_mpag & 0x80); return 0xFF;
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
        case 0x0A: _mb.status = (_mb.status & 0x7F) | (_mpag & 0x80); return;
        case 0x0B: _mb.status = (_mb.status & 0xFE) | ((_channelVol[0] >> 5) & 1); return;
        default: return;
    }
}

/// endregion </GS-side ports>

/// region <Memory subsystem>

void SoundChip_GeneralSound::applyBanking()
{
    // Window 0: ROM page 0 (writes discarded); window 1: RAM page 3, fixed
    // (the DAC sample buffers 0x6000-0x7FFF live in its upper half)
    _bankR[0] = _rom.data();
    _bankW[0] = nullptr;
    _bankR[1] = _ram.data() + 3 * PAGE_SIZE;
    _bankW[1] = _ram.data() + 3 * PAGE_SIZE;

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
    dacFetch(addr, value);
    return value;
}

void SoundChip_GeneralSound::writeMem(uint16_t addr, uint8_t value)
{
    uint8_t* bank = _bankW[(addr >> 14) & 3];
    if (bank) // nullptr = ROM window: write goes nowhere
        bank[addr & (PAGE_SIZE - 1)] = value;
}

void SoundChip_GeneralSound::dacFetch(uint16_t addr, uint8_t value)
{
    // Any read in 0x6000-0x7FFF latches the byte into the DAC selected by
    // address bits 9-8 (design §2.1) - including opcode fetches, matching
    // Unreal's rm() hook
    if ((addr & 0xE000) == 0x6000)
    {
        int channel = (addr >> 8) & 3;
        _channelData[channel] = value;
        _activityCounters.dacFetches++;
        _activityCounters.lastDacFetchGsCycle = totalGsCycles();
        _activityCounters.lastDacFetchFrame = currentFrameNumber();
        traceEvent(GSTraceSide::DacFetch, addr, value, false, static_cast<uint8_t>(channel));
        emitSample();
    }
}

/// endregion </Memory subsystem>

/// region <z80ex callbacks>

Z80EX_BYTE SoundChip_GeneralSound::gsMemRead(Z80EX_CONTEXT* /*cpu*/, Z80EX_WORD addr, int /*m1State*/, void* userData)
{
    return static_cast<SoundChip_GeneralSound*>(userData)->readMem(addr);
}

void SoundChip_GeneralSound::gsMemWrite(Z80EX_CONTEXT* /*cpu*/, Z80EX_WORD addr, Z80EX_BYTE value, void* userData)
{
    static_cast<SoundChip_GeneralSound*>(userData)->writeMem(addr, value);
}

Z80EX_BYTE SoundChip_GeneralSound::gsPortRead(Z80EX_CONTEXT* /*cpu*/, Z80EX_WORD port, void* userData)
{
    return static_cast<SoundChip_GeneralSound*>(userData)->gsIn(port);
}

void SoundChip_GeneralSound::gsPortWrite(Z80EX_CONTEXT* /*cpu*/, Z80EX_WORD port, Z80EX_BYTE value, void* userData)
{
    static_cast<SoundChip_GeneralSound*>(userData)->gsOut(port, value);
}

Z80EX_BYTE SoundChip_GeneralSound::gsIntRead(Z80EX_CONTEXT* /*cpu*/, void* /*userData*/)
{
    return 0xFF; // IM2 vector: nothing drives the GS data bus during INTA
}

/// endregion </z80ex callbacks>

/// region <TTDSerializable (P1.5 - parent TDD 6.4)>

namespace
{
// Little-endian fixed-width helpers: the blob must be portable across
// platforms and the z80ex context itself is not (unsigned long member)
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
    gsTtdWrite16(&dst[21], static_cast<uint16_t>(_intQuantum));
    // bits 2/3 were the queue-era pending flags; _mb.status (dst[0]) is
    // authoritative now, the slots stay reserved for layout compatibility
    dst[23] = static_cast<uint8_t>((_nmiPending ? 1 : 0) | (_intPending ? 2 : 0));

    uint8_t* z80 = &dst[24];
    gsTtdWrite16(z80 + 0, _cpu->af.w);
    gsTtdWrite16(z80 + 2, _cpu->bc.w);
    gsTtdWrite16(z80 + 4, _cpu->de.w);
    gsTtdWrite16(z80 + 6, _cpu->hl.w);
    gsTtdWrite16(z80 + 8, _cpu->af_.w);
    gsTtdWrite16(z80 + 10, _cpu->bc_.w);
    gsTtdWrite16(z80 + 12, _cpu->de_.w);
    gsTtdWrite16(z80 + 14, _cpu->hl_.w);
    gsTtdWrite16(z80 + 16, _cpu->ix.w);
    gsTtdWrite16(z80 + 18, _cpu->iy.w);
    gsTtdWrite16(z80 + 20, _cpu->sp.w);
    gsTtdWrite16(z80 + 22, _cpu->pc.w);
    gsTtdWrite16(z80 + 24, _cpu->memptr.w); // not exposed by the z80ex reg API
    z80[26] = _cpu->i;
    gsTtdWrite16(z80 + 27, _cpu->r);
    z80[29] = _cpu->r7;
    z80[30] = _cpu->iff1;
    z80[31] = _cpu->iff2;
    z80[32] = static_cast<uint8_t>(_cpu->im);
    z80[33] = _cpu->halted ? 1 : 0;
    z80[34] = _cpu->prefix; // runTo can legally stop right after a prefix byte

    // dst[59..94]: queue-era slots, reserved (zero) for layout compatibility
    std::fill_n(&dst[59], 36, static_cast<uint8_t>(0));
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
    _mb.status = src[0];
    _mb.dataFromHost = src[1];
    _mb.dataToHost = src[2];
    _mb.commandFromHost = src[3];
    _mpag = src[4];
    memcpy(_channelVol, &src[5], 4);
    memcpy(_channelData, &src[9], 4);
    _gsCyclesAbs = gsTtdRead64(&src[13]);
    _intQuantum = static_cast<int16_t>(gsTtdRead16(&src[21]));
    _nmiPending = (src[23] & 1) != 0; // bit1 = intPending (pre-level-hold captures: 0/1 only)
    _intPending = (src[23] & 2) != 0;
    // bits 2/3: queue-era pending flags, ignored - _mb.status is authoritative

    const uint8_t* z80 = &src[24];
    _cpu->af.w = gsTtdRead16(z80 + 0);
    _cpu->bc.w = gsTtdRead16(z80 + 2);
    _cpu->de.w = gsTtdRead16(z80 + 4);
    _cpu->hl.w = gsTtdRead16(z80 + 6);
    _cpu->af_.w = gsTtdRead16(z80 + 8);
    _cpu->bc_.w = gsTtdRead16(z80 + 10);
    _cpu->de_.w = gsTtdRead16(z80 + 12);
    _cpu->hl_.w = gsTtdRead16(z80 + 14);
    _cpu->ix.w = gsTtdRead16(z80 + 16);
    _cpu->iy.w = gsTtdRead16(z80 + 18);
    _cpu->sp.w = gsTtdRead16(z80 + 20);
    _cpu->pc.w = gsTtdRead16(z80 + 22);
    _cpu->memptr.w = gsTtdRead16(z80 + 24);
    _cpu->i = z80[26];
    _cpu->r = gsTtdRead16(z80 + 27);
    _cpu->r7 = z80[29];
    _cpu->iff1 = z80[30];
    _cpu->iff2 = z80[31];
    _cpu->im = static_cast<IM_MODE>(z80[32] & 3);
    _cpu->halted = z80[33] ? 1 : 0;
    _cpu->prefix = z80[34];

    // src[59..94]: queue-era slots, ignored

    memcpy(_ram.data(), src + TTD_FIXED_STATE_SIZE, _ram.size());
    applyBanking();

    // Host-side pipeline follows the restored levels without emitting a
    // step (the seek position already produced its own audio)
    computeStereo(_lastL, _lastR);
    if (_blipL) blip_clear(_blipL);
    if (_blipR) blip_clear(_blipR);
    _frameHadActivity = false;

    // Frame-relative bases, exactly as handleFrameStart() would set them: a
    // checkpoint is captured at the frame boundary, after the previous
    // frame's handleFrameEnd but before the next frame's handleFrameStart
    // (RunNFrames runs OnFrameStart after OnFrameEnd). The resumed frame
    // does not re-run handleFrameStart, so without this its handleFrameEnd
    // catches up to a target computed from the pre-seek history - the card
    // then ran whole frames ahead of (or behind) the recording on replay.
    // CPU t and the chipset are restored before the peripherals, so
    // currentZxTacts() already reads the resumed position
    _frameStartZxTacts = currentZxTacts();
    _frameStartGsCycles = totalGsCycles();
    _frameGsCycles = frameGsLength();
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
