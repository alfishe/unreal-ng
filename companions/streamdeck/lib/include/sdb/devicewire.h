// The device wire v2: the daemon listens, software decks (Stream Deck Studio windows) connect as viewers
// (docs/inprogress/2026-10-19-streamdeck-integration/device-wire.md).
// Every message is a frame:  [u8 type][u32 length, little-endian][payload]
// HID payloads are the exact report buffers (report ID first) a real device would get or give; Join, Welcome,
// DeckState and Control carry JSON.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace sdb::wire
{

inline constexpr std::uint16_t kDefaultPort = 37050;
inline constexpr int kVersion = 2;
inline constexpr std::size_t kMaxFrame = 16u << 20;  // a bigger length is a broken peer

enum Type : std::uint8_t
{
    // daemon -> viewer
    Output = 0x01,       // output report (images)
    SetFeature = 0x02,   // feature report
    GetFeature = 0x03,   // own role only: payload = request buffer (ID first, full requested length)
    HostInput = 0x06,    // mirror role: an input report the daemon read from the real deck (lights presses only)
    HostNote = 0x07,     // UTF-8 log line: what the runtime did with the input
    HostPage = 0x08,     // UTF-8 JSON: what the keys mean now (scn::DescribePage)
    Welcome = 0x21,      // JSON: {"v":2,"deck":serial,"model":id,"pid":n,"real":bool,"daemon":version} or {"error":..}
    DeckState = 0x23,    // JSON: {"connected":bool,"busy_by":..,"target":..}
    ReplayBegin = 0x24,  // the current picture follows (frames up to ReplayEnd)
    ReplayEnd = 0x25,
    Control = 0x26,  // JSON request from the daemon's automation (e.g. {"op":"snapshot","path":..})
    // viewer -> daemon
    FeatureReply = 0x04,  // own role only: the answer to GetFeature
    Input = 0x05,         // an input report, as hid_read returns it
    Join = 0x20,          // JSON: {"v":2,"role":"mirror","deck":serial|"first"} | {"v":2,"role":"own","model":..}
    Leave = 0x22,         // empty: clean disconnect
};

std::string EncodeFrame(std::uint8_t type, std::string_view payload);

struct Frame
{
    std::uint8_t type = 0;
    std::string payload;
};

// Splits a byte stream into frames. Next(): true and a frame, false when more bytes are needed; Broken() after a
// length over kMaxFrame (the connection is then closed).
class FrameReader
{
public:
    void Feed(std::string_view bytes)
    {
        _buffer.append(bytes);
    }
    bool Next(Frame& out);
    [[nodiscard]] bool Broken() const
    {
        return _broken;
    }
    [[nodiscard]] std::size_t Pending() const
    {
        return _buffer.size() - _at;
    }

private:
    std::string _buffer;
    std::size_t _at = 0;
    bool _broken = false;
};

}  // namespace sdb::wire
