#include "emulator/sound/chips/gs/gscommandlog.h"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <string>
#include <vector>

#include "3rdparty/digestpp/digestpp.hpp"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/sound/chips/gs/firmware/gsfirmwareprofile.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"

namespace
{

const GSFirmwareProfile* ProfileAt(uint8_t index)
{
    const auto& profiles = GSFirmwareProfiles();
    return index < profiles.size() ? profiles[index] : nullptr;
}

const GSCommandInfo* CommandOf(const GSCommandLogEntry& e)
{
    const GSFirmwareProfile* profile = ProfileAt(e.profile);
    return profile ? &profile->Command(e.command) : nullptr;
}

const char* StreamCommandName(const GSFirmwareProfile* profile, uint8_t code)
{
    if (!profile)
        return nullptr;
    for (size_t i = 0; i < profile->streamCommandCount; i++)
        if (profile->streamCommands[i].code == code)
            return profile->streamCommands[i].name;
    if (code == 0xF3 || code == 0xF4)
        return profile->Command(code).name;
    return "Acknowledged during the upload";
}

/// The name the profile gives the entry's command; nullptr when it has none
const char* CommandName(const GSCommandLogEntry& e)
{
    if ((e.kind == GSCommandLogKind::Command || e.kind == GSCommandLogKind::CardTook) &&
        (e.flags & GSCommandLogFlags::kSelection))
        return nullptr;
    if (e.kind == GSCommandLogKind::Command && (e.flags & GSCommandLogFlags::kStream))
        return StreamCommandName(ProfileAt(e.profile), e.command);
    const GSCommandInfo* info = CommandOf(e);
    return info ? info->name : nullptr;
}

/// The name of the profile's mode that takes the entry's command as a selection
std::string SelectingMode(const GSCommandLogEntry& e)
{
    if (const GSFirmwareProfile* p = ProfileAt(e.profile))
        for (const GSProfileMode& mode : p->modes)
            for (const uint8_t code : mode.selects)
                if (code == e.command)
                    return mode.name;
    return "mode";
}

/// `direction`: a sector transfer's (1 read, 2 write)
const char* TransferName(GSTransferKind kind, uint8_t direction = 0)
{
    switch (kind)
    {
        case GSTransferKind::ModuleUpload: return "module upload";
        case GSTransferKind::SampleUpload: return "sample upload";
        case GSTransferKind::BlockUpload: return "block upload";
        case GSTransferKind::BlockDownload: return "block download";
        case GSTransferKind::Stream: return "stream";
        case GSTransferKind::FixedBlock: return "block";
        case GSTransferKind::PagedBlock: return "page block";
        case GSTransferKind::Sectors: return direction == 1 ? "sector read" : direction == 2 ? "sector write" : "sectors";
        default: return "transfer";
    }
}

/// 12345 -> "12,345"
std::string Grouped(uint32_t value)
{
    std::string digits = std::to_string(value);
    for (int i = static_cast<int>(digits.size()) - 3; i > 0; i -= 3)
        digits.insert(static_cast<size_t>(i), ",");
    return digits;
}

std::string CommandText(const GSFirmwareProfile* profile, uint8_t command)
{
    char hex[4];
    std::snprintf(hex, sizeof(hex), "%02X", command);
    const char* name = profile ? profile->Command(command).name : nullptr;
    return std::string("#") + hex + (name ? std::string(" ") + name : std::string());
}

} // namespace

const char* GSCommandLogKindName(GSCommandLogKind kind)
{
    switch (kind)
    {
        case GSCommandLogKind::Command: return "command";
        case GSCommandLogKind::Param: return "param";
        case GSCommandLogKind::Data: return "data";
        case GSCommandLogKind::Reply: return "reply";
        case GSCommandLogKind::Block: return "block";
        case GSCommandLogKind::CardTook: return "card_took";
        case GSCommandLogKind::Anomaly: return "anomaly";
        case GSCommandLogKind::Marker: return "reset";
    }
    return "?";
}

const char* GSCommandLogAnomalyName(GSCommandLogAnomaly anomaly)
{
    switch (anomaly)
    {
        case GSCommandLogAnomaly::DataOverwritten: return "data_overwritten";
        case GSCommandLogAnomaly::CommandOverwritten: return "command_overwritten";
        case GSCommandLogAnomaly::ReplyNotReady: return "reply_not_ready";
        case GSCommandLogAnomaly::TransferStalled: return "transfer_stalled";
        case GSCommandLogAnomaly::ReplyReadWithoutPoll: return "reply_read_without_poll";
        case GSCommandLogAnomaly::DataBeforeCommand: return "data_before_command";
        case GSCommandLogAnomaly::CommandWhileBusy: return "command_while_busy";
        case GSCommandLogAnomaly::CardReplyStall: return "card_reply_stall";
        case GSCommandLogAnomaly::StaleReplyDrain: return "stale_reply_drain";
        case GSCommandLogAnomaly::UploadOverRunningCode: return "upload_over_running_code";
        case GSCommandLogAnomaly::CommandRejectedInState: return "command_rejected_in_state";
    }
    return "?";
}

/// region <Entry>

std::string GSCommandLogEntry::Text() const
{
    char buffer[160];
    const char* name = CommandName(*this);
    const GSCommandInfo* info = CommandOf(*this);
    const std::string command = std::string("#") + [&] {
        char hex[4];
        std::snprintf(hex, sizeof(hex), "%02X", this->command);
        return std::string(hex);
    }() + (name ? std::string(" ") + name : std::string());
    switch (kind)
    {
        case GSCommandLogKind::Command:
            if (flags & GSCommandLogFlags::kSelection)
                return command + " (" + SelectingMode(*this) + " selection)";
            if (flags & GSCommandLogFlags::kHasPre)
            {
                const char* param = info && info->params[0].name ? info->params[0].name : "pre";
                std::snprintf(buffer, sizeof(buffer), "%s, %s #%02X", command.c_str(), param, value);
                return buffer;
            }
            return command;
        case GSCommandLogKind::Param:
        {
            const char* param = info && index < GSCommandInfo::kMaxParams && info->params[index].name ? info->params[index].name
                                                                                         : "param";
            std::snprintf(buffer, sizeof(buffer), "%s #%02X (%s)", param, value, command.c_str());
            return buffer;
        }
        case GSCommandLogKind::Data:
            std::snprintf(buffer, sizeof(buffer), "data #%02X", value);
            return buffer;
        case GSCommandLogKind::Reply:
            std::snprintf(buffer, sizeof(buffer), "reply %u #%02X%s%s (%s)", unsigned(index), value,
                          info && info->replyMeaning && !(flags & GSCommandLogFlags::kUnasked)
                              ? (std::string(": ") + info->replyMeaning).c_str() : "",
                          (flags & GSCommandLogFlags::kNotReady) ? ", not ready" : "", command.c_str());
            return buffer;
        case GSCommandLogKind::Block:
        {
            if (hostData)
            {
                std::snprintf(buffer, sizeof(buffer), "stream: %u bytes of host data%s", unsigned(count),
                              (flags & GSCommandLogFlags::kTruncated) ? ", cut short" : "");
                return buffer;
            }
            const GSTransferKind transfer = info ? info->transfer : GSTransferKind::None;
            if (dest == GSCommandLogDest::Ram)
                std::snprintf(buffer, sizeof(buffer), "%s: %u bytes at #%04X%s (%s)", TransferName(transfer, value),
                              unsigned(count), destAddr, (flags & GSCommandLogFlags::kTruncated) ? ", cut short" : "",
                              command.c_str());
            else
                std::snprintf(buffer, sizeof(buffer), "%s: %u bytes%s (%s)", TransferName(transfer, value), unsigned(count),
                              (flags & GSCommandLogFlags::kTruncated) ? ", cut short" : "", command.c_str());
            return buffer;
        }
        case GSCommandLogKind::CardTook:
            if (flags & GSCommandLogFlags::kSelection)
                return "card took " + command + " (" + SelectingMode(*this) + " selection)";
            return "card took " + command;
        case GSCommandLogKind::Anomaly:
            std::snprintf(buffer, sizeof(buffer), "anomaly %s #%X (%s)",
                          GSCommandLogAnomalyName(static_cast<GSCommandLogAnomaly>(value)), unsigned(count),
                          command.c_str());
            return buffer;
        case GSCommandLogKind::Marker:
            return "card reset";
    }
    return "?";
}

void GSCommandLogEntry::ToState(StateNode& node) const
{
    node["kind"] = std::string(GSCommandLogKindName(kind));
    node["card_cycle"] = cardCycle;
    node["frame"] = int64_t(frame);
    node["t"] = int64_t(mainT);
    node["pc"] = int(mainPc);
    node["command"] = int(command);
    if (const char* name = CommandName(*this))
        node["name"] = std::string(name);
    switch (kind)
    {
        case GSCommandLogKind::Command:
            if (flags & GSCommandLogFlags::kHasPre)
                node["pre"] = int(value);
            if (flags & GSCommandLogFlags::kStream)
                node["stream"] = true;
            if (flags & GSCommandLogFlags::kSelection)
                node["selection"] = SelectingMode(*this);
            if (flags & GSCommandLogFlags::kHandler)
            {
                node["card_pc"] = int(destAddr);
                const GSCommandInfo* info = CommandOf(*this);
                if (info && info->handler)
                    node["card_label"] = std::string(info->handler);
            }
            break;
        case GSCommandLogKind::Param:
        case GSCommandLogKind::Reply:
        case GSCommandLogKind::Data:
            node["value"] = int(value);
            node["index"] = int(index);
            if (kind == GSCommandLogKind::Param)
            {
                const GSCommandInfo* info = CommandOf(*this);
                if (info && index < GSCommandInfo::kMaxParams && info->params[index].name)
                    node["param"] = std::string(info->params[index].name);
            }
            if (flags & GSCommandLogFlags::kNotReady)
                node["not_ready"] = true;
            if (flags & GSCommandLogFlags::kUnasked)
                node["unasked"] = true;
            break;
        case GSCommandLogKind::Block:
            if (hostData)
                node["host_data"] = true;
            node["bytes"] = int64_t(count);
            if (value == 1 || value == 2)
                node["direction"] = std::string(value == 1 ? "read" : "write");
            node["dest"] = std::string(dest == GSCommandLogDest::Ram ? "ram" : "unknown");
            if (dest == GSCommandLogDest::Ram)
                node["address"] = int(destAddr);
            if (flags & GSCommandLogFlags::kTruncated)
                node["truncated"] = true;
            break;
        case GSCommandLogKind::Anomaly:
            node["anomaly"] = std::string(GSCommandLogAnomalyName(static_cast<GSCommandLogAnomaly>(value)));
            node["detail"] = int64_t(count);
            break;
        default:
            break;
    }
    if (flags & GSCommandLogFlags::kAutomation)
        node["automation"] = true;
    const GSFirmwareProfile* profile = ProfileAt(this->profile);
    node["profile"] = profile ? std::string(profile->id) : std::string();
    node["text"] = Text();
}

/// endregion </Entry>

/// region <Protocol state>

std::string GSProtocolState::Text() const
{
    if (!on)
        return "the command log is off: no protocol state";
    if (inTransfer)
    {
        std::string text = std::string(TransferName(static_cast<GSTransferKind>(transfer), direction)) + ": " +
                           Grouped(transferDone);
        text += transferTotal ? " of " + Grouped(transferTotal) + " bytes" : std::string(" bytes so far");
        if (hasAddress)
        {
            char at[12];
            std::snprintf(at, sizeof(at), " at #%04X", transferAddress);
            text += at;
        }
        return text + (hostData ? std::string(" (host data)") : " (" + CommandText(profile, transferCommand) + ")");
    }
    if (command < 0)
        return "idle";
    const std::string name = CommandText(profile, static_cast<uint8_t>(command));
    std::string due;
    if (paramsReceived < paramsExpected)
        due = "parameter " + std::to_string(paramsReceived + 1) + " of " + std::to_string(paramsExpected) + " due";
    else if (repliesRead < repliesExpected)
        due = "reply " + std::to_string(repliesRead + 1) + " of " + std::to_string(repliesExpected) + " due";
    if (!taken)
        return name + ": not taken yet" + (due.empty() ? std::string() : ", then " + due);
    if (!due.empty())
        return name + ": " + due;
    return "idle (last " + name + ")";
}

void GSProtocolState::ToState(StateNode& node) const
{
    node["on"] = on;
    node["text"] = Text();
    if (!on)
        return;
    node["profile"] = profile ? std::string(profile->id) : std::string();
    if (command >= 0)
    {
        node["command"] = command;
        if (const char* name = profile ? profile->Command(static_cast<uint8_t>(command)).name : nullptr)
            node["name"] = std::string(name);
        node["taken"] = taken;
        node["params_expected"] = int(paramsExpected);
        node["params_received"] = int(paramsReceived);
        node["replies_expected"] = int(repliesExpected);
        node["replies_read"] = int(repliesRead);
    }
    if (inTransfer)
    {
        StateNode t = StateNode::Object();
        t["kind"] = std::string(TransferName(static_cast<GSTransferKind>(transfer), direction));
        if (hostData)
            t["host_data"] = true;
        else
            t["command"] = int(transferCommand);
        t["bytes"] = int64_t(transferDone);
        if (transferTotal)
            t["total"] = int64_t(transferTotal);
        if (hasAddress)
            t["address"] = int(transferAddress);
        node["transfer"] = t;
    }
    StateNode list = StateNode::Array();
    for (const GSUploadRecord& u : uploads)
    {
        StateNode n = StateNode::Object();
        n["address"] = int(u.address);
        n["length"] = int64_t(u.length);
        n["sha256"] = u.sha256;
        n["profile"] = u.profile ? std::string(u.profile->id) : std::string();
        if (u.profile && u.matchedLength != u.length)
            n["matched_length"] = int64_t(u.matchedLength);
        n["frame"] = int64_t(u.frame);
        n["started"] = u.started;
        list.push(n);
    }
    node["uploads"] = list;
}

GSProtocolState GSCommandLog::State() const
{
    if (!On())
        return {};
    std::lock_guard lock(_stateMutex);
    GSProtocolState state = _state;
    state.on = true;
    state.uploads = _uploads;
    if (!_armed.load(std::memory_order_relaxed))
    {
        // Switched on, no host access since: nothing decoded yet
        state = {};
        state.on = true;
    }
    return state;
}

void GSCommandLog::Publish()
{
    GSProtocolState s;
    s.on = true;
    s.profile = _profile;
    s.command = _command;
    s.taken = !_commandPending;
    s.paramsExpected = static_cast<uint8_t>(_paramIndex + _postLeft);
    s.paramsReceived = _paramIndex;
    s.repliesExpected = static_cast<uint8_t>(_replyIndex + _repliesLeft);
    s.repliesRead = _replyIndex;
    s.inTransfer = _inTransfer;
    if (_inTransfer)
    {
        s.transferCommand = _transferCommand;
        s.transfer = _transfer;
        s.transferDone = _blockCount;
        s.transferTotal = _blockExpected;
        s.direction = _direction;
        s.hasAddress = _blockDest == GSCommandLogDest::Ram;
        s.transferAddress = _blockAddr;
    }
    else if (_hostStream)
    {
        s.inTransfer = true;
        s.hostData = true;
        s.transfer = static_cast<uint8_t>(GSTransferKind::Stream);
        s.transferDone = _hostStreamCount;
    }
    std::lock_guard lock(_stateMutex);
    _state = s;
}

/// endregion </Protocol state>

/// region <Decoder>

bool GSCommandLog::Skip() const
{
    // A TTD replay re-runs what the log already holds (the WD1793's history does the same)
    return _context && _context->ttdReplayActive;
}

namespace
{
bool Contains(const std::vector<uint8_t>& list, uint8_t code)
{
    return std::find(list.begin(), list.end(), code) != list.end();
}
}  // namespace

void GSCommandLog::PushAnomaly(GSCommandLogAnomaly anomaly, uint32_t detail, bool automation, int command)
{
    GSCommandLogEntry e = Make(GSCommandLogKind::Anomaly, uint8_t(anomaly), automation);
    if (command >= 0)
        e.command = static_cast<uint8_t>(command);
    e.count = detail;
    Push(e);
}

bool GSCommandLog::NoteMode(uint8_t command, bool automation)
{
    if (!_profile || _profile->modes.empty())
        return false;
    if (_mode >= 0)
    {
        const GSProfileMode& mode = _profile->modes[static_cast<size_t>(_mode)];
        if (Contains(mode.selects, command))
            return true;
        if (Contains(mode.leave, command) || command >= mode.leaveFrom)
        {
            _mode = -1;
            for (size_t i = 0; i < _profile->modes.size(); i++)
                if (Contains(_profile->modes[i].enter, command))
                    _mode = static_cast<int>(i);
            return false;
        }
        if (!Contains(mode.accepts, command) && !Contains(mode.enter, command))
            PushAnomaly(GSCommandLogAnomaly::CommandRejectedInState, command, automation, command);
    }
    for (size_t i = 0; i < _profile->modes.size(); i++)
        if (Contains(_profile->modes[i].enter, command))
            _mode = static_cast<int>(i);
    return false;
}

bool GSCommandLog::UploadOverRunningCode() const
{
    // The block's 16K windows against the window the card executes from: the same RAM page is code being replaced
    Z80CpuRegisters regs{};
    if (!_card || !_card->cardRegisters(regs))
        return false;
    const GeneralSoundCard::CardPage running = _card->mapCardAddress(regs.pc);
    if (running.kind != GeneralSoundCard::CardPage::Ram)
        return false;
    const uint32_t first = _blockAddr;
    const uint32_t last = std::min<uint32_t>(first + (_blockExpected ? _blockExpected : 0x10000) - 1, 0xFFFF);
    for (uint32_t window = first & 0xC000; window <= last; window += 0x4000)
    {
        const GeneralSoundCard::CardPage page = _card->mapCardAddress(static_cast<uint16_t>(window));
        if (page.kind == running.kind && page.page == running.page)
            return true;
    }
    return false;
}

void GSCommandLog::Arm()
{
    ResetDecoder();
    _commandPending = false;
    _lastDataFromHost = false;
    _hostStream = false;  // a stream from an earlier recording is not this one's
    _hostStreamCount = 0;
    {
        std::lock_guard lock(_stateMutex);
        _uploads.clear();
        _unknownCounts.fill(0);
    }
    _romProfile = _card ? GSFirmwareProfileOf(*_card) : nullptr;
    SetProfile(_romProfile);
    _armed.store(true, std::memory_order_relaxed);
}

namespace
{
uint8_t ProfileIndex(const GSFirmwareProfile* profile)
{
    const auto& profiles = GSFirmwareProfiles();
    for (size_t i = 0; i < profiles.size() && i < 0xFF; i++)
        if (profiles[i] == profile)
            return static_cast<uint8_t>(i);
    return 0xFF;
}
}  // namespace

void GSCommandLog::SetProfile(const GSFirmwareProfile* profile)
{
    // The stack back to one profile: the ROM's (a reset, a start) or code that replaces the firmware
    if (_hostStream && profile != _profile)
        EndHostStream(true, false);
    _stack.clear();
    if (profile)
        _stack.push_back(profile);
    _profile = profile;
    _profileIndex = ProfileIndex(profile);
    _mode = -1;
}

void GSCommandLog::PushProfile(const GSFirmwareProfile* profile)
{
    if (Skip() || !profile)
        return;
    const PublishOnExit publish{*this};
    if (!_armed.load(std::memory_order_relaxed))
        Arm();
    if (profile == _profile)
        return;
    if (!profile->extends)
    {
        SetProfile(profile);  // the code took over the card: nothing below it answers
        return;
    }
    if (_hostStream)
        EndHostStream(true, false);
    _stack.push_back(profile);
    _profile = profile;
    _profileIndex = ProfileIndex(profile);
}

const GSFirmwareProfile* GSCommandLog::Resolver(uint8_t code) const
{
    for (auto it = _stack.rbegin(); it != _stack.rend(); ++it)
    {
        if ((*it)->Command(code).name)
            return *it;
        if (!(*it)->extends)
            break;
    }
    return _profile;
}

const GSCommandInfo& GSCommandLog::Info(uint8_t code) const
{
    return Resolver(code)->Command(code);
}

void GSCommandLog::Push(GSCommandLogEntry entry)
{
    if (_stack.size() > 1 && entry.profile == _profileIndex)
        entry.profile = ProfileIndex(Resolver(entry.command));
    _history.Push(entry);
}

void GSCommandLog::ResetDecoder()
{
    _command = -1;
    _postLeft = 0;
    _paramIndex = 0;
    _repliesLeft = 0;
    _replyIndex = 0;
    _hasPre = false;
    _inTransfer = false;
    _blockCount = 0;
    _blockExpected = 0;
    _stallReported = false;
    _polledReady = false;
    _draining = false;
    _replyWaitSeen = false;
    _replyStallReported = false;
}

GSCommandLogEntry GSCommandLog::Make(GSCommandLogKind kind, uint8_t value, bool automation) const
{
    GSCommandLogEntry e;
    e.kind = kind;
    e.value = value;
    e.command = static_cast<uint8_t>(_command < 0 ? 0 : _command);
    e.profile = _profileIndex;
    e.cardCycle = _card ? _card->cardTime() : 0;
    if (_context)
    {
        e.frame = static_cast<uint32_t>(_context->emulatorState.frame_counter);
        if (const Z80* cpu = _context->pCore ? _context->pCore->GetZ80() : nullptr)
        {
            e.mainT = cpu->t;
            e.mainPc = cpu->m1_pc;
        }
    }
    if (automation)
        e.flags |= GSCommandLogFlags::kAutomation;
    return e;
}

void GSCommandLog::NoteTaken(uint8_t status)
{
    // The card's RSCOM clears status bit 0: it took the command
    if (_commandPending && !(status & 0x01))
    {
        GSCommandLogEntry e = Make(GSCommandLogKind::CardTook, 0, false);
        e.command = _pendingCommand;
        if (_pendingSelection)
            e.flags |= GSCommandLogFlags::kSelection;
        if (_pendingProfile != 0xFF)
            e.profile = _pendingProfile;
        Push(e);
        _commandPending = false;
        _pendingSelection = false;
        // A transfer's time runs from when the card took its command (a busy card takes it seconds later)
        if (_context)
            _progressFrame = _context->emulatorState.frame_counter;
    }
}

void GSCommandLog::BeginTransfer()
{
    const GSCommandInfo& info = Info(static_cast<uint8_t>(_command));
    static_assert(sizeof(_params) == GSCommandInfo::kMaxParams);
    _direction = 0;
    if (info.transfer == GSTransferKind::Sectors)
    {
        // The selecting parameter says read or write; any other value (init, compare) moves no sectors, the reply
        // follows the parameters
        const uint8_t select = _params[info.sectors.selectParam];
        _direction = select == info.sectors.read ? 1 : select == info.sectors.write ? 2 : 0;
        if (_direction == 0)
            return;
    }
    _inTransfer = true;
    _transfer = static_cast<uint8_t>(info.transfer);
    _transferCommand = static_cast<uint8_t>(_command);
    _blockCount = 0;
    _blockExpected = 0;
    _blockDest = GSCommandLogDest::Unknown;
    _stallReported = false;
    _progressFrame = _context ? _context->emulatorState.frame_counter : 0;
    _uploadBytes.clear();
    if (info.transfer == GSTransferKind::BlockUpload || info.transfer == GSTransferKind::BlockDownload)
    {
        // #14 / #15: length low (the pre byte), length high, address low, address high
        const uint32_t length = _params[0] | (_params[1] << 8);
        _blockExpected = length ? length : 0x10000;
        _blockAddr = static_cast<uint16_t>(_params[2] | (_params[3] << 8));
        _blockDest = GSCommandLogDest::Ram;
        if (info.transfer == GSTransferKind::BlockUpload && UploadOverRunningCode())
            PushAnomaly(GSCommandLogAnomaly::UploadOverRunningCode, _blockAddr, false, _command);
    }
    if (info.transfer == GSTransferKind::FixedBlock)
        _blockExpected = info.transferLength;
    if (info.transfer == GSTransferKind::PagedBlock)
        _blockExpected = _params[0] * 256u;  // 0 pages: until a command ends it
    if (info.transfer == GSTransferKind::Sectors)
    {
        // The reply (the status) comes after the sectors
        const uint8_t count = _params[info.sectors.countParam];
        _blockExpected = (count ? count : 256u) * info.transferLength;
    }
    if (info.transfer == GSTransferKind::BlockDownload)
        _repliesLeft = 0;  // the reads are the block's bytes, counted
}

bool GSCommandLog::HostReadsTransfer() const
{
    const auto transfer = static_cast<GSTransferKind>(_transfer);
    return transfer == GSTransferKind::BlockDownload || (transfer == GSTransferKind::Sectors && _direction == 1);
}

void GSCommandLog::TransferDone(bool automation)
{
    EndTransfer(false, automation);
    // A sector transfer's status reply is still due; any other block ends the command
    if (static_cast<GSTransferKind>(_transfer) != GSTransferKind::Sectors)
        ResetDecoder();
}

void GSCommandLog::EndTransfer(bool truncated, bool automation)
{
    GSCommandLogEntry e = Make(GSCommandLogKind::Block, 0, automation);
    e.command = _transferCommand;
    e.count = _blockCount;
    e.dest = _blockDest;
    e.destAddr = _blockAddr;
    e.value = _direction;
    if (truncated)
        e.flags |= GSCommandLogFlags::kTruncated;
    Push(e);
    _inTransfer = false;
    if (!truncated && static_cast<GSTransferKind>(_transfer) == GSTransferKind::BlockUpload)
        RecordUpload();
    _uploadBytes.clear();
}

void GSCommandLog::EndHostStream(bool truncated, bool automation)
{
    GSCommandLogEntry e = Make(GSCommandLogKind::Block, 0, automation);
    e.command = 0;
    e.hostData = 1;
    e.count = _hostStreamCount;
    e.profile = _hostStreamProfile;
    if (truncated)
        e.flags |= GSCommandLogFlags::kTruncated;
    _history.Push(e);  // not Push(): no command to resolve the profile by
    _hostStream = false;
    _hostStreamCount = 0;
}

void GSCommandLog::RecordUpload()
{
    GSUploadRecord record;
    record.address = _blockAddr;
    record.length = static_cast<uint32_t>(_uploadBytes.size());
    record.frame = _context ? static_cast<uint32_t>(_context->emulatorState.frame_counter) : 0;
    record.sha256 = digestpp::sha256().absorb(_uploadBytes.data(), _uploadBytes.size()).hexdigest();
    // An upload match covers its length from the block's start: a loader may send its code padded
    for (const GSFirmwareProfile* profile : GSFirmwareProfiles())
    {
        for (const GSProfileMatch& match : profile->matches)
        {
            if (match.kind != GSMatchKind::Upload || match.address != record.address || match.length == 0 ||
                match.length > record.length)
                continue;
            const std::string digest = match.length == record.length
                                           ? record.sha256
                                           : digestpp::sha256().absorb(_uploadBytes.data(), match.length).hexdigest();
            if (digest == match.sha256 && match.length > record.matchedLength)
            {
                record.profile = profile;
                record.matchedLength = match.length;
            }
        }
    }
    std::lock_guard lock(_stateMutex);
    if (_uploads.size() == kMaxUploads)
        _uploads.erase(_uploads.begin());
    _uploads.push_back(std::move(record));
}

void GSCommandLog::StartUploadedCode(uint16_t address)
{
    // #13 into a known upload: its profile decodes from now on (the newest block covering the address wins)
    const GSFirmwareProfile* profile = nullptr;
    {
        std::lock_guard lock(_stateMutex);
        for (auto it = _uploads.rbegin(); it != _uploads.rend(); ++it)
            if (address >= it->address && address < it->address + it->length)
            {
                it->started = true;
                profile = it->profile;
                break;
            }
    }
    if (profile)
        PushProfile(profile);
}

bool GSCommandLog::DispatchHandler(uint8_t code, uint16_t& handler) const
{
    if (!_card)
        return false;
    const auto word = [this](uint16_t at, uint16_t& out) {
        uint8_t lo = 0, hi = 0;
        if (!_card->peekCardMemory(at, lo) || !_card->peekCardMemory(static_cast<uint16_t>(at + 1), hi))
            return false;
        out = static_cast<uint16_t>(lo | (hi << 8));
        return true;
    };
    for (auto it = _stack.rbegin(); it != _stack.rend(); ++it)
    {
        const GSFirmwareProfile& p = **it;
        if (p.dispatch.kind == GSDispatchKind::Words && p.dispatch.place == GSPlace::Window)
        {
            // Entries #00-#1F, then `highFrom` on
            uint32_t index = code;
            if (code >= 0x20)
            {
                if (code < p.dispatch.highFrom)
                    return false;
                index = 0x20u + code - p.dispatch.highFrom;
            }
            return index < p.dispatch.entries && word(static_cast<uint16_t>(p.dispatch.table + 2 * index), handler);
        }
        if (p.comtab)
        {
            // The stock split: COMTAB words for #00-#1F and #F0-#FF, COMTABH bytes (high 256 later) for #20-#EF
            if (code < 0x20 || code >= 0xF0)
                return word(static_cast<uint16_t>(p.comtab + 2 * (code < 0x20 ? code : code - 0xD0)), handler);
            uint8_t lo = 0, hi = 0;
            const uint16_t at = static_cast<uint16_t>(p.comtabh + code - 0x20);
            if (!_card->peekCardMemory(at, lo) || !_card->peekCardMemory(static_cast<uint16_t>(at + 0x100), hi))
                return false;
            handler = static_cast<uint16_t>(lo | (hi << 8));
            return true;
        }
    }
    return false;
}

std::array<uint32_t, 256> GSCommandLog::UnknownCommands() const
{
    std::lock_guard lock(_stateMutex);
    return _unknownCounts;
}

std::vector<GSUploadRecord> GSCommandLog::Uploads() const
{
    std::lock_guard lock(_stateMutex);
    return _uploads;
}

void GSCommandLog::HostCommand(uint8_t command, bool automation)
{
    if (Skip())
        return;
    const PublishOnExit publish{*this};
    if (!_armed.load(std::memory_order_relaxed))
        Arm();
    const uint8_t status = _card ? _card->getStatusRaw() : 0;
    NoteTaken(status);
    if (_inTransfer && _profile && Info(_transferCommand).handshake.strobe)
    {
        // A #BB write inside this transfer is its strobe (M6), not a command
        _progressFrame = _context ? _context->emulatorState.frame_counter : 0;
        _stallReported = false;
        return;
    }
    // A firmware that leaves the command flag set (handshake ack: data, M6) has the host wait on the data flag
    const bool ackByData = _profile && Info(_pendingCommand).handshake.ack == GSHandshakeAck::Data;
    if (_commandPending && (status & 0x01) && !ackByData)
        PushAnomaly(GSCommandLogAnomaly::CommandOverwritten, _pendingCommand, automation, _pendingCommand);
    _commandPending = true;
    _pendingCommand = command;
    _pendingSelection = false;
    _pendingProfile = 0xFF;
    _polledReady = false;
    _draining = false;
    _replyWaitSeen = false;
    _replyStallReported = false;
    if (NoteMode(command, automation))
    {
        // The firmware's mode takes it (the test mode's test number): no parameters, replies or transfer follow
        GSCommandLogEntry e = Make(GSCommandLogKind::Command, 0, automation);
        e.command = command;
        e.flags |= GSCommandLogFlags::kSelection;
        Push(e);
        _pendingSelection = true;
        return;
    }
    if (_hostStream)
    {
        // The code's own polls go on inside the stream; what resets the conversation or starts a transfer ends it
        const GSCommandInfo* info = _profile ? &Info(command) : nullptr;
        if (!info || !info->name || info->handshake.exit || info->handshake.barrier ||
            info->transfer != GSTransferKind::None)
            EndHostStream(false, automation);
    }
    const GSCommandInfo* barrier = _profile ? &Info(command) : nullptr;
    uint8_t exitProfile = 0xFF;
    if (barrier && barrier->name && barrier->handshake.exit)
    {
        // The uploaded code jumps back to the ROM: the ROM's profile decodes from here (the command is the code's)
        exitProfile = ProfileIndex(Resolver(command));
        SetProfile(_romProfile);
        barrier = nullptr;
        if (_inTransfer)
            EndTransfer(true, automation);
        ResetDecoder();
    }
    if (barrier && barrier->name && barrier->handshake.barrier)
    {
        // The command resets the conversation: what was open ends here
        if (_inTransfer)
            EndTransfer(true, automation);
        ResetDecoder();
    }

    if (_inTransfer)
    {
        const auto transfer = static_cast<GSTransferKind>(_transfer);
        if (transfer == GSTransferKind::ModuleUpload || transfer == GSTransferKind::SampleUpload)
        {
            // LOAD takes a command as a stream command: #D2 closes, #F3 / #F4 restart, any other is acknowledged
            const bool ends = command == 0xD2 || command == 0xF3 || command == 0xF4;
            // #F3 / #F4 restart the upload mid-stream; a command that is not a stream command is acknowledged and
            // not run: either way not what the host meant
            bool streamCommand = false;
            for (size_t i = 0; _profile && i < _profile->streamCommandCount; i++)
                streamCommand = streamCommand || _profile->streamCommands[i].code == command;
            if (!streamCommand && _profile)
                PushAnomaly(GSCommandLogAnomaly::CommandWhileBusy, command, automation, command);
            if (ends)
                EndTransfer(false, automation);
            GSCommandLogEntry e = Make(GSCommandLogKind::Command, 0, automation);
            e.command = command;
            e.flags |= GSCommandLogFlags::kStream;
            Push(e);
            if (ends)
            {
                ResetDecoder();
                _command = command;
            }
            return;
        }
        // A block, a download or a Covox stream ends at the next command
        EndTransfer(_blockExpected != 0 && _blockCount < _blockExpected, automation);
    }

    _command = command;
    _postLeft = 0;
    _repliesLeft = 0;
    _replyIndex = 0;
    _paramIndex = 0;
    GSCommandLogEntry e = Make(GSCommandLogKind::Command, 0, automation);
    const GSCommandInfo* info = _profile ? &Info(command) : nullptr;
    if (_profile && (!info || !info->name))
    {
        std::lock_guard lock(_stateMutex);
        _unknownCounts[command]++;
    }
    if (uint16_t handler = 0; DispatchHandler(command, handler))
    {
        e.destAddr = handler;
        e.flags |= GSCommandLogFlags::kHandler;
    }
    if (info && info->name && _hasPre && !info->pre)
        PushAnomaly(GSCommandLogAnomaly::DataBeforeCommand, _pre, automation, command);
    if (info && info->name)
    {
        if (info->pre)
        {
            _params[0] = _hasPre ? _pre : 0;
            if (_hasPre)
            {
                e.value = _pre;
                e.flags |= GSCommandLogFlags::kHasPre;
            }
            _paramIndex = 1;
        }
        if (info->post == GSCommandInfo::kVariablePost)
            // #0D: a byte per channel of the mask's low nibble; #6B: the optional high byte
            _postLeft = command == 0x0D ? static_cast<uint8_t>(std::popcount(static_cast<unsigned>(_params[0] & 0x0F))) : 1;
        else
            _postLeft = info->post;
        _repliesLeft = info->reply == GSCommandInfo::kVariablePost ? 0 : info->reply;
    }
    _hasPre = false;
    if (exitProfile != 0xFF)
        e.profile = exitProfile;
    _pendingProfile = exitProfile;  // its CardTook is named by the same profile
    Push(e);
    // #3E loads only for format #00 / #01 (any other replies 0, no upload)
    const bool loads = !(command == 0x3E && _params[0] > 1);
    if (info && info->name && info->transfer != GSTransferKind::None && _postLeft == 0 && loads)
        BeginTransfer();
}

void GSCommandLog::HostData(uint8_t value, bool automation)
{
    if (Skip())
        return;
    const PublishOnExit publish{*this};
    if (!_armed.load(std::memory_order_relaxed))
        Arm();
    const uint8_t status = _card ? _card->getStatusRaw() : 0;
    NoteTaken(status);
    const bool overwrites = (status & 0x80) && _lastDataFromHost && _repliesLeft == 0;
    _draining = false;
    const uint8_t lost = _lastData;
    _lastDataFromHost = true;
    _lastData = value;

    if (_inTransfer && !HostReadsTransfer())
    {
        if (overwrites)
        {
            GSCommandLogEntry e = Make(GSCommandLogKind::Anomaly, uint8_t(GSCommandLogAnomaly::DataOverwritten), automation);
            e.command = _transferCommand;
            e.count = lost;
            Push(e);
        }
        _blockCount++;
        if (static_cast<GSTransferKind>(_transfer) == GSTransferKind::BlockUpload && _uploadBytes.size() < 0x10000)
            _uploadBytes.push_back(value);
        _progressFrame = _context ? _context->emulatorState.frame_counter : 0;
        _stallReported = false;
        if (_blockExpected && _blockCount >= _blockExpected)
            TransferDone(automation);
        return;
    }
    if (overwrites)
    {
        GSCommandLogEntry e = Make(GSCommandLogKind::Anomaly, uint8_t(GSCommandLogAnomaly::DataOverwritten), automation);
        e.count = lost;
        Push(e);
    }
    if (_postLeft > 0)
    {
        GSCommandLogEntry e = Make(GSCommandLogKind::Param, value, automation);
        e.index = _paramIndex;
        Push(e);
        if (_paramIndex < GSCommandInfo::kMaxParams)
            _params[_paramIndex] = value;
        _paramIndex++;
        if (--_postLeft == 0 && _command == 0x13 && _profile && Info(0x13).name)
            StartUploadedCode(static_cast<uint16_t>(_params[0] | (_params[1] << 8)));
        else if (_postLeft == 0 && _profile && _command >= 0 &&
                 Info(static_cast<uint8_t>(_command)).transfer != GSTransferKind::None)
            BeginTransfer();
        return;
    }
    if (_profile && _profile->hostDataStream)
    {
        // The code takes data outside its commands as a stream: counted, not a pre byte
        if (!_hostStream)
        {
            _hostStream = true;
            _hostStreamCount = 0;
            _hostStreamProfile = _profileIndex;
        }
        _hostStreamCount++;
        return;
    }
    GSCommandLogEntry e = Make(GSCommandLogKind::Data, value, automation);
    Push(e);
    _hasPre = true;
    _pre = value;
}

void GSCommandLog::HostReplyRead(uint8_t value, bool automation)
{
    if (Skip())
        return;
    const PublishOnExit publish{*this};
    if (!_armed.load(std::memory_order_relaxed))
        Arm();
    const uint8_t status = _card ? _card->getStatusRaw() : 0;
    NoteTaken(status);
    // A handler that reads its data byte after the reply clears the flag itself: once it took the command the
    // reply is in the register
    const bool unflagged = _command >= 0 && _profile &&
                           Info(static_cast<uint8_t>(_command)).replyUnflagged && !_commandPending;
    const bool ready = (status & 0x80) != 0 || unflagged;
    const bool polled = _polledReady || unflagged;
    const bool hostByte = _lastDataFromHost;  // the flag is the host's own byte, not a reply
    _polledReady = false;
    _replyWaitSeen = false;
    _lastDataFromHost = false;

    if (_inTransfer && HostReadsTransfer())
    {
        _blockCount++;
        _progressFrame = _context ? _context->emulatorState.frame_counter : 0;
        _stallReported = false;
        if (_blockCount >= _blockExpected)
            TransferDone(automation);
        return;
    }
    GSCommandLogEntry e = Make(GSCommandLogKind::Reply, value, automation);
    e.index = _replyIndex;
    if (!ready)
        e.flags |= GSCommandLogFlags::kNotReady;
    if (_repliesLeft != 0 && value == 0 && _command >= 0 && _profile &&
        Info(static_cast<uint8_t>(_command)).handshake.valuePoll)
    {
        // The host polls the value (#F4: 0 until INIT wrote the page count): the reply is still due
        e.flags |= GSCommandLogFlags::kNotReady;
        Push(e);
        return;
    }
    if (_repliesLeft == 0)
        e.flags |= GSCommandLogFlags::kUnasked;
    Push(e);
    if (_repliesLeft == 0)
    {
        // Reads that empty a byte nobody asked for: once a run (a command or a data write starts a new one)
        if ((status & 0x80) && !hostByte && !_draining && !automation && !(_profile && _profile->unsolicitedReplies))
        {
            PushAnomaly(GSCommandLogAnomaly::StaleReplyDrain, value, automation);
            _draining = true;
        }
        return;
    }
    // The byte was there but the host did not look: it relied on the card's speed (a value poll is a poll)
    const bool valuePoll = _command >= 0 && _profile && Info(static_cast<uint8_t>(_command)).handshake.valuePoll;
    if (ready && !polled && !automation && !valuePoll)
        PushAnomaly(GSCommandLogAnomaly::ReplyReadWithoutPoll, value, automation);
    {
        if (!ready)
        {
            // The host did not wait for bit 7: the byte is stale, the reply still due
            GSCommandLogEntry a = Make(GSCommandLogKind::Anomaly, uint8_t(GSCommandLogAnomaly::ReplyNotReady), automation);
            a.count = value;
            Push(a);
            return;
        }
        _replyIndex++;
        _repliesLeft--;
    }
}

void GSCommandLog::HostStatusRead(uint8_t status)
{
    if (Skip())
        return;
    if (!_armed.load(std::memory_order_relaxed))
    {
        const PublishOnExit publish{*this};
        Arm();
    }
    // A poll in a loop: no entry, no publish unless the card took the command
    if (status & 0x80)
        _polledReady = true;
    if (_commandPending && !(status & 0x01))
    {
        const PublishOnExit publish{*this};
        NoteTaken(status);
    }
}

void GSCommandLog::HostReset()
{
    if (Skip())
        return;
    const PublishOnExit publish{*this};
    if (!_armed.load(std::memory_order_relaxed))
        Arm();
    if (_inTransfer)
        EndTransfer(true, false);
    if (_hostStream)
        EndHostStream(true, false);
    Push(Make(GSCommandLogKind::Marker, 0, false));
    ResetDecoder();
    _commandPending = false;
    _lastDataFromHost = false;
    SetProfile(_romProfile);  // the uploaded code is gone (M5)
}

void GSCommandLog::Frame()
{
    // A host that only polls the status (IN #BB, no hook) sees the card take the command: so does the log, at the
    // frame start at the latest
    if (_commandPending && _card && !Skip() && _armed.load(std::memory_order_relaxed))
    {
        NoteTaken(_card->getStatusRaw());
        if (!_commandPending)
            Publish();
    }
    if (_repliesLeft > 0 && _command >= 0 && _profile && _card && _context && !Skip() &&
        _armed.load(std::memory_order_relaxed) && !_replyStallReported &&
        Info(static_cast<uint8_t>(_command)).handshake.replyWait)
    {
        // The handler waits until its reply is read; the host does not read it
        const uint64_t frame = _context->emulatorState.frame_counter;
        if (!(_card->getStatusRaw() & 0x80))
            _replyWaitSeen = false;
        else if (!_replyWaitSeen)
        {
            _replyWaitSeen = true;
            _replyWaitFrame = frame;
        }
        else if (frame - _replyWaitFrame >= kStallFrames)
        {
            PushAnomaly(GSCommandLogAnomaly::CardReplyStall, static_cast<uint32_t>(frame - _replyWaitFrame), false);
            _replyStallReported = true;
        }
    }
    if (!_inTransfer || _stallReported || !_context || Skip())
        return;
    if (_commandPending || _context->emulatorState.frame_counter - _progressFrame < kStallFrames)
        return;  // the card has not taken the command yet: busy, not stalled
    GSCommandLogEntry e = Make(GSCommandLogKind::Anomaly, uint8_t(GSCommandLogAnomaly::TransferStalled), false);
    e.command = _transferCommand;
    e.count = _blockCount;
    Push(e);
    _stallReported = true;
}

/// endregion </Decoder>
