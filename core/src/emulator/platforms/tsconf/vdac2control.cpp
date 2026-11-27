#include "stdafx.h"

#include "vdac2control.h"

#include <eve/eve.h>

#include <algorithm>
#include <optional>

#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/ports/models/portdecoder_tsconf.h"
#include "emulator/platforms/tsconf/vdac2card.h"


namespace Vdac2Control
{

namespace
{

EveHostCard* FindCard(EmulatorContext* context, std::string* error)
{
    auto fail = [error](const char* text) -> EveHostCard* {
        if (error)
            *error = text;
        return nullptr;
    };
#ifndef ENABLE_VDAC2
    (void)context;
    return fail("this build has no VDAC2 support (CMake ENABLE_VDAC2=OFF)");
#else
    if (!context)
        return fail("no emulator");
    auto* decoder = dynamic_cast<PortDecoder_TSConf*>(context->pPortDecoder);
    if (!decoder)
        return fail("the machine is not TS-Conf: the VDAC2 card fits the TS-Conf IDE connector");
    EveHostCard* card = decoder->GetEveCard();
    if (!card)
        return fail("no VDAC2 card: the TS-Conf configuration is not the VDAC2 build ([MISC] TS_VDAC2=1)");
    if (!card->IsReady())
        return fail("the VDAC2 card's FT812 is not available");
    return card;
#endif
}

} // namespace

bool HasCard(EmulatorContext* context, std::string* error)
{
    return FindCard(context, error) != nullptr;
}

bool StartCapture(EmulatorContext* context, const std::string& path, std::string* error)
{
    if (path.empty())
    {
        if (error)
            *error = "no file path";
        return false;
    }
    EveHostCard* card = FindCard(context, error);
    if (!card)
        return false;
    // The CPU held (Emulator::CpuHold): every plane calls this from its own thread while the machine, and on the
    // VDAC4 an ESP job hosting the FT812, run on theirs; the chip is saved and its stream opened or closed meanwhile
    std::optional<Emulator::CpuHold> hold;
    if (context->pEmulator)
        hold.emplace(*context->pEmulator, "vdac2 capture start");
    return card->StartCapture(path, error);
}

bool StopCapture(EmulatorContext* context, std::string* error)
{
    EveHostCard* card = FindCard(context, error);
    if (!card)
        return false;
    // The CPU held (Emulator::CpuHold): every plane calls this from its own thread while the machine, and on the
    // VDAC4 an ESP job hosting the FT812, run on theirs; the chip is saved and its stream opened or closed meanwhile
    std::optional<Emulator::CpuHold> hold;
    if (context->pEmulator)
        hold.emplace(*context->pEmulator, "vdac2 capture stop");
    if (!card->StopCapture())
    {
        if (error)
            *error = "no capture is running";
        return false;
    }
    return true;
}

bool GetCaptureStatus(EmulatorContext* context, CaptureStatus& status, std::string* error)
{
    EveHostCard* card = FindCard(context, error);
    if (!card)
        return false;
    const Vdac2Capture::Stats stats = card->GetCaptureStats();
    status.capturing = card->IsCapturing();
    status.path = stats.path;
    status.bytesWritten = stats.bytesWritten;
    status.selects = stats.selects;
    status.exchanges = stats.exchanges;
    status.frames = stats.frames;
    status.startClock = stats.startClock;
    status.lastClock = stats.lastClock;
    return true;
}

bool GetFrameMetrics(EmulatorContext* context, FrameMetrics& metrics, bool withLines, bool inFlight, std::string* error)
{
    EveHostCard* card = FindCard(context, error);
    if (!card)
        return false;
    // The frame in flight moves the chip to the machine's position: only on a
    // stopped machine, where nothing else drives the card
    Emulator* emulator = context->pEmulator;
    const bool stopped = !emulator || emulator->IsPaused() || !emulator->IsRunning();
    card->ReadFrameMetrics(metrics, withLines, inFlight && stopped);
    return true;
}

bool ResetHostDrawStats(EmulatorContext* context, std::string* error)
{
    EveHostCard* card = FindCard(context, error);
    if (!card)
        return false;
    if (card->Chip())
        EveResetHostDrawStats(card->Chip());
    return true;
}

bool GetPresentedFrameMetrics(EmulatorContext* context, FrameMetrics& metrics, std::string* error)
{
    EveHostCard* card = FindCard(context, error);
    if (!card)
        return false;
    if (!card->PresentedFrameMetrics(metrics))
    {
        if (error)
            *error = "the monitor does not show an FT812 picture with metrics (the Evo is shown, or measure-always is off)";
        return false;
    }
    return true;
}

bool SetLineBudgetMargin(EmulatorContext* context, uint32_t percent, std::string* error)
{
    if (percent > 50)
    {
        if (error)
            *error = "the margin is a percent from 0 to 50";
        return false;
    }
    EveHostCard* card = FindCard(context, error);
    if (!card)
        return false;
    card->SetLineBudgetMargin(percent);
    return true;
}

bool SetMeasureAlways(EmulatorContext* context, bool on, std::string* error)
{
    EveHostCard* card = FindCard(context, error);
    if (!card)
        return false;
    card->SetMeasureAlways(on);
    return true;
}

} // namespace Vdac2Control
