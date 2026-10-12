#include "stdafx.h"

#include "emulator/bus/busmanager.h"

#include <algorithm>

#include "emulator/bus/bustap.h"

BusManager::~BusManager()
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (BusTap* tap : _taps)
        tap->_manager = nullptr;
    _taps.clear();
}

void BusManager::Register(BusTap* tap)
{
    if (!tap)
        return;
    std::lock_guard<std::mutex> lock(_mutex);
    if (std::find(_taps.begin(), _taps.end(), tap) != _taps.end())
        return;
    // The base id, without a suffix a previous registration gave it
    const std::string& base = tap->_baseId;
    auto taken = [&](const std::string& id) {
        return std::any_of(_taps.begin(), _taps.end(), [&](const BusTap* other) { return other->Id() == id; });
    };
    std::string id = base;
    for (int n = 2; taken(id); n++)
        id = base + "." + std::to_string(n);
    tap->SetIdLocked(id);
    tap->_manager = this;
    _taps.push_back(tap);
}

void BusManager::Unregister(BusTap* tap)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _taps.erase(std::remove(_taps.begin(), _taps.end(), tap), _taps.end());
    if (tap)
        tap->_manager = nullptr;
}

bool BusManager::With(const std::string& id, const std::function<void(BusTap&)>& fn) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (BusTap* tap : _taps)
    {
        if (tap->Id() == id)
        {
            fn(*tap);
            return true;
        }
    }
    return false;
}

void BusManager::ForEach(const std::function<void(BusTap&)>& fn) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (BusTap* tap : _taps)
        fn(*tap);
}

std::vector<std::string> BusManager::Ids() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<std::string> ids;
    for (const BusTap* tap : _taps)
        ids.push_back(tap->Id());
    return ids;
}

size_t BusManager::Count() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _taps.size();
}

void BusManager::NextEpoch()
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (BusTap* tap : _taps)
        tap->NextEpoch();
}
