#pragma once

/// @file deviceaccesswatch.h
/// @brief What a program uses, from the machine's own decode (docs/inprogress/2026-11-02-device-access-watch): every
/// IN and OUT after the decoder applied it, attributed to the slot registry's claim that answers it (PortClaimIndex)
/// and to the claim's channel (a half-row, a selected register; PortClaim::channel), counted per frame and folded into
/// a window. For the reads of an input claim, the bits the code tests after the read; for every channel, the code
/// sites that access it. The watch knows no port and no device: the registry's declarations say what an access is.
///
/// Cost: off, the decoder's test of one flag. On, a table load, a switch on the claim's channel rule, a counter, a
/// scan of up to 8 sites. The machine's thread counts and folds; any thread reads the evidence.

// Qt defines `slots` and `signals` as macros; this header reaches Qt translation units through portdecoder.h
#pragma push_macro("slots")
#pragma push_macro("signals")
#undef slots
#undef signals

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "emulator/slots/slottypes.h"

class EmulatorContext;
class PortClaimIndex;

/// The code at @p address as the CPU sees it, without side effects
using AccessCodeReader = uint8_t (*)(const void* context, uint16_t address);

/// The register a device has selected now, by the input role of its select claims (an AY's, NextREG's); -1 when
/// not known. Asked when the watch starts: the selects written while it was off are not seen
using SelectedIndexReader = int (*)(const void* context, slots::InputRole role);

/// The bits of the IN's result the code after it tests: the code at @p next (the instruction after the IN) read
/// through @p read, without running it, up to 8 instructions. The IN is the one ending at @p next (`IN A,(n)` or
/// `IN r,(C)`); an `AND n` of A, a `BIT b` of the register and a right shift of A followed by a carry test count; the
/// scan stops at an unconditional jump, a call, a return, another IN. 0 when nothing tests it there
uint8_t TestedBitsAfterIn(AccessCodeReader read, const void* context, uint16_t next);

/// "key-matrix", "kempston-joystick", "kempston-joystick-2", "fuller-joystick", "kempston-mouse", "amx-mouse",
/// "ay-io", "nextreg"; "" for None
const char* InputRoleName(slots::InputRole role);

/// One code site that accessed a channel
struct DeviceAccessSite
{
    uint16_t pc = 0;       ///< the IN / OUT instruction
    uint32_t count = 0;    ///< accesses from it since the watch started
    uint8_t tested = 0;    ///< reads of an input claim: the bits the code there tests after the read
};

/// What the watch saw on one channel of one device over its window
struct DeviceAccessChannel
{
    static constexpr int kNoIndex = -1;

    std::string device;    ///< the registry id ("ula", "ay", "zxbus.1"); "board" for an access no device claims
    std::string channel;   ///< "row.fe", "reg.0e", "reg.select", "1f"; a board access: its port ("7ffd")
    slots::InputRole input = slots::InputRole::None;
    uint16_t port = 0;     ///< the claim's documented port; a board access: the port itself
    int index = kNoIndex;  ///< Channel::AddressBits: the low line (8-15 for a half-row); Indexed: the register
    bool out = false;
    uint16_t frames = 0;   ///< frames of the window with an access
    float perFrame = 0;    ///< accesses per frame, over the window
    uint8_t tested = 0;    ///< reads of an input claim: the bits the code tested (union over its sites)
    std::vector<DeviceAccessSite> sites;  ///< up to kSites, most used first
    uint32_t otherSites = 0;              ///< accesses from sites past the first kSites
};

struct DeviceAccessEvidence
{
    bool active = false;
    uint32_t framesObserved = 0;  ///< since the watch started
    uint32_t window = 0;          ///< frames in the window (up to the watch's kWindow)
    std::vector<DeviceAccessChannel> channels;  ///< the channels with an access since the start, in first-seen order
    uint32_t droppedChannels = 0;               ///< accesses of channels past the watch's capacity

    /// The channel of an input role (and index, kNoIndex = any) read most often in the window; nullptr for none
    const DeviceAccessChannel* Find(slots::InputRole input, int index = DeviceAccessChannel::kNoIndex,
                                    bool out = false) const;
};

class DeviceAccessWatch
{
public:
    static constexpr uint32_t kWindow = 50;  ///< frames folded into the evidence
    static constexpr unsigned kSites = 8;    ///< code sites kept per channel
    static constexpr unsigned kSlots = 255;  ///< channels per run (more are counted as dropped)

    DeviceAccessWatch();
    ~DeviceAccessWatch();

    /// The claims and the code reader of a machine (control path, before it runs, after the slots are built)
    void Build(const EmulatorContext* context, PortClaimIndex& index);
    /// The same from parts (tests): the code reader may be null (no tested bits), the select reader too (every
    /// device's selection unknown until the program writes one)
    void Build(PortClaimIndex& index, AccessCodeReader read, const void* readContext,
               SelectedIndexReader selected = nullptr, const void* selectedContext = nullptr);

    /// The decoder's flag: the watch is counting (machine thread)
    bool On() const
    {
        return _on;
    }

    /// One access, after the decoder applied it; @p pc is the IN / OUT instruction (machine thread)
    void Record(uint16_t port, uint8_t value, bool out, bool dos, uint16_t pc);

    /// region <Requests (any thread), applied at the next frame boundary>
    /// A window of @p frames counted from zero (a media load: the auto connector's evidence); while @p loading at the
    /// boundary (a tape playing) the count restarts, the program is not in yet
    void RequestWindow(uint32_t frames);
    /// End the window now (the continuous watch, when on, keeps counting)
    void CloseWindow();
    /// The continuous watch (feature `deviceaccess`): counting while on, from zero when it turns on
    void SetContinuous(bool on);
    /// endregion

    /// The frame boundary (machine thread): the frame's counts into the window, the requests applied
    void OnFrameBoundary(bool loading);

    /// Start from zero / stop at once (machine thread; tests)
    void Start();
    void Stop();
    /// The frame's counts into the window (machine thread, at the frame boundary)
    void EndFrame();
    /// Clear the counts and the window and count from zero (machine thread)
    void Reset();

    /// What the watch saw (any thread)
    DeviceAccessEvidence Evidence() const;
    /// Bumped at every start, stop and folded frame (any thread): a reader's cache key (`devices.access`)
    uint64_t Revision() const
    {
        return _revision.load(std::memory_order_acquire);
    }
    /// The continuous watch as last requested (feature `deviceaccess`; any thread)
    bool Continuous() const
    {
        return _continuousRequest.load(std::memory_order_acquire);
    }

private:
    static constexpr uint16_t kValueSelect = 256;  ///< a select access (Channel::Select / SelectIndexed OUT)
    static constexpr uint16_t kValueNone = 257;    ///< AddressBits with no line low
    static constexpr uint16_t kValueMulti = 258;   ///< AddressBits with several lines low
    static constexpr uint16_t kValueUnknown = 259; ///< Indexed before any select the watch knows of
    static constexpr uint16_t kValues = 260;

    struct Rule
    {
        slots::Channel channel = slots::Channel::None;
        uint16_t mask = 0;     ///< AddressBits: the lines; Select / SelectIndexed: the index bits (0xFF when any)
        uint8_t owner = 0;     ///< the device's index for its register select
        bool input = false;
    };
    struct Slot
    {
        uint16_t claim = 0;    ///< PortClaimIndex::kBoard for a board access
        uint16_t value = 0;    ///< the channel value; a board access: the port
        bool out = false;
        std::array<DeviceAccessSite, kSites> sites{};
        uint8_t siteCount = 0;
        uint32_t otherSites = 0;
    };

    uint8_t SlotOf(uint16_t claim, uint16_t value, uint16_t port, bool out);
    void Count(uint8_t slot, uint16_t pc, bool input);

    PortClaimIndex* _index = nullptr;
    AccessCodeReader _read = nullptr;
    const void* _readContext = nullptr;
    SelectedIndexReader _selectedRead = nullptr;
    const void* _selectedContext = nullptr;
    bool _on = false;

    std::vector<Rule> _rules;                    ///< by claim
    std::vector<uint16_t> _selected;             ///< by device: its selected register, or kValueUnknown
    std::vector<slots::InputRole> _selectRole;   ///< by device: the input role of its select claims (the seed)
    std::vector<uint8_t> _claimSlots;            ///< slot + 1 by (claim * 2 + out) * kValues + value; 0 = none yet
    std::vector<uint8_t> _boardSlots;            ///< slot + 1 by out << 16 | port
    std::vector<Slot> _slots;                    ///< machine thread
    std::array<uint16_t, kSlots> _frame{};       ///< the frame being counted
    std::array<uint8_t, kSlots> _frameTested{};
    uint32_t _dropped = 0;

    mutable std::mutex _mutex;  ///< guards what follows
    std::vector<Slot> _published;                ///< _slots as of the last frame end
    uint32_t _publishedDropped = 0;
    std::vector<std::array<uint16_t, kSlots>> _ring;
    std::vector<std::array<uint8_t, kSlots>> _ringTested;
    uint32_t _frames = 0;
    std::atomic<bool> _active{false};
    std::atomic<uint64_t> _revision{0};

    std::atomic<uint32_t> _windowRequest{0};  ///< frames of a requested window; kCloseWindow ends it
    std::atomic<bool> _continuousRequest{false};
    static constexpr uint32_t kCloseWindow = UINT32_MAX;
    uint32_t _windowLeft = 0;                 ///< machine thread: frames the window still runs
    bool _continuous = false;                 ///< machine thread: the continuous watch as applied
};

#pragma pop_macro("signals")
#pragma pop_macro("slots")
