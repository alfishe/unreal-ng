#include "emulator/io/sprinter/isa/cards/isawildsoundcard.h"

#include <cstdio>

#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulatorcontext.h"
#include "emulator/platform.h"
#include "debugger/ttd/ttdserializable.h"
#include "emulator/sound/soundmanager.h"

namespace sprinterisa
{
namespace
{
std::string Hex(unsigned value, int digits)
{
    char text[16];
    std::snprintf(text, sizeof(text), "#%0*X", digits, value);
    return text;
}

const char* ModeName(wildsound::WildSoundMode mode)
{
    return mode == wildsound::WildSoundMode::Xm ? "xm" : "ay";
}

const char* CommandName(uint16_t cmd)
{
    using C = wildsound::WildSoundCore;
    switch (cmd)
    {
        case C::kCmdNop: return "NOP";
        case C::kCmdClear: return "CLEAR";
        case C::kCmdErase: return "ERASE";
        case C::kCmdXmAStart: return "XMAST (track)";
        case C::kCmdXmAData: return "XMADO";
        case C::kCmdXmBStart: return "XMBST (instruments)";
        case C::kCmdXmBData: return "XMBDO (samples)";
        case C::kCmdDigPlay: return "DIGPLAY";
        default: return "unknown";
    }
}
}  // namespace

IsaWildSoundCard::IsaWildSoundCard(EmulatorContext* context, uint32_t base) : _context(context), _base(base & 0x3FE)
{
    _core.Reset(Now());
    _resetAtMs = AbsoluteMs();
    auto& flash = _core.Storage().Flash();
    _flashTracker.Bind(flash.data(), flash.size());
    _core.Storage().SetWriteHook([this](size_t offset, size_t length) {
        if (_regionsArmed)
            _flashTracker.MarkRange(offset, length);
    });
    if (_context)
    {
        _audio = std::make_unique<wildsound::WildSoundAudio>(_context, _core);
        if (_context->pSoundManager)
        {
            _context->pSoundManager->attachSlotCard(_audio.get());
            _audioAttached = true;
        }
    }
}

IsaWildSoundCard::~IsaWildSoundCard()
{
    DetachAudio();
}

void IsaWildSoundCard::TTDRegions(std::vector<ttd::TTDDeviceRegion>& out)
{
    auto& flash = _core.Storage().Flash();
    _flashTracker.Bind(flash.data(), flash.size());
    ttd::TTDDeviceRegion r;
    r.desc.id = ttd::TTDRegionId::WildSoundFlash;
    r.desc.name = "wildsound.flash";
    r.desc.ownerType = static_cast<uint16_t>(ttd::PeripheralId::SprinterIsa);
    r.desc.ownerInstance = _regionInstance;
    r.desc.memory = flash.data();
    r.desc.bytes = static_cast<uint32_t>(flash.size());
    r.desc.pieces = _flashTracker.Pieces();
    // The parsed track and instrument tables follow the restored bytes
    r.desc.onRestored = [this]() { _core.Storage().Reparse(); };
    r.tracker = &_flashTracker;
    out.push_back(r);
}

void IsaWildSoundCard::DetachAudio()
{
    if (_audioAttached && _context && _context->pSoundManager)
        _context->pSoundManager->detachSlotCard(_audio.get());
    _audioAttached = false;
}

size_t IsaWildSoundCard::StateSize() const
{
    return wildsound::WildSoundCore::kStateSize + (_audio ? _audio->StateSize() : 0);
}

void IsaWildSoundCard::SaveState(uint8_t* dst) const
{
    _core.SaveState(dst);
    if (_audio)
        _audio->SaveState(dst + wildsound::WildSoundCore::kStateSize);
}

void IsaWildSoundCard::LoadState(const uint8_t* src)
{
    _core.LoadState(src);
    if (_audio)
        _audio->LoadState(src + wildsound::WildSoundCore::kStateSize);
}

int IsaWildSoundCard::PortOf(uint32_t address) const
{
    if ((address & 0x3FE) != _base)
        return -1;
    return static_cast<int>(address & 1);
}

uint64_t IsaWildSoundCard::Now() const
{
    if (_context && _context->pCore && _context->pCore->GetZ80())
        return _context->emulatorState.AudioTstate(_context->pCore->GetZ80()->t);
    return 0;
}

uint64_t IsaWildSoundCard::AbsoluteMs() const
{
    if (!_context || _context->config.frame == 0)
        return 0;
    const uint64_t frameUs = _context->config.frame_duration_us ? _context->config.frame_duration_us : 20000;
    const uint64_t us = _context->emulatorState.frame_counter * frameUs + Now() * frameUs / _context->config.frame;
    return us / 1000;
}

uint32_t IsaWildSoundCard::UptimeMs() const
{
    const uint64_t now = AbsoluteMs();
    return static_cast<uint32_t>(now > _resetAtMs ? now - _resetAtMs : 0);
}

bool IsaWildSoundCard::IoRead(const IsaCycle& cycle, uint8_t& value)
{
    if (cycle.aen || PortOf(cycle.address) < 0)
        return false;
    value = _core.Read(Now());
    return true;
}

bool IsaWildSoundCard::IoWrite(const IsaCycle& cycle, uint8_t value)
{
    if (cycle.aen)
        return false;
    const int port = PortOf(cycle.address);
    if (port < 0)
        return false;
    if (port == 0)
    {
        _core.SetUptimeMs(UptimeMs());
        _core.WriteAddress(value, Now());
    }
    else
    {
        _core.WriteData(value, Now());
    }
    return true;
}

bool IsaWildSoundCard::IoPeek(uint32_t address, uint8_t& value) const
{
    if (PortOf(address) < 0)
        return false;
    value = _core.Peek();
    return true;
}

void IsaWildSoundCard::SetReset(bool asserted)
{
    if (asserted == _resetHeld)
        return;
    _resetHeld = asserted;
    if (asserted)
    {
        ++_resetPulses;
        return;
    }
    // The STM32 waits for the line's release, then restarts (NVIC_SystemReset)
    _core.Reset(Now());
    _resetAtMs = AbsoluteMs();
}

bool IsaWildSoundCard::IoRange(uint32_t& first, uint32_t& last) const
{
    first = _base;
    last = _base + 1;
    return true;
}

std::string IsaWildSoundCard::DecodeNote() const
{
    return "A9-A1 against the base, mirrored every #400: base + 0 write = AY register select, base + 1 write = AY data, "
           "a read of either = AY read (the card answers with the byte it prepared after the previous access)";
}

const char* IsaWildSoundCard::RegisterName(bool io, uint32_t address, bool write) const
{
    const int port = io ? PortOf(address) : -1;
    if (port < 0)
        return "";
    if (!write)
        return "AY read";
    return port == 0 ? "AY register select" : "AY data";
}

std::string IsaWildSoundCard::SummaryNote() const
{
    const auto& track = _core.Storage().Track();
    std::string s = std::string(" -> Wild Sound (") + ModeName(_core.Mode()) + ")";
    if (_core.Mode() == wildsound::WildSoundMode::Xm && track.valid)
        s += ": XM \"" + track.name + "\", " + std::to_string(track.channels) + " channels";
    if (_resetHeld)
        s += ", RESET held";
    return s;
}

void IsaWildSoundCard::Describe(StateNode& out) const
{
    StateNode ws = StateNode::Object();
    ws["card"] = "Wild Sound (Robus): STM32F405, AYX-32 / Wild Sound II firmware 2021-04-23 (165931.23042021), two AY "
                 "and an XM player; the protocol: docs/inprogress/2026-10-09-wildsound/reference-firmware.md";
    ws["base"] = Hex(_base, 3);
    ws["mode"] = ModeName(_core.Mode());
    ws["unlocked"] = _core.Unlocked();
    ws["chip"] = _core.SelectedChip();
    ws["register"] = Hex(_core.SelectedRegister(), 2);
    ws["next_read"] = Hex(_core.Peek(), 2);
    ws["reset_held"] = _resetHeld;
    ws["reset_pulses"] = _resetPulses;

    StateNode stream = StateNode::Object();
    stream["command"] = Hex(_core.Command(), 4);
    stream["command_name"] = CommandName(_core.Command());
    stream["size"] = _core.StreamSize();
    stream["pos"] = _core.StreamPos();
    stream["fifo"] = static_cast<uint64_t>(_core.FifoSize());
    stream["packets"] = _core.Commands();
    if (!_core.LastError().empty())
        stream["error"] = _core.LastError();
    ws["stream"] = stream;

    const auto& st = _core.Storage();
    StateNode storage = StateNode::Object();
    storage["used"] = st.Used();
    storage["size"] = wildsound::WildSoundStorage::kFlashSize;
    storage["descriptors"] = st.DescriptorsUsed();
    storage["track_bytes"] = st.TrackReceived();
    storage["instruments"] = static_cast<uint64_t>(st.Instruments().size());
    storage["samples_complete"] = st.InstrumentsComplete();
    storage["sample_bytes"] = st.SamplePosition();
    ws["storage"] = storage;

    const auto& track = st.Track();
    if (track.valid)
    {
        StateNode t = StateNode::Object();
        t["name"] = track.name;
        t["channels"] = track.channels;
        t["speed"] = track.speed;
        t["bpm"] = track.bpm;
        t["frequencies"] = track.linearFrequencies ? "linear" : "amiga";
        t["song_length"] = track.songLength;
        t["restart"] = track.restart;
        t["patterns"] = static_cast<uint64_t>(track.patterns.size());
        ws["track"] = t;
    }

    StateNode chips = StateNode::Array();
    for (int c = 0; c < 2; ++c)
    {
        std::string regs;
        for (int r = 0; r < 16; ++r)
        {
            char b[4];
            std::snprintf(b, sizeof(b), "%02X", _core.ChipRegister(c, r));
            if (r)
                regs += ' ';
            regs += b;
        }
        StateNode chip = StateNode::Object();
        chip["registers"] = regs;
        chip["duty"] = Hex(_core.ChipRegister(c, 0x18), 2) + " " + Hex(_core.ChipRegister(c, 0x19), 2) + " " +
                       Hex(_core.ChipRegister(c, 0x1A), 2);
        chips.push(chip);
    }
    ws["ay"] = chips;
    out["wild_sound"] = ws;
}

}  // namespace sprinterisa
