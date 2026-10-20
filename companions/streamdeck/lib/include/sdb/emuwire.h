// Virtual HID over TCP: how libsdb talks to Stream Deck Studio instead of a USB device.
// Every message is a frame:  [u8 type][u16 length, little-endian][payload]
// Payloads are the exact HID report buffers (report ID first) a real device would get or give.
#pragma once
#include <cstdint>

namespace sdb::emuwire
{

inline constexpr std::uint16_t kDefaultPort = 37050;
inline constexpr std::uint8_t kVersion = 1;

enum Type : std::uint8_t
{
    // host -> emulator
    Output = 0x01,      // hid_write: output report (images)
    SetFeature = 0x02,  // hid_send_feature_report
    GetFeature = 0x03,  // hid_get_feature_report: payload = request buffer (ID first, full requested length)
    SetModel = 0x11,    // u16 PID: switch the emulator to this model (mirroring a real device); answered by Hello
    HostInput = 0x06,   // an input report the host read from the REAL device (mirror): for the emulator's log only
    HostNote = 0x07,    // UTF-8 line for the emulator's log: what the host did with the input (action, navigation)
    HostPage = 0x08,    // UTF-8 JSON: what the keys mean now (scn::describePage), so agents need no screenshots
    HostMirror = 0x09,  // UTF-8 serial of the real deck this host mirrors: what it draws here is on that deck too
    Snapshot = 0x12,    // UTF-8 path: save a PNG of what the emulator shows; answered by SnapshotDone
    Record = 0x14,      // u8 fps (0 = default), UTF-8 path: record the deck to a video (.mp4 .mov .mkv .webm .gif);
                        // an empty path stops. Answered by RecordDone
    // emulator -> host
    FeatureReply = 0x04,  // reply to GetFeature: the filled buffer
    Input = 0x05,         // input report, as hid_read would return it
    Hello = 0x10,         // on connect and after SetModel: u8 version, u16 VID, u16 PID, u8 n, char serial[n]
    SnapshotDone = 0x13,  // u8 1 = saved, 0 = failed
    RecordDone = 0x15,    // u8 1 = ok, 0 = failed; UTF-8 text: the error, or what was recorded (after a stop)
};

}  // namespace sdb::emuwire
