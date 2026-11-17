#include "stdafx.h"

#include "vdac4control.h"

#include "emulator/emulatorcontext.h"
#include "emulator/ports/models/portdecoder_tsconf.h"
#include "esp/espapphost.h"
#include "esp/espxtensaruntime.h"
#include "vdac4audio.h"
#include "vdac4board.h"
#include "vdac4esp.h"
#include "vdac4espframes.h"
#include "vdac4esplink.h"
#include "vdac4espfirmware.h"
#include "vdac4tang.h"

namespace Vdac4Control
{

namespace
{

PortDecoder_TSConf* FindDecoder(EmulatorContext* context, std::string* error)
{
    auto fail = [error](const char* text) -> PortDecoder_TSConf* {
        if (error)
            *error = text;
        return nullptr;
    };
#ifndef ENABLE_VDAC2
    (void)context;
    return fail("this build has no VDAC2 / VDAC4 support (CMake ENABLE_VDAC2=OFF)");
#else
    if (!context)
        return fail("no emulator");
    auto* decoder = dynamic_cast<PortDecoder_TSConf*>(context->pPortDecoder);
    if (!decoder)
        return fail("the machine is not TS-Conf: the VDAC4 board fits the TS-Conf IDE connector");
    if (!decoder->GetVdac4Tang() || !decoder->GetVdac4Board())
        return fail("no VDAC4 board: the TS-Conf configuration has [MISC] TS_VDAC4=0 (machine TSL-VDAC4 has it)");
    return decoder;
#endif
}

const char* PersonalityName(Vdac4Tang::Personality personality)
{
    switch (personality)
    {
        case Vdac4Tang::Personality::Ts:
            return "ts";
        case Vdac4Tang::Personality::Loading:
            return "loading";
        case Vdac4Tang::Personality::Ft:
            return "ft";
    }
    return "?";
}

const char* PhaseName(Vdac4Esp::Phase phase)
{
    switch (phase)
    {
        case Vdac4Esp::Phase::Off:
            return "off";
        case Vdac4Esp::Phase::Booting:
            return "booting";
        case Vdac4Esp::Phase::Running:
            return "running";
        case Vdac4Esp::Phase::Flushing:
            return "flushing";
        case Vdac4Esp::Phase::RestartWait:
            return "restart_wait";
    }
    return "?";
}

} // namespace

bool HasBoard(EmulatorContext* context, std::string* error)
{
    return FindDecoder(context, error) != nullptr;
}

void CaptureLive(Vdac4Board& board, const Vdac4Tang& tang, BoardState& state)
{
    const Vdac4Tang::State& tangState = tang.GetState();
    state.personality = PersonalityName(tangState.personality);
    state.armed = tang.IsArmed();
    state.espHost = tang.IsEspHost();
    state.ramG3M = tang.IsRamG3M();
    state.ftCfg = tangState.ftCfg;

    Vdac4Esp& esp = board.Esp();
    state.espPhase = PhaseName(esp.GetPhase());
    state.espMs = esp.NowMs();
    state.boots = esp.Boots();
    state.bootNonce = esp.BootNonce();
    Vdac4EspFirmware& firmware = esp.Firmware();
    state.lastStep = firmware.LastStep();
    state.lastError = firmware.LastError() ? firmware.LastError() : "";
    state.resources = firmware.ResourceCount();
    state.clockRestarts = board.ClockRestarts();
    state.clockOffsetUs = board.ClockOffsetNs() / 1000;
    state.appRunning = firmware.AppRunning();
    state.appModule = firmware.AppModule();
    const Vdac4AppInfo& info = firmware.AppInfo();
    state.appName = info.name;
    state.appVersion = info.version;
    state.commandFirst = info.commandFirst;
    state.commandLast = info.commandLast;
    state.appError = board.Runtime().Host().LastError();
    state.appSections.clear();
    if (const vdac4::EspElfImage* image = board.Runtime().Host().AppImage())
        for (const vdac4::EspElfSection& section : image->sections)
            if (section.size != 0 && section.addr != 0)
                state.appSections.push_back({section.vaddr, section.size, section.addr});

    state.pendingFromZ80 = esp.Link().PendingFromZ80();
    state.pendingToZ80 = esp.Link().PendingToZ80();
    state.sendBlocked = esp.Transport().Blocked();
    state.badChecksums = esp.Transport().Parser().BadChecksums();
    state.resyncs = esp.Transport().Parser().Resyncs();
    state.droppedFrames = esp.Transport().DroppedFrames();
}

bool GetState(EmulatorContext* context, BoardState& state, size_t logLines, std::string* error)
{
    PortDecoder_TSConf* decoder = FindDecoder(context, error);
    if (!decoder)
        return false;
    Vdac4Board& board = *decoder->GetVdac4Board();
    state = board.Published();
    state.log = board.RecentLog(logLines);
    state.recentPackets.clear();
    for (const Vdac4Board::Packet& packet : board.RecentPackets(&state.packets))
        state.recentPackets.push_back({packet.command, packet.length, packet.ms});
    const Vdac4Audio::Stats audio = board.Audio().GetStats();
    state.audioInputRate = audio.inputRate;
    state.audioOutputRate = audio.outputRate;
    state.audioQueued = audio.queued;
    state.audioUnderruns = audio.underruns;
    state.audioDropped = audio.dropped;
    state.audioSound = audio.sound;
    return true;
}

bool ReadEspMemory(EmulatorContext* context, uint32_t address, uint32_t size, bool appAddress,
                   std::vector<uint8_t>& out, std::string* error)
{
    auto fail = [error](const std::string& text) {
        if (error)
            *error = text;
        return false;
    };
    PortDecoder_TSConf* decoder = FindDecoder(context, error);
    if (!decoder)
        return false;
    if (size == 0 || size > kMaxEspRead)
        return fail("size is 1.." + std::to_string(kMaxEspRead));
    Vdac4Board& board = *decoder->GetVdac4Board();
    uint32_t at = address;
    if (appAddress)
    {
        const BoardState published = board.Published();
        if (published.appSections.empty())
            return fail("no app is running on the ESP");
        at = 0;
        for (const BoardState::AppSection& section : published.appSections)
            if (address >= section.elfAddress && address - section.elfAddress < section.size &&
                size <= section.size - (address - section.elfAddress))
                at = section.loadedAt + (address - section.elfAddress);
        if (at == 0)
            return fail("the range is not inside one section of the app's image");
    }
    // The PSRAM and SRAM buffers live as long as the board: a read beside the running ESP sees the bytes of the
    // moment, as a Z80 memory read beside the running machine does
    const uint8_t* bytes = board.Runtime().Host().Memory().Bytes(at, size);
    if (bytes == nullptr)
        return fail("the range is not ESP memory (PSRAM, SRAM)");
    out.assign(bytes, bytes + size);
    return true;
}

} // namespace Vdac4Control
