#include "protocolpads.h"

#include <algorithm>
#include <utility>

namespace controllers
{

bool ProtocolPads::Handles(uint16_t vendor, uint16_t product, const std::string& productName)
{
    return SonyPad::ModelOf(vendor, product).has_value() || SwitchProPad::Handles(vendor, product, productName) ||
           SwitchProPad::IsChargingGrip(vendor, product) || SteamPad::ModelOf(vendor, product).has_value() ||
           GameCubeAdapter::Handles(vendor, product);
}

bool ProtocolPads::TakesCollection(uint16_t vendor, uint16_t product, uint16_t usagePage,
                                   const std::string& productName)
{
    if (!Handles(vendor, product, productName))
        return false;
    constexpr uint16_t kVendorPages = 0xFF00;
    return !SteamPad::ModelOf(vendor, product).has_value() || usagePage >= kVendorPages;
}

HostDeviceId ProtocolPads::PortId(const HostDeviceId& adapter, int port)
{
    return adapter + ":p" + std::to_string(port + 1);
}

size_t ProtocolPads::ReportSize(uint16_t vendor, uint16_t product, bool bluetooth, const uint8_t* report, size_t size)
{
    const std::optional<SonyModel> model = SonyPad::ModelOf(vendor, product);
    if (!model || size == 0)
        return size;
    // SonyPad's table: USB 0x01 of 64 bytes; Bluetooth 0x31 (DualSense) / 0x11 (DualShock 4) of 78
    constexpr size_t kUsbSize = 64;
    constexpr size_t kBluetoothSize = 78;
    const uint8_t bluetoothId = *model == SonyModel::DualShock4 ? 0x11 : 0x31;
    if (!bluetooth && report[0] == 0x01 && size > kUsbSize)
        return kUsbSize;
    if (bluetooth && report[0] == bluetoothId && size > kBluetoothSize)
        return kBluetoothSize;
    return size;
}

ProtocolPads::ProtocolPads(IBackendSink& sink) : ProtocolPads(sink, Options{})
{
}

ProtocolPads::ProtocolPads(IBackendSink& sink, Options options) : _sink(sink), _options(std::move(options))
{
}

ProtocolPads::~ProtocolPads()
{
    std::map<std::string, std::unique_ptr<Device>> devices;
    {
        std::lock_guard lock(_mutex);
        devices.swap(_devices);
    }
    // Outside the lock: each pump writes its final report (the motors stop)
    devices.clear();
}

bool ProtocolPads::Add(Endpoint endpoint)
{
    const std::optional<SonyModel> sony = SonyPad::ModelOf(endpoint.vendor, endpoint.product);
    const std::optional<SwitchLayout> switchLayout =
        SwitchProPad::LayoutFor(endpoint.vendor, endpoint.product, endpoint.productName);
    const bool grip = SwitchProPad::IsChargingGrip(endpoint.vendor, endpoint.product);
    const std::optional<SteamModel> steam = SteamPad::ModelOf(endpoint.vendor, endpoint.product);
    const bool gameCube = GameCubeAdapter::Handles(endpoint.vendor, endpoint.product);
    if (!sony && !switchLayout && !grip && !steam && !gameCube)
        return false;

    auto device = std::make_unique<Device>();
    device->key = endpoint.key;
    device->id = endpoint.info.id;
    device->reported = endpoint.info.id;
    device->bluetooth = endpoint.bluetooth;
    device->vendor = endpoint.vendor;
    device->product = endpoint.product;
    device->hid = std::move(endpoint.hid);
    device->outputsUnavailable = endpoint.outputsUnavailable;
    const bool writable = static_cast<bool>(endpoint.write);
    if (sony)
    {
        device->sony = std::make_unique<SonyPad>(*sony);
        device->light = true;
        device->actuators = *sony == SonyModel::DualShock4 ? static_cast<uint8_t>(kRumbleLow | kRumbleHigh)
                                                           : static_cast<uint8_t>(kRumbleLow | kRumbleHigh |
                                                                                  kRumbleLeftTrigger |
                                                                                  kRumbleRightTrigger);
        if (writable)
            device->pump = std::make_unique<PadPump>(std::make_unique<SonyProtocol>(*sony, endpoint.bluetooth),
                                                     std::move(endpoint.write), std::move(endpoint.getFeature),
                                                     _options.clock);
    }
    else if (steam)
    {
        // Its own reports (no usable descriptor), out of lizard mode first through feature reports; a dongle's slot
        // shows once a pad is on it
        device->steam = std::make_unique<SteamPad>(*steam);
        device->zeroReportIdFirst = endpoint.zeroReportIdFirst && !SteamPad::IsTriton(*steam);
        device->slotPending = SteamPad::IsDongle(*steam);
        device->noRumble = "The emulator drives no Steam Controller haptics yet";
        if (endpoint.setFeature)
            device->pump = std::make_unique<PadPump>(std::make_unique<SteamProtocol>(*steam), PadPump::Write{},
                                                     PadPump::GetFeature{}, _options.clock,
                                                     std::move(endpoint.setFeature));
        else if (device->outputsUnavailable.empty())
            device->outputsUnavailable = "No feature reports: the Steam Controller stays in lizard mode";
    }
    else if (gameCube)
    {
        // Four controllers on one device: the adapter is never reported, each port with a controller is (OnGameCube).
        // It sends nothing before its start report: no writes, no adapter
        if (!writable)
            return false;
        device->gameCube = std::make_unique<GameCubeAdapter>();
        device->slotPending = true;
        device->noRumble = "The emulator drives no GameCube adapter rumble yet";
        device->pump = std::make_unique<PadPump>(std::make_unique<GameCubeProtocol>(), std::move(endpoint.write),
                                                 std::move(endpoint.getFeature), _options.clock);
    }
    else
    {
        const SwitchLayout layout = switchLayout.value_or(SwitchLayout::Pro);
        device->switchPro = std::make_unique<SwitchProPad>(layout);
        if (SwitchProPad::HasRumble(layout))
            device->actuators = kRumbleLow | kRumbleHigh;
        else
            device->noRumble = "This Nintendo Switch Online pad has no motors";
        if (SwitchProPad::IsJoyCon(layout))
            device->joyCon = layout == SwitchLayout::JoyConLeft ? 1 : 2;
        // A half of the charging grip: its side from the device info, then a Joy-Con of that side
        SwitchProtocol::TypeSink onType;
        if (grip)
        {
            device->gripPending = true;
            std::shared_ptr<std::atomic<int>> type = device->gripType;
            onType = [type](uint8_t value) { type->store(value); };
        }
        if (writable)
        {
            // The calibration comes with a subcommand reply, on the reading thread (PadPump::Input): the decoder is its
            SwitchProPad* decoder = device->switchPro.get();
            device->pump = std::make_unique<PadPump>(
                std::make_unique<SwitchProtocol>(
                    endpoint.bluetooth,
                    [decoder](const SwitchStickCalibration& calibration) { decoder->SetCalibration(calibration); },
                    _options.switchStartDelayMs, std::move(onType)),
                std::move(endpoint.write), std::move(endpoint.getFeature), _options.clock);
        }
        else if (grip)
            return false;  // no device info without writes: which half it is stays unknown
    }

    HostDeviceInfo info = endpoint.info;
    Device* raw = device.get();
    bool show = !device->gripPending && !device->slotPending;
    Events events;
    {
        std::lock_guard lock(_mutex);
        if (_devices.count(device->key) != 0)
            return true;
        // The player LEDs: the lowest number no other pad shows
        if (device->pump && (device->sony || device->switchPro))
        {
            int player = 1;
            while (std::any_of(_devices.begin(), _devices.end(),
                               [player](const auto& entry) { return entry.second->player == player; }))
                ++player;
            device->player = player;
            device->pump->SetPlayer(player);
        }
        device->single = info;
        device->shown = show;
        _devices[raw->key] = std::move(device);
        if (show && raw->joyCon != 0)
        {
            // With a Joy-Con of the other side that waits: one pad (its own device never shows)
            const JoyConPairing::Change change = _joyCons.Add(raw->id, raw->joyCon == 1);
            if (change.formed)
            {
                raw->shown = false;
                show = false;
            }
            ApplyJoyConChangeLocked(change, {}, events);
        }
    }
    if (show)
        _sink.OnDeviceAdded(info);
    Report(events);
    return true;
}

void ProtocolPads::Remove(const std::string& key)
{
    std::unique_ptr<Device> device;
    Events events;
    {
        std::lock_guard lock(_mutex);
        const auto it = _devices.find(key);
        if (it == _devices.end())
            return;
        device = std::move(it->second);
        _devices.erase(it);
        if (device->shown)
            events.removed.push_back(device->id);
        for (int port = 0; port < GameCubeAdapter::kPorts; port++)
            if (device->portShown[static_cast<size_t>(port)])
                events.removed.push_back(PortId(device->id, port));
        if (device->joyCon != 0 && !device->gripPending)
            ApplyJoyConChangeLocked(_joyCons.Remove(device->id), device->id, events);
    }
    Report(events);
    // The pump ends with the device, outside the lock: a write in flight finishes (a gone device refuses it at once)
}

void ProtocolPads::Clear()
{
    std::map<std::string, std::unique_ptr<Device>> devices;
    std::vector<HostDeviceId> gone;
    {
        std::lock_guard lock(_mutex);
        for (const auto& [key, device] : _devices)
        {
            if (!device->gripPending && !device->slotPending &&
                std::find(gone.begin(), gone.end(), device->reported) == gone.end())
                gone.push_back(device->reported);  // a Joy-Con pair once
            for (int port = 0; port < GameCubeAdapter::kPorts; port++)
                if (device->portShown[static_cast<size_t>(port)])
                    gone.push_back(PortId(device->id, port));
        }
        devices.swap(_devices);
        _joyCons = JoyConPairing();
    }
    devices.clear();
    for (const HostDeviceId& id : gone)
        _sink.OnDeviceRemoved(id);
}

void ProtocolPads::Input(const std::string& key, const uint8_t* report, size_t size, uint64_t timeNs)
{
    if (report == nullptr || size == 0)
        return;
    Device* found = nullptr;
    {
        std::lock_guard lock(_mutex);
        const auto it = _devices.find(key);
        if (it != _devices.end())
            found = it->second.get();  // only this thread erases
    }
    if (found == nullptr)
        return;
    Device& device = *found;
    if (device.zeroReportIdFirst)
    {
        if (report[0] != 0 || size < 2)
            return;
        ++report;
        --size;
    }
    // The protocol sees every report first (subcommand replies, acknowledgements, a dongle's wireless events)
    if (device.pump)
        device.pump->Input(report, size);
    if (device.gameCube)
    {
        OnGameCube(device, report, size, timeNs);
        return;
    }
    if (device.steam)
    {
        OnSteam(device, report, size, timeNs);
        return;
    }
    if (device.gripPending)
    {
        ResolveGrip(device);
        if (device.gripPending)
            return;  // which half it is decides what its buttons are
    }
    const auto sink = [this, &device, timeNs](const std::string& control, float value) {
        _sink.OnControl({device.reported, control, value, timeNs});
    };
    const size_t full = ReportSize(device.vendor, device.product, device.bluetooth, report, size);
    const bool decoded =
        device.sony ? device.sony->Decode(report, full, sink) : device.switchPro->Decode(report, full, sink);
    if (decoded)
    {
        const BatteryState battery = device.sony ? device.sony->Battery() : device.switchPro->Battery();
        if (battery != device.lastBattery)
        {
            device.lastBattery = battery;
            std::lock_guard lock(_mutex);
            device.battery = battery;
        }
        return;
    }
    if (device.hid)
        device.hid->Decode(report, size, sink);
}

void ProtocolPads::OnGameCube(Device& device, const uint8_t* report, size_t size, uint64_t timeNs)
{
    std::vector<GameCubeAdapter::PortChange> changes;
    std::vector<std::pair<int, std::pair<std::string, float>>> controls;
    if (!device.gameCube->Decode(
            report, size,
            [&controls](int port, const std::string& control, float value) {
                controls.push_back({port, {control, value}});
            },
            changes))
        return;
    Events events;
    {
        std::lock_guard lock(_mutex);
        for (const GameCubeAdapter::PortChange& change : changes)
        {
            device.portShown[static_cast<size_t>(change.port)] = change.connected;
            if (!change.connected)
            {
                events.removed.push_back(PortId(device.id, change.port));
                continue;
            }
            HostDeviceInfo info = device.single;
            info.id = PortId(device.id, change.port);
            info.name += " (port " + std::to_string(change.port + 1) + ")";
            events.added.push_back(info);
        }
    }
    Report(events);
    for (const auto& [port, control] : controls)
        _sink.OnControl({PortId(device.id, port), control.first, control.second, timeNs});
}

void ProtocolPads::OnSteam(Device& device, const uint8_t* report, size_t size, uint64_t timeNs)
{
    const auto sink = [this, &device, timeNs](const std::string& control, float value) {
        _sink.OnControl({device.reported, control, value, timeNs});
    };
    bool pending = false;
    {
        std::lock_guard lock(_mutex);
        pending = device.slotPending;
    }
    const SteamPad::Event event = device.steam->Decode(report, size, pending ? ControlSink{} : ControlSink(sink));
    if (event == SteamPad::Event::Disconnected)
        device.steam->Release(pending ? ControlSink{} : ControlSink(sink));
    if (!SteamPad::IsDongle(device.steam->Model()))
        return;
    // A dongle's slot: shown when a pad comes to it (its connect event or a state), gone when it goes
    const bool there = event == SteamPad::Event::Connected || event == SteamPad::Event::State;
    const bool gone = event == SteamPad::Event::Disconnected;
    if ((there && !pending) || (gone && pending) || (!there && !gone))
        return;
    Events events;
    {
        std::lock_guard lock(_mutex);
        device.slotPending = gone;
        device.shown = !gone;
        if (gone)
            events.removed.push_back(device.id);
        else
            events.added.push_back(device.single);
    }
    Report(events);
}

/// region <Queries and outputs>

ProtocolPads::Device* ProtocolPads::FindLocked(const HostDeviceId& id) const
{
    for (const auto& [key, device] : _devices)
        if (device->id == id && device->shown)
            return device.get();
    return nullptr;
}

ProtocolPads::Device* ProtocolPads::PortLocked(const HostDeviceId& id) const
{
    for (const auto& [key, device] : _devices)
        if (device->gameCube)
            for (int port = 0; port < GameCubeAdapter::kPorts; port++)
                if (device->portShown[static_cast<size_t>(port)] && PortId(device->id, port) == id)
                    return device.get();
    return nullptr;
}

bool ProtocolPads::Has(const HostDeviceId& device) const
{
    std::lock_guard lock(_mutex);
    return FindLocked(device) != nullptr || PairLocked(device).first != nullptr || PortLocked(device) != nullptr;
}

bool ProtocolPads::IdTaken(const HostDeviceId& id) const
{
    std::lock_guard lock(_mutex);
    for (const auto& [key, device] : _devices)
        if (device->id == id || device->reported == id)
            return true;
    return false;
}

size_t ProtocolPads::DeviceCount() const
{
    std::lock_guard lock(_mutex);
    size_t count = 0;
    for (const auto& [key, device] : _devices)
    {
        count += device->shown ? 1 : 0;
        count += static_cast<size_t>(std::count(device->portShown.begin(), device->portShown.end(), true));
    }
    return count + _joyCons.PairCount();
}

uint8_t ProtocolPads::RumbleActuators(const HostDeviceId& device) const
{
    std::lock_guard lock(_mutex);
    if (const auto [left, right] = PairLocked(device); left != nullptr)
        return left->pump && right->pump ? static_cast<uint8_t>(kRumbleLow | kRumbleHigh) : 0;
    const Device* known = FindLocked(device);
    return known != nullptr && known->pump ? known->actuators : 0;
}

std::string ProtocolPads::RumbleUnavailable(const HostDeviceId& device) const
{
    std::lock_guard lock(_mutex);
    if (const auto [left, right] = PairLocked(device); left != nullptr)
        return left->pump && right->pump ? std::string()
                                         : (left->pump ? right : left)->outputsUnavailable;
    if (const Device* adapter = PortLocked(device))
        return adapter->noRumble;
    const Device* known = FindLocked(device);
    if (known == nullptr)
        return "The device is not connected";
    if (!known->pump)
        return known->outputsUnavailable;
    return known->actuators != 0 ? std::string() : known->noRumble;
}

bool ProtocolPads::SetRumble(const HostDeviceId& device, const RumbleLevels& levels)
{
    std::lock_guard lock(_mutex);
    if (const auto [left, right] = PairLocked(device); left != nullptr)
    {
        // A Joy-Con pair: the left half the low motor's level, the right one the high one's (SDL)
        if (left->pump)
            left->pump->SetRumble(SwitchProPad::HalfRumble(levels, true));
        if (right->pump)
            right->pump->SetRumble(SwitchProPad::HalfRumble(levels, false));
        return left->pump || right->pump;
    }
    Device* known = FindLocked(device);
    if (known == nullptr || !known->pump || known->actuators == 0)
        return false;
    // Queued: a DualSense's trigger levels become their resistance (haptics.md §6.4), a Switch pad plays the HD
    // frequencies of the levels (SwitchRumble::Pair)
    known->pump->SetRumble(levels);
    return true;
}

bool ProtocolPads::HasLight(const HostDeviceId& device) const
{
    std::lock_guard lock(_mutex);
    const Device* known = FindLocked(device);
    return known != nullptr && known->light && known->pump;
}

std::string ProtocolPads::LightUnavailable(const HostDeviceId& device) const
{
    std::lock_guard lock(_mutex);
    if (PairLocked(device).first != nullptr)
        return "Joy-Cons have no light the emulator can set (their player lights show the player)";
    if (PortLocked(device) != nullptr)
        return "A GameCube controller has no light";
    const Device* known = FindLocked(device);
    if (known == nullptr)
        return "The device is not connected";
    if (!known->light)
    {
        if (known->steam)
            return "The emulator sets no Steam Controller light";
        if (known->joyCon != 0)
            return "Joy-Cons have no light the emulator can set (their player lights show the player)";
        return "This Switch pad has no light the emulator can set (its player lights show the player)";
    }
    return known->pump ? std::string() : known->outputsUnavailable;
}

bool ProtocolPads::SetLight(const HostDeviceId& device, const LightColor& color)
{
    std::lock_guard lock(_mutex);
    Device* known = FindLocked(device);
    // The light bar report through the pad's pump (a DualSense's first one releases the firmware's light bar)
    return known != nullptr && known->light && known->pump && known->pump->SetLight(color);
}

BatteryState ProtocolPads::Battery(const HostDeviceId& device) const
{
    std::lock_guard lock(_mutex);
    if (const auto [left, right] = PairLocked(device); left != nullptr)
        return JoyConPairing::Battery(left->battery, right->battery);
    const Device* known = FindLocked(device);
    return known != nullptr ? known->battery : BatteryState{};
}

int ProtocolPads::Player(const HostDeviceId& device) const
{
    std::lock_guard lock(_mutex);
    if (const auto [left, right] = PairLocked(device); left != nullptr)
        return left->player;
    const Device* known = FindLocked(device);
    return known != nullptr ? known->player : 0;
}

/// endregion </Queries and outputs>

/// region <Joy-Con pairs>

ProtocolPads::Device* ProtocolPads::JoyConLocked(const HostDeviceId& id) const
{
    for (const auto& [key, device] : _devices)
        if (device->joyCon != 0 && device->id == id)
            return device.get();
    return nullptr;
}

std::pair<ProtocolPads::Device*, ProtocolPads::Device*> ProtocolPads::PairLocked(const HostDeviceId& device) const
{
    const std::optional<JoyConPairing::Pair> pair = _joyCons.Find(device);
    if (!pair)
        return {nullptr, nullptr};
    Device* left = JoyConLocked(pair->left);
    Device* right = JoyConLocked(pair->right);
    if (left == nullptr || right == nullptr)
        return {nullptr, nullptr};
    return {left, right};
}

void ProtocolPads::ApplyJoyConChangeLocked(const JoyConPairing::Change& change, const HostDeviceId& gone, Events& out)
{
    if (change.broken)
    {
        out.removed.push_back(change.broken->id);
        const HostDeviceId& alone = change.broken->left == gone ? change.broken->right : change.broken->left;
        if (Device* device = JoyConLocked(alone))
        {
            device->reported = device->id;
            device->switchPro->SetLayout(SwitchProPad::JoyConLayout(device->joyCon == 1, false));
            if (device->pump)
            {
                device->pump->SetPlayer(device->player);
                device->pump->SetRumble({});
            }
            device->shown = true;
            out.added.push_back(device->single);
        }
    }
    if (!change.formed)
        return;
    const JoyConPairing::Pair& pair = *change.formed;
    Device* left = JoyConLocked(pair.left);
    Device* right = JoyConLocked(pair.right);
    if (left == nullptr || right == nullptr)
        return;
    for (Device* half : {left, right})
    {
        if (half->shown)
        {
            // Shown alone until now: gone, or never shown when it came back alone in this same change
            const auto again = std::find_if(out.added.begin(), out.added.end(),
                                            [half](const HostDeviceInfo& info) { return info.id == half->id; });
            if (again != out.added.end())
                out.added.erase(again);
            else
                out.removed.push_back(half->id);
        }
        half->shown = false;
        half->reported = pair.id;
        half->switchPro->SetLayout(SwitchProPad::JoyConLayout(half == left, true));
        if (half->pump)
        {
            half->pump->SetPlayer(left->player);  // both show the pair's number (SDL sets each half's lights)
            half->pump->SetRumble({});
        }
    }
    HostDeviceInfo info = left->single;
    info.id = pair.id;
    info.name = "Joy-Con (L/R)";
    out.added.push_back(info);
}

void ProtocolPads::ResolveGrip(Device& device)
{
    const int type = device.gripType->load();
    const bool left = type == SwitchProtocol::kTypeJoyConLeft;
    if (!left && type != SwitchProtocol::kTypeJoyConRight)
        return;  // another type, or none yet: SDL's "a Joy-Con that's missing from a charging grip slot"
    Events events;
    {
        std::lock_guard lock(_mutex);
        device.gripPending = false;
        device.joyCon = left ? 1 : 2;
        device.switchPro->SetLayout(SwitchProPad::JoyConLayout(left, false));
        device.single.name = left ? "Joy-Con (L)" : "Joy-Con (R)";
        device.shown = true;
        const JoyConPairing::Change change = _joyCons.Add(device.id, left);
        if (change.formed)
            device.shown = false;  // straight into the pair
        else
            events.added.push_back(device.single);
        ApplyJoyConChangeLocked(change, {}, events);
    }
    Report(events);
}

void ProtocolPads::Report(const Events& events)
{
    for (const HostDeviceId& id : events.removed)
        _sink.OnDeviceRemoved(id);
    for (const HostDeviceInfo& info : events.added)
        _sink.OnDeviceAdded(info);
}

/// endregion </Joy-Con pairs>

}  // namespace controllers
