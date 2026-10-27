#pragma once

/// @file busmanager.h
/// @brief One per emulator: knows every bus of the machine and of the cards in its slots
/// (docs/inprogress/2026-10-11-bus-analyzer/technical-design.md §3.3), the way the MediaManager knows every
/// media slot.
///
/// Buses (BusTap) register when their owner attaches them and unregister when the owner goes (a model switch,
/// a card removed). Readers on any thread reach a bus through With / ForEach, which hold the registry lock,
/// so a bus cannot unregister while it is being read. An id taken twice gets a suffix (".2", ".3").

#include <functional>
#include <mutex>
#include <string>
#include <vector>

class BusTap;

class BusManager
{
public:
    BusManager() = default;
    ~BusManager();
    BusManager(const BusManager&) = delete;
    BusManager& operator=(const BusManager&) = delete;

    void Register(BusTap* tap);
    void Unregister(BusTap* tap);

    /// Run `fn` on the bus with this id; false when there is none
    bool With(const std::string& id, const std::function<void(BusTap&)>& fn) const;
    /// Every bus, in registration order
    void ForEach(const std::function<void(BusTap&)>& fn) const;
    std::vector<std::string> Ids() const;
    size_t Count() const;

    /// A machine reset: every bus starts a new epoch
    void NextEpoch();

private:
    mutable std::mutex _mutex;
    std::vector<BusTap*> _taps;
};
