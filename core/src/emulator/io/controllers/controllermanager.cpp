#include "controllermanager.h"

#include <algorithm>
#include <cctype>
#include <chrono>

#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/timetravelhooks.h"
#include "debugger/ttd/ttdinputapply.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/joystick/joystick.h"
#include "emulator/ports/portdecoder.h"
#include "ihostinputsource.h"

namespace controllers
{

namespace
{
constexpr uint8_t kControllersTTDVersion = 2;

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
    connector.device = src[0] <= static_cast<uint8_t>(RetroDeviceKind::KeysJoystick)
                           ? static_cast<RetroDeviceKind>(src[0])
                           : RetroDeviceKind::None;
    connector.options.sinclairPort = src[1] == 2 ? 2 : 1;
    connector.options.cursorShift = src[2] != 0;
    for (unsigned bit = 0; bit < kOverlayControls; bit++)
        connector.options.keys[bit] = static_cast<ZXKeysEnum>(src[3 + bit]);
}

bool SameOptions(const OverlayOptions& a, const OverlayOptions& b)
{
    return a.sinclairPort == b.sinclairPort && a.cursorShift == b.cursorShift && a.keys == b.keys;
}

Connector MakeConnector(const char* id, const char* alias, ConnectorKind kind, bool fixed, RetroDeviceKind device,
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

    // Every machine: two keyboard-joystick sockets for keys-only games (no card needed)
    built.push_back(
        MakeConnector("keyjoy.1", "", ConnectorKind::KeyboardJoystick, false, RetroDeviceKind::None, overlays));
    built.push_back(
        MakeConnector("keyjoy.2", "", ConnectorKind::KeyboardJoystick, false, RetroDeviceKind::None, overlays));

    std::lock_guard<std::mutex> lock(_mutex);
    unsigned perKind[3] = {0, 0, 0};
    for (Connector& connector : built)
    {
        connector.kindIndex = perKind[static_cast<unsigned>(connector.kind)]++;
        // A connector that survives keeps its plug and what it holds
        for (const Connector& old : _connectors)
        {
            if (old.id != connector.id)
                continue;
            if (!connector.fixed)
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
    if (connector.fixed)
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
}

ControllerResult ControllerManager::Plug(size_t index, RetroDeviceKind device, const OverlayOptions& options)
{
    bool defer = false;
    if (ControllerResult refused = PlugRefused(defer); !refused.Ok())
        return refused;
    std::lock_guard<std::mutex> lock(_mutex);
    if (ControllerResult checked = CheckPlugLocked(index, device); !checked.Ok())
        return checked;

    if (!defer)
    {
        PlugLocked(index, device, options);
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
            if (connector.device == RetroDeviceKind::None)
                return false;
            ev.kind = ttd::TTDInputKind::ConnectorState;
            ev.key = static_cast<uint8_t>(index);
            ev.dx = static_cast<int16_t>(mask);
            return true;
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
    if (connector.kind != ConnectorKind::KeyboardJoystick || connector.device == RetroDeviceKind::None)
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

void ControllerManager::OnFrame(uint64_t frame)
{
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
    }

    std::shared_ptr<IHostInputSource> source;
    {
        std::lock_guard<std::mutex> lock(_sourceMutex);
        source = _source;
    }
    if (!source)
        return;

    _samples.clear();
    source->Sample(frame, _samples);
    for (const ConnectorSample& sample : _samples)
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
    if (_context && _context->pKeyboard)
        _context->pKeyboard->SetOverlay(rows);
}

size_t ControllerManager::OverlayCount() const
{
    return static_cast<size_t>(std::count_if(_connectors.begin(), _connectors.end(), [](const Connector& c) {
        return c.kind == ConnectorKind::KeyboardJoystick;
    }));
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
}

/// region <TTD>

size_t ControllerManager::TTDStateSize() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return 2 + (kPlugBytes + 2) * OverlayCount();
}

void ControllerManager::TTDSaveState(uint8_t* dst) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    dst[0] = kControllersTTDVersion;
    dst[1] = static_cast<uint8_t>(OverlayCount());
    size_t offset = 2;
    for (const Connector& connector : _connectors)
    {
        if (connector.kind != ConnectorKind::KeyboardJoystick)
            continue;
        SavePlug(connector, dst + offset);
        offset += kPlugBytes;
        dst[offset++] = static_cast<uint8_t>(connector.mask & 0xFF);
        dst[offset++] = static_cast<uint8_t>(connector.mask >> 8);
    }
}

void ControllerManager::TTDLoadState(const uint8_t* src)
{
    std::lock_guard<std::mutex> lock(_mutex);
    // The connector set is the machine's; a blob of another set changes nothing
    const uint8_t version = src[0];
    if ((version != 1 && version != kControllersTTDVersion) || src[1] != OverlayCount())
        return;
    size_t offset = 2;
    bool replugged = false;
    for (size_t i = 0; i < _connectors.size(); i++)
    {
        Connector& connector = _connectors[i];
        if (connector.kind != ConnectorKind::KeyboardJoystick)
            continue;
        if (version == kControllersTTDVersion)
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
            }
        }
        connector.mask = static_cast<uint16_t>(src[offset] | (src[offset + 1] << 8));
        offset += 2;
    }
    if (replugged)
        _revision++;
    UpdateOverlay();
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
        if (connector.kind != ConnectorKind::KeyboardJoystick)
            continue;
        uint8_t bytes[kPlugBytes];
        SavePlug(connector, bytes);
        for (uint8_t b : bytes)
            mix(b);
        mix(static_cast<uint8_t>(connector.mask & 0xFF));
        mix(static_cast<uint8_t>(connector.mask >> 8));
    }
    return hash;
}

/// endregion </TTD>

}  // namespace controllers
