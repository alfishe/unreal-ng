#include "emulator/io/network/traffic/networktraffictap.h"

#include <algorithm>
#include <cstring>

#include "common/filehelper.h"
#include "emulator/io/network/traffic/socketpacketizer.h"
#include "emulator/io/network/vnet/ethernetaccess.h"

namespace
{
// pcapng (https://www.ietf.org/archive/id/draft-tuexen-opsawg-pcapng-05.html): little-endian blocks, 32-bit aligned
void Put16(std::vector<uint8_t>& v, uint16_t x)
{
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
}
void Put32(std::vector<uint8_t>& v, uint32_t x)
{
    Put16(v, static_cast<uint16_t>(x));
    Put16(v, static_cast<uint16_t>(x >> 16));
}
void Pad(std::vector<uint8_t>& v)
{
    while (v.size() % 4)
        v.push_back(0);
}
void Option(std::vector<uint8_t>& v, uint16_t code, const void* data, size_t length)
{
    Put16(v, code);
    Put16(v, static_cast<uint16_t>(length));
    const auto* p = static_cast<const uint8_t*>(data);
    v.insert(v.end(), p, p + length);
    Pad(v);
}
/// A block: type, total length, body, total length
std::vector<uint8_t> Block(uint32_t type, const std::vector<uint8_t>& body)
{
    std::vector<uint8_t> b;
    const uint32_t length = static_cast<uint32_t>(12 + body.size());
    Put32(b, type);
    Put32(b, length);
    b.insert(b.end(), body.begin(), body.end());
    Put32(b, length);
    return b;
}
std::vector<uint8_t> SectionHeader()
{
    std::vector<uint8_t> body;
    Put32(body, 0x1A2B3C4D);   // byte-order magic
    Put16(body, 1);            // version 1.0
    Put16(body, 0);
    Put32(body, 0xFFFFFFFF);   // section length unknown
    Put32(body, 0xFFFFFFFF);
    const char* app = "unreal-ng";
    Option(body, 4, app, std::strlen(app));   // shb_userappl
    Option(body, 0, nullptr, 0);
    return Block(0x0A0D0D0A, body);
}
std::vector<uint8_t> InterfaceDescription(const std::string& name)
{
    std::vector<uint8_t> body;
    Put16(body, 1);   // LINKTYPE_ETHERNET
    Put16(body, 0);
    Put32(body, 0);   // no snap length
    Option(body, 2, name.data(), name.size());   // if_name
    const uint8_t microseconds = 6;
    Option(body, 9, &microseconds, 1);           // if_tsresol: 10^-6
    Option(body, 0, nullptr, 0);
    return Block(1, body);
}
std::vector<uint8_t> EnhancedPacket(uint32_t interfaceId, const TrafficRecord& r, const std::vector<uint8_t>& bytes)
{
    std::vector<uint8_t> body;
    Put32(body, interfaceId);
    Put32(body, static_cast<uint32_t>(r.time.us >> 32));
    Put32(body, static_cast<uint32_t>(r.time.us));
    Put32(body, static_cast<uint32_t>(bytes.size()));
    Put32(body, static_cast<uint32_t>(bytes.size()));
    body.insert(body.end(), bytes.begin(), bytes.end());
    Pad(body);
    // The TTD position, so a packet seen in Wireshark can be found on the timeline; a socket operation says what it
    // really was (its TCP framing is synthetic)
    std::string comment = "#" + std::to_string(r.index) + " frame " + std::to_string(r.time.frame) + " t " +
                          std::to_string(r.time.tInFrame) + (r.out ? " out" : " in");
    if (r.kind == TrafficRecord::Kind::Socket)
        comment += " - socket " + std::to_string(r.socket) + " " + r.op + " (synthetic packet)";
    Option(body, 1, comment.data(), comment.size());   // opt_comment
    Option(body, 0, nullptr, 0);
    return Block(6, body);
}

std::string HexOf(const std::vector<uint8_t>& bytes)
{
    static const char* digits = "0123456789ABCDEF";
    std::string hex;
    hex.reserve(bytes.size() * 2);
    for (uint8_t b : bytes)
    {
        hex.push_back(digits[b >> 4]);
        hex.push_back(digits[b & 15]);
    }
    return hex;
}

const char* ProtoName(NetProto p)
{
    switch (p)
    {
        case NetProto::Udp: return "UDP";
        case NetProto::Icmp: return "ICMP";
        case NetProto::Serial: return "serial";
        default: return "TCP";
    }
}
}  // namespace

struct NetworkTrafficTap::Encoder
{
    std::map<std::string, uint32_t> interfaces;   ///< adapter -> pcapng interface id
    SocketPacketizer packetizer;                  ///< socket operations as packets (T2)

    /// The pcapng blocks of one record (an interface description first for a new adapter); nothing for an operation
    /// with no packet
    void Encode(const TrafficRecord& r, std::vector<uint8_t>& out)
    {
        const std::vector<std::vector<uint8_t>> packets =
            r.kind == TrafficRecord::Kind::Frame ? std::vector<std::vector<uint8_t>>{r.bytes} : packetizer.Packets(r);
        if (packets.empty())
            return;
        auto it = interfaces.find(r.adapter);
        if (it == interfaces.end())
        {
            const std::vector<uint8_t> idb = InterfaceDescription(r.adapter);
            out.insert(out.end(), idb.begin(), idb.end());
            it = interfaces.emplace(r.adapter, static_cast<uint32_t>(interfaces.size())).first;
        }
        for (const std::vector<uint8_t>& packet : packets)
        {
            const std::vector<uint8_t> epb = EnhancedPacket(it->second, r, packet);
            out.insert(out.end(), epb.begin(), epb.end());
        }
    }
};

NetworkTrafficTap::NetworkTrafficTap(std::function<TrafficTime()> clock) : _clock(std::move(clock))
{
}

NetworkTrafficTap::~NetworkTrafficTap()
{
    StopFile();
}

void NetworkTrafficTap::Frame(const std::string& adapter, bool out, const uint8_t* frame, size_t length)
{
    std::lock_guard<std::mutex> lock(_mutex);
    TrafficRecord r;
    r.kind = TrafficRecord::Kind::Frame;
    r.out = out;
    r.adapter = adapter;
    r.bytes.assign(frame, frame + length);
    ++_frames;
    Add(std::move(r));
}

void NetworkTrafficTap::Socket(const std::string& adapter, bool out, const char* op, uint16_t socket, NetProto proto,
                               const NetEndpoint& peer, uint16_t localPort, const uint8_t* data, size_t length)
{
    std::lock_guard<std::mutex> lock(_mutex);
    TrafficRecord r;
    r.kind = TrafficRecord::Kind::Socket;
    r.out = out;
    r.adapter = adapter;
    r.op = op;
    r.socket = socket;
    r.proto = proto;
    r.peer = peer;
    r.localPort = localPort;
    if (data && length)
        r.bytes.assign(data, data + length);
    ++_socketOps;
    Add(std::move(r));
}

void NetworkTrafficTap::Add(TrafficRecord r)
{
    r.index = _nextIndex++;
    if (_clock)
        r.time = _clock();
    _bytes += r.bytes.size();
    if (_file)
        WriteToFile(r);
    for (LiveReader& reader : _live)
    {
        if (reader.dropped)
            continue;
        reader.encoder->Encode(r, reader.pending);
        if (reader.pending.size() > kMaxLivePending)
        {
            // Too slow (or gone without a word): drop it rather than grow without end
            reader.dropped = true;
            reader.pending.clear();
            ++_liveDropped;
        }
    }
    _ringBytes += r.bytes.size() + 64;   // the bytes and the record's own fields
    _ring.push_back(std::move(r));
    while (_ringBytes > _ringBudget && _ring.size() > 1)
    {
        _ringBytes -= _ring.front().bytes.size() + 64;
        _ring.pop_front();
        ++_trimmed;
    }
}

std::vector<TrafficRecord> NetworkTrafficTap::Records(const Filter& filter) const
{
    std::lock_guard<std::mutex> lock(_mutex);
    std::vector<TrafficRecord> out;
    for (const TrafficRecord& r : _ring)
    {
        if (r.index < filter.since)
            continue;
        if (!filter.adapter.empty() && r.adapter != filter.adapter)
            continue;
        if (filter.kind >= 0 && static_cast<int>(r.kind) != filter.kind)
            continue;
        out.push_back(r);
    }
    if (filter.last && out.size() > filter.last)
        out.erase(out.begin(), out.end() - static_cast<std::ptrdiff_t>(filter.last));
    return out;
}

void NetworkTrafficTap::Clear()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _ring.clear();
    _ringBytes = 0;
}

void NetworkTrafficTap::SetRingBytes(size_t bytes)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _ringBudget = std::max<size_t>(bytes, 64 * 1024);
    while (_ringBytes > _ringBudget && _ring.size() > 1)
    {
        _ringBytes -= _ring.front().bytes.size() + 64;
        _ring.pop_front();
        ++_trimmed;
    }
}

std::vector<uint8_t> NetworkTrafficTap::Pcapng(const Filter& filter) const
{
    const std::vector<TrafficRecord> records = Records(filter);
    std::vector<uint8_t> out = SectionHeader();
    Encoder encoder;
    for (const TrafficRecord& r : records)
        encoder.Encode(r, out);
    return out;
}

bool NetworkTrafficTap::StartFile(const std::string& path, std::string& error)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _file.reset();
    _file = std::make_unique<std::ofstream>(FileHelper::ToFsPath(path), std::ios::binary | std::ios::trunc);
    if (!*_file)
    {
        _file.reset();
        error = "cannot write '" + path + "'";
        return false;
    }
    const std::vector<uint8_t> shb = SectionHeader();
    _file->write(reinterpret_cast<const char*>(shb.data()), static_cast<std::streamsize>(shb.size()));
    _filePath = path;
    _fileEncoder = std::make_unique<Encoder>();
    _fileRecords = 0;
    return true;
}

void NetworkTrafficTap::StopFile()
{
    std::lock_guard<std::mutex> lock(_mutex);
    _file.reset();
}

void NetworkTrafficTap::WriteToFile(const TrafficRecord& r)
{
    std::vector<uint8_t> blocks;
    _fileEncoder->Encode(r, blocks);
    if (blocks.empty())
        return;
    _file->write(reinterpret_cast<const char*>(blocks.data()), static_cast<std::streamsize>(blocks.size()));
    _file->flush();   // a live reader (tail, Wireshark on the file) sees each packet
    ++_fileRecords;
}

std::string NetworkTrafficTap::Summary(const TrafficRecord& r)
{
    if (r.kind == TrafficRecord::Kind::Frame)
        return EthernetAccess::Summary(r.bytes);
    std::string s = std::string(ProtoName(r.proto)) + " " + r.op;
    if (r.peer.addr || r.peer.port)
        s += (r.out ? " to " : " from ") + NetIpToString(r.peer.addr) + ":" + std::to_string(r.peer.port);
    if (!r.bytes.empty())
    {
        s += ", " + std::to_string(r.bytes.size()) + " bytes";
        // The first printable line of the data (an HTTP request line, an SMTP greeting)
        std::string line;
        for (uint8_t b : r.bytes)
        {
            if (b == '\r' || b == '\n' || line.size() >= 60)
                break;
            line.push_back((b >= 0x20 && b < 0x7F) ? static_cast<char>(b) : '.');
        }
        if (line.size() >= 4)
            s += ": " + line;
    }
    return s;
}

StateNode NetworkTrafficTap::RecordNode(const TrafficRecord& r)
{
    StateNode n = StateNode::Object();
    n["index"] = r.index;
    n["frame"] = r.time.frame;
    n["t_in_frame"] = static_cast<uint64_t>(r.time.tInFrame);
    n["time_us"] = r.time.us;
    n["kind"] = r.kind == TrafficRecord::Kind::Frame ? "frame" : "socket";
    n["direction"] = r.out ? "out" : "in";
    n["adapter"] = r.adapter;
    if (r.kind == TrafficRecord::Kind::Socket)
    {
        n["op"] = r.op;
        n["socket"] = static_cast<uint64_t>(r.socket);
        n["proto"] = ProtoName(r.proto);
        n["peer"] = NetIpToString(r.peer.addr) + ":" + std::to_string(r.peer.port);
        if (r.localPort)
            n["local_port"] = static_cast<uint64_t>(r.localPort);
    }
    n["length"] = static_cast<uint64_t>(r.bytes.size());
    n["summary"] = Summary(r);
    n["hex"] = HexOf(r.bytes);
    return n;
}

StateNode NetworkTrafficTap::Describe() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    StateNode d = StateNode::Object();
    d["records"] = static_cast<uint64_t>(_ring.size());
    d["ring_bytes"] = static_cast<uint64_t>(_ringBytes);
    d["ring_budget"] = static_cast<uint64_t>(_ringBudget);
    d["first_index"] = _ring.empty() ? _nextIndex : _ring.front().index;
    d["next_index"] = _nextIndex;
    d["frames"] = _frames;
    d["socket_ops"] = _socketOps;
    d["bytes"] = _bytes;
    d["trimmed"] = _trimmed;
    StateNode f = StateNode::Object();
    f["recording"] = _file != nullptr;
    f["path"] = _filePath;
    f["records"] = _fileRecords;
    d["file"] = f;
    d["live_readers"] = static_cast<uint64_t>(_live.size());
    d["live_dropped"] = _liveDropped;
    return d;
}

size_t NetworkTrafficTap::RingBytes() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _ringBudget;
}

uint64_t NetworkTrafficTap::NextIndex() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _nextIndex;
}

bool NetworkTrafficTap::FileOpen() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _file != nullptr;
}

std::string NetworkTrafficTap::FilePath() const
{
    std::lock_guard<std::mutex> lock(_mutex);
    return _filePath;
}

uint32_t NetworkTrafficTap::AttachLive()
{
    std::lock_guard<std::mutex> lock(_mutex);
    LiveReader reader;
    reader.id = _nextLiveId++;
    reader.encoder = std::make_unique<Encoder>();
    reader.pending = SectionHeader();
    for (const TrafficRecord& r : _ring)
        reader.encoder->Encode(r, reader.pending);   // what already happened comes first
    const uint32_t id = reader.id;
    _live.push_back(std::move(reader));
    return id;
}

bool NetworkTrafficTap::TakeLive(uint32_t id, std::vector<uint8_t>& out)
{
    std::lock_guard<std::mutex> lock(_mutex);
    for (LiveReader& reader : _live)
    {
        if (reader.id != id)
            continue;
        if (reader.dropped)
            return false;
        out.swap(reader.pending);
        reader.pending.clear();
        return true;
    }
    return false;
}

void NetworkTrafficTap::DetachLive(uint32_t id)
{
    std::lock_guard<std::mutex> lock(_mutex);
    _live.erase(std::remove_if(_live.begin(), _live.end(), [id](const LiveReader& r) { return r.id == id; }), _live.end());
}
