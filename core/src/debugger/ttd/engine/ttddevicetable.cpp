#include "ttddevicetable.h"

#include <algorithm>

namespace ttd
{

namespace
{
std::string Describe(const TTDDeviceKey& key)
{
    return key.instance + " (type " + std::to_string(static_cast<uint16_t>(key.type)) + ")";
}

/// v1's order between two devices free to go either way
bool TieBefore(const TTDDeviceDescriptor& a, const TTDDeviceDescriptor& b)
{
    const auto ia = static_cast<uint16_t>(a.legacyId);
    const auto ib = static_cast<uint16_t>(b.legacyId);
    if (ia != ib)
        return ia < ib;
    return a.Key() < b.Key();
}
}  // namespace

bool TTDDeviceTable::Build(std::vector<TTDDeviceEntry> entries, std::string& error)
{
    const size_t n = entries.size();
    for (size_t i = 0; i < n; ++i)
    {
        const TTDDeviceDescriptor& d = entries[i].descriptor;
        if (d.stateSize == 0)
        {
            error = "device " + Describe(d.Key()) + " declares no state";
            return false;
        }
        for (const TTDTimeField& tf : d.timeFields)
            if ((tf.width != 1 && tf.width != 2 && tf.width != 4 && tf.width != 8) ||
                size_t(tf.offset) + tf.width > d.stateSize)
            {
                error = "device " + Describe(d.Key()) + " declares a time field (offset " + std::to_string(tf.offset) +
                        ", " + std::to_string(tf.width) + " bytes) outside its " + std::to_string(d.stateSize) +
                        " bytes of state";
                return false;
            }
        for (size_t j = 0; j < i; ++j)
            if (entries[j].descriptor.Key() == d.Key())
            {
                error = "device " + Describe(d.Key()) + " is registered twice";
                return false;
            }
    }

    // Dependencies as indices
    std::vector<std::vector<uint32_t>> after(n);   // after[i]: devices that must load before i
    std::vector<uint32_t> waiting(n, 0);
    std::vector<std::vector<uint32_t>> dependents(n);
    for (uint32_t i = 0; i < n; ++i)
    {
        for (const TTDDeviceKey& dep : entries[i].descriptor.restoreAfter)
        {
            if (dep == entries[i].descriptor.Key())
            {
                error = "device " + Describe(dep) + " is to restore after itself";
                return false;
            }
            uint32_t found = static_cast<uint32_t>(n);
            for (uint32_t j = 0; j < n; ++j)
                if (entries[j].descriptor.Key() == dep)
                    found = j;
            if (found == n)
            {
                error = "device " + Describe(entries[i].descriptor.Key()) + " restores after " + Describe(dep) +
                        ", which this machine does not have";
                return false;
            }
            dependents[found].push_back(i);
            ++waiting[i];
        }
    }

    // Kahn's algorithm, the ready device that comes first in v1's order each time
    std::vector<uint32_t> order;
    order.reserve(n);
    std::vector<uint32_t> ready;
    for (uint32_t i = 0; i < n; ++i)
        if (waiting[i] == 0)
            ready.push_back(i);
    while (!ready.empty())
    {
        auto best = std::min_element(ready.begin(), ready.end(), [&](uint32_t a, uint32_t b) {
            return TieBefore(entries[a].descriptor, entries[b].descriptor);
        });
        const uint32_t next = *best;
        ready.erase(best);
        order.push_back(next);
        for (uint32_t d : dependents[next])
            if (--waiting[d] == 0)
                ready.push_back(d);
    }
    if (order.size() != n)
    {
        error = "devices restore after each other in a cycle:";
        for (uint32_t i = 0; i < n; ++i)
            if (waiting[i] != 0)
                error += " " + Describe(entries[i].descriptor.Key());
        return false;
    }

    if (!SameSet(entries, _entries))
        ++_setVersion;
    _entries = std::move(entries);
    _order = std::move(order);
    return true;
}

void TTDDeviceTable::Clear()
{
    if (!_entries.empty())
        ++_setVersion;
    _entries.clear();
    _order.clear();
}

const TTDDeviceEntry* TTDDeviceTable::Find(const TTDDeviceKey& key) const
{
    for (const TTDDeviceEntry& e : _entries)
        if (e.descriptor.Key() == key)
            return &e;
    return nullptr;
}

bool TTDDeviceTable::SameSet(const std::vector<TTDDeviceEntry>& a, const std::vector<TTDDeviceEntry>& b)
{
    if (a.size() != b.size())
        return false;
    for (const TTDDeviceEntry& x : a)
    {
        bool found = false;
        for (const TTDDeviceEntry& y : b)
            if (x.descriptor.Key() == y.descriptor.Key() && x.descriptor.layoutVersion == y.descriptor.layoutVersion &&
                x.descriptor.stateSize == y.descriptor.stateSize)
                found = true;
        if (!found)
            return false;
    }
    return true;
}

}  // namespace ttd
