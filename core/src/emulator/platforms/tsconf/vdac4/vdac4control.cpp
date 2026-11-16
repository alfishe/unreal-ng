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

} // namespace Vdac4Control
