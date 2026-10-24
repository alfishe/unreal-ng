#include "controllermanager.h"

#include <algorithm>
#include <cctype>
#include <chrono>

#include "3rdparty/message-center/messagecenter.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/mouse/debugmousemanager.h"
#include "debugger/ttd/timetravelhooks.h"
#include "debugger/ttd/ttdinputapply.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/joystick/joystick.h"
#include "emulator/io/mouse/mousemanager.h"
#include "emulator/io/tape/tape.h"
#include "emulator/memory/memory.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"
#include "emulator/ports/models/portdecoder_next.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/slots/card.h"
#include "emulator/slots/slotmanager.h"
#include "emulator/sound/chips/iturbosounddevice.h"
#include "emulator/sound/chips/soundchip_ay8910.h"
#include "emulator/sound/soundmanager.h"
#include "ihapticfeed.h"
#include "ihostinputsource.h"

namespace controllers
{

namespace
{
/// v3: the host key overlay (8 half-rows) after the connectors
constexpr uint8_t kControllersTTDVersion = 3;
constexpr size_t kHostKeyRows = 8;

/// The key each control bit holds (kRight, kLeft, kDown, kUp, kFire), per wiring
constexpr ZXKeysEnum kSinclair1Keys[5] = {ZXKEY_7, ZXKEY_6, ZXKEY_8, ZXKEY_9, ZXKEY_0};
constexpr ZXKeysEnum kSinclair2Keys[5] = {ZXKEY_2, ZXKEY_1, ZXKEY_3, ZXKEY_4, ZXKEY_5};
constexpr ZXKeysEnum kCursorKeys[5] = {ZXKEY_8, ZXKEY_5, ZXKEY_6, ZXKEY_7, ZXKEY_0};
constexpr uint16_t kDirections = kRight | kLeft | kDown | kUp;
/// A plug in the TTD blob and the fingerprint: device, Sinclair port, Cursor shift, eight keys
constexpr size_t kPlugBytes = 3 + kOverlayControls;

constexpr const char* kControlNames[kOverlayControls] = {"right", "left",  "down",  "up",
                                                         "fire",  "fire2", "fire3", "fire4"};

std::string Lower(std::string text)
{
    for (char& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

std::string Trim(const std::string& text)
{
    const size_t begin = text.find_first_not_of(" \t");
    if (begin == std::string::npos)
        return "";
    const size_t end = text.find_last_not_of(" \t");
    return text.substr(begin, end - begin + 1);
}

void HoldKey(uint8_t rows[8], ZXKeysEnum key)
{
    uint8_t row = 0;
    uint8_t bit = 0;
    if (Keyboard::MatrixPosition(key, row, bit))
        rows[row] &= static_cast<uint8_t>(~(1u << bit));
}

void SavePlug(const Connector& connector, uint8_t* dst)
{
    dst[0] = static_cast<uint8_t>(connector.device);
    dst[1] = connector.options.sinclairPort;
    dst[2] = connector.options.cursorShift ? 1 : 0;
    for (unsigned bit = 0; bit < kOverlayControls; bit++)
        dst[3 + bit] = static_cast<uint8_t>(connector.options.keys[bit]);
}

void LoadPlug(const uint8_t* src, Connector& connector)
{
    connector.device = src[0] <= static_cast<uint8_t>(RetroDeviceKind::AmxMouse)
                           ? static_cast<RetroDeviceKind>(src[0])
                           : RetroDeviceKind::None;
    connector.options.sinclairPort = src[1] == 2 ? 2 : 1;
    connector.options.cursorShift = src[2] != 0;
    for (unsigned bit = 0; bit < kOverlayControls; bit++)
        connector.options.keys[bit] = static_cast<ZXKeysEnum>(src[3 + bit]);
}

bool SameOptions(const OverlayOptions& a, const OverlayOptions& b)
{
    return a.sinclairPort == b.sinclairPort && a.cursorShift == b.cursorShift && a.keys == b.keys &&
           a.extended == b.extended && a.im2Leak == b.im2Leak;
}

/// The manager holds the controls (journaled as ConnectorState, in the Controllers blob)
bool Journaled(const Connector& connector)
{
    return connector.kind == ConnectorKind::KeyboardJoystick || connector.kind == ConnectorKind::Fuller;
}

Connector MakeConnector(const std::string& id, const char* alias, ConnectorKind kind, bool fixed, RetroDeviceKind device,
                        std::vector<RetroDeviceKind> accepts)
{
    Connector connector;
    connector.id = id;
    connector.alias = alias;
    connector.kind = kind;
    connector.fixed = fixed;
    connector.device = device;
    connector.accepts = std::move(accepts);
    connector.options = ControllerManager::DefaultOptions(device);
    return connector;
}
}  // namespace

ControllerManager::ControllerManager(EmulatorContext* context) : _context(context)
{
    Rebuild();
}

/// region <Connectors>

void ControllerManager::Rebuild()
{
    PortDecoder* decoder = _context ? _context->pPortDecoder : nullptr;
    const MEM_MODEL model = _context ? _context->config.mem_model : MM_PENTAGON;
    const std::vector<RetroDeviceKind> overlays = {RetroDeviceKind::SinclairJoystick, RetroDeviceKind::CursorJoystick,
                                                   RetroDeviceKind::KeysJoystick};

    std::vector<Connector> built;
    if (decoder && decoder->HasPads())
    {
        // The Next's two DB9 connectors: the pad is part of the machine, its projection follows NR #05
        built.push_back(MakeConnector("next.left", "left", ConnectorKind::NextDb9, true, RetroDeviceKind::Md6Pad,
                                      {RetroDeviceKind::Md6Pad}));
        built.push_back(MakeConnector("next.right", "right", ConnectorKind::NextDb9, true, RetroDeviceKind::Md6Pad,
                                      {RetroDeviceKind::Md6Pad}));
    }
    else if (decoder && decoder->HasKempstonJoystick())
    {
        built.push_back(MakeConnector("kempston", "", ConnectorKind::Kempston, true, RetroDeviceKind::KempstonJoystick,
                                      {RetroDeviceKind::KempstonJoystick}));
    }

    // The +2 / +2A / +3 joystick sockets are wired to the keyboard rows
    if (model == MM_PLUS2 || model == MM_PLUS2A || model == MM_PLUS3)
    {
        built.push_back(MakeConnector("sinclair-1", "", ConnectorKind::KeyboardJoystick, true,
                                      RetroDeviceKind::SinclairJoystick, {RetroDeviceKind::SinclairJoystick}));
        built.push_back(MakeConnector("sinclair-2", "", ConnectorKind::KeyboardJoystick, true,
                                      RetroDeviceKind::SinclairJoystick, {RetroDeviceKind::SinclairJoystick}));
        built.back().options.sinclairPort = 2;
    }

    // The sockets the fitted cards bring (a Fuller Box): the card answers its port from the connector's controls
    if (SlotManager* slotManager = _context ? _context->pSlotManager : nullptr)
    {
        std::vector<CardConnector> brought;
        for (const std::unique_ptr<ICard>& card : slotManager->Cards())
        {
            brought.clear();
            card->Connectors(brought);
            for (const CardConnector& socket : brought)
            {
                std::string id = socket.id;
                for (unsigned n = 2; std::any_of(built.begin(), built.end(),
                                                 [&id](const Connector& c) { return c.id == id; });
                     n++)
                    id = socket.id + "." + std::to_string(n);
                built.push_back(MakeConnector(id, "", socket.kind, true, socket.device, {socket.device}));
                built.back().owner = card.get();
            }
        }
    }

    // Every machine: two keyboard-joystick sockets for keys-only games (no card needed)
    built.push_back(
        MakeConnector("keyjoy.1", "", ConnectorKind::KeyboardJoystick, false, RetroDeviceKind::None, overlays));
    built.push_back(
        MakeConnector("keyjoy.2", "", ConnectorKind::KeyboardJoystick, false, RetroDeviceKind::None, overlays));

    std::unique_lock<std::mutex> lock(_mutex);
    const bool rebuilt = !_connectors.empty();  // the constructor's first build tells nobody
    unsigned perKind[kConnectorKinds] = {};
    for (Connector& connector : built)
    {
        connector.kindIndex = perKind[static_cast<unsigned>(connector.kind)]++;
        // A connector that survives keeps its plug and what it holds
        for (const Connector& old : _connectors)
        {
            if (old.id != connector.id)
                continue;
            // A fixed socket keeps its options (Kempston's) while its device stays the board's
            if (!connector.fixed || old.device == connector.device)
            {
                connector.device = old.device;
                connector.options = old.options;
            }
            connector.mask = old.mask;
        }
    }

    _connectors = std::move(built);
    _lastSample.assign(_connectors.size(), -1);
    _revision++;
    UpdateOverlay();
    UpdateKempston();
    if (rebuilt)
        QueuePostLocked({}, {});
    lock.unlock();
    FlushPosts();
}

size_t ControllerManager::Count() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _connectors.size();
}

Connector ControllerManager::At(size_t index) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return index < _connectors.size() ? _connectors[index] : Connector{};
}

std::vector<Connector> ControllerManager::Connectors() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _connectors;
}

uint64_t ControllerManager::Revision() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _revision;
}

int ControllerManager::Find(const std::string& selector) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return FindLocked(selector);
}

int ControllerManager::FindLocked(const std::string& selector) const
{
    const std::string wanted = Lower(Trim(selector));
    if (wanted.empty())
        return -1;
    for (size_t i = 0; i < _connectors.size(); i++)
    {
        if (_connectors[i].id == wanted || (!_connectors[i].alias.empty() && _connectors[i].alias == wanted))
            return static_cast<int>(i);
    }

    // kind:index
    const size_t colon = wanted.find(':');
    if (colon == std::string::npos || colon + 1 >= wanted.size())
        return -1;
    const std::string kind = wanted.substr(0, colon);
    const std::string number = wanted.substr(colon + 1);
    if (!std::all_of(number.begin(), number.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
        return -1;
    const unsigned index = static_cast<unsigned>(std::stoul(number));
    for (size_t i = 0; i < _connectors.size(); i++)
    {
        if (ConnectorKindName(_connectors[i].kind) == kind && _connectors[i].kindIndex == index)
            return static_cast<int>(i);
    }
    return -1;
}

/// endregion </Connectors>

/// region <Plugs>

ControllerResult ControllerManager::PlugRefused(bool& defer) const
{
    ttd::ITimeTravelHooks* hooks = _context ? _context->pTimeTravelHooks : nullptr;
    defer = false;
    if (InputOwnedByReplay())
        return ControllerResult::Fail(ControllerError::OwnedByReplay, "recorded input drives the machine");
    if (hooks && hooks->IsRecording())
    {
        // A plug decides what the journal records: it lands at the boundary, where the recording keeps it
        if (!hooks->RecordsMediaChanges())
        {
            return ControllerResult::Fail(ControllerError::Recording,
                                          "TTD v1 keeps no configuration changes: plug before the recording starts");
        }
        defer = true;
    }
    return ControllerResult::Success();
}

ControllerResult ControllerManager::CheckPlugLocked(size_t index, RetroDeviceKind device) const
{
    if (index >= _connectors.size())
        return ControllerResult::Fail(ControllerError::UnknownConnector, "no connector " + std::to_string(index));
    const Connector& connector = _connectors[index];
    // A fixed socket takes new options for its own device, nothing else
    if (connector.fixed && device != connector.device)
        return ControllerResult::Fail(ControllerError::Fixed, connector.id + " is part of the board");
    if (device != RetroDeviceKind::None &&
        std::find(connector.accepts.begin(), connector.accepts.end(), device) == connector.accepts.end())
    {
        return ControllerResult::Fail(ControllerError::Incompatible,
                                      connector.id + " does not take " + RetroDeviceKindName(device));
    }
    return ControllerResult::Success();
}

void ControllerManager::PlugLocked(size_t index, RetroDeviceKind device, const OverlayOptions& options)
{
    Connector& connector = _connectors[index];
    connector.device = device;
    connector.options = device == RetroDeviceKind::None ? OverlayOptions{} : options;
    connector.mask = 0;  // a new device holds nothing yet
    _lastSample[index] = -1;
    _revision++;
    UpdateOverlay();
    UpdateKempston();
    QueuePostLocked(connector.id, RetroDeviceKindName(device));
}

void ControllerManager::QueuePostLocked(const std::string& connector, const std::string& device)
{
    _posts.push_back({connector, device, _revision});
}

void ControllerManager::FlushPosts()
{
    std::vector<QueuedPost> posts;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        posts.swap(_posts);
    }
    for (QueuedPost& post : posts)
    {
        auto* payload = new ControllerConnectorPayload(std::move(post.connector), std::move(post.device), post.revision);
        // Every payload names its emulator: the context's id is there from the start (the emulator object may not be)
        if (_context)
            payload->emulatorId = _context->emulatorId;
        MessageCenter::DefaultMessageCenter().Post(NC_CONTROLLER_CONNECTOR_CHANGED, payload, true);
    }
}

ControllerResult ControllerManager::Plug(size_t index, RetroDeviceKind device, const OverlayOptions& options)
{
    bool defer = false;
    if (ControllerResult refused = PlugRefused(defer); !refused.Ok())
        return refused;
    std::unique_lock<std::mutex> lock(_mutex);
    if (ControllerResult checked = CheckPlugLocked(index, device); !checked.Ok())
        return checked;
    // The Kempston options are not in the Controllers blob: a seek could not take them back
    if (defer && _connectors[index].kind == ConnectorKind::Kempston)
    {
        return ControllerResult::Fail(ControllerError::Recording,
                                      "the Kempston options are not journaled: set them before the recording starts");
    }

    if (!defer)
    {
        PlugLocked(index, device, options);
        lock.unlock();
        FlushPosts();
        return ControllerResult::Success();
    }
    // The latest request per connector wins
    const std::string& id = _connectors[index].id;
    _pending.erase(
        std::remove_if(_pending.begin(), _pending.end(), [&id](const PendingPlug& p) { return p.connector == id; }),
        _pending.end());
    _pending.push_back({id, device, options});
    return ControllerResult::Success();
}

ControllerResult ControllerManager::Unplug(size_t index)
{
    return Plug(index, RetroDeviceKind::None, {});
}

void ControllerManager::ApplyPending()
{
    // A replay lands nothing of its own: the plugs wait for live input
    if (InputOwnedByReplay())
        return;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        if (_pending.empty())
            return;
        for (const PendingPlug& plug : _pending)
        {
            const int index = FindLocked(plug.connector);
            // The connector may have gone with a rebuild; its plug goes with it
            if (index >= 0 && CheckPlugLocked(static_cast<size_t>(index), plug.device).Ok())
                PlugLocked(static_cast<size_t>(index), plug.device, plug.options);
        }
        _pending.clear();
    }
    FlushPosts();
    _applied.notify_all();
}

bool ControllerManager::HasPending() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return !_pending.empty();
}

bool ControllerManager::FramesRun() const
{
    Emulator* emulator = _context ? _context->pEmulator : nullptr;
    return emulator && emulator->IsRunning() && !emulator->IsPaused();
}

bool ControllerManager::WaitApplied(uint32_t timeoutMs)
{
    // Asked before the lock: a paused machine reaches no boundary, so there is nothing to wait for
    const bool framesRun = FramesRun();
    std::unique_lock<std::mutex> lock(_mutex);
    if (_pending.empty())
        return true;
    if (!framesRun)
        return false;
    return _applied.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] { return _pending.empty(); });
}

bool ControllerManager::ParseKeys(const std::string& spec, std::array<ZXKeysEnum, kOverlayControls>& keys)
{
    bool allParsed = true;
    size_t start = 0;
    while (start <= spec.size())
    {
        size_t end = spec.find(',', start);
        if (end == std::string::npos)
            end = spec.size();
        const std::string entry = Trim(spec.substr(start, end - start));
        start = end + 1;
        if (entry.empty())
            continue;

        const size_t colon = entry.find(':');
        const std::string control = colon == std::string::npos ? "" : Lower(Trim(entry.substr(0, colon)));
        const ZXKeysEnum key = colon == std::string::npos
                                   ? ZXKEY_NONE
                                   : DebugKeyboardManager::ResolveKeyName(Trim(entry.substr(colon + 1)));
        uint8_t row = 0;
        uint8_t bit = 0;
        const auto named = std::find(std::begin(kControlNames), std::end(kControlNames), control);
        if (named == std::end(kControlNames) || !Keyboard::MatrixPosition(key, row, bit))
        {
            allParsed = false;
            continue;
        }
        keys[static_cast<size_t>(named - std::begin(kControlNames))] = key;
    }
    return allParsed;
}

std::string ControllerManager::KeysSpec(const std::array<ZXKeysEnum, kOverlayControls>& keys)
{
    std::string spec;
    for (unsigned bit = 0; bit < kOverlayControls; bit++)
    {
        if (keys[bit] == ZXKEY_NONE)
            continue;
        spec += (spec.empty() ? "" : ",") + std::string(kControlNames[bit]) + ":" +
                Lower(DebugKeyboardManager::GetKeyDisplayName(keys[bit]));
    }
    return spec;
}

OverlayOptions ControllerManager::DefaultOptions(RetroDeviceKind device)
{
    OverlayOptions options;
    if (device == RetroDeviceKind::KeysJoystick)
        options.keys = {ZXKEY_P, ZXKEY_O, ZXKEY_A, ZXKEY_Q, ZXKEY_SPACE, ZXKEY_NONE, ZXKEY_NONE, ZXKEY_NONE};
    return options;
}

/// endregion </Plugs>

/// region <Input>

bool ControllerManager::InputOwnedByReplay() const
{
    if (_context && _context->rzxPlayer)
        return true;
    return _context && _context->pTimeTravelHooks && _context->pTimeTravelHooks->OwnsInput();
}

uint16_t ControllerManager::State(size_t index) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (index >= _connectors.size())
        return 0;
    const Connector& connector = _connectors[index];
    switch (connector.kind)
    {
        case ConnectorKind::Kempston:
            return _context && _context->pJoystick ? _context->pJoystick->State() : 0;
        case ConnectorKind::NextDb9:
            return _context && _context->pPortDecoder ? _context->pPortDecoder->PadState(connector.kindIndex) : 0;
        case ConnectorKind::KeyboardJoystick:
        case ConnectorKind::Fuller:
            return connector.mask;
        case ConnectorKind::Amx:
            return 0;  // a mouse: its input is the mouse's (MouseManager), not a control mask
    }
    return 0;
}

uint16_t ControllerManager::OwnedState(const void* owner) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (const Connector& connector : _connectors)
    {
        if (owner && connector.owner == owner)
            return connector.mask;
    }
    return 0;
}

bool ControllerManager::EventFor(size_t index, uint16_t mask, ttd::TTDInputEvent& ev) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (index >= _connectors.size())
        return false;
    const Connector& connector = _connectors[index];
    switch (connector.kind)
    {
        case ConnectorKind::Kempston:
            ev.kind = ttd::TTDInputKind::Joystick;
            ev.buttonMask = static_cast<uint8_t>(mask);
            return true;
        case ConnectorKind::NextDb9:
            ev.kind = ttd::TTDInputKind::NextPad;
            ev.key = static_cast<uint8_t>(connector.kindIndex);
            ev.dx = static_cast<int16_t>(mask & PortDecoder::kPadMask);
            return true;
        case ConnectorKind::KeyboardJoystick:
        case ConnectorKind::Fuller:
            if (connector.device == RetroDeviceKind::None)
                return false;
            ev.kind = ttd::TTDInputKind::ConnectorState;
            ev.key = static_cast<uint8_t>(index);
            ev.dx = static_cast<int16_t>(mask);
            return true;
        case ConnectorKind::Amx:
            return false;  // the AMX mouse is fed and journaled through the mouse input kinds
    }
    return false;
}

bool ControllerManager::Submit(size_t index, uint16_t mask)
{
    ttd::TTDInputEvent ev;
    if (!EventFor(index, mask, ev))
        return false;
    if (_context && _context->pTimeTravelHooks)
        return _context->pTimeTravelHooks->SubmitLiveInput(ev);
    return ttd::ApplyInputEvent(ev, ttd::InputDevicesOf(_context));
}

bool ControllerManager::ApplyConnectorState(size_t index, uint16_t mask)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (index >= _connectors.size())
        return false;
    Connector& connector = _connectors[index];
    if (!Journaled(connector) || connector.device == RetroDeviceKind::None)
        return false;
    connector.mask = mask;
    UpdateOverlay();
    return true;
}

void ControllerManager::SetHostInputSource(std::shared_ptr<IHostInputSource> source)
{
    std::lock_guard<std::mutex> lock(_sourceMutex);
    _source = std::move(source);
}

void ControllerManager::SetHapticFeed(std::shared_ptr<IHapticFeed> feed)
{
    std::lock_guard<std::mutex> lock(_sourceMutex);
    _hapticFeed = std::move(feed);
}

void ControllerManager::FeedHaptics(uint64_t frame)
{
    std::shared_ptr<IHapticFeed> feed;
    {
        std::lock_guard<std::mutex> lock(_sourceMutex);
        feed = _hapticFeed;
    }
    SoundManager* sound = _context ? _context->pSoundManager : nullptr;
    if (!feed || sound == nullptr)
    {
        _hapticFed = nullptr;
        return;
    }
    HapticFrame summary;
    summary.frame = frame;
    summary.frameTStates = _context->GetFrameTStates();
    summary.cpuHz = _context->emulatorState.current_z80_frequency;
    summary.beeperEdges = sound->getBeeper().TakeEarEdges();
    const int chips = std::min<int>(sound->getAYChipCount(), static_cast<int>(HapticFrame::kMaxAy));
    for (int index = 0; index < chips; index++)
    {
        SoundChip_AY8910* chip = sound->getAYChip(index);
        if (chip == nullptr)
            continue;
        HapticAyFrame& ay = summary.ay[summary.ayCount++];
        const uint8_t* regs = chip->getRegisters();
        for (unsigned channel = 0; channel < 3; channel++)
        {
            const uint8_t volume = regs[AY_A_VOLUME + channel];
            const bool envelope = (volume & 0x10) != 0;
            ay.envelopeUsed = ay.envelopeUsed || envelope;
            // R7 bits 3-5 clear: the channel's noise is on
            if (!(regs[AY_MIXER_CONTROL] & (0x08u << channel)))
                ay.noiseVolume[channel] = envelope ? 16 : static_cast<uint8_t>(volume & 0x0F);
        }
        ay.envelopeRestarts = chip->TakeEnvelopeRestarts();
        ay.envelopeShape = regs[AY_ENVELOPE_SHAPE];
    }
    if (_hapticFed != feed.get())
    {
        _hapticFed = feed.get();  // the counters ran up while nobody listened
        _hapticWatches = feed->Watches();
        if (_hapticWatches.size() > HapticFrame::kMaxWatches)
            _hapticWatches.resize(HapticFrame::kMaxWatches);
        return;
    }
    if (Memory* memory = _context->pMemory)
    {
        // Read, never written: the probes cost nothing to the emulation (haptics.md §9)
        for (const HapticWatch& watch : _hapticWatches)
        {
            uint8_t value = 0;
            if (watch.page == 0xFFFF)
                value = memory->DirectReadFromZ80Memory(watch.address);
            else if (const uint8_t* page = memory->RAMPageAddress(watch.page))
                value = page[watch.address & 0x3FFF];
            summary.watched[summary.watchCount++] = value;
        }
    }
    feed->OnHapticFrame(summary);
}

void ControllerManager::WatchPortReads(bool on)
{
    _watchRequest.store(on ? 1 : 2, std::memory_order_release);
}

PortReadEvidence ControllerManager::PortReads() const
{
    PortReadEvidence evidence = _watch.Evidence();
    evidence.active = _watching.load(std::memory_order_acquire);
    return evidence;
}

namespace
{
/// The watch's register probe (PortReadRegisterProbe): an IN of the AY's data port (#FFFD as the 128K family decodes
/// it) with R14 selected, an IN of the Next's #253B with NR #B2 selected. Runs only while the watch does, after the IN
bool ProbeRegisterRead(const void* context, uint16_t port, PortReadClass& cls)
{
    const EmulatorContext* ctx = static_cast<const EmulatorContext*>(context);
    if (port == 0x253B)
    {
        auto* next = dynamic_cast<PortDecoder_Next*>(ctx->pPortDecoder);
        if (next == nullptr || next->Board().SelectedRegister() != 0xB2)
            return false;
        cls = PortReadClass::NextB2;
        return true;
    }
    if ((port & 0xC002) != 0xC000 || ctx->pSoundManager == nullptr)
        return false;
    const ITurboSoundDevice* ay = ctx->pSoundManager->getTurboSound();
    if (ay == nullptr || ay->getBusReadRegister() != 14)
        return false;
    cls = PortReadClass::AyR14;
    return true;
}
}  // namespace

void ControllerManager::ApplyWatchRequest()
{
    Z80* cpu = _context->pCore != nullptr ? _context->pCore->GetZ80() : nullptr;
    const int request = _watchRequest.exchange(0, std::memory_order_acq_rel);
    if (request == 1)
    {
        _watch.Reset();
        // The row bits read the code after a half-row IN as the CPU sees it, without side effects
        if (_context->pMemory != nullptr)
            _watch.SetCodeReader(
                [](const void* memory, uint16_t address) {
                    return static_cast<Memory*>(const_cast<void*>(memory))->DirectReadFromZ80Memory(address);
                },
                _context->pMemory);
        _watch.SetRegisterProbe(&ProbeRegisterRead, _context);
        _watching.store(true, std::memory_order_release);
        if (cpu != nullptr)
            cpu->SetPortReadWatch(&_watch);
        return;
    }
    if (!_watching.load(std::memory_order_relaxed))
        return;
    // While a tape plays the program is still loading: the watch starts counting when it stops
    if (_context->pTape != nullptr && _context->pTape->IsPlaying())
    {
        _watch.Reset();
        return;
    }
    _watch.EndFrame();
    if (request == 2 || _watch.Evidence().framesObserved >= kPortReadWatchFrames)
    {
        _watching.store(false, std::memory_order_release);
        if (cpu != nullptr)
            cpu->SetPortReadWatch(nullptr);
        _watch.SetCodeReader(nullptr, nullptr);
        _watch.SetRegisterProbe(nullptr, nullptr);
    }
}

void ControllerManager::OnFrame(uint64_t frame)
{
    ApplyWatchRequest();
    if (InputOwnedByReplay())
    {
        _wasOwned = true;
        return;
    }
    if (_wasOwned)
    {
        // Replay gave input back (a seek resumed live): the devices hold the restored state, so the next sample
        // goes in whole
        std::lock_guard<std::mutex> lock(_mutex);
        _lastSample.assign(_connectors.size(), -1);
        _wasOwned = false;
        _mouseButtons = 0;  // the seek cleared the mouse's button sources
    }
    FeedHaptics(frame);

    std::shared_ptr<IHostInputSource> source;
    {
        std::lock_guard<std::mutex> lock(_sourceMutex);
        source = _source;
    }
    if (!source)
        return;

    _sample.Clear();
    source->Sample(frame, _sample);
    for (const ConnectorSample& sample : _sample.connectors)
    {
        int index = -1;
        {
            std::lock_guard<std::mutex> lock(_mutex);
            index = FindLocked(sample.connector);
            if (index < 0 || _lastSample[static_cast<size_t>(index)] == sample.mask)
                continue;
        }
        // Not under the lock: the submit may apply here and come back through ApplyConnectorState
        if (Submit(static_cast<size_t>(index), sample.mask))
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (static_cast<size_t>(index) < _lastSample.size())
                _lastSample[static_cast<size_t>(index)] = sample.mask;
        }
    }
    SubmitHostKeys(_sample.keys);
    SubmitMouse(_sample.mouse);
}

void ControllerManager::SubmitHostKeys(const std::vector<uint8_t>& keys)
{
    std::array<uint8_t, kHostKeyRows> rows{};
    for (uint8_t key : keys)
    {
        uint8_t row = 0;
        uint8_t bit = 0;
        if (Keyboard::MatrixPosition(static_cast<ZXKeysEnum>(key), row, bit))
            rows[row] = static_cast<uint8_t>(rows[row] | (1u << bit));
    }
    for (uint8_t row = 0; row < kHostKeyRows; row++)
    {
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if (_hostKeys[row] == rows[row])
                continue;
        }
        ttd::TTDInputEvent ev;
        ev.kind = ttd::TTDInputKind::HostKeys;
        ev.key = row;
        ev.dx = rows[row];
        // Not under the lock: the submit may apply here and come back through ApplyHostKeys
        if (_context && _context->pTimeTravelHooks)
            _context->pTimeTravelHooks->SubmitLiveInput(ev);
        else
            ttd::ApplyInputEvent(ev, ttd::InputDevicesOf(_context));
    }
}

void ControllerManager::SubmitMouse(const MouseSample& mouse)
{
    MouseManager* manager = _context ? _context->pMouseManager : nullptr;
    if (!manager)
        return;
    // The journaled path when the machine has one (DebugMouseManager), as the host mouse does
    DebugMouseManager* funnel =
        _context->pDebugManager ? _context->pDebugManager->GetMouseManager() : nullptr;
    if (mouse.dx != 0 || mouse.dy != 0)
    {
        if (funnel)
            funnel->ApplyHostMove(mouse.dx, mouse.dy);
        else
            manager->ApplyMotion(mouse.dx, mouse.dy);
    }
    if (mouse.wheel != 0)
    {
        if (funnel)
            funnel->ApplyHostWheel(mouse.wheel);
        else
            manager->ApplyWheel(mouse.wheel);
    }
    if (mouse.buttons != _mouseButtons)
    {
        _mouseButtons = mouse.buttons;
        if (funnel)
            funnel->ApplyControllerButtons(mouse.buttons);
        else
            manager->ApplyButtons(manager->ComposeButtons(MouseManager::ButtonSource::Controllers, mouse.buttons));
    }
}

bool ControllerManager::ApplyHostKeys(uint8_t row, uint8_t bits)
{
    std::lock_guard<std::mutex> lock(_mutex);
    if (row >= kHostKeyRows)
        return false;
    _hostKeys[row] = static_cast<uint8_t>(bits & 0x1F);
    UpdateOverlay();
    return true;
}

uint8_t ControllerManager::HostKeys(uint8_t row) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return row < kHostKeyRows ? _hostKeys[row] : 0;
}

/// endregion </Input>

void ControllerManager::UpdateOverlay()
{
    uint8_t rows[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    for (const Connector& connector : _connectors)
    {
        if (connector.kind != ConnectorKind::KeyboardJoystick || connector.mask == 0)
            continue;

        const ZXKeysEnum* wiring = nullptr;
        switch (connector.device)
        {
            case RetroDeviceKind::SinclairJoystick:
                wiring = connector.options.sinclairPort == 2 ? kSinclair2Keys : kSinclair1Keys;
                break;
            case RetroDeviceKind::CursorJoystick:
                wiring = kCursorKeys;
                if (connector.options.cursorShift && (connector.mask & kDirections))
                    HoldKey(rows, ZXKEY_CAPS_SHIFT);
                break;
            case RetroDeviceKind::KeysJoystick:
                for (unsigned bit = 0; bit < kOverlayControls; bit++)
                {
                    if (connector.mask & (1u << bit))
                        HoldKey(rows, connector.options.keys[bit]);
                }
                break;
            default:
                break;
        }
        if (!wiring)
            continue;
        // A wired stick has the four directions and one fire button
        for (unsigned bit = 0; bit < 5; bit++)
        {
            if (connector.mask & (1u << bit))
                HoldKey(rows, wiring[bit]);
        }
    }
    // The keys host controllers hold (key, chord and macro targets)
    for (size_t row = 0; row < kHostKeyRows; row++)
        rows[row] = static_cast<uint8_t>(rows[row] & ~_hostKeys[row]);
    if (_context && _context->pKeyboard)
        _context->pKeyboard->SetOverlay(rows);
}

void ControllerManager::UpdateKempston()
{
    if (!_context || !_context->pJoystick)
        return;
    for (const Connector& connector : _connectors)
    {
        if (connector.kind != ConnectorKind::Kempston)
            continue;
        _context->pJoystick->SetExtended(connector.options.extended);
        _context->pJoystick->SetIm2Leak(connector.options.im2Leak);
        return;
    }
    // No Kempston connector: the device keeps the original interface's view
    _context->pJoystick->SetExtended(true);
    _context->pJoystick->SetIm2Leak(false);
}

size_t ControllerManager::JournaledCount() const
{
    return static_cast<size_t>(std::count_if(_connectors.begin(), _connectors.end(), Journaled));
}

bool ControllerManager::HasOverlayDevices() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return std::any_of(_connectors.begin(), _connectors.end(), [](const Connector& c) {
        return c.kind == ConnectorKind::KeyboardJoystick && c.device != RetroDeviceKind::None;
    });
}

void ControllerManager::AddTtdFingerprint(ttd::TTDConfigFingerprint& fp) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (const Connector& connector : _connectors)
    {
        // Only what was plugged: a machine with empty sockets keeps the fingerprint it had before C1
        if (connector.kind != ConnectorKind::KeyboardJoystick || connector.fixed ||
            connector.device == RetroDeviceKind::None)
            continue;
        uint8_t bytes[kPlugBytes];
        SavePlug(connector, bytes);
        uint64_t hash = 0xcbf29ce484222325ull;
        for (uint8_t b : bytes)
        {
            hash ^= b;
            hash *= 0x100000001b3ull;
        }
        // The checkpoint holds the plug: a restore across the change is exact
        fp.Add("controllers." + connector.id, hash, false);
    }
    // The Kempston options, when not the default: they cannot change while recording; a replay under others is not exact
    for (const Connector& connector : _connectors)
    {
        if (connector.kind == ConnectorKind::Kempston && (!connector.options.extended || connector.options.im2Leak))
            fp.Add("controllers." + connector.id,
                   (connector.options.extended ? 1u : 0u) | (connector.options.im2Leak ? 2u : 0u), false);
    }
}

/// region <TTD>

size_t ControllerManager::TTDStateSize() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return 2 + (kPlugBytes + 2) * JournaledCount() + kHostKeyRows;
}

void ControllerManager::TTDSaveState(uint8_t* dst) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    dst[0] = kControllersTTDVersion;
    dst[1] = static_cast<uint8_t>(JournaledCount());
    size_t offset = 2;
    for (const Connector& connector : _connectors)
    {
        if (!Journaled(connector))
            continue;
        SavePlug(connector, dst + offset);
        offset += kPlugBytes;
        dst[offset++] = static_cast<uint8_t>(connector.mask & 0xFF);
        dst[offset++] = static_cast<uint8_t>(connector.mask >> 8);
    }
    for (uint8_t row : _hostKeys)
        dst[offset++] = row;
}

void ControllerManager::TTDLoadState(const uint8_t* src)
{
    std::unique_lock<std::mutex> lock(_mutex);
    // The connector set is the machine's; a blob of another set changes nothing
    const uint8_t version = src[0];
    if (version < 1 || version > kControllersTTDVersion || src[1] != JournaledCount())
        return;
    size_t offset = 2;
    bool replugged = false;
    for (size_t i = 0; i < _connectors.size(); i++)
    {
        Connector& connector = _connectors[i];
        if (!Journaled(connector))
            continue;
        if (version >= 2)
        {
            // A seek before a plug made while recording takes it back; a fixed socket keeps its device
            Connector restored = connector;
            LoadPlug(src + offset, restored);
            offset += kPlugBytes;
            const bool accepted = restored.device == RetroDeviceKind::None ||
                                  std::find(connector.accepts.begin(), connector.accepts.end(), restored.device) !=
                                      connector.accepts.end();
            if (!connector.fixed && accepted &&
                (restored.device != connector.device || !SameOptions(restored.options, connector.options)))
            {
                connector.device = restored.device;
                connector.options = restored.options;
                _lastSample[i] = -1;
                replugged = true;
                _posts.push_back({connector.id, RetroDeviceKindName(connector.device), 0});
            }
        }
        connector.mask = static_cast<uint16_t>(src[offset] | (src[offset + 1] << 8));
        offset += 2;
    }
    // v1 and v2 had no host key overlay: nothing held
    for (size_t row = 0; row < kHostKeyRows; row++)
        _hostKeys[row] = version >= 3 ? static_cast<uint8_t>(src[offset + row] & 0x1F) : uint8_t{0};
    if (replugged)
        _revision++;
    for (QueuedPost& post : _posts)
        post.revision = _revision;
    UpdateOverlay();
    lock.unlock();
    FlushPosts();
}

uint64_t ControllerManager::TTDHashState() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    // FNV-1a over the keyboard joysticks' plugs and masks
    uint64_t hash = 0xcbf29ce484222325ull;
    auto mix = [&hash](uint8_t b) {
        hash ^= b;
        hash *= 0x100000001b3ull;
    };
    for (const Connector& connector : _connectors)
    {
        if (!Journaled(connector))
            continue;
        uint8_t bytes[kPlugBytes];
        SavePlug(connector, bytes);
        for (uint8_t b : bytes)
            mix(b);
        mix(static_cast<uint8_t>(connector.mask & 0xFF));
        mix(static_cast<uint8_t>(connector.mask >> 8));
    }
    // Only when held: a machine no host key touched hashes as before
    if (std::any_of(_hostKeys.begin(), _hostKeys.end(), [](uint8_t row) { return row != 0; }))
    {
        for (uint8_t row : _hostKeys)
            mix(row);
    }
    return hash;
}

/// endregion </TTD>

}  // namespace controllers
