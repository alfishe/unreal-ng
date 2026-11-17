#include "emulator/sound/chips/gs/gscommandlog.h"

#include <bit>
#include <cstdio>

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
    if (e.kind == GSCommandLogKind::Command && (e.flags & GSCommandLogFlags::kStream))
        return StreamCommandName(ProfileAt(e.profile), e.command);
    const GSCommandInfo* info = CommandOf(e);
    return info ? info->name : nullptr;
}

const char* TransferName(GSTransferKind kind)
{
    switch (kind)
    {
        case GSTransferKind::ModuleUpload: return "module upload";
        case GSTransferKind::SampleUpload: return "sample upload";
        case GSTransferKind::BlockUpload: return "block upload";
        case GSTransferKind::BlockDownload: return "block download";
        case GSTransferKind::Stream: return "stream";
        default: return "transfer";
    }
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
            if (flags & GSCommandLogFlags::kHasPre)
            {
                const char* param = info && info->params[0].name ? info->params[0].name : "pre";
                std::snprintf(buffer, sizeof(buffer), "%s, %s #%02X", command.c_str(), param, value);
                return buffer;
            }
            return command;
        case GSCommandLogKind::Param:
        {
            const char* param = info && index < 4 && info->params[index].name ? info->params[index].name : "param";
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
            const GSTransferKind transfer = info ? info->transfer : GSTransferKind::None;
            if (dest == GSCommandLogDest::Ram)
                std::snprintf(buffer, sizeof(buffer), "%s: %u bytes at #%04X%s (%s)", TransferName(transfer),
                              unsigned(count), destAddr, (flags & GSCommandLogFlags::kTruncated) ? ", cut short" : "",
                              command.c_str());
            else
                std::snprintf(buffer, sizeof(buffer), "%s: %u bytes%s (%s)", TransferName(transfer), unsigned(count),
                              (flags & GSCommandLogFlags::kTruncated) ? ", cut short" : "", command.c_str());
            return buffer;
        }
        case GSCommandLogKind::CardTook:
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
            break;
        case GSCommandLogKind::Param:
        case GSCommandLogKind::Reply:
        case GSCommandLogKind::Data:
            node["value"] = int(value);
            node["index"] = int(index);
            if (kind == GSCommandLogKind::Param)
            {
                const GSCommandInfo* info = CommandOf(*this);
                if (info && index < 4 && info->params[index].name)
                    node["param"] = std::string(info->params[index].name);
            }
            if (flags & GSCommandLogFlags::kNotReady)
                node["not_ready"] = true;
            if (flags & GSCommandLogFlags::kUnasked)
                node["unasked"] = true;
            break;
        case GSCommandLogKind::Block:
            node["bytes"] = int64_t(count);
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

/// region <Decoder>

bool GSCommandLog::Skip() const
{
    // A TTD replay re-runs what the log already holds (the WD1793's history does the same)
    return _context && _context->ttdReplayActive;
}

void GSCommandLog::Arm()
{
    ResetDecoder();
    _commandPending = false;
    _lastDataFromHost = false;
    _profile = nullptr;
    _profileIndex = 0xFF;
    if (_card)
    {
        // The lightweight card answers as gs105a does (soundchip_gslw.h); a card with a ROM by its digest
        if (_card->implementation() == GSCardImplementation::LW)
            _profile = GSFirmwareProfiles()[1];
        else
            _profile = IdentifyGSFirmware(*_card).profile;
    }
    const auto& profiles = GSFirmwareProfiles();
    for (size_t i = 0; i < profiles.size(); i++)
        if (profiles[i] == _profile)
            _profileIndex = static_cast<uint8_t>(i);
    _armed.store(true, std::memory_order_relaxed);
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
        Push(e);
        _commandPending = false;
    }
}

void GSCommandLog::BeginTransfer()
{
    const GSCommandInfo& info = _profile->Command(static_cast<uint8_t>(_command));
    _inTransfer = true;
    _transfer = static_cast<uint8_t>(info.transfer);
    _transferCommand = static_cast<uint8_t>(_command);
    _blockCount = 0;
    _blockExpected = 0;
    _blockDest = GSCommandLogDest::Unknown;
    _stallReported = false;
    _progressFrame = _context ? _context->emulatorState.frame_counter : 0;
    if (info.transfer == GSTransferKind::BlockUpload || info.transfer == GSTransferKind::BlockDownload)
    {
        // #14 / #15: length low (the pre byte), length high, address low, address high
        const uint32_t length = _params[0] | (_params[1] << 8);
        _blockExpected = length ? length : 0x10000;
        _blockAddr = static_cast<uint16_t>(_params[2] | (_params[3] << 8));
        _blockDest = GSCommandLogDest::Ram;
    }
    if (info.transfer == GSTransferKind::BlockDownload)
        _repliesLeft = 0;  // the reads are the block's bytes, counted
}

void GSCommandLog::EndTransfer(bool truncated, bool automation)
{
    GSCommandLogEntry e = Make(GSCommandLogKind::Block, 0, automation);
    e.command = _transferCommand;
    e.count = _blockCount;
    e.dest = _blockDest;
    e.destAddr = _blockAddr;
    if (truncated)
        e.flags |= GSCommandLogFlags::kTruncated;
    Push(e);
    _inTransfer = false;
}

void GSCommandLog::HostCommand(uint8_t command, bool automation)
{
    if (Skip())
        return;
    if (!_armed.load(std::memory_order_relaxed))
        Arm();
    const uint8_t status = _card ? _card->getStatusRaw() : 0;
    NoteTaken(status);
    if (_commandPending && (status & 0x01))
    {
        GSCommandLogEntry e = Make(GSCommandLogKind::Anomaly, uint8_t(GSCommandLogAnomaly::CommandOverwritten), automation);
        e.command = _pendingCommand;
        e.count = _pendingCommand;
        Push(e);
    }
    _commandPending = true;
    _pendingCommand = command;

    if (_inTransfer)
    {
        const auto transfer = static_cast<GSTransferKind>(_transfer);
        if (transfer == GSTransferKind::ModuleUpload || transfer == GSTransferKind::SampleUpload)
        {
            // LOAD takes a command as a stream command: #D2 closes, #F3 / #F4 restart, any other is acknowledged
            const bool ends = command == 0xD2 || command == 0xF3 || command == 0xF4;
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
    const GSCommandInfo* info = _profile ? &_profile->Command(command) : nullptr;
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
    if (!_armed.load(std::memory_order_relaxed))
        Arm();
    const uint8_t status = _card ? _card->getStatusRaw() : 0;
    NoteTaken(status);
    const bool overwrites = (status & 0x80) && _lastDataFromHost && _repliesLeft == 0;
    const uint8_t lost = _lastData;
    _lastDataFromHost = true;
    _lastData = value;

    if (_inTransfer && static_cast<GSTransferKind>(_transfer) != GSTransferKind::BlockDownload)
    {
        if (overwrites)
        {
            GSCommandLogEntry e = Make(GSCommandLogKind::Anomaly, uint8_t(GSCommandLogAnomaly::DataOverwritten), automation);
            e.command = _transferCommand;
            e.count = lost;
            Push(e);
        }
        _blockCount++;
        _progressFrame = _context ? _context->emulatorState.frame_counter : 0;
        _stallReported = false;
        if (_blockExpected && _blockCount >= _blockExpected)
        {
            EndTransfer(false, automation);
            ResetDecoder();
        }
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
        if (_paramIndex < 4)
            _params[_paramIndex] = value;
        _paramIndex++;
        if (--_postLeft == 0 && _profile && _command >= 0 &&
            _profile->Command(static_cast<uint8_t>(_command)).transfer != GSTransferKind::None)
            BeginTransfer();
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
    if (!_armed.load(std::memory_order_relaxed))
        Arm();
    const uint8_t status = _card ? _card->getStatusRaw() : 0;
    NoteTaken(status);
    // A handler that reads its data byte after the reply clears the flag itself: once it took the command the
    // reply is in the register
    const bool unflagged = _command >= 0 && _profile &&
                           _profile->Command(static_cast<uint8_t>(_command)).replyUnflagged && !_commandPending;
    const bool ready = (status & 0x80) != 0 || unflagged;
    _lastDataFromHost = false;

    if (_inTransfer && static_cast<GSTransferKind>(_transfer) == GSTransferKind::BlockDownload)
    {
        _blockCount++;
        _progressFrame = _context ? _context->emulatorState.frame_counter : 0;
        _stallReported = false;
        if (_blockCount >= _blockExpected)
        {
            EndTransfer(false, automation);
            ResetDecoder();
        }
        return;
    }
    GSCommandLogEntry e = Make(GSCommandLogKind::Reply, value, automation);
    e.index = _replyIndex;
    if (!ready)
        e.flags |= GSCommandLogFlags::kNotReady;
    if (_repliesLeft == 0)
        e.flags |= GSCommandLogFlags::kUnasked;
    Push(e);
    if (_repliesLeft > 0)
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

void GSCommandLog::HostReset()
{
    if (Skip())
        return;
    if (!_armed.load(std::memory_order_relaxed))
        Arm();
    if (_inTransfer)
        EndTransfer(true, false);
    Push(Make(GSCommandLogKind::Marker, 0, false));
    ResetDecoder();
    _commandPending = false;
    _lastDataFromHost = false;
}

void GSCommandLog::Frame()
{
    if (!_inTransfer || _stallReported || !_context || Skip())
        return;
    if (_context->emulatorState.frame_counter - _progressFrame < kStallFrames)
        return;
    GSCommandLogEntry e = Make(GSCommandLogKind::Anomaly, uint8_t(GSCommandLogAnomaly::TransferStalled), false);
    e.command = _transferCommand;
    e.count = _blockCount;
    Push(e);
    _stallReported = true;
}

/// endregion </Decoder>
