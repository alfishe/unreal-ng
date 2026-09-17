#ifdef UNREALNG_HAVE_OPL4

#include "soundchip_moonsound.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "common/filehelper.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"

/// region <Constants>

static const PlatformModulesEnum _MODULE = PlatformModulesEnum::MODULE_SOUND;
static const uint16_t _SUBMODULE = PlatformSoundSubmodulesEnum::SUBMODULE_SOUND_MOONSOUND;

/// endregion </Constants>

/// region <Gain staging>

/// Static gain staging (integration 5.3): MoonSound is a full-scale 16-bit
/// source summed with AY + beeper on the wide bus, so the device lands in
/// the bus at -6 dB and the legacy MoonSoundVol/8192 applies on top in the
/// mixer (device_gain = MoonSoundVol/8192 * headroom_trim).
/// The library render layer normalizes to [-1, 1]; this scales back to int16.
static constexpr float kHeadroomTrim = 32768.0f;

/// Quantise one library float (int16 scale) into the registry buffer domain
/// through the headroom trim.
static int16_t TrimToI16(float value)
{
    const long rounded = std::lrintf(value * kHeadroomTrim);
    return static_cast<int16_t>(std::max(-32768L, std::min(32767L, rounded)));
}

/// endregion </Gain staging>

/// region <Constructors / destructors>

SoundChip_Moonsound::SoundChip_Moonsound(EmulatorContext* context, size_t coreRate)
    : _context(context)
    , _coreRate(coreRate)
{
    _logger = context->pModuleLogger;

    const CONFIG& config = _context->config;

    // Library configuration (3.1). The chip time axis is the 3.5 MHz host
    // T-state grid: hardware turbo is already descaled by AudioTstate before
    // values reach the library, and the host speed multiplier scales the
    // axis exactly like the audio sample budget.
    opl4::Opl4Config opl4Config;
    opl4Config.hostTickRate = CPU_CLOCK_RATE;
    opl4Config.outputRate = static_cast<uint32_t>(coreRate);
    opl4Config.romSizeBytes = 2u << 20;  // YRW801 wave ROM (2 MiB)
    opl4Config.ramSizeBytes = config.moonsound.ramSizeKb * 1024u;
    opl4Config.mode = config.moonsound.renderMode ? opl4::RenderMode::HiFi : opl4::RenderMode::Authentic;
    opl4Config.quality = config.moonsound.quality ? opl4::Quality::HighFidelity : opl4::Quality::Reference;

    _waveMemory.Configure(opl4Config.romSizeBytes, opl4Config.ramSizeBytes);

    // Wave ROM image (D10 / 6.2): the region is zero-filled by Configure; a
    // present image overwrites it, a missing one keeps it - never fatal.
    loadWaveRom();

    _opl4.Configure(opl4Config, &_waveMemory);

    // TTD R5: the chip-state size is fixed for this instance from here on.
    // The hash path stacks a fixed-capacity buffer, so guard the pair now -
    // a library state growth must fail loudly at construction, not silently
    // disable hashing on frame 4000.
    _ttdChipStateSize = _opl4.StateSize();
    if (sizeof(MoonSoundTTDHeader) + _ttdChipStateSize > kTtdBlobCapacity)
    {
        MLOGERROR("MoonSound: chip state size %u exceeds the TTD blob capacity %u - TTD hashing disabled",
                  static_cast<unsigned>(_ttdChipStateSize), static_cast<unsigned>(kTtdBlobCapacity));
    }

    // Separate FM / PCM mixer sources (D5): render both group streams.
    _opl4.EnableSplitStreams(true);

    // D6: the core rate is not forced by this device's presence. Anything
    // but 44100 resamples the OPL4 output; 44100 takes the bit-exact
    // bypass path. Recommend, don't force - a hidden global side effect of
    // one card would violate R2/R6.
    if (coreRate != AUDIO_SAMPLING_RATE)
    {
        MLOGWARNING("MoonSound: core rate %u Hz resamples the OPL4 output; 44100 Hz takes the bit-exact bypass path",
                    static_cast<unsigned>(coreRate));
    }

    // Character presets (5.4): punch defaults to off; BoardAnalog models
    // the YAC513 + LF347 output filter when requested.
    _opl4.SetPunch(opl4::ChannelGroup::Pcm,
                   config.moonsound.punch >= 1 ? opl4::PunchPreset::Opl4Pcm : opl4::PunchPreset::Off);
    _opl4.SetPunch(opl4::ChannelGroup::Fm,
                   config.moonsound.punch >= 2 ? opl4::PunchPreset::Opl4Fm : opl4::PunchPreset::Off);
    _opl4.SetBoardAnalog(config.moonsound.boardAnalog != 0);

    reset();
}

SoundChip_Moonsound::~SoundChip_Moonsound() = default;

/// endregion </Constructors / destructors>

/// region <Wave ROM>

void SoundChip_Moonsound::loadWaveRom()
{
    // D10 / 6.2: host-supplied image resolved like every other ROM image
    // (working directory, then executable, then resources). A missing or
    // short image is a warning plus a zero-filled region - the emulator
    // must still start (GM tones are silent, SRAM uploads still work).
    const char* path = _context->config.moonsound.waveRom;
    if (!path || !path[0])
        return;

    const std::string normalized = FileHelper::NormalizePath(path);
    std::string resolved = normalized;
    if (!FileHelper::FileExists(resolved))
    {
        resolved = FileHelper::PathCombine(FileHelper::GetExecutablePath(), normalized);
        if (!FileHelper::FileExists(resolved))
        {
            resolved = FileHelper::PathCombine(FileHelper::GetResourcesPath(), normalized);
            if (!FileHelper::FileExists(resolved))
            {
                MLOGWARNING("MoonSound wave ROM image not found: '%s' - running with a zero-filled ROM region", path);
                return;
            }
        }
    }

    const uint32_t romSize = _waveMemory.RomEnd();
    const size_t loaded = FileHelper::ReadFileToBuffer(resolved, _waveMemory.RomData(), romSize);
    if (loaded == 0)
    {
        MLOGWARNING("MoonSound wave ROM image could not be read: '%s' - running with a zero-filled ROM region",
                    FileHelper::PrintablePath(resolved).c_str());
        return;
    }

    _waveRomLoadedBytes = loaded;

    // Content hash over the whole ROM region (TTD D6): image identity for
    // the Tier A blob - restore warns when the loaded image differs from
    // the recorded one (7.3). Hashing the region (not just the loaded
    // prefix) keeps short images distinguishable from full ones.
    const uint8_t* romRegion = _waveMemory.RomData();
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (uint32_t i = 0; i < romSize; i++)
    {
        hash ^= static_cast<uint64_t>(romRegion[i]);
        hash *= 0x100000001b3ULL;
    }
    _romHash = hash;
    if (loaded < romSize)
    {
        MLOGWARNING("MoonSound wave ROM image is short: %u of %u bytes - the tail stays zero-filled",
                    static_cast<unsigned>(loaded), romSize);
    }
    else
    {
        MLOGINFO("MoonSound wave ROM loaded: '%s' (%u bytes)", FileHelper::PrintablePath(resolved).c_str(), romSize);
    }
}

/// endregion </Wave ROM>

/// region <Frame lifecycle>

void SoundChip_Moonsound::reset()
{
    _fmLatch[0] = 0;
    _fmLatch[1] = 0;
    _fmBank = 0;
    _waveLatch = 0;

    // The time axis restarts; the monotonic clamp below keeps any straggler
    // call from walking the freshly reset core backwards.
    _tstateOrigin = 0;
    _lastChipTime = 0;
    _opl4.Reset(0);

    memset(_fmBuffer, 0, _fmAudioDescriptor.memoryBufferSizeInBytes);
    memset(_pcmBuffer, 0, _pcmAudioDescriptor.memoryBufferSizeInBytes);
}

void SoundChip_Moonsound::handleFrameStart()
{
    // Rebase the origin (3.2): the previous frame is complete when this is
    // called, so fold its whole duration into the absolute axis whether or
    // not handleFrameEnd ran (the mainloop skips it under turbo without
    // audio). Then advance the core to the frame boundary - run() always,
    // render() never here (D3).
    _tstateOrigin += frameDuration();
    _opl4.Run(monotonicChipTime(_tstateOrigin));

    if (_synthesisSuppressed)
    {
        // Turbo without audio (3.3): handleFrameEnd is skipped by the
        // mainloop in this mode, so drop pending delivery audio here to
        // keep the library's buffers bounded across a turbo session.
        _opl4.DiscardPendingAudio();
    }
}

void SoundChip_Moonsound::handleFrameEnd(size_t expectedSamples)
{
    if (expectedSamples > static_cast<size_t>(MAX_SAMPLES_PER_FRAME))
        expectedSamples = static_cast<size_t>(MAX_SAMPLES_PER_FRAME);

    // Flush the core to the exact frame end before rendering (3.2) - also
    // bounds how far the core may fall behind between port accesses.
    _opl4.Run(monotonicChipTime(_tstateOrigin + frameDuration()));

    const size_t frameBytes = expectedSamples * AUDIO_CHANNELS * sizeof(int16_t);

    if (_synthesisSuppressed)
    {
        // Turbo without audio (3.3): drop the pending delivery audio; chip
        // state is untouched (core TDD D11 / R8).
        _opl4.DiscardPendingAudio();
        memset(_fmBuffer, 0, frameBytes);
        memset(_pcmBuffer, 0, frameBytes);
        return;
    }

    // Render path (4.2): pull both group streams and quantise into the
    // registry buffers through the headroom trim (5.3). The library emits
    // interleaved-stereo floats at int16 scale, hard-bounded by the frames
    // asked for; the return is the minimum across the two groups, and the
    // unwritten tail of the frame stays zeroed.
    memset(_fmBuffer, 0, frameBytes);
    memset(_pcmBuffer, 0, frameBytes);
    const size_t rendered = _opl4.RenderSplit(_fmScratch.data(), _pcmScratch.data(), expectedSamples);
    const size_t renderedSamples = rendered * AUDIO_CHANNELS;
    for (size_t i = 0; i < renderedSamples; i++)
    {
        _fmBuffer[i] = TrimToI16(_fmScratch[i]);
        _pcmBuffer[i] = TrimToI16(_pcmScratch[i]);
    }
}

void SoundChip_Moonsound::setSynthesisSuppressed(bool suppressed)
{
    _synthesisSuppressed = suppressed;
}

void SoundChip_Moonsound::setCoreRate(size_t coreRate)
{
    // The library's output rate is fixed at Configure() time; a live change
    // is a re-design, not a state-preserving switch. Record the request -
    // the render stage re-designs; chip state must not reset on a reroute.
    if (coreRate != _coreRate)
        _coreRate = coreRate;
}

/// endregion </Frame lifecycle>

/// region <Channel taps>

size_t SoundChip_Moonsound::channelCount(opl4::ChannelGroup group) const
{
    return _opl4.ChannelCount(group);
}

void SoundChip_Moonsound::setChannelMute(opl4::ChannelGroup group, size_t index, bool mute)
{
    opl4::ChannelId id;
    id.group = group;
    id.index = static_cast<uint8_t>(index);
    _opl4.SetChannelMute(id, mute);
}

float SoundChip_Moonsound::channelPeak(opl4::ChannelGroup group, size_t index) const
{
    opl4::ChannelId id;
    id.group = group;
    id.index = static_cast<uint8_t>(index);
    return _opl4.ChannelPeak(id);
}

/// endregion </Channel taps>

/// region <Port interface>

uint8_t SoundChip_Moonsound::portDeviceInMethod(uint16_t port)
{
    // The card CPLD decodes only A0..A7 (MoonService-verified): every
    // high-byte alias of a card port is the card port.
    switch (port & 0xFF)
    {
        case PORT_FM_ADDR1:
            // FM status read: BUSY/LD live in the library (2.1)
            return _opl4.ReadStatus(chipTimeNow());

        case PORT_WAVE_DATA:
            // Wave register read: BUSY semantics and the register value from
            // the library; the address is the host latch.
            return _opl4.ReadWave(chipTimeNow(), _waveLatch);

        default:
            return 0xFF;  // The card does not drive the bus on other addresses
    }
}

void SoundChip_Moonsound::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    // Every write advances the core to the exact T-state first (2.3): the
    // library advances lazily, no per-step polling is needed. Low-byte decode
    // as in portDeviceInMethod: the Z80 `out (n),a` form leaves A in the high
    // address byte and the author's driver depends on the card ignoring it.
    switch (port & 0xFF)
    {
        case PORT_FM_ADDR1:
            _fmBank = 0;
            _fmLatch[0] = value;  // address latch only - no chip traffic
            break;

        case PORT_FM_DATA1:
        case PORT_FM_DATA2:
            // Card-decode truth (mfm_sample_2 evidence, 2026-09-17): the
            // data ports #C5/#C7 are equivalent - a data write delivers to
            // the bank of the most recent ADDRESS-port write (#C4 -> bank 0,
            // #C6 -> bank 1), not to a per-port bank. The author's own
            // MBPlayer (moonsound.bin $8AC8, MBPlayer_out_fm2) programs bank-2
            // registers as `out (#C6),reg` + `out (#C5),data`; under a
            // per-data-port decode those writes (the 0x105 NEW/NEW2 enable
            // included) landed on the stale bank-0 latch, NEW never latched,
            // and the bus then aliased every later bank-1 register write onto
            // bank 0 - MFM Music sample 2 melody 7 rendered as voices with
            // mismatched modulator/carrier patches (noise) while clean bank-0
            // voices played on. Measured melody-7 traffic: C4->C5 1341,
            // C6->C7 1166, C6->C5 112, C4->C7 0.
            _opl4.WriteFm(chipTimeNow(), _fmBank, _fmLatch[_fmBank], value);
            break;

        case PORT_FM_ADDR2:
            _fmBank = 1;
            _fmLatch[1] = value;
            break;

        case PORT_WAVE_ADDR:
            // NEW2 gate (openMSX YMF278B::writeIO, verified on real YMF278):
            // while NEW2 (FM bank-1 reg 0x105 bit 1) is clear the chip
            // ignores wave register access — select and data alike.
            if (_opl4.New2Mode())
                _waveLatch = value;
            break;

        case PORT_WAVE_DATA:
            if (_opl4.New2Mode())
                _opl4.WriteWave(chipTimeNow(), _waveLatch, value);
            break;

        default:
            break;
    }
}

bool SoundChip_Moonsound::portDeviceClaimsRead(uint16_t port)
{
    const uint8_t low = static_cast<uint8_t>(port & 0xFF);

    // FM1 status (#C4): the card's status register drives the bus on its own
    // decode, unconditionally - the register exists regardless of the NEW2
    // arming state, and guest players poll BUSY/LD here through dirty-high-
    // byte forms (mfm_player.asm `in a,(0C4h)` loop). Claiming it keeps the
    // motherboard decode (keyboard FE arm on the even mirror) from winning
    // the cycle while the card is attached
    if (low == static_cast<uint8_t>(PORT_FM_ADDR1))
        return true;

    // MoonService-verified arming rule: until the guest sets OPL4 NEW2
    // (FM2 reg 05 bit 1, honored from bank 1 even in OPL3 mode — the same
    // signal the chip itself uses to accept wave access, openMSX getNew2),
    // the card leaves #7F to the Beta-128 FDC mirror - TR-DOS traffic stays
    // byte-identical (R6). Once armed, the card drives the wave data port
    // over the mirror.
    return low == static_cast<uint8_t>(PORT_WAVE_DATA) && _opl4.New2Mode();
}

/// endregion </Port interface>

/// region <Ports interaction>

bool SoundChip_Moonsound::attachToPorts(PortDecoder* decoder)
{
    bool result = false;

    if (decoder)
    {
        _portDecoder = decoder;

        // Attach as a low-byte full-decode observer on the card's six port
        // addresses (D4): the CPLD wires A0..A7 only, so every high-byte
        // alias hits the card - including the dirty aliases the Z80 immediate
        // forms produce (A lands in the high address byte; MoonService v0.3a
        // depends on it). The card shares the bus, so partial-decode devices
        // (ULA/AY/Beta-128) still see MoonSound cycles and MoonSound sees
        // theirs. The exclusive map would steal #7F from the WD1793 FDC.
        result = decoder->RegisterFullDecodeLowBytePort(static_cast<uint8_t>(PORT_FM_ADDR1), this);
        result &= decoder->RegisterFullDecodeLowBytePort(static_cast<uint8_t>(PORT_FM_DATA1), this);
        result &= decoder->RegisterFullDecodeLowBytePort(static_cast<uint8_t>(PORT_FM_ADDR2), this);
        result &= decoder->RegisterFullDecodeLowBytePort(static_cast<uint8_t>(PORT_FM_DATA2), this);
        result &= decoder->RegisterFullDecodeLowBytePort(static_cast<uint8_t>(PORT_WAVE_ADDR), this);
        result &= decoder->RegisterFullDecodeLowBytePort(static_cast<uint8_t>(PORT_WAVE_DATA), this);

        if (result)
        {
            _chipAttachedToPortDecoder = true;
        }
    }

    return result;
}

void SoundChip_Moonsound::detachFromPorts()
{
    if (_portDecoder && _chipAttachedToPortDecoder)
    {
        _portDecoder->UnregisterFullDecodeLowBytePort(static_cast<uint8_t>(PORT_FM_ADDR1), this);
        _portDecoder->UnregisterFullDecodeLowBytePort(static_cast<uint8_t>(PORT_FM_DATA1), this);
        _portDecoder->UnregisterFullDecodeLowBytePort(static_cast<uint8_t>(PORT_FM_ADDR2), this);
        _portDecoder->UnregisterFullDecodeLowBytePort(static_cast<uint8_t>(PORT_FM_DATA2), this);
        _portDecoder->UnregisterFullDecodeLowBytePort(static_cast<uint8_t>(PORT_WAVE_ADDR), this);
        _portDecoder->UnregisterFullDecodeLowBytePort(static_cast<uint8_t>(PORT_WAVE_DATA), this);

        _chipAttachedToPortDecoder = false;
    }
}

/// endregion </Ports interaction>

/// region <TTD serialization>

size_t SoundChip_Moonsound::TTDStateSize() const
{
    // Tier A (TTD 4.2): the host header plus the library's chip state, exactly.
    return sizeof(MoonSoundTTDHeader) + _ttdChipStateSize;
}

void SoundChip_Moonsound::TTDSaveState(uint8_t* dst) const
{
    // A plain write of exactly TTDStateSize() bytes (TTD R6): a zeroed
    // header, then the library's POD state. Side-effect free by
    // construction - the save-neutrality test is what keeps it that way.
    MoonSoundTTDHeader header{};
    header.magic[0] = 'M';
    header.magic[1] = 'S';
    header.magic[2] = 'N';
    header.magic[3] = 'D';
    header.layoutVersion = kTtdLayoutVersion;
    header.headerSize = static_cast<uint16_t>(sizeof(header));
    header.fmLatch[0] = _fmLatch[0];
    header.fmLatch[1] = _fmLatch[1];
    header.waveLatch = _waveLatch;
    header.fmBank = _fmBank;
    header.tstateOrigin = _tstateOrigin;
    header.lastChipTime = _lastChipTime;
    header.romHash = _romHash;
    std::memcpy(dst, &header, sizeof(header));
    _opl4.SaveState(dst + sizeof(header));
}

void SoundChip_Moonsound::TTDLoadState(const uint8_t* src)
{
    MoonSoundTTDHeader header{};
    std::memcpy(&header, src, sizeof(header));
    if (header.magic[0] != 'M' || header.magic[1] != 'S' || header.magic[2] != 'N'
        || header.magic[3] != 'D' || header.layoutVersion != kTtdLayoutVersion
        || header.headerSize != sizeof(header))
    {
        MLOGWARNING("MoonSound TTD restore refused: blob envelope mismatch (magic/version/size)");
        return;
    }
    if (header.romHash != _romHash)
    {
        MLOGWARNING("MoonSound TTD restore: wave ROM image differs from the recorded session "
                    "(recorded hash %016llx, loaded %016llx) - replay may sound wrong",
                    static_cast<unsigned long long>(header.romHash),
                    static_cast<unsigned long long>(_romHash));
    }

    _fmLatch[0] = header.fmLatch[0];
    _fmLatch[1] = header.fmLatch[1];
    _waveLatch = header.waveLatch;
    _fmBank = header.fmBank;
    _tstateOrigin = header.tstateOrigin;
    _lastChipTime = header.lastChipTime;
    _opl4.LoadState(src + sizeof(header));

    // Tier C (D7): the render layer is never captured and resets here. The
    // pending delivery audio belongs to the abandoned timeline (7.4) and
    // the registry buffers drop with it.
    _opl4.ResetRenderState();
    _opl4.DiscardPendingAudio();
    memset(_fmBuffer, 0, _fmAudioDescriptor.memoryBufferSizeInBytes);
    memset(_pcmBuffer, 0, _pcmAudioDescriptor.memoryBufferSizeInBytes);
}

uint64_t SoundChip_Moonsound::TTDHashState() const
{
    // FNV-1a over the same bytes TTDSaveState writes (TTD D9): Tier A only -
    // SRAM integrity is the page store's job once Tier B exists. Zeroed
    // first so no byte the state chunks leave unwritten can ever leak stack
    // garbage into the hash.
    uint8_t blob[kTtdBlobCapacity] = {};
    const size_t size = TTDStateSize();
    if (size > sizeof(blob))
        return 0;  // constructor guard makes this unreachable

    TTDSaveState(blob);
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < size; i++)
    {
        h ^= static_cast<uint64_t>(blob[i]);
        h *= 0x100000001b3ULL;
    }
    return h;
}

/// endregion </TTD serialization>

/// region <Time base>

uint64_t SoundChip_Moonsound::frameDuration() const
{
    // Same budget the SoundManager sample accumulator uses: the HOST speed
    // multiplier scales the axis, hardware turbo does not (AudioTstate
    // already descales it on the position reads).
    const CONFIG& config = _context->config;
    const uint64_t multiplier = _context->emulatorState.HostSpeedMultiplier();
    return static_cast<uint64_t>(config.frame) * multiplier;
}

uint64_t SoundChip_Moonsound::currentChipTime() const
{
    if (!_context || !_context->pCore || !_context->pCore->GetZ80())
        return _tstateOrigin;

    const uint32_t frameT = _context->emulatorState.AudioTstate(_context->pCore->GetZ80()->t);
    const uint64_t multiplier = _context->emulatorState.HostSpeedMultiplier();
    return _tstateOrigin + static_cast<uint64_t>(frameT) * multiplier;
}

uint64_t SoundChip_Moonsound::monotonicChipTime(uint64_t time)
{
    if (time < _lastChipTime)
        return _lastChipTime;
    _lastChipTime = time;
    return time;
}

uint64_t SoundChip_Moonsound::chipTimeNow()
{
    return monotonicChipTime(currentChipTime());
}

/// endregion </Time base>

#endif  // UNREALNG_HAVE_OPL4
