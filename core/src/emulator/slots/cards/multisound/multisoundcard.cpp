#include "multisoundcard.h"

#include <algorithm>
#include <cstring>

#include "common/filehelper.h"
#include "debugger/ttd/ttdserializable.h"
#include "emulator/sound/audio.h"
#include "emulator/sound/chips/gs/soundchip_gs.h"
#include "emulator/sound/chips/tsfm/ym2203pair.h"
#include "sam2695/sam2695.h"
#include "sam2695/soundbank.h"

namespace
{
constexpr size_t kMaxFrames = static_cast<size_t>(MAX_SAMPLES_PER_FRAME);

Saa1099RenderMode SaaMode(MultiSoundRenderMode mode)
{
    // Authentic: the PDM stream into the board's ladder filter (the mixer's Authentic SAA path)
    return mode == MultiSoundRenderMode::Authentic ? Saa1099RenderMode::Authentic : Saa1099RenderMode::HiFi;
}

/// A data file the way ROMs are found (SoundChip_GeneralSound::readROM): as given (working dir), then next to the
/// executable, then in the resources (macOS bundle). Empty when none exists
std::string ResolveDataFile(const std::string& path)
{
    if (path.empty())
        return {};
    const std::string normalized = FileHelper::NormalizePath(path);
    if (FileHelper::FileExists(normalized))
        return normalized;
    const std::string nearExecutable = FileHelper::PathCombine(FileHelper::GetExecutablePath(), normalized);
    if (FileHelper::FileExists(nearExecutable))
        return nearExecutable;
    const std::string inResources = FileHelper::PathCombine(FileHelper::GetResourcesPath(), path);
    if (FileHelper::FileExists(inResources))
        return inResources;
    return {};
}
/// Little-endian fixed-width fields of the card's time-travel blob
struct TtdWriter
{
    uint8_t* p;
    void U8(uint8_t v) { *p++ = v; }
    void U16(uint16_t v)
    {
        for (int i = 0; i < 2; i++)
            *p++ = static_cast<uint8_t>(v >> (8 * i));
    }
    void U64(uint64_t v)
    {
        for (int i = 0; i < 8; i++)
            *p++ = static_cast<uint8_t>(v >> (8 * i));
    }
};

struct TtdReader
{
    const uint8_t* p;
    uint8_t U8() { return *p++; }
    uint16_t U16()
    {
        uint16_t v = 0;
        for (int i = 0; i < 2; i++)
            v = static_cast<uint16_t>(v | (static_cast<uint16_t>(*p++) << (8 * i)));
        return v;
    }
    uint64_t U64()
    {
        uint64_t v = 0;
        for (int i = 0; i < 8; i++)
            v |= static_cast<uint64_t>(*p++) << (8 * i);
        return v;
    }
};

/// Offsets of the card's blob (multisoundcard.h, "Time travel")
constexpr size_t kTtdTimesOffset = 1;
constexpr size_t kTtdFmChangesOffset = kTtdTimesOffset + 5 * 8 + 1 + 2;
constexpr size_t kTtdLatchesOffset = kTtdFmChangesOffset + MultiSoundCard::kMaxFmMuteChanges * 9;
constexpr size_t kTtdYmOffset = kTtdLatchesOffset + 11 + 4 * 2;
}  // namespace

/// region <Construction>

MultiSoundCard::MultiSoundCard(EmulatorContext* context, const MultiSoundCardConfig& config)
    : _config(config), _context(context), _logic(config.options)
{
    // YM2203 pair on the card's own 3.5 MHz, a true ratio on hosts that are not 3.5 MHz (architecture.md §2)
    Ym2203PairConfig ymConfig;
    ymConfig.masterClockHz = kYmMasterClockHz;
    ymConfig.hostTickRate = _config.hostTickRate;
    ymConfig.continuousHostAxis = true;   // the card axis is never rebased
    _ym = std::make_unique<Ym2203Pair>(context, ymConfig);
    _ym->configureChannelOutputs(_config.outputRate);

    Saa1099Config saaConfig;
    saaConfig.hostTickRate = _config.hostTickRate;
    saaConfig.chipClockHz = 8000000;  // 32 MHz / 4
    saaConfig.outputRate = _config.outputRate;
    saaConfig.renderMode = SaaMode(_config.renderMode);
    _saa.Configure(saaConfig);

    MultiSoundDacsConfig dacConfig;
    dacConfig.hostTickRate = _config.hostTickRate;
    dacConfig.outputRate = _config.outputRate;
    dacConfig.renderMode = _config.renderMode;
    _dacs.Configure(dacConfig);

    MultiSoundMixerConfig mixConfig;
    mixConfig.outputRate = _config.outputRate;
    mixConfig.renderMode = _config.renderMode;
    _mixer.Configure(mixConfig);
    _mixer.SetFmTrimDb(_config.fmTrimDb);

    // The GS: MultiSound profile, DACs into this card's MultiSoundDacs, host time from this card's axis
    const size_t gsRamKB = _config.options.gsRam == MultiSoundGsRam::TwoMb ? 2048 : 1024;
    _gs = std::make_unique<SoundChip_GeneralSound>(context, gsRamKB, _config.outputRate,
                                                   GSProfile::MultiSound(gsRamKB, this, this));
    _gs->loadROM(_config.gsRomPath);

    // SAM2695 on the host axis, the chip's whole output path and its 50 ms boot window
    _synth = std::make_unique<sam2695::Synth>();
    sam2695::SynthConfig synthConfig;
    synthConfig.hostTickRate = _config.hostTickRate;
    synthConfig.outputRate = _config.outputRate;
    _synth->Configure(synthConfig);
    LoadMidiBank();
    _midiLine.Connect(_synth.get());
    _ym->setIoPortListener(kMidiChip, &_midiLine);

    for (auto& v : _fm)
        v.assign(kMaxFrames, 0.0f);
    for (auto& chip : _ssg)
        for (auto& v : chip)
            v.assign(kMaxFrames, 0.0f);
    _saaOut.assign(kMaxFrames * 2, 0);
    _dacOut.assign(kMaxFrames * 2, 0);
    _midiOut.assign(kMaxFrames * 2, 0.0f);
    for (auto& row : _rows)
        row.assign(kMaxFrames * 2, 0);
    _fmMuteChanges.reserve(kMaxFmMuteChanges);

    // Power-on = the board reset at time 0
    BusReset(0);
}

MultiSoundCard::~MultiSoundCard()
{
    // The pair's SSG must not call the line after it is gone (members are destroyed in reverse order: the line
    // before the synthesizer, the pair last)
    if (_ym)
        _ym->setIoPortListener(kMidiChip, nullptr);
}

void MultiSoundCard::LoadMidiBank()
{
    _bankLoaded = false;
    _bankName.clear();
    _bankSource.clear();
    _bankError.clear();

    std::shared_ptr<const sam2695::ISoundBank> bank = _config.midiBank;
    if (bank)
    {
        _bankSource = "(supplied)";
    }
    else if (_config.midiBankPath.empty())
    {
        _bankError = "no bank configured ([MIDI] Bank=NONE)";
    }
    else
    {
        const std::string resolved = ResolveDataFile(_config.midiBankPath);
        if (resolved.empty())
        {
            _bankError = "bank '" + _config.midiBankPath + "' not found";
        }
        else
        {
            std::vector<uint8_t> bytes(FileHelper::GetFileSize(resolved));
            const size_t read = bytes.empty() ? 0 : FileHelper::ReadFileToBuffer(resolved, bytes.data(), bytes.size());
            sam2695::Sf2Bank::LoadResult result = sam2695::Sf2Bank::LoadMemory(bytes.data(), read);
            if (result.bank)
            {
                bank = result.bank;
                _bankSource = resolved;
            }
            else
            {
                _bankError = std::string(sam2695::BankErrorName(result.error)) + ": " + result.reason;
            }
        }
    }

    if (bank && _synth->LoadBank(bank))
    {
        _bankLoaded = true;
        _bankName = bank->Model().name;
    }
    else if (bank)
    {
        _bankError = "the synthesizer refused the bank";
    }
}

/// endregion </Construction>

/// region <Options>

void MultiSoundCard::SetOptions(const MultiSoundOptions& options)
{
    MultiSoundOptions applied = options;
    applied.gsRam = _config.options.gsRam;  // sizes the GS RAM: construction only
    _config.options = applied;
    _logic.Configure(applied);
}

void MultiSoundCard::SetOutputRate(uint32_t rate)
{
    _config.outputRate = rate;
    _ym->configureChannelOutputs(rate);
    _saa.SetOutputRate(rate);
    _dacs.SetOutputRate(rate);
    _synth->SetOutputRate(rate);
    _mixer.SetOutputRate(rate);
    _gs->setSampleRate(rate);
    _frameAccumulator = 0;
}

void MultiSoundCard::SetFmTrimDb(double db)
{
    _config.fmTrimDb = db;
    _mixer.SetFmTrimDb(db);
}

void MultiSoundCard::SetRenderMode(MultiSoundRenderMode mode)
{
    _config.renderMode = mode;
    _saa.SetRenderMode(SaaMode(mode));
    _dacs.SetRenderMode(mode);
    _mixer.SetRenderMode(mode);
}

/// endregion </Options>

/// region <Bus>

void MultiSoundCard::Out(uint16_t port, uint8_t value, uint64_t t)
{
    _now = std::max(_now, t);
    const MultiSoundBusActions actions = _logic.Write(port, value);
    for (const MultiSoundBusAction& action : actions)
    {
        switch (action.kind)
        {
            case MultiSoundBusAction::Kind::None:
                break;
            case MultiSoundBusAction::Kind::Control:
                ApplyControl(t);
                break;
            case MultiSoundBusAction::Kind::YmAddress:
                // A control byte arrives here too: the newly selected chip latches it as an address (L5)
                _ym->syncTo(t);
                _ym->writeAddress(action.chip, action.value);
                break;
            case MultiSoundBusAction::Kind::YmData:
                _ym->syncTo(t);
                _ym->writeData(action.chip, action.value);
                break;
            case MultiSoundBusAction::Kind::SaaAddress:
                _saa.WriteAddress(t, action.value);
                break;
            case MultiSoundBusAction::Kind::SaaData:
                _saa.WriteData(t, action.value);
                break;
            case MultiSoundBusAction::Kind::GsData:
                _gs->portDeviceOutMethod(GeneralSoundCard::PORT_DATA, action.value);
                break;
            case MultiSoundBusAction::Kind::GsCommand:
                _gs->portDeviceOutMethod(GeneralSoundCard::PORT_COMMAND, action.value);
                break;
            case MultiSoundBusAction::Kind::SoundriveSample:
                // The GS runs to t first (its DAC events up to t are in), then the shared volume register and the
                // DAC event of this write
                _gs->sharedVolumeWrite(action.chip, 0x3F);
                _dacs.SoundriveWrite(t + kHostStrobeEndOffset, action.chip, action.value);
                break;
        }
    }
    if (_trace) [[unlikely]]
        TraceHost(MultiSoundBusEvent::Kind::HostOut, port, value, false, t);
}

void MultiSoundCard::ApplyControl(uint64_t t)
{
    const MultiSoundLatches& latches = _logic.Latches();
    if (_saa.ClockEnabled() != latches.saaClock)
        _saa.SetClockEnabled(t, latches.saaClock);

    // FM*_ENA: the render splits the frame at each change
    const bool lastMuted = _fmMuteChanges.empty() ? _fmMutedRendered : _fmMuteChanges.back().muted;
    if (latches.fmMuted != lastMuted)
    {
        if (_fmMuteChanges.size() < kMaxFmMuteChanges)
            _fmMuteChanges.push_back({t, latches.fmMuted});
        else
            _fmMuteChanges.back() = {t, latches.fmMuted};
    }
}

uint8_t MultiSoundCard::In(uint16_t port, uint64_t t, bool& drives)
{
    _now = std::max(_now, t);
    const MultiSoundReadResult result = _logic.Read(port);
    drives = result.Drives();
    uint8_t value = 0xFF;
    switch (result.source)
    {
        case MultiSoundReadResult::Source::YmStatus:
            _ym->syncTo(t);
            value = _ym->readStatus(result.chip);
            break;
        case MultiSoundReadResult::Source::YmRegister:
            _ym->syncTo(t);
            value = _ym->readData(result.chip);
            break;
        case MultiSoundReadResult::Source::GsOutput:
            value = _gs->portDeviceInMethod(GeneralSoundCard::PORT_DATA);
            break;
        case MultiSoundReadResult::Source::GsStatus:
            value = _gs->portDeviceInMethod(GeneralSoundCard::PORT_COMMAND);
            break;
        case MultiSoundReadResult::Source::None:
            break;
    }
    if (_trace) [[unlikely]]
        TraceHost(MultiSoundBusEvent::Kind::HostIn, port, value, drives, t);
    return value;
}

void MultiSoundCard::SetBusTrace(IMultiSoundBusTrace* trace)
{
    _trace = trace;
    _gs->setBusObserver(trace != nullptr ? static_cast<IGSBusObserver*>(this) : nullptr);
}

void MultiSoundCard::TraceHost(MultiSoundBusEvent::Kind kind, uint16_t port, uint8_t value, bool drives, uint64_t t) const
{
    MultiSoundBusEvent event;
    event.kind = kind;
    event.drives = drives;
    event.value = value;
    event.address = port;
    event.m1 = _lastM1;
    event.time = t;
    _trace->OnMultiSoundBus(event);
}

void MultiSoundCard::GsPortCycle(uint64_t time, uint8_t port, uint8_t value, bool write)
{
    if (_trace == nullptr)
        return;
    MultiSoundBusEvent event;
    event.kind = write ? MultiSoundBusEvent::Kind::GsOut : MultiSoundBusEvent::Kind::GsIn;
    event.drives = !write;
    event.value = value;
    event.address = port;
    event.time = _frameBase + time;
    _trace->OnMultiSoundBus(event);
}

void MultiSoundCard::GsDacFetch(uint64_t time, uint16_t address, uint8_t value)
{
    if (_trace == nullptr)
        return;
    MultiSoundBusEvent event;
    event.kind = MultiSoundBusEvent::Kind::GsDacFetch;
    event.value = value;
    event.address = address;
    event.time = _frameBase + time;
    _trace->OnMultiSoundBus(event);
}

uint8_t MultiSoundCard::Peek(uint16_t port, bool& drives) const
{
    const MultiSoundReadResult result = _logic.Peek(port);
    drives = result.Drives();
    switch (result.source)
    {
        case MultiSoundReadResult::Source::YmStatus:
            return _ym->chip(result.chip)->fm.read_status();
        case MultiSoundReadResult::Source::YmRegister:
            return _ym->readData(result.chip);
        case MultiSoundReadResult::Source::GsOutput:
            return _gs->getDataToHost();
        case MultiSoundReadResult::Source::GsStatus:
            return static_cast<uint8_t>(_gs->getStatusRaw() | 0x7E);
        case MultiSoundReadResult::Source::None:
            break;
    }
    return 0xFF;
}

void MultiSoundCard::MidiPanic(uint64_t t)
{
    _now = std::max(_now, t);
    _synth->Panic(t);
}

void MultiSoundCard::BusReset(uint64_t t)
{
    _now = std::max(_now, t);

    // Everything before the reset is in: the GS to t (its DAC events), the SAA and the DACs run to t
    _gs->resetAtHostNow();
    _saa.Run(t);
    _dacs.Run(t);

    _logic.Reset();
    _ym->syncTo(t);
    _ym->reset();
    _ym->syncTo(t);  // adopt t: the chips run from the reset on
    _saa.Reset(t);
    _saa.SetClockEnabled(t, _logic.Latches().saaClock);
    _dacs.Reset(t);
    _midiLine.Reset(t);
    _synth->Reset(t);

    // The reset branch drives FM*_ENA low
    if (_fmMuteChanges.size() < kMaxFmMuteChanges)
        _fmMuteChanges.push_back({t, _logic.Latches().fmMuted});
    else
        _fmMuteChanges.back() = {t, _logic.Latches().fmMuted};

    if (_trace) [[unlikely]]
        TraceHost(MultiSoundBusEvent::Kind::BusReset, 0, 0, false, t);
}

/// endregion </Bus>

/// region <Frames>

void MultiSoundCard::FrameStart(uint64_t t, uint64_t frameTicks)
{
    _now = std::max(_now, t);
    _frameBase = t;
    _frameTicks = frameTicks;
    if (_trace) [[unlikely]]
        TraceHost(MultiSoundBusEvent::Kind::FrameStart, 0, 0, false, t);
    _gs->handleFrameStart();
}

size_t MultiSoundCard::FrameEnd(uint64_t t, size_t frames)
{
    _now = std::max(_now, t);
    if (_trace) [[unlikely]]
        TraceHost(MultiSoundBusEvent::Kind::FrameEnd, 0, 0, false, t);

    if (frames == 0)
    {
        const uint64_t elapsed = t > _renderedTo ? t - _renderedTo : 0;
        _frameAccumulator += elapsed * _config.outputRate;
        frames = static_cast<size_t>(_frameAccumulator / _config.hostTickRate);
        _frameAccumulator %= _config.hostTickRate;
    }
    frames = std::min(frames, kMaxFrames);

    // The GS to its frame's end (its own buffer stays silent: the DACs are the card's)
    if (_frameTicks > 0)
        _gs->handleFrameEnd(frames);
    _ym->syncTo(t);

    RenderYm(frames, t);
    _saa.EndFrame(t, _saaOut.data(), frames);
    _dacs.EndFrame(t, _dacOut.data(), frames);

    _synth->Run(t);
    const size_t got = _synth->Render(_midiOut.data(), frames);
    if (got > 0)
    {
        _midiLast[0] = _midiOut[(got - 1) * 2];
        _midiLast[1] = _midiOut[(got - 1) * 2 + 1];
    }
    for (size_t i = got; i < frames; i++)
    {
        // The synthesizer runs in control blocks: the first short frame holds the last level, the backlog then
        // covers every later frame
        _midiOut[i * 2] = _midiLast[0];
        _midiOut[i * 2 + 1] = _midiLast[1];
    }

    MultiSoundMixerInput in;
    in.frames = frames;
    for (int c = 0; c < 2; c++)
    {
        in.fm[c] = _fm[c].data();
        for (int ch = 0; ch < 3; ch++)
            in.ssg[c][ch] = _ssg[c][ch].data();
    }
    in.saa = _saaOut.data();
    in.dac = _dacOut.data();
    in.midi = _midiOut.data();

    MultiSoundMixerOutput out;
    for (int c = 0; c < 2; c++)
    {
        out.fm[c] = _rows[static_cast<size_t>(c == 0 ? MultiSoundRow::Fm1 : MultiSoundRow::Fm2)].data();
        out.ssg[c] = _rows[static_cast<size_t>(c == 0 ? MultiSoundRow::Ssg1 : MultiSoundRow::Ssg2)].data();
    }
    out.saa = _rows[static_cast<size_t>(MultiSoundRow::Saa)].data();
    out.dac = _rows[static_cast<size_t>(MultiSoundRow::Pcm)].data();
    out.midi = _rows[static_cast<size_t>(MultiSoundRow::Midi)].data();
    _mixer.Mix(in, out);

    _rowFrames = frames;
    _renderedTo = t;
    return frames;
}

void MultiSoundCard::RenderYm(size_t frames, uint64_t t)
{
    // Split the block at the FM*_ENA changes: each part gets the output samples its share of the time is worth
    // The pair is synced to t: the cursor rule for the whole frame's samples, before its blocks
    _ym->beginChannelRender(frames);

    const uint64_t start = _renderedTo;
    const uint64_t span = t > start ? t - start : 0;
    size_t done = 0;
    bool muted = _fmMutedRendered;
    for (const FmMuteChange& change : _fmMuteChanges)
    {
        const uint64_t at = std::clamp(change.t, start, std::max(start, t));
        const size_t upto = span ? std::min(frames, static_cast<size_t>((at - start) * frames / span)) : 0;
        if (upto > done)
        {
            RenderYmBlock(done, upto - done, !muted);
            done = upto;
        }
        muted = change.muted;
    }
    if (frames > done)
        RenderYmBlock(done, frames - done, !muted);
    _fmMutedRendered = muted;
    _fmMuteChanges.clear();
}

void MultiSoundCard::RenderYmBlock(size_t offset, size_t frames, bool fmEnabled)
{
    Ym2203ChannelBlock block;
    for (int c = 0; c < 2; c++)
    {
        block.fm[c] = _fm[c].data() + offset;
        for (int ch = 0; ch < 3; ch++)
            block.ssg[c][ch] = _ssg[c][ch].data() + offset;
    }
    _ym->renderChannels(frames, block, fmEnabled);
}

/// endregion </Frames>

/// region <Report>

void MultiSoundCard::Describe(MultiSoundCardReport& out) const
{
    out.options = _logic.Options();
    out.latches = _logic.Latches();
    out.latches.gsData = _gs->getDataFromHost();
    out.latches.gsCommand = _gs->getCommandFromHost();
    out.latches.gsPage = _gs->getMPAG();
    out.latches.gsOutput = _gs->getDataToHost();
    out.latches.dataFlag = (_gs->getStatusRaw() & 0x80) != 0;
    out.latches.commandFlag = (_gs->getStatusRaw() & 0x01) != 0;

    for (int c = 0; c < 2; c++)
    {
        Ym2203Chip* chip = _ym->chip(c);
        MultiSoundCardReport::Ym& ym = out.ym[c];
        ym.address = chip->address;
        ym.status = chip->fm.read_status();
        for (uint8_t r = 0; r < 16; r++)
            ym.ssgRegisters[r] = chip->ssg.readRegister(r);
        std::memcpy(ym.fmKeyOn, chip->fmKeyOn, sizeof(ym.fmKeyOn));
    }
    out.ymRatioPhase = _ym->ratioPhase();

    _saa.Describe(out.saa);

    out.gs.romLoaded = _gs->isROMLoaded();
    out.gs.ramKB = _gs->getRamSizeKB();
    out.gs.page = _gs->getMPAG();
    out.gs.status = static_cast<uint8_t>(_gs->getStatusRaw() | 0x7E);
    out.gs.dataFromHost = _gs->getDataFromHost();
    out.gs.dataToHost = _gs->getDataToHost();
    out.gs.commandFromHost = _gs->getCommandFromHost();
    out.gs.firmwareReady = _gs->isReadyForCommands();
    out.gs.cpuSteps = _gs->getActivityCounters().cpuSteps;
    out.gs.dacFetches = _gs->getActivityCounters().dacFetches;

    for (int ch = 0; ch < 4; ch++)
        out.dac[static_cast<size_t>(ch)] = _dacs.Channel(ch);
    out.dacPendingEvents = _dacs.PendingEvents();
    out.dacLateEvents = _dacs.LateEvents();

    _midiLine.Describe(out.midiLine);
    sam2695::SynthReport synth;
    _synth->Describe(synth);
    out.midi.bankLoaded = _bankLoaded;
    out.midi.bankStatus = _bankLoaded ? "loaded" : "no bank";
    out.midi.bankName = _bankName;
    out.midi.bankSource = _bankSource;
    out.midi.bankError = _bankError;
    out.midi.bytesReceived = synth.bytesReceived;
    out.midi.framingErrors = synth.framingErrors;
    out.midi.activeVoices = synth.activeVoices;

    out.time = _now;
}

void MultiSoundCard::DescribeSynth(sam2695::SynthReport& out) const
{
    _synth->Describe(out);
}

/// endregion </Report>

/// region <Time travel>

size_t MultiSoundCard::TtdStateSize() const
{
    return kTtdYmOffset + 8 + _ym->TTDStateSize() + _midiLine.TTDStateSize() + _dacs.TTDStateSize();
}

void MultiSoundCard::TtdSave(uint8_t* dst) const
{
    TtdWriter w{ dst };
    w.U8(kTtdVersion);
    w.U64(_now);
    w.U64(_frameBase);
    w.U64(_frameTicks);
    w.U64(_renderedTo);
    w.U64(_frameAccumulator);
    w.U8(_fmMutedRendered ? 1 : 0);
    w.U16(static_cast<uint16_t>(_fmMuteChanges.size()));
    for (size_t i = 0; i < kMaxFmMuteChanges; i++)
    {
        const FmMuteChange change = i < _fmMuteChanges.size() ? _fmMuteChanges[i] : FmMuteChange{ 0, false };
        w.U64(change.t);
        w.U8(change.muted ? 1 : 0);
    }
    const MultiSoundLatches& l = _logic.Latches();
    for (const uint8_t v : { l.ymChip, static_cast<uint8_t>(l.ymReadStatus), static_cast<uint8_t>(l.fmMuted),
                             static_cast<uint8_t>(l.saaClock), static_cast<uint8_t>(l.romLock), l.gsData, l.gsCommand,
                             l.gsPage, l.gsOutput, static_cast<uint8_t>(l.dataFlag), static_cast<uint8_t>(l.commandFlag) })
        w.U8(v);
    for (int ch = 0; ch < 4; ch++)
    {
        w.U8(_logic.Dac(ch).sample);
        w.U8(_logic.Dac(ch).volume);
    }
    int64_t ymSynced = 0;
    _ym->TTDSyncedTime(0, ymSynced);
    w.U64(static_cast<uint64_t>(ymSynced));
    _ym->TTDSaveState(w.p);
    w.p += _ym->TTDStateSize();
    _midiLine.TTDSaveState(w.p);
    w.p += _midiLine.TTDStateSize();
    _dacs.TTDSaveState(w.p);
}

bool MultiSoundCard::TtdLoad(const uint8_t* src)
{
    TtdReader r{ src };
    if (r.U8() != kTtdVersion)
        return false;
    _now = r.U64();
    _frameBase = r.U64();
    _frameTicks = r.U64();
    _renderedTo = r.U64();
    _frameAccumulator = r.U64();
    _fmMutedRendered = r.U8() != 0;
    const size_t changes = std::min<size_t>(r.U16(), kMaxFmMuteChanges);
    _fmMuteChanges.clear();
    for (size_t i = 0; i < kMaxFmMuteChanges; i++)
    {
        const uint64_t t = r.U64();
        const bool muted = r.U8() != 0;
        if (i < changes)
            _fmMuteChanges.push_back({ t, muted });
    }
    MultiSoundLatches l;
    l.ymChip = static_cast<uint8_t>(r.U8() & 1);
    l.ymReadStatus = r.U8() != 0;
    l.fmMuted = r.U8() != 0;
    l.saaClock = r.U8() != 0;
    l.romLock = r.U8() != 0;
    l.gsData = r.U8();
    l.gsCommand = r.U8();
    l.gsPage = r.U8();
    l.gsOutput = r.U8();
    l.dataFlag = r.U8() != 0;
    l.commandFlag = r.U8() != 0;
    std::array<MultiSoundDacState, 4> dac{};
    for (MultiSoundDacState& channel : dac)
    {
        channel.sample = r.U8();
        channel.volume = r.U8();
    }
    _logic.Restore(l, dac);
    const uint64_t ymSynced = r.U64();
    _ym->TTDLoadState(r.p, ymSynced);
    r.p += _ym->TTDStateSize();
    _midiLine.TTDLoadState(r.p);
    r.p += _midiLine.TTDStateSize();
    _dacs.TTDLoadState(r.p);

    // Render layers back to their start: the audio after a restore does not depend on what played before it
    _mixer.Reset();
    ++_renderEpoch;   // the host's SSG voicing restarts with the mixer
    _midiLast[0] = _midiLast[1] = 0.0f;
    _rowFrames = 0;
    return true;
}

void MultiSoundCard::TtdTimeFields(std::vector<ttd::TTDTimeField>& out, uint16_t offset) const
{
    for (const size_t field : { size_t(0), size_t(1), size_t(3) })   // now, frame base, rendered-to
        out.push_back({ static_cast<uint16_t>(offset + kTtdTimesOffset + field * 8), 8 });
    out.push_back({ static_cast<uint16_t>(offset + kTtdYmOffset), 8 });   // the pair's synced time
    Ym2203Pair::TTDTimeFields(out, static_cast<uint16_t>(offset + kTtdYmOffset + 8 + Ym2203Pair::kStateChipsOffset));
    // The DACs' time axis: after the version byte and the four channels
    const size_t dacs = kTtdYmOffset + 8 + _ym->TTDStateSize() + _midiLine.TTDStateSize();
    out.push_back({ static_cast<uint16_t>(offset + dacs + 1 + 4 * 2), 8 });
}

bool MultiSoundCard::TtdSynced(uint64_t now, int64_t& offset) const
{
    return _ym->TTDSyncedTime(now, offset);
}

/// endregion </Time travel>

