#pragma once

/// @file gscommandlog.h
/// @brief The General Sound command log (gs-debugger design §6.3, F1, F4): the host's commands, their parameters, the
/// replies and the uploads, decoded by the firmware profile (gsfirmwareprofile.h), with the protocol's anomalies.
///
/// The log is a device history (debugger/history/devicehistory.h) named `<slot>.commands` (`zxbus.1.commands`), so
/// every plane reads, configures (off / ring / capture), clears and streams it (a JSON Lines file: the export)
/// through the history verbs. SoundManager owns the machine's one and every personality it fits (LLE, LW, NeoGS)
/// feeds it, so the log and its decoder survive a personality switch; a MultiSound card owns its card's.
///
/// Off it costs a null test and one relaxed load in the card's host port handlers, which already call flush. The
/// card side is not hooked: that the card took a command is seen at the next host access (its status bit 0 clear).
///
/// Threads: the card feeds it on the emulation thread; the planes read the history (its own lock).

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "debugger/history/devicehistory.h"

class EmulatorContext;
class GeneralSoundCard;
struct GSFirmwareProfile;

enum class GSCommandLogKind : uint8_t
{
    Command,   ///< the host wrote a command (#BB): `value` its pre byte (flag kHasPre)
    Param,     ///< a data byte the command takes after it (#B3): `index` the parameter
    Data,      ///< a data byte no command expects (yet): the next command's pre byte, or a stray one
    Reply,     ///< the host read a reply byte (IN #B3): `index` the reply byte
    Block,     ///< an upload / download / stream ended: `count` bytes, the destination when known
    CardTook,  ///< the card took the command (seen at the next host access)
    Anomaly,   ///< a protocol fault: `value` the GSCommandLogAnomaly, `count` its detail
    Marker,    ///< the card was reset (#33 bit 7): the decoder starts over
};

enum class GSCommandLogAnomaly : uint8_t
{
    DataOverwritten,     ///< the host wrote data while the card had not taken the previous byte: `count` the lost byte
    CommandOverwritten,  ///< the host wrote a command while the card had not taken the previous one: `count` it
    ReplyNotReady,       ///< the host read a reply the card had not written yet: `count` the stale byte
    TransferStalled,     ///< an upload made no progress for kStallFrames frames: `count` the bytes so far
};

namespace GSCommandLogFlags
{
constexpr uint8_t kHasPre = 1u << 0;     ///< Command: `value` is the pre byte (the data written before it)
constexpr uint8_t kStream = 1u << 1;     ///< Command: written during an upload (a stream command, #D1 / #D2 / ...)
constexpr uint8_t kNotReady = 1u << 2;   ///< Reply: the status said no reply was there (a stale byte)
constexpr uint8_t kUnasked = 1u << 3;    ///< Reply: no reply byte was due
constexpr uint8_t kTruncated = 1u << 4;  ///< Block: ended before its length (a command, a reset)
constexpr uint8_t kAutomation = 1u << 5; ///< written by a tool (sendCommand / sendData / readData), not the host CPU
}  // namespace GSCommandLogFlags

enum class GSCommandLogDest : uint8_t
{
    Unknown,
    Ram,      ///< `destAddr` in the card CPU's window (#14 / #15)
};

/// One entry of the command log. 32 bytes
struct GSCommandLogEntry
{
    int64_t cardCycle = 0;    ///< the card's clock (LLE, NeoGS: card cycles; LW: 0)
    uint32_t frame = 0;       ///< the machine's frame
    uint32_t mainT = 0;       ///< the main CPU's T-state in the frame
    uint32_t count = 0;       ///< Block: bytes; Anomaly: its detail
    uint16_t mainPc = 0;      ///< the main CPU instruction that did the access
    uint16_t destAddr = 0;    ///< Block: the start address (dest Ram)
    GSCommandLogKind kind = GSCommandLogKind::Command;
    uint8_t command = 0;      ///< the command the entry belongs to
    uint8_t value = 0;        ///< the byte (pre, param, data, reply); the anomaly code
    uint8_t index = 0;        ///< Param / Reply: which one (the pre byte is parameter 0)
    uint8_t flags = 0;        ///< GSCommandLogFlags
    uint8_t profile = 0xFF;   ///< GSFirmwareProfiles() index of the decoding profile; #FF none (raw)
    GSCommandLogDest dest = GSCommandLogDest::Unknown;
    uint8_t reserved = 0;

    /// The fields, the names the profile gives (command, parameter, reply) and a one-line `text`
    void ToState(StateNode& node) const;
    std::string Text() const;
};
static_assert(sizeof(GSCommandLogEntry) == 32);

using GSCommandHistory = history::DeviceHistory<GSCommandLogEntry>;

/// What the host's conversation with the card is at (gs-debugger design §6.4, F3): the decoder's view, so valid only
/// while the log records (`on`), whatever the card (a ROM without a profile: commands by number)
struct GSProtocolState
{
    bool on = false;
    const GSFirmwareProfile* profile = nullptr;
    int command = -1;              ///< the last command written, -1 none
    bool taken = true;             ///< the card took it (status bit 0 seen clear)
    uint8_t paramsExpected = 0;    ///< its pre byte and the bytes after it
    uint8_t paramsReceived = 0;
    uint8_t repliesExpected = 0;
    uint8_t repliesRead = 0;
    bool inTransfer = false;
    uint8_t transferCommand = 0;
    uint8_t transfer = 0;          ///< GSTransferKind
    uint32_t transferDone = 0;     ///< bytes so far
    uint32_t transferTotal = 0;    ///< 0: until a command ends it
    bool hasAddress = false;
    uint16_t transferAddress = 0;

    /// One line: `#2F Set current track: parameter 2 of 2 due`, `module upload: 12,288 bytes so far (#30 Load
    /// module)`, `idle (last #2C Set current module)`
    std::string Text() const;
    void ToState(StateNode& node) const;
};

const char* GSCommandLogKindName(GSCommandLogKind kind);
const char* GSCommandLogAnomalyName(GSCommandLogAnomaly anomaly);

/// The decoder's state (§6.3): what the current command still takes and gives, the transfer it started, the mailbox
/// as the host's accesses left it. A plain value, so time travel can put it back (GSCommandLog's marks)
struct GSCommandLogDecoder
{
    const GSFirmwareProfile* _profile = nullptr;
    uint8_t _profileIndex = 0xFF;

    int _command = -1;           ///< the current command, -1 none
    uint8_t _postLeft = 0;       ///< data bytes it takes after the command
    uint8_t _paramIndex = 0;     ///< the next parameter's index
    uint8_t _repliesLeft = 0;    ///< reply bytes due
    uint8_t _replyIndex = 0;
    uint8_t _params[4] = {};
    bool _hasPre = false;        ///< a Data byte since the last command: the next command's pre byte
    uint8_t _pre = 0;

    // The transfer the current command started
    bool _inTransfer = false;
    uint8_t _transfer = 0;       ///< GSTransferKind
    uint8_t _transferCommand = 0;
    uint32_t _blockCount = 0;
    uint32_t _blockExpected = 0; ///< 0: until a command ends it
    uint16_t _blockAddr = 0;
    GSCommandLogDest _blockDest = GSCommandLogDest::Unknown;
    uint64_t _progressFrame = 0;
    bool _stallReported = false;

    // The mailbox as the host's accesses left it
    bool _commandPending = false;  ///< a command written, its taking not seen yet
    uint8_t _pendingCommand = 0;
    bool _lastDataFromHost = false; ///< the data flag's last raise was the host's write (not taken yet)
    uint8_t _lastData = 0;
};

/// The log follows time travel (history::ITimeFollower, gs-debugger design §6.3 D3): it shows the entries up to where
/// the machine stands. A move back holds the later entries back, a move forward on the same line brings them back, a
/// live run from the past (a resume forks a branch) drops them and logs anew, a branch switch keeps the shared past.
/// The decoder follows the machine: a checkpoint restore puts back its state at that frame's start (a mark the log
/// takes at each frame start after traffic), and a TTD replay runs the decoder without logging
class GSCommandLog : public history::ITimeFollower, private GSCommandLogDecoder
{
public:
    static constexpr size_t kDefaultCapacity = 65536;  ///< 2 MB: twice 5 minutes at 2 commands a frame (F1c)
    static constexpr uint32_t kStallFrames = 50;       ///< an upload with no byte for this long is stalled

    /// `device` names the history (the registry id of the card + ".commands"), listed in `registry` while the log
    /// lives (null: unlisted)
    template <typename Name>
    GSCommandLog(const EmulatorContext* context, history::Registry* registry, Name&& device)
        : _context(context), _registry(registry),
          _history(std::forward<Name>(device), "GS host commands, decoded by the firmware profile", kDefaultCapacity)
    {
        // Switched on or off (a configuration, a pause): the decoder starts over and the firmware is identified again
        // (a ROM loaded meanwhile) at the next entry
        _history.OnSwitch([this](bool) {
            _armed.store(false, std::memory_order_relaxed);
            _switched.store(true, std::memory_order_relaxed);
        });
        if (_registry)
        {
            _registry->Add(&_history);
            _registry->AddFollower(this);
        }
    }
    ~GSCommandLog() override
    {
        if (_registry)
        {
            _registry->RemoveFollower(this);
            _registry->Remove(&_history);
        }
    }
    GSCommandLog(const GSCommandLog&) = delete;
    GSCommandLog& operator=(const GSCommandLog&) = delete;

    GSCommandHistory& History() { return _history; }
    const GSCommandHistory& History() const { return _history; }

    /// The card that feeds the log (a personality switch binds the new one; nullptr: none). The decoder starts over
    /// and the profile is identified again at the next entry. The emulation thread parked
    void Bind(const GeneralSoundCard* card)
    {
        _card = card;
        _armed.store(false, std::memory_order_relaxed);
        _dirty = true;
    }

    /// True while the log records: the card's test before it calls the hooks below
    bool On() const { return _history.On(); }

    /// The hooks, called by the card before it changes its mailbox (the status they read is the one the access met).
    /// `automation`: a tool's access (the card's sendCommand / sendData / readData)
    void HostCommand(uint8_t command, bool automation = false);
    void HostData(uint8_t value, bool automation = false);
    void HostReplyRead(uint8_t value, bool automation = false);
    void HostReset();
    /// Once a frame (the card's frame start): an upload that stopped is reported once
    void Frame();

    /// The profile the decoder uses now (nullptr: raw); identified when the log starts and at a new card
    const GSFirmwareProfile* Profile() const { return _profile; }
    /// The conversation's state (F3), as the last host access left it; any thread
    GSProtocolState State() const;

    /// region <history::ITimeFollower (time travel; the emulation thread parked)>
    void Restored() override;
    void Landed(const history::TimeMoment* lineSince) override;
    void SaveLive(std::vector<uint8_t>& out) const override;
    void LoadLive(const std::vector<uint8_t>& in) override;
    /// endregion </history::ITimeFollower>

    /// Frame-start decoder states kept for time travel (the frames after traffic)
    size_t Marks() const { return _marks.size(); }
    static constexpr size_t kMaxMarks = 65536;

private:
    void Arm();
    GSCommandLogEntry Make(GSCommandLogKind kind, uint8_t value, bool automation) const;
    void Push(const GSCommandLogEntry& entry)
    {
        if (!Replaying())
            _history.Push(entry);
    }
    void NoteTaken(uint8_t status);
    void BeginTransfer();
    void EndTransfer(bool truncated, bool automation);
    void ResetDecoder();
    /// In a TTD replay the decoder runs and nothing is logged
    bool Replaying() const;
    /// A live hook's first work: marks of a log switched meanwhile go; the first after a time travel move settles it
    void Live();
    /// The machine runs live from where a move left it: the entries and marks after that moment are another line's
    void Settle();
    /// The machine's moment (frame, the main CPU's T in it) and an entry's being after it
    struct Moment
    {
        uint64_t frame = 0;
        uint32_t t = 0;
    };
    Moment Now() const;
    static bool After(const GSCommandLogEntry& e, const Moment& m)
    {
        return e.frame > m.frame || (e.frame == m.frame && e.mainT > m.t);
    }
    /// At a frame start (Frame, live): the decoder's state there when traffic changed it since the last mark
    void TakeMark();
    void Publish();
    /// Publishes the decoder's state when a hook returns
    struct PublishOnExit
    {
        GSCommandLog& log;
        ~PublishOnExit() { log.Publish(); }
    };

    const EmulatorContext* _context;
    history::Registry* const _registry;
    GSCommandHistory _history;
    const GeneralSoundCard* _card = nullptr;
    const GSFirmwareProfile* _profile = nullptr;
    uint8_t _profileIndex = 0xFF;
    std::atomic<bool> _armed{false};  ///< the decoder runs (cleared by a switch of the history, on any thread)

    // Time travel (the emulation thread, or the control thread with it parked)
    struct Mark
    {
        uint64_t frame = 0;
        bool armed = false;
        GSCommandLogDecoder decoder;
    };
    std::vector<Mark> _marks;            ///< the decoder at frame starts, oldest first
    bool _dirty = false;                 ///< a hook ran since the last mark
    bool _settle = false;                ///< a time travel move happened: the next live hook settles it
    std::atomic<bool> _switched{false};  ///< the history was switched (any thread): the marks are of another run

    mutable std::mutex _stateMutex;  ///< the published state: written by the hooks while on, read by the planes
    GSProtocolState _state;
};
