#pragma once

/// @file fakezifiplugin.h
/// @brief The Z80 side of the ZIFI-NATIVE file bridge for tests: an in-memory SD card answering the VFS request
/// frames (40..5E) as the Wild Commander plugins do (ZiFi-ESP32-S3-Zero docs/PROTOCOL.md "VFS: команды ESP -> Z80").
/// Flavors: the S3 ZIFIFTP.WMF v0.15 (read / write windows, FILEX metadata in STAT, dates in READDIR, optional
/// batched READDIR) and the old / ESP-01S plugin (one-byte OPEN answer, 512-byte READ / BLOCK, plain entries).
/// Handle() takes one request frame and returns the answer frames (none for a write-window fragment that is not the
/// last). Faults can be injected (a status to answer, a frame to swallow).

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

class FakeZiFiPlugin
{
public:
    using Bytes = std::vector<uint8_t>;
    struct Frame
    {
        uint8_t cmd = 0;
        Bytes data;
    };

    struct Node
    {
        bool dir = false;
        Bytes data;
        uint16_t date = 0x5944;   // 2024-10-04
        uint16_t time = 0x6000;   // 12:00:00
    };

    bool windows = true;     ///< OPEN announces read / write windows (S3 plugin v0.15)
    bool filex = true;       ///< OPEN third byte (FILEX), STAT metadata
    bool batch = false;      ///< OPENDIR announces batched READDIR
    bool dates = true;       ///< READDIR entries carry [0][date][time]
    uint8_t failCmd = 0;     ///< answer this command with failStatus
    uint8_t failStatus = 0;
    uint8_t swallowCmd = 0;  ///< never answer this command (a lost frame)
    std::map<std::string, Node> files;   ///< "/A/B.TXT" -> node; "/" exists implicitly
    std::vector<uint8_t> commands;       ///< every request seen

    FakeZiFiPlugin() { files["/"] = Node{true, {}, 0, 0}; }

    void Put(const std::string& path, const Bytes& data) { files[path] = Node{false, data, 0x5944, 0x6000}; }
    void Mkdir(const std::string& path) { files[path] = Node{true, {}, 0x5944, 0x6000}; }
    const Bytes* Get(const std::string& path) const
    {
        auto it = files.find(path);
        return it == files.end() || it->second.dir ? nullptr : &it->second.data;
    }

    static uint16_t Crc16(const uint8_t* d, size_t n, uint16_t crc = 0xFFFF)
    {
        for (size_t i = 0; i < n; ++i)
        {
            crc = static_cast<uint16_t>(crc ^ (static_cast<uint16_t>(d[i]) << 8));
            for (int b = 0; b < 8; ++b)
                crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
        }
        return crc;
    }

    std::vector<Frame> Handle(uint8_t cmd, const Bytes& p)
    {
        commands.push_back(cmd);
        if (cmd == swallowCmd)
            return {};
        if (cmd == failCmd && failStatus)
        {
            if (cmd == 0x57)   // a window is answered once, after its last fragment
                return (p[0] & 2) ? std::vector<Frame>{{cmd, {failStatus, p[1], 0, 0}}} : std::vector<Frame>{};
            return {{cmd, {failStatus}}};
        }
        switch (cmd)
        {
            case 0x40: return {Stat(Str(p, 0))};
            case 0x41:
            {
                const std::string path = Str(p, 0);
                auto it = files.find(path);
                if (it == files.end() || !it->second.dir)
                    return {{cmd, {1}}};
                _dirList.clear();
                const std::string prefix = path == "/" ? "/" : path + "/";
                for (auto& [name, node] : files)
                {
                    if (name.size() > prefix.size() && name.compare(0, prefix.size(), prefix) == 0 &&
                        name.find('/', prefix.size()) == std::string::npos)
                        _dirList.push_back(name);
                }
                _dirNext = 0;
                if (batch)
                    return {{cmd, {0, 1}}};
                return {{cmd, {0}}};
            }
            case 0x42:
            {
                if (p.empty())
                {
                    if (_dirNext >= _dirList.size())
                        return {{cmd, {1}}};
                    return {Entry(_dirList[_dirNext++])};
                }
                std::vector<Frame> out;
                for (uint8_t n = 0; n < p[0] && _dirNext < _dirList.size(); ++n)
                    out.push_back(Entry(_dirList[_dirNext++]));
                out.push_back({cmd, {static_cast<uint8_t>(_dirNext < _dirList.size() ? 2 : 1)}});
                return out;
            }
            case 0x50:
            {
                const uint8_t mode = p.empty() ? 0 : p[0];
                const std::string path = Str(p, 1);
                auto it = files.find(path);
                if (mode == 0 && (it == files.end() || it->second.dir))
                    return {{cmd, {4}}};
                if (mode == 1)
                    files[path] = Node{false, {}, 0x5944, 0x6000};
                if ((mode == 2 || mode == 3) && it == files.end())
                    return {{cmd, {4}}};
                _open = path;
                _mode = mode;
                _pos = 0;
                _pending.clear();
                if (!windows)
                    return {{cmd, {0}}};
                return {{cmd, {0, 0x07, static_cast<uint8_t>(filex ? 0x3F : 0)}}};
            }
            case 0x51:
            {
                const size_t wanted = static_cast<size_t>(p[0] | (p[1] << 8));
                Bytes& d = files[_open].data;
                Frame f{cmd, {0}};
                const size_t n = std::min(wanted, d.size() - std::min(d.size(), _pos));
                f.data.insert(f.data.end(), d.begin() + static_cast<std::ptrdiff_t>(_pos),
                              d.begin() + static_cast<std::ptrdiff_t>(_pos + n));
                _pos += n;
                return {f};
            }
            case 0x58:
            {
                const uint8_t seq = p[0];
                const size_t wanted = static_cast<size_t>(p[1] | (p[2] << 8));
                Bytes& d = files[_open].data;
                const size_t n = std::min(wanted, d.size() - std::min(d.size(), _pos));
                if (n == 0)
                    return {{cmd, {1}}};
                const uint16_t crc = Crc16(d.data() + _pos, n);
                std::vector<Frame> out;
                for (size_t off = 0; off < n;)
                {
                    const size_t part = std::min<size_t>(n - off, 1015);
                    Frame f{cmd, {0, static_cast<uint8_t>((off == 0 ? 1 : 0) | (off + part == n ? 2 : 0)), seq}};
                    Le16(f.data, static_cast<uint16_t>(off));
                    Le16(f.data, static_cast<uint16_t>(n));
                    Le16(f.data, off + part == n ? crc : 0);
                    f.data.insert(f.data.end(), d.begin() + static_cast<std::ptrdiff_t>(_pos + off),
                                  d.begin() + static_cast<std::ptrdiff_t>(_pos + off + part));
                    out.push_back(std::move(f));
                    off += part;
                }
                _pos += n;
                return out;
            }
            case 0x56:
            {
                const uint8_t seq = p[1];
                if (p[0] == 0x00)
                {
                    _blockRaw = static_cast<size_t>(p[2] | (p[3] << 8));
                    _blockCrc = static_cast<uint16_t>(p[6] | (p[7] << 8));
                    _block.assign(p.begin() + 8, p.end());
                }
                else
                    _block.insert(_block.end(), p.begin() + 4, p.end());
                if (_block.size() == _blockRaw)
                {
                    if (Crc16(_block.data(), _block.size()) != _blockCrc)
                        return {{cmd, {0x30, seq, 0, 0}}};
                    Append(_block);
                }
                Frame f{cmd, {0, seq}};
                Le16(f.data, static_cast<uint16_t>(_block.size()));
                return {f};
            }
            case 0x57:
            {
                const uint8_t flags = p[0], seq = p[1];
                if (flags & 1)
                    _window.clear();
                _window.insert(_window.end(), p.begin() + 8, p.end());
                if (!(flags & 2))
                    return {};
                const uint16_t total = static_cast<uint16_t>(p[4] | (p[5] << 8));
                const uint16_t crc = static_cast<uint16_t>(p[6] | (p[7] << 8));
                if (_window.size() != total || Crc16(_window.data(), _window.size()) != crc)
                    return {{cmd, {0x31, seq, 0, 0}}};
                Append(_window);
                Frame f{cmd, {0, seq}};
                Le16(f.data, total);
                return {f};
            }
            case 0x53: _open.clear(); return {{cmd, {0}}};
            case 0x54:
            {
                auto it = files.find(Str(p, 0));
                if (it == files.end())
                    return {{cmd, {4}}};
                files.erase(it);
                return {{cmd, {0}}};
            }
            case 0x55:
            {
                const std::string path = Str(p, 0);
                if (files.count(path))
                    return {{cmd, {8}}};
                Mkdir(path);
                return {{cmd, {0}}};
            }
            case 0x5B:
                _pos = static_cast<size_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24));
                return {{cmd, {0}}};
            case 0x5E:
            {
                Node& n = files[_open];
                if (p[3] & 0x04)
                {
                    n.time = static_cast<uint16_t>(p[11] | (p[12] << 8));
                    n.date = static_cast<uint16_t>(p[13] | (p[14] << 8));
                }
                return {{cmd, {0, 0x20}}};
            }
            default: return {{cmd, {0x7F}}};
        }
    }

private:
    static std::string Str(const Bytes& p, size_t at)
    {
        std::string s;
        for (size_t i = at; i < p.size() && p[i]; ++i)
            s.push_back(static_cast<char>(p[i]));
        return s;
    }
    static void Le16(Bytes& v, uint16_t x)
    {
        v.push_back(static_cast<uint8_t>(x));
        v.push_back(static_cast<uint8_t>(x >> 8));
    }
    static void Le32(Bytes& v, uint32_t x)
    {
        Le16(v, static_cast<uint16_t>(x));
        Le16(v, static_cast<uint16_t>(x >> 16));
    }

    Frame Stat(const std::string& path)
    {
        auto it = files.find(path);
        if (it == files.end())
            return {0x40, {4}};
        Frame f{0x40, {0, static_cast<uint8_t>(it->second.dir ? 1 : 0)}};
        Le32(f.data, static_cast<uint32_t>(it->second.data.size()));
        if (filex && path != "/")
        {
            f.data.insert(f.data.end(), {16, 0, static_cast<uint8_t>(it->second.dir ? 0x10 : 0x20), 0, 0});
            Le16(f.data, it->second.time);
            Le16(f.data, it->second.date);
            Le16(f.data, it->second.date);
            Le16(f.data, it->second.time);
            Le16(f.data, it->second.date);
            f.data.push_back(0);
        }
        return f;
    }

    Frame Entry(const std::string& path)
    {
        const Node& n = files[path];
        Frame f{0x42, {0, static_cast<uint8_t>(n.dir ? 1 : 0)}};
        Le32(f.data, static_cast<uint32_t>(n.data.size()));
        const std::string name = path.substr(path.rfind('/') + 1);
        f.data.insert(f.data.end(), name.begin(), name.end());
        if (dates)
        {
            f.data.push_back(0);
            Le16(f.data, n.date);
            Le16(f.data, n.time);
        }
        return f;
    }

    void Append(const Bytes& b)
    {
        Bytes& d = files[_open].data;
        if (_mode == 3)
        {
            if (d.size() < _pos + b.size())
                d.resize(_pos + b.size());
            std::copy(b.begin(), b.end(), d.begin() + static_cast<std::ptrdiff_t>(_pos));
            _pos += b.size();
        }
        else
            d.insert(d.end(), b.begin(), b.end());
    }

    std::vector<std::string> _dirList;
    size_t _dirNext = 0;
    std::string _open;
    uint8_t _mode = 0;
    size_t _pos = 0;
    Bytes _pending;
    Bytes _block;
    size_t _blockRaw = 0;
    uint16_t _blockCrc = 0;
    Bytes _window;
};
