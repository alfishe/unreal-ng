#pragma once

/// @file netstatetail.h
/// @brief What a fixed network state blob could not hold (owner decision 2026-10-08, TTD state-registry gap 16).
///
/// The network blobs (netstate.h) are fixed-size structures: the registers, the socket tables and the first
/// items of every list (unsent bytes, receive packets, backlog chunks, received runs). Received bytes are
/// journal references (option A of the network TDD). Two things did not fit and were marked "incomplete":
/// a list longer than its fixed array, and bytes the journal does not hold (received before the recording
/// started). Both now go into a tail written after the fixed part, only when there is something to write:
///   - an item past the fixed array, in the array's own element format (`Add`);
///   - the bytes of an item the journal cannot give back, inline (`AddBytes`).
/// The tail is a list of records [u16 list][u16 owner][u32 index][u32 size][bytes], preceded by its own
/// length; `owner` is the socket / slot number, `index` the item's position in its list.
///
/// Worked example: a socket holds 70 receive packets, the first two received before the recording. The fixed
/// array takes packets 0-63; the tail gets packets 64-69 (six `Add` records of netstate::RxPacket) and the
/// bytes of packets 0 and 1 (two `AddBytes` records, index 0 and 1). The loader rebuilds all 70.

#include <cstdint>
#include <cstring>
#include <vector>

#include "emulator/io/network/netstate.h"

namespace netstate
{

class Tail
{
public:
    /// The lists a tail record belongs to (stable numbers: they are in recorded sessions)
    enum class List : uint16_t
    {
        W5300Tx = 1,            ///< unsent bytes past kMaxTxBytes (one bytes record, index 0)
        W5300Packet = 2,        ///< receive packets past kMaxPackets (netstate::RxPacket)
        W5300PacketBytes = 3,   ///< a receive packet's bytes the journal does not hold (index = packet)
        W5300Backlog = 4,       ///< backlog chunks past kMaxBacklog (netstate::Reference)
        W5300BacklogBytes = 5,  ///< a backlog chunk's bytes the journal does not hold (index = chunk)
        NetSocket = 10,         ///< virtual-network sockets past kMaxNetSockets (netstate::NetSocket)
        NetListener = 11,       ///< guest servers past kMaxListeners (netstate::Listener, its first waiting / pending)
        NetWaiting = 12,        ///< a guest server's waiting connections past kMaxWaiting (u16 each; owner = server)
        NetPending = 13,        ///< a guest server's queued clients past kMaxPending (the pending element; owner = server)
        NetLease = 14,          ///< DHCP leases past kMaxLeases (netstate::Lease)
        ComLoopback = 20,       ///< loopback echo bytes past kMaxComBytes (one bytes record, index 0)
        ComRun = 21,            ///< a stream peer's received runs past kMaxComRuns (netstate::Reference)
        ComRunBytes = 22,       ///< a received run's bytes the journal does not hold (index = run)
        ComUnsent = 23,         ///< a stream peer's unsent bytes past kMaxComBytes (one bytes record, index 0)
        EspRx = 30,             ///< ESP module: bytes from the ZX past kEspRxBytes (one bytes record, index 0)
        EspOut = 31,            ///< ESP module: reply bytes past kEspOutBytes (one bytes record, index 0)
        EspRun = 32,            ///< a slot's unread runs past kEspRuns (netstate::Reference; owner = slot)
        EspRunBytes = 33,       ///< a slot run's bytes the journal does not hold (owner = slot, index = run)
        EspDatagram = 34,       ///< a slot's datagrams past kEspDatagrams (netstate::EspDatagram; owner = slot)
        EspDatagramBytes = 35,  ///< a datagram's bytes when it is no single journaled run (owner = slot, index = datagram)
        EspPending = 36,        ///< a server slot's queued clients past kEspPending (netstate::EspPending; owner = slot)
        EspPendingRun = 37,     ///< a queued client's runs past kEspPendingRuns (owner = slot, index = client << 16 | run)
        EspPendingRunBytes = 38,///< such a run's bytes the journal does not hold (index as EspPendingRun)
        EspClose = 39,          ///< sockets to close past kEspClose (u16 each, index = position)
    };

    bool Empty() const { return _bytes.empty(); }
    void Clear() { _bytes.clear(); }

    /// One item past a fixed array, in the array's element format
    void Add(List list, uint16_t owner, uint32_t index, const void* item, size_t size)
    {
        AddBytes(list, owner, index, static_cast<const uint8_t*>(item), size);
    }

    /// Bytes of an item, inline
    void AddBytes(List list, uint16_t owner, uint32_t index, const uint8_t* data, size_t size)
    {
        Put16(static_cast<uint16_t>(list));
        Put16(owner);
        Put32(index);
        Put32(static_cast<uint32_t>(size));
        if (size)
            _bytes.insert(_bytes.end(), data, data + size);
    }

    /// [u32 length][records] after `out`
    void AppendTo(std::vector<uint8_t>& out) const
    {
        const uint32_t length = static_cast<uint32_t>(_bytes.size());
        const uint8_t* l = reinterpret_cast<const uint8_t*>(&length);
        out.insert(out.end(), l, l + 4);
        out.insert(out.end(), _bytes.begin(), _bytes.end());
    }

    /// The tail written by AppendTo at `src`; empty when malformed
    static Tail Read(const uint8_t* src)
    {
        Tail t;
        uint32_t length = 0;
        std::memcpy(&length, src, 4);
        t._bytes.assign(src + 4, src + 4 + length);
        if (!t.Valid())
            t._bytes.clear();
        return t;
    }

    /// The bytes of record (list, owner, index), or nullptr
    const std::vector<uint8_t>* Find(List list, uint16_t owner, uint32_t index) const
    {
        Scan([&](uint16_t l, uint16_t o, uint32_t i, const uint8_t* data, uint32_t size) {
            if (l == static_cast<uint16_t>(list) && o == owner && i == index)
            {
                _found.assign(data, data + size);
                return true;
            }
            return false;
        });
        return _hit ? &_found : nullptr;
    }

    /// Every record of (list, owner) in tail order: f(index, data, size)
    template <typename F>
    void ForEach(List list, uint16_t owner, F&& f) const
    {
        Scan([&](uint16_t l, uint16_t o, uint32_t i, const uint8_t* data, uint32_t size) {
            if (l == static_cast<uint16_t>(list) && o == owner)
                f(i, data, size);
            return false;
        });
    }

private:
    void Put16(uint16_t v) { _bytes.insert(_bytes.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 2); }
    void Put32(uint32_t v) { _bytes.insert(_bytes.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 4); }

    bool Valid() const
    {
        size_t p = 0;
        while (p < _bytes.size())
        {
            if (p + 12 > _bytes.size())
                return false;
            uint32_t size = 0;
            std::memcpy(&size, _bytes.data() + p + 8, 4);
            if (size > _bytes.size() - p - 12)
                return false;
            p += 12 + size;
        }
        return true;
    }

    /// f(list, owner, index, data, size) for each record until it returns true
    template <typename F>
    void Scan(F&& f) const
    {
        _hit = false;
        for (size_t p = 0; p + 12 <= _bytes.size();)
        {
            uint16_t list = 0;
            uint16_t owner = 0;
            uint32_t index = 0;
            uint32_t size = 0;
            std::memcpy(&list, _bytes.data() + p, 2);
            std::memcpy(&owner, _bytes.data() + p + 2, 2);
            std::memcpy(&index, _bytes.data() + p + 4, 4);
            std::memcpy(&size, _bytes.data() + p + 8, 4);
            if (f(list, owner, index, _bytes.data() + p + 12, size))
            {
                _hit = true;
                return;
            }
            p += 12 + size;
        }
    }

    std::vector<uint8_t> _bytes;
    mutable std::vector<uint8_t> _found;
    mutable bool _hit = false;
};

/// Received bytes ({value, source, offset} items) as runs of one journal record: `runs[0, max)` first, the runs
/// past it as `list` records; a run of bytes the journal does not hold (source 0) is a Reference {0, 0, length}
/// with its bytes as a `bytesList` record of the run's index. Indices are `indexBase` + the run's position.
/// Returns the run count
template <typename It>
uint32_t SaveRuns(It begin, It end, Reference* runs, uint32_t max, Tail& tail, Tail::List list, Tail::List bytesList,
                  uint16_t owner, uint32_t indexBase = 0)
{
    uint32_t count = 0;
    Reference current{};
    std::vector<uint8_t> unjournaled;
    auto close = [&]() {
        if (current.length == 0)
            return;
        if (count < max)
            runs[count] = current;
        else
            tail.Add(list, owner, indexBase + count, &current, sizeof(current));
        if (current.source == 0)
            tail.AddBytes(bytesList, owner, indexBase + count, unjournaled.data(), unjournaled.size());
        ++count;
        current = {};
        unjournaled.clear();
    };
    for (It it = begin; it != end; ++it)
    {
        const bool extends = current.length && (it->source == 0 ? current.source == 0
                                                                : current.source == it->source &&
                                                                      current.sourceOffset + current.length == it->offset);
        if (!extends)
        {
            close();
            current = {it->source, it->source ? it->offset : 0, 0};
        }
        ++current.length;
        if (it->source == 0)
            unjournaled.push_back(it->value);
    }
    close();
    return count;
}

/// The bytes of run `index` (SaveRuns): the journal's, or the tail's inline ones. False when neither has them
template <typename ByteSource>
bool RunBytes(const Reference& run, const Tail& tail, Tail::List bytesList, uint16_t owner, uint32_t index,
              const ByteSource& bytes, std::vector<uint8_t>& out)
{
    if (run.source == 0)
    {
        const std::vector<uint8_t>* own = tail.Find(bytesList, owner, index);
        if (!own || own->size() != run.length)
            return false;
        out = *own;
        return true;
    }
    return bytes && bytes(run.source, run.sourceOffset, run.length, out) && out.size() == run.length;
}

/// Back from SaveRuns: the fixed runs (`count` of them, at most `max`), then the tail's whose index matches
/// `indexBase` under `group` (several lists sharing one owner: EspPendingRun). Items are pushed as
/// {value, source, offset}. False when some bytes were missing (the rest are loaded)
template <typename ByteSource, typename Container>
bool LoadRuns(const Reference* runs, uint32_t count, uint32_t max, const Tail& tail, Tail::List list,
              Tail::List bytesList, uint16_t owner, const ByteSource& bytes, Container& out, uint32_t indexBase = 0,
              uint32_t group = 0)
{
    bool complete = true;
    std::vector<uint8_t> chunk;
    auto load = [&](const Reference& run, uint32_t index) {
        if (!RunBytes(run, tail, bytesList, owner, index, bytes, chunk))
        {
            complete = false;
            return;
        }
        for (uint32_t i = 0; i < run.length; ++i)
            out.push_back({chunk[i], run.source, run.source ? run.sourceOffset + i : 0});
    };
    const uint32_t fixed = count < max ? count : max;
    for (uint32_t r = 0; r < fixed; ++r)
        load(runs[r], indexBase + r);
    if (count > max)
        tail.ForEach(list, owner, [&](uint32_t index, const uint8_t* data, uint32_t size) {
            if (size != sizeof(Reference) || (index & group) != (indexBase & group))
                return;
            Reference run;
            std::memcpy(&run, data, sizeof(run));
            load(run, index);
        });
    return complete;
}

}  // namespace netstate
